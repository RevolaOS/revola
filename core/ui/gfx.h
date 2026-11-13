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
/* api->ui_set_colors: `roles` replace the stock colours while set, NULL
 * restores them; RV_ALREADY_BOUND while another owner holds them. */
int core_gfx_colors(const struct rv_extension_record *owner, const rv_color *roles);
/* Drops the owner's colours when they lie in [start, end), a module image about
 * to be freed, or always when `start` is NULL: the extension unloads. */
void core_gfx_release_range(const struct rv_extension_record *owner, const void *start,
                            const void *end);

#endif
