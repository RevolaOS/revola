/* SPDX-License-Identifier: GPL-3.0-only */

/* Controllers the runtime creates get a copy of their class vtable with one
 * slot replaced, so Apple's controllers of the same class pay nothing.
 * Copies are made on the first push that needs one and kept for the boot. */

#include "ui/controllers.h"
#include "loader/loader.h"
#include "osos-2.0.5.h"
#include "hooks/sites.h"
#include <stddef.h>

#define PREFIX_WORDS (CxxVtable_prefix_bytes / 4)

void **core_controller_vtable(void *const *vtable, uint32_t method, void *handler,
                              void **original)
{
    uint32_t words = (CxxVtable_prefix_bytes + ScreenController_vtable_bytes) / 4;
    void **copy = core_heap_allocate(NULL, words * 4);
    if (!copy)
        return NULL;
    const uint8_t *from = (const uint8_t *)vtable - CxxVtable_prefix_bytes;
    for (uint32_t i = 0; i < words; i++)
        copy[i] = ((void *const *)from)[i];
    for (uint32_t site = 0; site < CORE_SITE_COUNT; site++) {
        uint32_t slot = (core_sites[site].address - (uint32_t)from) / 4;
        if (core_sites[site].kind == CORE_SITE_VTABLE && slot < words)
            copy[slot] = (void *)core_site_stubs[site];
    }
    uint32_t slot = PREFIX_WORDS + method / 4;
    *original = copy[slot];
    copy[slot] = handler;
    return copy;
}

void core_controller_attach(void *controller, void **copy)
{
    *(void ***)controller = copy + PREFIX_WORDS;
}
