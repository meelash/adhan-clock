/*
 * Main Clock Application — implementation
 *
 * Central state machine on Core 0 (display refresh runs on Core 1).
 *
 * Timekeeping: the DS3231 holds UTC. Local time = UTC + timezone + DST (rule
 * from settings), recomputed continuously, so DST switches by itself and
 * changing the timezone never corrupts the stored time. GPS, when it has a
 * fix, corrects the RTC and (optionally) the location.
 */

#include "app/clock_app.h"
#include "app/settings.h"
#include "app/adhan.h"
#include "app/timeutil.h"
#include "config.h"

#include "drivers/hub75.h"
#include "drivers/ds3231.h"
#include "drivers/gps.h"
#include "drivers/audio_i2s.h"
#include "drivers/sdcard.h"
#include "drivers/buttons.h"
#include "drivers/light_sensor.h"
#include "drivers/ir_remote.h"
#include "drivers/buzzer.h"

#include "prayer/prayer_times.h"
#include "prayer/methods.h"
#include "prayer/hijri.h"

#include "ui/screens.h"
#include "ui/theme.h"
#include "ui/fonts.h"

#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define MENU_IDLE_TIMEOUT_MS   60000u
#define GPS_RESYNC_MS          600000u   /* re-check RTC against GPS every 10 min */
#define NO_AUDIO_ALERT_MS      60000u    /* silent adhan alert duration */
#define HOME_PAGE_HOLD_MS      15000u
#define DEFAULT_EPOCH          1767225600 /* 2026-01-01 00:00 UTC */

/* ── State ──────────────────────────────────────────────────────────────── */

static settings_t      settings;
static bool            settings_dirty;
static bool            sd_ok;

/* Time */
static epoch_t    now_utc;
static int32_t    utc_off;
static rtc_time_t local;              /* current local time */
static bool       rtc_ok;
static bool       rtc_lost_power;
static bool       time_valid;
static epoch_t    sw_epoch;           /* software clock anchor (UTC) ... */
static uint32_t   sw_ms;              /* ... at this ms-since-boot */
static bool       gps_synced;
static uint32_t   last_gps_sync_ms;

/* Prayer times */
static prayer_config_t prayer_cfg;
static prayer_day_t    today_prayers;
static int64_t         prayers_day = INT64_MIN;
static int32_t         prayers_off;
static bool            prayers_dirty = true;
static hijri_date_t    hijri_date;

/* UI */
static screen_id_t screen = SCREEN_HOME;
static int         menu_cursor;
static int         adj_cursor;
static int         adhan_cursor;
static int         time_field;
static rtc_time_t  edit_time;
static uint32_t    last_input_ms;
static int         home_page_held = -1;
static uint32_t    home_hold_until;
static uint32_t    last_brightness_ms;
static char        msg_lines[3][20];
static uint32_t    msg_until;
static screen_id_t msg_return = SCREEN_HOME;

/* Adhan */
static int64_t     last_checked_min = INT64_MIN;
static const char *adhan_title;
static char        adhan_file_playing[64];
static bool        adhan_audio_ok;
static uint32_t    adhan_started_ms;
static screen_id_t adhan_return;

static clock_app_mode_t requested_mode = CLOCK_APP_MODE_CLOCK;

static inline uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

/* ── Settings helpers ───────────────────────────────────────────────────── */

static void save_settings_now(void) {
    if (sd_ok && settings_save(&settings)) settings_dirty = false;
}

static void show_message(const char *l1, const char *l2, const char *l3,
                         screen_id_t ret, uint32_t ms) {
    snprintf(msg_lines[0], sizeof(msg_lines[0]), "%s", l1 ? l1 : "");
    snprintf(msg_lines[1], sizeof(msg_lines[1]), "%s", l2 ? l2 : "");
    snprintf(msg_lines[2], sizeof(msg_lines[2]), "%s", l3 ? l3 : "");
    msg_return = ret;
    msg_until = now_ms() + ms;
    screen = SCREEN_MESSAGE;
}

/* ── Time ───────────────────────────────────────────────────────────────── */

