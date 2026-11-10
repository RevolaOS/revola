/* SPDX-License-Identifier: GPL-3.0-only */

/* Screen navigation. A declared screen (registry.c) gets, on first push, the
 * clones in clones.c and lists.c; its text comes from the Settings model
 * properties in properties.c. The clones go with the owner's id block
 * (resources.c) when it unloads. */

#include "ui/screens.h"
#include "ui/submenus.h"
#include "ui/extensions.h"
#include "ui/bitmaps.h"
#include "ui/canvas.h"
#include "ui/clones.h"
#include "ui/indexed.h"
#include "ui/lists.h"
#include "ui/menus.h"
#include "ui/panes.h"
#include "ui/properties.h"
#include "osos-2.0.5.h"
#include "ui/resources.h"
#include "ui/templates.h"
#include "ui/toggles.h"
#include "settings.h"
#include "hooks/sites.h"
#include <stddef.h>

#define FIELD(base, offset, type) (*(type *)((uint8_t *)(base) + (offset)))

void *core_screens_caller;

/* Navigation */

/* Controllers the runtime pushed, in push order, so a deactivate finds its
 * screen without knowing the controller class. The screen is NULL once its
 * owner unloaded under the controller. */
#define MAX_OPEN 8
static struct {
    void *controller;
    struct core_screen *screen;
} open[MAX_OPEN];
static uint32_t open_count;

struct core_screen *core_screens_of_controller(void *controller)
{
    for (uint32_t i = 0; i < open_count; i++)
        if (open[i].controller == controller)
            return open[i].screen;
    return NULL;
}

uint32_t core_screens_abandon(struct rv_extension_record *owner)
{
    uint32_t abandoned = 0;
    for (uint32_t i = 0; i < open_count; i++) {
        struct core_screen *s = open[i].screen;
        if (!s || s->owner != owner)
            continue;
        core_bitmaps_screen_closed(s);
        open[i].screen = NULL;
        abandoned++;
    }
    return abandoned;
}

uint32_t core_screens_orphans(void)
{
    uint32_t orphans = 0;
    for (uint32_t i = 0; i < open_count; i++)
        orphans += !open[i].screen;
    return orphans;
}

void core_screens_setting_changed(struct rv_extension_record *owner, uint16_t key)
{
    for (uint32_t i = 0; i < open_count; i++) {
        struct core_screen *s = open[i].screen;
        const struct rv_screen *d = s ? s->declaration : NULL;
        if (!d || s->owner != owner || d->kind != RV_SCREEN_LIST)
            continue;
        for (uint32_t r = 0; r < d->list.count; r++)
            if ((d->list.rows[r].kind == RV_ROW_TOGGLE || d->list.rows[r].kind == RV_ROW_SETTING) &&
                d->list.rows[r].key == key)
                core_screens_notify(CORE_SCREEN_ROWS(s) + 2 * r + 1);
    }
    core_menus_setting_changed(owner, key);
}

struct core_screen *core_screens_top(uint8_t kind)
{
    for (uint32_t i = open_count; i-- > 0;) {
        struct core_screen *s = open[i].screen;
        if (s && s->declaration && s->declaration->kind == kind)
            return s;
    }
    return NULL;
}

/* The canvas popped last while no other canvas stays open, until the next
 * push. A pop deactivates the controller before the transition draws the
 * leaving screen once more, into the copy that slides out. */
static struct core_screen *popped_canvas;

struct core_screen *core_screens_popped_canvas(void)
{
    return popped_canvas;
}

static void screen_left(void *controller)
{
    for (uint32_t i = 0; i < open_count; i++) {
        if (open[i].controller != controller)
            continue;
        struct core_screen *s = open[i].screen;
        open_count--;
        for (uint32_t j = i; j < open_count; j++)
            open[j] = open[j + 1];
        if (!s)
            return;
        if (s->open)
            s->open--;
        if (!s->open)
            core_bitmaps_screen_closed(s);
        if (!s->open && !s->owner)
            core_extensions_closed(s);
        if (s->owner && s->declaration)
            core_loader_screen_popped(s->declaration->name);
        if (s->declaration && s->declaration->kind == RV_SCREEN_CANVAS &&
            !core_screens_top(RV_SCREEN_CANVAS))
            popped_canvas = s;
        return;
    }
}

/* Every deactivate of the two controller classes passes here; only the
 * runtime's controllers are in `open`. */
static uint32_t deactivated(uint32_t controller, uint32_t leaving, const struct core_next *next)
{
    uint32_t result = core_next_call(next, controller, leaving, 0, 0);
    if (leaving)
        screen_left((void *)controller);
    return result;
}

/* The layout a list screen opens in from `controller`'s screen: what it
 * declares, else its parent's: a split list of the runtime or one of Apple's
 * Settings lists (the Settings menu and its submenus share one class) gives
 * split, anything else full width. */
static int split_from(const struct core_screen *s, void *controller)
{
    uint8_t layout = s->declaration->layout;
    if (layout != RV_LAYOUT_AUTO)
        return layout == RV_LAYOUT_SPLIT;
    const struct core_screen *parent = core_screens_of_controller(controller);
    if (parent)
        return parent->declaration && core_lists_kind(parent->declaration->kind) && parent->split;
    return FIELD(controller, 0, void *) == (void *)SettingsMenuListController_vtable;
}

