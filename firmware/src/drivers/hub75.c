/*
 * HUB75 RGB Matrix Driver — implementation
 *
 * Drives a 64×32 P3 panel with 1/16 scan using GPIO bit-banging on Core 1.
 * Binary Code Modulation (BCM) provides COLOR_DEPTH_BITS bits per channel.
 *
 * Double buffering: Core 0 draws into hub75_framebuf and calls
 * hub75_present(), which converts the frame into pre-computed GPIO words
 * ("bit planes") in the back plane buffer. Core 1 swaps front/back only at a
 * frame boundary, so it never shows a half-drawn frame.
 */

#include "drivers/hub75.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include <string.h>

/* ── Frame buffers ──────────────────────────────────────────────────────── */
colour_t hub75_framebuf[DISPLAY_HEIGHT][DISPLAY_WIDTH];      /* Core 0 draws here */

typedef uint32_t plane_t[COLOR_DEPTH_BITS][DISPLAY_SCAN_ROWS][DISPLAY_WIDTH];
static plane_t planes[2];
static plane_t *volatile front = &planes[0];                 /* Core 1 shows this */
static plane_t *volatile back  = &planes[1];
static volatile bool swap_pending = false;
static colour_t last_presented[DISPLAY_HEIGHT][DISPLAY_WIDTH];
static bool have_presented = false;

static volatile uint8_t g_brightness = 8; /* 1-16 */
static bool g_refresh_started = false;

#define PINMASK(p) (1u << (p))
#define DATA_MASK (PINMASK(PIN_HUB75_R1) | PINMASK(PIN_HUB75_G1) | PINMASK(PIN_HUB75_B1) | \
                   PINMASK(PIN_HUB75_R2) | PINMASK(PIN_HUB75_G2) | PINMASK(PIN_HUB75_B2))
#define ADDR_MASK (PINMASK(PIN_HUB75_ADDR_A) | PINMASK(PIN_HUB75_ADDR_B) | \
                   PINMASK(PIN_HUB75_ADDR_C) | PINMASK(PIN_HUB75_ADDR_D))
#define CLK_MASK  PINMASK(PIN_HUB75_CLK)
#define LAT_MASK  PINMASK(PIN_HUB75_LAT)
#define OE_MASK   PINMASK(PIN_HUB75_OE)

static uint32_t row_addr_bits[DISPLAY_SCAN_ROWS];

static void hub75_gpio_init(void) {
    const uint8_t pins[] = {
        PIN_HUB75_R1, PIN_HUB75_G1, PIN_HUB75_B1,
        PIN_HUB75_R2, PIN_HUB75_G2, PIN_HUB75_B2,
        PIN_HUB75_ADDR_A, PIN_HUB75_ADDR_B, PIN_HUB75_ADDR_C, PIN_HUB75_ADDR_D,
        PIN_HUB75_CLK, PIN_HUB75_LAT, PIN_HUB75_OE,
    };
    for (unsigned i = 0; i < sizeof(pins); i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);
    }
    gpio_put(PIN_HUB75_OE, 1);   /* high = display off */
    gpio_put(PIN_HUB75_LAT, 0);
    gpio_put(PIN_HUB75_CLK, 0);

    for (int r = 0; r < DISPLAY_SCAN_ROWS; r++) {
        uint32_t v = 0;
        if (r & 1) v |= PINMASK(PIN_HUB75_ADDR_A);
        if (r & 2) v |= PINMASK(PIN_HUB75_ADDR_B);
        if (r & 4) v |= PINMASK(PIN_HUB75_ADDR_C);
        if (r & 8) v |= PINMASK(PIN_HUB75_ADDR_D);
        row_addr_bits[r] = v;
    }
}

