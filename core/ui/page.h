/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_PAGE_H
#define CORE_PAGE_H
#include "rv_api.h"
#include <stdint.h>

/* Canvas pages that look like the stock lists: white, black and dim text, and
 * rows with the list's highlight. */

#define CORE_PAGE_LEFT 10
#define CORE_PAGE_ROW 25
#define CORE_PAGE_TEXT 0xff000000u
#define CORE_PAGE_DIM 0xff6b6d6bu

/* Clears the canvas to the list background. */
void core_page_clear(const struct rv_gfx *g, int width, int height);
/* `text` wrapped to `width` from (x, y), at most `lines` lines; the y below. */
int core_page_text(const struct rv_gfx *g, int x, int y, int width, uint8_t font, rv_color c,
                   const char *text, uint32_t lines);
/* A list row at `y`: the label left, the value right, highlighted or not. */
void core_page_row(const struct rv_gfx *g, int y, int width, const char *label, const char *value,
                   int highlighted);

/* Text into out..end, NUL-terminated; each returns the end. A number with
 * thousands separators; bytes as KB or MB with one decimal. */
char *core_page_number(char *out, const char *end, uint32_t value);
char *core_page_size(char *out, const char *end, uint32_t bytes);

#endif
