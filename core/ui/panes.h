/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_PANES_H
#define CORE_PANES_H
#include "ui/resources.h"
#include <stdint.h>

/* The name core rows' delayedselected bindings switch to. */
extern const char core_panes_layout_name[];

/* The VLyt a stock layout gives its preview pane instance, 0 for none. */
uint32_t core_panes_stock_layout(uint32_t layout);
/* A pane VLyt (the Settings > Main Menu pane's views) with the caption, detail
 * and icon bound to these source ids; a 0 source hides its view, and every
 * other view is hidden. Returns the stock pane VLyt it copied, 0 on failure. */
uint32_t core_panes_write(struct core_writer *w, uint32_t caption, uint32_t detail, uint32_t icon);
/* Stores a pane VLyt under `id` with the TEVT of `stock_pane`, which loading
 * the layout's events requires. */
void core_panes_store(uint32_t id, const void *data, uint32_t bytes, uint32_t stock_pane);
/* Builds the shared pane layout of core Settings rows once; 0 when it cannot. */
int core_panes_prepare(void);
/* Makes the Settings row with this item id the one the pane shows. */
void core_panes_show(uint32_t item);
/* Answers the pane's caption, detail and icon properties; 0 for anything else. */
int core_panes_property(uint32_t type, uint32_t id, uint32_t out);
void core_panes_init(void *bank);

#endif
