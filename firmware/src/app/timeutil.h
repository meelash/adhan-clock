/*
 * Time utilities — epoch conversion and daylight-saving rules.
 *
 * The DS3231 holds UTC. Local time = UTC + timezone + DST, where DST is
 * computed from the rule in settings, so it switches automatically.
 */

#ifndef APP_TIMEUTIL_H
#define APP_TIMEUTIL_H

#include <stdint.h>
#include <stdbool.h>
#include "drivers/ds3231.h"

typedef int64_t epoch_t;   /* seconds since 1970-01-01 00:00:00 UTC */

typedef enum {
    DST_OFF = 0,     /* never */
    DST_ON,          /* always +1 h (manual) */
    DST_US,          /* US/Canada: 2nd Sun Mar 02:00 → 1st Sun Nov 02:00 local */
    DST_EU,          /* EU/UK: last Sun Mar → last Sun Oct, 01:00 UTC */
    DST_MODE_COUNT
} dst_mode_t;

int64_t days_from_civil(int y, int m, int d);
void    civil_from_days(int64_t z, int *y, int *m, int *d);
int     weekday_from_days(int64_t z);          /* 0 = Sunday */
int     days_in_month(int y, int m);

epoch_t rtc_to_epoch(const rtc_time_t *t);
void    epoch_to_rtc(epoch_t e, rtc_time_t *t); /* also fills dow (1 = Sunday) */

/* Total UTC offset in seconds at the given UTC instant. */
int32_t utc_offset_seconds(epoch_t utc, double tz_hours, dst_mode_t dst);

/* True if DST is in effect at the given UTC instant. */
bool    dst_active(epoch_t utc, double tz_hours, dst_mode_t dst);

const char *dst_mode_name(dst_mode_t m);

#endif /* APP_TIMEUTIL_H */
