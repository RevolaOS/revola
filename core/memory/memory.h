/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_MEMORY_H
#define CORE_MEMORY_H
#include <stdint.h>

/* Apple's global memory policy, adjusted per core setting. */
void core_memory_bind(void);
int core_storage_is_flash(const uint16_t *identify);
uint32_t core_block_cache_size(uint32_t heap_total, uint32_t import_cost, int flash);
uint32_t core_media_read_ahead(uint32_t cache_bytes);
uint32_t core_block_cache_shortfall(uint32_t total, uint32_t kept, uint32_t request,
                                    uint32_t heap_free, uint32_t block_bytes);

#endif
