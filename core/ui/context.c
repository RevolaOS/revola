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