static bool rtc_plausible(const rtc_time_t *r) {
    return r->year >= 2000 && r->year <= 2099 &&
           r->month >= 1 && r->month <= 12 &&
           r->day >= 1 && r->day <= days_in_month(r->year, r->month) &&
           r->hour < 24 && r->minute < 60 && r->second < 60;
}

static void set_utc_time(epoch_t e) {
    rtc_time_t r;
    epoch_to_rtc(e, &r);
    if (ds3231_write_time(&r)) rtc_lost_power = false;
    sw_epoch = e;
    sw_ms = now_ms();
    time_valid = true;
}

static void update_time(void) {
    uint32_t ms = now_ms();
    rtc_time_t r;
    if (ds3231_read_time(&r) && rtc_plausible(&r)) {
        rtc_ok = true;
        sw_epoch = rtc_to_epoch(&r);
        sw_ms = ms;
    } else {
        rtc_ok = false;
    }
    now_utc = sw_epoch + (epoch_t)((ms - sw_ms) / 1000);
    if (rtc_ok) time_valid = !rtc_lost_power || gps_synced;

    utc_off = utc_offset_seconds(now_utc, settings.timezone, settings.dst);
    epoch_to_rtc(now_utc + utc_off, &local);
}

/* ── Prayer times + Hijri ───────────────────────────────────────────────── */

static void rebuild_prayer_config(void) {
    prayer_cfg.method            = settings.calc_method;
    prayer_cfg.asr_method        = settings.asr_method;
    prayer_cfg.high_lat          = settings.high_lat;
    prayer_cfg.latitude          = settings.latitude;
    prayer_cfg.longitude         = settings.longitude;
    prayer_cfg.elevation         = settings.elevation;
    prayer_cfg.timezone          = utc_off / 3600.0;   /* includes DST */
    prayer_cfg.custom_fajr_angle = settings.custom_fajr_angle;
    prayer_cfg.custom_isha_angle = settings.custom_isha_angle;
    for (int i = 0; i < PRAYER_COUNT; i++)
        prayer_cfg.adjustments[i] = settings.adjustments[i];
}

static void recalc_hijri(void) {
    gregorian_to_hijri(local.year, local.month, local.day, &hijri_date);
    /* The Islamic day starts at Maghrib */
    int now_min = local.hour * 60 + local.minute;
    if (now_min >= today_prayers.times[PRAYER_MAGHRIB].total_minutes)
        hijri_adjust(&hijri_date, 1);
    if (settings.hijri_adjust != 0)
        hijri_adjust(&hijri_date, settings.hijri_adjust);
}

static void update_prayers(void) {
    int64_t day = days_from_civil(local.year, local.month, local.day);
    if (prayers_dirty || day != prayers_day || utc_off != prayers_off) {
        rebuild_prayer_config();
        prayer_calc(local.year, local.month, local.day, &prayer_cfg, &today_prayers);
        prayers_day   = day;
        prayers_off   = utc_off;
        prayers_dirty = false;
    }
    recalc_hijri();
}

/* ── GPS ────────────────────────────────────────────────────────────────── */

static void update_gps(void) {
    const gps_data_t *g = gps_get_data();
    uint32_t ms = now_ms();

    /* Time: first valid fix after boot, then every GPS_RESYNC_MS. */
    if (g->fix_valid && g->time_valid) {
        uint32_t age = ms - g->time_ms;
        if (age < 1500 && (!gps_synced || ms - last_gps_sync_ms >= GPS_RESYNC_MS)) {
            epoch_t ge = days_from_civil(g->year, g->month, g->day) * 86400
                       + g->hour * 3600 + g->minute * 60 + g->second
                       + (epoch_t)((age + 500) / 1000);
            epoch_t diff = ge - now_utc;
            if (!gps_synced || diff >= 2 || diff <= -2 || rtc_lost_power || !rtc_ok)
                set_utc_time(ge);
            gps_synced = true;
            last_gps_sync_ms = ms;
        }
    }

    /* Location: only when it actually moved (~1 km), to avoid SD writes. */
    if (settings.gps_location && g->fix_valid) {
        if (!settings.location_set ||
            fabs(g->latitude - settings.latitude) > 0.01 ||
            fabs(g->longitude - settings.longitude) > 0.01) {
            settings.latitude     = g->latitude;
            settings.longitude    = g->longitude;
            settings.elevation    = g->altitude_m;
            settings.location_set = true;
            if (!settings.tz_set) {
                /* First-ever fix and no timezone configured: rough guess
                 * from longitude. The user can correct it in the menu. */
                settings.timezone = round(g->longitude / 15.0);
                settings.tz_set   = true;
            }
            prayers_dirty = true;
            save_settings_now();
        }
    }
}

