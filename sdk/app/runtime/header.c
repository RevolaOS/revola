/* SPDX-License-Identifier: GPL-3.0-only */

/* The eApp header at the start of the image: what the host reads to find
 * the import list and the entries. */

#include "runtime.h"

void rv_app_start(void);
uint32_t rv_app_reserved_entry(void);
extern const uint8_t rv_app_imports[];

const struct rv_app_eapp_header rv_app_eapp_header __attribute__((section(".eapp"), used)) = {
    .magic = GameImage_Magic,
    .version = GameImage_MaxVersion,
    .entry_count = RV_APP_EAPP_SLOT_COUNT,
    .imports = (uint32_t)rv_app_imports,
    .entries = {
        [RV_APP_EAPP_SLOT_INIT] = (uint32_t)rv_app_start,
        [RV_APP_EAPP_SLOT_SHUTDOWN] = (uint32_t)rv_app_runtime_shutdown,
        [2] = (uint32_t)rv_app_reserved_entry,
        [3] = (uint32_t)rv_app_reserved_entry,
        [RV_APP_EAPP_SLOT_FRAME] = (uint32_t)rv_app_runtime_frame,
    },
};
