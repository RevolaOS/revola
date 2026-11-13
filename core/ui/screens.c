/* SPDX-License-Identifier: GPL-3.0-only */

/* Screen navigation. A declared screen (registry.c) gets, on first push, the
 * clones in clones.c and lists.c; its text comes from the Settings model
 * properties in properties.c. The clones go with the owner's id block
 * (resources.c) when it unloads. */

#include "ui/screens.h"
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

void *core_screens_caller;
const struct core_menu_origin *core_screens_menu;

/* Navigation */

/* Controllers the runtime pushed, in push order, so a deactivate finds its
 * screen without knowing the controller class. The screen is NULL once its
 * owner unloaded under the controller. */
#define MAX_OPEN 8
static struct {
    void *controller;
    struct core_screen *screen;
    void *arg;        /* what ui_push passed */
    uint32_t selection; /* requested row while another screen covers this one */
    uint8_t selection_pending;
    uint8_t selected; /* the first activation set its selection */
    uint8_t origin;   /* 1 + its slot in origins, 0 for none */
} open[MAX_OPEN];
static uint32_t open_count;

/* A popped screen whose closed callback waits: a canvas draws once more after
 * its controller deactivated, so its callback runs after that draw, or at the
 * next push or grace tick when no draw comes. */
static struct {
    struct core_screen *screen;
    void *controller;
    void *arg;
    uint8_t ticks; /* grace ticks it has waited */
    uint8_t origin;
} closing;

/* What a screen opened from a context menu keeps of it for ctx->item,
 * ctx->row and ctx->list. A slot stays put while its screen moves through open and closing,
 * so ctx->item does not move under a callback. */
static struct {
    uint32_t item[MediaItem_bytes / 4];
    int32_t row;
    void *list;
    uint8_t used, has_item;
} origins[MAX_OPEN + 1];

/* A slot for a screen pushed on `controller`: the menu whose row runs, else
 * what the screen underneath keeps; 0 for none. */
static uint8_t origin_keep(void *controller)
{
    const struct core_menu_origin *menu = core_screens_menu;
    struct core_menu_origin under;
    for (uint32_t i = 0; !menu && i < open_count; i++)
        if (open[i].controller == controller && open[i].origin) {
            under.item = origins[open[i].origin - 1].has_item ? origins[open[i].origin - 1].item
                                                              : NULL;
            under.row = origins[open[i].origin - 1].row;
            under.list = origins[open[i].origin - 1].list;
            menu = &under;
        }
    if (!menu)
        return 0;
    for (uint32_t n = 0; n < sizeof origins / sizeof origins[0]; n++) {
        if (origins[n].used)
            continue;
        origins[n].used = 1;
        origins[n].has_item = menu->item != NULL;
        origins[n].row = menu->row;
        origins[n].list = menu->list;
        for (uint32_t w = 0; menu->item && w < MediaItem_bytes / 4; w++)
            origins[n].item[w] = ((const uint32_t *)menu->item)[w];
        return (uint8_t)(n + 1);
    }
    return 0;
}

static void origin_into(uint8_t origin, struct rv_context *ctx)
{
    if (!origin)
        return;
    ctx->item = origins[origin - 1].has_item ? origins[origin - 1].item : NULL;
    ctx->row = origins[origin - 1].row;
    ctx->list = origins[origin - 1].list;
}

static void origin_release(uint8_t origin)
{
    if (origin)
        origins[origin - 1].used = 0;
}

void core_screens_context(const struct core_screen *s, struct rv_context *ctx)
{
    ctx->self = s ? s->owner : NULL;
    ctx->controller = NULL;
    ctx->arg = NULL;
    ctx->item = NULL;
    ctx->row = -1;
    ctx->list = NULL;
    for (uint32_t i = open_count; s && i-- > 0;)
        if (open[i].screen == s) {
            ctx->controller = open[i].controller;
            ctx->arg = open[i].arg;
            origin_into(open[i].origin, ctx);
            return;
        }
    if (s && closing.screen == s) {
        ctx->controller = closing.controller;
        ctx->arg = closing.arg;
        origin_into(closing.origin, ctx);
    }
}

static struct core_screen *popped_canvas;

const char *core_row_label(const struct rv_row *row, struct rv_extension_record *owner,
                           const struct core_screen *screen)
{
    if (!row->text)
        return row->label;
    struct rv_context ctx;
    core_screens_context(screen, &ctx);
    ctx.self = owner;
    const char *text = row->text(&ctx);
    return text ? text : row->label;
}