/* ── Adhan ──────────────────────────────────────────────────────────────── */

static const char *pick_adhan_file(int prayer) {
    if (prayer == PRAYER_FAJR && settings.fajr_adhan_file[0] &&
        adhan_find_file(settings.fajr_adhan_file) >= 0)
        return settings.fajr_adhan_file;
    if (settings.adhan_file[0] && adhan_find_file(settings.adhan_file) >= 0)
        return settings.adhan_file;
    return adhan_get_filename(0);   /* first file on the card, or NULL */
}

static void start_adhan(const char *title, const char *file, screen_id_t ret) {
    adhan_title = title;
    adhan_audio_ok = false;
    adhan_file_playing[0] = '\0';

    if (file) {
        snprintf(adhan_file_playing, sizeof(adhan_file_playing), "%s", file);
        audio_error_t err = adhan_play_by_name(file);
        adhan_audio_ok = (err == AUDIO_OK);
        if (!adhan_audio_ok && ret != SCREEN_HOME) {
            /* Test from the menu: say why, don't show a silent alert */
            show_message("Can't play", file, audio_error_str(err), ret, 4000);
            return;
        }
    }
    adhan_return = ret;
    adhan_started_ms = now_ms();
    screen = SCREEN_ADHAN_PLAYING;
}

static void check_adhan_trigger(void) {
    int64_t cur_min = (now_utc + utc_off) / 60;   /* local minutes since epoch */
    if (last_checked_min == INT64_MIN) { last_checked_min = cur_min; return; }
    if (cur_min == last_checked_min) return;

    int64_t from  = last_checked_min;
    int64_t delta = cur_min - from;
    last_checked_min = cur_min;

    /* Only fire on normal clock progress — not when the time was just set,
     * corrected by GPS, or shifted by DST. */
    if (delta < 1 || delta > 3) return;
    if (!time_valid || !settings.location_set) return;
    if (screen == SCREEN_ADHAN_PLAYING) return;

    for (int64_t m = from + 1; m <= cur_min; m++) {
        int mod = (int)(((m % 1440) + 1440) % 1440);
        for (int i = 0; i < PRAYER_COUNT; i++) {
            if (i == PRAYER_SUNRISE) continue;
            if (!(settings.adhan_enabled & (1u << i))) continue;
            if (today_prayers.times[i].total_minutes == mod) {
                if (screen != SCREEN_HOME && settings_dirty) save_settings_now();
                start_adhan(prayer_names[i], pick_adhan_file(i), SCREEN_HOME);
                return;
            }
        }
    }
}

/* ── Input ──────────────────────────────────────────────────────────────── */

static void leave_settings(void) {
    if (settings_dirty) save_settings_now();
    screen = SCREEN_HOME;
}

