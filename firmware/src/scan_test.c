/*
 * scan_test.c — definitive HUB75 E-line test using production-style refresh
 *
 * This test intentionally uses the same "row pair" strategy as the main
 * firmware (16 address rows, upper+lower data channels), then alternates:
 *
 *   Phase A: force E=0 for all rows
 *   Phase B: force E=1 for all rows
 *
 * Interpretation:
 * - If image is identical in both phases -> panel behaves as 1/16-scan (E unused)
 * - If image changes dramatically between phases -> panel uses E (1/32-scan style)
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdint.h>

#define W 64
#define H 32
#define SCAN_ROWS 16
#define COLOR_BITS 4

/* Waveshare Pico-RGB-Matrix-P3-64x32 pinout */
#define PIN_R1   2
#define PIN_G1   3
#define PIN_B1   4
#define PIN_R2   5
#define PIN_G2   8
#define PIN_B2   9
#define PIN_A    10
#define PIN_B    16
#define PIN_C    18
#define PIN_D    20
#define PIN_E    22
#define PIN_CLK  11
#define PIN_LAT  12
#define PIN_OE   13

typedef uint16_t colour_t; /* 0x0RGB, 4 bits per channel */

static colour_t fb[H][W];

static inline colour_t rgb4(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF) << 8) | ((g & 0xF) << 4) | (b & 0xF);
}

static const colour_t bands[8] = {
    /* 8 bands x 4 rows = 32 rows */
    0x0F00, /* red */
    0x00F0, /* green */
    0x000F, /* blue */
    0x0FF0, /* yellow */
    0x00FF, /* cyan */
    0x0F0F, /* magenta */
    0x0FFF, /* white */
    0x0888  /* gray */
};

static void init_pins(void) {
    const uint8_t pins[] = {
        PIN_R1, PIN_G1, PIN_B1,
        PIN_R2, PIN_G2, PIN_B2,
        PIN_A, PIN_B, PIN_C, PIN_D, PIN_E,
        PIN_CLK, PIN_LAT, PIN_OE
    };
    for (int i = 0; i < (int)(sizeof(pins) / sizeof(pins[0])); i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);
        gpio_put(pins[i], 0);
    }
    gpio_put(PIN_OE, 1);
}

static inline void set_row_addr(uint8_t row, bool force_e_high) {
    gpio_put(PIN_A, (row >> 0) & 1);
    gpio_put(PIN_B, (row >> 1) & 1);
    gpio_put(PIN_C, (row >> 2) & 1);
    gpio_put(PIN_D, (row >> 3) & 1);
    gpio_put(PIN_E, force_e_high ? 1 : 0);
}

static inline void shift_pixel(colour_t upper, colour_t lower, uint8_t bit) {
    uint8_t mask = (uint8_t)(1u << bit);

    gpio_put(PIN_R1, (((upper >> 8) & 0xF) & mask) ? 1 : 0);
    gpio_put(PIN_G1, (((upper >> 4) & 0xF) & mask) ? 1 : 0);
    gpio_put(PIN_B1, (((upper >> 0) & 0xF) & mask) ? 1 : 0);
    gpio_put(PIN_R2, (((lower >> 8) & 0xF) & mask) ? 1 : 0);
    gpio_put(PIN_G2, (((lower >> 4) & 0xF) & mask) ? 1 : 0);
    gpio_put(PIN_B2, (((lower >> 0) & 0xF) & mask) ? 1 : 0);

    gpio_put(PIN_CLK, 1);
    __asm volatile("nop\nnop\nnop\n");
    gpio_put(PIN_CLK, 0);
}

static void refresh_once(bool force_e_high) {
    const uint8_t brightness = 8;

    for (uint8_t bit = 0; bit < COLOR_BITS; bit++) {
        uint32_t on_us = (uint32_t)(1u << bit) * brightness;

        for (uint8_t row = 0; row < SCAN_ROWS; row++) {
            gpio_put(PIN_OE, 1);

            for (uint8_t col = 0; col < W; col++) {
                shift_pixel(fb[row][col], fb[row + SCAN_ROWS][col], bit);
            }

            gpio_put(PIN_LAT, 1);
            __asm volatile("nop\nnop\n");
            gpio_put(PIN_LAT, 0);

            set_row_addr(row, force_e_high);

            gpio_put(PIN_OE, 0);
            busy_wait_us(on_us);
            gpio_put(PIN_OE, 1);
        }
    }
}

static void fill_test_pattern(void) {
    for (int y = 0; y < H; y++) {
        colour_t c = bands[y / 4];
        for (int x = 0; x < W; x++) {
            fb[y][x] = c;
        }
    }

    /* Vertical white markers help detect spatial remapping quickly. */
    for (int y = 0; y < H; y++) {
        fb[y][7]  = rgb4(15, 15, 15);
        fb[y][31] = rgb4(15, 15, 15);
        fb[y][55] = rgb4(15, 15, 15);
    }
}

static void set_phase_marker(bool force_e_high) {
    /*
     * Make phase changes obvious to the eye:
     * - E=0 phase: top-left 2x2 marker is RED
     * - E=1 phase: top-left 2x2 marker is GREEN
     */
    colour_t marker = force_e_high ? rgb4(0, 15, 0) : rgb4(15, 0, 0);
    fb[0][0] = marker;
    fb[0][1] = marker;
    fb[1][0] = marker;
    fb[1][1] = marker;
}

int main(void) {
    stdio_init_all();
    init_pins();
    fill_test_pattern();

    /* ~2 second per phase */
    const uint32_t phase_ms = 2000;

    while (true) {
        absolute_time_t t0 = get_absolute_time();
        set_phase_marker(false);
        while (absolute_time_diff_us(t0, get_absolute_time()) < (int64_t)phase_ms * 1000) {
            refresh_once(false);
        }

        absolute_time_t t1 = get_absolute_time();
        set_phase_marker(true);
        while (absolute_time_diff_us(t1, get_absolute_time()) < (int64_t)phase_ms * 1000) {
            refresh_once(true);
        }
    }
}
