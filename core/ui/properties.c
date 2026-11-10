/* SPDX-License-Identifier: GPL-3.0-only */

/* Settings model properties and actions of the runtime's screens and menu rows.
 * Titles, labels and paragraphs are properties answered from the declarations,
 * so no string is copied and a module's text is read only while it is loaded;
 * screens and menu rows share one filter on the slot, which bails out on
 * Apple's property ids at once. Row handlers are named "Rv" plus the row's item
 * id in hex, so every action slot resolves them the same way. */

#include "ui/properties.h"
#include "osos-2.0.5.h"
#include "ui/canvas.h"
#include "ui/indexed.h"
#include "ui/lists.h"
#include "ui/menus.h"
#include "ui/panes.h"
#include "ui/resources.h"
#include "ui/screens.h"
#include "ui/toggles.h"
#include "settings.h"
#include "hooks/sites.h"
#include <stddef.h>

#define FIELD(base, offset, type) (*(type *)((uint8_t *)(base) + (offset)))

static void *settings_model; /* last seen on the property slot; refresh notifies it */

static uint32_t settings_owner(struct rv_extension_record *owner)
{
    return owner ? owner->owner : CORE_SETTINGS_OWNER_CORE;
}

void *core_screens_settings_model(void)
{
    return settings_model;
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
    if (s->declaration->kind != RV_SCREEN_KIND_LIST || id < rows ||
        (id - rows) / 2 >= s->declaration->list.count)
        return NULL;
    *item = rows + (id - rows) / 2 * 2;
    return &s->declaration->list.rows[(id - rows) / 2];
}

/* A setting row's value: the chosen label of a setting with labels (nothing
 * for a multi-choice), else On or Off. An info row has the value row's
 * layout, for its lack of a chevron, and no value. */
static const char *row_text(struct rv_extension_record *owner, const struct core_screen *s,
                            const struct rv_row *row, int value)
{
    if (value && row->kind == RV_ROW_KIND_INFO)
        return "";
    if (!value || row->kind != RV_ROW_KIND_SETTING)
        return core_row_label(row, owner, s);
    const struct rv_setting *setting = core_screens_setting(owner, row->key);
    int32_t v = core_screens_setting_value(owner, row->key);
    if (setting && setting->labels) {
        if (setting->style == RV_SETTING_STYLE_MULTI || (uint32_t)v >= setting->label_count)
            return "";
        return setting->labels[v];
    }
    return v ? "On" : "Off";
}

static uint32_t property(uint32_t model, uint32_t type, uint32_t id, uint32_t out,
                         const struct core_next *next)
{
    settings_model = (void *)model;
    if (id >> 24 != CORE_RESOURCE_PREFIX)
        return core_toggles_property(model, type, id, out, next);
    struct core_screen *s = core_screens_of_id(id);
    struct rv_extension_record *owner = s ? s->owner : NULL;
    uint32_t item = 0;
    const struct rv_row *row = s ? screen_row(s, id, &item) : core_menus_row(id, &owner, &item);
    if (!s && !row && core_menus_owns(id) && type == Property_Count) {
        *(uint32_t *)out = 1; /* a hidden row of a gone extension keeps the list's count */
        return 1;
    }
    if (!s && !row)
        return core_panes_property(type, id, out) ? 1 : core_next_call(next, model, type, id, out);
    if (s && s->declaration->kind == RV_SCREEN_KIND_SLIDER && id >= CORE_SCREEN_ROWS(s))
        return core_slider_property(s, type, id, out) ? 1 : core_next_call(next, model, type, id, out);
    if (s && core_indexed_property(s, type, id, out))
        return 1;
    if (s && core_panes_list_property(s, type, id, out))
        return 1;
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
        text = row_text(owner, s, row, id == item + 1);
    else if (id == s->id + 2)
        text = s->declaration->title;
    else if (s->declaration->kind == RV_SCREEN_KIND_TEXT && id >= CORE_SCREEN_ROWS(s))
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
    if (core_menus_pane(item, argument))
        return 1;
    struct core_screen *s = core_screens_of_id(item);
    struct rv_extension_record *owner = s ? s->owner : NULL;
    const struct rv_row *row = s ? screen_row(s, item, &found) : core_menus_row(item, &owner, &found);
    uint8_t kind = s ? s->declaration->kind : RV_SCREEN_KIND_LIST;
    int indexed = core_lists_indexed(kind);
    if (!row && !indexed)
        return 0;
    if (argument == ActionSupportQuery)
        return 1;
    /* A context menu's row hands its item and row to what it runs and opens. */
    const struct rv_menu_entry *e = s ? NULL : core_menus_entry(found, &owner);
    int context = e && e->menu >= RV_MENU_NOW_PLAYING_CONTEXT;
    uint32_t media[MediaItem_bytes / 4];
    struct core_menu_origin menu = {NULL, -1};
    const struct core_menu_origin *outer = core_screens_menu;
    if (context) {
        menu.item = core_context_item(e->menu, controller, media, &menu.row);
        core_screens_menu = &menu;
    }
    core_screens_caller = controller;
    if (indexed) {
        core_indexed_chosen(s);
        core_screens_caller = NULL;
        return 1;
    }
    struct core_screen *target;
    switch (row->kind) {
    case RV_ROW_KIND_PUSH: {
        target = core_loader_screen(owner, row->screen);
        int pushed = target && core_screens_push(target, controller, NULL);
        if (context)
            core_context_pushed(controller, pushed);
        break;
    }
    case RV_ROW_KIND_SETTING:
        target = core_screens_find_setting(owner, row->key);
        if (target) {
            core_screens_push(target, controller, NULL);
            break;
        }
        core_settings_set_int(settings_owner(owner), row->key,
                              !core_screens_setting_value(owner, row->key));
        core_screens_setting_changed(owner, row->key);
        break;
    case RV_ROW_KIND_ACTION:
        if (row->action) {
            struct rv_context ctx;
            core_screens_context(s, &ctx);
            ctx.self = owner;
            ctx.controller = controller;
            if (context) {
                ctx.item = menu.item;
                ctx.row = menu.row;
            }
            row->action(&ctx);
        }
        break;
    default:
        break;
    }
    core_screens_caller = NULL;
    core_screens_menu = outer;
    return 1;
}

int core_screens_context_push(const char *name)
{
    uint32_t item = parse_handler(name), found;
    struct rv_extension_record *owner;
    if (!item || core_screens_of_id(item))
        return 0;
    const struct rv_row *row = core_menus_row(item, &owner, &found);
    const struct rv_menu_entry *e = row ? core_menus_entry(found, &owner) : NULL;
    return e && e->menu >= RV_MENU_NOW_PLAYING_CONTEXT && row->kind == RV_ROW_KIND_PUSH;
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

void core_properties_init(void)
{
    for (uint32_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        core_hook_bind(&hooks[i]);
}
