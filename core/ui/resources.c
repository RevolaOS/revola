/* SPDX-License-Identifier: GPL-3.0-only */

/* Reading and writing Apple UI resources: block lists, event streams, property
 * sources and the per-owner resource id blocks. */

#include "ui/resources.h"
#include "loader/loader.h"
#include "osos-2.0.5.h"

uint32_t core_word_at(const void *data, uint32_t offset)
{
    const uint8_t *p = (const uint8_t *)data + offset;
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint32_t core_blocks_count(const void *list)
{
    return core_word_at(list, 0);
}

int core_block_at(const void *list, uint32_t index, struct core_block *out)
{
    const uint8_t *p = (const uint8_t *)list + 4;
    if (index >= core_blocks_count(list))
        return 0;
    for (uint32_t i = 0; i <= index; i++) {
        uint32_t bytes = core_word_at(p, 0), kind = core_word_at(p, 4);
        if (i == index) {
            out->data = p + 8;
            out->bytes = bytes;
            out->kind = kind;
            return 1;
        }
        p += 8 + ((bytes + 3) & ~3u);
    }
    return 0;
}

const uint8_t *core_block_with_word(const void *list, uint32_t offset, uint32_t id,
                                    uint32_t *bytes)
{
    struct core_block block;
    for (uint32_t i = 0; i < core_blocks_count(list); i++) {
        if (!core_block_at(list, i, &block))
            break;
        if (block.bytes > offset + 3 && core_word_at(block.data, offset) == id) {
            *bytes = block.bytes;
            return block.data;
        }
    }
    return 0;
}

void core_write_bytes(struct core_writer *w, const void *data, uint32_t bytes)
{
    if (w->out)
        for (uint32_t i = 0; i < bytes; i++)
            w->out[w->at + i] = ((const uint8_t *)data)[i];
    w->at += bytes;
}

void core_write_copy(struct core_writer *w, const void *data, uint32_t bytes,
                     const uint32_t *words, uint32_t count)
{
    uint32_t start = w->at;
    core_write_bytes(w, data, bytes);
    if (w->out)
        for (uint32_t i = 0; i < count; i++)
            for (unsigned b = 0; b < 4; b++)
                w->out[start + words[2 * i] + b] = (uint8_t)(words[2 * i + 1] >> (8 * b));
}

void core_write_word(struct core_writer *w, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 24)};
    core_write_bytes(w, bytes, 4);
}

static uint32_t length(const char *text)
{
    uint32_t n = 0;
    while (text[n])
        n++;
    return n;
}

void core_write_string(struct core_writer *w, const char *text)
{
    core_write_word(w, length(text));
    core_write_bytes(w, text, length(text));
}

uint32_t core_write_block_begin(struct core_writer *w, uint32_t kind)
{
    uint32_t begin = w->at;
    core_write_word(w, 0);
    core_write_word(w, kind);
    return begin;
}

void core_write_block_end(struct core_writer *w, uint32_t begin)
{
    uint32_t bytes = w->at - begin - 8;
    if (w->out)
        for (unsigned i = 0; i < 4; i++)
            w->out[begin + i] = (uint8_t)(bytes >> (8 * i));
    while (w->at % 4)
        core_write_bytes(w, "", 1);
}

void core_write_event(struct core_writer *w, const char *name, const char *handler,
                      const char *action, const char *const *arguments, uint32_t count)
{
    core_write_string(w, name);
    core_write_bytes(w, "1", 1);
    core_write_string(w, handler);
    core_write_word(w, action != 0);
    if (!action)
        return;
    core_write_string(w, action);
    core_write_word(w, count);
    for (uint32_t i = 0; i < count; i++)
        core_write_string(w, arguments[i]);
}

void core_set_source(void *bank, uint32_t id, uint32_t property, uint32_t destination)
{
    uint32_t source[6] = {1, 12, Resource_SORC, Service_Settings, property, destination};
    ResourceBank_SetResource(bank, Resource_SORC, id, source, sizeof source);
}

void core_set_source2(void *bank, uint32_t id, uint32_t title, uint32_t second,
                      uint32_t destination)
{
    uint32_t source[11] = {2, 12, Resource_SORC, Service_Settings, title, SourceDestination_Title,
                           12, Resource_SORC, Service_Settings, second, destination};
    ResourceBank_SetResource(bank, Resource_SORC, id, source, sizeof source);
}

uint32_t core_resource_owner(const struct rv_extension_record *owner)
{
    return owner ? owner->owner : 0;
}

#define MAX_BLOCKS 17 /* the core and at most 16 extensions */

/* Blocks in the order owners first take ids; an owner keeps its block for the
 * boot, so an extension loaded again finds its clones under the same ids. */
static struct {
    uint32_t owner;
    uint32_t used;
} blocks[MAX_BLOCKS];

static uint32_t slot_of(uint32_t owner)
{
    for (uint32_t i = 0; i < MAX_BLOCKS; i++)
        if (blocks[i].used && blocks[i].owner == owner)
            return i;
    return MAX_BLOCKS;
}

static uint32_t base_of(uint32_t owner, uint32_t slot)
{
    return owner ? 0x0c000000u + slot * CORE_RESOURCE_BLOCK_BYTES : CORE_RESOURCE_CORE_BLOCK;
}

uint32_t core_resource_block(uint32_t owner)
{
    uint32_t slot = slot_of(owner);
    return slot < MAX_BLOCKS ? base_of(owner, slot) : 0;
}

uint32_t core_resource_take(uint32_t owner, uint32_t count)
{
    uint32_t slot = slot_of(owner);
    if (slot == MAX_BLOCKS)
        for (uint32_t i = 0; i < MAX_BLOCKS && slot == MAX_BLOCKS; i++)
            if (!blocks[i].used)
                slot = i;
    if (slot == MAX_BLOCKS || !count || blocks[slot].used + count > CORE_RESOURCE_BLOCK_BYTES)
        return 0;
    blocks[slot].owner = owner;
    blocks[slot].used += count;
    return base_of(owner, slot) + blocks[slot].used - count;
}

char *core_put_text(char *out, const char *end, const char *value)
{
    while (*value && out < end - 1)
        *out++ = *value++;
    *out = 0;
    return out;
}

char *core_put_number(char *out, const char *end, uint32_t value)
{
    char digits[12];
    int n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n && out < end - 1)
        *out++ = digits[--n];
    *out = 0;
    return out;
}
