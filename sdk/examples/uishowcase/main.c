/* SPDX-License-Identifier: GPL-3.0-only */

/* Every UI declaration of the SDK behind a row that names it: the row kinds
 * with the screen calls of rv_api, the screen kinds with their layouts, and the
 * menus an entry can appear in. Open it from Settings, Main, Music (after
 * Settings > Music Menu turns it on) or Extras. It declares eleven screens and
 * the core makes three more from the settings. Its assets are in
 * manifest.toml: the pane icon, the canvas bitmap and a note the canvas reads
 * back and draws. */

#include "rv_api.h"
#include "rv_libc.h"
#include "rv_menus.h"
#include "rv_ui.h"

static const struct rv_api *api;
static rv_extension_handle self;

/* Settings: one of each kind. */
#define KEY_FLAG 1
#define KEY_CHOICE 2
#define KEY_SLIDER 3
#define KEY_MULTI 4

static const char *const seasons[] = {"Spring", "Summer", "Autumn", "Winter"};
static const char *const volumes[] = {"Mute", "Quiet", "Normal", "Loud", "Max"};
static const char *const days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

static const struct rv_setting settings[] = {
    RV_SETTING_BOOL(KEY_FLAG, "Flag", 0),
    RV_SETTING_CHOICE(KEY_CHOICE, "Choice", 1, seasons),
    RV_SETTING_SLIDER(KEY_SLIDER, "Slider", 2, volumes),
    RV_SETTING_MULTI(KEY_MULTI, "Multi-choice", 0x1f, days),
};

/* Actions and the long-press menu rows count presses; the Counter screen
 * shows the total. */
static uint32_t presses;

static void count_up(void)
{
    presses++;
}

/* Rows, and the screen calls an action may make: ui_push by id, ui_pop. */
static void push_from_action(void)
{
    api->ui_push(self, "text");
}

static void pop_from_action(void)
{
    api->ui_pop(self);
}

/* An info row's text, drawn each time the row is. */
static const char *presses_text(void)
{
    static char text[24];
    rv_format(text, sizeof text, "Info: %lu presses", presses);
    return text;
}

static const struct rv_row row_rows[] = {
    RV_ROW_PUSH("Push", "text"),
    RV_ROW_ACTION("Action: count up", count_up),
    RV_ROW_ACTION("Action: push a screen", push_from_action),
    RV_ROW_ACTION("Action: pop this list", pop_from_action),
    RV_ROW_SETTING("Setting: flag", KEY_FLAG),
    RV_ROW_SETTING("Setting: choice", KEY_CHOICE),
    RV_ROW_SETTING("Setting: slider", KEY_SLIDER),
    RV_ROW_SETTING("Setting: multi", KEY_MULTI),
    RV_ROW_INFO("Info", presses_text),
};

/* Screens. */

static const char *const text_page[] = {
    "A text page shows its paragraphs in the stock text font, wrapped to the screen.",
    "Each string of the array is one paragraph; the wheel scrolls when they do not fit.",
    "A push row opened this one, or an action that pushed it by name. Menu returns.",
};

static const char *const moons[][2] = {
    {"Moon", "Earth"},   {"Phobos", "Mars"},    {"Io", "Jupiter"},
    {"Europa", "Jupiter"}, {"Titan", "Saturn"}, {"Triton", "Neptune"},
};

static uint32_t moon_count(void)
{
    return RV_COUNT(moons);
}

static const char *moon_row(uint32_t index, const char **detail)
{
    *detail = moons[index][1];
    return moons[index][0];
}

static int moon_select(uint32_t index)
{
    return index + 1 == RV_COUNT(moons); /* the last row pops */
}

/* Canvas: swatches of the colour roles, a line in each font, a box the wheel
 * moves and Select recolours, a bitmap drawn by name and the same bitmap from
 * the extension's own copy (loaded on the first draw, freed when Menu closes
 * the canvas), what asset_load and asset_read returned, and the last input in
 * the corner. */
static int box_left = 120;
static uint8_t box_red;
static const char *last_input = "No input yet";
static char loaded[40] = "asset_load failed";
static char read_back[24] = "asset_read failed";
static void *disc; /* the "disc" asset, for gfx->bmap while the canvas is open */

/* The first line of `bytes` of text after `prefix`. */
static void first_line(char *to, uint32_t room, const char *prefix, const char *from, uint32_t bytes)
{
    uint32_t line = 0;
    while (line < bytes && from[line] != '\n')
        line++;
    rv_format(to, room, "%s%.*s", prefix, (int)line, from);
}

static void read_assets(void)
{
    uint32_t bytes = 0, got = 0;
    char *note = api->asset_load(self, "note", &bytes);
    if (note) {
        first_line(loaded, sizeof loaded, "Load: ", note, bytes);
        api->free(self, note);
    }
    char part[9];
    if (api->asset_read(self, "note-raw", 7, part, sizeof part, &got) == RV_OK)
        first_line(read_back, sizeof read_back, "Read: ", part, got);
}

