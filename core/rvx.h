/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

/* Flat module image written by rvx-pack (tools/rvx.py). The header is followed by
 * text and data, then reloc_count 32-bit image offsets of words that get the load
 * address added. BSS follows data in RAM. */
#define RVX_MAGIC 0x30585652 /* RVX0 */
#define RVX_FORMAT 1

struct rvx_header {
    uint32_t magic;
    uint32_t format;
    uint32_t api;      /* core API major << 16 | minor the module was built against */
    uint32_t firmware; /* RV_FIRMWARE_ID of the OSOS build the symbols came from */
    uint32_t module;   /* FNV-1a of "<extension id>/<module name>" */
    uint32_t text_bytes;
    uint32_t data_bytes;
    uint32_t bss_bytes;
    uint32_t reloc_count;
    uint32_t entry; /* image offset, bit 0 set for Thumb */
};

_Static_assert(sizeof(struct rvx_header) == 40, "rvx header layout");
