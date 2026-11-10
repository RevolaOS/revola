/* SPDX-License-Identifier: GPL-3.0-only */

/* Containers as submenus. A container's screen lists the shown entries that
 * name it, in registration order: rows of extensions keep their own ids, so
 * their text and actions resolve as anywhere else, and a stock Settings row
 * moved here is a copy of the stock row with the stock row's bindings. The row
 * that opens a container shows while one of its rows shows, nested containers
 * included. The screen is rebuilt on its next push whenever entries came or
 * went. */

#include "ui/submenus.h"
#include "loader/containers.h"
#include "osos-2.0.5.h"
#include "settings.h"
#include "ui/clones.h"
#include "ui/lists.h"
#include "ui/menurows.h"
#include "ui/resources.h"
#include "ui/screens.h"
#include "hooks/sites.h"
#include <stddef.h>

#define ROW_BYTES 0x98
#define MAX_DEPTH 4

static void *bank;
static uint32_t settings_hash;

/* The container a core row opens, or NULL. */
static struct core_container *opened_by(const struct row *r)
{
    if (r->owner || !r->entry || r->entry->row.kind != RV_ROW_PUSH)
        return NULL;
    return core_containers_find(core_settings_owner(r->entry->row.screen));
}

static int rows_show(const struct core_container *c, uint32_t depth);

static int row_shows(const struct row *r, uint32_t depth)
{
    if (r->stale || !r->entry)
        return 0;
    struct core_container *child = opened_by(r);
    if (child)
        return depth < MAX_DEPTH && rows_show(child, depth + 1);
    return core_submenus_shown(r);
}

/* 1 when a row in the container shows. */
static int rows_show(const struct core_container *c, uint32_t depth)
{
    EACH_ROW(r)
        if (r->root == CORE_ROOT_CONTAINER && r->container == c->id && row_shows(r, depth))
            return 1;
    return 0;
}

/* 1 when the container and every container above it are declared and the top
 * one is under Settings. */
static int under_settings(const struct core_container *c)
{
    for (uint32_t depth = 0; c && depth < MAX_DEPTH; depth++) {
        if (c->parent == settings_hash)
            return 1;
        c = core_containers_find(c->parent);
    }
    return 0;
}

int core_submenus_shown(const struct row *r)
{
    if (r->stale || !r->entry)
        return 0;
    struct core_container *child = opened_by(r);
    if (child)
        return rows_show(child, 0);
    if (r->entry->row.kind == RV_ROW_STOCK)
        return r->root == CORE_ROOT_CONTAINER && r->entry->row.stock != RES_SettingsMenu_Legal_Item &&
               under_settings(core_containers_find(r->container));
    return 1;
}

static const void *stock(uint32_t type, uint32_t id);

int core_submenus_moves(uint32_t id, const void *data)
{
    uint32_t bytes;
    const uint8_t *items = ResourceBank_GetResource(bank, Resource_ITEM, RES_SettingsMenu_Items, &bytes);
    if (!items || (uint32_t)((const uint8_t *)data - items) >= bytes)
        return 0; /* only the Settings table loses the row; a container's copy stays */
    EACH_ROW(r)
        if (r->entry && r->entry->row.kind == RV_ROW_STOCK && r->entry->row.stock == id &&
            core_submenus_shown(r))
            return 1;
    return 0;
}

struct core_container *core_submenus_of(const struct core_screen *s)
{
    struct core_container *c = s->owner || !s->declaration
                                   ? NULL
                                   : core_containers_find(core_settings_owner(s->declaration->name));
    return c && s->declaration == &c->screen ? c : NULL;
}

int core_submenus_stale(struct core_screen *s)
{
    struct core_container *c = core_submenus_of(s);
    if (!c || !c->dirty)
        return 0;
    c->dirty = 0;
    return 1;
}

/* Event streams: a count, then per event the name, '1', the handler, a word
 * that says whether an action follows, the action and its arguments. Strings
 * are a length word and the bytes; words are unaligned. */
struct event {
    const uint8_t *start, *name, *handler_end, *end;
    uint32_t name_bytes;
    const uint8_t *action;
    uint32_t action_bytes;
};

static uint32_t word(const uint8_t *at)
{
    return at[0] | at[1] << 8 | at[2] << 16 | (uint32_t)at[3] << 24;
}

static const uint8_t *read_event(const uint8_t *at, struct event *e)
{
    e->start = at;
    e->name_bytes = word(at);
    e->name = at + 4;
    at = e->name + e->name_bytes + 1;
    at += 4 + word(at);
    e->handler_end = at;
    e->action = NULL;
    e->action_bytes = 0;
    if (word(at)) {
        at += 4;
        e->action_bytes = word(at);
        e->action = at + 4;
        at = e->action + e->action_bytes;
        uint32_t arguments = word(at);
        at += 4;
        for (uint32_t i = 0; i < arguments; i++)
            at += 4 + word(at);
    } else {
        at += 4;
    }
    e->end = at;
    return at;
}

static int starts(const uint8_t *text, uint32_t bytes, const char *prefix, uint32_t prefix_bytes)
{
    if (bytes < prefix_bytes)
        return 0;
    for (uint32_t i = 0; i < prefix_bytes; i++)
        if (text[i] != (uint8_t)prefix[i])
            return 0;
    return 1;
}

