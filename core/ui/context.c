/* SPDX-License-Identifier: GPL-3.0-only */

/* Rows that extensions add to the menus a long press of Select opens. These
 * menus are built on each open, so a row is appended before the Cancel row at
 * the menu's append site; the label is a string resource and the action the
 * row's handler name, both referenced by Apple while the menu is open. The
 * screens' action slots run the handler. */

#include "ui/menus.h"
#include "osos-2.0.5.h"
#include "ui/screens.h"
#include "hooks/sites.h"
#include <stddef.h>

#define FIELD(base, offset, type) (*(type *)((uint8_t *)(base) + (offset)))

/* The list controller whose context menu opened last and the model row it
 * opened on (-1 for a leading row such as All Albums). */
static void *menu_list;
static int32_t menu_row = -1;

static uint32_t menu_opening(uint32_t controller, const struct core_next *next)
{
    void *selection = UI_GetSelectionModel();
    int32_t (*current)(void *) =
        FIELD(FIELD(selection, 0, void **), SelectionModel_current_index_method, void *);
    int32_t visible = current(selection);
    menu_list = NULL;
    if (visible >= 0) {
        int32_t (*map)(void *, int32_t) = FIELD(FIELD(controller, 0, void **),
                                                MediaListController_map_visible_row_method, void *);
        menu_row = map((void *)controller, visible);
        menu_list = (void *)controller;
    }
    return core_next_call(next, controller, 0, 0, 0);
}

void *core_context_list(void)
{
    return menu_list;
}

const void *core_context_item(uint8_t menu, void *controller, void *item, int32_t *row)
{
    *row = -1;
    if (menu == RV_MENU_NOW_PLAYING_CONTEXT) {
        MediaItem_InitFromRecord(item, NULL, 0);
        MediaPlayer_GetCurrentItem(MediaPlayer_GetInstance(), item);
    } else if (menu >= RV_MENU_SONG_CONTEXT && controller && controller == menu_list) {
        *row = menu_row;
        if (menu != RV_MENU_SONG_CONTEXT || menu_row < 0)
            return NULL;
        MediaItem_InitFromRecord(item, NULL, 0);
        void *build = FIELD(FIELD(controller, 0, void **),
                            MediaListController_show_context_menu_method, void *);
        if (build == (void *)ADDR_Genius_BuildAndShowContextualMenu) {
            void *genius = Genius_GetInstance();
            Genius_GetItem(genius, (uint32_t)Genius_MapVisibleRow(genius, menu_row), item);
        } else {
            void *model = LibraryModelRef_Get((uint8_t *)controller + MediaListController_model);
            void (*get)(void *, void *, int32_t) =
                FIELD(FIELD(model, 0, void **), SongModel_get_item_method, void *);
            if (model)
                get(item, model, menu_row);
        }
    } else {
        return NULL;
    }
    return FIELD(item, MediaItem_record, void *) && MediaItem_IsValid(item) ? item : NULL;
}

static uint8_t context_root(uint32_t site)
{
    switch (site) {
    case CORE_SITE_NowPlaying_CancelAppend_call:
        return RV_MENU_NOW_PLAYING_CONTEXT;
    case CORE_SITE_Artists_CancelAppend_call:
        return RV_MENU_ARTIST_CONTEXT;
    case CORE_SITE_Albums_CancelAppend_call:
        return RV_MENU_ALBUM_CONTEXT;
    default:
        return RV_MENU_SONG_CONTEXT;
    }
}

/* Appends the context rows of the menu being built, then the Cancel row. */
static uint32_t context_rows(uint32_t items, uint32_t label, uint32_t action, uint32_t flags,
                             const struct core_next *next)
{
    uint8_t root = context_root(next->site);
    uint32_t string;
    void *handler;
    /* Flags 1: an ordinary row whose action is dispatched to the controller;
     * the Cancel row's own flags only close the menu. */
    for (uint32_t n = 0; core_menus_context_row(root, n, &string, &handler); n++)
        if (string)
            ContextMenuItems_Append((void *)items, string, handler, 1);
    return core_next_call(next, items, label, action, flags);
}

static const struct core_hook hooks[] = {
    {CORE_SITE_MediaListController_HandleShowContextualMenu_entry, CORE_HOOK_FILTER, 0,
     menu_opening},
    {CORE_SITE_NowPlaying_SecondaryAction_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
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

void core_context_init(void)
{
    for (uint32_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        core_hook_bind(&hooks[i]);
}
