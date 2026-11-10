/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_LZ_H
#define CORE_LZ_H
#include <stdint.h>

/* Where an lz stream (loader/rvx.h) comes from: `stored` bytes in memory, or
 * read from an open file in chunks through `chunk`. */
struct core_lz_input {
    void *file;          /* NULL for memory */
    const uint8_t *next; /* unread bytes of the memory stream or the chunk */
    uint32_t have;       /* bytes at `next` */
    uint32_t left;       /* bytes still in the file */
    uint8_t chunk[128];
};

void core_lz_from_memory(struct core_lz_input *in, const void *stream, uint32_t stored);
/* The stream starts at the file's position. */
void core_lz_from_file(struct core_lz_input *in, void *file, uint32_t stored);
/* Decodes exactly `bytes` into `out`; 1 when the stream ends there, 0 for a
 * short, long or malformed stream or a read error. */
int core_lz_decode(struct core_lz_input *in, uint8_t *out, uint32_t bytes);

#endif
