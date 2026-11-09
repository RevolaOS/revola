/* SPDX-License-Identifier: GPL-3.0-only */

/* Site table, handler lists and the chain walker. One pointer slot per site: the
 * handler itself when one is bound, core_chain_N when several are, so hot sites pay
 * one indirect jump. Handler lists are published by a single aligned store and the
 * retired list is freed at the next grace point; nothing is counted or locked while
 * a handler runs. */

#include "osos-2.0.5.h"
#include "hooks/sites.h"
#include <stddef.h>

#define BL(address, callee) (0xeb000000u | ((((callee) - (address) - 8u) >> 2) & 0xffffffu))
#define REPLAY(name) ((uint32_t)core_replay_##name)

#define SITE(name, address, kind, original, word0, word1, chain, argc, copy)                     \
    extern char core_replay_##name[];
#include "hooks/sites.def"
#undef SITE

const struct core_site_row core_sites[CORE_SITE_COUNT] = {
#define SITE(name, address, kind, original, word0, word1, chain, argc, copy)                     \
    {address, (uint32_t)(original), word0, word1, CORE_SITE_##kind, chain, argc, 0},
#include "hooks/sites.def"
#undef SITE
};
const uint32_t core_site_count = CORE_SITE_COUNT;

struct hook_list {
    struct hook_list *retired; /* pending-free chain */
    uint32_t count;
    const struct core_hook *hooks[];
};

static struct hook_list *lists[CORE_SITE_TOTAL];
static struct hook_list *retired;
struct core_mmu_probe core_mmu_probe;

enum { RAW_FREE, RAW_CLAIMED, RAW_RETIRING, RAW_RETIRED };

/* Written when the site is claimed. Up to three arguments: &root goes in the
 * register after them and the stub jumps through slot. Four: the frame of the
 * generated four-argument stubs, &root as the first stack argument. */
struct raw_stub {
    uint32_t code[7];
    uint32_t slot;
    struct core_next root;
};

static const uint32_t raw_frame[7] = {
    0xe92d4030, /* push {r4, r5, lr} */
    0xe28f4014, /* add r4, pc, #20: &root */
    0xe52d4004, /* push {r4} */
    0xe59fc008, /* ldr r12, slot */
    0xe12fff3c, /* blx r12 */
    0xe28dd004, /* add sp, sp, #4 */
    0xe8bd8030, /* pop {r4, r5, pc} */
};

struct raw_site {
    struct core_site_row row;
    uint32_t owner;
    uint8_t state;
};

static struct raw_site raw_sites[CORE_RAW_SITES];
static struct raw_stub raw_stubs[CORE_RAW_SITES];

static const struct core_site_row *row_of(uint32_t site)
{
    return site < CORE_SITE_COUNT ? &core_sites[site] : &raw_sites[site - CORE_SITE_COUNT].row;
}

static uint32_t *slot_of(uint32_t site)
{
    return site < CORE_SITE_COUNT ? &core_site_slots[site]
                                  : &raw_stubs[site - CORE_SITE_COUNT].slot;
}

static uint32_t stub_of(uint32_t site)
{
    return site < CORE_SITE_COUNT ? core_site_stubs[site]
                                  : (uint32_t)&raw_stubs[site - CORE_SITE_COUNT];
}

/* A site index that can take a handler: a core row or a claimed raw site. */
static int usable(uint32_t site)
{
    return site < CORE_SITE_COUNT ||
           (site < CORE_SITE_TOTAL && raw_sites[site - CORE_SITE_COUNT].state == RAW_CLAIMED);
}

typedef uint32_t (*fn0)(const struct core_next *);
typedef uint32_t (*fn1)(uint32_t, const struct core_next *);
typedef uint32_t (*fn2)(uint32_t, uint32_t, const struct core_next *);
typedef uint32_t (*fn3)(uint32_t, uint32_t, uint32_t, const struct core_next *);
typedef uint32_t (*fn4)(uint32_t, uint32_t, uint32_t, uint32_t, const struct core_next *,
                        uint32_t, uint32_t);

static uint32_t call(void *fn, uint8_t argc, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                     const struct core_next *next, uint32_t e, uint32_t f)
{
    switch (argc & 15) {
    case 0:
        return ((fn0)fn)(next);
    case 1:
        return ((fn1)fn)(a, next);
    case 2:
        return ((fn2)fn)(a, b, next);
    case 3:
        return ((fn3)fn)(a, b, c, next);
    default:
        return ((fn4)fn)(a, b, c, d, next, e, f);
    }
}

uint32_t core_next_call6(const struct core_next *next, uint32_t a, uint32_t b, uint32_t c,
                         uint32_t d, uint32_t e, uint32_t f)
{
    const struct core_site_row *row = row_of(next->site);
    struct hook_list *list = lists[next->site];
    if (list && next->index < list->count) {
        struct core_next following = {next->site, next->index + 1};
        return call(list->hooks[next->index]->handler, row->argc, a, b, c, d, &following, e,
                    f);
    }
    typedef uint32_t (*original_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                    uint32_t);
    return ((original_fn)row->original)(a, b, c, d, e, f);
}

/* Chain entries: the slot target while two or more handlers are bound. The stub
 * passes the site's root continuation, whose index is past any list. */
static uint32_t chain(const struct core_next *root, uint32_t a, uint32_t b, uint32_t c,
                      uint32_t d)
{
    struct core_next first = {root->site, 0};
    return core_next_call(&first, a, b, c, d);
}
static uint32_t chain0(const struct core_next *n) { return chain(n, 0, 0, 0, 0); }
static uint32_t chain1(uint32_t a, const struct core_next *n) { return chain(n, a, 0, 0, 0); }
static uint32_t chain2(uint32_t a, uint32_t b, const struct core_next *n)
{
    return chain(n, a, b, 0, 0);
}
static uint32_t chain3(uint32_t a, uint32_t b, uint32_t c, const struct core_next *n)
{
    return chain(n, a, b, c, 0);
}
static uint32_t chain4(uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                       const struct core_next *n, uint32_t e, uint32_t f)
{
    struct core_next first = {n->site, 0};
    return core_next_call6(&first, a, b, c, d, e, f);
}
static void *const chains[] = {chain0, chain1, chain2, chain3, chain4};

/* The uncached alias covers DRAM only; IRAM is uncached and writable already. */
static volatile uint32_t *writable(uint32_t address)
{
    if (address >= DRAM_BASE && address < DRAM_END)
        address |= core_mmu_probe.alias;
    return (volatile uint32_t *)address;
}

void core_patch_word(uint32_t address, uint32_t value)
{
    volatile uint32_t *word = writable(address);
    uint32_t cpsr = core_irq_save();
    *word = value;
    core_cache_sync((void *)address, (void *)(address + 4));
    core_irq_restore(cpsr);
}

static void patch_entry(const struct core_site_row *row, uint32_t stub)
{
    volatile uint32_t *word = writable(row->address);
    uint32_t cpsr = core_irq_save();
    word[1] = stub;
    word[0] = 0xe51ff004; /* ldr pc, [pc, #-4] */
    core_cache_sync((void *)row->address, (void *)(row->address + 8));
    core_irq_restore(cpsr);
}

/* Section descriptor access permission bits [11:10]; 0 means no privileged write. */
static void probe_mmu(void)
{
    uint32_t ttbr = core_read_ttbr() & ~0x3fffu;
    uint32_t descriptor = ((volatile uint32_t *)ttbr)[DRAM_BASE >> 20];
    core_mmu_probe.ttbr = ttbr;
    core_mmu_probe.descriptor = descriptor;
    core_mmu_probe.alias = (descriptor & 0xc00u) ? 0 : DRAM_UNCACHED_ALIAS;
}

void core_sites_init(void)
{
    probe_mmu();
    for (uint32_t i = 0; i < CORE_SITE_COUNT; i++) {
        const struct core_site_row *row = &core_sites[i];
        core_site_slots[i] = row->original;
        if (row->kind == CORE_SITE_ENTRY || row->kind == CORE_SITE_ENTRY_NORETURN)
            patch_entry(row, core_site_stubs[i]);
    }
}

static void patch_site(const struct core_site_row *row, uint32_t stub, int bound,
                       uint32_t word)
{
    switch (row->kind) {
    case CORE_SITE_CALL:
    case CORE_SITE_INSERT:
        core_patch_word(row->address, bound ? BL(row->address, stub) : row->word0);
        break;
    case CORE_SITE_VTABLE:
        core_patch_word(row->address, bound ? stub : row->word0);
        break;
    case CORE_SITE_WORD:
        core_patch_word(row->address, bound ? word : row->word0);
        break;
    default: /* entry sites stay written; the slot decides */
        break;
    }
}

static void publish(uint32_t site, struct hook_list *list)
{
    const struct core_site_row *row = row_of(site);
    struct hook_list *old = lists[site];
    uint32_t count = list ? list->count : 0;
    uint32_t target = row->original;
    if (count == 1)
        target = (uint32_t)list->hooks[0]->handler;
    else if (count > 1)
        target = (uint32_t)chains[row->argc & 15];

    lists[site] = list;
    if (row->kind != CORE_SITE_WORD)
        *slot_of(site) = target;
    if (row->kind == CORE_SITE_WORD)
        patch_site(row, 0, count != 0, count ? (uint32_t)list->hooks[0]->handler : 0);
    else if ((old == NULL) != (list == NULL))
        patch_site(row, stub_of(site), list != NULL, 0);
    if (old) {
        old->retired = retired;
        retired = old;
    }
}

int core_hook_bind(const struct core_hook *hook)
{
    if (!usable(hook->site))
        return CORE_HOOK_BAD_SITE;
    const struct core_site_row *row = row_of(hook->site);
    struct hook_list *old = lists[hook->site];
    uint32_t count = old ? old->count : 0;
    if (count && (!row->chain || hook->mode == CORE_HOOK_REPLACE ||
                  old->hooks[0]->mode == CORE_HOOK_REPLACE))
        return CORE_HOOK_CONFLICT;
    if (count && row->kind == CORE_SITE_WORD)
        return CORE_HOOK_CONFLICT;

    struct hook_list *list = Heap_AllocateZeroed(sizeof *list + (count + 1) * sizeof(void *));
    if (!list)
        return CORE_HOOK_NO_MEMORY;
    uint32_t at = 0;
    while (at < count && old->hooks[at]->priority <= hook->priority)
        at++;
    for (uint32_t i = 0; i < at; i++)
        list->hooks[i] = old->hooks[i];
    list->hooks[at] = hook;
    for (uint32_t i = at; i < count; i++)
        list->hooks[i + 1] = old->hooks[i];
    list->count = count + 1;
    publish(hook->site, list);
    return CORE_HOOK_OK;
}

int core_hook_unbind(const struct core_hook *hook)
{
    if (hook->site >= CORE_SITE_TOTAL)
        return CORE_HOOK_BAD_SITE;
    struct hook_list *old = lists[hook->site];
    uint32_t count = old ? old->count : 0;
    uint32_t at = 0;
    while (at < count && old->hooks[at] != hook)
        at++;
    if (at == count)
        return CORE_HOOK_NOT_BOUND;
    struct hook_list *list = NULL;
    if (count > 1) {
        list = Heap_AllocateZeroed(sizeof *list + (count - 1) * sizeof(void *));
        if (!list)
            return CORE_HOOK_NO_MEMORY;
        for (uint32_t i = 0, j = 0; i < count; i++)
            if (i != at)
                list->hooks[j++] = old->hooks[i];
        list->count = count - 1;
    }
    publish(hook->site, list);
    return CORE_HOOK_OK;
}

const struct core_hook *core_hook_find(uint32_t site, const void *handler)
{
    struct hook_list *list = site < CORE_SITE_TOTAL ? lists[site] : NULL;
    for (uint32_t i = 0; list && i < list->count; i++)
        if (list->hooks[i]->handler == handler)
            return list->hooks[i];
    return NULL;
}

void core_grace_tick(void)
{
    for (uint32_t i = 0; i < CORE_RAW_SITES; i++)
        if (raw_sites[i].state == RAW_RETIRED)
            raw_sites[i].state = RAW_FREE;
        else if (raw_sites[i].state == RAW_RETIRING)
            raw_sites[i].state = RAW_RETIRED;
    struct hook_list *list = retired;
    retired = NULL;
    while (list) {
        struct hook_list *following = list->retired;
        Heap_Free(list);
        list = following;
    }
}

/* Raw sites */

static int covers(uint32_t start, uint32_t words, uint32_t address)
{
    return address - start < 4 * words;
}

int core_raw_claim(uint32_t owner, uint32_t address, uint8_t kind, uint32_t expected,
                   uint8_t argc, uint32_t *site)
{
    if (address % 4 || address < OSOS_RUNTIME_BASE || address >= OSOS_NATIVE_HEAP_BASE ||
        argc > 4 || (kind != CORE_SITE_CALL && kind != CORE_SITE_VTABLE && kind != CORE_SITE_WORD) ||
        (kind == CORE_SITE_CALL && (expected & 0xff000000u) != 0xeb000000u))
        return CORE_RAW_BAD;
    for (uint32_t i = 0; i < CORE_SITE_COUNT; i++) {
        const struct core_site_row *row = &core_sites[i];
        uint32_t words = row->kind == CORE_SITE_ENTRY || row->kind == CORE_SITE_ENTRY_NORETURN ? 2 : 1;
        if (covers(row->address, words, address)) {
            *site = i;
            return CORE_RAW_OVERLAP;
        }
    }
    uint32_t free = CORE_RAW_SITES;
    for (uint32_t i = 0; i < CORE_RAW_SITES; i++) {
        if (raw_sites[i].state == RAW_FREE) {
            if (free == CORE_RAW_SITES)
                free = i;
        } else if (raw_sites[i].state == RAW_CLAIMED && raw_sites[i].row.address == address) {
            *site = CORE_SITE_TOTAL;
            return CORE_RAW_OVERLAP;
        }
    }
    if (*(volatile uint32_t *)address != expected)
        return CORE_RAW_MISMATCH;
    if (free == CORE_RAW_SITES)
        return CORE_RAW_FULL;

    struct raw_site *raw = &raw_sites[free];
    raw->row.address = address;
    raw->row.word0 = expected;
    raw->row.word1 = 0;
    raw->row.kind = kind;
    raw->row.chain = 0;
    raw->row.argc = argc;
    raw->row.original = expected;
    if (kind == CORE_SITE_CALL) {
        int32_t offset = (int32_t)(expected << 8) >> 6;
        raw->row.original = address + 8 + (uint32_t)offset;
    }
    raw->owner = owner;
    raw->state = RAW_CLAIMED;

    struct raw_stub *stub = &raw_stubs[free];
    for (uint32_t i = 0; i < 7; i++)
        stub->code[i] = raw_frame[i];
    if (argc < 4) {
        stub->code[0] = 0xe28f0018u | (uint32_t)argc << 12; /* add r<argc>, pc, #24: &root */
        stub->code[1] = 0xe59ff010u;                         /* ldr pc, slot */
    }
    stub->slot = raw->row.original;
    stub->root.site = (uint16_t)(CORE_SITE_COUNT + free);
    stub->root.index = 0xffff;
    core_cache_sync(stub, stub + 1);
    *site = CORE_SITE_COUNT + free;
    return CORE_RAW_OK;
}

void core_raw_release(uint32_t site)
{
    if (site < CORE_SITE_COUNT || site >= CORE_SITE_TOTAL)
        return;
    struct raw_site *raw = &raw_sites[site - CORE_SITE_COUNT];
    if (raw->state != RAW_CLAIMED)
        return;
    if (lists[site])
        publish(site, NULL);
    raw->state = RAW_RETIRING;
}

uint32_t core_raw_owner_of(uint32_t address)
{
    uint32_t start = (uint32_t)raw_stubs;
    if (address - start >= sizeof raw_stubs)
        return 0;
    return raw_sites[(address - start) / sizeof raw_stubs[0]].owner;
}
