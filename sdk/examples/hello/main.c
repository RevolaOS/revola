/* SPDX-License-Identifier: GPL-3.0-only */

/* Loader and UI test fixture: two filters on the Settings property site, one
 * setting, and a Settings row that opens a list with a text page, a toggle and
 * an action. Each hook call appends an event to a log the emulator reads
 * through the exported symbol in the module's data. */

#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_resources.h"
#include "rv_sites.h"

#define SETTING_CALLS 1
#define SETTING_BEEP 2

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

static const struct rv_row rows[] = {
    {"About", RV_ROW_PUSH, 0, 0, "hello.about", 0, 0},
    {"Beep", RV_ROW_TOGGLE, 0, SETTING_BEEP, 0, 0, 0},
    {"Wave", RV_ROW_ACTION, 0, 0, 0, wave, 0},
    {"Wave again", RV_ROW_ACTION, 0, 0, 0, wave, 0},
};

static const struct rv_screen screens[] = {
    {"hello.menu", "Hello", RV_SCREEN_LIST, {0}, {.list = {rows, 4}}},
    {"hello.about", "About Hello", RV_SCREEN_TEXT, {0}, {.text = {about, 2}}},
};

#define PUSH_ROW {"Hello", RV_ROW_PUSH, 0, 0, "hello.menu", 0, 0}
#define WAVE_ROW {"Wave", RV_ROW_ACTION, 0, 0, 0, wave, 0}

static const struct rv_entry entries[] = {
    {RV_ROOT_SETTINGS, 1, {0}, 0, RV_RES_SettingsMenu_Legal_Item, PUSH_ROW, "Fixture extension", 0},
    {RV_ROOT_MAIN, 1, {0}, 0, RV_RES_MainMenu_Extras_Item, PUSH_ROW, 0, 0},
    {RV_ROOT_MUSIC, 1, {0}, 0, 0, PUSH_ROW, 0, 0},
    {RV_ROOT_EXTRAS, 1, {0}, 0, 0, PUSH_ROW, 0, 0},
    {RV_ROOT_NOWPLAYING_MENU, 1, {0}, 0, 0, WAVE_ROW, 0, 0},
    {RV_ROOT_SONG_MENU, 1, {0}, 0, 0, WAVE_ROW, 0, 0},
    {RV_ROOT_ALBUM_MENU, 1, {0}, 0, 0, WAVE_ROW, 0, 0},
    {RV_ROOT_ARTIST_MENU, 1, {0}, 0, 0, WAVE_ROW, 0, 0},
};

static void unload(void)
{
    record(0xff);
}

static const struct rv_extension extension = {
    sizeof extension, hooks, 2, settings, 2, 0, 0, unload, screens, 2, entries, 8,
};

const struct rv_extension *rvx_entry(const struct rv_api *core, rv_handle handle)
{
    api = core;
    self = handle;
    record(0x01);
    return &extension;
}
