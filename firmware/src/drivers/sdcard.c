/*
 * SD Card Driver — bit-banged SPI + FatFS diskio implementation
 *
 * Protocol handling follows elm-chan's reference MMC/SDC SPI driver
 * (mmc_avr_spi.c), including multi-block CMD18/CMD25 transfers, which matter
 * a lot here: every command costs card latency, and audio streaming reads
 * 4 KB at a time.
 *
 * The pins are bit-banged because the Waveshare matrix board leaves no
 * hardware-SPI-capable pin set free. SIO registers are written directly
 * (the old gpio_put()-per-edge version was ~4x slower).
 */

#include "drivers/sdcard.h"
#include "config.h"
#include "ff.h"
#include "diskio.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "hardware/clocks.h"
#include <string.h>

/* Card type flags */
#define CT_MMC3   0x01
#define CT_SDC1   0x02
#define CT_SDC2   0x04
#define CT_BLOCK  0x08  /* block addressing (SDHC/SDXC) */

#define CMD0    0
#define CMD1    1
#define CMD8    8
#define CMD9    9
#define CMD12   12
#define CMD16   16
#define CMD17   17
#define CMD18   18
#define CMD24   24
#define CMD25   25
#define CMD55   55
#define CMD58   58
#define CMD59   59
#define ACMD23  (0x80 | 23)
#define ACMD41  (0x80 | 41)

#define SCK_MASK  (1u << PIN_SD_SCK)
#define MOSI_MASK (1u << PIN_SD_MOSI)
#define CS_MASK   (1u << PIN_SD_CS)

static uint8_t  card_type;
static bool     sd_initialized;
static bool     slow_clock = true;
static uint8_t  last_error = SD_ERR_NONE;
static uint32_t sector_count;
static uint32_t fast_delay_loops = SD_SPI_FAST_DELAY;  /* set by speed_probe() */
static uint32_t measured_khz;
static uint32_t crc_errors;      /* bad data CRCs + rejected writes (probe uses it) */
static uint32_t retries;         /* every failed read/write attempt, any cause */

/* ── CRCs (SD cards send CRC16 with every data block; CMD59 makes the card
 *    check our command CRC7 and write CRC16 too) ──────────────────────── */

static uint8_t crc7(const uint8_t *d, int n) {
    uint8_t crc = 0;
    for (int i = 0; i < n; i++) {
        uint8_t b = d[i];
        for (int j = 0; j < 8; j++) {
            crc <<= 1;
            if ((b ^ crc) & 0x80) crc ^= 0x09;
            b <<= 1;
        }
    }
    return crc & 0x7F;
}

static uint16_t crc16_table[256];

