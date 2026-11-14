/* SPDX-License-Identifier: GPL-3.0-only */

/* Bitmap assets as BMap resources for Apple.
 *
 * Pane bitmaps: an icon in the stock Settings pane format, a 78 x 121 coverage
 * mask (BMapFormat_Mask8) drawn in the view's colour, a picture, a whole BMap
 * whose size names the pane that shows it, and the badge shown beside one.
 * Only the ones shown are decoded: Apple keeps the pane bitmap's pointer until
 * it asks for the next one, and draws it again while a row without a pane is
 * selected. A replaced block is retired, not freed, so a draw still under way
 * finds its pixels.
 *
 * Canvas bitmaps: decoded on first draw and kept while their canvas is open,
 * so redraws decode nothing; freed when it closes. One stored raw in the
 * preloaded block is lent from there while no provider has a say over it. Canvases copy
 * pixels while drawing, so these blocks are freed at once.
 *
 * An asset provider, one extension's say over every bitmap asset, is asked
 * before a decode and offered its result.
 *
 * Every block is counted to its extension; nothing is allocated while no
 * bitmap is shown. */

#include "ui/bitmaps.h"
#include "ui/icons.h"
#include "ui/panes.h"
#include "loader/lz.h"
#include "loader/rvx.h"
#include "osos-2.0.5.h"
#include "settings.h"
#include "ui/screens.h"
#include <stddef.h>

#define CANVAS_HELD 8
#define ICON_WIDTH 78
#define ICON_HEIGHT 121
#define ICON_STRIDE 80
#define ICON_BYTES (BMap_payload + ICON_STRIDE * ICON_HEIGHT)

/* The pane's picture and its badge, shown at the same time. */
enum { PICTURE, BADGE, SLOTS };
static struct {
    struct rv_extension_record *owner;
    uint32_t name;
    uint32_t bytes;
    void *block;
} decoded[SLOTS];

struct held {
    struct core_screen *screen;
    struct rv_extension_record *owner; /* the block is counted to it */
    uint32_t name;
    void *block;
};

static struct held held[CANVAS_HELD]; /* most recent first */

static const struct rv_asset_provider *provider;
static struct rv_extension_record *provider_owner;

/* Exactly the header of a stock pane icon. */
static int icon_bmap(const uint8_t *b, uint32_t bytes)
{
    const int32_t *bounds = (const int32_t *)(b + BMap_bounds);
    return bytes == ICON_BYTES && *(const uint16_t *)(b + BMap_format) == BMapFormat_Mask8 &&
           *(const uint16_t *)(b + BMap_stride) == ICON_STRIDE &&
           *(const uint16_t *)(b + BMap_depth) == 8 && !bounds[0] && !bounds[1] &&
           bounds[2] == ICON_HEIGHT && bounds[3] == ICON_WIDTH &&
           *(const uint32_t *)(b + BMap_payload_bytes) == ICON_STRIDE * ICON_HEIGHT;
}

/* A BMap whose header and rows fit `bytes`. */
static int whole_bmap(const uint8_t *b, uint32_t bytes)
{
    return bytes >= BMap_payload && core_bitmap_whole(b) &&
           *(const uint32_t *)(b + BMap_payload_bytes) <= bytes - BMap_payload;
}

/* The hash a provider knows the owner of an asset by; the core's is the hash
 * of "revola". */
static uint32_t owner_hash(struct rv_extension_record *owner)
{
    static uint32_t core;
    if (owner)
        return owner->owner;
    if (!core)
        core = core_settings_owner("revola");
    return core;
}

int core_bitmaps_replacement(uint32_t owner, uint32_t name, struct rv_asset_stream *stream)
{
    return provider && provider->replacement && provider->replacement_read &&
           provider->replacement(owner, name, stream) && stream->bytes >= BMap_payload;
}

int core_bitmaps_replacement_read(uint32_t owner, uint32_t name, void *out, uint32_t stored)
{
    return provider && provider->replacement_read &&
           provider->replacement_read(owner, name, out, stored);
}

