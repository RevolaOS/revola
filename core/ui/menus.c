/* SPDX-License-Identifier: GPL-3.0-only */

/* Rows that extensions add to Apple's menus. Each native root has an ITEM table,
 * the screen whose CEVT binds its rows and a row the core appends after when an
 * entry names none. Rows are copies of the Legal row (Sound Check's for toggles)
 * appended while Apple builds the table, so they must be in place before the
 * menu first loads and are hidden, not freed, when their extension goes. Their
 * chosen bindings are added to the screen's event table when Apple loads it, and
 * the handler names resolve through core_screens_action. */

#include "ui/menus.h"
#include "ui/menurows.h"
#include "ui/panes.h"
#include "ui/clones.h"
#include "osos-2.0.5.h"
#include "ui/resources.h"
#include "ui/screens.h"
#include "settings.h"
#include "hooks/sites.h"
#include <stddef.h>

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

struct row_set *core_menu_sets;
static void *bank;



static const char *const root_names[RV_ROOT_COUNT] = {
    [RV_ROOT_MAIN] = "main",       [RV_ROOT_MUSIC] = "music",
    [RV_ROOT_EXTRAS] = "extras",   [RV_ROOT_SETTINGS] = "settings",
    [RV_ROOT_NOWPLAYING_MENU] = "nowplaying", [RV_ROOT_SONG_MENU] = "song",
    [RV_ROOT_ALBUM_MENU] = "album", [RV_ROOT_ARTIST_MENU] = "artist",
};

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

/* The root an entry names, -1 for a container id. */
static int root_of(const char *in)
{
    for (int i = 0; in && i < RV_ROOT_COUNT; i++)
        if (same(in, root_names[i]))
            return i;
    return -1;
}

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

/* The owner's stale row of this root and label, or NULL. */
static struct row *stale_row(uint32_t key, int root, uint32_t label)
{
    for (struct row_set *set = core_menu_sets; set; set = set->next)
        for (uint32_t i = 0; set->key == key && i < set->count; i++)
            if (set->rows[i].stale && set->rows[i].root == root && set->rows[i].label == label)
                return &set->rows[i];
    return NULL;
}

int core_menus_register(struct rv_extension_record *owner, const struct rv_entry *entries,
                        uint32_t count)
{
    uint32_t key = core_resource_owner(owner), fresh = 0;
    for (uint32_t n = 0; n < count; n++) {
        if (root_of(entries[n].in) < 0)
            return RV_BAD_ARGUMENT; /* TODO: containers */
        fresh += !stale_row(key, root_of(entries[n].in), core_settings_owner(entries[n].row.label));
    }
    struct row_set *set = NULL;
    if (fresh) {
        set = Heap_AllocateZeroed(sizeof *set + fresh * sizeof set->rows[0]);
        if (!set)
            return RV_NO_MEMORY;
        set->key = key;
        struct row_set **tail = &core_menu_sets;
        while (*tail)
            tail = &(*tail)->next;
        *tail = set; /* rows keep their registration order */
    }
    for (uint32_t n = 0; n < count; n++) {
        const struct rv_entry *e = &entries[n];
        int root = root_of(e->in);
        uint32_t label = core_settings_owner(e->row.label);
        struct row *r = stale_row(key, root, label);
        if (r) {
            r->owner = owner;
            r->entry = e;
            r->index = (uint8_t)n;
            r->stale = 0;
            apply_visibility(r);
            continue;
        }
        /* Item, title source, and for a Settings row its pane handler. */
        uint32_t item = core_resource_take(key, root == RV_ROOT_SETTINGS ? 3 : 2);
        if (!item)
            return RV_NO_MEMORY;
        r = &set->rows[set->count++];
        r->owner = owner;
        r->entry = e;
        r->item = item;
        r->label = label;
        r->root = (uint8_t)root;
        r->index = (uint8_t)n;
        r->stale = 0;
        if (!roots[root].table) {
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
        r->after = e->after ? e->after : roots[root].anchor;
        if (root == RV_ROOT_SETTINGS)
            core_panes_prepare();
        /* The item id is the title property, the source id the value's. */
        if (e->row.kind == RV_ROW_TOGGLE || e->row.kind == RV_ROW_SETTING)
            core_set_source2(bank, item + 1, item, item + 1, SourceDestination_InlineValue);
        else
            core_set_source(bank, item + 1, item, 1);
    }
    return RV_OK;
}

void core_menus_release(struct rv_extension_record *owner)
{
    EACH_ROW(r)
        if (r->entry && r->owner == owner) {
            r->entry = NULL;
            r->stale = 1;
            apply_visibility(r);
        }
}

const struct rv_row *core_menus_row(uint32_t id, struct rv_extension_record **owner,
                                    uint32_t *item)
{
    EACH_ROW(r)
        if (r->entry && id - r->item < 2) {
            *owner = r->owner;
            *item = r->item;
            return &r->entry->row;
        }
    return NULL;
}

/* The n-th live row of a root, or NULL. */
static struct row *nth(uint8_t root, uint32_t n)
{
    EACH_ROW(r)
        if (r->entry && r->root == root && n-- == 0)
            return r;
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
    EACH_ROW(other)
        if (other->entry && other->owner == r->owner && toggleable(other))
            mask |= (uint32_t)(shown(other) ^ (other == r)) << other->index;
    core_settings_set(r->owner->owner, RV_KEY_MENU_VISIBILITY, CORE_SETTING_INT, &mask, 4);
    apply_visibility(r);
}

void core_menus_setting_changed(struct rv_extension_record *owner, uint16_t key)
{
    EACH_ROW(r) {
        const struct rv_row *row = r->entry ? &r->entry->row : NULL;
        if (row && r->owner == owner && row->key == key &&
            (row->kind == RV_ROW_TOGGLE || row->kind == RV_ROW_SETTING))
            core_screens_notify(r->item + 1);
    }
}

const struct rv_entry *core_menus_entry(uint32_t item, struct rv_extension_record **owner)
{
    EACH_ROW(r)
        if (r->entry && r->item == item) {
            *owner = r->owner;
            return r->entry;
        }
    return NULL;
}

int core_menus_pane(uint32_t id, uint32_t argument)
{
    EACH_ROW(r)
        if (r->entry && r->root == RV_ROOT_SETTINGS && r->item + 2 == id) {
            if (argument != ActionSupportQuery)
                core_panes_show(r->item);
            return 1;
        }
    return 0;
}

int core_menus_owns(uint32_t id)
{
    EACH_ROW(r)
        if (id - r->item < 2)
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
    EACH_ROW(r) {
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

int core_menus_serves(uint8_t root, uint32_t screen_id)
{
    for (uint32_t i = 0; i < ROOT_SCREENS; i++)
        if (roots[root].screens[i] && roots[root].screens[i] == screen_id)
            return 1;
    return 0;
}

int core_menus_context_row(uint8_t root, uint32_t n, uint32_t *label, void **handler)
{
    struct row *r = nth(root, n);
    if (!r)
        return 0;
    *label = r->item + 1;
    *handler = r->data;
    return 1;
}

static const struct core_hook hooks[] = {
    {CORE_SITE_ResourceBlockList_AppendBorrowedBlock_call, CORE_HOOK_FILTER, -1, append_rows},
    {CORE_SITE_Main_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_Music_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_Main_SecondaryAction_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_Music_SecondaryAction_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_Extras_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
};

void core_menus_init(void *resource_bank)
{
    bank = resource_bank;
    for (uint32_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        core_hook_bind(&hooks[i]);
    core_context_init();
    core_bindings_init();
}