static void enter_menu_item(int item) {
    switch (item) {
    case MENU_SET_TIME:
        edit_time = local;
        edit_time.second = 0;
        time_field = 0;
        screen = SCREEN_SET_TIME;
        break;
    case MENU_CALC_METHOD: screen = SCREEN_SET_CALC_METHOD; break;
    case MENU_ASR_METHOD:  screen = SCREEN_SET_ASR_METHOD;  break;
    case MENU_ADJUSTMENTS: adj_cursor = 0; screen = SCREEN_SET_ADJUSTMENTS; break;
    case MENU_HIJRI_ADJ:   screen = SCREEN_SET_HIJRI_ADJ;   break;
    case MENU_ADHAN_FILE:
        if (sd_ok) adhan_scan_files();
        adhan_cursor = adhan_find_file(settings.adhan_file);
        if (adhan_cursor < 0) adhan_cursor = 0;
        screen = SCREEN_SET_ADHAN_SEL;
        break;
    case MENU_TEST_ADHAN: {
        const char *f = pick_adhan_file(-1);
        if (!f) show_message("No adhan", "files found", "in /adhan", SCREEN_SETTINGS_MENU, 4000);
        else    start_adhan("Test", f, SCREEN_SETTINGS_MENU);
        break;
    }
    case MENU_VOLUME:      screen = SCREEN_SET_VOLUME;      break;
    case MENU_BRIGHTNESS:  screen = SCREEN_SET_BRIGHTNESS;  break;
    case MENU_TIMEZONE:    screen = SCREEN_SET_TIMEZONE;    break;
    case MENU_DST:         screen = SCREEN_SET_DST;         break;
    case MENU_KEY_BEEP:    screen = SCREEN_SET_KEY_BEEP;    break;
    case MENU_STATUS:      screen = SCREEN_STATUS;          break;
    case MENU_USB:         screen = SCREEN_USB_CONFIRM;     break;
    }
}

static int wrap(int v, int lo, int hi) {
    if (v < lo) return hi;
    if (v > hi) return lo;
    return v;
}

static void adjust_time_field(int d) {
    rtc_time_t *t = &edit_time;
    switch (time_field) {
    case 0: t->year   = (uint16_t)wrap(t->year + d, 2024, 2099); break;
    case 1: t->month  = (uint8_t)wrap(t->month + d, 1, 12); break;
    case 2: t->day    = (uint8_t)wrap(t->day + d, 1, days_in_month(t->year, t->month)); break;
    case 3: t->hour   = (uint8_t)wrap(t->hour + d, 0, 23); break;
    case 4: t->minute = (uint8_t)wrap(t->minute + d, 0, 59); break;
    }
    int dim = days_in_month(t->year, t->month);
    if (t->day > dim) t->day = (uint8_t)dim;
}

static void commit_edit_time(void) {
    epoch_t local_e = rtc_to_epoch(&edit_time);
    /* Offset at that moment (DST may differ from now) */
    epoch_t guess = local_e - (epoch_t)(settings.timezone * 3600.0);
    int32_t off = utc_offset_seconds(guess, settings.timezone, settings.dst);
    set_utc_time(local_e - off);
    last_checked_min = INT64_MIN;
    update_time();
    prayers_dirty = true;
}

