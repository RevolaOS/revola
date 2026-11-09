/* SPDX-License-Identifier: GPL-3.0-only */

/* Loader and UI test fixture: two filters on the Settings property site, its
 * settings, and a Settings row that opens a list with a text page, a toggle, an
 * action, a slider, a canvas, a choice, a multi-choice and a runtime list. Each
 * hook call appends an event to a log the emulator reads through the exported
 * symbol in the module's data. */

#include "osos-2.0.5.h" /* the BMap layout of the pane icon */
#include "rv_api.h"
#include "rv_menus.h"
#include "rv_sites.h"
#include "rv_ui.h"

#define SETTING_CALLS 1
#define SETTING_BEEP 2
#define SETTING_LEVEL 3
#define SETTING_COLOUR 4
#define SETTING_TOPPINGS 5

struct hello_log {
    uint32_t magic; /* 'HELO' */
    uint32_t count;
    uint32_t events[14];
} hello_log = {0x4f4c4548, 0, {0}};

static const struct rv_api *api;
static rv_handle self;

static void record(uint32_t event)
{
    if (hello_log.count < 14)
        hello_log.events[hello_log.count] = event;
    hello_log.count++;
}

/* The property filters log their first calls only, so UI events stay visible. */
static uint32_t first(uint32_t model, uint32_t type, uint32_t property, uint32_t out,
                      const void *next)
{
    if (hello_log.count < 5)
        record(0x10 | (hello_log.count << 8));
    return api->next(next, model, type, property, out, 0, 0);
}

static uint32_t second(uint32_t model, uint32_t type, uint32_t property, uint32_t out,
                       const void *next)
{
    if (hello_log.count < 5)
        record(0x20 | (hello_log.count << 8));
    uint32_t result = api->next(next, model, type, property, out, 0, 0);
    int32_t calls = 0;
    api->setting_get(self, SETTING_CALLS, &calls, sizeof calls);
    calls++;
    api->setting_set(self, SETTING_CALLS, RV_SETTING_INT, &calls, sizeof calls);
    return result;
}

/* The second-priority handler binds first so the order on the site is tested. */
static const struct rv_hook hooks[] = {
    {RV_SITE_Settings_Property_slot, RV_HOOK_FILTER, 0, 20, second},
    {RV_SITE_Settings_Property_slot, RV_HOOK_FILTER, 0, 10, first},
};

static const char *const levels[] = {"Off", "Low", "Medium", "High", "Full"};
static const char *const colours[] = {"Blue", "Red", "Green", "Grey"};
static const char *const toppings[] = {"Cheese", "Olives", "Basil", "Chili"};

/* Level and Toppings open the screens the core makes from their style; Colour
 * keeps a declared choice screen so both paths are exercised. */
static const struct rv_setting settings[] = {
    RV_SETTING_NUMBER(SETTING_CALLS, "Property calls", 0, 0, 1000000),
    RV_SETTING_FLAG(SETTING_BEEP, "Beep", 0),
    RV_SETTING_SLIDER(SETTING_LEVEL, "Level", 2, levels),
    RV_SETTING_NUMBER(SETTING_COLOUR, "Colour", 0, 0, 3),
    RV_SETTING_MULTI(SETTING_TOPPINGS, "Toppings", 1, toppings),
};

static void wave(void)
{
    record(0xaa);
}

static const char *const about[] = {
    "Hello is the fixture extension of the RevolaOS SDK.",
    "It declares one screen of every kind so the emulator tests can open them.",
};

/* Sketch: a box the wheel moves and Select recolours; inputs are logged as
 * 0xc0 | kind << 8 | key << 12. */
static int32_t box_left = 20;
static uint8_t box_colour;

static void sketch_draw(const struct rv_gfx *g, int width, int height)
{
    struct rv_rect all = {0, 0, (int16_t)width, (int16_t)height};
    struct rv_rect box = {(int16_t)box_left, 20, 60, 60};
    int line = g->line_height(RV_FONT_TEXT);
    g->fill(&all, g->color(RV_COLOR_BACKGROUND));
    g->fill(&box, box_colour ? RV_RGB(204, 51, 0) : RV_RGB(0, 102, 204));
    g->text(4, height - line - 4, RV_FONT_TEXT, g->color(RV_COLOR_TEXT), "Select recolours");
}

static int sketch_input(const struct rv_input *input)
{
    record(0xc0 | (uint32_t)input->kind << 8 | (uint32_t)input->key << 12);
    if (input->kind == RV_INPUT_WHEEL) {
        box_left += input->delta * 8;
        box_left = box_left < 0 ? 0 : box_left > 240 ? 240 : box_left;
    } else if (input->kind == RV_INPUT_PRESS && input->key == RV_KEY_CENTER) {
        box_colour ^= 1;
    } else {
        return 0;
    }
    api->ui_invalidate(self);
    return 1;
}

