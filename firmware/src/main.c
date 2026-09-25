/*
 * Adhan Clock — main entry point
 *
 * Modes:
 *   Normal boot                    → prayer clock application
 *   Settings → USB File Mode (OK)  → USB Mass Storage (SD card exposed)
 *   Settings → USB File Mode (DN)  → SD card diagnostic
 *
 * USB mode ends (reboot into the clock) when the PC ejects the drive, when
 * BACK is pressed on the remote, or after 2 minutes with no PC attached.
 */

#include "config.h"
#include "app/clock_app.h"
#include "drivers/usb_msc.h"
#include "drivers/sdcard.h"
#include "drivers/hub75.h"
#include "drivers/buttons.h"
#include "drivers/ir_remote.h"
#include "ui/fonts.h"

#include "ff.h"

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include <string.h>
#include <stdio.h>
#include <strings.h>

#define USB_NO_HOST_TIMEOUT_MS   120000u

static void reboot_to_clock(void) {
    watchdog_reboot(0, 0, 0);
    while (1) tight_loop_contents();
}

static void show_lines(const char *l1, const char *l2, const char *l3,
                       colour_t c1, colour_t c2, colour_t c3) {
    hub75_clear(COL_BLACK);
    if (l1) font_draw_string(0, 1, l1, c1);
    if (l2) font_draw_string(0, 11, l2, c2);
    if (l3) font_draw_string(0, 21, l3, c3);
    hub75_present();
}

static bool back_pressed(void) {
    buttons_poll();
    ir_remote_poll();
    btn_event_t evt;
    bool back = false;
    while (buttons_get_event(&evt))
        if (evt.button == BTN_BACK) back = true;
    return back;
}

/* ── USB Mass Storage mode ──────────────────────────────────────────────── */

static void run_usb_mode(void) {
    hub75_init();
    hub75_start_refresh();

    sdcard_unmount();   /* the PC owns the filesystem now */
    if (!sdcard_is_ready() && !sdcard_init_raw()) {
        show_lines("NO SD CARD", sdcard_error_str(sdcard_get_last_error()),
                   "BACK = exit", COL_RED, COL_RED, COL_DIM_WHITE);
        while (!back_pressed()) sleep_ms(20);
        reboot_to_clock();
    }

    show_lines("USB MODE", "Eject on PC", "when done", COL_GREEN, COL_YELLOW, COL_YELLOW);
    usb_msc_init();

    uint32_t no_host_since = to_ms_since_boot(get_absolute_time());
    uint32_t last_blink = 0;
    bool blink = false;

    while (1) {
        usb_msc_task();
        uint32_t now = to_ms_since_boot(get_absolute_time());
        bool mounted = usb_msc_mounted();

        if (usb_msc_ejected()) {
            /* Let the host finish its last transactions, then leave. */
            uint32_t t0 = now;
            while (to_ms_since_boot(get_absolute_time()) - t0 < 500) usb_msc_task();
            sdcard_sync();
            reboot_to_clock();
        }
        if (mounted) {
            no_host_since = now;
        } else if (now - no_host_since >= USB_NO_HOST_TIMEOUT_MS) {
            reboot_to_clock();
        }
        if (back_pressed()) {
            sdcard_sync();
            reboot_to_clock();
        }

        if (now - last_blink >= 800) {
            last_blink = now;
            blink = !blink;
            hub75_framebuf[30][62] = blink ? COL_GREEN : COL_BLACK;
            hub75_framebuf[30][63] = mounted ? COL_GREEN : COL_RED;
            hub75_present();
        }
    }
}

/* ── SD diagnostic mode ─────────────────────────────────────────────────── */

static void wait_step(void) {
    uint32_t t0 = to_ms_since_boot(get_absolute_time());
    while (to_ms_since_boot(get_absolute_time()) - t0 < 2500) {
        if (back_pressed()) reboot_to_clock();
        sleep_ms(20);
    }
}

static uint32_t crc32_table[256];

