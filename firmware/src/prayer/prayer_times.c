/*
 * Prayer Time Calculation Engine — wrapper around libadhan
 *
 * Uses the libadhan C library for astronomical prayer time computation.
 * This wrapper maps our firmware types/enums to the library's API and
 * converts the time_t results into our compact hour:minute format.
 */

#include "prayer/prayer_times.h"
#include "config.h"
#include <time.h>

/* libadhan headers */
#include <coordinates.h>
#include <data_components.h>
#include <calculation_parameters.h>

/* libadhan's prayer_times.h — no name conflict because ours is included
 * via "prayer/prayer_times.h" (directory-prefixed) while this one is
 * resolved from the -I lib/adhan/include path.  Different header guards. */
#include <prayer_times.h>

/* ── Method parameters table (kept for UI display names) ────────────────── */

static const calc_method_params_t method_table[CALC_METHOD_COUNT] = METHOD_PARAMS_TABLE;

const calc_method_params_t *prayer_get_method_params(calc_method_t m) {
    if (m >= CALC_METHOD_COUNT) m = CALC_KARACHI;
    return &method_table[m];
}

void prayer_config_defaults(prayer_config_t *cfg) {
    cfg->method      = DEFAULT_CALC_METHOD;
    cfg->asr_method  = (asr_method_t)DEFAULT_ASR_METHOD;
    cfg->high_lat    = HL_MIDDLE_OF_NIGHT;
    cfg->latitude    = DEFAULT_LATITUDE;
    cfg->longitude   = DEFAULT_LONGITUDE;
    cfg->elevation   = DEFAULT_ELEVATION;
    cfg->timezone    = DEFAULT_TIMEZONE;
    cfg->custom_fajr_angle = 18.0;
    cfg->custom_isha_angle = 18.0;
    for (int i = 0; i < PRAYER_COUNT; i++)
        cfg->adjustments[i] = 0;
}

/* ── Enum mapping helpers ───────────────────────────────────────────────── */

static calculation_method map_calc_method(calc_method_t m) {
    switch (m) {
    case CALC_KARACHI:      return KARACHI;
    case CALC_MWL:          return MUSLIM_WORLD_LEAGUE;
    case CALC_ISNA:         return NORTH_AMERICA;
    case CALC_EGYPT:        return EGYPTIAN;
    case CALC_UMM_AL_QURA:  return UMM_AL_QURA;
    case CALC_GULF:         return GULF;
    case CALC_KUWAIT:       return KUWAIT;
    case CALC_QATAR:        return QATAR;
    default:                return OTHER;
    }
}

static madhab_t map_madhab(asr_method_t m) {
    return (m == ASR_HANAFI) ? HANAFI : SHAFI;
}

static high_latitude_rule_t map_high_lat(high_lat_method_t h) {
    switch (h) {
    case HL_MIDDLE_OF_NIGHT:  return MIDDLE_OF_THE_NIGHT;
    case HL_SEVENTH_OF_NIGHT: return SEVENTH_OF_THE_NIGHT;
    case HL_ANGLE_BASED:
    default:                  return TWILIGHT_ANGLE;
    }
}

/* ── Main calculation ───────────────────────────────────────────────────── */

void prayer_calc(int year, int month, int day,
                 const prayer_config_t *cfg,
                 prayer_day_t *out)
{
    /* Determine Fajr/Isha angles from our method table */
    const calc_method_params_t *mp;
    if (cfg->method == CALC_CUSTOM) {
        static calc_method_params_t custom;
        custom = method_table[CALC_CUSTOM];
        custom.fajr_angle = cfg->custom_fajr_angle;
        custom.isha_angle = cfg->custom_isha_angle;
        mp = &custom;
    } else {
        mp = &method_table[cfg->method];
    }

    /* Build libadhan structures */
    coordinates_t coords = { cfg->latitude, cfg->longitude };
    date_components_t date = new_date_components(day, month, year);

    calculation_parameters_t params;
    if (mp->isha_interval_min > 0) {
        params = new_calculation_parameters2(mp->fajr_angle, mp->isha_interval_min);
    } else {
        params = new_calculation_parameters(mp->fajr_angle, mp->isha_angle);
    }
    params.method           = map_calc_method(cfg->method);
    params.madhab           = map_madhab(cfg->asr_method);
    params.highLatitudeRule  = map_high_lat(cfg->high_lat);

    /* Per-prayer minute adjustments */
    params.adjustments.fajr    = cfg->adjustments[PRAYER_FAJR];
    params.adjustments.sunrise = cfg->adjustments[PRAYER_SUNRISE];
    params.adjustments.dhuhr   = cfg->adjustments[PRAYER_DHUHR];
    params.adjustments.asr     = cfg->adjustments[PRAYER_ASR];
    params.adjustments.maghrib = cfg->adjustments[PRAYER_MAGHRIB];
    params.adjustments.isha    = cfg->adjustments[PRAYER_ISHA];

    /* Compute prayer times — library returns UTC time_t values */
    prayer_times_t pt = new_prayer_times(&coords, &date, &params);

    /* Convert each time_t result to local hour:minute.
     * On Pico (bare-metal newlib), localtime()==gmtime() so the library
     * returns UTC-based time_t values.  We add our timezone offset to
     * shift into local time before extracting hour/minute. */
    int tz_seconds = (int)(cfg->timezone * 3600.0);

    time_t times_arr[PRAYER_COUNT] = {
        pt.fajr, pt.sunrise, pt.dhuhr, pt.asr, pt.maghrib, pt.isha
    };

    for (int i = 0; i < PRAYER_COUNT; i++) {
        time_t local = times_arr[i] + tz_seconds;
        struct tm *tm = gmtime(&local);
        if (tm) {
            out->times[i].hour = (uint8_t)tm->tm_hour;
            out->times[i].minute = (uint8_t)tm->tm_min;
            out->times[i].total_minutes = tm->tm_hour * 60 + tm->tm_min;
        } else {
            out->times[i].hour = 0;
            out->times[i].minute = 0;
            out->times[i].total_minutes = 0;
        }
    }
}

/* ── Next prayer helper (unchanged) ─────────────────────────────────────── */

prayer_index_t prayer_next(const prayer_day_t *today,
                           int cur_hour, int cur_minute,
                           int *minutes_until)
{
    int now_min = cur_hour * 60 + cur_minute;

    for (int i = 0; i < PRAYER_COUNT; i++) {
        if (today->times[i].total_minutes > now_min) {
            if (minutes_until)
                *minutes_until = today->times[i].total_minutes - now_min;
            return (prayer_index_t)i;
        }
    }

    /* All prayers have passed — next is Fajr (tomorrow) */
    if (minutes_until)
        *minutes_until = (1440 - now_min) + today->times[PRAYER_FAJR].total_minutes;
    return PRAYER_FAJR;
}
