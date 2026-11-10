/* SPDX-License-Identifier: GPL-3.0-only */

/* Every UI declaration of the SDK behind a row that names it: the row kinds
 * with the screen calls of rv_api, the screen kinds with their layouts, and the
 * menus an entry can appear in. Open it from Settings, Main, Music (after
 * Settings > Music Menu turns it on) or Extras. The core gives an extension
 * 256 resource ids and shares 16 screens among all of them; this one takes 229
 * ids and 12 screens, three of which the core makes from the settings. */

#include "osos-2.0.5.h" /* the BMap layout of the Settings pane icon */
#include "rv_api.h"
#include "rv_menus.h"
#include "rv_ui.h"

static const struct rv_api *api;
static rv_handle self;

/* Settings: one of each kind. */
#define KEY_FLAG 1
#define KEY_CHOICE 2
#define KEY_SLIDER 3
#define KEY_MULTI 4

static const char *const seasons[] = {"Spring", "Summer", "Autumn", "Winter"};
static const char *const volumes[] = {"Mute", "Quiet", "Normal", "Loud", "Max"};
static const char *const days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

static const struct rv_setting settings[] = {
    RV_SETTING_FLAG(KEY_FLAG, "Flag", 0),
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

static char *format(char *end, uint32_t value)
{
    *--end = 0;
    do
        *--end = (char)('0' + value % 10);
    while ((value /= 10));
    return end;
}

/* Rows, and the screen calls an action may make: ui_push by name, ui_pop. */
static void push_from_action(void)
{
    api->ui_push(self, "text");
}

static void pop_from_action(void)
{
    api->ui_pop(self);
}

static const struct rv_row row_rows[] = {
    RV_PUSH("Push", "text"),
    RV_TOGGLE("Toggle", KEY_FLAG),
    RV_ACTION("Action: count up", count_up),
    RV_ACTION("Action: push a screen", push_from_action),
    RV_ACTION("Action: pop this list", pop_from_action),
    RV_SETTING("Setting: flag", KEY_FLAG),
    RV_SETTING("Setting: choice", KEY_CHOICE),
    RV_SETTING("Setting: slider", KEY_SLIDER),
    RV_SETTING("Setting: multi", KEY_MULTI),
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
 * moves and Select recolours, and the last input in the corner. */
static int box_left = 120;
static uint8_t box_red;
static const char *last_input = "No input yet";

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

    int line = g->line_height(RV_FONT_TEXT);
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
    } else if (input->key == RV_KEY_MENU) {
        return 0; /* left to the system, which pops the canvas */
    } else if (input->kind == RV_INPUT_PRESS) {
        last_input = keys[input->key];
        if (input->key == RV_KEY_CENTER)
            box_red ^= 1;
    } else {
        return 1; /* releases change nothing */
    }
    api->ui_invalidate(self);
    return 1;
}

/* Counter: a runtime list whose rows change, redrawn with ui_refresh. */
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
        *detail = format(digits + sizeof digits, presses);
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
 * the settings, named by their labels. List screens follow their parent's
 * layout unless .layout says otherwise; the two runtime lists force one each. */
static const struct rv_row screen_rows[] = {
    RV_PUSH("Text page", "text"),
    RV_PUSH("Choice", "Choice"),
    RV_PUSH("Slider", "Slider"),
    RV_PUSH("Multi-choice", "Multi-choice"),
    RV_PUSH("Runtime list, full", "runtime"),
    RV_PUSH("Runtime list, split", "stacked"),
    RV_PUSH("Counter, refreshed", "counter"),
    RV_PUSH("Canvas", "canvas"),
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
    RV_PUSH("Rows", "rows"),
    RV_PUSH("Screens", "screens"),
    RV_PUSH("Entries", "entries"),
};

