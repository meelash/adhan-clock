/*
 * Display Composition — framebuffer drawing primitives
 */

#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include <stdint.h>
#include "drivers/hub75.h"

/* Draw a filled rectangle */
void draw_rect(int x, int y, int w, int h, colour_t c);

/* Draw a horizontal line */
void draw_hline(int x, int y, int w, colour_t c);

/* Draw a 1px border rectangle */
void draw_border(int x, int y, int w, int h, colour_t c);

/* Draw a progress bar (filled portion: 0.0–1.0) */
void draw_progress_bar(int x, int y, int w, int h,
                       float progress, colour_t fg, colour_t bg);

/* Dim the entire framebuffer by shifting colour channels right */
void display_dim(int shift);

#endif /* UI_DISPLAY_H */
