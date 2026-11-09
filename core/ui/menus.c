/* SPDX-License-Identifier: GPL-3.0-only */

/* Rows that extensions add to Apple's menus. Each native root has an ITEM table,
 * the screen whose CEVT binds its rows and a row the core appends after when an
 * entry names none. Rows are copies of the Legal row (Sound Check's for toggles)
 * appended while Apple builds the table, so they must be in place before the
 * menu first loads and are hidden, not freed, when their extension goes. Their
 * chosen bindings are added to the screen's event table when Apple loads it, and
 * the handler names resolve through core_screens_action. */

#include "ui/menus.h"
#include "ui/panes.h"
#include "ui/clones.h"
#include "osos-2.0.5.h"
#include "ui/resources.h"
#include "ui/screens.h"
#include "settings.h"
#include "hooks/sites.h"
#include <stddef.h>

#define MAX_ROWS 16
#define ROW_BYTES 0x98

#define ROOT_SCREENS 4

struct root {
    uint32_t table;  /* ITEM resource; 0 for a context menu */
    uint32_t anchor; /* row appended after by default */
    /* Screens whose event tables get the rows' bindings: list.pid.<item>.chosen on
     * a menu, contextualMenu.<action> on the screens that open a context menu. */
    uint32_t screens[ROOT_SCREENS];
};

static const struct root roots[RV_ROOT_COUNT] = {
    [RV_ROOT_MAIN] = {RES_MainMenu_Items, RES_MainMenu_Sleep_Item, {RES_MainMenus_Main_Screen}},
    [RV_ROOT_MUSIC] = {RES_MusicMenu_Items, RES_MusicMenu_Search_Item, {RES_MainMenus_Music_Screen}},
    [RV_ROOT_EXTRAS] = {RES_ExtrasMenu_Items, RES_ExtrasMenu_Stopwatch_Item, {RES_ExtrasMenu_Screen}},
    [RV_ROOT_SETTINGS] = {RES_SettingsMenu_Items, RES_SettingsMenu_Legal_Item,
                          {RES_SettingsMenu_Main_Screen}},
    [RV_ROOT_NOWPLAYING_MENU] = {0, 0, {RES_NowPlaying_Screen}},
    [RV_ROOT_SONG_MENU] = {0, 0, {RES_MediaLists_Songs_Screen, RES_MediaLists_NestedPlaylists_Screen,
                                  RES_MediaLists_Genius_Screen, RES_MediaLists_GeniusPlaylist_Screen}},
    [RV_ROOT_ALBUM_MENU] = {0, 0, {RES_MediaLists_Albums_Screen}},
    [RV_ROOT_ARTIST_MENU] = {0, 0, {RES_MediaLists_Artists_Screen}},
};

/* Context menus are built on each open, so a row is appended before the Cancel
 * row at the menu's append site; the label is a string resource and the action
 * the row's handler name, both referenced by Apple while the menu is open. */
static uint8_t context_root(uint32_t site)
{
    switch (site) {
    case CORE_SITE_NowPlaying_CancelAppend_call:
        return RV_ROOT_NOWPLAYING_MENU;
    case CORE_SITE_Artists_CancelAppend_call:
        return RV_ROOT_ARTIST_MENU;
    case CORE_SITE_Albums_CancelAppend_call:
        return RV_ROOT_ALBUM_MENU;
    default:
        return RV_ROOT_SONG_MENU;
    }
}

struct row {
    struct rv_extension_record *owner;
    const struct rv_entry *entry; /* NULL while stale: the owner's image is gone */
    uint32_t item;                /* resource id; item + 1 is the title source or label string */
    uint32_t label;               /* FNV-1a of the label, to find the row again after a reload */
    uint32_t after;               /* the stock row it follows; 0 for a context menu */
    /* Heap, kept by Apple: the ROW_BYTES of a menu row, built when Apple builds
     * the table, or the "Rv" handler name a context row's action points at. */
    void *data;
    uint8_t root;
    uint8_t index; /* the entry's index in its descriptor, its visibility bit */
    uint8_t stale;
};

static struct row rows[MAX_ROWS];
static void *bank;

/* Main and Music rows can be hidden in Settings > Main Menu and Music Menu: bit
 * n of the owner's RV_KEY_MENU_VISIBILITY record is entry n, and without a
 * record an entry shows as its `visible` says. Other rows always show. */
