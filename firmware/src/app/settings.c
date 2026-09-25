/*
 * Settings — implementation
 *
 * settings.txt format: one "key = value" per line, '#' starts a comment.
 * Unknown keys are ignored, missing keys keep their defaults, so the file can
 * be hand-edited and survives firmware upgrades.
 */

#include "app/settings.h"
#include "config.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>

static const char * const method_keys[CALC_METHOD_COUNT] = {
    "karachi", "mwl", "isna", "egypt", "ummalqura", "tehran",
    "gulf", "kuwait", "qatar", "singapore", "turkey", "custom",
};

static const char * const dst_keys[DST_MODE_COUNT] = { "off", "on", "us", "eu" };
static const char * const high_lat_keys[] = { "middle", "seventh", "angle" };
static const char * const prayer_keys[PRAYER_COUNT] = {
    "fajr", "sunrise", "dhuhr", "asr", "maghrib", "isha"
};

const char *settings_method_key(calc_method_t m) {
    return (m < CALC_METHOD_COUNT) ? method_keys[m] : "?";
}

void settings_defaults(settings_t *s) {
    memset(s, 0, sizeof(*s));
    s->calc_method       = DEFAULT_CALC_METHOD;
    s->asr_method        = DEFAULT_ASR_METHOD;
    s->high_lat          = HL_MIDDLE_OF_NIGHT;
    s->custom_fajr_angle = 18.0;
    s->custom_isha_angle = 18.0;
    s->latitude          = DEFAULT_LATITUDE;
    s->longitude         = DEFAULT_LONGITUDE;
    s->elevation         = DEFAULT_ELEVATION;
    s->gps_location      = true;
    s->timezone          = DEFAULT_TIMEZONE;
    s->dst               = DST_OFF;
    s->brightness        = BRIGHTNESS_AUTO;
    s->key_beep          = true;
    s->volume            = 200;
    s->adhan_enabled     = ADHAN_ENABLE_ALL;
}

/* ── Legacy binary format (firmware before Sept 2026) ───────────────────── */
typedef struct {
    uint32_t          magic;
    uint8_t           version;
    calc_method_t     calc_method;
    asr_method_t      asr_method;
    high_lat_method_t high_lat;
    int               adjustments[6];
    double            custom_fajr_angle;
    double            custom_isha_angle;
    double            latitude;
    double            longitude;
    double            elevation;
    double            timezone;
    int               hijri_adjust;
    uint8_t           brightness;
    uint8_t           volume;
    char              adhan_file[64];
    uint8_t           dst;
    uint8_t           _reserved[31];
} settings_v1_t;

#define SETTINGS_V1_MAGIC 0x41444E31

static bool load_v1(settings_t *s, double *utc_offset_h) {
    FIL f;
    UINT br;
    settings_v1_t v;
    if (f_open(&f, SETTINGS_FILE_V1, FA_READ) != FR_OK) return false;
    bool ok = f_read(&f, &v, sizeof(v), &br) == FR_OK && br == sizeof(v) &&
              v.magic == SETTINGS_V1_MAGIC && v.version == 1;
    f_close(&f);
    if (!ok) return false;

    if (v.calc_method < CALC_METHOD_COUNT) s->calc_method = v.calc_method;
    if (v.asr_method == ASR_STANDARD || v.asr_method == ASR_HANAFI)
        s->asr_method = v.asr_method;
    if (v.high_lat <= HL_ANGLE_BASED) s->high_lat = v.high_lat;
    for (int i = 0; i < PRAYER_COUNT; i++) {
        if (v.adjustments[i] >= -30 && v.adjustments[i] <= 30)
            s->adjustments[i] = v.adjustments[i];
    }
    s->custom_fajr_angle = v.custom_fajr_angle;
    s->custom_isha_angle = v.custom_isha_angle;
    if (v.latitude != 0.0 || v.longitude != 0.0) {
        s->latitude  = v.latitude;
        s->longitude = v.longitude;
        s->elevation = v.elevation;
        s->location_set = true;
    }
    s->timezone = v.timezone;
    s->tz_set   = true;
    s->dst      = v.dst ? DST_ON : DST_OFF;
    s->hijri_adjust = v.hijri_adjust;
    if (v.brightness <= BRIGHTNESS_LEVELS) s->brightness = v.brightness;
    s->volume = v.volume;
    v.adhan_file[sizeof(v.adhan_file) - 1] = '\0';
    strcpy(s->adhan_file, v.adhan_file);

    *utc_offset_h = v.timezone + (v.dst ? 1.0 : 0.0);
    return true;
}

/* ── Text parsing ───────────────────────────────────────────────────────── */

