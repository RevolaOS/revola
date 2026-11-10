/* SPDX-License-Identifier: GPL-3.0-only */

/* The drawing table a canvas draws with: fills and text through the graphics
 * context the canvas view is drawn with, in canvas coordinates. The fonts are
 * the stock ones, built once on the first use; the colour roles are the stock
 * list colours. Bitmaps are copied from a source context on the BMap, as
 * BitmapView_Draw does; bitmap assets stay decoded while the canvas is open
 * (ui/bitmaps.c). TODO: a colour-role site for themes. */

#include "ui/gfx.h"
#include "ui/bitmaps.h"
#include "osos-2.0.5.h"
#include <stddef.h>

static void *context;
static int32_t top, left;
static struct core_screen *screen;

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

/* Copies a BMap to (x, y) through a source context on it. */
static void draw_bmap(int x, int y, const uint8_t *resource)
{
    const int32_t *bounds = (const int32_t *)(resource + BMap_bounds);
    int32_t rect[4] = {top + y, left + x, top + y + bounds[2] - bounds[0],
                       left + x + bounds[3] - bounds[1]};
    uint32_t storage[Bitmap_bytes / 4];
    uint32_t source[GraphicsContext_bytes / 4];
    Bitmap_Construct(storage);
    GraphicsContext_InitFromBitmapResource(source, storage, resource);
    GraphicsContext_CopyRect(context, source, bounds, rect, 0xff, 0);
    GraphicsContext_Destroy(source);
    Bitmap_Destroy(storage);
}

void core_gfx_part(int x, int y, const void *resource, int rows, rv_color c)
{
    const int32_t *bounds = (const int32_t *)((const uint8_t *)resource + BMap_bounds);
    int32_t source_rect[4] = {bounds[0], bounds[1], bounds[0] + rows, bounds[3]};
    int32_t rect[4] = {top + y, left + x, top + y + rows, left + x + bounds[3] - bounds[1]};
    uint32_t storage[Bitmap_bytes / 4];
    uint32_t source[GraphicsContext_bytes / 4];
    uint16_t format = *(const uint16_t *)((const uint8_t *)resource + BMap_format);
    /* A coverage mask draws in the context's colour in text mode, as a bitmap
     * view draws it in its tint. */
    if (format == BMapFormat_Mask8 || format == BMapFormat_Mask4) {
        set_color(c);
        GraphicsContext_SetDrawMode(context, GraphicsDrawMode_Text);
    }
    Bitmap_Construct(storage);
    GraphicsContext_InitFromBitmapResource(source, storage, resource);
    GraphicsContext_CopyRect(context, source, source_rect, rect, 0xff, 0);
    GraphicsContext_Destroy(source);
    Bitmap_Destroy(storage);
}

void core_gfx_tile(int x, int y, int width, const void *resource)
{
    const int32_t *bounds = (const int32_t *)((const uint8_t *)resource + BMap_bounds);
    int32_t step = bounds[3] - bounds[1], rows = bounds[2] - bounds[0];
    uint32_t storage[Bitmap_bytes / 4];
    uint32_t source[GraphicsContext_bytes / 4];
    if (step <= 0)
        return;
    Bitmap_Construct(storage);
    GraphicsContext_InitFromBitmapResource(source, storage, resource);
    for (int at = 0; at < width; at += step) {
        int32_t columns = width - at < step ? width - at : step;
        int32_t source_rect[4] = {bounds[0], bounds[1], bounds[2], bounds[1] + columns};
        int32_t rect[4] = {top + y, left + x + at, top + y + rows, left + x + at + columns};
        GraphicsContext_CopyRect(context, source, source_rect, rect, 0xff, 0);
    }
    GraphicsContext_Destroy(source);
    Bitmap_Destroy(storage);
}

static void bitmap(int x, int y, const struct rv_bitmap *asset)
{
    const uint8_t *resource = core_bitmap_canvas(screen, asset);
    if (resource)
        draw_bmap(x, y, resource);
}

static void bmap(int x, int y, const void *resource)
{
    if (resource && core_bitmap_whole(resource))
        draw_bmap(x, y, resource);
}

const struct rv_gfx core_gfx = {sizeof core_gfx, color, fill, text, measure, line_height,
                                 bitmap, bmap};

void core_gfx_begin(void *graphics, const int32_t *bounds, struct core_screen *canvas)
{
    context = graphics;
    top = bounds[0];
    left = bounds[1];
    screen = canvas;
}
