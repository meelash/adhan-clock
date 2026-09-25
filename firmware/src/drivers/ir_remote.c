/*
 * IR Remote Input Driver — NEC protocol decoder
 *
 * Hardware: onboard IR receiver on PIN_IR_DATA (GP28, active-LOW output).
 *
 * NEC frame: 9 ms mark + 4.5 ms space, then 32 bits LSB-first
 * (address, ~address, command, ~command). Each bit is a 562.5 µs mark
 * followed by 562.5 µs (0) or 1687.5 µs (1) of space.
 * While a key is held the remote sends a repeat code (9 ms + 2.25 ms)
 * every ~108 ms.
 *
 * Decoding: time between consecutive FALLING edges.
 *   ≥ 12 ms  → start of new frame (9 ms + 4.5 ms)
 *   9–12 ms  → repeat code        (9 ms + 2.25 ms)
 *   ≥ 1.6 ms → bit 1
 *   < 1.6 ms → bit 0
 *
 * Repeat handling: only UP/DOWN auto-repeat, and only after the key has been
 * held ~1/3 s. OK/BACK ignore repeats entirely — previously a slightly long
 * press on OK counted as two presses, which made menus flicker open/closed.
 */

#include "drivers/ir_remote.h"
#include "drivers/buttons.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdint.h>
#include <stdbool.h>

#define NEC_FRAME_START_US   12000U
#define NEC_REPEAT_MIN_US     9000U
#define NEC_BIT1_MIN_US       1600U
#define NEC_BIT_MIN_US         500U

#define REPEATS_BEFORE_AUTOREPEAT 3     /* ~330 ms hold before auto-repeat */
#define REPEAT_VALID_MS           250   /* repeat must follow a frame/repeat */

static volatile uint32_t ir_last_fall_us  = 0;
static volatile uint32_t ir_bits          = 0;
static volatile int      ir_bit_count     = 0;
static volatile bool     ir_in_frame      = false;
static volatile uint8_t  ir_last_cmd      = 0;
static volatile bool     ir_new_command   = false;
static volatile uint32_t ir_repeat_count  = 0;   /* incremented by ISR */
static volatile uint32_t ir_last_activity_ms = 0;

static void ir_gpio_callback(uint gpio, uint32_t events) {
    if (gpio != PIN_IR_DATA) return;
    if (!(events & GPIO_IRQ_EDGE_FALL)) return;

    uint32_t now = time_us_32();
    uint32_t gap = now - ir_last_fall_us;
    ir_last_fall_us = now;

    if (gap >= NEC_FRAME_START_US) {
        ir_bits      = 0;
        ir_bit_count = 0;
        ir_in_frame  = true;
    } else if (gap >= NEC_REPEAT_MIN_US) {
        uint32_t now_ms = now / 1000;
        if (now_ms - ir_last_activity_ms < REPEAT_VALID_MS) {
            ir_repeat_count++;
            ir_last_activity_ms = now_ms;
        }
        ir_in_frame = false;
    } else if (ir_in_frame && gap >= NEC_BIT_MIN_US) {
        uint32_t bit = (gap >= NEC_BIT1_MIN_US) ? 1U : 0U;
        ir_bits |= (bit << (uint32_t)ir_bit_count);
        ir_bit_count++;

        if (ir_bit_count == 32) {
            uint8_t addr  = (uint8_t)( ir_bits        & 0xFF);
            uint8_t cmd   = (uint8_t)((ir_bits >> 16) & 0xFF);
            uint8_t cmd_n = (uint8_t)((ir_bits >> 24) & 0xFF);

            bool addr_ok = (IR_REMOTE_ADDR == 0xFF) ||
                           (addr == (uint8_t)IR_REMOTE_ADDR);
            bool cmd_ok  = ((uint8_t)(cmd ^ cmd_n) == 0xFF);

            if (addr_ok && cmd_ok) {
                ir_last_cmd     = cmd;
                ir_repeat_count = 0;
                ir_new_command  = true;
                ir_last_activity_ms = now / 1000;
            }
            ir_in_frame = false;
        }
    } else {
        ir_in_frame = false;
    }
}

void ir_remote_init(void) {
    gpio_init(PIN_IR_DATA);
    gpio_set_dir(PIN_IR_DATA, GPIO_IN);
    gpio_pull_up(PIN_IR_DATA);
    gpio_set_irq_enabled_with_callback(PIN_IR_DATA, GPIO_IRQ_EDGE_FALL,
                                       true, &ir_gpio_callback);
}

static button_id_t map_key(uint8_t cmd) {
    switch (cmd) {
    case IR_KEY_UP:   case IR_KEY_UP_ALT:   return BTN_UP;
    case IR_KEY_DOWN: case IR_KEY_DOWN_ALT: return BTN_DOWN;
    case IR_KEY_OK:   case IR_KEY_OK_ALT:   return BTN_OK;
    case IR_KEY_BACK: case IR_KEY_BACK_ALT: return BTN_BACK;
    default:                                return BTN_NONE;
    }
}

void ir_remote_poll(void) {
    static uint32_t repeats_seen = 0;

    if (ir_new_command) {
        ir_new_command = false;
        repeats_seen = 0;
        button_id_t btn = map_key(ir_last_cmd);
        if (btn != BTN_NONE) buttons_inject(btn, BTN_EVT_PRESS);
        return;
    }

    uint32_t reps = ir_repeat_count;
    if (reps == repeats_seen) return;
    repeats_seen = reps;

    button_id_t btn = map_key(ir_last_cmd);
    if ((btn == BTN_UP || btn == BTN_DOWN) && reps >= REPEATS_BEFORE_AUTOREPEAT)
        buttons_inject(btn, BTN_EVT_REPEAT);
}

uint8_t ir_remote_last_code(void) {
    return ir_last_cmd;
}
