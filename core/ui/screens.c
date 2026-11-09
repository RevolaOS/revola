/* SPDX-License-Identifier: GPL-3.0-only */

/* Screen registry and navigation. A declared screen gets ids from its owner's
 * block and, on first push, the clones in clones.c and lists.c; its text comes
 * from the Settings model properties in properties.c. Clones survive the
 * owner's unload (Apple keeps pointers) and are reused when it loads again in
 * the same boot. */

#include "ui/screens.h"
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

#define MAX_SCREENS 16
#define FIELD(base, offset, type) (*(type *)((uint8_t *)(base) + (offset)))

struct core_screen core_screens[MAX_SCREENS];

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

/* Registry */

int core_screens_register(struct rv_extension_record *owner, const struct rv_screen *screens,
                          uint32_t count)
{
    uint32_t key = core_resource_owner(owner), block = core_resource_block(key);
    for (uint32_t n = 0; n < count; n++) {
        const struct rv_screen *d = &screens[n];
        uint32_t name = core_settings_owner(d->name), ids = core_clone_ids(d);
        struct core_screen *s = NULL;
        for (uint32_t i = 0; i < MAX_SCREENS && !s; i++)
            if (core_screens[i].stale && core_screens[i].id - block < CORE_RESOURCE_BLOCK_BYTES &&
                core_screens[i].ids == ids && core_screens[i].name == name)
                s = &core_screens[i];
        if (s) {
            s->owner = owner;
            s->declaration = d;
            s->stale = 0;
            continue;
        }
        for (uint32_t i = 0; i < MAX_SCREENS && !s; i++)
            if (!core_screens[i].ids)
                s = &core_screens[i];
        if (!s)
            return RV_NO_MEMORY;
        uint32_t id = core_resource_take(key, ids);
        if (!id)
            return core_resource_block_held(key) ? RV_CONFLICT : RV_NO_MEMORY;
        s->owner = owner;
        s->declaration = d;
        s->name = name;
        s->id = id;
        s->ids = ids;
        s->built = s->open = s->stale = 0;
    }
    return RV_OK;
}

void core_screens_release(struct rv_extension_record *owner)
{
    for (uint32_t i = 0; i < MAX_SCREENS; i++) {
        struct core_screen *s = &core_screens[i];
        if (s->ids && s->owner == owner) {
            s->declaration = NULL;
            s->stale = 1;
        }
    }
}

struct core_screen *core_screens_find(struct rv_extension_record *owner, const char *name)
{
    for (uint32_t i = 0; i < MAX_SCREENS; i++) {
        struct core_screen *s = &core_screens[i];
        if (s->declaration && s->owner == owner && same(s->declaration->name, name))
            return s;
    }
    return NULL;
}

struct core_screen *core_screens_of_id(uint32_t id)
{
    for (uint32_t i = 0; i < MAX_SCREENS; i++) {
        struct core_screen *s = &core_screens[i];
        if (s->declaration && id - s->id < s->ids)
            return s;
    }
    return NULL;
}

void core_screens_handler_name(char *out, uint32_t item)
{
    static const char digits[] = "0123456789abcdef";
    out[0] = 'R';
    out[1] = 'v';
    for (unsigned i = 0; i < 8; i++)
        out[2 + i] = digits[(item >> (28 - 4 * i)) & 15];
    out[10] = 0;
}

/* Navigation */

/* Controllers the runtime pushed, in push order, so a deactivate finds its
 * screen without knowing the controller class. */
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

struct core_screen *core_screens_top(uint8_t kind)
{
    for (uint32_t i = open_count; i-- > 0;) {
        struct core_screen *s = open[i].screen;
        if (s->declaration && s->declaration->kind == kind)
            return s;
    }
    return NULL;
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
        if (s->open)
            s->open--;
        if (s->declaration)
            core_loader_screen_popped(s->declaration->name);
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

static int build(struct core_screen *s)
{
    if (s->built)
        return 1;
    switch (s->declaration->kind) {
    case RV_SCREEN_LIST:
    case RV_SCREEN_CHOICE:
    case RV_SCREEN_MULTI_CHOICE:
    case RV_SCREEN_RUNTIME_LIST:
        s->built = core_clone_list(s);
        break;
    case RV_SCREEN_TEXT:
        s->built = core_clone_text(s);
        break;
    case RV_SCREEN_SLIDER:
        s->built = core_clone_slider(s);
        break;
    case RV_SCREEN_CANVAS:
        s->built = core_clone_canvas(s);
        break;
    default:
        break;
    }
    return s->built;
}

int core_screens_push(struct core_screen *s, void *controller)
{
    uint8_t kind = s->declaration->kind;
    int input = kind == RV_SCREEN_SLIDER || kind == RV_SCREEN_CANVAS;
    if (open_count == MAX_OPEN || (input && !core_canvas_prepare()) || !core_indexed_prepare(s))
        return 0;
    core_loader_screen_pushed(s->declaration->name);
    if (!build(s))
        return 0;
    void *created = UI_CreateScreenController(UI_GetControllerRegistry(), s->id, s->id + 1,
                                              Transition_ForegroundPush);
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

void core_screens_init(void *bank)
{
    core_properties_init();
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
