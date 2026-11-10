/* SPDX-License-Identifier: GPL-3.0-only */

/* The core's own Settings pane icons: core/assets/<name>.png, stored as
 * compressed menu masks by the build (icons.inc) and decoded only while one is
 * shown, like an extension's menu bitmap. */

#include "ui/icons.h"
#include "loader/loader.h"
#include "loader/lz.h"
#include "settings.h"
#include <stddef.h>

struct icon {
    const char *name;
    const uint8_t *stream;
    uint32_t stored;
    uint32_t bytes;
};

#include "icons.inc"

void *core_icon_decode(uint32_t name)
{
    for (uint32_t i = 0; i < sizeof icons / sizeof icons[0]; i++) {
        if (core_settings_owner(icons[i].name) != name)
            continue;
        uint8_t *block = core_loader_block(icons[i].bytes);
        struct core_lz_input in;
        core_lz_from_memory(&in, icons[i].stream, icons[i].stored);
        if (block && !core_lz_decode(&in, block, icons[i].bytes)) {
            core_loader_retire(block);
            block = NULL;
        }
        return block;
    }
    return NULL;
}
