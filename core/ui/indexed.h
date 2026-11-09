/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_INDEXED_H
#define CORE_INDEXED_H
#include <stdint.h>

struct core_screen;

/* Answers the plain properties of a choice, multi-choice or runtime list's row
 * (Property_Count, Property_LayoutSet); 0 for anything else. */
int core_indexed_property(struct core_screen *screen, uint32_t type, uint32_t id, uint32_t out);
/* Makes the controller vtable a choice screen needs; 0 when the heap is full.
 * Called before the controller is created. */
int core_indexed_prepare(struct core_screen *screen);
/* Gives a created choice controller that vtable. */
void core_indexed_attach(struct core_screen *screen, void *controller);
/* The row of a choice, multi-choice or runtime list was chosen. */
void core_indexed_chosen(struct core_screen *screen);
void core_indexed_init(void *bank);

#endif