/* A stored bitmap, raw or an lz stream, decoded into a block of `bytes`
 * counted to the provider's extension; NULL when it is not a whole BMap. */
static void *decode_stored(const void *stored, uint8_t codec, uint32_t stored_bytes, uint32_t bytes)
{
    uint8_t *block = core_loader_allocate(provider_owner, bytes);
    if (!block)
        return NULL;
    int ok = 0;
    if (codec == RV_ASSET_RAW && stored_bytes == bytes) {
        const uint8_t *in = stored;
        for (uint32_t i = 0; i < bytes; i++)
            block[i] = in[i];
        ok = 1;
    } else if (codec == RV_ASSET_LZ) {
        struct core_lz_input in;
        core_lz_from_memory(&in, stored, stored_bytes);
        ok = core_lz_decode(&in, block, bytes);
    }
    if (ok && whole_bmap(block, bytes))
        return block;
    core_loader_free(provider_owner, block);
    return NULL;
}

/* The provider's bitmap for asset `name` of `owner`, decoded into a block
 * counted to the provider's extension: from the owner's preloaded block when
 * it sits there, else read from the provider; NULL when it has none. */
static void *replacement(struct rv_extension_record *owner, uint32_t hash, uint32_t name,
                         uint32_t *bytes)
{
    const struct rvx_entry *entry;
    const void *memory = owner ? core_loader_preloaded(owner, name, &entry) : NULL;
    if (memory && (entry->reserved & CORE_PRELOADED_PROVIDED)) {
        *bytes = entry->bytes;
        return decode_stored(memory, entry->codec, entry->stored, entry->bytes);
    }
    struct rv_asset_stream stream;
    if (!core_bitmaps_replacement(hash, name, &stream))
        return NULL;
    uint8_t *stored = core_loader_allocate(provider_owner, stream.stored);
    void *block = NULL;
    if (stored && core_bitmaps_replacement_read(hash, name, stored, stream.stored))
        block = decode_stored(stored, stream.codec, stream.stored, stream.bytes);
    if (stored)
        core_loader_free(provider_owner, stored);
    *bytes = stream.bytes;
    return block;
}

/* The bitmap asset `name` of `owner` as a block of *bytes, counted to
 * *charged: the provider's replacement, counted to the provider's extension,
 * else the owner's own; either decoded and offered to the provider to adjust.
 * NULL when there is none. */
static void *fetch(struct rv_extension_record *owner, uint32_t name, uint32_t *bytes,
                   struct rv_extension_record **charged)
{
    uint32_t hash = owner_hash(owner);
    uint8_t *block = replacement(owner, hash, name, bytes);
    if (block) {
        *charged = provider_owner;
        if (provider && provider->adjust)
            provider->adjust(hash, name, block, *bytes);
        return block;
    }
    uint8_t kind = RVX_BITMAP;
    *bytes = ICON_BYTES;
    *charged = owner;
    block = owner ? core_loader_asset(owner, name, bytes, &kind) : core_icon_decode(name);
    if (!block)
        return NULL;
    if (kind != RVX_BITMAP || !whole_bmap(block, *bytes)) {
        core_loader_free(owner, block);
        return NULL;
    }
    if (provider && provider->adjust)
        provider->adjust(hash, name, block, *bytes);
    return block;
}

static void drop_held(uint32_t i)
{
    core_loader_free(held[i].owner, held[i].block);
    held[i].block = NULL;
}

void core_bitmaps_release_range(struct rv_extension_record *owner, const void *start,
                                const void *end)
{
    int inside = !start || ((const void *)provider >= start && (const void *)provider < end);
    if (provider_owner == owner && inside)
        core_bitmaps_provider(owner, NULL); /* its bitmaps for every owner go with it */
}

