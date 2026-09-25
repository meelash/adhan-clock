/*
 * Time-of-Day Colour Theme — implementation
 */

#include "ui/theme.h"

/*
 * Colour palettes for each time period.
 * Colours are 12-bit (0xRGB, 4 bits per channel).
 */
static const theme_palette_t palettes[PERIOD_COUNT] = {
    /* PERIOD_NIGHT: Isha → midnight — deep teal/blue */
    {
        .time_colour     = 0x0AA, /* teal digits */
        .date_colour     = 0x066,
        .prayer_label    = 0x08B,
        .countdown_colour= 0x0CC,
        .accent          = 0x044,
        .bg              = 0x000,
    },
    /* PERIOD_LATE_NIGHT: midnight → Fajr — deep indigo */
    {
        .time_colour     = 0x63A, /* soft indigo/purple */
        .date_colour     = 0x428,
        .prayer_label    = 0x74B,
        .countdown_colour= 0x85C,
        .accent          = 0x316,
        .bg              = 0x000,
    },
    /* PERIOD_DAWN: Fajr → Sunrise — warm amber/orange */
    {
        .time_colour     = 0xFA0, /* amber */
        .date_colour     = 0xC80,
        .prayer_label    = 0xFC0, /* gold label */
        .countdown_colour= 0xFD0,
        .accent          = 0x860,
        .bg              = 0x000,
    },
    /* PERIOD_MORNING: Sunrise → Dhuhr — bright white/gold */
    {
        .time_colour     = 0xFFF, /* white */
        .date_colour     = 0xCCC,
        .prayer_label    = 0xFC0, /* gold */
        .countdown_colour= 0xFFF,
        .accent          = 0x880,
        .bg              = 0x000,
    },
    /* PERIOD_AFTERNOON: Dhuhr → Asr — warm yellow */
    {
        .time_colour     = 0xFE0, /* yellow-gold */
        .date_colour     = 0xBB0,
        .prayer_label    = 0xFB0,
        .countdown_colour= 0xFF0,
        .accent          = 0x770,
        .bg              = 0x000,
    },
    /* PERIOD_LATE_AFT: Asr → Maghrib — deep orange/copper */
    {
        .time_colour     = 0xF80, /* deep orange */
        .date_colour     = 0xB60,
        .prayer_label    = 0xFA0,
        .countdown_colour= 0xFB0,
        .accent          = 0x840,
        .bg              = 0x000,
    },
    /* PERIOD_EVENING: Maghrib → Isha — deep purple/magenta */
    {
        .time_colour     = 0xC4E, /* magenta-purple */
        .date_colour     = 0x83A,
        .prayer_label    = 0xD5F,
        .countdown_colour= 0xE6F,
        .accent          = 0x528,
        .bg              = 0x000,
    },
};

void theme_init(void) {
    /* nothing needed for now; could load custom themes from SD */
}

const theme_palette_t *theme_get_period_palette(time_period_t period) {
    if (period >= PERIOD_COUNT) period = PERIOD_NIGHT;
    return &palettes[period];
}

const theme_palette_t *theme_get_palette(int cur_hour, int cur_minute,
                                          const int prayer_minutes[6])
{
    int now = cur_hour * 60 + cur_minute;

    /* prayer_minutes[]: Fajr, Sunrise, Dhuhr, Asr, Maghrib, Isha */
    int fajr    = prayer_minutes[0];
    int sunrise = prayer_minutes[1];
    int dhuhr   = prayer_minutes[2];
    int asr     = prayer_minutes[3];
    int maghrib = prayer_minutes[4];
    int isha    = prayer_minutes[5];

    time_period_t period;

    if (now < fajr) {
        /* After midnight, before Fajr */
        period = PERIOD_LATE_NIGHT;
    } else if (now < sunrise) {
        period = PERIOD_DAWN;
    } else if (now < dhuhr) {
        period = PERIOD_MORNING;
    } else if (now < asr) {
        period = PERIOD_AFTERNOON;
    } else if (now < maghrib) {
        period = PERIOD_LATE_AFT;
    } else if (now < isha) {
        period = PERIOD_EVENING;
    } else {
        period = PERIOD_NIGHT;
    }

    return &palettes[period];
}
