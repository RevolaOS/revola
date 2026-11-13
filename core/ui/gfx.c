/* SPDX-License-Identifier: GPL-3.0-only */

/* The drawing table a canvas draws with: fills and text through the graphics
 * context the canvas view is drawn with, in canvas coordinates. The fonts are
 * the stock ones, built once on the first use; the colour roles are the stock
 * list colours. Bitmaps are copied from a source context on the BMap, as
 * BitmapView_Draw does; bitmap assets stay decoded while the canvas is open
 * (ui/bitmaps.c). Paragraphs, rows and the card copy the stock text pages, lists
 * and Settings > About. An extension may replace the colour roles (ui_set_colors),
 * which is how a theme reaches every canvas. */

#include "ui/gfx.h"
#include "ui/bitmaps.h"
#include "ui/screens.h"
#include "osos-2.0.5.h"
#include <stddef.h>

static void *context;
static int32_t top, left, width, height;
static struct core_screen *screen;
static struct rv_rect clip_rect;

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

static const rv_color *colors;
static const struct rv_extension_record *colors_owner;

int core_gfx_colors(const struct rv_extension_record *owner, const rv_color *roles)
{
    if (colors_owner && colors_owner != owner)
        return RV_ALREADY_BOUND;
    colors = roles;
    colors_owner = roles ? owner : NULL;
    return RV_OK;
}

void core_gfx_release_range(const struct rv_extension_record *owner, const void *start,
                            const void *end)
{
    int inside = !start || ((const void *)colors >= start && (const void *)colors < end);
    if (colors_owner == owner && inside)
        colors = NULL, colors_owner = NULL;
}

static rv_color color(uint8_t role)
{
    const rv_color *table = colors ? colors : roles;
    return role < RV_COLOR_ROLE_COUNT ? table[role] : table[RV_COLOR_TEXT];
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
    GraphicsContext_SetDrawMode(context, (c >> 24) == 0xff ? GraphicsDrawMode_Copy
                                                           : GraphicsDrawMode_Blend);
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

/* Copies the top `rows` of a BMap (all of it when 0) to (x, y) through a
 * source context on it, at `alpha`/255 of its opacity. */
static void copy_bmap(int x, int y, const uint8_t *resource, int rows, uint32_t alpha)
{
    const int32_t *bounds = (const int32_t *)(resource + BMap_bounds);
    if (!rows || rows > bounds[2] - bounds[0])
        rows = bounds[2] - bounds[0];
    if (rows <= 0)
        return;
    int32_t source_rect[4] = {bounds[0], bounds[1], bounds[0] + rows, bounds[3]};
    int32_t rect[4] = {top + y, left + x, top + y + rows, left + x + bounds[3] - bounds[1]};
    uint32_t storage[Bitmap_bytes / 4];
    uint32_t source[GraphicsContext_bytes / 4];
    Bitmap_Construct(storage);
    GraphicsContext_InitFromBitmapResource(source, storage, resource);
    GraphicsContext_CopyRect(context, source, source_rect, rect, alpha, 0);
    GraphicsContext_Destroy(source);
    Bitmap_Destroy(storage);
}

static void draw_bmap(int x, int y, const uint8_t *resource)
{
    copy_bmap(x, y, resource, 0, 0xff);
}

/* The top `rows` of a BMap; a coverage mask draws in the context's colour in
 * text mode, as a bitmap view draws it in its tint. */
static void bmap_part(int x, int y, const void *resource, int rows, rv_color c)
{
    if (!resource || !core_bitmap_whole(resource) || rows <= 0)
        return;
    uint16_t format = *(const uint16_t *)((const uint8_t *)resource + BMap_format);
    if (format == BMapFormat_Mask8 || format == BMapFormat_Mask4) {
        set_color(c);
        GraphicsContext_SetDrawMode(context, GraphicsDrawMode_Text);
    }
    copy_bmap(x, y, resource, rows, 0xff);
}

static void bmap_blend(int x, int y, const void *resource, int rows, uint8_t alpha)
{
    if (!resource || !core_bitmap_whole(resource) || rows <= 0)
        return;
    uint16_t format = *(const uint16_t *)((const uint8_t *)resource + BMap_format);
    if (format == BMapFormat_Mask8 || format == BMapFormat_Mask4)
        return;
    copy_bmap(x, y, resource, rows, alpha);
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

/* The context's clip is in surface coordinates: the view's, moved by the
 * origin. Back in canvas coordinates and cut to the canvas; empty when the
 * two do not meet. */
static const struct rv_rect *clip(void)
{
    const int32_t *c = (const int32_t *)((const uint8_t *)context + GraphicsContext_clip_top);
    int32_t dx = RV_FIELD(context, GraphicsContext_origin_x, int32_t) + left;
    int32_t dy = RV_FIELD(context, GraphicsContext_origin_y, int32_t) + top;
    int32_t y0 = c[0] - dy, x0 = c[1] - dx, y1 = c[2] - dy, x1 = c[3] - dx;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > width)
        x1 = width;
    if (y1 > height)
        y1 = height;
    clip_rect.x = (int16_t)x0;
    clip_rect.y = (int16_t)y0;
    clip_rect.width = (int16_t)(x1 > x0 ? x1 - x0 : 0);
    clip_rect.height = (int16_t)(y1 > y0 ? y1 - y0 : 0);
    return &clip_rect;
}

const struct rv_gfx core_gfx = {sizeof core_gfx, color,     fill,      text,     measure,
                                 line_height,    bitmap,    bmap,      paragraph, row,
                                 bmap_part,      bmap_tile, card,      text_in,   mix,
                                 clip,           bmap_blend};

void core_gfx_begin(void *graphics, const int32_t *bounds, struct core_screen *canvas)
{
    context = graphics;
    top = bounds[0];
    left = bounds[1];
    height = bounds[2] - bounds[0];
    width = bounds[3] - bounds[1];
    screen = canvas;
}
