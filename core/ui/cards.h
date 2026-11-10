/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_CARDS_H
#define CORE_CARDS_H
#include "rv_api.h"
#include <stdint.h>

/* Pages in the style of Settings > About, drawn on a canvas: the background
 * and a band with a value on each side of the Extensions icon. */

/* Draws the background and the band. */
void core_cards_frame(const struct rv_gfx *g, int width, int height, const char *left,
                      const char *right);

#endif
