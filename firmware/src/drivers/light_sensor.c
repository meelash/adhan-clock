/*
 * Light Sensor Driver — implementation
 */

#include "drivers/light_sensor.h"
#include "config.h"
#include "hardware/adc.h"

void light_sensor_init(void) {
    if (PIN_LIGHT_SENSOR > 29) return;
    adc_init();
    adc_gpio_init(PIN_LIGHT_SENSOR);
}

uint16_t light_sensor_read_raw(void) {
    if (PIN_LIGHT_SENSOR > 29) return 2048;
    adc_select_input(LIGHT_ADC_INPUT);
    return adc_read(); /* 12-bit, 0-4095 */
}

uint8_t light_sensor_to_brightness(void) {
    uint16_t raw = light_sensor_read_raw();

    /*
     * Map ADC range to brightness 1-16.
     * Higher ADC = brighter room = higher display brightness.
     * The exact curve depends on the sensor; use a simple linear
     * mapping with clamping. Adjust thresholds after testing.
     */
    if (raw < 100)  return 1;
    if (raw > 3800) return 16;

    return (uint8_t)(1 + (raw - 100) * 15 / 3700);
}
