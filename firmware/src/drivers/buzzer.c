/*
 * Buzzer — implementation
 *
 * Active buzzer (BUZZER_PASSIVE 0): the pin is held high for the beep.
 * Passive buzzer (BUZZER_PASSIVE 1): a BUZZER_FREQ_HZ square wave via PWM.
 */

#include "drivers/buzzer.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#define BUZZER_USABLE (PIN_BUZZER <= 29 && PIN_BUZZER != PIN_SD_MOSI)

#if BUZZER_USABLE

static bool     beeping;
static uint32_t beep_end_ms;

static void buzzer_on(bool on) {
#if BUZZER_PASSIVE
    /* PWM runs continuously; level 0 = pin held low (silent) */
    uint32_t wrap = pwm_hw->slice[pwm_gpio_to_slice_num(PIN_BUZZER)].top;
    pwm_set_gpio_level(PIN_BUZZER, on ? (uint16_t)((wrap + 1) / 2) : 0);
#else
    gpio_put(PIN_BUZZER, on);
#endif
}

void buzzer_init(void) {
#if BUZZER_PASSIVE
    gpio_set_function(PIN_BUZZER, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_BUZZER);
    pwm_config cfg = pwm_get_default_config();
    /* 1 MHz PWM clock, wrap for the requested tone */
    pwm_config_set_clkdiv(&cfg, (float)clock_get_hz(clk_sys) / 1000000.0f);
    pwm_config_set_wrap(&cfg, (uint16_t)(1000000u / BUZZER_FREQ_HZ - 1));
    pwm_init(slice, &cfg, true);
#else
    gpio_init(PIN_BUZZER);
    gpio_set_dir(PIN_BUZZER, GPIO_OUT);
#endif
    buzzer_on(false);
    beeping = false;
}

bool buzzer_available(void) { return true; }

void buzzer_beep(uint16_t ms) {
    beep_end_ms = to_ms_since_boot(get_absolute_time()) + ms;
    beeping = true;
    buzzer_on(true);
}

void buzzer_poll(void) {
    if (beeping && (int32_t)(to_ms_since_boot(get_absolute_time()) - beep_end_ms) >= 0) {
        buzzer_on(false);
        beeping = false;
    }
}

#else  /* buzzer pin shared with SD MOSI */

void buzzer_init(void) {}
bool buzzer_available(void) { return false; }
void buzzer_beep(uint16_t ms) { (void)ms; }
void buzzer_poll(void) {}

#endif
