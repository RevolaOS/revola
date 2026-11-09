/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_CLONES_H
#define CORE_CLONES_H
#include "rv_api.h"
#include <stdint.h>

struct core_screen;

#define CORE_MAX_LIST_ROWS 16

/* Ids a screen of this declaration takes from its owner's block. */
uint32_t core_clone_ids(const struct rv_screen *declaration);
/* Store the clones a screen is made of under its ids; 0 when a stock resource
 * is missing or the heap is full. */
int core_clone_list(struct core_screen *screen);
int core_clone_text(struct core_screen *screen);
int core_clone_slider(struct core_screen *screen);
int core_clone_canvas(struct core_screen *screen);
/* Reads an event stream into a loaded layout's event map. */
void core_clone_read_events(void *layout, const void *stream, uint32_t bytes);
void core_clones_init(void *bank);

#endif
