/*
 * Screen Rendering — implementation
 *
 * Each screen composites text and graphics into the HUB75 framebuffer.
 */

#include "ui/screens.h"
#include "ui/fonts.h"
#include "ui/display.h"
#include "ui/theme.h"
#include "drivers/hub75.h"
#include "prayer/methods.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ── Helper: centre text horizontally ───────────────────────────────────── */
static void draw_small_centered(int y, const char *str, colour_t c) {
    int w = font_measure_string(str);
    int x = (DISPLAY_WIDTH - w) / 2;
    if (x < 0) x = 0;
    font_draw_string(x, y, str, c);
}

/* ── Home Screen (3-page carousel) ──────────────────────────────────────── */
/*  page 0 = clock + Hijri + next prayer
 *  page 1 = prayer times (Fajr / Sunrise / Dhuhr) with compact header
 *  page 2 = prayer times (Asr / Maghrib / Isha) with compact header
 */
void screen_render_home(const rtc_time_t *time,
                        const hijri_date_t *hijri,
                        const prayer_day_t *prayers,
                        const home_status_t *status,
                        int page)
{
    /* Determine colour palette from time of day */
    int prayer_mins[6];
    for (int i = 0; i < PRAYER_COUNT; i++)
        prayer_mins[i] = prayers->times[i].total_minutes;

    const theme_palette_t *pal = theme_get_palette(
        time->hour, time->minute, prayer_mins);

    hub75_clear(pal->bg);

    if (page == 0) {
        /* ── Page 0: big clock ── */

        /* Row 0–13: Large time display "HH:MM" with blinking colon.
         * Draw digits and colon separately so the colon is never
         * drawn then erased in the same frame (avoids dim flicker). */
        char hh[4], mm[4];
        snprintf(hh, sizeof(hh), "%02d", time->hour);
        snprintf(mm, sizeof(mm), "%02d", time->minute);

        /* Measure full "HH:MM" width to centre it */
        int tw = font_measure_large_string("00:00");
        int tx = (DISPLAY_WIDTH - tw) / 2;
        colour_t tcol = status->time_valid ? pal->time_colour : COL_RED;

        /* Draw hours */
        font_draw_large_string(tx, 0, hh, tcol);

        /* Colon: draw only on even seconds */
        int colon_x = tx + font_measure_large_string("00:") - FONT_LARGE_COLON_W;
        if (time->second % 2 == 0)
            font_draw_large(colon_x, 0, ':', tcol);

        /* Minutes start right after the (narrow) colon */
        int mm_x = colon_x + FONT_LARGE_COLON_W + 1;
        font_draw_large_string(mm_x, 0, mm, tcol);

        draw_hline(0, 14, DISPLAY_WIDTH, pal->accent);

        /* Row 15–20: Hijri date (or a setup hint) */
        if (!status->time_valid) {
            draw_small_centered(15, "SET TIME", COL_RED);
        } else {
            char hijri_str[32];
            snprintf(hijri_str, sizeof(hijri_str), "%d %s %d",
                     hijri->day, hijri_month_abbrev(hijri->month), hijri->year);
            draw_small_centered(15, hijri_str, pal->date_colour);
        }

        draw_hline(0, 22, DISPLAY_WIDTH, pal->accent);

        /* Row 23–28: Next prayer + countdown */
        int minutes_until;
        prayer_index_t next = prayer_next(prayers, time->hour, time->minute,
                                           &minutes_until);

        int hrs = minutes_until / 60;
        int mins = minutes_until % 60;

        char next_str[32];
        if (hrs > 0) {
            snprintf(next_str, sizeof(next_str), "%s -%d:%02d",
                     prayer_names[next], hrs, mins);
        } else {
            /* Keep it ≤ 13 chars (64 px): "Maghrib -53m" */
            snprintf(next_str, sizeof(next_str), "%s -%dm",
                     prayer_names[next], mins);
        }

        if (!status->location_set) {
            draw_small_centered(23, "NO LOCATION", COL_RED);
        } else if (minutes_until <= 1) {
            colour_t flash = (time->second % 2) ? pal->prayer_label : pal->bg;
            char now_str[16];
            snprintf(now_str, sizeof(now_str), "%s now", prayer_names[next]);
            draw_small_centered(23, now_str, flash);
        } else {
            draw_small_centered(23, next_str, pal->countdown_colour);
        }

        /* Page indicator dots */
        colour_t d0 = COL_WHITE, d1 = 0x333, d2 = 0x333;
        hub75_set_pixel(29, 31, d0);
        hub75_set_pixel(32, 31, d1);
        hub75_set_pixel(35, 31, d2);
    } else {
        /* ── Pages 1 & 2: prayer times with compact header ── */
        int prayer_page = page - 1; /* 0 or 1 */
        int start_prayer = prayer_page * 3;
        int now_min = time->hour * 60 + time->minute;

        /* Compact header: "HH:MM  dd Mon" */
        static const char * const mon_abbr[12] = {
            "Jan","Feb","Mar","Apr","May","Jun",
            "Jul","Aug","Sep","Oct","Nov","Dec"
        };

        /* Left: current time */
        char hdr_time[8];
        snprintf(hdr_time, sizeof(hdr_time), "%02d:%02d", time->hour, time->minute);
        font_draw_string(0, 0, hdr_time, pal->time_colour);

        /* Right: abbreviated date */
        char hdr_date[10];
        snprintf(hdr_date, sizeof(hdr_date), "%d %s",
                 time->day, mon_abbr[(time->month > 0 && time->month <= 12)
                                      ? time->month - 1 : 0]);
        int dw = font_measure_string(hdr_date);
        font_draw_string(DISPLAY_WIDTH - dw, 0, hdr_date, pal->date_colour);

        /* Separator line below header */
        draw_hline(0, 7, DISPLAY_WIDTH, pal->accent);

        /* Short prayer names */
        static const char * const short_names[6] = {
            "Fjr", "Shr", "Dhr", "Asr", "Mgh", "Ish"
        };

        /* Draw 3 prayers per page */
        for (int i = 0; i < 3; i++) {
            int pi = start_prayer + i;
            int y = 9 + i * 8; /* rows: 9, 17, 25 */

            int tm = prayers->times[pi].total_minutes;

            colour_t name_col, time_col;
            bool is_next = false;

            if (tm <= now_min) {
                name_col = 0x0444;
                time_col = 0x0444;
            } else {
                is_next = true;
                for (int j = 0; j < pi; j++) {
                    if (prayers->times[j].total_minutes > now_min) {
                        is_next = false;
                        break;
                    }
                }
                name_col = is_next ? COL_GOLD : COL_DIM_WHITE;
                time_col = is_next ? COL_WHITE : COL_DIM_WHITE;
            }

            font_draw_string(2, y, short_names[pi], name_col);

            char tstr[8];
            snprintf(tstr, sizeof(tstr), "%02d:%02d",
                     prayers->times[pi].hour, prayers->times[pi].minute);
            int tw = font_measure_string(tstr);
            font_draw_string(DISPLAY_WIDTH - tw - 2, y, tstr, time_col);

            /* Marker bar left of the next prayer (an underline under the
             * bottom row would fall off the panel) */
            if (is_next) draw_rect(0, y, 1, 5, COL_GOLD);
        }

        /* Page indicator dots */
        colour_t d0 = 0x333, d1 = 0x333, d2 = 0x333;
        if (page == 1) d1 = COL_WHITE;
        else           d2 = COL_WHITE;
        hub75_set_pixel(29, 31, d0);
        hub75_set_pixel(32, 31, d1);
        hub75_set_pixel(35, 31, d2);
    }
}