static void crc16_init(void) {
    for (int i = 0; i < 256; i++) {
        uint16_t c = (uint16_t)(i << 8);
        for (int j = 0; j < 8; j++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
        crc16_table[i] = c;
    }
}

static uint16_t crc16(const uint8_t *d, unsigned n) {
    uint16_t crc = 0;
    for (unsigned i = 0; i < n; i++)
        crc = (uint16_t)((crc << 8) ^ crc16_table[(crc >> 8) ^ d[i]]);
    return crc;
}

static FATFS fatfs;
static bool  mounted;

/* ── Bit-banged SPI mode 0 ──────────────────────────────────────────────── */

static inline void fast_delay(void) {
    /* 3 cycles per iteration (SUBS + taken BNE on Cortex-M0+). A plain C
     * loop costs ~5 cycles per nop, which made the clock ~3x slower than
     * intended. */
    uint32_t n = fast_delay_loops;
    if (n) __asm volatile(".syntax unified\n"
                          "1: subs %0, #1\n\t"
                          "bne 1b\n\t"
                          ".syntax divided" : "+l"(n) : : "cc");
}

static uint8_t xchg(uint8_t tx) {
    uint32_t rx = 0;
    if (slow_clock) {
        for (int i = 7; i >= 0; i--) {
            if ((tx >> i) & 1) sio_hw->gpio_set = MOSI_MASK;
            else               sio_hw->gpio_clr = MOSI_MASK;
            busy_wait_us_32(2);
            sio_hw->gpio_set = SCK_MASK;
            busy_wait_us_32(2);
            rx = (rx << 1) | ((sio_hw->gpio_in >> PIN_SD_MISO) & 1u);
            sio_hw->gpio_clr = SCK_MASK;
        }
    } else {
        /* Commands and writes: MOSI changes every bit, and on GP0 it also
         * drives the GPS module's RX input, so give it extra setup time
         * before the rising edge. Block reads (rcvr_multi) hold MOSI high
         * and keep the full speed; commands are only a few bytes each. */
        uint32_t setup = fast_delay_loops * 4 + 8;
        for (int i = 7; i >= 0; i--) {
            if ((tx >> i) & 1) sio_hw->gpio_set = MOSI_MASK;
            else               sio_hw->gpio_clr = MOSI_MASK;
            uint32_t n = setup;
            __asm volatile(".syntax unified\n"
                           "1: subs %0, #1\n\t"
                           "bne 1b\n\t"
                           ".syntax divided" : "+l"(n) : : "cc");
            sio_hw->gpio_set = SCK_MASK;
            fast_delay();
            rx = (rx << 1) | ((sio_hw->gpio_in >> PIN_SD_MISO) & 1u);
            sio_hw->gpio_clr = SCK_MASK;
        }
    }
    return (uint8_t)rx;
}

/* Receive a block with MOSI held high (0xFF) — the hot path for audio. */
static void __not_in_flash_func(rcvr_multi)(uint8_t *buf, unsigned len) {
    sio_hw->gpio_set = MOSI_MASK;
    for (unsigned n = 0; n < len; n++) {
        uint32_t rx = 0;
        for (int i = 0; i < 8; i++) {
            fast_delay();
            sio_hw->gpio_set = SCK_MASK;
            fast_delay();
            rx = (rx << 1) | ((sio_hw->gpio_in >> PIN_SD_MISO) & 1u);
            sio_hw->gpio_clr = SCK_MASK;
        }
        buf[n] = (uint8_t)rx;
    }
}

static void xmit_multi(const uint8_t *buf, unsigned len) {
    for (unsigned n = 0; n < len; n++) xchg(buf[n]);
}

/* ── Low-level card control ─────────────────────────────────────────────── */

static bool wait_ready(uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    do {
        if (xchg(0xFF) == 0xFF) return true;
    } while (!time_reached(deadline));
    return false;
}

static void sd_deselect(void) {
    sio_hw->gpio_set = CS_MASK;
    xchg(0xFF);                       /* dummy clock: card releases MISO */
    sio_hw->gpio_clr = MOSI_MASK;     /* MOSI idles low (shared with buzzer) */
}

static bool sd_select(void) {
    sio_hw->gpio_clr = CS_MASK;
    xchg(0xFF);
    if (wait_ready(500)) return true;
    sd_deselect();
    return false;
}

static bool rcvr_datablock(uint8_t *buf, unsigned len) {
    uint8_t token;
    absolute_time_t deadline = make_timeout_time_ms(200);
    do {
        token = xchg(0xFF);
    } while (token == 0xFF && !time_reached(deadline));
    if (token != 0xFE) return false;

    if (slow_clock) {
        for (unsigned i = 0; i < len; i++) buf[i] = xchg(0xFF);
    } else {
        rcvr_multi(buf, len);
    }
    uint16_t rx_crc = (uint16_t)(xchg(0xFF) << 8);
    rx_crc |= xchg(0xFF);
    if (rx_crc != crc16(buf, len)) {   /* corrupted on the wire: retry */
        crc_errors++;
        return false;
    }
    return true;
}

static bool xmit_datablock(const uint8_t *buf, uint8_t token) {
    if (!wait_ready(500)) return false;
    xchg(token);
    if (token != 0xFD) {               /* not StopTran */
        xmit_multi(buf, 512);
        uint16_t crc = crc16(buf, 512);
        xchg((uint8_t)(crc >> 8));
        xchg((uint8_t)crc);
        uint8_t resp = xchg(0xFF);
        if ((resp & 0x1F) != 0x05) {   /* 0x0B = card saw a CRC error */
            crc_errors++;
            return false;
        }
    }
    return true;
}

static uint8_t send_cmd(uint8_t cmd, uint32_t arg) {
    if (cmd & 0x80) {                  /* ACMD<n> = CMD55 + CMD<n> */
        cmd &= 0x7F;
        uint8_t res = send_cmd(CMD55, 0);
        if (res > 1) return res;
    }

    if (cmd != CMD12) {
        sd_deselect();
        if (!sd_select()) return 0xFF;
    }

    uint8_t frame[6] = {
        (uint8_t)(0x40 | cmd), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
        (uint8_t)(arg >> 8), (uint8_t)arg, 0
    };
    frame[5] = (uint8_t)((crc7(frame, 5) << 1) | 1);   /* CRC7 + stop bit */
    for (int i = 0; i < 6; i++) xchg(frame[i]);

    if (cmd == CMD12) xchg(0xFF);      /* skip stuff byte */

    uint8_t res;
    int n = 10;
    do {
        res = xchg(0xFF);
    } while ((res & 0x80) && --n);
    return res;
}

static uint32_t read_capacity(void) {
    uint8_t csd[16];
    uint32_t count = 0;
    if (send_cmd(CMD9, 0) == 0 && rcvr_datablock(csd, 16)) {
        if ((csd[0] >> 6) == 1) {      /* CSD v2 (SDHC/SDXC) */
            uint32_t c_size = ((uint32_t)(csd[7] & 0x3F) << 16) |
                              ((uint32_t)csd[8] << 8) | csd[9];
            count = (c_size + 1) << 10;
        } else {                       /* CSD v1 */
            uint32_t c_size = ((uint32_t)(csd[6] & 0x03) << 10) |
                              ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
            uint32_t mult = ((csd[9] & 0x03) << 1) | (csd[10] >> 7);
            uint32_t read_bl_len = csd[5] & 0x0F;
            count = (c_size + 1) << (mult + 2 + read_bl_len - 9);
        }
    }
    sd_deselect();
    return count;
}

static bool speed_probe(void);

static bool card_init(void) {
    sd_initialized = false;
    card_type = 0;
    slow_clock = true;
    last_error = SD_ERR_NONE;

    /* CS must actually go high. If it reads low while driven high, the pin
     * is shorted (e.g. HUB75 'E' line grounded by the panel). */
    sio_hw->gpio_set = CS_MASK;
    busy_wait_us_32(5);
    if (!gpio_get(PIN_SD_CS)) {
        last_error = SD_ERR_CS_STUCK;
        return false;
    }

    /* ≥74 clocks with CS high to enter native mode */
    for (int i = 0; i < 10; i++) xchg(0xFF);

    uint8_t r = 0xFF;
    for (int retry = 0; retry < 10 && r != 1; retry++) {
        r = send_cmd(CMD0, 0);
        if (r != 1) {
            sd_deselect();
            for (int i = 0; i < 10; i++) xchg(0xFF);
        }
    }
    if (r != 1) {
        last_error = (r == 0xFF) ? SD_ERR_NO_RESPONSE : SD_ERR_CMD0;
        sd_deselect();
        return false;
    }

    uint8_t ty = 0;
    absolute_time_t deadline = make_timeout_time_ms(2000);

    if (send_cmd(CMD8, 0x1AA) == 1) {  /* SDv2 */
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) ocr[i] = xchg(0xFF);
        if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
            while (!time_reached(deadline) && send_cmd(ACMD41, 1UL << 30) != 0)
                sleep_ms(1);
            if (!time_reached(deadline) && send_cmd(CMD58, 0) == 0) {
                for (int i = 0; i < 4; i++) ocr[i] = xchg(0xFF);
                ty = (ocr[0] & 0x40) ? (CT_SDC2 | CT_BLOCK) : CT_SDC2;
            } else {
                last_error = SD_ERR_ACMD41;
            }
        } else {
            last_error = SD_ERR_CMD8;
        }
    } else {                           /* SDv1 or MMCv3 */
        uint8_t cmd;
        if (send_cmd(ACMD41, 0) <= 1) { ty = CT_SDC1; cmd = ACMD41; }
        else                          { ty = CT_MMC3; cmd = CMD1; }
        while (!time_reached(deadline) && send_cmd(cmd, 0) != 0)
            sleep_ms(1);
        if (time_reached(deadline) || send_cmd(CMD16, 512) != 0) {
            ty = 0;
            last_error = SD_ERR_ACMD41;
        }
    }
    sd_deselect();

    if (!ty) return false;

    /* Ask the card to verify command and write CRCs, so a garbled command
     * (e.g. a wrong sector address) is rejected instead of executed. */
    send_cmd(CMD59, 1);
    sd_deselect();

    card_type = ty;
    sd_initialized = true;
    if (!speed_probe()) {
        sd_initialized = false;
        last_error = SD_ERR_READ;
        return false;
    }
    sector_count = read_capacity();
    crc_errors = 0;          /* count only errors in normal use */
    retries = 0;
    last_error = SD_ERR_NONE;
    return true;
}

