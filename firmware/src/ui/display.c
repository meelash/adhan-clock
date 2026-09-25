/*
 * Display Composition — implementation
 */

#include "ui/display.h"

void draw_rect(int x, int y, int w, int h, colour_t c) {
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            hub75_set_pixel(col, row, c);
}

void draw_hline(int x, int y, int w, colour_t c) {
    for (int col = x; col < x + w; col++)
        hub75_set_pixel(col, y, c);
}

void draw_border(int x, int y, int w, int h, colour_t c) {
    draw_hline(x, y, w, c);
    draw_hline(x, y + h - 1, w, c);
    for (int row = y; row < y + h; row++) {
        hub75_set_pixel(x, row, c);
        hub75_set_pixel(x + w - 1, row, c);
    }
}

void draw_progress_bar(int x, int y, int w, int h,
                       float progress, colour_t fg, colour_t bg)
{
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    int filled = (int)(progress * w);
    draw_rect(x, y, filled, h, fg);
    draw_rect(x + filled, y, w - filled, h, bg);
}

void display_dim(int shift) {
    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
            colour_t c = hub75_framebuf[y][x];
            uint8_t r = (c >> 8) & 0xF;
            uint8_t g = (c >> 4) & 0xF;
            uint8_t b = (c >> 0) & 0xF;
            r >>= shift; g >>= shift; b >>= shift;
            hub75_framebuf[y][x] = colour_rgb(r, g, b);
        }
    }
}
