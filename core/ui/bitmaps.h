/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_BITMAPS_H
#define CORE_BITMAPS_H
#include "loader/loader.h"
#include "rv_api.h"

struct core_screen;

/* The BMap resource of an extension's menu bitmap, valid until Apple asks for
 * another one or the extension unloads; NULL when it is missing or not in the
 * stock pane icon format. */
const void *core_bitmap(struct rv_extension_record *owner, const struct rv_bitmap *bitmap);
/* A bitmap asset the canvas `screen` draws, decoded on first use and kept until
 * the screen is closed; NULL when it is missing or not a whole BMap. */
const void *core_bitmap_canvas(struct core_screen *screen, const struct rv_bitmap *bitmap);
/* 1 when the BMap's rows and palette fit its payload. */
int core_bitmap_whole(const void *bmap);
/* Frees what `screen` decoded; it was closed. */
void core_bitmaps_screen_closed(struct core_screen *screen);
/* Drops every decoded bitmap of `owner`. */
void core_bitmaps_release(struct rv_extension_record *owner);

#endif
