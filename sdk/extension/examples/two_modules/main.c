/* SPDX-License-Identifier: GPL-3.0-only */

/* Loader test fixture: the main module declares the setting and the Settings
 * row; the row opens "viewer", a screen of the viewer module, which the core
 * loads on the push and unloads ten grace ticks after the screen closed. */

#include "rv_api.h"
#include "rv_menus.h"
#include "rv_ui.h"
#include "shared.h"

static const struct rv_setting settings[] = {
    RV_SETTING_INT(KEY_LOADS, "Viewer loads", 0, 0, 1000000),
};

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_LEGAL, RV_ROW_PUSH("Two Modules", "viewer"),
                  .caption = "Loads on open"),
};

static const struct rv_descriptor descriptor = {
    .size = sizeof descriptor, RV_SETTINGS(settings), RV_ENTRIES(entries),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *api, rv_extension_handle self)
{
    (void)api;
    (void)self;
    return &descriptor;
}
