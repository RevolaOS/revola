/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_ICONS_H
#define CORE_ICONS_H
#include <stdint.h>

/* The core icon whose name hashes to `name` decoded into a loader block of the
 * core (retire it with core_loader_retire), or NULL. */
void *core_icon_decode(uint32_t name);

#endif
