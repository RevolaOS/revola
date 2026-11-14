/* SPDX-License-Identifier: GPL-3.0-only */

/* Pixel work on the RGB565 framebuffer a frame callback is handed. Every call
 * clips to the framebuffer and takes coordinates outside it; a NULL
 * framebuffer or pixels draws nothing. */

#ifndef RV_APP_DRAW_H
#define RV_APP_DRAW_H

#include "rv_app.h"

#ifdef __cplusplus
extern "C" {
#endif

enum rv_app_align { RV_APP_ALIGN_LEFT = -1, RV_APP_ALIGN_CENTER, RV_APP_ALIGN_RIGHT };

/* RGB565 pixels, rows `stride_pixels` apart; pixels equal to `transparent`
 * are skipped, -1 draws every pixel. */
struct rv_app_bitmap {
    const uint16_t *pixels;
    uint32_t width, height, stride_pixels;
    int32_t transparent;
};

void rv_app_clear(const struct rv_app_framebuffer *fb, uint16_t color);
void rv_app_fill_rect(const struct rv_app_framebuffer *fb, int x, int y, int width, int height,
                      uint16_t color);
/* The 1 px outline just inside the rectangle. */
void rv_app_draw_rect(const struct rv_app_framebuffer *fb, int x, int y, int width, int height,
                      uint16_t color);
void rv_app_draw_pixel(const struct rv_app_framebuffer *fb, int x, int y, uint16_t color);
/* Both ends included. */
void rv_app_draw_line(const struct rv_app_framebuffer *fb, int x0, int y0, int x1, int y1,
                      uint16_t color);
void rv_app_fill_triangle(const struct rv_app_framebuffer *fb, int x0, int y0, int x1, int y1,
                          int x2, int y2, uint16_t color);
void rv_app_draw_circle(const struct rv_app_framebuffer *fb, int cx, int cy, int radius,
                        uint16_t color);
void rv_app_fill_circle(const struct rv_app_framebuffer *fb, int cx, int cy, int radius,
                        uint16_t color);

void rv_app_draw_bitmap(const struct rv_app_framebuffer *fb, int x, int y,
                        const struct rv_app_bitmap *bitmap);
/* The `width` by `height` part of the bitmap from (source_x, source_y), cut
 * to the bitmap. */
void rv_app_draw_bitmap_part(const struct rv_app_framebuffer *fb, int x, int y,
                             const struct rv_app_bitmap *bitmap, int source_x, int source_y,
                             int width, int height);
/* One bit per pixel, most significant bit first, each row padded to a byte;
 * set bits are drawn in `color`. */
void rv_app_draw_mono_bitmap(const struct rv_app_framebuffer *fb, int x, int y,
                             const uint8_t *bits, unsigned width, unsigned height,
                             uint16_t color);

/* Text in the SDK's 5 x 7 font (digits, upper-case letters, '-', '+' and ':';
 * lower-case letters are drawn in upper case, other characters leave their
 * cell empty), every font pixel `scale` pixels wide and high. A glyph cell is
 * 6 x 7 font pixels; the last glyph has no trailing gap. */
void rv_app_draw_text(const struct rv_app_framebuffer *fb, int x, int y, const char *text,
                      uint16_t color, unsigned scale);
/* The same text placed by `align` across the `width` pixels from `x`. */
void rv_app_draw_text_in(const struct rv_app_framebuffer *fb, int x, int y, unsigned width,
                         const char *text, uint16_t color, unsigned scale,
                         enum rv_app_align align);
unsigned rv_app_text_width(const char *text, unsigned scale);

#ifdef __cplusplus
}
#endif
#endif
