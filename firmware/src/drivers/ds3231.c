/*
 * DS3231 Real-Time Clock Driver — implementation (I2C)
 */

#include "drivers/ds3231.h"
#include "config.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

/* BCD helpers */
static uint8_t bcd_to_dec(uint8_t bcd) { return (bcd >> 4) * 10 + (bcd & 0x0F); }
static uint8_t dec_to_bcd(uint8_t dec) { return ((dec / 10) << 4) | (dec % 10); }

static bool ds3231_read_regs(uint8_t reg, uint8_t *buf, size_t len) {
    if (i2c_write_blocking_until(RTC_I2C, RTC_I2C_ADDR, &reg, 1, true,
            make_timeout_time_ms(100)) < 0)
        return false;
    return i2c_read_blocking_until(RTC_I2C, RTC_I2C_ADDR, buf, len, false,
            make_timeout_time_ms(100)) >= 0;
}

static bool ds3231_write_regs(uint8_t reg, const uint8_t *data, size_t len) {
    uint8_t buf[8];
    buf[0] = reg;
    for (size_t i = 0; i < len && i < sizeof(buf) - 1; i++)
        buf[i + 1] = data[i];
    return i2c_write_blocking_until(RTC_I2C, RTC_I2C_ADDR, buf, len + 1, false,
            make_timeout_time_ms(100)) >= 0;
}

void ds3231_init(void) {
    i2c_init(RTC_I2C, RTC_I2C_BAUD);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SDA);
    gpio_pull_up(PIN_I2C_SCL);

    /* Disable SQW output, enable battery-backed oscillator */
    uint8_t ctrl = 0x04; /* INTCN=1, no alarms */
    ds3231_write_regs(0x0E, &ctrl, 1);
}

bool ds3231_read_time(rtc_time_t *t) {
    uint8_t buf[7];
    if (!ds3231_read_regs(0x00, buf, 7))
        return false;
    t->second = bcd_to_dec(buf[0] & 0x7F);
    t->minute = bcd_to_dec(buf[1] & 0x7F);
    if (buf[2] & 0x40) {
        /* 12-hour mode (e.g. set by other firmware): bit 5 = PM */
        uint8_t h12 = bcd_to_dec(buf[2] & 0x1F) % 12;
        t->hour = (uint8_t)(h12 + ((buf[2] & 0x20) ? 12 : 0));
    } else {
        t->hour = bcd_to_dec(buf[2] & 0x3F);
    }
    t->dow    = buf[3] & 0x07;
    t->day    = bcd_to_dec(buf[4] & 0x3F);
    t->month  = bcd_to_dec(buf[5] & 0x1F);
    t->year   = 2000 + bcd_to_dec(buf[6]);
    return true;
}

bool ds3231_lost_power(void) {
    uint8_t status;
    if (!ds3231_read_regs(0x0F, &status, 1)) return false;
    return (status & 0x80) != 0;   /* OSF: oscillator stopped since last set */
}

bool ds3231_write_time(const rtc_time_t *t) {
    uint8_t buf[7];
    buf[0] = dec_to_bcd(t->second);
    buf[1] = dec_to_bcd(t->minute);
    buf[2] = dec_to_bcd(t->hour);          /* bit 6 clear = 24-hour mode */
    buf[3] = t->dow;
    buf[4] = dec_to_bcd(t->day);
    buf[5] = dec_to_bcd(t->month);
    buf[6] = dec_to_bcd((uint8_t)(t->year - 2000));
    if (!ds3231_write_regs(0x00, buf, 7)) return false;

    /* Clear OSF so ds3231_lost_power() reports the time as valid again. */
    uint8_t status;
    if (ds3231_read_regs(0x0F, &status, 1)) {
        status &= (uint8_t)~0x80;
        ds3231_write_regs(0x0F, &status, 1);
    }
    return true;
}

float ds3231_read_temperature(void) {
    uint8_t buf[2];
    if (!ds3231_read_regs(0x11, buf, 2))
        return -999.0f;
    int16_t raw = ((int16_t)buf[0] << 8) | buf[1];
    return (float)(raw >> 6) * 0.25f;
}