static uint32_t crc32_update(uint32_t crc, const uint8_t *d, unsigned n) {
    if (!crc32_table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
            crc32_table[i] = c;
        }
    }
    crc = ~crc;
    while (n--) crc = crc32_table[(crc ^ *d++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/* CRC32 of a whole file (zlib/PKZIP compatible), plus one CRC per 64 KB
 * block so two passes can be compared to locate a difference.
 * first_read > 0 makes the first f_read that size (e.g. 78 = a WAV header),
 * so every later 4 KB read is misaligned exactly like audio playback. */
#define CHECK_BLOCK   65536u
#define CHECK_BLOCKS  1024u        /* files up to 64 MB */
static uint32_t file_crc32(const char *path, uint8_t *buf, unsigned bufsize,
                           unsigned first_read, uint32_t *blocks, bool *ok) {
    static FIL f;
    UINT br;
    uint32_t crc = 0, pos = 0;
    *ok = false;
    memset(blocks, 0, CHECK_BLOCKS * sizeof(uint32_t));
    if (f_open(&f, path, FA_READ) != FR_OK) return 0;
    unsigned want = first_read ? first_read : bufsize;
    while (f_read(&f, buf, want, &br) == FR_OK) {
        if (br == 0) { *ok = true; break; }
        crc = crc32_update(crc, buf, br);
        for (UINT i = 0; i < br; ) {            /* per-block CRCs by file offset */
            uint32_t blk = pos / CHECK_BLOCK;
            UINT n = CHECK_BLOCK - pos % CHECK_BLOCK;
            if (n > br - i) n = br - i;
            if (blk < CHECK_BLOCKS) blocks[blk] = crc32_update(blocks[blk], buf + i, n);
            i += n;
            pos += n;
        }
        want = bufsize;
        if (back_pressed()) break;
    }
    f_close(&f);
    return crc;
}

static void run_sd_debug_mode(void) {
    char l2[24], l3[24];
    static FIL f;          /* static: keep large FatFS objects off the stack */
    static DIR dir;
    static FILINFO fno;
    static uint8_t buf[4096];

    hub75_init();
    hub75_start_refresh();

    sdcard_unmount();
    show_lines("SD DIAG", "card init...", NULL, COL_GREEN, COL_YELLOW, 0);
    bool ok = sdcard_init_raw();
    snprintf(l2, sizeof(l2), "%s", sdcard_error_str(sdcard_get_last_error()));
    uint32_t khz = sdcard_get_spi_khz();
    snprintf(l3, sizeof(l3), "%luGB %lu.%luMHz",            /* ≤ 13 chars */
             (unsigned long)((sdcard_get_sector_count() + 1048576) / 2097152),
             (unsigned long)(khz / 1000), (unsigned long)(khz % 1000 / 100));
    show_lines("1 CARD", l2, ok ? l3 : NULL, ok ? COL_GREEN : COL_RED, COL_WHITE, COL_WHITE);
    wait_step();
    if (!ok) goto done;

    ok = sdcard_mount();
    show_lines("2 MOUNT", ok ? "FAT ok" : "no FAT volume", NULL,
               ok ? COL_GREEN : COL_RED, COL_WHITE, 0);
    wait_step();
    if (!ok) goto done;

    /* Count WAVs and remember the first one for a speed test */
    int wavs = 0;
    char first[sizeof(ADHAN_DIR) + 256] = "";
    if (f_opendir(&dir, ADHAN_DIR) == FR_OK) {
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
            const char *ext = strrchr(fno.fname, '.');
            if (!(fno.fattrib & AM_DIR) && ext && strcasecmp(ext, ".wav") == 0) {
                if (!wavs++) snprintf(first, sizeof(first), "%s/%s", ADHAN_DIR, fno.fname);
            }
        }
        f_closedir(&dir);
    }
    snprintf(l2, sizeof(l2), "%d wav files", wavs);
    show_lines("3 FILES", l2, NULL, wavs ? COL_GREEN : COL_AMBER, COL_WHITE, 0);
    wait_step();

    if (wavs && f_open(&f, first, FA_READ) == FR_OK) {
        show_lines("4 SPEED", "reading...", NULL, COL_GREEN, COL_WHITE, 0);
        uint32_t t0 = to_ms_since_boot(get_absolute_time());
        uint32_t total = 0;
        UINT br;
        bool err = false;
        while (total < 256 * 1024) {
            if (f_read(&f, buf, sizeof(buf), &br) != FR_OK) { err = true; break; }
            if (br == 0) break;
            total += br;
        }
        uint32_t dt = to_ms_since_boot(get_absolute_time()) - t0;
        f_close(&f);
        uint32_t kbps = dt ? (total / dt) : 0;   /* bytes/ms == KB/s */
        snprintf(l2, sizeof(l2), "%lu KB/s", (unsigned long)kbps);
        /* 44.1 kHz 16-bit stereo needs 176 KB/s; mono needs half */
        snprintf(l3, sizeof(l3), "%s", err ? "READ ERROR" : kbps >= 250 ? "stereo ok" :
                                       kbps >= 120 ? "use mono" : "too slow");
        show_lines("4 SPEED", l2, l3, COL_GREEN, COL_WHITE,
                   (!err && kbps >= 250) ? COL_GREEN : COL_RED);
        wait_step();
    }

    /* 5: read the selected adhan file end to end, twice, and fingerprint it.
     * Compare with a CRC32 computed on a PC (e.g. python zlib.crc32). */
    const char *sel = clock_app_selected_adhan();
    if (sel) {
        char path[sizeof(ADHAN_DIR) + 72], name[14];
        snprintf(path, sizeof(path), "%s/%s", ADHAN_DIR, sel);
        snprintf(name, sizeof(name), "%s", sel);
        static uint32_t blocks[2][CHECK_BLOCKS];
        uint32_t crc[2] = {0, 0};
        bool ok[2] = {false, false};
        /* Pass A: aligned 4 KB reads. Pass B: playback's pattern (78-byte
         * header read, then misaligned 4 KB reads). */
        for (int pass = 0; pass < 2; pass++) {
            show_lines(pass ? "5 CHECK B" : "5 CHECK A", name, "reading...",
                       COL_GREEN, COL_AMBER, COL_WHITE);
            crc[pass] = file_crc32(path, buf, sizeof(buf), pass ? 78 : 0,
                                   blocks[pass], &ok[pass]);
        }
        snprintf(l2, sizeof(l2), "A %08lX", (unsigned long)crc[0]);
        snprintf(l3, sizeof(l3), "B %08lX %s", (unsigned long)crc[1],
                 !(ok[0] && ok[1]) ? "ER" : crc[0] == crc[1] ? "ok" : "NG");
        colour_t c = (ok[0] && ok[1] && crc[0] == crc[1]) ? COL_GREEN : COL_RED;
        show_lines("5 FILE CRC32", l2, l3, COL_GREEN, c, c);

        /* Where do the passes first disagree? */
        int first = -1, count = 0;
        for (unsigned b = 0; b < CHECK_BLOCKS; b++) {
            if (blocks[0][b] != blocks[1][b]) {
                if (first < 0) first = (int)b;
                count++;
            }
        }
        while (!back_pressed()) {
            sleep_ms(20);
            if (first >= 0 && (to_ms_since_boot(get_absolute_time()) / 4000) % 2) {
                snprintf(l2, sizeof(l2), "at %lu KB", (unsigned long)first * 64u);
                snprintf(l3, sizeof(l3), "%d blocks", count);
                show_lines("FIRST DIFF", l2, l3, COL_RED, COL_WHITE, COL_WHITE);
            } else {
                snprintf(l2, sizeof(l2), "A %08lX", (unsigned long)crc[0]);
                snprintf(l3, sizeof(l3), "B %08lX %s", (unsigned long)crc[1],
                         !(ok[0] && ok[1]) ? "ER" : crc[0] == crc[1] ? "ok" : "NG");
                show_lines("5 FILE CRC32", l2, l3, COL_GREEN, c, c);
            }
        }
        reboot_to_clock();
    }

    show_lines("SD DIAG DONE", "BACK = exit", NULL, COL_GREEN, COL_DIM_WHITE, 0);
done:
    while (!back_pressed()) sleep_ms(20);
    reboot_to_clock();
}

/* ── Entry point ────────────────────────────────────────────────────────── */

int main(void) {
    stdio_init_all();

    clock_app_init();
    while (1) {
        clock_app_mode_t mode = clock_app_update();
        if (mode == CLOCK_APP_MODE_USB)      run_usb_mode();
        if (mode == CLOCK_APP_MODE_SD_DEBUG) run_sd_debug_mode();
    }
}
