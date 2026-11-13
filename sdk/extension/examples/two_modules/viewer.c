/* SPDX-License-Identifier: GPL-3.0-only */

/* The viewer module: a canvas that shows how often this module was loaded,
 * counted in the main module's setting at each load, the seconds since, from
 * a timer the core stops when the module unloads, and the part of the canvas
 * each draw covers. */

#include "rv_api.h"
#include "rv_libc.h"
#include "rv_ui.h"
#include "shared.h"

static const struct rv_api *api;
static rv_extension_handle self;
static char loads[32];
static uint32_t seconds;

static void tick(void *arg)
{
    (void)arg;
    uint32_t state = api->irq_save(); /* as a handoff to an audio handler would */
    seconds++;
    api->irq_restore(state);
    static const struct rv_rect line = {0, 40, 320, 40}; /* the two text lines */
    api->ui_invalidate_rect(self, &line);
}

static void draw(const struct rv_context *ctx, const struct rv_gfx *g, int width, int height)
{
    (void)ctx;
    struct rv_rect all = {0, 0, (int16_t)width, (int16_t)height};
    g->fill(&all, g->color(RV_COLOR_BACKGROUND));
    g->text(10, 10, RV_FONT_TITLE, g->color(RV_COLOR_TEXT), "Viewer module");
    char line[48];
    const struct rv_rect *clip = g->clip(); /* the two lines once the timer redraws them */
    rv_format(line, sizeof line, "%lu s, clip %d,%d %dx%d", seconds, clip->x, clip->y,
              clip->width, clip->height);
    g->text(10, 40, RV_FONT_TEXT, g->color(RV_COLOR_TEXT), loads);
    g->text(10, 40 + g->line_height(RV_FONT_TEXT), RV_FONT_TEXT, g->color(RV_COLOR_DIM), line);
}

static int input(const struct rv_context *ctx, const struct rv_input *in)
{
    (void)ctx;
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
    api->timer_start(self, 1000, 1, tick, 0);
    return &descriptor;
}
