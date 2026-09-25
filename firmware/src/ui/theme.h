/*
 * Time-of-Day Colour Theme
 *
 * Colours smoothly transition based on the current prayer time period.
 */

#ifndef UI_THEME_H
#define UI_THEME_H

#include <stdint.h>
#include "drivers/hub75.h"
#include "prayer/methods.h"

/* A colour palette for one time-of-day period */
typedef struct {
    colour_t time_colour;       /* large clock digits */
    colour_t date_colour;       /* Hijri date line */
    colour_t prayer_label;      /* next prayer name */
    colour_t countdown_colour;  /* countdown digits */
    colour_t accent;            /* separators, icons */
    colour_t bg;                /* background (usually black) */
} theme_palette_t;

/* Time-of-day periods, mapped from prayer times */
typedef enum {
    PERIOD_NIGHT,       /* Isha → midnight (deep blue/teal) */
    PERIOD_LATE_NIGHT,  /* midnight → Fajr (indigo) */
    PERIOD_DAWN,        /* Fajr → Sunrise (warm amber/orange) */
    PERIOD_MORNING,     /* Sunrise → Dhuhr (bright gold/white) */
    PERIOD_AFTERNOON,   /* Dhuhr → Asr (warm yellow) */
    PERIOD_LATE_AFT,    /* Asr → Maghrib (deep orange/copper) */
    PERIOD_EVENING,     /* Maghrib → Isha (purple/magenta) */
    PERIOD_COUNT
} time_period_t;

/* Get the active palette for the current time. */
const theme_palette_t *theme_get_palette(int cur_hour, int cur_minute,
                                          const int prayer_minutes[6]);

/* Get a named period palette directly */
const theme_palette_t *theme_get_period_palette(time_period_t period);

/* Initialise the theme system */
void theme_init(void);

#endif /* UI_THEME_H */