/* ── Adhan Playing Screen ───────────────────────────────────────────────── */
void screen_render_adhan_playing(const char *title, const char *file_name,
                                 bool audio_ok, uint32_t anim_ms)
{
    hub75_clear(COL_BLACK);
    draw_small_centered(2, title, COL_GOLD);
    draw_small_centered(10, "ADHAN", COL_GREEN);

    if (file_name && *file_name) {
        char display_name[13];
        strncpy(display_name, file_name, 12);
        display_name[12] = '\0';
        char *dot = strrchr(display_name, '.');
        if (dot) *dot = '\0';
        draw_small_centered(18, display_name, COL_AMBER);
    }

    if (audio_ok) {
        /* Simple animated equaliser bars */
        for (int i = 0; i < 9; i++) {
            uint32_t phase = (anim_ms / 90 + (uint32_t)i * 7u) % 12u;
            int h = (int)(phase < 6 ? phase : 12 - phase) + 1;
            draw_rect(10 + i * 5, 31 - h, 3, h, (i & 1) ? COL_TEAL : COL_GREEN);
        }
    } else {
        draw_small_centered(26, "no audio", COL_RED);
    }
}

/* ── Settings Menu Screen ───────────────────────────────────────────────── */

static const char * const settings_items[MENU_COUNT] = {
    [MENU_SET_TIME]     = "Set Time",
    [MENU_CALC_METHOD]  = "Calc Method",
    [MENU_ASR_METHOD]   = "Asr Method",
    [MENU_ADJUSTMENTS]  = "Adjustments",
    [MENU_HIJRI_ADJ]    = "Hijri Adj",
    [MENU_ADHAN_FILE]   = "Adhan File",
    [MENU_TEST_ADHAN]   = "Test Adhan",
    [MENU_VOLUME]       = "Volume",
    [MENU_BRIGHTNESS]   = "Brightness",
    [MENU_TIMEZONE]     = "Timezone",
    [MENU_DST]          = "DST",
    [MENU_KEY_BEEP]     = "Key Beep",
    [MENU_STATUS]       = "Status",
    [MENU_USB]          = "USB Drive",
};