/* Planets: a runtime list; choosing logs 0xd0 | index << 8, the last row pops. */
static const char *const planets[][2] = {
    {"Mercury", "0.4 AU"}, {"Venus", "0.7 AU"}, {"Earth", "1 AU"}, {"Mars", "1.5 AU"},
    {"Jupiter", "5.2 AU"}, {"Saturn", "9.5 AU"},
};

static uint32_t planet_count(void)
{
    return RV_COUNT(planets);
}

static const char *planet_row(uint32_t index, const char **detail)
{
    *detail = planets[index][1];
    return planets[index][0];
}

static int planet_select(uint32_t index)
{
    record(0xd0 | index << 8);
    return index + 1 == planet_count();
}

static const struct rv_row rows[] = {
    RV_PUSH("About", "about"),
    RV_SETTING("Beep", SETTING_BEEP),
    RV_ACTION("Wave", wave),
    RV_ACTION("Wave again", wave),
    RV_SETTING("Level", SETTING_LEVEL),
    RV_PUSH("Sketch", "sketch"),
    RV_PUSH("Colour", "colour"),
    RV_PUSH("Planets", "planets"),
    RV_SETTING("Toppings", SETTING_TOPPINGS),
};

static const struct rv_screen screens[] = {
    RV_LIST("menu", "Hello", rows),
    RV_TEXT("about", "About Hello", about),
    RV_CANVAS("sketch", "Sketch", sketch_draw, sketch_input),
    RV_CHOICE("colour", "Colour", SETTING_COLOUR, colours, .layout = RV_LAYOUT_FULL),
    RV_RUNTIME_LIST("planets", "Planets", planet_count, planet_row, planet_select,
                    .layout = RV_LAYOUT_SPLIT, .runtime.stacked = 1),
};

/* The Settings pane icon: a 48 x 48 indexed BMap of a red disc, palette
 * entries B, G, R, A. Built when the module loads. */
#define ICON_SIZE 48
static uint32_t icon[(BMap_payload + 4 + 3 * 4 + ICON_SIZE * ICON_SIZE + 3) / 4];

static void build_icon(void)
{
    static const uint8_t palette[3][4] = {{0, 0, 0, 0}, {0, 0, 255, 255}, {255, 255, 255, 255}};
    uint8_t *b = (uint8_t *)icon, *pixels = b + BMap_payload + 4 + sizeof palette;
    *(uint16_t *)(b + BMap_format) = BMapFormat_Indexed8;
    *(uint16_t *)(b + BMap_stride) = ICON_SIZE;
    *(uint16_t *)(b + BMap_depth) = 8;
    int32_t *bounds = (int32_t *)(b + BMap_bounds);
    bounds[2] = bounds[3] = ICON_SIZE;
    *(uint32_t *)(b + BMap_payload_bytes) = 4 + sizeof palette + ICON_SIZE * ICON_SIZE;
    *(uint32_t *)(b + BMap_payload) = 3;
    for (unsigned i = 0; i < sizeof palette; i++)
        b[BMap_payload + 4 + i] = palette[i / 4][i % 4];
    for (int y = 0; y < ICON_SIZE; y++)
        for (int x = 0; x < ICON_SIZE; x++) {
            int dx = 2 * x + 1 - ICON_SIZE, dy = 2 * y + 1 - ICON_SIZE;
            pixels[y * ICON_SIZE + x] = dx * dx + dy * dy < ICON_SIZE * ICON_SIZE ? 1 : 0;
        }
}

static const struct rv_entry entries[] = {
    RV_ENTRY("settings", RV_SETTINGS_LEGAL, RV_PUSH("Hello", "menu"),
             .caption = "Fixture extension", .bitmap = (const struct rv_bitmap *)icon),
    RV_ENTRY("main", RV_MAIN_EXTRAS, RV_PUSH("Hello", "menu")),
    RV_ENTRY("music", 0, RV_PUSH("Hello", "menu")),
    RV_ENTRY("extras", 0, RV_PUSH("Hello", "menu")),
    RV_ENTRY("nowplaying", 0, RV_ACTION("Wave", wave)),
    RV_ENTRY("song", 0, RV_ACTION("Wave", wave)),
    RV_ENTRY("album", 0, RV_ACTION("Wave", wave)),
    RV_ENTRY("artist", 0, RV_ACTION("Wave", wave)),
};

static void unload(void)
{
    record(0xff);
}

static const struct rv_extension extension = {
    .size = sizeof extension, RV_HOOKS(hooks),     RV_SETTINGS(settings),
    RV_SCREENS(screens),      RV_ENTRIES(entries), .unload = unload,
};

const struct rv_extension *rvx_entry(const struct rv_api *core, rv_handle handle)
{
    api = core;
    self = handle;
    build_icon();
    record(0x01);
    return &extension;
}