/* ── Block access ───────────────────────────────────────────────────────── */

static bool read_sectors(uint8_t *buf, uint32_t sector, uint32_t count);

/* Find a fast clock that reads correctly. CRCs are not checked, so a read
 * "succeeding" proves nothing — compare against a slow-clock reference read.
 * Long wires or extra load on a line (e.g. the GPS RX pin sharing GP0) can
 * corrupt bits at full speed. */
static void measure_clock(void) {
    /* Clock 256 dummy bytes with CS high (the card ignores them) and time it */
    static uint8_t dummy[256];
    uint64_t t0 = time_us_64();
    rcvr_multi(dummy, sizeof(dummy));
    uint32_t dt = (uint32_t)(time_us_64() - t0);
    sio_hw->gpio_clr = MOSI_MASK;
    measured_khz = dt ? (sizeof(dummy) * 8u * 1000u) / dt : 0;
}

static bool speed_probe(void) {
    static uint8_t ref[1024], ref_deep[1024], probe[1024];

    /* References at the slow clock: sectors 0-1, and two sectors mid-card
     * whose address has many 1 bits, so MOSI gets exercised too. */
    slow_clock = true;
    if (!read_sectors(ref, 0, 2)) return false;
    uint32_t deep = (read_capacity() / 2) | 0x5555u;
    if (!read_sectors(ref_deep, deep, 2)) deep = 0;
    slow_clock = false;

    /* Try 0 (fastest), 1, 2, 4 ... delay loops per half clock */
    for (uint32_t d = SD_SPI_FAST_DELAY; d <= 128; d = d ? d * 2 : 1) {
        fast_delay_loops = d;
        bool ok = true;
        uint32_t errs_before = crc_errors;
        for (int pass = 0; pass < 8 && ok; pass++) {
            ok = read_sectors(probe, 0, 2) && memcmp(probe, ref, 1024) == 0 &&
                 read_sectors(probe, 1, 1) && memcmp(probe, ref + 512, 512) == 0;
            if (ok && deep)
                ok = read_sectors(probe, deep, 2) && memcmp(probe, ref_deep, 1024) == 0 &&
                     read_sectors(probe, deep + 1, 1) && memcmp(probe, ref_deep + 512, 512) == 0;
        }
        if (crc_errors != errs_before) ok = false;   /* any wire error disqualifies */
        if (ok) {
            /* One step slower than the fastest passing speed, as margin —
             * writes use the same clock but aren't verified here. */
            fast_delay_loops = d ? d * 2 : 1;
            measure_clock();
            return true;
        }
    }
    slow_clock = true;       /* nothing faster works: stay at ~125 kHz */
    measured_khz = 125;
    return true;
}

