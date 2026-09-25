/*
 * Light Sensor Driver (ADC)
 */

#ifndef DRIVER_LIGHT_SENSOR_H
#define DRIVER_LIGHT_SENSOR_H

#include <stdint.h>

void     light_sensor_init(void);
uint16_t light_sensor_read_raw(void);      /* 0-4095 (12-bit ADC) */
uint8_t  light_sensor_to_brightness(void); /* 1-16 mapped from ambient */

#endif /* DRIVER_LIGHT_SENSOR_H */
