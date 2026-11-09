/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_MENUS_H
#define CORE_MENUS_H
#include "loader/loader.h"
#include "rv_api.h"

/* The native menus an entry names in rv_entry.in. */
enum rv_root {
    RV_ROOT_MAIN,            /* "main" */
    RV_ROOT_MUSIC,           /* "music" */
    RV_ROOT_EXTRAS,          /* "extras" */
    RV_ROOT_SETTINGS,        /* "settings" */
    RV_ROOT_NOWPLAYING_MENU, /* "nowplaying": the Now Playing context menu */
    RV_ROOT_SONG_MENU,       /* "song": the song list context menus */
    RV_ROOT_ALBUM_MENU,      /* "album" */
    RV_ROOT_ARTIST_MENU,     /* "artist" */
    RV_ROOT_COUNT,
};

/* Registers the descriptor's entries as rows of the native menus. */
int core_menus_register(struct rv_extension_record *owner, const struct rv_entry *entries,
                        uint32_t count);
/* Hides the owner's rows; they stay allocated because Apple keeps pointers. */
void core_menus_release(struct rv_extension_record *owner);
/* The entry row whose item or source id is `id`, or NULL; *owner receives its
 * extension and *item its item id. */
const struct rv_row *core_menus_row(uint32_t id, struct rv_extension_record **owner,
                                    uint32_t *item);

/* Live rows of a Main or Music root, in registration order, for the Settings
 * toggle screens: their count, the n-th row's label, whether it shows and the
 * stock row it follows, and flipping it. */
uint32_t core_menus_count(uint8_t root);
const char *core_menus_label(uint8_t root, uint32_t n, int *visible, uint32_t *after);
void core_menus_toggle(uint8_t root, uint32_t n);
/* The live entry of the row with this item id, or NULL. */
const struct rv_entry *core_menus_entry(uint32_t item);
/* 1 when `id` is a Settings row's pane handler id; shows that row in the pane
 * unless `argument` is the support query. */
int core_menus_pane(uint32_t id, uint32_t argument);
/* 1 when `id` is the item or source id of a row, live or stale. */
int core_menus_owns(uint32_t id);

void core_menus_init(void *bank);

#endif
