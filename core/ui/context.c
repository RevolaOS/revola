/* SPDX-License-Identifier: GPL-3.0-only */

/* Rows that extensions add to the menus a long press of Select opens. These
 * menus are built on each open, so a row is appended before the Cancel row at
 * the menu's append site; the label is a string resource and the action the
 * row's handler name, both referenced by Apple while the menu is open. Movie
 * and TV episode lists build no menu of their own, so the core builds one of
 * the rows at their show-context-menu slot. The screens' action slots run the
 * handler. A row that opens a screen hides the menu's pop, as Apple's View
 * Album does, so no frame of the screen underneath shows between the two. */

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
        if (menu == RV_MENU_ALBUM_CONTEXT || menu == RV_MENU_ARTIST_CONTEXT || menu_row < 0)
            return NULL;
        MediaItem_InitFromRecord(item, NULL, 0);
        void *build = FIELD(FIELD(controller, 0, void **),
                            MediaListController_show_context_menu_method, void *);
        if (build == (void *)ADDR_Genius_BuildAndShowContextualMenu) {
            void *genius = Genius_GetInstance();
            Genius_GetItem(genius, (uint32_t)Genius_MapVisibleRow(genius, menu_row), item);
        } else {
            void *model = LibraryModelRef_Get((uint8_t *)controller + MediaListController_model);
            if (model) {
                void (*get)(void *, void *, int32_t) =
                    FIELD(FIELD(model, 0, void **), SongModel_get_item_method, void *);
                get(item, model, menu_row);
            }
        }
    } else {
        return NULL;
    }
    return FIELD(item, MediaItem_record, void *) && MediaItem_IsValid(item) ? item : NULL;
}

/* The menu a Cancel append site builds, and the layout id and cache fields of
 * the controller it opens on. */
static const struct {
    uint32_t site;
    uint8_t menu;
    uint16_t layout, cache;
} sources[] = {
    {CORE_SITE_PodcastEpisodes_CancelAppend_call, RV_MENU_PODCAST_EPISODE_CONTEXT,
     PodcastEpisodesController_layout, PodcastEpisodesController_layout_cache},
    {CORE_SITE_SongList_CancelAppend_call, RV_MENU_SONG_CONTEXT, SongListController_layout,
     SongListController_layout_cache},
    {CORE_SITE_NestedPlaylists_CancelAppend_call, RV_MENU_SONG_CONTEXT,
     NestedPlaylistsController_layout, NestedPlaylistsController_layout_cache},
    {CORE_SITE_Genius_CancelAppend_call, RV_MENU_SONG_CONTEXT, GeniusController_layout,
     GeniusController_layout_cache},
    {CORE_SITE_NowPlaying_CancelAppend_call, RV_MENU_NOW_PLAYING_CONTEXT,
     NowPlayingController_layout, NowPlayingController_layout_cache},
    {CORE_SITE_Artists_CancelAppend_call, RV_MENU_ARTIST_CONTEXT, ArtistsController_layout,
     ArtistsController_layout_cache},
    {CORE_SITE_Albums_CancelAppend_call, RV_MENU_ALBUM_CONTEXT, AlbumsController_layout,
     AlbumsController_layout_cache},
    {CORE_SITE_ITunesUEpisodes_CancelAppend_call, RV_MENU_ITUNES_U_EPISODE_CONTEXT,
     ITunesUEpisodesController_layout, ITunesUEpisodesController_layout_cache},
    {CORE_SITE_Audiobooks_CancelAppend_call, RV_MENU_AUDIOBOOK_CONTEXT, AudiobooksController_layout,
     AudiobooksController_layout_cache},
    /* Movies and TV episodes have no stock menu; the core builds one at the
     * show-context-menu slot, so that slot stands for the source. */
    {CORE_SITE_Movies_ShowContextMenu_slot, RV_MENU_MOVIE_CONTEXT, MoviesController_layout,
     MoviesController_layout_cache},
    {CORE_SITE_TVEpisodes_ShowContextMenu_slot, RV_MENU_TV_EPISODE_CONTEXT,
     TVEpisodesController_layout, TVEpisodesController_layout_cache},
    /* Video playlists share the nested playlists' builder and Cancel row, so
     * this source is chosen by the controller's vtable, not by a site. */
    {0, RV_MENU_VIDEO_PLAYLIST_CONTEXT, NestedVideoPlaylistsController_layout,
     NestedVideoPlaylistsController_layout_cache},
};
#define VIDEO_PLAYLISTS_SOURCE (sizeof sources / sizeof sources[0] - 1)

/* The source of the menu built last, and 1 between hiding its pop and the
 * action that should push a screen. */
