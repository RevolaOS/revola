/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_SCREENMENUS_H
#define CORE_SCREENMENUS_H
#include "ui/resources.h"
#include <stdint.h>

struct core_screen;

/* Writes the long-press menu's CEVT bindings of a list screen; returns how many
 * (0 without a menu). `w->out` NULL only counts. */
uint32_t core_screen_menu_events(struct core_writer *w, const struct core_screen *screen);
/* Stores the labels and the menu screen with its owner's title. */
int core_screen_menu_build(const struct core_screen *screen, uint8_t *scratch, uint32_t bytes);
/* Opens the menu or runs its row for the handler id `item`; 0 when `item` is
 * not one of the screen's menu. */
int core_screen_menu_action(struct core_screen *screen, void *controller, uint32_t item,
                            uint32_t argument);

#endif