static void *bank;

static int build(struct core_screen *s, int split, uint32_t screen)
{
    uint8_t bit = split ? CORE_BUILT_SPLIT : CORE_BUILT_FULL;
    uint32_t bytes;
    if (!s->open && core_submenus_stale(s))
        s->built = 0;
    /* An owner loaded again before its last release finished finds its
     * clones still in the bank; a controller may still read them. */
    else if (s->owner && ResourceBank_GetResource(bank, Resource_SCST, screen, &bytes))
        s->built |= bit;
    if (s->built & bit)
        return 1;
    int built = 0;
    switch (s->declaration->kind) {
    case RV_SCREEN_LIST:
        built = core_submenus_of(s) ? core_submenus_clone(s, split) : core_clone_list(s, split);
        break;
    case RV_SCREEN_CHOICE:
    case RV_SCREEN_MULTI_CHOICE:
    case RV_SCREEN_RUNTIME_LIST:
        built = core_clone_list(s, split);
        break;
    case RV_SCREEN_TEXT:
        built = core_clone_text(s);
        break;
    case RV_SCREEN_SLIDER:
        built = core_clone_slider(s);
        break;
    case RV_SCREEN_CANVAS:
        built = core_clone_canvas(s);
        break;
    default:
        break;
    }
    if (built)
        s->built |= bit;
    return built;
}

/* The template of a screen that opens as Settings > Brightness, 0 for others. */
static uint32_t template_of(uint8_t kind)
{
    switch (kind) {
    case RV_SCREEN_SLIDER:
        return core_template_slider;
    case RV_SCREEN_CANVAS:
        return core_template_canvas;
    default:
        return 0;
    }
}

/* The transition that opens `s` over the layout on screen now.
 * - A split list shares the status bar, background and pane with Settings, so
 *   only its list moves in, as stock half-screen Settings lists do.
 * - A slider or canvas slides in as Settings > About does, through its
 *   template's styles (templates.c); from a split list it may instead start at
 *   the pane, as Extras > Stopwatch does, with Stopwatch's own style. Over
 *   another full screen the two share the template, which then would not move,
 *   so the whole screen slides.
 * - Text pages and other lists slide the whole screen (Settings opens Legal's
 *   page kind with a foreground push whatever is asked). */
static uint32_t opening(const struct core_screen *s, uint8_t kind)
{
    uint8_t from = FIELD(UI_GetLayoutManager(), LayoutManager_kind, uint8_t);
    uint32_t template = template_of(kind);
    if (s->split)
        return Transition_Standard;
    if (!template)
        return Transition_ScreenPush;
    int pane = s->declaration->opening == RV_OPENING_FROM_PANE && from == LayoutKind_SplitList;
    core_clone_set_style(s->id + 1, template,
                         pane ? core_templates_enter_style(RES_Stopwatch_Template, LayoutKind_SplitList)
                              : -1);
    return from == LayoutKind_FullScreen ? Transition_ScreenPush : Transition_Standard;
}

int core_screens_push(struct core_screen *s, void *controller)
{
    uint8_t kind = s->declaration->kind;
    int input = kind == RV_SCREEN_SLIDER || kind == RV_SCREEN_CANVAS;
    int split = core_lists_kind(kind) && split_from(s, controller);
    if (open_count == MAX_OPEN || (input && !core_canvas_prepare()) || !core_indexed_prepare(s))
        return 0;
    popped_canvas = NULL;
    if (s->owner)
        core_loader_screen_pushed(s->declaration->name);
    s->split = (uint8_t)split;
    uint32_t screen = core_lists_screen_id(s, split);
    if (!build(s, split, screen))
        return 0;
    uint32_t transition = opening(s, kind);
    void *created =
        UI_CreateScreenController(UI_GetControllerRegistry(), screen, screen + 1, transition);
    if (!created)
        return 0;
    if (input)
        core_canvas_attach(created);
    core_indexed_attach(s, created);
    open[open_count].controller = created;
    open[open_count].screen = s;
    open_count++;
    s->open++;
    void (*push)(void *, void *) =
        FIELD(FIELD(controller, 0, void **), ScreenController_push_method, void *);
    push(controller, created);
    return 1;
}

void core_screens_pop(void)
{
    Navigator_PopTop(0x10);
}

void core_screens_invalidate(void)
{
    core_canvas_invalidate();
}

static const struct core_hook hooks[] = {
    {CORE_SITE_ScreenController_Deactivate_slot, CORE_HOOK_FILTER, 0, deactivated},
    {CORE_SITE_SettingsMenuListController_Deactivate_slot, CORE_HOOK_FILTER, 0, deactivated},
};

void core_screens_init(void *resource_bank)
{
    bank = resource_bank;
    core_properties_init();
    core_resources_init(bank);
    core_clones_init(bank);
    core_panes_init(bank);
    core_lists_init(bank);
    core_submenus_init(bank);
    core_toggles_init(bank);
    core_indexed_init(bank);
    core_canvas_init();
    core_templates_init(bank);
    for (uint32_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        core_hook_bind(&hooks[i]);
}
