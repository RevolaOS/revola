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

/* Stores the clones of a list, choice, multi-choice or runtime list screen; 0
 * when a stock resource is missing or the heap is full. */
int core_clone_list(struct core_screen *screen);
void core_lists_init(void *bank);

#endif