static int toggleable(const struct row *r)
{
    return r->root == RV_ROOT_MAIN || r->root == RV_ROOT_MUSIC;
}

static int shown(const struct row *r)
{
    uint32_t mask;
    if (r->stale)
        return 0;
    if (!toggleable(r))
        return 1;
    if (core_settings_get(r->owner->owner, RV_KEY_MENU_VISIBILITY, &mask, 4) == 4)
        return mask >> r->index & 1;
    return r->entry->visible;
}

/* Apple's registry is built before the core runs and never sees the rows the
 * core appends, so this does what MenuItem_SetVisible does: the row's own data
 * carries the visibility, and an open list rebuilds on the item notification. */
static void apply_visibility(struct row *r)
{
    uint8_t *visible = r->data && roots[r->root].table ? (uint8_t *)r->data + MenuRow_visible : NULL;
    if (!visible || *visible == shown(r))
        return;
    *visible = (uint8_t)shown(r);
    void *service = UI_GetResourceService();
    void (*notify)(void *, uint32_t, uint32_t) =
        *(void **)(*(uint8_t **)service + Model_notify_method);
    notify(service, Property_ItemChanged, r->item);
    notify(service, Notify_None, r->item);
}

int core_menus_register(struct rv_extension_record *owner, const struct rv_entry *entries,
                        uint32_t count)
{
    uint32_t key = core_resource_owner(owner);
    for (uint32_t n = 0; n < count; n++) {
        const struct rv_entry *e = &entries[n];
        if (e->root >= RV_ROOT_COUNT || e->container)
            return RV_BAD_ARGUMENT; /* TODO: containers */
        uint32_t label = core_settings_owner(e->row.label);
        struct row *r = NULL;
        for (uint32_t i = 0; i < MAX_ROWS && !r; i++)
            if (rows[i].stale && rows[i].root == e->root && rows[i].label == label &&
                core_resource_owner(rows[i].owner) == key)
                r = &rows[i];
        if (r) {
            r->owner = owner;
            r->entry = e;
            r->index = (uint8_t)n;
            r->stale = 0;
            apply_visibility(r);
            continue;
        }
        for (uint32_t i = 0; i < MAX_ROWS && !r; i++)
            if (!rows[i].item)
                r = &rows[i];
        if (!r)
            return RV_NO_MEMORY;
        /* Item, title source, and for a Settings row its pane handler. */
        uint32_t item = core_resource_take(key, e->root == RV_ROOT_SETTINGS ? 3 : 2);
        if (!item)
            return core_resource_block_held(key) ? RV_CONFLICT : RV_NO_MEMORY;
        r->owner = owner;
        r->entry = e;
        r->item = item;
        r->label = label;
        r->root = e->root;
        r->index = (uint8_t)n;
        r->stale = 0;
        r->data = NULL;
        if (!roots[e->root].table) {
            r->after = 0;
            r->data = Heap_AllocateZeroed(11);
            if (!r->data)
                return RV_NO_MEMORY;
            core_screens_handler_name(r->data, item);
            uint32_t n = 0;
            while (e->row.label[n])
                n++;
            ResourceBank_SetResource(bank, Resource_String, item + 1, e->row.label, n + 1);
            continue;
        }
        r->after = e->after ? e->after : roots[e->root].anchor;
        if (e->root == RV_ROOT_SETTINGS)
            core_panes_prepare();
        /* The item id is the title property, the source id the value's. */
        if (e->row.kind == RV_ROW_TOGGLE)
            core_set_source2(bank, item + 1, item, item + 1, SourceDestination_InlineValue);
        else
            core_set_source(bank, item + 1, item, 1);
    }
    return RV_OK;
}

void core_menus_release(struct rv_extension_record *owner)
{
    for (uint32_t i = 0; i < MAX_ROWS; i++)
        if (rows[i].entry && rows[i].owner == owner) {
            rows[i].entry = NULL;
            rows[i].stale = 1;
            apply_visibility(&rows[i]);
        }
}

const struct rv_row *core_menus_row(uint32_t id, struct rv_extension_record **owner,
                                    uint32_t *item)
{
    for (uint32_t i = 0; i < MAX_ROWS; i++)
        if (rows[i].entry && id - rows[i].item < 2) {
            *owner = rows[i].owner;
            *item = rows[i].item;
            return &rows[i].entry->row;
        }
    return NULL;
}

