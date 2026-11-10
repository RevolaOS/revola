/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_GFX_H
#define CORE_GFX_H
#include "rv_api.h"
#include <stdint.h>

/* The drawing table handed to a canvas's draw callback. */
extern const struct rv_gfx core_gfx;

struct core_screen;

/* Makes `context`, bound to the canvas view, the one the table draws into,
 * with `bounds` {top, left, bottom, right} as the canvas origin, for the
 * canvas `screen`. */
void core_gfx_begin(void *context, const int32_t *bounds, struct core_screen *screen);

/* Draws the top `rows` of a BMap at (x, y) inside a canvas draw; a coverage
 * mask in colour `c`. */
void core_gfx_part(int x, int y, const void *bmap, int rows, rv_color c);

/* Repeats a BMap across `width` columns from (x, y) inside a canvas draw; the
 * last copy is cut to fit. */
void core_gfx_tile(int x, int y, int width, const void *bmap);

#endif