/* ── Frame → bit planes (Core 0) ────────────────────────────────────────── */
static void build_planes(plane_t *dst) {
    for (int row = 0; row < DISPLAY_SCAN_ROWS; row++) {
        const colour_t *up = hub75_framebuf[row];
        const colour_t *lo = hub75_framebuf[row + DISPLAY_SCAN_ROWS];
        for (int col = 0; col < DISPLAY_WIDTH; col++) {
            colour_t u = up[col], l = lo[col];
            for (int bit = 0; bit < COLOR_DEPTH_BITS; bit++) {
                uint32_t v = 0;
                if (u & (0x100 << bit)) v |= PINMASK(PIN_HUB75_R1);
                if (u & (0x010 << bit)) v |= PINMASK(PIN_HUB75_G1);
                if (u & (0x001 << bit)) v |= PINMASK(PIN_HUB75_B1);
                if (l & (0x100 << bit)) v |= PINMASK(PIN_HUB75_R2);
                if (l & (0x010 << bit)) v |= PINMASK(PIN_HUB75_G2);
                if (l & (0x001 << bit)) v |= PINMASK(PIN_HUB75_B2);
                (*dst)[bit][row][col] = v;
            }
        }
    }
}

/* ── Core 1 refresh loop ───────────────────────────────────────────────── */
static void __not_in_flash_func(core1_refresh_loop)(void) {
    while (true) {
        if (swap_pending) {
            plane_t *t = front;
            front = back;
            back = t;
            __dmb();
            swap_pending = false;
        }

        uint32_t bright = g_brightness;
        if (bright == 0) bright = 8;
        const plane_t *fp = front;

        for (int bit = 0; bit < COLOR_DEPTH_BITS; bit++) {
            uint32_t on_time_us = (1u << bit) * bright;

            for (int row = 0; row < DISPLAY_SCAN_ROWS; row++) {
                const uint32_t *px = (*fp)[bit][row];

                for (int col = 0; col < DISPLAY_WIDTH; col++) {
                    /* Data + CLK low in one write, then rising edge. */
                    sio_hw->gpio_togl = (sio_hw->gpio_out ^ px[col]) & (DATA_MASK | CLK_MASK);
                    __asm volatile("nop\nnop");
                    sio_hw->gpio_set = CLK_MASK;
                    __asm volatile("nop\nnop");
                }
                sio_hw->gpio_clr = CLK_MASK;

                /* Blank, latch, select row, show for the BCM-weighted time. */
                sio_hw->gpio_set = OE_MASK;
                sio_hw->gpio_set = LAT_MASK;
                sio_hw->gpio_togl = (sio_hw->gpio_out ^ row_addr_bits[row]) & ADDR_MASK;
                __asm volatile("nop\nnop\nnop\nnop");
                sio_hw->gpio_clr = LAT_MASK;

                sio_hw->gpio_clr = OE_MASK;
                busy_wait_us_32(on_time_us);
                sio_hw->gpio_set = OE_MASK;
            }
        }
    }
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void hub75_init(void) {
    static bool gpio_done = false;
    if (!gpio_done) {
        hub75_gpio_init();
        memset(planes, 0, sizeof(planes));
        gpio_done = true;
    }
    memset(hub75_framebuf, 0, sizeof(hub75_framebuf));
}

void hub75_start_refresh(void) {
    if (g_refresh_started) return;
    g_refresh_started = true;
    multicore_launch_core1(core1_refresh_loop);
}

void hub75_present(void) {
    /* Skip identical frames — most main-loop iterations change nothing. */
    if (have_presented &&
        memcmp(last_presented, hub75_framebuf, sizeof(hub75_framebuf)) == 0)
        return;

    /* Wait until Core 1 has taken the previous back buffer. */
    while (swap_pending && g_refresh_started) tight_loop_contents();

    build_planes(back);
    memcpy(last_presented, hub75_framebuf, sizeof(hub75_framebuf));
    have_presented = true;
    __dmb();

    if (g_refresh_started) {
        swap_pending = true;
    } else {
        plane_t *t = front; front = back; back = t;
    }
}

void hub75_set_brightness(uint8_t level) {
    if (level > BRIGHTNESS_LEVELS) level = BRIGHTNESS_LEVELS;
    g_brightness = level;
}

uint8_t hub75_get_brightness(void) {
    return g_brightness;
}

void hub75_clear(colour_t fill) {
    for (int y = 0; y < DISPLAY_HEIGHT; y++)
        for (int x = 0; x < DISPLAY_WIDTH; x++)
            hub75_framebuf[y][x] = fill;
}
