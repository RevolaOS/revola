/* SPDX-License-Identifier: GPL-3.0-only */

/* The drawing table a canvas draws with: fills and text through the graphics
 * context the canvas view is drawn with, in canvas coordinates. The fonts are
 * the stock ones, built once on the first use; the colour roles are the stock
 * list colours. TODO: bitmaps, and a colour-role site for themes. */

#include "ui/gfx.h"
#include "osos-2.0.5.h"
#include <stddef.h>

static void *context;
static int32_t top, left;

/* Stock colours of the list screens, as the emulator shows them; the highlight
 * is the middle of Apple's gradient. */
static const rv_color roles[RV_COLOR_ROLE_COUNT] = {
    [RV_COLOR_BACKGROUND] = 0xffffffff,
    [RV_COLOR_TEXT] = 0xff000000,
    [RV_COLOR_HIGHLIGHT] = 0xff0084dc,
    [RV_COLOR_HIGHLIGHT_TEXT] = 0xffffffff,
    [RV_COLOR_DIM] = 0xff6b6d6b,
};

/* Helvetica in the firmware's size units: the status bar's RES_Font_BoldSmall,
 * the Legal page's RES_Font_RegularSmall, the preview caption's RES_Font_Bold. */
static const struct {
    uint8_t size, bold;
} faces[RV_FONT_COUNT] = {[RV_FONT_TITLE] = {0x3a, 1}, [RV_FONT_TEXT] = {0x3a, 0}, [RV_FONT_LARGE] = {0x4d, 1}};
static uint32_t fonts[RV_FONT_COUNT][2];
static uint8_t fonts_built;

static rv_color color(uint8_t role)
{
    return role < RV_COLOR_ROLE_COUNT ? roles[role] : roles[RV_COLOR_TEXT];
}

static void set_color(rv_color c)
{
    uint8_t rgba[4] = {(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c, (uint8_t)(c >> 24)};
    GraphicsContext_SetColor(context, rgba);
}

static void fill(const struct rv_rect *r, rv_color c)
{
    int32_t rect[4] = {top + r->y, left + r->x, top + r->y + r->height, left + r->x + r->width};
    set_color(c);
    GraphicsContext_SetDrawMode(context, 0);
    GraphicsContext_FillRect(context, rect);
}

static void use_font(uint8_t font)
{
    if (font >= RV_FONT_COUNT)
        font = RV_FONT_TEXT;
    if (!fonts_built) {
        for (uint32_t i = 0; i < RV_FONT_COUNT; i++)
            Font_Construct(fonts[i], "Helvetica", faces[i].size, faces[i].bold);
        fonts_built = 1;
    }
    GraphicsContext_SetFont(context, fonts[font]);
}

/* The pen is the baseline: the glyph top lands at `y`. */
static void text(int x, int y, uint8_t font, rv_color c, const char *utf8)
{
    use_font(font);
    set_color(c);
    /* A fresh context is in mode 0, which fills but draws no text. */
    GraphicsContext_SetDrawMode(context, GraphicsDrawMode_Text);
    GraphicsContext_SetTextPen(context, left + x, top + y + GraphicsContext_GetFontAscent(context));
    GraphicsContext_DrawUtf8(context, utf8);
}

static int measure(uint8_t font, const char *utf8)
{
    use_font(font);
    return GraphicsContext_MeasureUtf8(context, utf8);
}

static int line_height(uint8_t font)
{
    use_font(font);
    return GraphicsContext_GetFontAscent(context) + GraphicsContext_GetFontDescent(context);
}

const struct rv_gfx core_gfx = {sizeof core_gfx, color, fill, text, measure, line_height};

void core_gfx_begin(void *graphics, const int32_t *bounds)
{
    context = graphics;
    top = bounds[0];
    left = bounds[1];
}
