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
#include "ui/lists.h"
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

static const struct root roots[CORE_MENU_COUNT] = {
    [RV_MENU_MAIN] = {RES_MainMenu_Items, RES_MainMenu_Sleep_Item, {RES_MainMenus_Main_Screen}},
    [RV_MENU_MUSIC] = {RES_MusicMenu_Items, RES_MusicMenu_Search_Item, {RES_MainMenus_Music_Screen}},
    [RV_MENU_EXTRAS] = {RES_ExtrasMenu_Items, RES_ExtrasMenu_Stopwatch_Item, {RES_ExtrasMenu_Screen}},
    [RV_MENU_SETTINGS] = {RES_SettingsMenu_Items, RES_SettingsMenu_Legal_Item,
                          {RES_SettingsMenu_Main_Screen}},
    [RV_MENU_NOW_PLAYING_CONTEXT] = {0, 0, {RES_NowPlaying_Screen}},
    [RV_MENU_SONG_CONTEXT] = {0, 0, {RES_MediaLists_Songs_Screen, RES_MediaLists_NestedPlaylists_Screen,
                                  RES_MediaLists_Genius_Screen, RES_MediaLists_GeniusPlaylist_Screen}},
    [RV_MENU_ALBUM_CONTEXT] = {0, 0, {RES_MediaLists_Albums_Screen}},
    [RV_MENU_ARTIST_CONTEXT] = {0, 0, {RES_MediaLists_Artists_Screen}},
    [RV_MENU_PODCAST_EPISODE_CONTEXT] = {0, 0, {RES_MediaLists_PodcastEpisodes_Screen}},
    [RV_MENU_ITUNES_U_EPISODE_CONTEXT] = {0, 0, {RES_MediaLists_iTunesUEpisodes_Screen}},
    [RV_MENU_AUDIOBOOK_CONTEXT] = {0, 0, {RES_MediaLists_Audiobooks_Screen}},
    [RV_MENU_MOVIE_CONTEXT] = {0, 0, {RES_MediaLists_Movies_Screen}},
    [RV_MENU_TV_EPISODE_CONTEXT] = {0, 0, {RES_MediaLists_TVEpisodes_Screen}},
    [RV_MENU_VIDEO_PLAYLIST_CONTEXT] = {0, 0, {RES_MediaLists_MixedVideoTracks_Screen}},
};

struct row_set *core_menu_sets;
static void *bank;

/* Row data of unloaded owners, by item id. Apple's menu registry and the
 * context menu model keep the pointers, so a block is handed back to the row
 * that takes the item id again instead of being freed. */
struct parked {
    struct parked *next;
    uint32_t left;
    uint32_t bytes;
    struct {
        uint32_t item;
        void *data;
    } rows[];
};

static struct parked *parked;

/* The parked data of a row with this item id, counted to `owner` from now. */
static void *adopt(uint32_t item, struct rv_extension_record *owner, uint32_t bytes)
{
    for (struct parked **link = &parked; *link; link = &(*link)->next) {
        struct parked *p = *link;
        for (uint32_t i = 0; i < p->left; i++) {
            if (p->rows[i].item != item)
                continue;
            void *data = p->rows[i].data;
            p->rows[i] = p->rows[--p->left];
            if (!p->left) {
                *link = p->next;
                core_heap_free(NULL, p, p->bytes);
            }
            core_heap_move(NULL, owner, bytes);
            return data;
        }
    }
    return NULL;
}


/* An entry with `shown_by` shows while that bool setting of its owner is on;
 * under Main and Music, Settings > Main Menu and Music Menu list it and flip
 * the setting. Other rows always show. */
static int toggleable(const struct row *r)
{
    return r->owner && r->entry && r->entry->shown_by &&
           (r->root == RV_MENU_MAIN || r->root == RV_MENU_MUSIC);
}

