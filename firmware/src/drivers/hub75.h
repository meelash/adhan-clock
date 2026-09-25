/*
 * HUB75 RGB Matrix Driver (64×32, 1/16 scan, P3)
 * Adapted from Waveshare Pico-RGB-Matrix demo.
 *
 * Uses Binary Code Modulation (BCM) for COLOR_DEPTH_BITS bits per channel.
 * Display is refreshed continuously from Core 1.
 *
 * Drawing model: draw into hub75_framebuf, then call hub75_present(). Nothing
 * appears on the panel until present, so partially drawn frames never show.
 */

#ifndef DRIVER_HUB75_H
#define DRIVER_HUB75_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/* Pixel colour – 4 bits per channel packed into uint16_t: 0x0RGB */
typedef uint16_t colour_t;

static inline colour_t colour_rgb(uint8_t r4, uint8_t g4, uint8_t b4) {
    return (uint16_t)((r4 & 0xF) << 8) | ((g4 & 0xF) << 4) | (b4 & 0xF);
}

/* Convenience palette (4-bit channel values) */
#define COL_BLACK       0x000
#define COL_WHITE       0xFFF
#define COL_RED         0xF00
#define COL_GREEN       0x0F0
#define COL_BLUE        0x00F
#define COL_YELLOW      0xFF0
#define COL_CYAN        0x0FF
#define COL_MAGENTA     0xF0F
#define COL_ORANGE      0xF80
#define COL_PURPLE      0x80F
#define COL_AMBER       0xFA0
#define COL_TEAL        0x0AA
#define COL_INDIGO      0x408
#define COL_GOLD        0xFC0
#define COL_DIM_WHITE   0x444
#define COL_DIM_BLUE    0x004

/* Frame buffer: DISPLAY_WIDTH × DISPLAY_HEIGHT pixels, 12-bit colour */
extern colour_t hub75_framebuf[DISPLAY_HEIGHT][DISPLAY_WIDTH];

void hub75_init(void);

/* Start the display refresh on Core 1. Call from core 0. */
void hub75_start_refresh(void);

/* Publish hub75_framebuf to the panel (swapped in at the next frame
 * boundary). Cheap if the frame is unchanged. */
void hub75_present(void);

/* Set global brightness (1–16).  0 = use auto-brightness value. */
void hub75_set_brightness(uint8_t level);
uint8_t hub75_get_brightness(void);

/* Set a single pixel (bounds-checked) */
static inline void hub75_set_pixel(int x, int y, colour_t c) {
    if ((unsigned)x < DISPLAY_WIDTH && (unsigned)y < DISPLAY_HEIGHT)
        hub75_framebuf[y][x] = c;
}

/* Clear entire framebuffer */
void hub75_clear(colour_t fill);

#endif /* DRIVER_HUB75_H */
