/* SPDX-License-Identifier: GPL-3.0-only */

/* Bitmap assets as BMap resources for Apple.
 *
 * Menu bitmaps: the stock Settings pane icon format, a 78 x 121 coverage mask
 * (BMapFormat_Mask8) drawn in the view's colour. Only the one shown is decoded:
 * Apple keeps the pane bitmap's pointer until it asks for the next one, and
 * draws it again while a row without a pane is selected. A replaced block is
 * retired, not freed, so a draw still under way finds its pixels.
 *
 * Canvas bitmaps: decoded on first draw and kept while their canvas is open,
 * so redraws decode nothing; freed when it closes. One stored raw in the
 * resident block is lent from there. Canvases copy pixels while drawing, so
 * these blocks are freed at once.
 *
 * Every block is counted to its extension; nothing is allocated while no
 * bitmap is shown. */

#include "ui/bitmaps.h"
#include "loader/rvx.h"
#include "osos-2.0.5.h"
#include "settings.h"
#include "ui/screens.h"
#include <stddef.h>

#define CANVAS_HELD 8
#define MENU_WIDTH 78
#define MENU_HEIGHT 121
#define MENU_STRIDE 80
#define MENU_BYTES (BMap_payload + MENU_STRIDE * MENU_HEIGHT)

static struct {
    struct rv_extension_record *owner;
    uint32_t name;
    void *block;
} decoded;

struct held {
    struct core_screen *screen;
    uint32_t name;
    void *block;
};

static struct held held[CANVAS_HELD]; /* most recent first */

/* Exactly the header of a stock pane icon. */
static int menu_bmap(const uint8_t *b, uint32_t bytes)
{
    const int32_t *bounds = (const int32_t *)(b + BMap_bounds);
    return bytes == MENU_BYTES && *(const uint16_t *)(b + BMap_format) == BMapFormat_Mask8 &&
           *(const uint16_t *)(b + BMap_stride) == MENU_STRIDE &&
           *(const uint16_t *)(b + BMap_depth) == 8 && !bounds[0] && !bounds[1] &&
           bounds[2] == MENU_HEIGHT && bounds[3] == MENU_WIDTH &&
           *(const uint32_t *)(b + BMap_payload_bytes) == MENU_STRIDE * MENU_HEIGHT;
}

const void *core_bitmap(struct rv_extension_record *owner, const struct rv_bitmap *bitmap)
{
    if (!owner || !bitmap || !bitmap->asset)
        return NULL;
    uint32_t name = core_settings_owner(bitmap->asset);
    if (decoded.block && decoded.owner == owner && decoded.name == name)
        return decoded.block;
    uint32_t bytes;
    uint8_t kind;
    void *block = core_loader_asset(owner, name, &bytes, &kind);
    if (!block)
        return NULL;
    if (kind != RVX_BITMAP || !menu_bmap(block, bytes)) {
        core_loader_free(owner, block);
        return NULL;
    }
    if (decoded.block)
        core_loader_retire(decoded.block);
    decoded.owner = owner;
    decoded.name = name;
    decoded.block = block;
    return block;
}

int core_bitmap_whole(const void *bmap)
{
    const uint8_t *b = bmap;
    const int32_t *bounds = (const int32_t *)(b + BMap_bounds);
    uint32_t stride = *(const uint16_t *)(b + BMap_stride);
    uint32_t depth = *(const uint16_t *)(b + BMap_depth);
    uint32_t payload = *(const uint32_t *)(b + BMap_payload_bytes);
    uint32_t height = (uint32_t)(bounds[2] - bounds[0]);
    uint32_t width = (uint32_t)(bounds[3] - bounds[1]);
    uint32_t palette = 0;
    if (*(const uint16_t *)(b + BMap_format) == BMapFormat_Indexed8) {
        uint32_t colors = payload >= 4 ? *(const uint32_t *)(b + BMap_payload) : 0x10000;
        palette = colors <= 256 ? 4 + 4 * colors : payload + 1;
    }
    return bounds[2] >= bounds[0] && bounds[3] >= bounds[1] && height <= 0xffff &&
           width <= 0xffff && depth <= 32 && width * depth <= stride * 8 && palette <= payload &&
           height * stride <= payload - palette;
}

/* A bitmap asset whose BMap fits its `bytes`. */
static int canvas_bmap(const uint8_t *b, uint32_t bytes, uint8_t kind)
{
    return kind == RVX_BITMAP && bytes >= BMap_payload && core_bitmap_whole(b) &&
           *(const uint32_t *)(b + BMap_payload_bytes) <= bytes - BMap_payload;
}

static void drop_held(uint32_t i)
{
    core_loader_free(held[i].screen->owner, held[i].block);
    held[i].block = NULL;
}

const void *core_bitmap_canvas(struct core_screen *screen, const struct rv_bitmap *bitmap)
{
    struct rv_extension_record *owner = screen ? screen->owner : NULL;
    if (!owner || !bitmap || !bitmap->asset)
        return NULL;
    uint32_t name = core_settings_owner(bitmap->asset);
    const struct rvx_entry *entry;
    const uint8_t *memory = core_loader_resident(owner, name, &entry);
    if (memory && entry->codec == RVX_RAW)
        return canvas_bmap(memory, entry->bytes, entry->kind) ? memory : NULL;
    uint32_t i = 0;
    while (i < CANVAS_HELD && !(held[i].block && held[i].screen == screen && held[i].name == name))
        i++;
    if (i == CANVAS_HELD) {
        uint32_t bytes;
        uint8_t kind;
        uint8_t *block = core_loader_asset(owner, name, &bytes, &kind);
        if (!block)
            return NULL;
        if (!canvas_bmap(block, bytes, kind)) {
            core_loader_free(owner, block);
            return NULL;
        }
        i = CANVAS_HELD - 1;
        if (held[i].block)
            drop_held(i);
        held[i].screen = screen;
        held[i].name = name;
        held[i].block = block;
    }
    for (; i; i--) {
        struct held swap = held[i];
        held[i] = held[i - 1];
        held[i - 1] = swap;
    }
    return held[0].block;
}

void core_bitmaps_screen_closed(struct core_screen *screen)
{
    for (uint32_t i = 0; i < CANVAS_HELD; i++)
        if (held[i].block && held[i].screen == screen)
            drop_held(i);
}

void core_bitmaps_release(struct rv_extension_record *owner)
{
    if (decoded.block && decoded.owner == owner) {
        core_loader_retire(decoded.block);
        decoded.block = NULL;
    }
    for (uint32_t i = 0; i < CANVAS_HELD; i++)
        if (held[i].block && held[i].screen->owner == owner)
            drop_held(i);
}
