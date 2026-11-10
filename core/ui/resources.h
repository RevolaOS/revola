/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_RESOURCES_H
#define CORE_RESOURCES_H
#include <stdint.h>

/* Apple UI resources are block lists: a count, then {bytes, kind, data} padded
 * to four bytes. Event tables are byte streams whose words are unaligned. */

struct core_block {
    const uint8_t *data;
    uint32_t bytes;
    uint32_t kind;
};

uint32_t core_blocks_count(const void *list);
/* Block `index` of `list`; 0 when there is none. */
int core_block_at(const void *list, uint32_t index, struct core_block *out);
/* The block whose data starts with the word `id` (menu rows keyed by MenuRow_id). */
const uint8_t *core_block_with_word(const void *list, uint32_t offset, uint32_t id,
                                    uint32_t *bytes);
uint32_t core_word_at(const void *data, uint32_t offset);

/* Writer for resources and event streams. With `out` NULL it only counts, so a
 * buffer is sized by a first pass and filled by a second. */
struct core_writer {
    uint8_t *out;
    uint32_t at;
};

void core_write_bytes(struct core_writer *w, const void *data, uint32_t bytes);
void core_write_word(struct core_writer *w, uint32_t value);
/* A copy of `data` with words changed; `words` are {offset, value} pairs. */
void core_write_copy(struct core_writer *w, const void *data, uint32_t bytes,
                     const uint32_t *words, uint32_t count);
void core_write_string(struct core_writer *w, const char *text);
/* Starts a block; returns what core_write_block_end needs. */
uint32_t core_write_block_begin(struct core_writer *w, uint32_t kind);
void core_write_block_end(struct core_writer *w, uint32_t begin);
/* One event: name, handler (may be ""), optional action with its arguments. */
void core_write_event(struct core_writer *w, const char *name, const char *handler,
                      const char *action, const char *const *arguments, uint32_t count);

/* A property source: the title (destination 1) or inline value (10) of a row or
 * view comes from Settings model property `property`, which the core answers. */
void core_set_source(uint32_t id, uint32_t property, uint32_t destination);
/* A title source with a second binding: `second` for `destination`. */
void core_set_source2(uint32_t id, uint32_t title, uint32_t second, uint32_t destination);
/* Stores a resource in the bank, counting Apple's copy to the extension whose
 * ids it is under while it is loaded, else to the core. */
void core_resource_set(uint32_t type, uint32_t id, const void *data, uint32_t bytes);

/* Resource ids: a block per extension in the order extensions first take ids,
 * below the core's block and Apple's. An owner keeps its block for the boot
 * and takes the same ids again when it loads again, so the resources it stores
 * under them replace the earlier copies instead of adding to them.
 * Every core-made id starts with CORE_RESOURCE_PREFIX; Apple's Settings
 * property ids do not, which lets a property filter bail out on one compare. */
#define CORE_RESOURCE_PREFIX 0x0c
#define CORE_RESOURCE_BLOCK_BYTES 0x1000
#define CORE_RESOURCE_CORE_BLOCK 0x0cf00100u /* the core's screens and rows */
struct rv_extension_record;
/* The key of an owner's block: its settings owner hash, 0 for the core. */
uint32_t core_resource_owner(const struct rv_extension_record *owner);
/* The first id of the owner's block, 0 before it took any. */
uint32_t core_resource_block(uint32_t owner);
/* `count` consecutive ids of the owner's block, 0 when the block or the table
 * of blocks is full. */
uint32_t core_resource_take(uint32_t owner, uint32_t count);
/* An owner unloads: what stays under its ids counts to the core. With
 * `remove`, gives its ids back and removes the resources stored under them
 * from the bank, now or at a later tick once nothing of the owner is on
 * screen. ITEM tables stay: a list view keeps the table it last showed until
 * another is bound. */
void core_resource_release(uint32_t owner, int remove);
/* After the release: the owner's next load takes a new block, so nothing Apple
 * keeps by id from the old one (ITEM tables, a built SCST) reaches a changed
 * extension. The old ids are never given out again this boot. */
void core_resource_retire(uint32_t owner);
/* 1 when every block is held. */
int core_resource_full(void);
/* Grace point: finishes releases that had to wait. */
void core_resource_tick(void);
void core_resources_init(void *bank);

/* Decimal digits of `value` appended at `out`, bounded by `end`; returns the end. */
char *core_put_number(char *out, const char *end, uint32_t value);
char *core_put_text(char *out, const char *end, const char *value);

#endif