void screen_render_settings_menu(int selected_item) {
    hub75_clear(COL_BLACK);
    draw_small_centered(0, "SETTINGS", COL_CYAN);

    const int visible = 4;
    int start = selected_item - visible / 2;
    if (start + visible > MENU_COUNT) start = MENU_COUNT - visible;
    if (start < 0) start = 0;

    int y = 8;
    for (int i = start; i < start + visible && i < MENU_COUNT; i++) {
        bool sel = (i == selected_item);
        if (sel) font_draw_string(0, y, ">", COL_CYAN);
        font_draw_string(6, y, settings_items[i], sel ? COL_WHITE : COL_DIM_WHITE);
        y += 6;
    }
}

/* ── Sub-setting screens ────────────────────────────────────────────────── */

static void draw_hint(const char *hint) {
    draw_small_centered(26, hint, COL_DIM_WHITE);
}

void screen_render_set_time(const rtc_time_t *t, int field, bool blink_on) {
    hub75_clear(COL_BLACK);
    draw_small_centered(0, "SET TIME", COL_CYAN);

    char date[16], tm[8];
    snprintf(date, sizeof(date), "%04d-%02d-%02d", t->year, t->month, t->day);
    snprintf(tm, sizeof(tm), "%02d:%02d", t->hour, t->minute);

    const int cw = FONT_SMALL_W + 1;
    int dx = (DISPLAY_WIDTH - font_measure_string(date)) / 2;
    int tx = (DISPLAY_WIDTH - font_measure_string(tm)) / 2;
    font_draw_string(dx, 9, date, COL_WHITE);
    font_draw_string(tx, 17, tm, COL_WHITE);

    /* field: 0=year 1=month 2=day 3=hour 4=minute */
    static const int start[5] = { 0, 5, 8, 0, 3 };
    static const int len[5]   = { 4, 2, 2, 2, 2 };
    int fx = (field < 3 ? dx : tx) + start[field] * cw;
    int fy = field < 3 ? 9 : 17;
    char part[5];
    memcpy(part, (field < 3 ? date : tm) + start[field], (size_t)len[field]);
    part[len[field]] = '\0';
    font_draw_string(fx, fy, part, COL_GOLD);
    if (blink_on) draw_hline(fx, fy + 6, len[field] * cw - 1, COL_GOLD);

    draw_hint(field < 4 ? "OK=next" : "OK=save");
}

