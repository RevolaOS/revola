/* SPDX-License-Identifier: GPL-3.0-only */

/* Loader test fixture: two filters on the Settings property site, one setting.
 * Each call appends an event to a log the emulator reads through the exported
 * symbol in the module's data; the loader test checks their order. */

#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_sites.h"

#define SETTING_CALLS 1

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

static uint32_t first(uint32_t model, uint32_t type, uint32_t property, uint32_t out,
                      const void *next)
{
    record(0x10 | (hello_log.count << 8));
    return api->next(next, model, type, property, out, 0, 0);
}

static uint32_t second(uint32_t model, uint32_t type, uint32_t property, uint32_t out,
                       const void *next)
{
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
};

static void unload(void)
{
    record(0xff);
}

static const struct rv_extension extension = {
    sizeof extension, hooks, 2, settings, 1, 0, 0, unload,
};

const struct rv_extension *rvx_entry(const struct rv_api *core, rv_handle handle)
{
    api = core;
    self = handle;
    record(0x01);
    return &extension;
}
