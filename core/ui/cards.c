/* SPDX-License-Identifier: GPL-3.0-only */

/* About-style pages: the colours and the band come from Settings > About, the
 * icon is the Extensions icon on the box the Settings pane draws behind it. */

#include "ui/cards.h"
#include "osos-2.0.5.h"
#include "ui/bitmaps.h"
#include "ui/gfx.h"
#include "ui/resources.h"
#include <stddef.h>

#define LEFT 18
#define BAND_MIDDLE 60
#define WHITE 0xffffffffu

static const struct rv_bitmap icon = {"extensions"};

/* Light at the top and dark at the bottom, as the About screen. */
static void background(const struct rv_gfx *g, int width, int height)
{
    static const uint8_t top[3] = {0x84, 0x96, 0xa5}, bottom[3] = {0x42, 0x4d, 0x52};
    for (int y = 0; y < height; y += 4) {
        int t = y * 256 / height;
        rv_color c = 0xff000000u;
        for (int i = 0; i < 3; i++)
            c |= (rv_color)((top[i] * (256 - t) + bottom[i] * t) >> 8) << (16 - 8 * i);
        struct rv_rect r = {0, (int16_t)y, (int16_t)width, 4};
        g->fill(&r, c);
    }
}

void core_cards_frame(const struct rv_gfx *g, int width, int height, const char *left,
                      const char *right)
{
    background(g, width, height);
    /* The icon on its box, both without their reflections. */
    const void *box = ResourceService_GetBitmap(UI_GetResourceService(),
                                                RES_SettingsPreview_Background_Image);
    const void *glyph = core_bitmap(NULL, &icon);
    if (box)
        core_gfx_part((width - 78) / 2, BAND_MIDDLE - 39, box, 79, 0);
    if (glyph)
        core_gfx_part((width - 78) / 2, BAND_MIDDLE - 39, glyph, 79, WHITE);
    int y = BAND_MIDDLE - g->line_height(RV_FONT_TITLE) / 2;
    g->text(LEFT, y, RV_FONT_TITLE, WHITE, left);
    g->text(width - LEFT - g->measure(RV_FONT_TITLE, right), y, RV_FONT_TITLE, WHITE, right);
}
