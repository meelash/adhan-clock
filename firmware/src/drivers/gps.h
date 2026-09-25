/*
 * GPS NMEA Parser — Quectel L76B on UART
 *
 * Parses GGA and RMC sentences to extract position, time, and date.
 * RX is interrupt-driven into a ring buffer; gps_poll() parses on the main loop.
 */

#ifndef DRIVER_GPS_H
#define DRIVER_GPS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    /* Position (valid once ever_fixed) */
    double   latitude;      /* decimal degrees, + = N */
    double   longitude;     /* decimal degrees, + = E */
    double   altitude_m;    /* metres above sea level */
    /* UTC time of the last valid RMC sentence */
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
    uint32_t time_ms;       /* ms since boot when that sentence arrived */
    bool     time_valid;
    /* Status */
    uint8_t  satellites;
    bool     fix_valid;     /* fix within the last few seconds */
    bool     ever_fixed;
    uint32_t last_fix_ms;   /* ms since boot of last valid fix */
    uint32_t bytes;         /* diagnostics: bytes received */
    uint32_t sentences;     /* diagnostics: valid sentences */
} gps_data_t;

void             gps_init(void);
void             gps_poll(void);          /* call frequently from main loop */
const gps_data_t *gps_get_data(void);
bool             gps_has_fix(void);
/* Milliseconds since last valid fix, or UINT32_MAX if never */
uint32_t         gps_fix_age_ms(void);

#endif /* DRIVER_GPS_H */
