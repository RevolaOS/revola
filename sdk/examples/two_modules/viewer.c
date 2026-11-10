/* SPDX-License-Identifier: GPL-3.0-only */

/* The viewer module: a canvas that shows how often this module was loaded,
 * counted in the main module's setting at each load. */

#include "rv_api.h"
#include "rv_libc.h"
#include "rv_ui.h"
#include "shared.h"

static const struct rv_api *api;
static rv_extension_handle self;
static char loads[32];

static void draw(const struct rv_gfx *g, int width, int height)
{
    struct rv_rect all = {0, 0, (int16_t)width, (int16_t)height};
    g->fill(&all, g->color(RV_COLOR_BACKGROUND));
    g->text(10, 10, RV_FONT_TITLE, g->color(RV_COLOR_TEXT), "Viewer module");
    g->text(10, 40, RV_FONT_TEXT, g->color(RV_COLOR_TEXT), loads);
}

static int input(const struct rv_input *in)
{
    return in->kind != RV_INPUT_WHEEL && in->button != RV_BUTTON_MENU;
}

static const struct rv_screen screens[] = {
    RV_SCREEN_CANVAS("viewer", "Viewer", draw, input),
};

static const struct rv_descriptor descriptor = {
    .size = sizeof descriptor, RV_SCREENS(screens),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    int32_t count = api->setting_int(self, KEY_LOADS) + 1;
    api->setting_set_int(self, KEY_LOADS, count);
    rv_format(loads, sizeof loads, "Loaded %ld times", (long)count);
    return &descriptor;
}
