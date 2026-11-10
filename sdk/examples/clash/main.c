/* SPDX-License-Identifier: GPL-3.0-only */

/* Loader test fixture: claims the Settings row-append site exclusively, which
 * the core's menu rows already filter, so the load fails with a conflict. */

#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_sites.h"
#include "rv_ui.h"

static uint32_t append(uint32_t list, uint32_t data, uint32_t bytes, uint32_t metadata,
                       const void *next)
{
    (void)next;
    ResourceBlockList_AppendBorrowedBlock((void *)list, (const void *)data, bytes, metadata);
    return 0;
}

static const struct rv_hook hooks[] = {
    RV_HOOK_REPLACE(RV_SITE_ResourceBlockList_AppendBorrowedBlock_call, append),
};

static const struct rv_descriptor extension = {.size = sizeof extension, .hooks = hooks, .hook_count = 1};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    (void)core;
    (void)handle;
    return &extension;
}