static char *trim(char *p) {
    while (isspace((unsigned char)*p)) p++;
    char *e = p + strlen(p);
    while (e > p && isspace((unsigned char)e[-1])) *--e = '\0';
    return p;
}

static void lower(char *p) {
    for (; *p; p++) *p = (char)tolower((unsigned char)*p);
}

static int lookup(const char *v, const char * const *keys, int n) {
    for (int i = 0; i < n; i++)
        if (strcmp(v, keys[i]) == 0) return i;
    return -1;
}

static bool parse_bool(const char *v) {
    return strcmp(v, "1") == 0 || strcmp(v, "yes") == 0 ||
           strcmp(v, "on") == 0 || strcmp(v, "true") == 0;
}

static void apply_kv(settings_t *s, char *key, char *val) {
    lower(key);
    char raw_val[64];
    strncpy(raw_val, val, sizeof(raw_val) - 1);
    raw_val[sizeof(raw_val) - 1] = '\0';
    lower(val);
    int i;

    if (!strcmp(key, "latitude") && *val) {
        s->latitude = strtod(val, NULL);
        s->location_set = true;
    } else if (!strcmp(key, "longitude") && *val) {
        s->longitude = strtod(val, NULL);
        s->location_set = true;
    } else if (!strcmp(key, "elevation")) {
        s->elevation = strtod(val, NULL);
    } else if (!strcmp(key, "gps_location")) {
        s->gps_location = parse_bool(val);
    } else if (!strcmp(key, "timezone") && *val) {
        s->timezone = strtod(val, NULL);
        s->tz_set = true;
    } else if (!strcmp(key, "dst")) {
        if ((i = lookup(val, dst_keys, DST_MODE_COUNT)) >= 0) s->dst = (dst_mode_t)i;
    } else if (!strcmp(key, "method")) {
        if ((i = lookup(val, method_keys, CALC_METHOD_COUNT)) >= 0)
            s->calc_method = (calc_method_t)i;
    } else if (!strcmp(key, "asr")) {
        s->asr_method = strcmp(val, "hanafi") == 0 ? ASR_HANAFI : ASR_STANDARD;
    } else if (!strcmp(key, "high_lat")) {
        if ((i = lookup(val, high_lat_keys, 3)) >= 0) s->high_lat = (high_lat_method_t)i;
    } else if (!strcmp(key, "fajr_angle")) {
        s->custom_fajr_angle = strtod(val, NULL);
    } else if (!strcmp(key, "isha_angle")) {
        s->custom_isha_angle = strtod(val, NULL);
    } else if (!strncmp(key, "adjust_", 7)) {
        if ((i = lookup(key + 7, prayer_keys, PRAYER_COUNT)) >= 0)
            s->adjustments[i] = atoi(val);
    } else if (!strcmp(key, "hijri_adjust")) {
        s->hijri_adjust = atoi(val);
    } else if (!strcmp(key, "brightness")) {
        int b = strcmp(val, "auto") == 0 ? 0 : atoi(val);
        if (b >= 0 && b <= BRIGHTNESS_LEVELS) s->brightness = (uint8_t)b;
    } else if (!strcmp(key, "key_beep")) {
        s->key_beep = parse_bool(val);
    } else if (!strcmp(key, "volume")) {
        int pct = atoi(val);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        s->volume = (uint8_t)((pct * 255 + 50) / 100);
    } else if (!strcmp(key, "adhan")) {
        snprintf(s->adhan_file, sizeof(s->adhan_file), "%s", raw_val);
    } else if (!strcmp(key, "fajr_adhan")) {
        snprintf(s->fajr_adhan_file, sizeof(s->fajr_adhan_file), "%s", raw_val);
    } else if (!strcmp(key, "adhan_prayers")) {
        uint8_t mask = 0;
        for (char *tok = strtok(val, ", "); tok; tok = strtok(NULL, ", ")) {
            if ((i = lookup(tok, prayer_keys, PRAYER_COUNT)) >= 0 && i != PRAYER_SUNRISE)
                mask |= (uint8_t)(1u << i);
        }
        s->adhan_enabled = mask;
    }
}