/* The n-th live row of a root, or NULL. */
static struct row *nth(uint8_t root, uint32_t n)
{
    for (uint32_t i = 0; i < MAX_ROWS; i++)
        if (rows[i].entry && rows[i].root == root && n-- == 0)
            return &rows[i];
    return NULL;
}

uint32_t core_menus_count(uint8_t root)
{
    uint32_t count = 0;
    while (nth(root, count))
        count++;
    return count;
}

const char *core_menus_label(uint8_t root, uint32_t n, int *visible, uint32_t *after)
{
    struct row *r = nth(root, n);
    if (!r)
        return NULL;
    *visible = shown(r);
    *after = r->after;
    return r->entry->row.label;
}

void core_menus_toggle(uint8_t root, uint32_t n)
{
    struct row *r = nth(root, n);
    if (!r)
        return;
    uint32_t mask = 0;
    for (uint32_t i = 0; i < MAX_ROWS; i++)
        if (rows[i].entry && rows[i].owner == r->owner && toggleable(&rows[i]))
            mask |= (uint32_t)(shown(&rows[i]) ^ (&rows[i] == r)) << rows[i].index;
    core_settings_set(r->owner->owner, RV_KEY_MENU_VISIBILITY, CORE_SETTING_INT, &mask, 4);
    apply_visibility(r);
}

const struct rv_entry *core_menus_entry(uint32_t item)
{
    for (uint32_t i = 0; i < MAX_ROWS; i++)
        if (rows[i].entry && rows[i].item == item)
            return rows[i].entry;
    return NULL;
}

int core_menus_pane(uint32_t id, uint32_t argument)
{
    for (uint32_t i = 0; i < MAX_ROWS; i++)
        if (rows[i].entry && rows[i].root == RV_ROOT_SETTINGS && rows[i].item + 2 == id) {
            if (argument != ActionSupportQuery)
                core_panes_show(rows[i].item);
            return 1;
        }
    return 0;
}

int core_menus_owns(uint32_t id)
{
    for (uint32_t i = 0; i < MAX_ROWS; i++)
        if (rows[i].item && id - rows[i].item < 2)
            return 1;
    return 0;
}

/* Appends the rows anchored on the stock row Apple just appended. The first
 * build copies that row's bytes, so the copy needs the Legal or Sound Check row
 * as the anchor's prototype only when it is not one of them. */
static uint32_t append_rows(uint32_t list, uint32_t data, uint32_t bytes, uint32_t metadata,
                            const struct core_next *next)
{
    uint32_t result = core_next_call(next, list, data, bytes, metadata);
    if (bytes != ROW_BYTES)
        return result;
    uint32_t id = core_word_at((void *)data, MenuRow_id);
    for (uint32_t i = 0; i < MAX_ROWS; i++) {
        struct row *r = &rows[i];
        if (!r->after || r->after != id) /* stock rows may carry id 0 */
            continue;
        uint32_t *row = r->data;
        if (!row) {
            row = Heap_AllocateZeroed(ROW_BYTES);
            if (!row)
                continue;
            for (uint32_t w = 0; w < ROW_BYTES / 4; w++)
                row[w] = ((const uint32_t *)data)[w];
            row[MenuRow_id / 4] = r->item;
            row[MenuRow_title / 4] = r->item + 1;
            ((uint8_t *)row)[MenuRow_title_from_model] = 1;
            r->data = row;
            ((uint8_t *)row)[MenuRow_visible] = shown(r);
        }
        core_next_call(next, list, (uint32_t)row, bytes, metadata);
    }
    return result;
}

static int root_has_screen(uint8_t root, uint32_t screen_id)
{
    for (uint32_t i = 0; i < ROOT_SCREENS; i++)
        if (roots[root].screens[i] && roots[root].screens[i] == screen_id)
            return 1;
    return 0;
}