int core_bitmaps_provider(struct rv_extension_record *owner, const struct rv_asset_provider *p)
{
    if (!p && !provider_owner)
        return RV_OK;
    if (provider_owner && provider_owner != owner)
        return RV_ALREADY_BOUND;
    provider = p;
    provider_owner = p ? owner : NULL;
    /* Everything decoded is fetched again: the pane icon on its next show, a
     * canvas's bitmaps on its next draw (it copies pixels while drawing, so a
     * held block may go between draws). The preloaded blocks follow at the
     * next tick, which refreshes the pane again. */
    for (uint32_t i = 0; i < SLOTS; i++)
        if (decoded[i].block) {
            core_loader_retire(decoded[i].block);
            decoded[i].block = NULL;
        }
    for (uint32_t i = 0; i < CANVAS_HELD; i++)
        if (held[i].block)
            drop_held(i);
    core_loader_preload_refresh();
    core_panes_refresh();
    return RV_OK;
}

/* The decoded block of `bitmap` in `slot`: with `icon` a pane icon, else any
 * whole BMap. */
static const void *shown(struct rv_extension_record *owner, const struct rv_bitmap *bitmap, int icon,
                         uint32_t slot)
{
    if (!bitmap || !bitmap->asset)
        return NULL;
    uint32_t name = core_settings_owner(bitmap->asset);
    if (!(decoded[slot].block && decoded[slot].owner == owner && decoded[slot].name == name)) {
        uint32_t bytes;
        struct rv_extension_record *charged;
        void *block = fetch(owner, name, &bytes, &charged);
        if (!block)
            return NULL;
        if (decoded[slot].block)
            core_loader_retire(decoded[slot].block);
        decoded[slot].owner = owner;
        decoded[slot].name = name;
        decoded[slot].bytes = bytes;
        decoded[slot].block = block;
    }
    return !icon || icon_bmap(decoded[slot].block, decoded[slot].bytes) ? decoded[slot].block : NULL;
}

const void *core_bitmap_icon(struct rv_extension_record *owner, const struct rv_bitmap *bitmap)
{
    return shown(owner, bitmap, 1, PICTURE);
}

const void *core_bitmap_picture(struct rv_extension_record *owner, const struct rv_bitmap *bitmap)
{
    return shown(owner, bitmap, 0, PICTURE);
}

const void *core_bitmap_badge(struct rv_extension_record *owner, const struct rv_bitmap *bitmap)
{
    return shown(owner, bitmap, 0, BADGE);
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

const void *core_bitmap_canvas(struct core_screen *screen, const struct rv_bitmap *bitmap)
{
    struct rv_extension_record *owner = screen ? screen->owner : NULL;
    if (!owner || !bitmap || !bitmap->asset)
        return NULL;
    uint32_t name = core_settings_owner(bitmap->asset);
    const struct rvx_entry *entry;
    const uint8_t *memory = core_loader_preloaded(owner, name, &entry);
    if (memory && provider && !(entry->reserved & CORE_PRELOADED_PROVIDED))
        memory = NULL; /* the provider may adjust it, so it is decoded into a copy */
    if (memory && entry->codec == RVX_RAW && entry->kind == RVX_BITMAP)
        return whole_bmap(memory, entry->bytes) ? memory : NULL;
    uint32_t i = 0;
    while (i < CANVAS_HELD && !(held[i].block && held[i].screen == screen && held[i].name == name))
        i++;
    if (i == CANVAS_HELD) {
        uint32_t bytes;
        struct rv_extension_record *charged;
        uint8_t *block = fetch(owner, name, &bytes, &charged);
        if (!block)
            return NULL;
        i = CANVAS_HELD - 1;
        if (held[i].block)
            drop_held(i);
        held[i].screen = screen;
        held[i].owner = charged;
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
    core_bitmaps_release_range(owner, NULL, NULL);
    for (uint32_t i = 0; i < SLOTS; i++)
        if (decoded[i].block && decoded[i].owner == owner) {
            core_loader_retire(decoded[i].block);
            decoded[i].block = NULL;
        }
    for (uint32_t i = 0; i < CANVAS_HELD; i++)
        if (held[i].block && (held[i].screen->owner == owner || held[i].owner == owner))
            drop_held(i);
}