static void handle_input(void) {
    btn_event_t evt;
    while (buttons_get_event(&evt)) {
        last_input_ms = now_ms();
        bool up   = evt.button == BTN_UP;
        bool down = evt.button == BTN_DOWN;
        bool ok   = evt.button == BTN_OK   && evt.event == BTN_EVT_PRESS;
        bool back = evt.button == BTN_BACK && evt.event == BTN_EVT_PRESS;
        int  step = up ? 1 : down ? -1 : 0;

        if (settings.key_beep && evt.event == BTN_EVT_PRESS)
            buzzer_beep(BUZZER_CLICK_MS);

        /* Any key silences the adhan */
        if (screen == SCREEN_ADHAN_PLAYING) {
            adhan_stop();
            screen = adhan_return;
            continue;
        }
        if (screen == SCREEN_MESSAGE) {
            screen = msg_return;
            continue;
        }

        switch (screen) {
        case SCREEN_HOME:
            if (ok || back) {
                menu_cursor = 0;
                screen = SCREEN_SETTINGS_MENU;
            } else if (step) {
                int cur = home_page_held >= 0 ? home_page_held
                        : (local.hour * 3600 + local.minute * 60 + local.second) / 5 % 3;
                home_page_held = (cur - step + 3) % 3;
                home_hold_until = now_ms() + HOME_PAGE_HOLD_MS;
            }
            break;

        case SCREEN_SETTINGS_MENU:
            if (step) menu_cursor = wrap(menu_cursor - step, 0, MENU_COUNT - 1);
            if (ok)   enter_menu_item(menu_cursor);
            if (back) leave_settings();
            break;

        case SCREEN_SET_TIME:
            if (step) adjust_time_field(step);
            if (ok) {
                if (++time_field > 4) {
                    commit_edit_time();
                    show_message("Time set", NULL, NULL, SCREEN_SETTINGS_MENU, 1500);
                }
            }
            if (back) {
                if (time_field > 0) time_field--;
                else screen = SCREEN_SETTINGS_MENU;
            }
            break;

        case SCREEN_SET_CALC_METHOD:
            if (step) {
                settings.calc_method = (calc_method_t)wrap((int)settings.calc_method - step,
                                                           0, CALC_METHOD_COUNT - 1);
                prayers_dirty = settings_dirty = true;
            }
            if (ok || back) screen = SCREEN_SETTINGS_MENU;
            break;

        case SCREEN_SET_ASR_METHOD:
            if (step && evt.event == BTN_EVT_PRESS) {
                settings.asr_method = (settings.asr_method == ASR_HANAFI) ? ASR_STANDARD : ASR_HANAFI;
                prayers_dirty = settings_dirty = true;
            }
            if (ok || back) screen = SCREEN_SETTINGS_MENU;
            break;

        case SCREEN_SET_ADJUSTMENTS:
            if (step) {
                int v = settings.adjustments[adj_cursor] + step;
                if (v >= -30 && v <= 30) settings.adjustments[adj_cursor] = v;
                prayers_dirty = settings_dirty = true;
            }
            if (ok && ++adj_cursor >= PRAYER_COUNT) screen = SCREEN_SETTINGS_MENU;
            if (back) screen = SCREEN_SETTINGS_MENU;
            break;

        case SCREEN_SET_HIJRI_ADJ:
            if (step && evt.event == BTN_EVT_PRESS) {
                int v = settings.hijri_adjust + step;
                if (v >= -3 && v <= 3) settings.hijri_adjust = v;
                settings_dirty = true;
                recalc_hijri();
            }
            if (ok || back) screen = SCREEN_SETTINGS_MENU;
            break;

        case SCREEN_SET_ADHAN_SEL: {
            int count = adhan_get_count();
            if (step && count > 0) adhan_cursor = wrap(adhan_cursor - step, 0, count - 1);
            if (ok) {
                const char *name = adhan_get_filename(adhan_cursor);
                if (name) {
                    snprintf(settings.adhan_file, sizeof(settings.adhan_file), "%s", name);
                    settings_dirty = true;
                }
                screen = SCREEN_SETTINGS_MENU;
            }
            if (back) screen = SCREEN_SETTINGS_MENU;
            break;
        }

        case SCREEN_SET_VOLUME:
            if (step) {
                int v = settings.volume + step * 13;   /* ~5% steps */
                settings.volume = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                audio_set_volume(settings.volume);
                settings_dirty = true;
            }
            if (ok || back) screen = SCREEN_SETTINGS_MENU;
            break;

        case SCREEN_SET_BRIGHTNESS:
            if (step) {
                int v = settings.brightness + step;
                if (v >= 0 && v <= BRIGHTNESS_LEVELS) settings.brightness = (uint8_t)v;
                if (settings.brightness) hub75_set_brightness(settings.brightness);
                settings_dirty = true;
            }
            if (ok || back) screen = SCREEN_SETTINGS_MENU;
            break;

        case SCREEN_SET_TIMEZONE:
            if (step) {
                double tz = settings.timezone + step * 0.25;
                if (tz >= -12.0 && tz <= 14.0) settings.timezone = tz;
                settings.tz_set = true;
                prayers_dirty = settings_dirty = true;
                last_checked_min = INT64_MIN;
            }
            if (ok || back) screen = SCREEN_SETTINGS_MENU;
            break;

        case SCREEN_SET_DST:
            if (step && evt.event == BTN_EVT_PRESS) {
                settings.dst = (dst_mode_t)wrap((int)settings.dst + step, 0, DST_MODE_COUNT - 1);
                prayers_dirty = settings_dirty = true;
                last_checked_min = INT64_MIN;
            }
            if (ok || back) screen = SCREEN_SETTINGS_MENU;
            break;

        case SCREEN_SET_KEY_BEEP:
            if (step && evt.event == BTN_EVT_PRESS && buzzer_available()) {
                settings.key_beep = !settings.key_beep;
                settings_dirty = true;
            }
            if (ok || back) screen = SCREEN_SETTINGS_MENU;
            break;

        case SCREEN_STATUS:
            if (ok || back) screen = SCREEN_SETTINGS_MENU;
            break;

        case SCREEN_USB_CONFIRM:
            if (ok) {
                save_settings_now();
                requested_mode = CLOCK_APP_MODE_USB;
                return;
            }
            if (down && evt.event == BTN_EVT_PRESS) {
                save_settings_now();
                requested_mode = CLOCK_APP_MODE_SD_DEBUG;
                return;
            }
            if (back) screen = SCREEN_SETTINGS_MENU;
            break;

        default:
            screen = SCREEN_HOME;
            break;
        }
    }
}

