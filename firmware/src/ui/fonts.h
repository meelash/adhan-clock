/*
 * Bitmap Fonts for 64×32 RGB Matrix
 *
 * Large (10×14): digits 0-9, colon — for time display
 * Small (4×6):   ASCII 32-127 — for text
 */

#ifndef UI_FONTS_H
#define UI_FONTS_H

#include <stdint.h>

/* Large font: 10 pixels wide × 14 pixels tall.
 * Each row stored as uint16_t (lower 10 bits used). */
#define FONT_LARGE_W 10
#define FONT_LARGE_H 14
#define FONT_LARGE_COLON_W 4

/* Returns the bitmap for a large digit character ('0'-'9', ':').
 * Returns NULL for unsupported characters.
 * The bitmap is FONT_LARGE_H uint16_t values; bit 9 = leftmost pixel. */
const uint16_t *font_large_glyph(char c);

/* Small font: 4 pixels wide × 6 pixels tall.
 * Each row stored as uint8_t (lower 4 bits used). */
#define FONT_SMALL_W 4
#define FONT_SMALL_H 6

/* Returns the bitmap for a small character (ASCII 32-127).
 * Returns NULL for unsupported characters.
 * Bitmap is FONT_SMALL_H uint8_t values; bit 3 = leftmost pixel. */
const uint8_t *font_small_glyph(char c);

/* Draw a large character at (x,y) using the given colour.
 * Returns the advance width (pixels used, including spacing). */
int font_draw_large(int x, int y, char c, uint16_t colour);

/* Draw a small character at (x,y). Returns advance width. */
int font_draw_small(int x, int y, char c, uint16_t colour);

/* Draw a string with the small font. Returns total width drawn. */
int font_draw_string(int x, int y, const char *str, uint16_t colour);

/* Draw a string with the large font (digits + colon only). */
int font_draw_large_string(int x, int y, const char *str, uint16_t colour);

/* Measure the pixel width of a string in small font. */
int font_measure_string(const char *str);

/* Measure the pixel width of a string in large font. */
int font_measure_large_string(const char *str);

#endif /* UI_FONTS_H */