static int same_text(const uint8_t *text, uint32_t bytes, const char *value)
{
    uint32_t i = 0;
    for (; i < bytes && value[i]; i++)
        if (text[i] != (uint8_t)value[i])
            return 0;
    return i == bytes && !value[i];
}

/* Copies the events of `stream` whose names start with `prefix`; a layout
 * switch is left out, so a stock row's pane stays the container's. Returns
 * how many. */
static uint32_t copy_events(struct core_writer *w, const void *stream, const char *prefix)
{
    uint32_t prefix_bytes = 0, copied = 0;
    while (prefix[prefix_bytes])
        prefix_bytes++;
    const uint8_t *at = (const uint8_t *)stream + 4;
    for (uint32_t n = stream ? word(stream) : 0; n--;) {
        struct event e;
        at = read_event(at, &e);
        if (!starts(e.name, e.name_bytes, prefix, prefix_bytes))
            continue;
        if (e.action && same_text(e.action, e.action_bytes, "navigator.SwitchLayout")) {
            core_write_bytes(w, e.start, (uint32_t)(e.handler_end - e.start));
            core_write_word(w, 0);
        } else {
            core_write_bytes(w, e.start, (uint32_t)(e.end - e.start));
        }
        copied++;
    }
    return copied;
}

static void set_count(struct core_writer *w, uint32_t begin, uint32_t count)
{
    if (w->out)
        for (unsigned b = 0; b < 4; b++)
            w->out[begin + b] = (uint8_t)(count >> (8 * b));
}

static const void *stock(uint32_t type, uint32_t id)
{
    uint32_t bytes;
    return ResourceBank_GetResource(bank, type, id, &bytes);
}

/* The container's ITEM table, its CEVT and its layout's SEVT. A layout with a
 * stock row carries the controller.Goto* events of a stock Settings layout,
 * which some stock handlers post. */
static void write_lists(const struct core_container *c, struct core_writer *items,
                        struct core_writer *events, struct core_writer *layout)
{
    const void *settings_items = stock(Resource_ITEM, RES_SettingsMenu_Items);
    const void *settings_events = stock(Resource_CEVT, RES_SettingsMenu_Main_Screen);
    uint32_t item_count = 0, event_count = 0, stock_rows = 0;
    uint32_t item_begin = items->at, event_begin = events->at;
    core_write_word(items, 0);
    core_write_word(events, 0);
    EACH_ROW(r) {
        if (r->root != CORE_ROOT_CONTAINER || r->container != c->id || !row_shows(r, 0))
            continue;
        const struct rv_row *row = &r->entry->row;
        uint32_t bytes, begin;
        char name[40], handler[11], *n = core_put_text(name, name + sizeof name, "list.pid.");
        if (row->kind == RV_ROW_STOCK) {
            const uint8_t *data = settings_items
                                      ? core_block_with_word(settings_items, MenuRow_id, row->stock, &bytes)
                                      : NULL;
            if (!data || bytes != ROW_BYTES)
                continue;
            begin = core_write_block_begin(items, 0);
            core_write_bytes(items, data, ROW_BYTES);
            core_write_block_end(items, begin);
            n = core_put_number(n, name + sizeof name, row->stock);
            core_put_text(n, name + sizeof name, ".");
            event_count += copy_events(events, settings_events, name);
            stock_rows++;
        } else {
            const uint8_t *prototype =
                core_lists_prototype(row->kind == RV_ROW_TOGGLE || row->kind == RV_ROW_SETTING);
            if (!prototype)
                continue;
            uint32_t words[4] = {MenuRow_id, r->item, MenuRow_title, r->item + 1};
            begin = core_write_block_begin(items, 0);
            uint32_t at = items->at;
            core_write_copy(items, prototype, ROW_BYTES, words, 2);
            if (items->out) {
                items->out[at + MenuRow_title_from_model] = 1;
                items->out[at + MenuRow_visible] = 1;
            }
            core_write_block_end(items, begin);
            n = core_put_number(n, name + sizeof name, r->item);
            core_put_text(n, name + sizeof name, ".chosen");
            core_screens_handler_name(handler, r->item);
            core_write_event(events, name, handler, NULL, NULL, 0);
            event_count++;
        }
        item_count++;
    }
    set_count(items, item_begin, item_count);
    set_count(events, event_begin, event_count);
    core_clone_layout_events(layout);
    if (stock_rows) {
        uint32_t extra = copy_events(layout, stock(Resource_SEVT, RES_SettingsMenus_Legal_Layout),
                                     "controller.Goto");
        set_count(layout, 0, 2 + extra);
    }
}

int core_submenus_clone(struct core_screen *s, int split)
{
    struct core_container *c = core_submenus_of(s);
    if (!c)
        return 0;
    struct core_writer items = {NULL, 0}, events = {NULL, 0}, layout = {NULL, 0};
    write_lists(c, &items, &events, &layout);
    uint8_t *buffer = Heap_AllocateZeroed(items.at + events.at + layout.at);
    if (!buffer)
        return 0;
    struct core_writer i = {buffer, 0}, e = {buffer + items.at, 0},
                       l = {buffer + items.at + events.at, 0};
    write_lists(c, &i, &e, &l);
    int built = core_clone_list_with(s, split, i.out, i.at, e.out, e.at, l.out, l.at);
    Heap_Free(buffer);
    return built;
}

void core_submenus_init(void *resource_bank)
{
    bank = resource_bank;
    settings_hash = core_settings_owner("settings");
}