static void unload(void)
{
    api->free(self, disc); /* the canvas may be open */
}

static void canvas_draw(const struct rv_gfx *g, int width, int height)
{
    static const char *const fonts[RV_FONT_COUNT] = {"Title font", "Text font", "Large font"};
    struct rv_rect all = {0, 0, (int16_t)width, (int16_t)height};
    g->fill(&all, g->color(RV_COLOR_BACKGROUND));

    int swatch = width / RV_COLOR_ROLE_COUNT;
    for (int role = 0; role < RV_COLOR_ROLE_COUNT; role++) {
        struct rv_rect r = {(int16_t)(role * swatch), 0, (int16_t)swatch, 12};
        g->fill(&r, g->color((uint8_t)role));
    }

    int y = 16;
    for (int font = 0; font < RV_FONT_COUNT; font++) {
        g->text(4, y, (uint8_t)font, g->color(RV_COLOR_TEXT), fonts[font]);
        y += g->line_height((uint8_t)font);
    }

    struct rv_rect box = {(int16_t)box_left, (int16_t)y, 40, 40};
    g->fill(&box, box_red ? RV_RGB(204, 51, 0) : RV_RGB(0, 102, 204));
    g->bitmap(4, y, RV_BITMAP("disc"));
    if (!disc)
        disc = api->asset_load(self, "disc", 0);
    if (disc)
        g->bmap(60, y, disc);

    int line = g->line_height(RV_FONT_TEXT);
    g->text(4, y + 52, RV_FONT_TEXT, g->color(RV_COLOR_TEXT), loaded);
    g->text(4, y + 52 + line, RV_FONT_TEXT, g->color(RV_COLOR_TEXT), read_back);
    g->text(4, height - line - 2, RV_FONT_TEXT, g->color(RV_COLOR_DIM), "Wheel moves, Select recolours");
    g->text(width - g->measure(RV_FONT_TEXT, last_input) - 4, height - 2 * line - 2, RV_FONT_TEXT,
            g->color(RV_COLOR_HIGHLIGHT), last_input);
}

static int canvas_input(const struct rv_input *input)
{
    static const char *const keys[] = {"Center", "Menu", "Previous", "Next", "Play"};
    if (input->kind == RV_INPUT_WHEEL) {
        last_input = input->delta > 0 ? "Wheel forward" : "Wheel back";
        box_left += input->delta * 8;
        box_left = box_left < 0 ? 0 : box_left > 280 ? 280 : box_left;
    } else if (input->button == RV_BUTTON_MENU) {
        api->free(self, disc);
        disc = 0;
        return 0; /* left to the system, which pops the canvas */
    } else if (input->kind == RV_INPUT_PRESS) {
        last_input = keys[input->button];
        if (input->button == RV_BUTTON_CENTER)
            box_red ^= 1;
    } else {
        return 1; /* releases change nothing */
    }
    api->ui_invalidate(self);
    return 1;
}

/* Page: a wrapped paragraph over list rows, the wheel moves the highlight. */
static int page_row;

static void page_draw(const struct rv_gfx *g, int width, int height)
{
    static const char *const labels[] = {"First", "Second", "Third"};
    struct rv_rect all = {0, 0, (int16_t)width, (int16_t)height};
    g->fill(&all, g->color(RV_COLOR_BACKGROUND));
    g->paragraph(10, 10, width - 20, RV_FONT_TEXT, g->color(RV_COLOR_PARAGRAPH),
                 "A paragraph wraps at spaces to the width it is given and stops after "
                 "the number of lines it may use, here three of them.",
                 3);
    int y = height - (int)RV_COUNT(labels) * RV_GFX_ROW_HEIGHT;
    for (int i = 0; i < (int)RV_COUNT(labels); i++)
        g->row(y + i * RV_GFX_ROW_HEIGHT, width, labels[i], i == page_row ? "Selected" : 0,
               i == page_row);
}

static int page_input(const struct rv_input *input)
{
    if (input->kind != RV_INPUT_WHEEL)
        return input->button != RV_BUTTON_MENU; /* Menu pops the page */
    page_row += input->delta > 0 ? 1 : -1;
    page_row = page_row < 0 ? 0 : page_row > 2 ? 2 : page_row;
    api->ui_invalidate(self);
    return 1;
}

/* Card: the Settings > About card with the pane icon and formatted figures,
 * then the disc bitmap cut to its top half and tiled across the width. */
static void card_draw(const struct rv_gfx *g, int width, int height)
{
    char number[16], size[16];
    api->format_number(number, sizeof number, 1234567);
    api->format_size(size, sizeof size, 6 * 1024 + 100);
    g->card(width, height, number, size, RV_BITMAP("pane"));
    if (!disc)
        disc = api->asset_load(self, "disc", 0);
    if (disc) {
        g->bmap_part(10, 130, disc, 16, 0);
        g->bmap_tile(0, 170, width, disc);
    }
}

static int card_input(const struct rv_input *input)
{
    if (input->kind == RV_INPUT_WHEEL || input->button != RV_BUTTON_MENU)
        return 1;
    api->free(self, disc);
    disc = 0;
    return 0; /* Menu pops the card */
}

