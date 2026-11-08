/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

/* Flat module image. Text and data follow the header, then reloc_count 32-bit
 * image offsets whose words get the load address added. BSS follows data in RAM. */
#define RVX_MAGIC 0x30585652 /* RVX0 */

struct rvx_header {
    uint32_t magic;
    uint32_t text_bytes;
    uint32_t data_bytes;
    uint32_t bss_bytes;
    uint32_t reloc_count;
    uint32_t entry; /* image offset, bit 0 set for Thumb */
};

struct rv_api {
    uint32_t version;
    void *(*alloc)(uint32_t bytes);
    void (*free)(void *block);
};
