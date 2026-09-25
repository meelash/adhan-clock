/*
 * Settings — persistent configuration
 *
 * Stored on the SD card as /settings.txt (key=value, editable on a PC via
 * USB File Mode). An old binary /settings.dat is migrated automatically.
 */

#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include "prayer/methods.h"
#include "app/timeutil.h"

#define ADHAN_ENABLE_ALL  0x3D   /* all prayers except Sunrise (bit = prayer index) */

typedef struct {
    /* Prayer calculation */
    calc_method_t     calc_method;
    asr_method_t      asr_method;
    high_lat_method_t high_lat;
    int               adjustments[PRAYER_COUNT];  /* per-prayer +/- minutes */
    double            custom_fajr_angle;
    double            custom_isha_angle;

    /* Location (updated from GPS when gps_location is on) */
    double            latitude;
    double            longitude;
    double            elevation;
    bool              location_set;
    bool              gps_location;

    /* Time zone: standard offset from UTC in hours, plus DST rule */
    double            timezone;
    bool              tz_set;
    dst_mode_t        dst;

    /* Hijri */
    int               hijri_adjust;  /* +/- days */

    /* Display / input */
    uint8_t           brightness;    /* 0 = auto, 1-16 = manual */
    bool              key_beep;      /* click on remote key presses */

    /* Audio */
    uint8_t           volume;        /* 0-255 */
    char              adhan_file[64];      /* filename in /adhan */
    char              fajr_adhan_file[64]; /* optional, "" = same as adhan_file */
    uint8_t           adhan_enabled;       /* bit per prayer index */
} settings_t;

/* Set factory defaults. */
void settings_defaults(settings_t *s);

/* Load settings from the SD card (migrating settings.dat if needed).
 * Returns true if a settings file was found. Always leaves *s valid.
 * *migrated_v1 is set when old settings.dat values were imported — the old
 * firmware kept LOCAL time in the RTC, so the caller must convert it. */
bool settings_load(settings_t *s, bool *migrated_v1, double *v1_utc_offset_h);

/* Save settings to the SD card (atomic: write temp file, then rename). */
bool settings_save(const settings_t *s);

/* Names for method keys in settings.txt and on screen. */
const char *settings_method_key(calc_method_t m);

#endif /* APP_SETTINGS_H */