/* One binding per row of the roots a screen serves. */
static void write_bindings(struct core_writer *w, uint32_t screen_id)
{
    uint32_t count = 0, begin = w->at;
    core_write_word(w, 0);
    for (uint32_t i = 0; i < MAX_ROWS; i++) {
        struct row *r = &rows[i];
        if (!r->item || !root_has_screen(r->root, screen_id))
            continue;
        char name[40], handler[11], *n;
        core_screens_handler_name(handler, r->item);
        if (r->after) {
            n = core_put_text(name, name + sizeof name, "list.pid.");
            n = core_put_number(n, name + sizeof name, r->item);
            core_put_text(n, name + sizeof name, ".chosen");
        } else {
            n = core_put_text(name, name + sizeof name, "contextualMenu.");
            core_put_text(n, name + sizeof name, handler);
        }
        core_write_event(w, name, handler, NULL, NULL, 0);
        count++;
        if (r->root != RV_ROOT_SETTINGS)
            continue;
        /* The preview pane: the row's pane handler, then the shared layout. */
        static const char *const layout[] = {core_panes_layout_name};
        n = core_put_text(name, name + sizeof name, "list.pid.");
        n = core_put_number(n, name + sizeof name, r->item);
        core_put_text(n, name + sizeof name, ".delayedselected");
        core_screens_handler_name(handler, r->item + 2);
        core_write_event(w, name, handler, "navigator.SwitchLayout", layout, 1);
        count++;
    }
    if (w->out)
        for (unsigned b = 0; b < 4; b++)
            w->out[begin + b] = (uint8_t)(count >> (8 * b));
}

/* Adds the rows' bindings to a screen's event map after Apple loaded its own. */
static uint32_t layout_events(uint32_t layout, const struct core_next *next)
{
    uint32_t result = core_next_call(next, layout, 0, 0, 0);
    uint8_t *screen = *(uint8_t **)((uint8_t *)layout + Layout_screen);
    uint32_t screen_id = screen ? core_word_at(screen, LayoutScreen_id) : 0;
    struct core_writer measure = {NULL, 0};
    write_bindings(&measure, screen_id);
    if (measure.at <= 4)
        return result;
    uint8_t *stream = Heap_AllocateZeroed(measure.at);
    if (!stream)
        return result;
    struct core_writer w = {stream, 0};
    write_bindings(&w, screen_id);
    core_clone_read_events((void *)layout, stream, w.at);
    Heap_Free(stream);
    return result;
}

/* Appends the context rows of the menu being built, then the Cancel row. */
static uint32_t context_rows(uint32_t items, uint32_t label, uint32_t action, uint32_t flags,
                             const struct core_next *next)
{
    uint8_t root = context_root(next->site);
    for (uint32_t i = 0; i < MAX_ROWS; i++) {
        struct row *r = &rows[i];
        /* Flags 1: an ordinary row whose action is dispatched to the controller;
         * the Cancel row's own flags only close the menu. */
        if (r->entry && r->root == root)
            ContextMenuItems_Append((void *)items, r->item + 1, r->data, 1);
    }
    return core_next_call(next, items, label, action, flags);
}

static const struct core_hook hooks[] = {
    {CORE_SITE_ResourceBlockList_AppendBorrowedBlock_call, CORE_HOOK_FILTER, -1, append_rows},
    {CORE_SITE_UI_LoadLayoutEvents_call, CORE_HOOK_FILTER, 5, layout_events},
    {CORE_SITE_Main_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_Music_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_Main_SecondaryAction_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_Music_SecondaryAction_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_NowPlaying_SecondaryAction_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_Extras_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_NowPlaying_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_Artists_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_Albums_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_SongList_DispatchAction_entry, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_NestedPlaylists_DispatchAction_entry, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_Genius_DispatchAction_entry, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_SongList_CancelAppend_call, CORE_HOOK_FILTER, 5, context_rows},
    {CORE_SITE_NestedPlaylists_CancelAppend_call, CORE_HOOK_FILTER, 5, context_rows},
    {CORE_SITE_Genius_CancelAppend_call, CORE_HOOK_FILTER, 5, context_rows},
    {CORE_SITE_NowPlaying_CancelAppend_call, CORE_HOOK_FILTER, 5, context_rows},
    {CORE_SITE_Artists_CancelAppend_call, CORE_HOOK_FILTER, 5, context_rows},
    {CORE_SITE_Albums_CancelAppend_call, CORE_HOOK_FILTER, 5, context_rows},
};

void core_menus_init(void *resource_bank)
{
    bank = resource_bank;
    for (uint32_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        core_hook_bind(&hooks[i]);
}
