/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_SUBMENUS_H
#define CORE_SUBMENUS_H
#include <stdint.h>

struct core_screen;
struct core_container;

/* The container whose screen this is, or NULL. */
struct core_container *core_submenus_of(const struct core_screen *screen);
/* 1 when the screen is a container's whose rows changed since it was built;
 * clears the mark. */
int core_submenus_stale(struct core_screen *screen);
/* Stores a container screen's clones in one layout, with the rows shown now. */
int core_submenus_clone(struct core_screen *screen, int split);
void core_submenus_init(void *bank);

#endif
