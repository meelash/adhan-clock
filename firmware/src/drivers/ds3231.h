/*
 * DS3231 Real-Time Clock Driver (I2C)
 */

#ifndef DRIVER_DS3231_H
#define DRIVER_DS3231_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t  second;    /* 0-59  */
    uint8_t  minute;    /* 0-59  */
    uint8_t  hour;      /* 0-23  */
    uint8_t  dow;       /* 1-7 (1=Sunday) */
    uint8_t  day;       /* 1-31  */
    uint8_t  month;     /* 1-12  */
    uint16_t year;      /* 2000-2099 */
} rtc_time_t;

/* The clock app stores UTC in the RTC. */
void     ds3231_init(void);
bool     ds3231_read_time(rtc_time_t *t);
bool     ds3231_write_time(const rtc_time_t *t);   /* also clears lost-power flag */
bool     ds3231_lost_power(void);  /* true if the oscillator stopped (dead battery) */
float    ds3231_read_temperature(void);

#endif /* DRIVER_DS3231_H */
