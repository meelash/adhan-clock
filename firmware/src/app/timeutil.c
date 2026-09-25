/*
 * Time utilities — implementation
 *
 * Civil-date algorithms are Howard Hinnant's days_from_civil/civil_from_days.
 */

#include "app/timeutil.h"

int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void civil_from_days(int64_t z, int *y, int *m, int *d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp  = (5 * doy + 2) / 153;
    int dd = (int)(doy - (153 * mp + 2) / 5 + 1);
    int mm = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400) + (mm <= 2);
    *m = mm;
    *d = dd;
}

int weekday_from_days(int64_t z) {
    return (int)(z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6);
}

int days_in_month(int y, int m) {
    static const uint8_t dm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dm[(m - 1) % 12];
}

epoch_t rtc_to_epoch(const rtc_time_t *t) {
    return days_from_civil(t->year, t->month, t->day) * 86400
         + t->hour * 3600 + t->minute * 60 + t->second;
}

void epoch_to_rtc(epoch_t e, rtc_time_t *t) {
    int64_t days = e / 86400;
    int64_t secs = e % 86400;
    if (secs < 0) { secs += 86400; days--; }
    int y, m, d;
    civil_from_days(days, &y, &m, &d);
    t->year   = (uint16_t)y;
    t->month  = (uint8_t)m;
    t->day    = (uint8_t)d;
    t->hour   = (uint8_t)(secs / 3600);
    t->minute = (uint8_t)((secs / 60) % 60);
    t->second = (uint8_t)(secs % 60);
    t->dow    = (uint8_t)(weekday_from_days(days) + 1);
}

/* Day number of the n-th (1-based) Sunday of a month; n = 0 → last Sunday. */
static int64_t nth_sunday(int y, int m, int n) {
    if (n > 0) {
        int64_t first = days_from_civil(y, m, 1);
        int wd = weekday_from_days(first);
        return first + (7 - wd) % 7 + 7 * (n - 1);
    }
    int64_t last = days_from_civil(y, m, days_in_month(y, m));
    return last - weekday_from_days(last);
}

bool dst_active(epoch_t utc, double tz_hours, dst_mode_t dst) {
    switch (dst) {
    case DST_ON:
        return true;
    case DST_US: {
        /* Transitions at 02:00 local standard time (start) and
         * 02:00 local daylight time = 01:00 standard (end). */
        int32_t std = (int32_t)(tz_hours * 3600.0);
        rtc_time_t t;
        epoch_to_rtc(utc + std, &t);
        epoch_t start = nth_sunday(t.year, 3, 2) * 86400 + 2 * 3600 - std;
        epoch_t end   = nth_sunday(t.year, 11, 1) * 86400 + 1 * 3600 - std;
        return utc >= start && utc < end;
    }
    case DST_EU: {
        rtc_time_t t;
        epoch_to_rtc(utc, &t);
        epoch_t start = nth_sunday(t.year, 3, 0) * 86400 + 3600;
        epoch_t end   = nth_sunday(t.year, 10, 0) * 86400 + 3600;
        return utc >= start && utc < end;
    }
    default:
        return false;
    }
}

int32_t utc_offset_seconds(epoch_t utc, double tz_hours, dst_mode_t dst) {
    int32_t off = (int32_t)(tz_hours * 3600.0 + (tz_hours >= 0 ? 0.5 : -0.5));
    if (dst_active(utc, tz_hours, dst)) off += 3600;
    return off;
}

const char *dst_mode_name(dst_mode_t m) {
    switch (m) {
    case DST_OFF: return "Off";
    case DST_ON:  return "On (+1h)";
    case DST_US:  return "Auto US";
    case DST_EU:  return "Auto EU";
    default:      return "?";
    }
}
