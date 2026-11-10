/* SPDX-License-Identifier: GPL-3.0-only */

/* Canvas pages in the look of the stock lists. The highlight is Apple's list
 * gradient as the emulator shows it. */

#include "ui/page.h"
#include "ui/resources.h"
#include <stddef.h>

#define WHITE 0xffffffffu
#define ROW_TEXT_LEFT 8

void core_page_clear(const struct rv_gfx *g, int width, int height)
{
    struct rv_rect all = {0, 0, (int16_t)width, (int16_t)height};
    g->fill(&all, WHITE);
}

int core_page_text(const struct rv_gfx *g, int x, int y, int width, uint8_t font, rv_color c,
                   const char *text, uint32_t lines)
{
    char line[96];
    uint32_t used = 0;
    int step = g->line_height(font) + 3;
    while (*text && lines) {
        const char *word = text;
        while (*text && *text != ' ')
            text++;
        uint32_t keep = used;
        if (used && used < sizeof line - 1)
            line[used++] = ' ';
        for (const char *p = word; p < text && used < sizeof line - 1; p++)
            line[used++] = *p;
        line[used] = 0;
        if (keep && g->measure(font, line) > width) {
            line[keep] = 0;
            g->text(x, y, font, c, line);
            y += step;
            lines--;
            used = 0;
            for (const char *p = word; p < text && used < sizeof line - 1; p++)
                line[used++] = *p;
            line[used] = 0;
        }
        while (*text == ' ')
            text++;
    }
    if (used && lines) {
        g->text(x, y, font, c, line);
        y += step;
    }
    return y;
}

void core_page_row(const struct rv_gfx *g, int y, int width, const char *label, const char *value,
                   int highlighted)
{
    static const uint8_t top[3] = {0x00, 0x9e, 0xde}, bottom[3] = {0x00, 0x69, 0xd6};
    rv_color text = CORE_PAGE_TEXT, detail = CORE_PAGE_DIM;
    if (highlighted) {
        for (int i = 0; i < CORE_PAGE_ROW; i++) {
            rv_color c = 0xff000000u;
            for (int k = 0; k < 3; k++)
                c |= (rv_color)((top[k] * (CORE_PAGE_ROW - i) + bottom[k] * i) / CORE_PAGE_ROW)
                     << (16 - 8 * k);
            struct rv_rect r = {0, (int16_t)(y + i), (int16_t)width, 1};
            g->fill(&r, c);
        }
        text = detail = WHITE;
    }
    int baseline = y + (CORE_PAGE_ROW - g->line_height(RV_FONT_TITLE)) / 2;
    g->text(ROW_TEXT_LEFT, baseline, RV_FONT_TITLE, text, label);
    if (value && *value)
        g->text(width - ROW_TEXT_LEFT - g->measure(RV_FONT_TITLE, value), baseline, RV_FONT_TITLE,
                detail, value);
}

char *core_page_number(char *out, const char *end, uint32_t value)
{
    char digits[14];
    int n = 0;
    do {
        if (n && n % 4 == 3)
            digits[n++] = ',';
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n && out < end - 1)
        *out++ = digits[--n];
    *out = 0;
    return out;
}

char *core_page_size(char *out, const char *end, uint32_t bytes)
{
    int mega = bytes >= 1024 * 1024;
    uint32_t unit = mega ? 1024 * 1024 : 1024;
    uint32_t tenths = (bytes * 10 + unit / 2) / unit; /* sizes below 400 MB */
    out = core_page_number(out, end, tenths / 10);
    char fraction[] = {'.', (char)('0' + tenths % 10), ' ', mega ? 'M' : 'K', 'B', 0};
    return core_put_text(out, end, fraction);
}
