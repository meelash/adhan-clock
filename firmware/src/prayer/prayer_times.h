/*
 * Prayer Time Calculation Engine
 *
 * High-precision astronomical computation of Islamic prayer times.
 */

#ifndef PRAYER_TIMES_H
#define PRAYER_TIMES_H

#include <stdint.h>
#include "prayer/methods.h"

/* A single prayer time as hours + minutes (24-hour, local time) */
typedef struct {
    uint8_t hour;
    uint8_t minute;
    int     total_minutes; /* hour*60+minute for easy comparison */
} prayer_time_t;

/* All 6 prayer times for a given day */
typedef struct {
    prayer_time_t times[PRAYER_COUNT];
} prayer_day_t;

/* Configuration for the calculation */
typedef struct {
    calc_method_t    method;
    asr_method_t     asr_method;
    high_lat_method_t high_lat;

    double latitude;
    double longitude;
    double elevation;     /* metres above sea level */
    double timezone;      /* hours offset from UTC (e.g. +5.0 for PKT) */

    /* Per-prayer manual adjustments in minutes (+/−) */
    int    adjustments[PRAYER_COUNT];

    /* Custom angles (only used when method == CALC_CUSTOM) */
    double custom_fajr_angle;
    double custom_isha_angle;
} prayer_config_t;

/*
 * Calculate prayer times for a given Gregorian date.
 *
 * @param year   Gregorian year (e.g. 2026)
 * @param month  1-12
 * @param day    1-31
 * @param cfg    Calculation configuration
 * @param out    Output: computed prayer times
 */
void prayer_calc(int year, int month, int day,
                 const prayer_config_t *cfg,
                 prayer_day_t *out);

/*
 * Return the index of the next prayer after the given local time.
 * Returns PRAYER_FAJR (next day) if all today's prayers have passed.
 * Sets *minutes_until to the number of minutes remaining.
 */
prayer_index_t prayer_next(const prayer_day_t *today,
                           int cur_hour, int cur_minute,
                           int *minutes_until);

/* Get the method parameter table entry for a given method */
const calc_method_params_t *prayer_get_method_params(calc_method_t m);

/* Initialise config with sensible defaults */
void prayer_config_defaults(prayer_config_t *cfg);

#endif /* PRAYER_TIMES_H */
