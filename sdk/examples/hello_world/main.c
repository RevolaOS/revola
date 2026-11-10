/* SPDX-License-Identifier: GPL-3.0-only */

/* Loader test fixture: two filters on the Settings property site, a setting
 * the second one counts in, and a Settings row that opens a text page. Each
 * hook call appends an event to a log the emulator reads through the exported
 * symbol in the module's data. */

#include "rv_api.h"
#include "rv_menus.h"
#include "rv_sites.h"
#include "rv_ui.h"

#define SETTING_CALLS 1

struct hello_log {
    uint32_t magic; /* 'HELO' */
    uint32_t count;
    uint32_t events[14];
} hello_log = {0x4f4c4548, 0, {0}};

static const struct rv_api *api;
static rv_extension_handle self;

static void record(uint32_t event)
{
    if (hello_log.count < 14)
        hello_log.events[hello_log.count] = event;
    hello_log.count++;
}

/* The property filters log their first calls only. */
static uint32_t first(uint32_t model, uint32_t type, uint32_t property, uint32_t out,
                      const void *next)
{
    if (hello_log.count < 5)
        record(0x10 | (hello_log.count << 8));
    return api->hook_next(next, model, type, property, out, 0, 0);
}

static uint32_t second(uint32_t model, uint32_t type, uint32_t property, uint32_t out,
                       const void *next)
{
    if (hello_log.count < 5)
        record(0x20 | (hello_log.count << 8));
    uint32_t result = api->hook_next(next, model, type, property, out, 0, 0);
    int32_t calls = 0;
    api->setting_get(self, SETTING_CALLS, &calls, sizeof calls);
    calls++;
    api->setting_set(self, SETTING_CALLS, RV_SETTING_TYPE_INT, &calls, sizeof calls);
    return result;
}

/* The second-priority handler binds first so the order on the site is tested. */
static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER_AT(RV_SITE_Settings_Property_slot, second, 20),
    RV_HOOK_FILTER_AT(RV_SITE_Settings_Property_slot, first, 10),
};

static const struct rv_setting settings[] = {
    RV_SETTING_INT(SETTING_CALLS, "Property calls", 0, 0, 1000000),
};

static const char *const about[] = {
    "Hello World is the loader fixture of the RevolaOS SDK.",
    "It chains two filters on one hook site and counts the calls in a setting.",
};

static const struct rv_screen screens[] = {
    RV_SCREEN_TEXT("about", "Hello World", about),
};

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_LEGAL, RV_ROW_PUSH("Hello World", "about"),
             .caption = "Loader fixture"),
};

static void unload(void)
{
    record(0xff);
}

static const struct rv_descriptor extension = {
    .size = sizeof extension, RV_HOOKS(hooks),     RV_SETTINGS(settings),
    RV_SCREENS(screens),      RV_ENTRIES(entries), .unload = unload,
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    record(0x01);
    return &extension;
}
