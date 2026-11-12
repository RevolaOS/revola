/* SPDX-License-Identifier: GPL-3.0-only */

/* The drawing table a canvas draws with: fills and text through the graphics
 * context the canvas view is drawn with, in canvas coordinates. The fonts are
 * the stock ones, built once on the first use; the colour roles are the stock
 * list colours. Bitmaps are copied from a source context on the BMap, as
 * BitmapView_Draw does; bitmap assets stay decoded while the canvas is open
 * (ui/bitmaps.c). Paragraphs, rows and the card copy the stock text pages, lists
 * and Settings > About. TODO: a colour-role site for themes. */

#include "ui/gfx.h"
#include "ui/bitmaps.h"
#include "ui/screens.h"
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
    [RV_COLOR_PARAGRAPH] = 0xff424542,
    [RV_COLOR_ACCENT_TEXT] = 0xff3185d0,
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

/* The top `rows` of a BMap; a coverage mask draws in the context's colour in
 * text mode, as a bitmap view draws it in its tint. */
static void bmap_part(int x, int y, const void *resource, int rows, rv_color c)
{
    if (!resource || !core_bitmap_whole(resource))
        return;
    const int32_t *bounds = (const int32_t *)((const uint8_t *)resource + BMap_bounds);
    if (rows > bounds[2] - bounds[0])
        rows = bounds[2] - bounds[0];
    if (rows <= 0)
        return;
    int32_t source_rect[4] = {bounds[0], bounds[1], bounds[0] + rows, bounds[3]};
    int32_t rect[4] = {top + y, left + x, top + y + rows, left + x + bounds[3] - bounds[1]};
    uint32_t storage[Bitmap_bytes / 4];
    uint32_t source[GraphicsContext_bytes / 4];
    uint16_t format = *(const uint16_t *)((const uint8_t *)resource + BMap_format);
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

static void bmap_tile(int x, int y, int width, const void *resource)
{
    if (!resource || !core_bitmap_whole(resource))
        return;
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

static int paragraph(int x, int y, int width, uint8_t font, rv_color c, const char *utf8,
                     uint32_t lines)
{
    char line[96];
    uint32_t used = 0;
    int step = line_height(font) + CORE_GFX_PARAGRAPH_SPACING;
    if (!utf8)
        return y;
    while (*utf8 && lines) {
        const char *word = utf8;
        while (*utf8 && *utf8 != ' ')
            utf8++;
        uint32_t keep = used;
        if (used && used < sizeof line - 1)
            line[used++] = ' ';
        for (const char *p = word; p < utf8 && used < sizeof line - 1; p++)
            line[used++] = *p;
        line[used] = 0;
        if (keep && measure(font, line) > width) {
            line[keep] = 0;
            text(x, y, font, c, line);
            y += step;
            lines--;
            used = 0;
            for (const char *p = word; p < utf8 && used < sizeof line - 1; p++)
                line[used++] = *p;
            line[used] = 0;
        }
        while (*utf8 == ' ')
            utf8++;
    }
    if (used && lines) {
        text(x, y, font, c, line);
        y += step;
    }
    return y;
}

/* The highlight is Apple's list gradient as the emulator shows it. */
#define ROW_TEXT_LEFT 8

static void row(int y, int width, const char *label, const char *value, int highlighted)
{
    static const uint8_t from[3] = {0x00, 0x9e, 0xde}, to[3] = {0x00, 0x69, 0xd6};
    rv_color ink = color(RV_COLOR_TEXT), detail = color(RV_COLOR_DIM);
    if (highlighted) {
        for (int i = 0; i < RV_GFX_ROW_HEIGHT; i++) {
            rv_color c = 0xff000000u;
            for (int k = 0; k < 3; k++)
                c |= (rv_color)((from[k] * (RV_GFX_ROW_HEIGHT - i) + to[k] * i) /
                                RV_GFX_ROW_HEIGHT)
                     << (16 - 8 * k);
            struct rv_rect r = {0, (int16_t)(y + i), (int16_t)width, 1};
            fill(&r, c);
        }
        ink = detail = color(RV_COLOR_HIGHLIGHT_TEXT);
    }
    int baseline = y + (RV_GFX_ROW_HEIGHT - line_height(RV_FONT_TITLE)) / 2;
    if (label)
        text(ROW_TEXT_LEFT, baseline, RV_FONT_TITLE, ink, label);
    if (value && *value)
        text(width - ROW_TEXT_LEFT - measure(RV_FONT_TITLE, value), baseline, RV_FONT_TITLE,
             detail, value);
}

/* The colours and the band come from Settings > About; the icon sits on the box
 * the Settings pane draws behind the stock icons, both without reflections. */
#define CARD_LEFT 18
#define CARD_BAND 60
#define WHITE 0xffffffffu

static void card(int width, int height, const char *left_text, const char *right_text,
                 const struct rv_bitmap *icon)
{
    static const uint8_t from[3] = {0x84, 0x96, 0xa5}, to[3] = {0x42, 0x4d, 0x52};
    for (int y = 0; y < height; y += 4) {
        int t = y * 256 / height;
        rv_color c = 0xff000000u;
        for (int i = 0; i < 3; i++)
            c |= (rv_color)((from[i] * (256 - t) + to[i] * t) >> 8) << (16 - 8 * i);
        struct rv_rect r = {0, (int16_t)y, (int16_t)width, 4};
        fill(&r, c);
    }
    if (icon) {
        const void *box = ResourceService_GetBitmap(UI_GetResourceService(),
                                                    RES_SettingsPreview_Background_Image);
        const void *glyph = core_bitmap(screen ? screen->owner : NULL, icon);
        if (box)
            bmap_part((width - 78) / 2, CARD_BAND - 39, box, 79, 0);
        if (glyph)
            bmap_part((width - 78) / 2, CARD_BAND - 39, glyph, 79, WHITE);
    }
    int y = CARD_BAND - line_height(RV_FONT_TITLE) / 2;
    if (left_text)
        text(CARD_LEFT, y, RV_FONT_TITLE, WHITE, left_text);
    if (right_text)
        text(width - CARD_LEFT - measure(RV_FONT_TITLE, right_text), y, RV_FONT_TITLE, WHITE,
             right_text);
}

static void text_in(const struct rv_rect *r, uint8_t font, rv_color c, const char *utf8,
                    int8_t align)
{
    int advance = measure(font, utf8);
    int x = align == RV_ALIGN_LEFT    ? r->x
            : align == RV_ALIGN_RIGHT ? r->x + r->width - advance
                                      : r->x + (r->width - advance) / 2;
    text(x, r->y + (r->height - line_height(font)) / 2, font, c, utf8);
}

static rv_color mix(rv_color from, rv_color to, uint8_t weight)
{
    rv_color out = 0xff000000u;
    for (unsigned int shift = 0; shift < 24; shift += 8) {
        unsigned int a = from >> shift & 0xff, b = to >> shift & 0xff;
        unsigned int value = a * (255u - weight) + b * weight + 127;
        out |= (rv_color)((value + 1 + (value >> 8)) >> 8) << shift; /* value / 255 */
    }
    return out;
}

const struct rv_gfx core_gfx = {sizeof core_gfx, color,     fill,      text,     measure,
                                 line_height,    bitmap,    bmap,      paragraph, row,
                                 bmap_part,      bmap_tile, card,      text_in,   mix};

void core_gfx_begin(void *graphics, const int32_t *bounds, struct core_screen *canvas)
{
    context = graphics;
    top = bounds[0];
    left = bounds[1];
    screen = canvas;
}
