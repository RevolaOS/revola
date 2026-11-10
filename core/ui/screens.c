/* SPDX-License-Identifier: GPL-3.0-only */

/* Screen registry and navigation. A declared screen gets ids from its owner's
 * block and, on first push, the clones in clones.c and lists.c; its text comes
 * from the Settings model properties in properties.c. Clones survive the
 * owner's unload (Apple keeps pointers) and are reused when it loads again in
 * the same boot. */

#include "ui/screens.h"
#include "ui/submenus.h"
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

/* Records in one heap block per registration of an owner, kept for the boot:
 * a stale record keeps its clones' ids for the owner's next load. */
struct screen_set {
    struct screen_set *next;
    uint32_t key; /* core_resource_owner of the owner */
    uint32_t count;
    struct core_screen screens[];
};

static struct screen_set *sets;
void *core_screens_caller;

#define EACH_SCREEN(s)                                                                    \
    for (struct screen_set *set_ = sets; set_; set_ = set_->next)                          \
        for (struct core_screen *s = set_->screens; s < set_->screens + set_->count; s++)

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

/* Registry */

/* The owner's stale record of a screen with this name and size, or NULL. */
static struct core_screen *stale_record(uint32_t key, uint32_t name, uint32_t ids)
{
    for (struct screen_set *set = sets; set; set = set->next)
        for (uint32_t i = 0; set->key == key && i < set->count; i++)
            if (set->screens[i].stale && set->screens[i].ids == ids &&
                set->screens[i].name == name)
                return &set->screens[i];
    return NULL;
}

int core_screens_register(struct rv_extension_record *owner, const struct rv_screen *screens,
                          uint32_t count)
{
    uint32_t key = core_resource_owner(owner), fresh = 0;
    for (uint32_t n = 0; n < count; n++)
        fresh += !stale_record(key, core_settings_owner(screens[n].name),
                               core_clone_ids(&screens[n]));
    struct screen_set *set = NULL;
    if (fresh) {
        set = Heap_AllocateZeroed(sizeof *set + fresh * sizeof set->screens[0]);
        if (!set)
            return RV_NO_MEMORY;
        set->key = key;
        struct screen_set **tail = &sets;
        while (*tail)
            tail = &(*tail)->next;
        *tail = set;
    }
    for (uint32_t n = 0; n < count; n++) {
        const struct rv_screen *d = &screens[n];
        uint32_t name = core_settings_owner(d->name), ids = core_clone_ids(d);
        struct core_screen *s = stale_record(key, name, ids);
        if (!s) {
            uint32_t id = core_resource_take(key, ids);
            if (!id)
                return RV_NO_MEMORY;
            s = &set->screens[set->count++];
            s->name = name;
            s->id = id;
            s->ids = ids;
        }
        s->owner = owner;
        s->declaration = d;
        s->stale = 0;
    }
    return RV_OK;
}

void core_screens_release(struct rv_extension_record *owner)
{
    EACH_SCREEN(s)
        if (s->owner == owner && s->declaration) {
            s->declaration = NULL;
            s->stale = 1;
        }
}

void core_screens_retire(const struct rv_screen *declaration)
{
    EACH_SCREEN(s)
        if (s->declaration == declaration) {
            s->declaration = NULL;
            s->stale = 1;
        }
}

struct core_screen *core_screens_find(struct rv_extension_record *owner, const char *name)
{
    EACH_SCREEN(s)
        if (s->declaration && s->owner == owner && same(s->declaration->name, name))
            return s;
    return NULL;
}

const struct rv_setting *core_screens_setting(struct rv_extension_record *owner, uint16_t key)
{
    const struct rv_extension *d = owner ? owner->descriptor : NULL;
    for (uint32_t i = 0; d && i < d->setting_count; i++)
        if (d->settings[i].key == key)
            return &d->settings[i];
    return NULL;
}

