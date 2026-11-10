/* SPDX-License-Identifier: GPL-3.0-only */

/* Reading and writing Apple UI resources: block lists, event streams, property
 * sources and the per-owner resource id blocks. */

#include "ui/resources.h"
#include "loader/loader.h"
#include "osos-2.0.5.h"
#include "ui/screens.h"
#include <stddef.h>

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

void core_set_source(uint32_t id, uint32_t property, uint32_t destination)
{
    uint32_t source[6] = {1, 12, Resource_SORC, Service_Settings, property, destination};
    core_resource_set(Resource_SORC, id, source, sizeof source);
}

void core_set_source2(uint32_t id, uint32_t title, uint32_t second, uint32_t destination)
{
    uint32_t source[11] = {2, 12, Resource_SORC, Service_Settings, title, SourceDestination_Title,
                           12, Resource_SORC, Service_Settings, second, destination};
    core_resource_set(Resource_SORC, id, source, sizeof source);
}

uint32_t core_resource_owner(const struct rv_extension_record *owner)
{
    return owner ? owner->owner : 0;
}

/* The core, 16 extensions, and spares for extensions installed or replaced
 * since boot. */
#define MAX_BLOCKS 25
/* The owner of a retired block: no owner matches it, so its ids stay unused. */
#define RETIRED 0xffffffffu

/* Blocks in the order owners first take ids. An owner keeps its slot for the
 * boot, so it takes the same ids again when it loads again. */
static struct {
    uint32_t owner;
    uint32_t bytes; /* Apple's heap for the overrides under the block's ids */
    uint16_t used;
    uint8_t held;
    uint8_t releasing; /* the owner is gone; its overrides go once off screen */
    uint8_t counted;   /* `bytes` are in the loaded owner's live bytes */
} blocks[MAX_BLOCKS];
static void *bank;

/* The extension block an id lies in; MAX_BLOCKS for the core's ids and
 * Apple's. */
static uint32_t slot_of_id(uint32_t id)
{
    uint32_t slot = (id - 0x0c000000u) / CORE_RESOURCE_BLOCK_BYTES;
    return slot < MAX_BLOCKS && blocks[slot].held && blocks[slot].owner ? slot : MAX_BLOCKS;
}

/* An override costs its copy, a 16-byte entry and an 8-byte map slot. */
static int32_t override_cost(uint32_t bytes)
{
    return (int32_t)(core_heap_cost(bytes) + 16 + 8);
}

static void count(uint32_t id, int32_t bytes)
{
    uint32_t slot = slot_of_id(id);
    if (slot == MAX_BLOCKS) {
        core_loader_charge(NULL, bytes);
        return;
    }
    blocks[slot].bytes += (uint32_t)bytes;
    core_loader_charge(blocks[slot].counted ? core_loader_find(blocks[slot].owner) : NULL, bytes);
}

static uint32_t *override_of(uint32_t type, uint32_t id)
{
    uint32_t *entry;
    void *map = *(void **)((uint8_t *)bank + ResourceBank_overrides);
    return map && ResourceOverrides_Find(map, type, id, &entry) ? entry : NULL;
}

void core_resource_set(uint32_t type, uint32_t id, const void *data, uint32_t bytes)
{
    uint32_t *old = override_of(type, id);
    int32_t before = old ? override_cost(old[ResourceOverride_length / 4]) : 0;
    ResourceBank_SetResource(bank, type, id, data, bytes);
    if (override_of(type, id))
        count(id, override_cost(bytes) - before);
}