static bool read_sectors(uint8_t *buf, uint32_t sector, uint32_t count) {
    if (!sd_initialized || count == 0) return false;
    if (!(card_type & CT_BLOCK)) sector *= 512;

    if (count == 1) {
        if (send_cmd(CMD17, sector) == 0 && rcvr_datablock(buf, 512))
            count = 0;
    } else {
        if (send_cmd(CMD18, sector) == 0) {
            do {
                if (!rcvr_datablock(buf, 512)) break;
                buf += 512;
            } while (--count);
            send_cmd(CMD12, 0);
        }
    }
    sd_deselect();
    return count == 0;
}

static bool write_sectors(const uint8_t *buf, uint32_t sector, uint32_t count) {
    if (!sd_initialized || count == 0) return false;
    if (!(card_type & CT_BLOCK)) sector *= 512;

    if (count == 1) {
        if (send_cmd(CMD24, sector) == 0 && xmit_datablock(buf, 0xFE))
            count = 0;
    } else {
        if (card_type & (CT_SDC1 | CT_SDC2)) send_cmd(ACMD23, count);
        if (send_cmd(CMD25, sector) == 0) {
            do {
                if (!xmit_datablock(buf, 0xFC)) break;
                buf += 512;
            } while (--count);
            if (!xmit_datablock(NULL, 0xFD)) count = 1;
        }
    }
    sd_deselect();
    return count == 0;
}