int32_t core_screens_setting_value(struct rv_extension_record *owner, uint16_t key)
{
    const struct rv_setting *setting = core_screens_setting(owner, key);
    return core_settings_get_int(owner ? owner->owner : CORE_SETTINGS_OWNER_CORE, key,
                                 setting ? setting->fallback : 0);
}

/* A setting with labels and a style edits as a screen of that style, titled
 * and named by its label. */
static uint8_t setting_screen_kind(const struct rv_setting *setting)
{
    if (setting->type != RV_SETTING_INT || !setting->labels || !setting->label_count)
        return 0xff;
    switch (setting->style) {
    case RV_STYLE_CHOICE:
        return RV_SCREEN_CHOICE;
    case RV_STYLE_SLIDER:
        return RV_SCREEN_SLIDER;
    case RV_STYLE_MULTI:
        return RV_SCREEN_MULTI_CHOICE;
    default:
        return 0xff;
    }
}

uint32_t core_screens_from_settings(const struct rv_setting *settings, uint32_t count,
                                    struct rv_screen *out)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t kind = setting_screen_kind(&settings[i]);
        if (kind == 0xff)
            continue;
        if (out) {
            struct rv_screen *s = &out[n];
            s->name = s->title = settings[i].label;
            s->kind = kind;
            s->layout = RV_LAYOUT_AUTO;
            s->choice.key = settings[i].key;
            s->choice.labels = settings[i].labels;
            s->choice.count = settings[i].label_count;
        }
        n++;
    }
    return n;
}

struct core_screen *core_screens_find_setting(struct rv_extension_record *owner, uint16_t key)
{
    EACH_SCREEN(s) {
        const struct rv_screen *d = s->declaration;
        if (d && s->owner == owner && d->kind != RV_SCREEN_LIST && d->kind != RV_SCREEN_TEXT &&
            d->kind != RV_SCREEN_CANVAS && d->kind != RV_SCREEN_RUNTIME_LIST && d->choice.key == key)
            return s;
    }
    return NULL;
}

struct core_screen *core_screens_of_id(uint32_t id)
{
    for (struct screen_set *set = sets; set; set = set->next) {
        if (!set->count || id - set->screens[0].id >= CORE_RESOURCE_BLOCK_BYTES)
            continue; /* another owner's block */
        for (uint32_t i = 0; i < set->count; i++) {
            struct core_screen *s = &set->screens[i];
            if (s->declaration && id - s->id < s->ids)
                return s;
        }
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

void core_screens_setting_changed(struct rv_extension_record *owner, uint16_t key)
{
    for (uint32_t i = 0; i < open_count; i++) {
        struct core_screen *s = open[i].screen;
        const struct rv_screen *d = s->declaration;
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
        if (!s->open)
            core_bitmaps_screen_closed(s);
        if (s->owner && s->declaration)
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

static int build(struct core_screen *s, int split)
{
    uint8_t bit = split ? CORE_BUILT_SPLIT : CORE_BUILT_FULL;
    if (!s->open && core_submenus_stale(s))
        s->built = 0;
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

int core_screens_push(struct core_screen *s, void *controller)
{
    uint8_t kind = s->declaration->kind;
    int input = kind == RV_SCREEN_SLIDER || kind == RV_SCREEN_CANVAS;
    int split = core_lists_kind(kind) && split_from(s, controller);
    if (open_count == MAX_OPEN || (input && !core_canvas_prepare()) || !core_indexed_prepare(s))
        return 0;
    if (s->owner)
        core_loader_screen_pushed(s->declaration->name);
    s->split = (uint8_t)split;
    if (!build(s, split))
        return 0;
    uint32_t screen = core_lists_kind(kind) ? core_lists_screen_id(s, split) : s->id;
    void *created = UI_CreateScreenController(UI_GetControllerRegistry(), screen, screen + 1,
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
    core_submenus_init(bank);
    core_toggles_init(bank);
    core_indexed_init(bank);
    core_canvas_init();
    core_templates_init(bank);
    for (uint32_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        core_hook_bind(&hooks[i]);
}