void core_screens_closed(void)
{
    struct core_screen *s = closing.screen;
    uint8_t origin = closing.origin;
    closing.screen = NULL;
    closing.origin = 0;
    if (s && s == popped_canvas)
        popped_canvas = NULL; /* its callbacks may have freed what it draws with */
    if (s && s->declaration && s->declaration->closed) {
        struct rv_context ctx = {s->owner, closing.controller, closing.arg, NULL, -1, NULL};
        origin_into(origin, &ctx);
        s->declaration->closed(&ctx);
    }
    origin_release(origin);
}

void core_screens_tick(void)
{
    /* At least one whole grace tick has passed since the pop. */
    if (closing.screen && closing.ticks++)
        core_screens_closed();
}

int core_screens_first_activation(void *controller)
{
    for (uint32_t i = 0; i < open_count; i++)
        if (open[i].controller == controller && !open[i].selected) {
            open[i].selected = 1;
            return 1;
        }
    return 0;
}

void core_screens_select(struct core_screen *s, uint32_t row)
{
    if (s->declaration->kind != RV_SCREEN_KIND_DYNAMIC_LIST)
        return;
    for (uint32_t i = open_count; i-- > 0;)
        if (open[i].screen == s) {
            if (open[i].controller == core_screens_top_controller())
                core_indexed_select(s, row);
            else {
                open[i].selection = row;
                open[i].selection_pending = 1;
            }
            return;
        }
}

int core_screens_take_selection(void *controller, uint32_t *row)
{
    for (uint32_t i = 0; i < open_count; i++)
        if (open[i].controller == controller && open[i].selection_pending) {
            *row = open[i].selection;
            open[i].selection_pending = 0;
            return 1;
        }
    return 0;
}

void *core_screens_top_controller(void)
{
    return *(void **)((uint8_t *)Navigator_Get() + Navigator_top_controller);
}

struct core_screen *core_screens_top_canvas(void)
{
    struct core_screen *s = core_screens_of_controller(core_screens_top_controller());
    return s && s->declaration && s->declaration->kind == RV_SCREEN_KIND_CANVAS ? s : NULL;
}

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
        core_panes_screen_closed(s);
        open[i].screen = NULL;
        abandoned++;
    }
    if (closing.screen && closing.screen->owner == owner)
        closing.screen = NULL;
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
        if (!d || s->owner != owner || d->kind != RV_SCREEN_KIND_LIST)
            continue;
        for (uint32_t r = 0; r < d->list.count; r++)
            if (d->list.rows[r].kind == RV_ROW_KIND_SETTING &&
                d->list.rows[r].key == key)
                core_screens_notify(CORE_SCREEN_ROWS(s) + 2 * r + 1);
    }
    core_menus_setting_changed(owner, key);
    core_loader_setting_changed(owner, key);
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

/* popped_canvas: the canvas popped last while no other canvas stays open,
 * until the next push or its closed callback. A pop deactivates the
 * controller before the transition draws the leaving screen once more, into
 * the copy that slides out. */

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
        void *arg = open[i].arg;
        uint8_t origin = open[i].origin;
        open_count--;
        for (uint32_t j = i; j < open_count; j++)
            open[j] = open[j + 1];
        if (!s) {
            origin_release(origin);
            return;
        }
        core_screens_closed(); /* an earlier one still waiting */
        closing.screen = s;
        closing.controller = controller;
        closing.arg = arg;
        closing.ticks = 0;
        closing.origin = origin;
        if (s->open)
            s->open--;
        if (!s->open) {
            core_bitmaps_screen_closed(s);
            core_panes_screen_closed(s);
        }
        if (!s->open && !s->owner)
            core_extensions_closed(s);
        if (s->owner)
            core_loader_screen_popped(s->owner, s->module);
        if (s->declaration && s->declaration->kind == RV_SCREEN_KIND_CANVAS &&
            !core_screens_top(RV_SCREEN_KIND_CANVAS))
            popped_canvas = s;
        if (popped_canvas != s)
            core_screens_closed();
        return;
    }
}