/* Counter: a dynamic list whose rows change, redrawn with ui_refresh. */
static uint32_t counter_count(void)
{
    return 3;
}

static const char *counter_row(uint32_t index, const char **detail)
{
    static char digits[12];
    *detail = 0;
    switch (index) {
    case 0:
        rv_format(digits, sizeof digits, "%lu", presses);
        *detail = digits;
        return "Presses";
    case 1:
        return "Count up";
    default:
        return "Reset and close";
    }
}

static int counter_select(uint32_t index)
{
    if (index == 2) {
        presses = 0;
        return 1;
    }
    if (index == 1)
        presses++;
    api->ui_refresh(self, "counter");
    return 0;
}

/* The choice, slider and multi-choice screens are the ones the core makes from
 * the settings; their setting rows open them. List screens follow their
 * parent's layout unless .layout says otherwise; the two dynamic lists force
 * one each. */
static const struct rv_row screen_rows[] = {
    RV_ROW_PUSH("Text page", "text"),
    RV_ROW_SETTING("Choice", KEY_CHOICE),
    RV_ROW_SETTING("Slider", KEY_SLIDER),
    RV_ROW_SETTING("Multi-choice", KEY_MULTI),
    RV_ROW_PUSH("Dynamic list, full", "dynamic"),
    RV_ROW_PUSH("Dynamic list, split", "stacked"),
    RV_ROW_PUSH("Counter, refreshed", "counter"),
    RV_ROW_PUSH("Canvas", "canvas"),
    RV_ROW_PUSH("Page canvas", "page"),
    RV_ROW_PUSH("Card canvas", "card"),
};

/* Entries. */
static const char *const entries_text[] = {
    "Settings: this extension's row, after Legal, with a caption and an icon in the preview pane.",
    "Main Menu: after Extras. The Main Menu settings can hide it.",
    "Music: at the end, hidden until the Music Menu settings turn it on.",
    "Extras: at the end.",
    "Long press on Now Playing, a song, an album or an artist: a Count up row.",
};

static const struct rv_row menu_rows[] = {
    RV_ROW_PUSH("Rows", "rows"),
    RV_ROW_PUSH("Screens", "screens"),
    RV_ROW_PUSH("Entries", "entries"),
};

static const struct rv_screen screens[] = {
    RV_SCREEN_LIST("menu", "UI Showcase", menu_rows),
    RV_SCREEN_LIST("rows", "Rows", row_rows),
    RV_SCREEN_LIST("screens", "Screens", screen_rows),
    RV_SCREEN_TEXT("text", "Text page", text_page),
    RV_SCREEN_DYNAMIC_LIST("dynamic", "Dynamic list", moon_count, moon_row, moon_select,
                           .layout = RV_LAYOUT_FULL),
    RV_SCREEN_DYNAMIC_LIST("stacked", "Stacked", moon_count, moon_row, moon_select,
                           .layout = RV_LAYOUT_SPLIT, .dynamic.detail = RV_DETAIL_BELOW),
    RV_SCREEN_DYNAMIC_LIST("counter", "Counter", counter_count, counter_row, counter_select),
    RV_SCREEN_CANVAS("canvas", "Canvas", canvas_draw, canvas_input),
    RV_SCREEN_CANVAS("page", "Page", page_draw, page_input),
    RV_SCREEN_CANVAS("card", "Card", card_draw, card_input, .transition = RV_TRANSITION_FROM_PANE),
    RV_SCREEN_TEXT("entries", "Entries", entries_text),
};

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_LEGAL, RV_ROW_PUSH("UI Showcase", "menu"),
             .caption = "Every SDK screen", .bitmap = RV_BITMAP("pane")),
    RV_MENU_ENTRY(RV_MENU_MAIN, RV_MAIN_EXTRAS, RV_ROW_PUSH("UI Showcase", "menu")),
    RV_MENU_ENTRY(RV_MENU_MUSIC, 0, RV_ROW_PUSH("UI Showcase", "menu"), .hidden = 1),
    RV_MENU_ENTRY(RV_MENU_EXTRAS, 0, RV_ROW_PUSH("UI Showcase", "menu")),
    RV_MENU_ENTRY(RV_MENU_NOW_PLAYING_CONTEXT, 0, RV_ROW_ACTION("Count up", count_up)),
    RV_MENU_ENTRY(RV_MENU_SONG_CONTEXT, 0, RV_ROW_ACTION("Count up", count_up)),
    RV_MENU_ENTRY(RV_MENU_ALBUM_CONTEXT, 0, RV_ROW_ACTION("Count up", count_up)),
    RV_MENU_ENTRY(RV_MENU_ARTIST_CONTEXT, 0, RV_ROW_ACTION("Count up", count_up)),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension, RV_SETTINGS(settings),
    RV_SCREENS(screens), RV_ENTRIES(entries), .unload = unload,
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    read_assets();
    return &extension;
}
