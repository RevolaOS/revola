/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_TOGGLES_H
#define CORE_TOGGLES_H
#include "hooks/sites.h"
#include <stdint.h>

/* The Settings property path for Apple's property ids: adds the core's rows to
 * the toggle screens' counts, otherwise the result of `next`. */
uint32_t core_toggles_property(uint32_t model, uint32_t type, uint32_t id, uint32_t out,
                               const struct core_next *next);
/* 1 when (property, position) belongs to a toggle screen with core rows: the
 * position is mapped to Apple's index or answered for a core row, and *result
 * is what the indexed property returns. */
int core_toggles_indexed(uint32_t model, uint32_t type, uint32_t property, uint32_t position,
                         const struct core_next *next, uintptr_t *out, uint32_t *result);
void core_toggles_init(void *bank);

#endif
