/*
 * Hijri (Islamic) Calendar Conversion
 *
 * Converts Gregorian dates to Hijri using the Umm al-Qura algorithm
 * approximation (tabular calendar with astronomical corrections).
 */

#ifndef HIJRI_H
#define HIJRI_H

#include <stdint.h>

typedef struct {
    uint16_t year;    /* Hijri year */
    uint8_t  month;   /* 1-12 */
    uint8_t  day;     /* 1-30 */
} hijri_date_t;

/*
 * Convert Gregorian date to Hijri.
 * Valid range: approximately 1900-2099 CE.
 */
void gregorian_to_hijri(int g_year, int g_month, int g_day,
                        hijri_date_t *h);

/* Return the name of a Hijri month (1-12) */
const char *hijri_month_name(uint8_t month);

/* Return abbreviated name of a Hijri month (1-12), fits 64px display */
const char *hijri_month_abbrev(uint8_t month);

/* Manual adjustment: shift the computed Hijri date by +/- days.
 * Useful since the tabular calendar can differ by 1-2 days from
 * actual moon sighting. */
void hijri_adjust(hijri_date_t *h, int days);

#endif /* HIJRI_H */
