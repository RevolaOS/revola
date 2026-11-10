/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_MENUS_H
#define CORE_MENUS_H
#include "loader/loader.h"
#include "rv_api.h"

/* The menus of rv_menu, as an array bound. */
#define CORE_MENU_COUNT (RV_MENU_ARTIST_CONTEXT + 1)

/* Registers the descriptor's entries as rows of the native menus. */
int core_menus_register(struct rv_extension_record *owner, const struct rv_menu_entry *entries,
                        uint32_t count);
/* Hides the owner's rows and frees their records; the row data Apple keeps
 * pointers to is parked for the owner's next load. 0 when the heap had no room
 * to park it: the rows then stay as stale records and keep their ids. */
int core_menus_release(struct rv_extension_record *owner);
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
/* Redraws the value of every row of `owner` that shows the setting `key`. */
void core_menus_setting_changed(struct rv_extension_record *owner, uint16_t key);
/* The live entry of the row with this item id and *owner its extension, or NULL. */
const struct rv_menu_entry *core_menus_entry(uint32_t item, struct rv_extension_record **owner);
/* 1 when `id` is a Settings row's pane handler id; shows that row in the pane
 * unless `argument` is the support query. */
int core_menus_pane(uint32_t id, uint32_t argument);
/* 1 when `id` is the item or source id of a row, live, stale or parked. */
int core_menus_owns(uint32_t id);

/* The n-th live row of a context menu: its label's string id and the handler
 * name its action points at; 0 past the last. */
int core_menus_context_row(uint8_t root, uint32_t n, uint32_t *label, void **handler);

void core_menus_init(void *bank);
/* Context menu rows; called by core_menus_init. */
void core_context_init(void);
/* For an action in context menu `menu` dispatched to `controller`: the item
 * the menu opened on, read into `item` (MediaItem_bytes), or NULL, and in
 * *row the list's model row, -1 when none. */
const void *core_context_item(uint8_t menu, void *controller, void *item, int32_t *row);
/* The list controller whose context menu opened last, NULL when none did. */
void *core_context_list(void);
/* After a context row that opens a screen ran on `controller`: when no screen
 * was pushed, shows the layout the menu's hidden pop left behind again. */
void core_context_pushed(void *controller, int pushed);

#endif
