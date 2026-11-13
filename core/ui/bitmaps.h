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
/* Drops every decoded bitmap of `owner`, and its asset provider. */
void core_bitmaps_release(struct rv_extension_record *owner);
/* Drops the owner's asset provider when it lies in [start, end), a module
 * image or block about to be freed, or always when `start` is NULL. */
void core_bitmaps_release_range(struct rv_extension_record *owner, const void *start,
                                const void *end);
/* api->asset_set_provider: `provider` is asked before every bitmap asset the core
 * decodes and offered the result, NULL asks nobody; RV_ALREADY_BOUND while
 * another owner holds it. What is on screen is decoded again. */
int core_bitmaps_provider(struct rv_extension_record *owner,
                          const struct rv_asset_provider *provider);
/* The loader's questions to the provider for a preloaded block: whether it
 * holds a bitmap for asset `name` of the extension hashed `owner`, and its
 * stored bytes. 0 with no provider. */
int core_bitmaps_replacement(uint32_t owner, uint32_t name, struct rv_asset_stream *stream);
int core_bitmaps_replacement_read(uint32_t owner, uint32_t name, void *out, uint32_t stored);

#endif
