/* SPDX-License-Identifier: GPL-3.0-only */

/* Two boot hooks. core_resource_init runs when Apple initialises resource
 * overrides, before the data volume is readable. core_preferences_init runs when
 * Apple loads its preferences file, so the disk is available from here on. */

#include "osos-2.0.5.h"
#include "patch.h"

PATCH_ARM void core_resource_init(void *bank)
{
    ResourceBank_InitOverrides(bank);
}

PATCH_ARM void core_preferences_init(void *preferences)
{
    Preferences_LoadFile(preferences);
}

PATCH_CALL(SITE_ResourceBank_InitOverrides_call, ADDR_ResourceBank_InitOverrides,
           core_resource_init);
PATCH_CALL(SITE_Preferences_LoadFile_call, ADDR_Preferences_LoadFile,
           core_preferences_init);