static bool load_text(settings_t *s) {
    FIL f;
    /* settings.tmp exists alone only if power failed mid-save */
    if (f_open(&f, SETTINGS_FILE, FA_READ) != FR_OK &&
        f_open(&f, "/settings.tmp", FA_READ) != FR_OK)
        return false;
    char line[128];
    while (f_gets(line, sizeof(line), &f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(line);
        char *val = trim(eq + 1);
        if (*key) apply_kv(s, key, val);
    }
    f_close(&f);
    return true;
}

bool settings_load(settings_t *s, bool *migrated_v1, double *v1_utc_offset_h) {
    settings_defaults(s);
    *migrated_v1 = false;

    if (load_text(s)) return true;

    if (load_v1(s, v1_utc_offset_h)) {
        *migrated_v1 = true;
        if (settings_save(s)) f_rename(SETTINGS_FILE_V1, "/settings.dat.old");
        return true;
    }
    return false;
}

/* ── Saving ─────────────────────────────────────────────────────────────── */

static void put(FIL *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(FIL *f, const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    f_puts(buf, f);
}

bool settings_save(const settings_t *s) {
    static const char *tmp = "/settings.tmp";
    FIL f;
    if (f_open(&f, tmp, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;

    put(&f, "# Adhan Clock settings. Edit freely; lines starting with # are ignored.\n");
    put(&f, "# The clock rewrites this file when you change settings on the device.\n\n");

    put(&f, "# --- Location (decimal degrees, + = North/East) ---\n");
    if (s->location_set) {
        put(&f, "latitude = %.5f\n", s->latitude);
        put(&f, "longitude = %.5f\n", s->longitude);
    } else {
        put(&f, "#latitude = 40.71280\n#longitude = -74.00600\n");
    }
    put(&f, "elevation = %.0f\n", s->elevation);
    put(&f, "# Update location automatically from GPS: yes | no\n");
    put(&f, "gps_location = %s\n\n", s->gps_location ? "yes" : "no");

    put(&f, "# --- Time zone: standard (winter) UTC offset in hours ---\n");
    if (s->tz_set) put(&f, "timezone = %g\n", s->timezone);
    else           put(&f, "#timezone = -5\n");
    put(&f, "# Daylight saving: off | on | us (US/Canada) | eu (EU/UK)\n");
    put(&f, "dst = %s\n\n", dst_keys[s->dst < DST_MODE_COUNT ? s->dst : 0]);

    put(&f, "# --- Prayer calculation ---\n");
    put(&f, "# method: karachi | mwl | isna | egypt | ummalqura | tehran | gulf |\n");
    put(&f, "#         kuwait | qatar | singapore | turkey | custom\n");
    put(&f, "method = %s\n", settings_method_key(s->calc_method));
    put(&f, "# asr: standard (Shafi'i/Maliki/Hanbali) | hanafi\n");
    put(&f, "asr = %s\n", s->asr_method == ASR_HANAFI ? "hanafi" : "standard");
    put(&f, "# high_lat: middle | seventh | angle\n");
    put(&f, "high_lat = %s\n", high_lat_keys[s->high_lat <= HL_ANGLE_BASED ? s->high_lat : 0]);
    put(&f, "# Angles used only when method = custom\n");
    put(&f, "fajr_angle = %g\nisha_angle = %g\n", s->custom_fajr_angle, s->custom_isha_angle);
    put(&f, "# Per-prayer adjustments in minutes (+/-)\n");
    for (int i = 0; i < PRAYER_COUNT; i++)
        put(&f, "adjust_%s = %d\n", prayer_keys[i], s->adjustments[i]);
    put(&f, "hijri_adjust = %d\n\n", s->hijri_adjust);

    put(&f, "# --- Adhan audio (files in the /adhan folder) ---\n");
    put(&f, "adhan = %s\n", s->adhan_file);
    put(&f, "# Optional different file for Fajr (blank = same as above)\n");
    put(&f, "fajr_adhan = %s\n", s->fajr_adhan_file);
    put(&f, "# Which prayers play the adhan\n");
    put(&f, "adhan_prayers =");
    bool first = true;
    for (int i = 0; i < PRAYER_COUNT; i++) {
        if (s->adhan_enabled & (1u << i)) {
            put(&f, "%s%s", first ? " " : ", ", prayer_keys[i]);
            first = false;
        }
    }
    put(&f, "\n# Volume 0-100\n");
    put(&f, "volume = %d\n\n", (s->volume * 100 + 127) / 255);

    put(&f, "# --- Display: brightness auto | 1-16 ---\n");
    if (s->brightness == 0) put(&f, "brightness = auto\n");
    else                    put(&f, "brightness = %d\n", s->brightness);
    put(&f, "# Click the buzzer on remote key presses: yes | no\n");
    put(&f, "# (only works when the buzzer pin isn't shared with SD MOSI)\n");
    put(&f, "key_beep = %s\n", s->key_beep ? "yes" : "no");

    bool ok = f_close(&f) == FR_OK;
    if (!ok) return false;

    f_unlink(SETTINGS_FILE);
    return f_rename(tmp, SETTINGS_FILE) == FR_OK;
}
