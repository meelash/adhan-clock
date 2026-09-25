/*
 * Hijri Calendar — implementation
 *
 * Uses the Kuwaiti algorithm (a common tabular/arithmetic approach).
 * This approximates the civil Islamic calendar used in many countries.
 *
 * Note: The actual Islamic calendar is based on moon sighting and may
 * differ by 1-2 days. A manual adjustment parameter is provided.
 */

#include "prayer/hijri.h"
#include <math.h>

static const char * const month_names[12] = {
    "Muharram",  "Safar",       "Rabi' I",   "Rabi' II",
    "Jumada I",  "Jumada II",   "Rajab",     "Sha'ban",
    "Ramadan",   "Shawwal",     "Dhul Qi'dah","Dhul Hijjah"
};

static const char * const month_abbrevs[12] = {
    "Mhrm", "Safr", "Rb I", "RbII",
    "Jm I", "JmII", "Rajb", "Shbn",
    "Rmdn", "Shwl", "DhQi", "DhHj"
};

/*
 * Gregorian to Julian Day Number (JDN).
 */
static long gregorian_to_jdn(int year, int month, int day) {
    long a = (14 - month) / 12;
    long y = year + 4800 - a;
    long m = month + 12 * a - 3;
    return day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
}

/*
 * Julian Day Number to Hijri date (Kuwaiti algorithm).
 *
 * Based on the algorithm used by the Kuwait Ministry and derived from
 * the US Naval Observatory / Robert Harry van Gent.
 */
void gregorian_to_hijri(int g_year, int g_month, int g_day,
                        hijri_date_t *h)
{
    long jd = gregorian_to_jdn(g_year, g_month, g_day);

    /* Islamic epoch: July 16, 622 CE (Julian) = JDN 1948439.5
     * Using the civil (tabular) Islamic calendar:
     * - 30-year cycle with 11 leap years
     * - Months alternate 30/29 days, with month 12 having 30 in leap years
     */

    long l = jd - 1948440 + 10632;
    long n = (long)((l - 1) / 10631.0);
    l = l - 10631 * n + 354;

    long j = ((long)((10985.0 - l) / 5316.0)) * ((long)((50 * l) / 17719.0)) +
             ((long)(l / 5670.0)) * ((long)((43 * l) / 15238.0));

    l = l - ((long)((30 - j) / 15.0)) * ((long)((17719.0 * j) / 50.0)) -
            ((long)(j / 16.0)) * ((long)((15238.0 * j) / 43.0)) + 29;

    long m = (long)((24 * l) / 709.0);
    long d = l - (long)((709.0 * m) / 24.0);
    long y = 30 * n + j - 30;

    h->year  = (uint16_t)y;
    h->month = (uint8_t)m;
    h->day   = (uint8_t)d;
}

const char *hijri_month_name(uint8_t month) {
    if (month < 1 || month > 12) return "?";
    return month_names[month - 1];
}

const char *hijri_month_abbrev(uint8_t month) {
    if (month < 1 || month > 12) return "?";
    return month_abbrevs[month - 1];
}

/*
 * Adjust Hijri date by a given number of days (+/-).
 * Uses a simple day-counting approach with the tabular calendar
 * (alternating 30/29 day months, leap year in position 2,5,7,10,13,16,18,21,24,26,29).
 */
static int hijri_month_days(uint16_t year, uint8_t month) {
    if (month % 2 == 1) return 30; /* odd months: 30 days */
    if (month == 12) {
        /* Leap year check: years 2,5,7,10,13,16,18,21,24,26,29 in 30-year cycle */
        int pos = ((int)year - 1) % 30 + 1;
        int leap_years[] = {2, 5, 7, 10, 13, 16, 18, 21, 24, 26, 29};
        for (int i = 0; i < 11; i++) {
            if (pos == leap_years[i]) return 30;
        }
    }
    return 29; /* even months: 29 days */
}

void hijri_adjust(hijri_date_t *h, int days) {
    /* Add days */
    int d = (int)h->day + days;
    int m = (int)h->month;
    int y = (int)h->year;

    while (d > hijri_month_days((uint16_t)y, (uint8_t)m)) {
        d -= hijri_month_days((uint16_t)y, (uint8_t)m);
        m++;
        if (m > 12) { m = 1; y++; }
    }
    while (d < 1) {
        m--;
        if (m < 1) { m = 12; y--; }
        d += hijri_month_days((uint16_t)y, (uint8_t)m);
    }

    h->year  = (uint16_t)y;
    h->month = (uint8_t)m;
    h->day   = (uint8_t)d;
}
