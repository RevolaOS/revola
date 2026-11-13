/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

/* One extension in one file, written by rvx-pack (tools/rvx.py). Sections are in
 * the order the core reads them:
 *
 *   header    struct rvx_header
 *   manifest  manifest_bytes, padded to a word; read for every installed file
 *   catalog   entry_count struct rvx_entry: the manifest's modules in its order
 *             (module i is entry i), then the assets by preload class, always
 *             first, never last
 *   payloads  word-aligned, at the catalog's offsets: main module, the assets
 *             that may be preloaded, other modules, the assets read on request
 *
 * A module payload is struct rvx_module, text and data, then reloc_count 32-bit
 * image offsets of words that get the load address added. BSS follows data in
 * RAM. Modules are stored raw.
 *
 * A preloaded asset is read into RAM with the main module and stays there as
 * stored. The others are read when the extension asks for them. Which of the
 * optional ones are preloaded is the core's choice. */
#define RVX_MAGIC 0x30585652 /* RVX0 */
#define RVX_FORMAT 1

struct rvx_header {
    uint32_t magic;
    uint32_t format;
    uint32_t api;      /* core API major << 16 | minor the modules were built against */
    uint32_t firmware; /* RV_FIRMWARE_ID of the OSOS build the symbols came from */
    uint32_t manifest_bytes;
    uint32_t entry_count;
    uint32_t body_hash; /* FNV-1a of every byte after the header: manifest, catalog, payloads */
};

enum { RVX_MODULE, RVX_DATA, RVX_BITMAP };

/* When an asset is read into RAM: always, when the core prefers speed over
 * memory, or never. Modules carry always. */
enum { RVX_PRELOAD_ALWAYS, RVX_PRELOAD_OPTIONAL, RVX_PRELOAD_NEVER };

/* lz: groups of a flag byte and up to 8 items, bit 0 first. A clear bit is a
 * literal byte. A set bit is a match of two bytes, LLLLDDDD DDDDDDDD: it copies
 * L + 3 bytes from D + 1 bytes back in the output; L = 15 adds the following
 * bytes to the length while they are 255, and the first that is not. The stream
 * ends when the output is full. */
enum { RVX_RAW, RVX_LZ };

struct rvx_entry {
    uint32_t name; /* FNV-1a of the module or asset name */
    uint8_t kind;
    uint8_t codec;
    uint8_t preload;  /* RVX_PRELOAD_, non-decreasing over the assets */
    uint8_t reserved; /* 0 */
    uint32_t offset; /* file offset of the payload */
    uint32_t stored; /* payload bytes in the file */
    uint32_t bytes;  /* payload bytes once decoded */
};

struct rvx_module {
    uint32_t text_bytes;
    uint32_t data_bytes;
    uint32_t bss_bytes;
    uint32_t reloc_count;
    uint32_t entry; /* image offset, bit 0 set for Thumb */
};

_Static_assert(sizeof(struct rvx_header) == 28, "rvx header layout");
_Static_assert(sizeof(struct rvx_entry) == 20, "rvx entry layout");
_Static_assert(sizeof(struct rvx_module) == 20, "rvx module layout");
