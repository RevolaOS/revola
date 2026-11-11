/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_GFX_H
#define CORE_GFX_H
#include "rv_api.h"
#include <stdint.h>

#define CORE_GFX_PARAGRAPH_SPACING 3

/* The drawing table handed to a canvas's draw callback. */
extern const struct rv_gfx core_gfx;

struct core_screen;

/* Makes `context`, bound to the canvas view, the one the table draws into,
 * with `bounds` {top, left, bottom, right} as the canvas origin, for the
 * canvas `screen`. */
void core_gfx_begin(void *context, const int32_t *bounds, struct core_screen *screen);

#endif
