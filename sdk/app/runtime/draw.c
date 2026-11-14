/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/text.c and ui.c by lgreitis. */

#include "rv_app_draw.h"

static const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-+:";
static const uint8_t glyphs[][7] = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
    {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2},     {31, 16, 16, 30, 1, 1, 30},
    {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14},
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
    {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30},
    {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},
    {14, 4, 4, 4, 4, 4, 14},      {7, 2, 2, 2, 2, 18, 12},
    {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17},
    {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16},
    {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},
    {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},
    {0, 0, 0, 31, 0, 0, 0},       {0, 4, 4, 31, 4, 4, 0},
    {0, 4, 4, 0, 4, 4, 0},
};

static int usable(const struct rv_app_framebuffer *fb)
{
    return fb && fb->pixels;
}

static void pixel(const struct rv_app_framebuffer *fb, int x, int y, uint16_t color)
{
    if (x >= 0 && y >= 0 && x < (int)fb->width && y < (int)fb->height)
        fb->pixels[(unsigned)y * fb->stride_pixels + (unsigned)x] = color;
}

/* `width` pixels from (x, y) along the row, cut to the framebuffer. */
static void span(const struct rv_app_framebuffer *fb, int x, int y, int width, uint16_t color)
{
    if (y < 0 || y >= (int)fb->height)
        return;
    int right = x + width;
    if (x < 0)
        x = 0;
    if (right > (int)fb->width)
        right = fb->width;
    uint16_t *row = fb->pixels + (unsigned)y * fb->stride_pixels;
    for (; x < right; ++x)
        row[x] = color;
}

void rv_app_fill_rect(const struct rv_app_framebuffer *fb, int x, int y, int width, int height,
                      uint16_t color)
{
    if (!usable(fb))
        return;
    int bottom = y + height;
    if (y < 0)
        y = 0;
    if (bottom > (int)fb->height)
        bottom = fb->height;
    for (; y < bottom; ++y)
        span(fb, x, y, width, color);
}

void rv_app_clear(const struct rv_app_framebuffer *fb, uint16_t color)
{
    if (usable(fb))
        rv_app_fill_rect(fb, 0, 0, fb->width, fb->height, color);
}

void rv_app_draw_rect(const struct rv_app_framebuffer *fb, int x, int y, int width, int height,
                      uint16_t color)
{
    if (!usable(fb) || width <= 0 || height <= 0)
        return;
    span(fb, x, y, width, color);
    span(fb, x, y + height - 1, width, color);
    rv_app_fill_rect(fb, x, y + 1, 1, height - 2, color);
    rv_app_fill_rect(fb, x + width - 1, y + 1, 1, height - 2, color);
}

void rv_app_draw_pixel(const struct rv_app_framebuffer *fb, int x, int y, uint16_t color)
{
    if (usable(fb))
        pixel(fb, x, y, color);
}

