/* SPDX-License-Identifier: GPL-3.0-only */

/* Boot sequence. core_boot runs from the install-time call when Apple initialises
 * resource overrides, in the UI task after the other tasks were started and before
 * the data volume is readable: it writes the entry sites and binds the core's handlers. core_preferences_init runs when Apple loads its
 * preferences file, the first point where the disk can be read. */

#include "memory.h"
#include "osos-2.0.5.h"
#include "patch.h"
#include "settings.h"
#include "sites.h"

void core_about_init(void *bank);
void core_artwork_bind(void);
void core_selftest_boot(void);
void core_selftest_preferences(void);

static uint32_t core_preferences_init(void *preferences, const struct core_next *next)
{
    uint32_t result = core_next_call(next, (uint32_t)preferences, 0, 0, 0);
    core_settings_load();
    core_memory_bind();
    core_artwork_bind();
#ifdef CORE_SELFTEST
    core_selftest_preferences();
#endif
    return result;
}

static const struct core_hook preferences_hook = {
    CORE_SITE_Preferences_LoadFile_call, CORE_HOOK_FILTER, 0, core_preferences_init};

PATCH_ARM void core_boot(void *bank)
{
    ResourceBank_InitOverrides(bank);
    core_sites_init();
    core_hook_bind(&preferences_hook);
    core_settings_bind();
    core_about_init(bank);
#ifdef CORE_SELFTEST
    core_selftest_boot();
#endif
}