/* ── FatFS diskio interface ─────────────────────────────────────────────── */

DSTATUS disk_status(BYTE pdrv) {
    (void)pdrv;
    return sd_initialized ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
    (void)pdrv;
    if (sd_initialized) return 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) sleep_ms(100);
        if (card_init()) return 0;
        if (last_error == SD_ERR_CS_STUCK) break;
    }
    return STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    (void)pdrv;
    if (!sd_initialized) return RES_NOTRDY;
    /* CRC errors and rejected commands are retried */
    for (int attempt = 0; attempt < 4; attempt++) {
        if (read_sectors(buff, sector, count)) return RES_OK;
        retries++;
    }
    return RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    (void)pdrv;
    if (!sd_initialized) return RES_NOTRDY;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (write_sectors(buff, sector, count)) return RES_OK;
        retries++;
    }
    return RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    (void)pdrv;
    if (!sd_initialized) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC: {
        bool ok = sd_select();
        sd_deselect();
        return ok ? RES_OK : RES_ERROR;
    }
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = sector_count;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

DWORD get_fattime(void) {
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}

/* ── High-level API ─────────────────────────────────────────────────────── */

static void pins_init(void) {
    static bool done = false;
    if (done) return;
    done = true;
    crc16_init();

    gpio_init(PIN_SD_SCK);
    gpio_set_dir(PIN_SD_SCK, GPIO_OUT);
    gpio_put(PIN_SD_SCK, 0);

    gpio_init(PIN_SD_MOSI);
    gpio_set_dir(PIN_SD_MOSI, GPIO_OUT);
    gpio_put(PIN_SD_MOSI, 0);

    gpio_init(PIN_SD_MISO);
    gpio_set_dir(PIN_SD_MISO, GPIO_IN);
    gpio_pull_up(PIN_SD_MISO);

    gpio_init(PIN_SD_CS);
    gpio_set_dir(PIN_SD_CS, GPIO_OUT);
    gpio_put(PIN_SD_CS, 1);
}

bool sdcard_init(void) {
    pins_init();
    sd_initialized = false;        /* force a fresh card init */
    return sdcard_mount();
}

bool sdcard_init_raw(void) {
    pins_init();
    sd_initialized = false;
    return disk_initialize(0) == 0;
}

bool sdcard_is_mounted(void) { return mounted; }

void sdcard_unmount(void) {
    if (mounted) {
        f_unmount("");
        mounted = false;
    }
}

bool sdcard_mount(void) {
    FRESULT fr = f_mount(&fatfs, "", 1);
    mounted = (fr == FR_OK);
    if (!mounted && sd_initialized) last_error = SD_ERR_NO_FAT;
    return mounted;
}

FATFS *sdcard_get_fs(void) { return &fatfs; }

uint32_t sdcard_get_sector_count(void) { return sector_count; }

bool sdcard_is_ready(void) { return sd_initialized; }

bool sdcard_read_blocks(uint32_t lba, uint8_t *buffer, uint32_t count) {
    return disk_read(0, buffer, lba, count) == RES_OK;
}

bool sdcard_write_blocks(uint32_t lba, const uint8_t *buffer, uint32_t count) {
    return disk_write(0, buffer, lba, count) == RES_OK;
}

bool sdcard_sync(void) {
    return disk_ioctl(0, CTRL_SYNC, NULL) == RES_OK;
}

uint8_t sdcard_get_last_error(void) { return last_error; }

uint32_t sdcard_get_retries(void) { return retries; }

uint32_t sdcard_get_spi_khz(void) {
    if (!sd_initialized) return 0;
    return measured_khz;
}

const char *sdcard_error_str(uint8_t err) {
    switch (err) {
    case SD_ERR_NONE:        return "OK";
    case SD_ERR_NO_RESPONSE: return "no card";
    case SD_ERR_CMD0:        return "CMD0 fail";
    case SD_ERR_CMD8:        return "CMD8 fail";
    case SD_ERR_ACMD41:      return "init tmo";
    case SD_ERR_CS_STUCK:    return "CS short";
    case SD_ERR_NO_FAT:      return "no FAT";
    case SD_ERR_READ:        return "read fail";
    }
    return "?";
}