static int shown(const struct row *r)
{
    if (r->stale)
        return 0;
    if (!r->owner || !r->entry || !r->entry->shown_by)
        return 1;
    return core_screens_setting_value(r->owner, r->entry->shown_by) != 0;
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

int core_menus_register(struct rv_extension_record *owner, const struct rv_menu_entry *entries,
                        uint32_t count)
{
    uint32_t key = core_resource_owner(owner), fresh = 0;
    for (uint32_t n = 0; n < count; n++) {
        if (entries[n].menu >= CORE_MENU_COUNT)
            return RV_BAD_ARGUMENT;
        fresh += !stale_row(key, entries[n].menu,
                            core_settings_owner(entries[n].row.label));
    }
    struct row_set *set = NULL;
    if (fresh) {
        uint32_t bytes = sizeof *set + fresh * sizeof set->rows[0];
        set = core_heap_allocate(owner, bytes);
        if (!set)
            return RV_NO_MEMORY;
        set->key = key;
        set->bytes = bytes;
        struct row_set **tail = &core_menu_sets;
        while (*tail)
            tail = &(*tail)->next;
        *tail = set; /* rows keep their registration order */
    }
    for (uint32_t n = 0; n < count; n++) {
        const struct rv_menu_entry *e = &entries[n];
        int root = e->menu;
        uint32_t label = core_settings_owner(e->row.label);
        struct row *r = stale_row(key, root, label);
        if (r) {
            r->owner = owner;
            r->entry = e;
            r->stale = 0;
            apply_visibility(r);
            continue;
        }
        /* Item, title source, and for a Settings row its pane handler. */
        uint32_t item = core_resource_take(key, root == RV_MENU_SETTINGS ? 3 : 2);
        if (!item)
            return RV_NO_MEMORY;
        r = &set->rows[set->count++];
        r->owner = owner;
        r->entry = e;
        r->item = item;
        r->label = label;
        r->root = (uint8_t)root;
        r->stale = 0;
        r->after = roots[root].table ? e->after ? e->after : roots[root].anchor : 0;
        r->data = adopt(item, owner, CORE_ROW_DATA_BYTES(r));
        if (!roots[root].table) {
            if (!r->data)
                r->data = core_heap_allocate(owner, CORE_HANDLER_BYTES);
            if (!r->data)
                return RV_NO_MEMORY;
            core_screens_handler_name(r->data, item);
            uint32_t n = 0;
            while (e->row.label[n])
                n++;
            core_resource_set(Resource_String, item + 1, e->row.label, n + 1);
            continue;
        }
        if (root == RV_MENU_SETTINGS)
            core_panes_prepare();
        /* The item id is the title property, the source id the value's. */
        if (e->row.kind == RV_ROW_KIND_SETTING || e->row.kind == RV_ROW_KIND_INFO)
            core_set_source2(item + 1, item, item + 1, SourceDestination_InlineValue);
        else
            core_set_source(item + 1, item, 1);
        apply_visibility(r);
    }
    return RV_OK;
}

static void hide(struct row *r)
{
    r->entry = NULL;
    r->stale = 1;
    apply_visibility(r);
}

int core_menus_release(struct rv_extension_record *owner)
{
    uint32_t key = core_resource_owner(owner), held = 0;
    for (struct row_set *set = core_menu_sets; set; set = set->next)
        for (uint32_t i = 0; set->key == key && i < set->count; i++)
            held += set->rows[i].data != NULL;
    uint32_t bytes = sizeof(struct parked) + held * sizeof parked->rows[0];
    struct parked *p = held ? core_heap_allocate(NULL, bytes) : NULL;
    if (held && !p) {
        /* The rows stay as stale records, counted to the core. */
        for (struct row_set *set = core_menu_sets; set; set = set->next) {
            if (set->key != key)
                continue;
            core_heap_move(owner, NULL, set->bytes);
            for (uint32_t i = 0; i < set->count; i++) {
                struct row *r = &set->rows[i];
                if (r->data)
                    core_heap_move(owner, NULL, CORE_ROW_DATA_BYTES(r));
                if (r->entry)
                    hide(r);
            }
        }
        return 0;
    }
    if (p)
        p->bytes = bytes;
    for (struct row_set **link = &core_menu_sets; *link;) {
        struct row_set *set = *link;
        if (set->key != key) {
            link = &set->next;
            continue;
        }
        for (uint32_t i = 0; i < set->count; i++) {
            struct row *r = &set->rows[i];
            if (r->entry)
                hide(r);
            if (r->data) {
                p->rows[p->left].item = r->item;
                p->rows[p->left++].data = r->data;
                core_heap_move(owner, NULL, CORE_ROW_DATA_BYTES(r));
            }
        }
        *link = set->next;
        core_heap_free(owner, set, set->bytes);
    }
    if (p) {
        p->next = parked;
        parked = p;
    }
    return 1;
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
        if (r->entry && r->owner && r->root == root && n-- == 0)
            return r;
    return NULL;
}

/* The n-th row of Main or Music that Settings > Main Menu or Music Menu lists. */
static struct row *nth_toggleable(uint8_t root, uint32_t n)
{
    EACH_ROW(r)
        if (r->root == root && toggleable(r) && n-- == 0)
            return r;
    return NULL;
}

uint32_t core_menus_count(uint8_t root)
{
    uint32_t count = 0;
    while (nth_toggleable(root, count))
        count++;
    return count;
}

const char *core_menus_label(uint8_t root, uint32_t n, int *visible, uint32_t *after)
{
    struct row *r = nth_toggleable(root, n);
    if (!r)
        return NULL;
    *visible = shown(r);
    *after = r->after;
    return r->entry->row.label;
}

void core_menus_toggle(uint8_t root, uint32_t n)
{
    struct row *r = nth_toggleable(root, n);
    if (!r)
        return;
    core_settings_set_int(r->owner->owner, r->entry->shown_by, !shown(r));
    core_screens_setting_changed(r->owner, r->entry->shown_by);
}

void core_menus_setting_changed(struct rv_extension_record *owner, uint16_t key)
{
    EACH_ROW(r) {
        const struct rv_row *row = r->entry ? &r->entry->row : NULL;
        if (row && r->owner == owner && row->key == key &&
            row->kind == RV_ROW_KIND_SETTING)
            core_screens_notify(r->item + 1);
        if (r->entry && r->owner == owner && r->entry->shown_by == key)
            apply_visibility(r);
    }
}

const struct rv_menu_entry *core_menus_entry(uint32_t item, struct rv_extension_record **owner)
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
        if (r->entry && r->root == RV_MENU_SETTINGS && r->item + 2 == id) {
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
    for (struct parked *p = parked; p; p = p->next)
        for (uint32_t i = 0; i < p->left; i++)
            if (id - p->rows[i].item < 2)
                return 1;
    return 0;
}

/* Appends the rows anchored on the stock row Apple just appended. A row is
 * made on the first build: under Settings a copy of the Legal row, or of Sound
 * Check's for a row with a value, whatever row it follows; elsewhere a copy of
 * the row it follows. */
static uint32_t append_rows(uint32_t list, uint32_t data, uint32_t bytes, uint32_t metadata,
                            const struct core_next *next)
{
    uint32_t id = bytes == ROW_BYTES ? core_word_at((void *)data, MenuRow_id) : 0;
    uint32_t result = core_next_call(next, list, data, bytes, metadata);
    if (bytes != ROW_BYTES)
        return result;
    EACH_ROW(r) {
        if (!r->after || r->after != id) /* stock rows may carry id 0 */
            continue;
        uint32_t *row = r->data;
        if (!row) {
            row = core_heap_allocate(r->owner, ROW_BYTES);
            if (!row)
                continue;
            const uint32_t *from = (const uint32_t *)data;
            if (r->root == RV_MENU_SETTINGS) {
                uint8_t kind = r->entry ? r->entry->row.kind : RV_ROW_KIND_PUSH;
                const uint32_t *prototype = (const uint32_t *)core_lists_prototype(
                    kind == RV_ROW_KIND_SETTING || kind == RV_ROW_KIND_INFO);
                if (prototype)
                    from = prototype;
            }
            for (uint32_t w = 0; w < ROW_BYTES / 4; w++)
                row[w] = from[w];
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
    for (uint32_t i = 0; root < CORE_MENU_COUNT && i < ROOT_SCREENS; i++)
        if (roots[root].screens[i] && roots[root].screens[i] == screen_id)
            return 1;
    return 0;
}

int core_menus_context_row(uint8_t root, uint32_t n, uint32_t *label, void **handler)
{
    struct row *r = nth(root, n);
    if (!r)
        return 0;
    int show = shown(r);
    if (show && r->entry->available) {
        uint32_t item[MediaItem_bytes / 4];
        struct rv_context ctx = {r->owner, core_context_list(), NULL, NULL, -1, NULL};
        ctx.item = core_context_item(root, ctx.controller, item, &ctx.row, &ctx.list);
        show = r->entry->available(&ctx);
    }
    *label = show ? r->item + 1 : 0;
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
