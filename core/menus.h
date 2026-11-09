/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_MENUS_H
#define CORE_MENUS_H
#include "loader.h"
#include "rv_api.h"

/* Registers the descriptor's entries as rows of the native menus. */
int core_menus_register(struct rv_extension_record *owner, const struct rv_entry *entries,
                        uint32_t count);
/* Hides the owner's rows; they stay allocated because Apple keeps pointers. */
void core_menus_release(struct rv_extension_record *owner);
/* The entry row whose item or source id is `id`, or NULL; *owner receives its
 * extension and *item its item id. */
const struct rv_row *core_menus_row(uint32_t id, struct rv_extension_record **owner,
                                    uint32_t *item);

void core_menus_init(void *bank);

#endif
