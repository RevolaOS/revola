/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_MEMORY_H
#define CORE_MEMORY_H
#include <stdint.h>

/* Apple's global memory policy, adjusted per core setting. */
void core_memory_bind(void);
int core_storage_is_flash(const uint16_t *identify);
uint32_t core_block_cache_size(uint32_t heap_total, uint32_t import_cost, int flash);

#endif