/* ── Rendering ──────────────────────────────────────────────────────────── */

static void render_current_screen(void) {
    uint32_t ms = now_ms();

    switch (screen) {
    case SCREEN_HOME: {
        int page;
        if (home_page_held >= 0 && (int32_t)(home_hold_until - ms) > 0) {
            page = home_page_held;
        } else {
            home_page_held = -1;
            page = (local.hour * 3600 + local.minute * 60 + local.second) / 5 % 3;
        }
        home_status_t st = { .time_valid = time_valid, .location_set = settings.location_set };
        screen_render_home(&local, &hijri_date, &today_prayers, &st, page);
        break;
    }

    case SCREEN_ADHAN_PLAYING:
        screen_render_adhan_playing(adhan_title, adhan_file_playing, adhan_audio_ok,
                                    ms - adhan_started_ms);
        if (adhan_audio_ok ? !adhan_is_playing()
                           : (ms - adhan_started_ms) > NO_AUDIO_ALERT_MS)
            screen = adhan_return;
        break;

    case SCREEN_SETTINGS_MENU:   screen_render_settings_menu(menu_cursor); break;
    case SCREEN_SET_TIME:        screen_render_set_time(&edit_time, time_field, (ms / 400) & 1); break;
    case SCREEN_SET_CALC_METHOD: screen_render_calc_method(settings.calc_method); break;
    case SCREEN_SET_ASR_METHOD:  screen_render_asr_method(settings.asr_method); break;
    case SCREEN_SET_ADJUSTMENTS: screen_render_adjustments(settings.adjustments, adj_cursor); break;
    case SCREEN_SET_HIJRI_ADJ:   screen_render_hijri_adj(settings.hijri_adjust, &hijri_date); break;
    case SCREEN_SET_ADHAN_SEL:
        screen_render_adhan_select(adhan_get_filenames(), adhan_get_count(),
                                   adhan_cursor, settings.adhan_file);
        break;
    case SCREEN_SET_VOLUME:      screen_render_volume(settings.volume); break;
    case SCREEN_SET_BRIGHTNESS:  screen_render_brightness(settings.brightness); break;
    case SCREEN_SET_TIMEZONE:    screen_render_timezone(settings.timezone); break;
    case SCREEN_SET_DST:
        screen_render_dst(settings.dst, dst_active(now_utc, settings.timezone, settings.dst));
        break;
    case SCREEN_SET_KEY_BEEP:
        screen_render_key_beep(settings.key_beep, buzzer_available());
        break;

    case SCREEN_STATUS: {
        const gps_data_t *g = gps_get_data();
        status_info_t st = {
            .gps_data       = g->bytes > 0,
            .gps_fix        = g->fix_valid,
            .satellites     = g->satellites,
            .location_set   = settings.location_set,
            .lat            = settings.latitude,
            .lon            = settings.longitude,
            .sd_mounted     = sdcard_is_mounted(),
            .sd_error       = sdcard_error_str(sdcard_get_last_error()),
            .wav_count      = adhan_get_count(),
            .rtc_ok         = rtc_ok,
            .rtc_lost_power = rtc_lost_power,
            .ir_code        = ir_remote_last_code(),
            .underruns      = audio_get_underruns(),
            .sd_retries     = sdcard_get_retries(),
        };
        screen_render_status(&st);
        break;
    }

    case SCREEN_USB_CONFIRM:     screen_render_usb_confirm(); break;

    case SCREEN_MESSAGE:
        screen_render_message(msg_lines[0], msg_lines[1], msg_lines[2]);
        if ((int32_t)(ms - msg_until) >= 0) screen = msg_return;
        break;

    default:
        screen = SCREEN_HOME;
        break;
    }
    hub75_present();
}

