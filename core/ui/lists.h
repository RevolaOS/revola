/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_LISTS_H
#define CORE_LISTS_H
#include "rv_api.h"
#include <stdint.h>

struct core_screen;

/* 1 for the list kinds whose one row expands by index. */
static inline int core_lists_indexed(uint8_t kind)
{
    return kind == RV_SCREEN_CHOICE || kind == RV_SCREEN_MULTI_CHOICE ||
           kind == RV_SCREEN_RUNTIME_LIST;
}

/* 1 for every list kind: the ones with two layouts. */
static inline int core_lists_kind(uint8_t kind)
{
    return kind == RV_SCREEN_LIST || core_lists_indexed(kind);
}

/* Ids the split layout of a list screen takes after the full layout's: its
 * screen, its layout, the pane, four layout clones, and the pane's caption,
 * icon and box sources. */
#define CORE_LIST_SPLIT_IDS 10
#define CORE_LIST_PANE_CAPTION(screen) ((screen) + 7)

/* The screen id a list screen is pushed as: its first id for the full layout,
 * the split layout's otherwise. */
uint32_t core_lists_screen_id(const struct core_screen *screen, int split);
/* Stores the clones of a list, choice, multi-choice or runtime list screen in
 * one layout; 0 when a stock resource is missing or the heap is full. The rows
 * and sources are shared and built with the first layout. */
int core_clone_list(struct core_screen *screen, int split);
/* The row a list screen's rows are copied from: Sound Check's for a row with a
 * value beside its label, else Legal's; NULL when the table is missing. */
const uint8_t *core_lists_prototype(int value);
void core_lists_init(void *bank);

#endif