/* Every deactivate of the three controller classes passes here; only the
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
    return RV_FIELD(controller, 0, void *) == (void *)SettingsMenuListController_vtable;
}

static void *bank;

static int build(struct core_screen *s, int split, uint32_t screen)
{
    uint8_t bit = split ? CORE_BUILT_SPLIT : CORE_BUILT_FULL;
    uint32_t bytes;
    /* An owner loaded again before its last release finished finds its
     * clones still in the bank; a controller may still read them. */
    if (s->owner && ResourceBank_GetResource(bank, Resource_SCST, screen, &bytes))
        s->built |= bit;
    if (s->built & bit)
        return 1;
    int built = 0;
    switch (s->declaration->kind) {
    case RV_SCREEN_KIND_LIST:
    case RV_SCREEN_KIND_CHOICE:
    case RV_SCREEN_KIND_MULTI_CHOICE:
    case RV_SCREEN_KIND_DYNAMIC_LIST:
        built = core_clone_list(s, split);
        break;
    case RV_SCREEN_KIND_TEXT:
        built = core_clone_text(s);
        break;
    case RV_SCREEN_KIND_SLIDER:
        built = core_clone_slider(s);
        break;
    case RV_SCREEN_KIND_CANVAS:
        built = core_clone_canvas(s);
        break;
    case RV_SCREEN_KIND_STOCK:
        built = core_clone_stock(s);
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
    case RV_SCREEN_KIND_SLIDER:
        return core_template(CORE_TEMPLATE_SLIDER);
    case RV_SCREEN_KIND_CANVAS:
        return core_template(CORE_TEMPLATE_CANVAS);
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
    uint8_t from = RV_FIELD(UI_GetLayoutManager(), LayoutManager_kind, uint8_t);
    uint32_t template = template_of(kind);
    if (s->split)
        return Transition_Standard;
    if (!template)
        return Transition_ScreenPush;
    int pane = s->declaration->transition == RV_TRANSITION_FROM_PANE && from == LayoutKind_SplitList;
    core_clone_set_style(s->id + 1, template,
                         pane ? core_templates_enter_style(RES_Stopwatch_Template, LayoutKind_SplitList)
                              : -1);
    return from == LayoutKind_FullScreen ? Transition_ScreenPush : Transition_Standard;
}

int core_screens_push(struct core_screen *s, void *controller, void *arg)
{
    uint8_t kind = s->declaration->kind;
    int input = kind == RV_SCREEN_KIND_SLIDER || kind == RV_SCREEN_KIND_CANVAS;
    int split = core_lists_kind(kind) && split_from(s, controller);
    if (open_count == MAX_OPEN || (input && !core_canvas_prepare()) || !core_indexed_prepare(s))
        return 0;
    popped_canvas = NULL;
    core_screens_closed();
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
    open[open_count].arg = arg;
    open[open_count].selected = 0;
    open[open_count].selection_pending = 0;
    open[open_count].origin = origin_keep(controller);
    open_count++;
    s->open++;
    if (s->owner)
        core_loader_screen_pushed(s->owner, s->module);
    void (*push)(void *, void *) =
        RV_FIELD(RV_FIELD(controller, 0, void **), ScreenController_push_method, void *);
    push(controller, created);
    if (s->declaration->opened) {
        struct rv_context ctx = {s->owner, created, arg, NULL, -1, NULL};
        origin_into(open[open_count - 1].origin, &ctx);
        s->declaration->opened(&ctx);
    }
    return 1;
}

void core_screens_pop(struct rv_extension_record *owner)
{
    struct core_screen *s = core_screens_of_controller(core_screens_top_controller());
    if (s && s->owner == owner)
        Navigator_PopTop(0x10);
}

void core_screens_invalidate(struct rv_extension_record *owner, const struct rv_rect *rect)
{
    struct core_screen *s = core_screens_top_canvas();
    if (s && s->owner == owner)
        core_canvas_invalidate(rect);
}

static const struct core_hook hooks[] = {
    {CORE_SITE_ScreenController_Deactivate_slot, CORE_HOOK_FILTER, 0, deactivated},
    {CORE_SITE_SettingsMenuListController_Deactivate_slot, CORE_HOOK_FILTER, 0, deactivated},
    {CORE_SITE_GamesMenuController_Deactivate_slot, CORE_HOOK_FILTER, 0, deactivated},
};

void core_screens_init(void *resource_bank)
{
    bank = resource_bank;
    core_properties_init();
    core_resources_init(bank);
    core_clones_init(bank);
    core_panes_init(bank);
    core_lists_init(bank);
    core_toggles_init(bank);
    core_indexed_init(bank);
    core_canvas_init();
    core_templates_init(bank);
    for (uint32_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        core_hook_bind(&hooks[i]);
}