void screen_render_calc_method(calc_method_t method) {
    hub75_clear(COL_BLACK);
    draw_small_centered(0, "CALC METHOD", COL_CYAN);

    int total = CALC_METHOD_COUNT;
    for (int i = -1; i <= 1; i++) {
        int idx = ((int)method + i + total) % total;
        const calc_method_params_t *mp = prayer_get_method_params((calc_method_t)idx);
        int y = 9 + (i + 1) * 7;
        if (i == 0) font_draw_string(0, y, ">", COL_CYAN);
        font_draw_string(6, y, mp->name, (i == 0) ? COL_WHITE : COL_DIM_WHITE);
    }
}

void screen_render_asr_method(asr_method_t method) {
    hub75_clear(COL_BLACK);
    draw_small_centered(0, "ASR METHOD", COL_CYAN);
    draw_small_centered(12, method == ASR_HANAFI ? "Hanafi" : "Standard", COL_WHITE);
    draw_hint("UP/DN change");
}

void screen_render_adjustments(const int adjustments[6], int cursor) {
    hub75_clear(COL_BLACK);
    draw_small_centered(0, "ADJUSTMENTS", COL_CYAN);
    char line[20];
    snprintf(line, sizeof(line), "%s %+dm", prayer_names[cursor], adjustments[cursor]);
    draw_small_centered(12, line, COL_WHITE);
    draw_hint("OK=next");
}

void screen_render_hijri_adj(int adj_days, const hijri_date_t *hijri) {
    hub75_clear(COL_BLACK);
    draw_small_centered(0, "HIJRI ADJ", COL_CYAN);
    char line[24];
    snprintf(line, sizeof(line), "%+d days", adj_days);
    draw_small_centered(9, line, COL_WHITE);
    snprintf(line, sizeof(line), "%d %s %d",
             hijri->day, hijri_month_abbrev(hijri->month), hijri->year);
    draw_small_centered(17, line, COL_GOLD);
    draw_hint("UP/DN change");
}

void screen_render_adhan_select(const char *files[], int count, int cursor,
                                const char *selected) {
    hub75_clear(COL_BLACK);
    draw_small_centered(0, "ADHAN FILE", COL_CYAN);

    if (count == 0) {
        draw_small_centered(10, "No .wav in", COL_RED);
        draw_small_centered(17, "/adhan", COL_RED);
        return;
    }
    if (cursor < 0 || cursor >= count) return;

    char display_name[13];
    strncpy(display_name, files[cursor], 12);
    display_name[12] = '\0';
    bool is_sel = selected && strcmp(files[cursor], selected) == 0;
    draw_small_centered(10, display_name, is_sel ? COL_GOLD : COL_WHITE);

    char pos[16];
    snprintf(pos, sizeof(pos), "%d/%d%s", cursor + 1, count, is_sel ? " *" : "");
    draw_small_centered(18, pos, COL_DIM_WHITE);
    draw_hint("OK=select");
}

void screen_render_brightness(int level) {
    hub75_clear(COL_BLACK);
    draw_small_centered(0, "BRIGHTNESS", COL_CYAN);

    if (level == 0) {
        draw_small_centered(12, "AUTO", COL_GREEN);
    } else {
        char line[8];
        snprintf(line, sizeof(line), "%d/16", level);
        draw_small_centered(12, line, COL_WHITE);
    }
    float pct = (level == 0) ? 0.5f : (float)level / 16.0f;
    draw_progress_bar(4, 22, 56, 3, pct, COL_GOLD, 0x111);
}

void screen_render_timezone(double tz) {
    hub75_clear(COL_BLACK);
    draw_small_centered(0, "TIMEZONE", COL_CYAN);

    char line[16];
    int total_min = (int)(tz * 60.0 + (tz >= 0 ? 0.5 : -0.5));
    int a = total_min < 0 ? -total_min : total_min;
    snprintf(line, sizeof(line), "UTC%c%d:%02d", total_min < 0 ? '-' : '+', a / 60, a % 60);
    draw_small_centered(10, line, COL_WHITE);
    draw_small_centered(18, "(standard)", COL_DIM_WHITE);
    draw_hint("UP/DN change");
}