static uint32_t slot_of(uint32_t owner)
{
    for (uint32_t i = 0; i < MAX_BLOCKS; i++)
        if (blocks[i].held && blocks[i].owner == owner)
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

/* Everything the clones, rows and panes store under an owner's ids, except
 * the ITEM tables (see resources.h). */
static const uint32_t removable[] = {
    Resource_SCST, Resource_SLst, Resource_SLyt, Resource_SSin, Resource_CEVT,
    Resource_SEVT, Resource_VSlt, Resource_VLyt, Resource_TEVT, Resource_VCvs,
    Resource_VCrv, Resource_SORC, Resource_String,
};

/* The bank has no remove for a ROM-backed bank; its heap-bank remove is these
 * two map calls and the frees, which work on the override map as well. */
static void remove_override(uint32_t type, uint32_t id)
{
    uint32_t *entry = override_of(type, id);
    void *map = *(void **)((uint8_t *)bank + ResourceBank_overrides);
    if (!entry)
        return;
    count(id, -override_cost(entry[ResourceOverride_length / 4]));
    ResourceOverrides_EraseEntry(map, type, entry);
    Heap_Free((void *)entry[ResourceOverride_data / 4]);
    Heap_Free(entry);
}

/* 1 when the last displayed screen or layout is in the block: the layout
 * manager reads its SLyt and SSin again on the next display. */
static int displayed(uint32_t base)
{
    uint8_t *manager = UI_GetLayoutManager();
    return manager &&
           (*(uint32_t *)(manager + LayoutManager_screen) - base < CORE_RESOURCE_BLOCK_BYTES ||
            *(uint32_t *)(manager + LayoutManager_layout) - base < CORE_RESOURCE_BLOCK_BYTES);
}

/* Removes the block's overrides and gives its ids back; 0 while a controller
 * of the owner is still alive or its layout was the last displayed. */
static int clear(uint32_t slot)
{
    uint32_t base = base_of(blocks[slot].owner, slot);
    if (core_screens_orphans() || displayed(base))
        return 0;
    for (uint32_t id = base; id < base + blocks[slot].used; id++)
        for (uint32_t t = 0; t < sizeof removable / sizeof removable[0]; t++)
            remove_override(removable[t], id);
    blocks[slot].used = 0;
    blocks[slot].releasing = 0;
    return 1;
}

uint32_t core_resource_take(uint32_t owner, uint32_t count)
{
    uint32_t slot = slot_of(owner);
    if (slot == MAX_BLOCKS)
        for (uint32_t i = 0; i < MAX_BLOCKS && slot == MAX_BLOCKS; i++)
            if (!blocks[i].held)
                slot = i;
    /* Loaded again before its overrides could go: they stay, and the owner's
     * screens find them under the same ids. */
    if (slot < MAX_BLOCKS && blocks[slot].releasing && !clear(slot)) {
        blocks[slot].used = 0;
        blocks[slot].releasing = 0;
    }
    if (slot == MAX_BLOCKS || !count || blocks[slot].used + count > CORE_RESOURCE_BLOCK_BYTES)
        return 0;
    blocks[slot].owner = owner;
    blocks[slot].held = 1;
    blocks[slot].used += count;
    /* What an earlier load left counts to this one again. */
    struct rv_extension_record *record = owner ? core_loader_find(owner) : NULL;
    if (record && !blocks[slot].counted) {
        core_loader_charge(NULL, -(int32_t)blocks[slot].bytes);
        core_loader_charge(record, (int32_t)blocks[slot].bytes);
        blocks[slot].counted = 1;
    }
    return base_of(owner, slot) + blocks[slot].used - count;
}

void core_resource_release(uint32_t owner, int remove)
{
    uint32_t slot = slot_of(owner);
    if (slot == MAX_BLOCKS || !owner)
        return;
    if (blocks[slot].counted) {
        core_loader_charge(core_loader_find(owner), -(int32_t)blocks[slot].bytes);
        core_loader_charge(NULL, (int32_t)blocks[slot].bytes);
        blocks[slot].counted = 0;
    }
    if (!remove)
        return;
    blocks[slot].releasing = 1;
    clear(slot);
}

int core_resource_full(void)
{
    for (uint32_t i = 0; i < MAX_BLOCKS; i++)
        if (!blocks[i].held)
            return 0;
    return 1;
}

void core_resource_retire(uint32_t owner)
{
    uint32_t slot = slot_of(owner);
    if (slot < MAX_BLOCKS && owner)
        blocks[slot].owner = RETIRED;
}

void core_resource_tick(void)
{
    for (uint32_t i = 0; i < MAX_BLOCKS; i++)
        if (blocks[i].releasing)
            clear(i);
}

void core_resources_init(void *resource_bank)
{
    bank = resource_bank;
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
