/* SPDX-License-Identifier: GPL-3.0-only */

/* Loader and UI test fixture: two filters on the Settings property site, its
 * settings, and a Settings row that opens a list with a text page, a toggle, an
 * action, a slider, a canvas, a choice, a multi-choice and a runtime list. Each hook call
 * appends an event to a log the emulator reads through the exported symbol in
 * the module's data. */

#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_menus.h"
#include "rv_sites.h"

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

static const struct rv_setting settings[] = {
    {SETTING_CALLS, RV_SETTING_INT, 0, 0, 0, 1000000, "Property calls"},
    {SETTING_BEEP, RV_SETTING_BOOL, 0, 0, 0, 1, "Beep"},
    {SETTING_LEVEL, RV_SETTING_INT, 0, 0, 0, 4, "Level"},
    {SETTING_COLOUR, RV_SETTING_INT, 0, 0, 0, 3, "Colour"},
    {SETTING_TOPPINGS, RV_SETTING_INT, 0, 0, 0, 15, "Toppings"},
};

static int wave(void *controller)
{
    record(0xaa | ((uint32_t)controller & 0xffffff00));
    return 1;
}

static const char *const about[] = {
    "Hello is the fixture extension of the RevolaOS SDK.",
    "It declares one screen of every kind so the emulator tests can open them.",
};

static const char *const levels[] = {"Off", "Low", "Medium", "High", "Full"};

/* Sketch: a box the wheel moves and Select recolours; inputs are logged as
 * 0xc0 | kind << 8 | key << 12. */
static int32_t box_left = 20;
static uint8_t box_colour;

static void sketch_draw(void *context, const int32_t *bounds)
{
    static const uint8_t white[4] = {255, 255, 255, 255};
    static const uint8_t colours[2][4] = {{0, 102, 204, 255}, {204, 51, 0, 255}};
    int32_t box[4] = {bounds[0] + 40, bounds[1] + box_left, bounds[0] + 100, bounds[1] + box_left + 60};
    GraphicsContext_SetColor(context, white);
    GraphicsContext_FillRect(context, bounds);
    GraphicsContext_SetColor(context, colours[box_colour]);
    GraphicsContext_FillRect(context, box);
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

static const char *const colours[] = {"Blue", "Red", "Green", "Grey"};
static const char *const toppings[] = {"Cheese", "Olives", "Basil", "Chili"};

/* Planets: a runtime list; choosing logs 0xd0 | index << 8, the last row pops. */
static const char *const planets[][2] = {
    {"Mercury", "0.4 AU"}, {"Venus", "0.7 AU"}, {"Earth", "1 AU"}, {"Mars", "1.5 AU"},
    {"Jupiter", "5.2 AU"}, {"Saturn", "9.5 AU"},
};

static uint32_t planet_count(void)
{
    return sizeof planets / sizeof planets[0];
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
    {"About", RV_ROW_PUSH, 0, 0, "hello.about", 0, 0},
    {"Beep", RV_ROW_TOGGLE, 0, SETTING_BEEP, 0, 0, 0},
    {"Wave", RV_ROW_ACTION, 0, 0, 0, wave, 0},
    {"Wave again", RV_ROW_ACTION, 0, 0, 0, wave, 0},
    {"Level", RV_ROW_PUSH, 0, 0, "hello.level", 0, 0},
    {"Sketch", RV_ROW_PUSH, 0, 0, "hello.sketch", 0, 0},
    {"Colour", RV_ROW_PUSH, 0, 0, "hello.colour", 0, 0},
    {"Planets", RV_ROW_PUSH, 0, 0, "hello.planets", 0, 0},
    {"Toppings", RV_ROW_PUSH, 0, 0, "hello.toppings", 0, 0},
};

static const struct rv_screen screens[] = {
    {"hello.menu", "Hello", RV_SCREEN_LIST, RV_LAYOUT_FULL, {0}, {.list = {rows, 9}}},
    {"hello.about", "About Hello", RV_SCREEN_TEXT, 0, {0}, {.text = {about, 2}}},
    {"hello.level", "Level", RV_SCREEN_SLIDER, 0, {0}, {.choice = {SETTING_LEVEL, 0, levels, 5}}},
    {"hello.sketch", "Sketch", RV_SCREEN_CANVAS, 0, {0}, {.canvas = {sketch_draw, sketch_input}}},
    {"hello.colour", "Colour", RV_SCREEN_CHOICE, RV_LAYOUT_FULL, {0},
     {.choice = {SETTING_COLOUR, 0, colours, 4}}},
    {"hello.planets", "Planets", RV_SCREEN_RUNTIME_LIST, RV_LAYOUT_SPLIT, {0},
     {.runtime = {planet_count, planet_row, planet_select, 1}}},
    {"hello.toppings", "Toppings", RV_SCREEN_MULTI_CHOICE, RV_LAYOUT_SPLIT, {0},
     {.choice = {SETTING_TOPPINGS, 0, toppings, 4}}},
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

#define PUSH_ROW {"Hello", RV_ROW_PUSH, 0, 0, "hello.menu", 0, 0}
#define WAVE_ROW {"Wave", RV_ROW_ACTION, 0, 0, 0, wave, 0}

static const struct rv_entry entries[] = {
    {"settings", 1, {0}, RV_SETTINGS_LEGAL, PUSH_ROW, "Fixture extension", icon},
    {"main", 1, {0}, RV_MAIN_EXTRAS, PUSH_ROW, 0, 0},
    {"music", 1, {0}, 0, PUSH_ROW, 0, 0},
    {"extras", 1, {0}, 0, PUSH_ROW, 0, 0},
    {"nowplaying", 1, {0}, 0, WAVE_ROW, 0, 0},
    {"song", 1, {0}, 0, WAVE_ROW, 0, 0},
    {"album", 1, {0}, 0, WAVE_ROW, 0, 0},
    {"artist", 1, {0}, 0, WAVE_ROW, 0, 0},
};

static void unload(void)
{
    record(0xff);
}

static const struct rv_extension extension = {
    sizeof extension, hooks, 2, settings, 5, 0, 0, unload, screens, 7, entries, 8,
};

const struct rv_extension *rvx_entry(const struct rv_api *core, rv_handle handle)
{
    api = core;
    self = handle;
    build_icon();
    record(0x01);
    return &extension;
}
