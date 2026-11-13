/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/memory.c by lgreitis. */

#include "osos-2.0.5.h"
#include "rv_app.h"

/* Every block carries its requested size, so realloc can shrink in place and
 * copy on growth; the host's blocks have no header of their own. */
union allocation_header {
    size_t size;
    uint64_t alignment;
};

void *rv_app_realloc(void *pointer, size_t size)
{
    if (!size) {
        rv_app_free(pointer);
        return NULL;
    }
    if (!pointer)
        return rv_app_alloc(size);

    union allocation_header *old = (union allocation_header *)pointer - 1;
    if (size <= old->size) {
        old->size = size;
        return pointer;
    }

    void *result = rv_app_alloc(size);
    if (!result)
        return NULL;
    for (size_t i = 0; i < old->size; ++i)
        ((uint8_t *)result)[i] = ((const uint8_t *)pointer)[i];
    rv_app_free(pointer);
    return result;
}

void *rv_app_alloc(size_t size)
{
    if (!size)
        size = 1;
    if (size > 8 * 1024 * 1024 - sizeof(union allocation_header))
        return NULL;

    size_t bytes = size + sizeof(union allocation_header);
    union allocation_header *header = GameMemory_Allocate(bytes);
    /* The block entry serves from the reserve; larger requests go to the heap. */
    if (!header)
        header = GameMemory_Reallocate(NULL, bytes);
    if (!header)
        return NULL;

    header->size = size;
    return header + 1;
}

void rv_app_free(void *pointer)
{
    if (pointer)
        GameMemory_Free((union allocation_header *)pointer - 1);
}

/* GCC emits calls to these for aggregates in an app without a C library. */
void *memcpy(void *destination, const void *source, size_t bytes)
{
    unsigned char *out = destination;
    const unsigned char *in = source;
    for (size_t i = 0; i < bytes; ++i)
        out[i] = in[i];
    return destination;
}

void *memset(void *destination, int value, size_t bytes)
{
    unsigned char *out = destination;
    for (size_t i = 0; i < bytes; i++)
        out[i] = (unsigned char)value;
    return destination;
}
