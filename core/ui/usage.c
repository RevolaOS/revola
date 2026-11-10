/* SPDX-License-Identifier: GPL-3.0-only */

/* Settings > Extensions > Usage: a page in the style of Settings > About
 * (cards.c) with the memory used and free on the band, then a bar drawn from
 * the About screen's capacity pieces and a legend with each part's size. The
 * parts are Apple's heap use (mostly the music library), the playback cache
 * (the block cache, carved from the heap), the core (its image and the heap it
 * holds, Apple's copies of its resources included), the extensions (likewise,
 * for as long as they are loaded) and the free heap. Figures are read on every
 * draw. */

#include "ui/usage.h"
#include "loader/loader.h"
#include "osos-2.0.5.h"
#include "ui/cards.h"
#include "ui/gfx.h"
#include "ui/page.h"
#include "ui/resources.h"
#include "ui/screens.h"
#include <stddef.h>

extern char __core_start[], __core_end[];

static uint32_t heap_word(uint32_t field)
{
    return *(uint32_t *)((uint8_t *)MainHeap + field);
}

static uint32_t active_count(void)
{
    uint32_t count = 0;
    for (struct rv_extension_record *r = core_extensions; r; r = r->next)
        count += r->failure == CORE_EXTENSION_OK && !r->pending_unload;
    return count;
}

const char *core_usage_active(char *out, const char *end)
{
    core_put_text(core_page_number(out, end, active_count()), end, " active");
    return out;
}

/* The bar: a left cap, a stretched fill and a right cap per colour set, 16 px
 * caps as the About screen draws them. */
#define BAR_LEFT 14
#define BAR_TOP 112
#define CAP 16
#define MIN_PART 3 /* the core and the extensions are a few KB of 50 MB */
#define LEGEND_LEFT 18
#define LEGEND_TOP 150
#define LEGEND_STEP 17
#define WHITE 0xffffffffu

enum { APPLE, PLAYBACK, CORE, EXTENSIONS, FREE, PARTS };

static const struct {
    const char *label;
    uint32_t pieces, swatch;
} parts[PARTS] = {
    [APPLE] = {"Library and system", RES_Capacity_Audio_Segment, RES_Capacity_Audio_Swatch_Bitmap},
    [PLAYBACK] = {"Playback cache", RES_Capacity_Other_Segment, RES_Capacity_Other_Swatch_Bitmap},
    [CORE] = {"Core", RES_Capacity_Video_Segment, RES_Capacity_Video_Swatch_Bitmap},
    [EXTENSIONS] = {"Extensions", RES_Capacity_Photos_Segment, RES_Capacity_Photos_Swatch_Bitmap},
    [FREE] = {NULL, RES_Capacity_Free_Segment, 0},
};

static const void *piece(uint32_t id)
{
    return ResourceService_GetBitmap(UI_GetResourceService(), id);
}

static int rows_of(const void *bmap)
{
    const int32_t *bounds = (const int32_t *)((const uint8_t *)bmap + BMap_bounds);
    return bounds[2] - bounds[0];
}

static void measure_parts(uint32_t *bytes)
{
    uint32_t used = heap_word(MainHeap_used_bytes);
    bytes[CORE] = (uint32_t)(__core_end - __core_start) + core_heap_bytes;
    bytes[EXTENSIONS] = core_loader_total_bytes();
    /* Each block also has a 16-byte list node. */
    bytes[PLAYBACK] = BlockCache_GetTotalBlocks() * (BlockCache_GetBlockBytes() + 16);
    uint32_t ours = bytes[EXTENSIONS] + bytes[PLAYBACK] + core_heap_bytes;
    bytes[APPLE] = used > ours ? used - ours : 0;
    bytes[FREE] = heap_word(MainHeap_free_bytes);
}

/* Widths in proportion, at least MIN_PART for a part that is there and a cap
 * for the parts at the ends; the largest part gives up what that takes. */
static void layout_parts(const uint32_t *bytes, int width, int *widths)
{
    uint32_t total = 0;
    for (int i = 0; i < PARTS; i++)
        total += bytes[i] / 1024;
    int sum = 0, largest = 0;
    for (int i = 0; i < PARTS; i++) {
        int minimum = i == APPLE || i == FREE ? CAP : bytes[i] ? MIN_PART : 0;
        /* In KB, so the product fits 32 bits. */
        widths[i] = total ? (int)(bytes[i] / 1024 * (uint32_t)width / total) : 0;
        if (widths[i] < minimum)
            widths[i] = minimum;
        sum += widths[i];
        if (widths[i] > widths[largest])
            largest = i;
    }
    widths[largest] += width - sum;
}

static void draw_bar(int width, const uint32_t *bytes)
{
    int widths[PARTS], x = BAR_LEFT;
    layout_parts(bytes, width - 2 * BAR_LEFT, widths);
    for (int i = 0; i < PARTS; i++) {
        const void *left = piece(parts[i].pieces), *fill = piece(parts[i].pieces + 1),
                   *right = piece(parts[i].pieces + 2);
        if (!left || !fill || !right || !widths[i])
            continue;
        int from = x, to = x + widths[i];
        if (i == APPLE) {
            core_gfx_part(from, BAR_TOP, left, rows_of(left), 0);
            from += CAP;
        }
        if (i == FREE)
            to -= CAP;
        core_gfx_tile(from, BAR_TOP, to - from, fill);
        if (i == FREE)
            core_gfx_part(to, BAR_TOP, right, rows_of(right), 0);
        x += widths[i];
    }
}

/* One line per part but free: its swatch, its name and its size. */
static void draw_legend(const struct rv_gfx *g, int width, const uint32_t *bytes)
{
    int text_height = g->line_height(RV_FONT_TITLE);
    for (int i = 0; i < FREE; i++) {
        int y = LEGEND_TOP + LEGEND_STEP * i;
        const void *swatch = piece(parts[i].swatch);
        if (swatch)
            core_gfx_part(LEGEND_LEFT, y + (text_height - rows_of(swatch)) / 2, swatch,
                          rows_of(swatch), 0);
        char size[16];
        core_page_size(size, size + sizeof size, bytes[i]);
        g->text(LEGEND_LEFT + 20, y, RV_FONT_TITLE, WHITE, parts[i].label);
        g->text(width - LEGEND_LEFT - g->measure(RV_FONT_TITLE, size), y, RV_FONT_TITLE, WHITE,
                size);
    }
}

static void draw(const struct rv_gfx *g, int width, int height)
{
    uint32_t bytes[PARTS];
    char used[24], available[24];
    measure_parts(bytes);
    core_put_text(core_page_size(used, used + sizeof used,
                                  bytes[APPLE] + bytes[CORE] + bytes[EXTENSIONS] + bytes[PLAYBACK]),
                  used + sizeof used, " Used");
    core_put_text(core_page_size(available, available + sizeof available, bytes[FREE]),
                  available + sizeof available, " Free");
    core_cards_frame(g, width, height, used, available);
    draw_bar(width, bytes);
    draw_legend(g, width, bytes);
}

static int input(const struct rv_input *in)
{
    return in->key != RV_KEY_MENU && in->kind == RV_INPUT_RELEASE; /* Menu pops it */
}

static const struct rv_screen screen = {
    .name = "core.usage", .title = "Memory Usage", .kind = RV_SCREEN_CANVAS,
    .canvas = {.draw = draw, .input = input},
};

void core_usage_register(void)
{
    core_screens_register(NULL, &screen, 1);
}
