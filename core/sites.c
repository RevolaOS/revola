/* SPDX-License-Identifier: GPL-3.0-only */

/* Site table, handler lists and the chain walker. One pointer slot per site: the
 * handler itself when one is bound, core_chain_N when several are, so hot sites pay
 * one indirect jump. Handler lists are published by a single aligned store and the
 * retired list is freed at the next grace point; nothing is counted or locked while
 * a handler runs. */

#include "osos-2.0.5.h"
#include "sites.h"
#include <stddef.h>

#define BL(address, callee) (0xeb000000u | ((((callee) - (address) - 8u) >> 2) & 0xffffffu))
#define REPLAY(name) ((uint32_t)core_replay_##name)

#define SITE(name, address, kind, original, word0, word1, chain, argc, copy)                     \
    extern char core_replay_##name[];
#include "sites.def"
#undef SITE

const struct core_site_row core_sites[CORE_SITE_COUNT] = {
#define SITE(name, address, kind, original, word0, word1, chain, argc, copy)                     \
    {address, (uint32_t)(original), word0, word1, CORE_SITE_##kind, chain, argc, 0},
#include "sites.def"
#undef SITE
};
const uint32_t core_site_count = CORE_SITE_COUNT;

struct hook_list {
    struct hook_list *retired; /* pending-free chain */
    uint32_t count;
    const struct core_hook *hooks[];
};

static struct hook_list *lists[CORE_SITE_COUNT];
static struct hook_list *retired;
struct core_mmu_probe core_mmu_probe;

typedef uint32_t (*fn0)(const struct core_next *);
typedef uint32_t (*fn1)(uint32_t, const struct core_next *);
typedef uint32_t (*fn2)(uint32_t, uint32_t, const struct core_next *);
typedef uint32_t (*fn3)(uint32_t, uint32_t, uint32_t, const struct core_next *);
typedef uint32_t (*fn4)(uint32_t, uint32_t, uint32_t, uint32_t, const struct core_next *);

static uint32_t call(void *fn, uint8_t argc, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                     const struct core_next *next)
{
    switch (argc) {
    case 0:
        return ((fn0)fn)(next);
    case 1:
        return ((fn1)fn)(a, next);
    case 2:
        return ((fn2)fn)(a, b, next);
    case 3:
        return ((fn3)fn)(a, b, c, next);
    default:
        return ((fn4)fn)(a, b, c, d, next);
    }
}

uint32_t core_next_call(const struct core_next *next, uint32_t a, uint32_t b, uint32_t c,
                        uint32_t d)
{
    const struct core_site_row *row = &core_sites[next->site];
    struct hook_list *list = lists[next->site];
    if (list && next->index < list->count) {
        struct core_next following = {next->site, next->index + 1};
        return call(list->hooks[next->index]->handler, row->argc, a, b, c, d, &following);
    }
    typedef uint32_t (*original_fn)(uint32_t, uint32_t, uint32_t, uint32_t);
    return ((original_fn)row->original)(a, b, c, d);
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
                       const struct core_next *n)
{
    return chain(n, a, b, c, d);
}
static void *const chains[] = {chain0, chain1, chain2, chain3, chain4};

void core_patch_word(uint32_t address, uint32_t value)
{
    volatile uint32_t *word = (volatile uint32_t *)(address | core_mmu_probe.alias);
    uint32_t cpsr = core_irq_save();
    *word = value;
    core_cache_sync((void *)address, (void *)(address + 4));
    core_irq_restore(cpsr);
}

static void patch_entry(const struct core_site_row *row, uint32_t stub)
{
    volatile uint32_t *word = (volatile uint32_t *)(row->address | core_mmu_probe.alias);
    uint32_t cpsr = core_irq_save();
    word[1] = stub;
    word[0] = 0xe51ff004; /* ldr pc, [pc, #-4] */
    core_cache_sync((void *)row->address, (void *)(row->address + 8));
    core_irq_restore(cpsr);
}

extern const uint32_t core_site_stubs[CORE_SITE_COUNT];

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
    const struct core_site_row *row = &core_sites[site];
    struct hook_list *old = lists[site];
    uint32_t count = list ? list->count : 0;
    uint32_t target = row->original;
    if (count == 1)
        target = (uint32_t)list->hooks[0]->handler;
    else if (count > 1)
        target = (uint32_t)chains[row->argc];

    lists[site] = list;
    if (row->kind != CORE_SITE_WORD)
        core_site_slots[site] = target;
    if (row->kind == CORE_SITE_WORD)
        patch_site(row, 0, count != 0, count ? (uint32_t)list->hooks[0]->handler : 0);
    else if ((old == NULL) != (list == NULL))
        patch_site(row, core_site_stubs[site], list != NULL, 0);
    if (old) {
        old->retired = retired;
        retired = old;
    }
}

int core_hook_bind(const struct core_hook *hook)
{
    if (hook->site >= CORE_SITE_COUNT)
        return CORE_HOOK_BAD_SITE;
    const struct core_site_row *row = &core_sites[hook->site];
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
    if (hook->site >= CORE_SITE_COUNT)
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

void core_grace_tick(void)
{
    struct hook_list *list = retired;
    retired = NULL;
    while (list) {
        struct hook_list *following = list->retired;
        Heap_Free(list);
        list = following;
    }
}