void rv_app_draw_line(const struct rv_app_framebuffer *fb, int x0, int y0, int x1, int y1,
                      uint16_t color)
{
    if (!usable(fb))
        return;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        pixel(fb, x0, y0, color);
        if (x0 == x1 && y0 == y1)
            return;
        int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

/* The column where the edge from (xs, ys) to (xe, ye) crosses row y. */
static int edge(int xs, int ys, int xe, int ye, int y)
{
    return ye == ys ? xe : xs + (xe - xs) * (y - ys) / (ye - ys);
}

static void swap(int *a, int *b)
{
    int t = *a;
    *a = *b;
    *b = t;
}

static void widen(int *a, int *b, int x)
{
    if (x < *a)
        *a = x;
    if (x > *b)
        *b = x;
}

void rv_app_fill_triangle(const struct rv_app_framebuffer *fb, int x0, int y0, int x1, int y1,
                          int x2, int y2, uint16_t color)
{
    if (!usable(fb))
        return;
    if (y0 > y1) {
        swap(&x0, &x1);
        swap(&y0, &y1);
    }
    if (y1 > y2) {
        swap(&x1, &x2);
        swap(&y1, &y2);
    }
    if (y0 > y1) {
        swap(&x0, &x1);
        swap(&y0, &y1);
    }
    int top = y0 < 0 ? 0 : y0, bottom = y2 >= (int)fb->height ? (int)fb->height - 1 : y2;
    for (int y = top; y <= bottom; ++y) {
        int a = edge(x0, y0, x2, y2, y);
        int b = y < y1 ? edge(x0, y0, x1, y1, y) : edge(x1, y1, x2, y2, y);
        if (a > b)
            swap(&a, &b);
        /* A flat side lies on a vertex's row, which an edge of zero height
         * does not span. */
        if (y == y0)
            widen(&a, &b, x0);
        if (y == y1)
            widen(&a, &b, x1);
        if (y == y2)
            widen(&a, &b, x2);
        span(fb, a, y, b - a + 1, color);
    }
}

/* Midpoint circle: `outline` plots the eight points of (x, y), the fill
 * covers the rows at ±y with x and, when x is about to shrink, the rows at ±x
 * with the widest y seen there. */
static void circle(const struct rv_app_framebuffer *fb, int cx, int cy, int radius,
                   uint16_t color, int outline)
{
    if (!usable(fb) || radius < 0)
        return;
    int x = radius, y = 0, error = 1 - radius;
    while (y <= x) {
        if (outline) {
            pixel(fb, cx + x, cy + y, color);
            pixel(fb, cx - x, cy + y, color);
            pixel(fb, cx + x, cy - y, color);
            pixel(fb, cx - x, cy - y, color);
            pixel(fb, cx + y, cy + x, color);
            pixel(fb, cx - y, cy + x, color);
            pixel(fb, cx + y, cy - x, color);
            pixel(fb, cx - y, cy - x, color);
        } else {
            span(fb, cx - x, cy + y, 2 * x + 1, color);
            span(fb, cx - x, cy - y, 2 * x + 1, color);
        }
        ++y;
        if (error < 0) {
            error += 2 * y + 1;
        } else {
            if (!outline) {
                span(fb, cx - y + 1, cy + x, 2 * y - 1, color);
                span(fb, cx - y + 1, cy - x, 2 * y - 1, color);
            }
            --x;
            error += 2 * (y - x) + 1;
        }
    }
}

void rv_app_draw_circle(const struct rv_app_framebuffer *fb, int cx, int cy, int radius,
                        uint16_t color)
{
    circle(fb, cx, cy, radius, color, 1);
}

void rv_app_fill_circle(const struct rv_app_framebuffer *fb, int cx, int cy, int radius,
                        uint16_t color)
{
    circle(fb, cx, cy, radius, color, 0);
}

void rv_app_draw_bitmap_part(const struct rv_app_framebuffer *fb, int x, int y,
                             const struct rv_app_bitmap *bitmap, int source_x, int source_y,
                             int width, int height)
{
    if (!usable(fb) || !bitmap || !bitmap->pixels)
        return;
    if (source_x < 0) {
        width += source_x;
        x -= source_x;
        source_x = 0;
    }
    if (source_y < 0) {
        height += source_y;
        y -= source_y;
        source_y = 0;
    }
    if (source_x + width > (int)bitmap->width)
        width = (int)bitmap->width - source_x;
    if (source_y + height > (int)bitmap->height)
        height = (int)bitmap->height - source_y;
    if (x < 0) {
        width += x;
        source_x -= x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        source_y -= y;
        y = 0;
    }
    if (x + width > (int)fb->width)
        width = (int)fb->width - x;
    if (y + height > (int)fb->height)
        height = (int)fb->height - y;
    for (int row = 0; row < height; ++row) {
        const uint16_t *source =
            bitmap->pixels + (unsigned)(source_y + row) * bitmap->stride_pixels + source_x;
        uint16_t *target = fb->pixels + (unsigned)(y + row) * fb->stride_pixels + x;
        if (bitmap->transparent < 0)
            for (int column = 0; column < width; ++column)
                target[column] = source[column];
        else
            for (int column = 0; column < width; ++column)
                if (source[column] != bitmap->transparent)
                    target[column] = source[column];
    }
}

void rv_app_draw_bitmap(const struct rv_app_framebuffer *fb, int x, int y,
                        const struct rv_app_bitmap *bitmap)
{
    if (bitmap)
        rv_app_draw_bitmap_part(fb, x, y, bitmap, 0, 0, bitmap->width, bitmap->height);
}

void rv_app_draw_mono_bitmap(const struct rv_app_framebuffer *fb, int x, int y,
                             const uint8_t *bits, unsigned width, unsigned height,
                             uint16_t color)
{
    if (!usable(fb) || !bits)
        return;
    unsigned pitch = (width + 7) / 8;
    for (unsigned row = 0; row < height; ++row, bits += pitch)
        for (unsigned column = 0; column < width; ++column)
            if (bits[column / 8] & (0x80u >> (column % 8)))
                pixel(fb, x + (int)column, y + (int)row, color);
}

static const uint8_t *glyph(char c)
{
    if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    for (const char *at = alphabet; *at; ++at)
        if (*at == c)
            return glyphs[at - alphabet];
    return NULL;
}

void rv_app_draw_text(const struct rv_app_framebuffer *fb, int x, int y, const char *text,
                      uint16_t color, unsigned scale)
{
    if (!usable(fb) || !text || !scale)
        return;
    int cell = 6 * (int)scale, size = (int)scale;
    if (y >= (int)fb->height || y + 7 * size <= 0)
        return;
    for (; *text && x < (int)fb->width; ++text, x += cell) {
        const uint8_t *rows = x + 5 * size > 0 ? glyph(*text) : NULL;
        if (!rows)
            continue;
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column)
                if (rows[row] & (16u >> column))
                    rv_app_fill_rect(fb, x + column * size, y + row * size, size, size, color);
    }
}

