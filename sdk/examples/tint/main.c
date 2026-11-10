/* SPDX-License-Identifier: GPL-3.0-only */

/* Loader test fixture: binds the two raw sites of its manifest, the clear color
 * calls of Cover Flow and the album art view, and clears to dark blue instead. */

#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_ui.h"

static const struct rv_api *api;

/* GLES fixed point: 0x10000 is 1.0. */
static uint32_t clear_color(uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha,
                            const void *next)
{
    (void)red;
    (void)green;
    (void)blue;
    return api->hook_next(next, 0, 0, 0x4000, alpha, 0, 0);
}

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_RAW_SITE(0), clear_color),
    RV_HOOK_FILTER(RV_RAW_SITE(1), clear_color),
};

static const struct rv_descriptor extension = {.size = sizeof extension, .hooks = hooks, .hook_count = 2};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    (void)handle;
    api = core;
    return &extension;
}
