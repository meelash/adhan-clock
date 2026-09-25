/*
 * Prayer Time Calculation Methods
 *
 * Defines calculation conventions with their Fajr/Isha angles.
 */

#ifndef PRAYER_METHODS_H
#define PRAYER_METHODS_H

typedef enum {
    CALC_KARACHI = 0,       /* University of Islamic Sciences, Karachi */
    CALC_MWL,               /* Muslim World League */
    CALC_ISNA,              /* Islamic Society of North America */
    CALC_EGYPT,             /* Egyptian General Authority of Survey */
    CALC_UMM_AL_QURA,      /* Umm al-Qura University, Makkah */
    CALC_TEHRAN,            /* Institute of Geophysics, Tehran */
    CALC_GULF,              /* Gulf Region (same as Umm al-Qura) */
    CALC_KUWAIT,            /* Kuwait */
    CALC_QATAR,             /* Qatar */
    CALC_SINGAPORE,         /* MUIS Singapore */
    CALC_TURKEY,            /* Diyanet, Turkey */
    CALC_CUSTOM,            /* User-defined angles */
    CALC_METHOD_COUNT
} calc_method_t;

typedef enum {
    ASR_STANDARD = 1,       /* Shafi'i/Hanbali/Maliki: shadow = 1× */
    ASR_HANAFI   = 2        /* Hanafi: shadow = 2× */
} asr_method_t;

typedef enum {
    HL_MIDDLE_OF_NIGHT = 0, /* Middle of the night */
    HL_SEVENTH_OF_NIGHT,    /* 1/7 of the night */
    HL_ANGLE_BASED          /* Angle-based (portion of night based on angle) */
} high_lat_method_t;

typedef struct {
    const char     *name;
    double          fajr_angle;
    double          isha_angle;
    int             isha_interval_min; /* 0 = use angle; >0 = minutes after Maghrib */
    int             maghrib_interval_min; /* 0 = sunset; >0 = minutes after sunset */
} calc_method_params_t;

#define METHOD_PARAMS_TABLE { \
    /* CALC_KARACHI     */ { "Karachi",      18.0, 18.0, 0, 0 }, \
    /* CALC_MWL         */ { "MWL",          18.0, 17.0, 0, 0 }, \
    /* CALC_ISNA        */ { "ISNA",         15.0, 15.0, 0, 0 }, \
    /* CALC_EGYPT       */ { "Egypt",        19.5, 17.5, 0, 0 }, \
    /* CALC_UMM_AL_QURA */ { "Umm al-Qura", 18.5,  0.0, 90, 0 }, \
    /* CALC_TEHRAN      */ { "Tehran",       17.7, 14.0, 0, 0 }, \
    /* CALC_GULF        */ { "Gulf",         19.5,  0.0, 90, 0 }, \
    /* CALC_KUWAIT      */ { "Kuwait",       18.0, 17.5, 0, 0 }, \
    /* CALC_QATAR       */ { "Qatar",        18.0,  0.0, 90, 0 }, \
    /* CALC_SINGAPORE   */ { "Singapore",    20.0, 18.0, 0, 0 }, \
    /* CALC_TURKEY      */ { "Turkey",       18.0, 17.0, 0, 0 }, \
    /* CALC_CUSTOM      */ { "Custom",       18.0, 18.0, 0, 0 }, \
}

/* Names of the 6 prayer times */
typedef enum {
    PRAYER_FAJR = 0,
    PRAYER_SUNRISE,
    PRAYER_DHUHR,
    PRAYER_ASR,
    PRAYER_MAGHRIB,
    PRAYER_ISHA,
    PRAYER_COUNT
} prayer_index_t;

static const char * const prayer_names[PRAYER_COUNT] = {
    "Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha"
};

#endif /* PRAYER_METHODS_H */
