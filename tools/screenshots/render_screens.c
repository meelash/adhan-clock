/*
 * Host-side screenshot renderer.
 *
 * Links the firmware's real UI code (src/ui) and prayer calculation, with a
 * stand-in for the HUB75 driver, and writes each screen's 64x32 framebuffer
 * as a binary PPM into the output directory. make_pngs.py turns those into
 * LED-matrix-style images. Run with TZ=UTC: libadhan uses localtime(), which
 * is UTC on the Pico.
 */

#include <stdio.h>
#include <string.h>
#include "drivers/hub75.h"
#include "ui/screens.h"
#include "ui/theme.h"
#include "prayer/prayer_times.h"
#include "prayer/hijri.h"
#include "app/timeutil.h"

/* ── HUB75 stand-in ─────────────────────────────────────────────────────── */
colour_t hub75_framebuf[DISPLAY_HEIGHT][DISPLAY_WIDTH];

void hub75_clear(colour_t fill) {
    for (int y = 0; y < DISPLAY_HEIGHT; y++)
        for (int x = 0; x < DISPLAY_WIDTH; x++)
            hub75_framebuf[y][x] = fill;
}

static const char *out_dir;

static void save(const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", out_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    for (int y = 0; y < DISPLAY_HEIGHT; y++)
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
            colour_t c = hub75_framebuf[y][x];
            unsigned char px[3] = { ((c >> 8) & 0xF) * 17, ((c >> 4) & 0xF) * 17, (c & 0xF) * 17 };
            fwrite(px, 1, 3, f);
        }
    fclose(f);
    printf("%s\n", name);
}

/* ── Sample day: New York, ISNA, US DST (in effect on this date) ────────── */
static prayer_day_t day;
static hijri_date_t hijri_base;

static void home(const char *name, int hour, int minute, int second, int page,
                 bool time_valid, bool location_set) {
    rtc_time_t t = { .second = second, .minute = minute, .hour = hour,
                     .day = 25, .month = 9, .year = 2026 };
    hijri_date_t h = hijri_base;
    /* The Islamic day starts at Maghrib, as in clock_app.c */
    if (hour * 60 + minute >= day.times[PRAYER_MAGHRIB].total_minutes)
        hijri_adjust(&h, 1);
    home_status_t st = { time_valid, location_set };
    screen_render_home(&t, &h, &day, &st, page);
    save(name);
}

static int at(prayer_index_t p, int delta_min) {
    return day.times[p].total_minutes + delta_min;
}

int main(int argc, char **argv) {
    out_dir = argc > 1 ? argv[1] : ".";
    theme_init();

    prayer_config_t cfg;
    prayer_config_defaults(&cfg);
    cfg.method     = CALC_ISNA;
    cfg.asr_method = ASR_STANDARD;
    cfg.latitude   = 40.7128;
    cfg.longitude  = -74.0060;
    cfg.timezone   = -4.0;          /* EST + DST */
    prayer_calc(2026, 9, 25, &cfg, &day);
    gregorian_to_hijri(2026, 9, 25, &hijri_base);

    for (int i = 0; i < PRAYER_COUNT; i++)
        fprintf(stderr, "prayer %d %02d:%02d\n", i, day.times[i].hour, day.times[i].minute);

    /* Home page 0 through the day, one per colour theme */
    int m;
    m = at(PRAYER_FAJR, 20);    home("home-dawn",      m / 60, m % 60, 0, 0, true, true);
    m = at(PRAYER_SUNRISE, 90); home("home-morning",   m / 60, m % 60, 0, 0, true, true);
    m = at(PRAYER_DHUHR, 70);   home("home-afternoon", m / 60, m % 60, 0, 0, true, true);
    m = at(PRAYER_ASR, 45);     home("home-late-afternoon", m / 60, m % 60, 0, 0, true, true);
    m = at(PRAYER_MAGHRIB, 30); home("home-evening",   m / 60, m % 60, 0, 0, true, true);
    m = at(PRAYER_ISHA, 60);    home("home-night",     m / 60, m % 60, 0, 0, true, true);
    home("home-late-night", 2, 30, 0, 0, true, true);
    m = at(PRAYER_MAGHRIB, -1); home("home-prayer-now", m / 60, m % 60, 1, 0, true, true);
    home("home-setup", 0, 0, 0, 0, false, false);

    /* Prayer time pages, at the same moment as home-morning */
    m = at(PRAYER_SUNRISE, 90);
    home("home-morning-p2", m / 60, m % 60, 0, 1, true, true);
    home("home-morning-p3", m / 60, m % 60, 0, 2, true, true);

    screen_render_adhan_playing("Maghrib", "makkah.wav", true, 400);
    save("adhan-playing");

    screen_render_settings_menu(MENU_CALC_METHOD);
    save("menu");
    screen_render_settings_menu(MENU_TEST_ADHAN);
    save("menu-2");

    rtc_time_t st = { .minute = 42, .hour = 14, .day = 25, .month = 9, .year = 2026 };
    screen_render_set_time(&st, 3, true);                 save("set-time");
    screen_render_calc_method(CALC_ISNA);                  save("calc-method");
    screen_render_asr_method(ASR_STANDARD);                save("asr-method");
    int adj[PRAYER_COUNT] = { 0, 0, 2, 0, 3, 0 };
    screen_render_adjustments(adj, PRAYER_MAGHRIB);        save("adjustments");
    screen_render_hijri_adj(0, &hijri_base);               save("hijri-adj");
    const char *files[] = { "makkah.wav", "madinah.wav", "alaqsa.wav", "mishary.wav" };
    screen_render_adhan_select(files, 4, 0, "makkah.wav"); save("adhan-file");
    screen_render_volume(191);                             save("volume");
    screen_render_brightness(0);                           save("brightness");
    screen_render_timezone(-5.0);                          save("timezone");
    screen_render_dst(DST_US, true);                       save("dst");
    screen_render_key_beep(true, true);                    save("key-beep");

    status_info_t s = {
        .gps_data = true, .gps_fix = true, .satellites = 9,
        .location_set = true, .lat = 40.71, .lon = -74.01,
        .sd_mounted = true, .wav_count = 4,
        .rtc_ok = true, .ir_code = 0x1C,
    };
    screen_render_status(&s);                              save("status");
    screen_render_usb_confirm();                           save("usb");
    return 0;
}