void screen_render_dst(dst_mode_t mode, bool active_now) {
    hub75_clear(COL_BLACK);
    draw_small_centered(0, "DST", COL_CYAN);
    draw_small_centered(10, dst_mode_name(mode), COL_WHITE);
    draw_small_centered(18, active_now ? "now: +1h" : "now: off",
                        active_now ? COL_GREEN : COL_DIM_WHITE);
    draw_hint("UP/DN change");
}

void screen_render_volume(uint8_t vol) {
    hub75_clear(COL_BLACK);
    draw_small_centered(0, "VOLUME", COL_CYAN);
    char line[8];
    snprintf(line, sizeof(line), "%d%%", (vol * 100 + 127) / 255);
    draw_small_centered(12, line, COL_WHITE);
    draw_progress_bar(4, 22, 56, 3, (float)vol / 255.0f, COL_GREEN, 0x111);
}

void screen_render_key_beep(bool on, bool available) {
    hub75_clear(COL_BLACK);
    draw_small_centered(0, "KEY BEEP", COL_CYAN);
    if (available) {
        draw_small_centered(12, on ? "On" : "Off", COL_WHITE);
        draw_hint("UP/DN change");
    } else {
        /* GP27 is still SD MOSI */
        draw_small_centered(9, "Buzzer pin is", COL_AMBER);
        draw_small_centered(16, "SD MOSI", COL_AMBER);
        draw_hint("see README");
    }
}

void screen_render_status(const status_info_t *st) {
    hub75_clear(COL_BLACK);
    char line[24];

    /* GPS */
    if (!st->gps_data)
        snprintf(line, sizeof(line), "GPS no data");
    else
        snprintf(line, sizeof(line), st->gps_fix ? "GPS OK %dsat" : "GPS srch %d", st->satellites);
    font_draw_string(0, 0, line, !st->gps_data ? COL_RED : st->gps_fix ? COL_GREEN : COL_AMBER);

    /* Location */
    if (st->location_set)
        snprintf(line, sizeof(line), fabs(st->lon) >= 100 ? "%.2f,%.1f" : "%.2f,%.2f",
                 st->lat, st->lon);
    else
        snprintf(line, sizeof(line), "no location");
    font_draw_string(0, 6, line, st->location_set ? COL_WHITE : COL_RED);

    /* SD */
    if (st->sd_mounted)
        snprintf(line, sizeof(line), "SD ok %d wav", st->wav_count);
    else
        snprintf(line, sizeof(line), "SD %s", st->sd_error);
    font_draw_string(0, 12, line, st->sd_mounted ? COL_GREEN : COL_RED);

    /* RTC */
    const char *rtc = !st->rtc_ok ? "RTC error" : st->rtc_lost_power ? "RTC not set" : "RTC ok";
    font_draw_string(0, 18, rtc, (!st->rtc_ok || st->rtc_lost_power) ? COL_RED : COL_GREEN);

    /* IR last code, audio underruns, SD CRC retries (capped to fit 64 px) */
    unsigned long un  = st->underruns > 99 ? 99 : st->underruns;
    unsigned long crc = st->sd_retries > 99 ? 99 : st->sd_retries;
    snprintf(line, sizeof(line), "IR%02X un%lu c%lu", st->ir_code, un, crc);
    font_draw_string(0, 24, line, (st->underruns || st->sd_retries) ? COL_AMBER : COL_DIM_WHITE);
}

void screen_render_usb_confirm(void) {
    hub75_clear(COL_BLACK);
    draw_small_centered(0, "USB FILE MODE", COL_CYAN);
    draw_small_centered(8, "Expose SD", COL_WHITE);
    draw_small_centered(14, "to PC?", COL_WHITE);
    draw_small_centered(22, "OK=USB DN=dbg", COL_DIM_WHITE);
}

void screen_render_message(const char *l1, const char *l2, const char *l3) {
    hub75_clear(COL_BLACK);
    if (l1) draw_small_centered(4, l1, COL_AMBER);
    if (l2) draw_small_centered(12, l2, COL_WHITE);
    if (l3) draw_small_centered(20, l3, COL_DIM_WHITE);
}