/* ── Public API ─────────────────────────────────────────────────────────── */

const char *clock_app_selected_adhan(void) {
    return pick_adhan_file(-1);
}

void clock_app_init(void) {
    /* Display first, so there is feedback even if other hardware is missing */
    hub75_init();
    hub75_set_brightness(8);
    hub75_start_refresh();
    font_draw_string(12, 13, "Starting", COL_GREEN);
    hub75_present();

    ds3231_init();
    gps_init();
    buttons_init();
    ir_remote_init();
    light_sensor_init();
    buzzer_init();          /* holds the buzzer off; no-op if pin is SD MOSI */
    audio_init();
    theme_init();

    /* SD card + settings */
    bool migrated = false;
    double v1_off_h = 0;
    sd_ok = sdcard_init();
    if (sd_ok) {
        bool found = settings_load(&settings, &migrated, &v1_off_h);
        adhan_scan_files();
        if (!found) settings_save(&settings);   /* leave a template to edit */
    } else {
        settings_defaults(&settings);
    }
    hub75_set_brightness(settings.brightness ? settings.brightness : 8);
    audio_set_volume(settings.volume);

    /* RTC */
    rtc_time_t r;
    sw_epoch = DEFAULT_EPOCH;
    sw_ms = now_ms();
    if (ds3231_read_time(&r) && rtc_plausible(&r)) {
        rtc_ok = true;
        rtc_lost_power = ds3231_lost_power();
        sw_epoch = rtc_to_epoch(&r);
        if (migrated && !rtc_lost_power) {
            /* Old firmware kept local time in the RTC — convert to UTC once. */
            set_utc_time(sw_epoch - (epoch_t)(v1_off_h * 3600.0));
        }
    }
    time_valid = rtc_ok && !rtc_lost_power;

    update_time();
    update_prayers();
    last_brightness_ms = now_ms();

    if (!sd_ok) {
        show_message("NO SD CARD", sdcard_error_str(sdcard_get_last_error()),
                     "no adhan", SCREEN_HOME, 4000);
    }
}

clock_app_mode_t clock_app_update(void) {
    uint32_t ms = now_ms();

    gps_poll();
    buttons_poll();
    ir_remote_poll();
    handle_input();
    if (requested_mode != CLOCK_APP_MODE_CLOCK) return requested_mode;

    audio_poll();
    buzzer_poll();

    update_time();
    update_gps();
    update_prayers();
    check_adhan_trigger();
    audio_poll();

    /* Leave settings after a minute of inactivity. Use a fresh timestamp and
     * a signed difference: handle_input() stamps last_input_ms *after* `ms`
     * was taken, so `ms - last_input_ms` could wrap to ~4e9 and exit the
     * menu on a random key press. */
    if (screen != SCREEN_HOME && screen != SCREEN_ADHAN_PLAYING &&
        screen != SCREEN_MESSAGE &&
        (int32_t)(now_ms() - last_input_ms) > (int32_t)MENU_IDLE_TIMEOUT_MS)
        leave_settings();

    if (ms - last_brightness_ms >= 2000) {
        last_brightness_ms = ms;
        if (settings.brightness == BRIGHTNESS_AUTO)
            hub75_set_brightness(light_sensor_to_brightness());
    }

    render_current_screen();
    audio_poll();
    sleep_ms(10);
    return CLOCK_APP_MODE_CLOCK;
}