static uint32_t menu_source;
static uint8_t pop_hidden;

static uint32_t source_of(uint32_t site)
{
    if (site == CORE_SITE_NestedPlaylists_CancelAppend_call && menu_list &&
        FIELD(menu_list, 0, void *) == NestedVideoPlaylistsController_vtable)
        return VIDEO_PLAYLISTS_SOURCE;
    for (uint32_t i = 0; i < sizeof sources / sizeof sources[0]; i++)
        if (sources[i].site == site)
            return i;
    return 0;
}

/* Appends the context rows of `root` to `items`; how many. Flags 1: an
 * ordinary row whose action is dispatched to the controller; the Cancel row's
 * own flags only close the menu. */
static uint32_t append_rows(void *items, uint8_t root)
{
    uint32_t string, count = 0;
    void *handler;
    for (uint32_t n = 0; core_menus_context_row(root, n, &string, &handler); n++)
        if (string) {
            ContextMenuItems_Append(items, string, handler, 1);
            count++;
        }
    return count;
}

/* Appends the context rows of the menu being built, then the Cancel row. */
static uint32_t context_rows(uint32_t items, uint32_t label, uint32_t action, uint32_t flags,
                             const struct core_next *next)
{
    menu_source = source_of(next->site);
    append_rows((void *)items, sources[menu_source].menu);
    return core_next_call(next, items, label, action, flags);
}

/* Opens a menu of the extensions' rows on a list without a stock one:
 * (controller, model row). With no row to show, the stock slot runs instead. */
static uint32_t own_menu(uint32_t controller, uint32_t row, const struct core_next *next)
{
    uint32_t items[ContextMenuItems_bytes / 4];
    menu_source = source_of(next->site);
    ContextMenuItems_Init(items, 0);
    if (!append_rows(items, sources[menu_source].menu)) {
        ContextMenuItems_Destroy(items);
        return core_next_call(next, controller, row, 0, 0);
    }
    ContextMenuItems_Append(items, RES_ContextMenu_Cancel_String, "HandleCancel", 0);
    UI_ShowContextMenuItems((void *)controller, items);
    ContextMenuItems_Destroy(items);
    return 1;
}

/* Hides the menu's pop for a row that opens a screen; Apple's own actions
 * compare as before. */
static uint32_t screen_action(uint32_t action, uint32_t expected, const struct core_next *next)
{
    if (core_screens_context_push((const char *)action)) {
        pop_hidden = 1;
        return 0;
    }
    return core_next_call(next, action, expected, 0, 0);
}

void core_context_pushed(void *controller, int pushed)
{
    if (!pop_hidden)
        return;
    pop_hidden = 0;
    if (pushed)
        return;
    /* Nothing follows the hidden pop: show the layout under the menu again,
     * prepared as it was, without switching to it. A controller without
     * layouts keeps the stock slot and has nothing to show. */
    void *switch_layout =
        FIELD(FIELD(controller, 0, void **), ScreenController_switch_layout_method, void *);
    if (switch_layout == (void *)ADDR_ScreenController_SwitchLayoutDefault)
        return;
    void *layout = ControllerLayoutCache_GetOrInsert(
        (uint8_t *)controller + sources[menu_source].cache,
        FIELD(controller, sources[menu_source].layout, uint32_t));
    if (layout)
        UI_DisplayPreparedLayout(UI_GetLayoutManager(), layout);
}

static const struct core_hook hooks[] = {
    {CORE_SITE_PodcastEpisodes_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_PodcastEpisodes_CancelAppend_call, CORE_HOOK_FILTER, 5, context_rows},
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
    {CORE_SITE_ContextMenu_ScreenActionCompare_call, CORE_HOOK_FILTER, 5, screen_action},
    {CORE_SITE_ITunesUEpisodes_CancelAppend_call, CORE_HOOK_FILTER, 5, context_rows},
    {CORE_SITE_ITunesUEpisodes_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_Audiobooks_CancelAppend_call, CORE_HOOK_FILTER, 5, context_rows},
    {CORE_SITE_Audiobooks_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_Movies_ShowContextMenu_slot, CORE_HOOK_FILTER, 5, own_menu},
    {CORE_SITE_Movies_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_TVEpisodes_ShowContextMenu_slot, CORE_HOOK_FILTER, 5, own_menu},
    {CORE_SITE_TVEpisodes_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
    {CORE_SITE_NestedVideoPlaylists_Action_slot, CORE_HOOK_FILTER, 5, core_screens_action_filter},
};

void core_context_init(void)
{
    for (uint32_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        core_hook_bind(&hooks[i]);
}