unsigned rv_app_text_width(const char *text, unsigned scale)
{
    unsigned length = 0;
    while (text && text[length])
        ++length;
    return length ? (length * 6 - 1) * scale : 0;
}

void rv_app_draw_text_in(const struct rv_app_framebuffer *fb, int x, int y, unsigned width,
                         const char *text, uint16_t color, unsigned scale,
                         enum rv_app_align align)
{
    int slack = (int)width - (int)rv_app_text_width(text, scale);
    if (align == RV_APP_ALIGN_CENTER)
        x += slack / 2;
    else if (align == RV_APP_ALIGN_RIGHT)
        x += slack;
    rv_app_draw_text(fb, x, y, text, color, scale);
}

/* Midpoint arcs as in circle(); each row of the top and bottom bands is one
 * span from the left arc to the right one. */
void rv_app_fill_round_rect(const struct rv_app_framebuffer *fb, int x, int y, int width,
                            int height, int radius, uint16_t color)
{
    if (!usable(fb) || width <= 0 || height <= 0)
        return;
    int limit = ((width < height ? width : height) - 1) / 2;
    if (radius > limit)
        radius = limit;
    if (radius <= 0) {
        rv_app_fill_rect(fb, x, y, width, height, color);
        return;
    }
    int left = x + radius, right = x + width - 1 - radius;
    int top = y + radius, bottom = y + height - 1 - radius;
    rv_app_fill_rect(fb, x, top, width, bottom - top + 1, color);
    int dx = radius, dy = 0, error = 1 - radius;
    while (dy <= dx) {
        span(fb, left - dx, top - dy, right - left + 1 + 2 * dx, color);
        span(fb, left - dx, bottom + dy, right - left + 1 + 2 * dx, color);
        span(fb, left - dy, top - dx, right - left + 1 + 2 * dy, color);
        span(fb, left - dy, bottom + dx, right - left + 1 + 2 * dy, color);
        ++dy;
        if (error < 0) {
            error += 2 * dy + 1;
        } else {
            --dx;
            error += 2 * (dy - dx) + 1;
        }
    }
}

void rv_app_draw_round_rect(const struct rv_app_framebuffer *fb, int x, int y, int width,
                            int height, int radius, uint16_t color)
{
    if (!usable(fb) || width <= 0 || height <= 0)
        return;
    int limit = ((width < height ? width : height) - 1) / 2;
    if (radius > limit)
        radius = limit;
    if (radius <= 0) {
        rv_app_draw_rect(fb, x, y, width, height, color);
        return;
    }
    int left = x + radius, right = x + width - 1 - radius;
    int top = y + radius, bottom = y + height - 1 - radius;
    span(fb, left + 1, y, right - left - 1, color);
    span(fb, left + 1, y + height - 1, right - left - 1, color);
    rv_app_fill_rect(fb, x, top + 1, 1, bottom - top - 1, color);
    rv_app_fill_rect(fb, x + width - 1, top + 1, 1, bottom - top - 1, color);
    int dx = radius, dy = 0, error = 1 - radius;
    while (dy <= dx) {
        pixel(fb, left - dx, top - dy, color);
        pixel(fb, right + dx, top - dy, color);
        pixel(fb, left - dx, bottom + dy, color);
        pixel(fb, right + dx, bottom + dy, color);
        pixel(fb, left - dy, top - dx, color);
        pixel(fb, right + dy, top - dx, color);
        pixel(fb, left - dy, bottom + dx, color);
        pixel(fb, right + dy, bottom + dx, color);
        ++dy;
        if (error < 0) {
            error += 2 * dy + 1;
        } else {
            --dx;
            error += 2 * (dy - dx) + 1;
        }
    }
}
