/* SPDX-License-Identifier: GPL-3.0-only */

/* Screen registry. A declared screen gets ids from its owner's block and, on
 * first push, the clones in clones.c. Titles, labels and paragraphs are Settings
 * model properties answered here from the declaration, so no string is copied
 * and the module's text is read only while its screen is open; the menu rows'
 * properties are answered here as well, so the slot carries one filter. Row
 * handlers are named "Rv" plus the row's item id in hex, so every action slot
 * resolves them the same way. Clones survive the owner's unload (Apple keeps
 * pointers) and are reused when it loads again in the same boot. */

#include "screens.h"
#include "clones.h"
#include "menus.h"
#include "osos-2.0.5.h"
#include "resources.h"
#include "settings.h"
#include "sites.h"
#include <stddef.h>

#define MAX_SCREENS 16
#define FIELD(base, offset, type) (*(type *)((uint8_t *)(base) + (offset)))

struct core_screen core_screens[MAX_SCREENS];
static void *settings_model; /* last seen on the property slot; refresh notifies it */

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static uint32_t settings_owner(struct rv_extension_record *owner)
{
    return owner ? owner->owner : CORE_SETTINGS_OWNER_CORE;
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

static struct core_screen *screen_of_id(uint32_t id)
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

/* Controllers the runtime pushed, so a deactivate finds its screen without
 * knowing the controller class. */
#define MAX_OPEN 8
static struct {
    void *controller;
    struct core_screen *screen;
} open[MAX_OPEN];

/* Copies of Apple's controller vtables with deactivate replaced, so the runtime
 * sees a screen leave. The prefix before the address point is copied too. */
#define VTABLE_WORDS ((CxxVtable_prefix_bytes + ScreenController_vtable_bytes) / 4)
static void *list_vtable[VTABLE_WORDS];
static void *plain_vtable[VTABLE_WORDS];
static uint32_t (*list_deactivate)(void *, uint32_t);
static uint32_t (*plain_deactivate)(void *, uint32_t);

static void screen_left(void *controller)
{
    for (uint32_t i = 0; i < MAX_OPEN; i++) {
        if (open[i].controller != controller)
            continue;
        struct core_screen *s = open[i].screen;
        open[i].controller = NULL;
        if (s->open)
            s->open--;
        if (s->declaration)
            core_loader_screen_popped(s->declaration->name);
    }
}

static uint32_t deactivate_list(void *controller, uint32_t leaving)
{
    uint32_t result = list_deactivate(controller, leaving);
    if (leaving)
        screen_left(controller);
    return result;
}

static uint32_t deactivate_plain(void *controller, uint32_t leaving)
{
    uint32_t result = plain_deactivate(controller, leaving);
    if (leaving)
        screen_left(controller);
    return result;
}

/* A slot the dispatcher patches in Apple's vtable points at the site's stub in
 * the copy, so handlers bound later still see it. */
static void copy_vtable(void **copy, void *const *original, void *deactivate,
                        uint32_t (**saved)(void *, uint32_t))
{
    const uint8_t *from = (const uint8_t *)original - CxxVtable_prefix_bytes;
    for (uint32_t i = 0; i < VTABLE_WORDS; i++)
        copy[i] = ((void *const *)from)[i];
    for (uint32_t site = 0; site < CORE_SITE_COUNT; site++) {
        uint32_t slot = (core_sites[site].address - (uint32_t)from) / 4;
        if (core_sites[site].kind == CORE_SITE_VTABLE && slot < VTABLE_WORDS)
            copy[slot] = (void *)core_site_stubs[site];
    }
    uint32_t slot = (CxxVtable_prefix_bytes + ScreenController_deactivate_method) / 4;
    *saved = (uint32_t (*)(void *, uint32_t))copy[slot];
    copy[slot] = deactivate;
}

static int build(struct core_screen *s)
{
    if (s->built)
        return 1;
    switch (s->declaration->kind) {
    case RV_SCREEN_LIST:
        s->built = core_clone_list(s);
        break;
    case RV_SCREEN_TEXT:
        s->built = core_clone_text(s);
        break;
    default:
        break;
    }
    return s->built;
}

int core_screens_push(struct core_screen *s, void *controller)
{
    uint32_t slot = 0;
    while (slot < MAX_OPEN && open[slot].controller)
        slot++;
    if (slot == MAX_OPEN)
        return 0;
    core_loader_screen_pushed(s->declaration->name);
    if (!build(s))
        return 0;
    void *created = UI_CreateScreenController(UI_GetControllerRegistry(), s->id, s->id + 1,
                                              Transition_ForegroundPush);
    if (!created)
        return 0;
    void **copy = s->declaration->kind == RV_SCREEN_LIST ? list_vtable : plain_vtable;
    FIELD(created, 0, void **) = copy + CxxVtable_prefix_bytes / 4;
    open[slot].controller = created;
    open[slot].screen = s;
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
    /* TODO: canvas redraw, with the canvas view. */
}

void core_screens_notify(uint32_t property)
{
    if (!settings_model)
        return;
    void (*notify)(void *, uint32_t, uint32_t) =
        FIELD(FIELD(settings_model, 0, void **), Model_notify_method, void *);
    notify(settings_model, Notify_All, property);
}

void core_screens_refresh(struct core_screen *s)
{
    if (s->built)
        for (uint32_t id = s->id + 2; id < s->id + s->ids; id++)
            core_screens_notify(id);
}

/* Properties */

/* The row of a list screen behind an item or source id, with its item id. */
static const struct rv_row *screen_row(struct core_screen *s, uint32_t id, uint32_t *item)
{
    uint32_t rows = CORE_SCREEN_ROWS(s);
    if (s->declaration->kind != RV_SCREEN_LIST || id < rows)
        return NULL;
    *item = rows + (id - rows) / 2 * 2;
    return &s->declaration->list.rows[(id - rows) / 2];
}

static const char *row_text(struct rv_extension_record *owner, const struct rv_row *row,
                            int value)
{
    if (!value || row->kind != RV_ROW_TOGGLE)
        return row->label;
    return core_settings_get_int(settings_owner(owner), row->key, 0) ? "On" : "Off";
}

static uint32_t property(uint32_t model, uint32_t type, uint32_t id, uint32_t out,
                         const struct core_next *next)
{
    settings_model = (void *)model;
    if (id >> 24 != CORE_RESOURCE_PREFIX)
        return core_next_call(next, model, type, id, out);
    struct core_screen *s = screen_of_id(id);
    struct rv_extension_record *owner = s ? s->owner : NULL;
    uint32_t item = 0;
    const struct rv_row *row = s ? screen_row(s, id, &item) : core_menus_row(id, &owner, &item);
    if (!s && !row)
        return core_next_call(next, model, type, id, out);
    /* The last model-backed row of a list sets the list's row count: one row per
     * title property, so the list shows every row. */
    if (type == Property_Count) {
        *(uint32_t *)out = 1;
        return 1;
    }
    if (type != Property_String)
        return core_next_call(next, model, type, id, out);
    const char *text;
    if (row)
        text = row_text(owner, row, id == item + 1);
    else if (id == s->id + 2)
        text = s->declaration->title;
    else if (s->declaration->kind == RV_SCREEN_TEXT && id >= CORE_SCREEN_ROWS(s))
        text = s->declaration->text.paragraphs[id - CORE_SCREEN_ROWS(s)];
    else
        return core_next_call(next, model, type, id, out);
    *(const char **)out = text;
    return 1;
}

/* Actions */

static uint32_t parse_handler(const char *name)
{
    if (name[0] != 'R' || name[1] != 'v')
        return 0;
    uint32_t value = 0;
    for (unsigned i = 2; i < 10; i++) {
        char c = name[i];
        uint32_t digit = c >= '0' && c <= '9' ? (uint32_t)(c - '0')
                         : c >= 'a' && c <= 'f' ? (uint32_t)(c - 'a' + 10)
                                                : 16;
        if (digit > 15)
            return 0;
        value = value << 4 | digit;
    }
    return name[10] ? 0 : value;
}

uint32_t core_screens_action(void *controller, const char *name, uint32_t argument)
{
    uint32_t item = parse_handler(name), found;
    if (!item)
        return 0;
    struct core_screen *s = screen_of_id(item);
    struct rv_extension_record *owner = s ? s->owner : NULL;
    const struct rv_row *row = s ? screen_row(s, item, &found) : core_menus_row(item, &owner, &found);
    if (!row)
        return 0;
    if (argument == ActionSupportQuery)
        return 1;
    struct core_screen *target;
    switch (row->kind) {
    case RV_ROW_PUSH:
        target = core_screens_find(owner, row->screen);
        if (target)
            core_screens_push(target, controller);
        break;
    case RV_ROW_TOGGLE: {
        uint32_t id = settings_owner(owner);
        core_settings_set_int(id, row->key, !core_settings_get_int(id, row->key, 0));
        if (s)
            core_screens_refresh(s);
        else
            core_screens_notify(item + 1);
        break;
    }
    case RV_ROW_ACTION:
        if (row->action)
            row->action(controller);
        break;
    default:
        break;
    }
    return 1;
}

uint32_t core_screens_action_filter(uint32_t controller, uint32_t name, uint32_t argument,
                                    const struct core_next *next)
{
    if (core_screens_action((void *)controller, (const char *)name, argument))
        return 1;
    return core_next_call(next, controller, name, argument, 0);
}

static const struct core_hook hooks[] = {
    {CORE_SITE_Settings_Property_slot, CORE_HOOK_FILTER, 5, property},
    {CORE_SITE_Settings_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
};

void core_screens_init(void *bank)
{
    core_clones_init(bank);
    copy_vtable(list_vtable, SettingsMenuListController_vtable, (void *)deactivate_list,
                &list_deactivate);
    copy_vtable(plain_vtable, ScreenController_vtable, (void *)deactivate_plain, &plain_deactivate);
    for (uint32_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        core_hook_bind(&hooks[i]);
}
