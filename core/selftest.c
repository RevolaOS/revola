/* SPDX-License-Identifier: GPL-3.0-only */

/* Dispatcher self-test, compiled only with -DCORE_SELFTEST. Results land in
 * core_selftest for the emulator to read; nothing here is shown on screen.
 *
 * Two filters share Preferences_LoadFile_call (order after the core's own handler),
 * two share Settings_Property_slot (order, short-circuit, direct call after the first
 * unbind, restore after the last), one claims the call site AppendBorrowedBlock to show
 * patch and restore by peek, and an entry site proves the replay stub. */

#ifdef CORE_SELFTEST
#include "osos-2.0.5.h"
#include "sites.h"

enum {
    EVENT_PREFERENCES_A = 0xa1,
    EVENT_PREFERENCES_B = 0xb1,
    EVENT_PROPERTY_A = 0xa2,
    EVENT_PROPERTY_B = 0xb2,
    EVENT_PROPERTY_B_SHORT = 0xb3,
    EVENT_RESET_MAIN_MENU = 0xe1,
};

struct core_selftest {
    uint32_t events[32];
    uint32_t count;
    uint32_t property_calls;
    uint32_t bind_results[8];
    uint32_t unbind_results[4];
} core_selftest;

static void record(uint32_t event)
{
    if (core_selftest.count < 32)
        core_selftest.events[core_selftest.count] = event;
    core_selftest.count++;
}

static uint32_t preferences_a(uint32_t preferences, const struct core_next *next)
{
    record(EVENT_PREFERENCES_A);
    return core_next_call(next, preferences, 0, 0, 0);
}

static uint32_t preferences_b(uint32_t preferences, const struct core_next *next)
{
    record(EVENT_PREFERENCES_B);
    return core_next_call(next, preferences, 0, 0, 0);
}

static const struct core_hook property_a, property_b, append_hook;

static uint32_t property_a_handler(uint32_t model, uint32_t type, uint32_t property,
                                   uint32_t out, const struct core_next *next)
{
    uint32_t call = ++core_selftest.property_calls;
    record(EVENT_PROPERTY_A | (call << 8));
    uint32_t result = core_next_call(next, model, type, property, out);
    if (call == 6) {
        core_selftest.unbind_results[1] = core_hook_unbind(&property_a);
        core_selftest.unbind_results[2] = core_hook_unbind(&append_hook);
        core_grace_tick();
    }
    return result;
}

static uint32_t property_b_handler(uint32_t model, uint32_t type, uint32_t property,
                                   uint32_t out, const struct core_next *next)
{
    uint32_t call = core_selftest.property_calls;
    if (call == 2) {
        record(EVENT_PROPERTY_B_SHORT | (call << 8));
        return 0; /* short-circuit: Apple sees "no such property" once */
    }
    record(EVENT_PROPERTY_B | (call << 8));
    uint32_t result = core_next_call(next, model, type, property, out);
    if (call == 4)
        core_selftest.unbind_results[0] = core_hook_unbind(&property_b);
    return result;
}

static uint32_t append_handler(uint32_t list, uint32_t data, uint32_t bytes, uint32_t metadata,
                               const struct core_next *next)
{
    return core_next_call(next, list, data, bytes, metadata);
}

static uint32_t reset_main_menu(uint32_t model, const struct core_next *next)
{
    record(EVENT_RESET_MAIN_MENU);
    return core_next_call(next, model, 0, 0, 0);
}

static const struct core_hook preferences_hook_a = {
    CORE_SITE_Preferences_LoadFile_call, CORE_HOOK_FILTER, 20, preferences_a};
static const struct core_hook preferences_hook_b = {
    CORE_SITE_Preferences_LoadFile_call, CORE_HOOK_FILTER, 10, preferences_b};
static const struct core_hook property_a = {CORE_SITE_Settings_Property_slot, CORE_HOOK_FILTER,
                                            10, property_a_handler};
static const struct core_hook property_b = {CORE_SITE_Settings_Property_slot, CORE_HOOK_FILTER,
                                            20, property_b_handler};
static const struct core_hook append_hook = {
    CORE_SITE_ResourceBlockList_AppendBorrowedBlock_call, CORE_HOOK_REPLACE, 0, append_handler};
static const struct core_hook append_conflict = {
    CORE_SITE_ResourceBlockList_AppendBorrowedBlock_call, CORE_HOOK_FILTER, 0, append_handler};
static const struct core_hook reset_hook = {CORE_SITE_SettingsModel_ResetMainMenu_entry,
                                            CORE_HOOK_FILTER, 0, reset_main_menu};

void core_selftest_boot(void)
{
    /* a binds first with the higher priority value, so the expected order is b, a. */
    core_selftest.bind_results[0] = core_hook_bind(&preferences_hook_a);
    core_selftest.bind_results[1] = core_hook_bind(&preferences_hook_b);
    core_selftest.bind_results[2] = core_hook_bind(&reset_hook);
}

void core_selftest_preferences(void)
{
    core_selftest.bind_results[3] = core_hook_bind(&property_a);
    core_selftest.bind_results[4] = core_hook_bind(&property_b);
    core_selftest.bind_results[5] = core_hook_bind(&append_hook);
    core_selftest.bind_results[6] = core_hook_bind(&append_conflict);
}
#endif