static const struct rv_screen screens[] = {
    RV_LIST("menu", "UI Showcase", menu_rows),
    RV_LIST("rows", "Rows", row_rows),
    RV_LIST("screens", "Screens", screen_rows),
    RV_TEXT("text", "Text page", text_page),
    RV_RUNTIME_LIST("runtime", "Runtime list", moon_count, moon_row, moon_select,
                    .layout = RV_LAYOUT_FULL),
    RV_RUNTIME_LIST("stacked", "Stacked", moon_count, moon_row, moon_select,
                    .layout = RV_LAYOUT_SPLIT, .runtime.stacked = 1),
    RV_RUNTIME_LIST("counter", "Counter", counter_count, counter_row, counter_select),
    RV_CANVAS("canvas", "Canvas", canvas_draw, canvas_input),
    RV_TEXT("entries", "Entries", entries_text),
};

/* The Settings pane icon: a 48 x 48 indexed BMap of a blue disc, palette
 * entries B, G, R, A. Built when the module loads.
 * TODO: replace with the SDK's PNG converter once it exists. */
#define ICON_SIZE 48
static uint32_t icon[(BMap_payload + 4 + 2 * 4 + ICON_SIZE * ICON_SIZE + 3) / 4];

static void build_icon(void)
{
    static const uint8_t palette[2][4] = {{0, 0, 0, 0}, {204, 102, 0, 255}};
    uint8_t *b = (uint8_t *)icon, *pixels = b + BMap_payload + 4 + sizeof palette;
    *(uint16_t *)(b + BMap_format) = BMapFormat_Indexed8;
    *(uint16_t *)(b + BMap_stride) = ICON_SIZE;
    *(uint16_t *)(b + BMap_depth) = 8;
    int32_t *bounds = (int32_t *)(b + BMap_bounds);
    bounds[2] = bounds[3] = ICON_SIZE;
    *(uint32_t *)(b + BMap_payload_bytes) = 4 + sizeof palette + ICON_SIZE * ICON_SIZE;
    *(uint32_t *)(b + BMap_payload) = RV_COUNT(palette);
    for (unsigned i = 0; i < sizeof palette; i++)
        b[BMap_payload + 4 + i] = palette[i / 4][i % 4];
    for (int y = 0; y < ICON_SIZE; y++)
        for (int x = 0; x < ICON_SIZE; x++) {
            int dx = 2 * x + 1 - ICON_SIZE, dy = 2 * y + 1 - ICON_SIZE;
            pixels[y * ICON_SIZE + x] = dx * dx + dy * dy < ICON_SIZE * ICON_SIZE ? 1 : 0;
        }
}

static const struct rv_entry entries[] = {
    RV_ENTRY("settings", RV_SETTINGS_LEGAL, RV_PUSH("UI Showcase", "menu"),
             .caption = "Every SDK screen", .bitmap = (const struct rv_bitmap *)icon),
    RV_ENTRY("main", RV_MAIN_EXTRAS, RV_PUSH("UI Showcase", "menu")),
    /* Hidden by default; spelt out because RV_ENTRY sets .visible = 1 and
     * -Werror=override-init refuses a second initializer. */
    {.in = "music", .visible = 0, .row = RV_PUSH("UI Showcase", "menu")},
    RV_ENTRY("extras", 0, RV_PUSH("UI Showcase", "menu")),
    RV_ENTRY("nowplaying", 0, RV_ACTION("Count up", count_up)),
    RV_ENTRY("song", 0, RV_ACTION("Count up", count_up)),
    RV_ENTRY("album", 0, RV_ACTION("Count up", count_up)),
    RV_ENTRY("artist", 0, RV_ACTION("Count up", count_up)),
};

static const struct rv_extension extension = {
    .size = sizeof extension, RV_SETTINGS(settings), RV_SCREENS(screens), RV_ENTRIES(entries),
};

const struct rv_extension *rvx_entry(const struct rv_api *core, rv_handle handle)
{
    api = core;
    self = handle;
    build_icon();
    return &extension;
}
