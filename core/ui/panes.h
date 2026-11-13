/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_PANES_H
#define CORE_PANES_H
#include "ui/resources.h"
#include <stdint.h>

struct core_screen;

/* The layout name the rows of a menu switch to for their pane, which their
 * pane handler points at the layout of the kind each row's picture asks for;
 * NULL for a menu without a pane, which is how callers tell the two apart. */
const char *core_panes_layout_name(uint8_t root);
/* A pane VLyt (the Settings > Main Menu pane's views) with the title line, the
 * line under the icon, the icon and the box behind it bound to these source
 * ids; a 0 source hides its view, and every other view is hidden. Returns the
 * stock pane VLyt it copied, 0 on failure. */
uint32_t core_panes_write(struct core_writer *w, uint32_t title, uint32_t line, uint32_t icon,
                          uint32_t box);
/* Stores a pane VLyt under `id` with the TEVT of `stock_pane`, which loading
 * the layout's events requires. */
void core_panes_store(uint32_t id, const void *data, uint32_t bytes, uint32_t stock_pane);
/* Builds the shared pane layouts of a menu's rows once: the kind its rows get
 * without a picture and the kinds in `pictures`, a bit per pane kind as the
 * manifest flags hold them (CORE_MANIFEST_PANES); 0 when it cannot or the menu
 * has no pane. */
int core_panes_prepare(uint8_t root, uint32_t pictures);
/* The row with this item id is highlighted: makes it the current row of the
 * pane its picture asks for and points the menu's pane name at that layout. */
void core_panes_show(uint8_t root, uint32_t item);
/* Apple loaded the events of the layout with this id, of screen `screen_id`'s
 * definition `definition`: a shared pane layout reads its stock row layout's
 * events, and the definition is the one the menu's panes must be known to. */
void core_panes_layout_events(void *layout, uint32_t layout_id, uint32_t screen_id,
                              const void *definition);
/* Asks the pane on screen for its icon and caption again: what they show changed. */
void core_panes_refresh(void);
/* The same for the panes showing a row of `owner`; api->pane_refresh. */
void core_panes_refresh_owner(struct rv_extension_record *owner);
/* `screen` closed: its pane, if it was the list pane shown, is forgotten. */
void core_panes_screen_closed(struct core_screen *screen);
/* Answers the properties of the panes' views; 0 for anything else. */
int core_panes_property(uint32_t type, uint32_t id, uint32_t out);
/* Answers the pane caption, icon and box of a split list screen. */
struct core_screen;
int core_panes_list_property(struct core_screen *screen, uint32_t type, uint32_t id, uint32_t out);
/* The handler a split choice list's highlight names: the pane line is read again. */
int core_panes_list_action(struct core_screen *screen, uint32_t id, uint32_t argument);
void core_panes_init(void *bank);

#endif
