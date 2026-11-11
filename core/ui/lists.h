/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_LISTS_H
#define CORE_LISTS_H
#include "rv_api.h"
#include <stdint.h>

struct core_screen;

/* 1 for the list kinds whose one row expands by index. */
static inline int core_lists_indexed(uint8_t kind)
{
    return kind == RV_SCREEN_KIND_CHOICE || kind == RV_SCREEN_KIND_MULTI_CHOICE ||
           kind == RV_SCREEN_KIND_DYNAMIC_LIST;
}

/* 1 for every list kind: the ones with two layouts. */
static inline int core_lists_kind(uint8_t kind)
{
    return kind == RV_SCREEN_KIND_LIST || core_lists_indexed(kind);
}

/* Ids the split layout of a list screen takes after the full layout's: its
 * screen, its layout, the pane, four layout clones, and the pane's caption,
 * icon and box sources. */
#define CORE_LIST_SPLIT_IDS 10
#define CORE_LIST_PANE_CAPTION(screen) ((screen) + 7)

/* A long-press menu takes one id for the long press and one per row (its label
 * string; "Rv" and the id name its handler), plus four for its titled screen
 * and layout, before the split layout's ids. */
#define CORE_MAX_CONTEXT_ROWS 16
static inline uint32_t core_lists_context_ids(const struct rv_screen *d)
{
    return d->context_count ? 5 + d->context_count : 0;
}
/* The long press's id of a list screen with a menu; its rows follow. */
#define CORE_LIST_CONTEXT(s)                                                                \
    ((s)->id + (s)->ids - CORE_LIST_SPLIT_IDS - core_lists_context_ids((s)->declaration))

/* The screen id a list screen is pushed as: its first id for the full layout,
 * the split layout's otherwise. */
uint32_t core_lists_screen_id(const struct core_screen *screen, int split);
/* Stores the clones of a list, choice, multi-choice or dynamic list screen in
 * one layout; 0 when a stock resource is missing or the heap is full. The rows
 * and sources are shared and built with the first layout. */
int core_clone_list(struct core_screen *screen, int split);
/* The row a list screen's rows are copied from: Sound Check's for a row with a
 * value beside its label, else Legal's; NULL when the table is missing. */
const uint8_t *core_lists_prototype(int value);
/* Appends the shared native empty-message view to a cloned list layout. */
int core_list_empty(struct core_screen *screen, uint32_t layout, uint32_t *next,
                    uint8_t *scratch, uint32_t bytes);
void core_lists_init(void *bank);

#endif
