/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_SITES_H
#define CORE_SITES_H
#include <stdint.h>

/* A handler is called with the Apple arguments followed by `next`: in r[argc] when
 * the site takes at most three register arguments, as the first stack argument when
 * it takes four, followed by any arguments Apple passed on the stack. A filter may
 * call core_next_call to continue; a replace handler never does and excludes every
 * other handler on the site. */
enum core_site_kind {
    CORE_SITE_CALL,
    CORE_SITE_VTABLE,
    CORE_SITE_ENTRY,
    CORE_SITE_ENTRY_NORETURN,
    CORE_SITE_WORD,
    CORE_SITE_INSERT, /* BL over an ordinary instruction: r0-r3, r12 preserved, no result */
};

enum core_hook_mode { CORE_HOOK_FILTER, CORE_HOOK_REPLACE };

enum core_hook_result {
    CORE_HOOK_OK,
    CORE_HOOK_CONFLICT, /* replace against another handler, or a second one on an exclusive site */
    CORE_HOOK_NO_MEMORY,
    CORE_HOOK_NOT_BOUND,
    CORE_HOOK_BAD_SITE,
};

enum core_site {
#define SITE(name, address, kind, original, word0, word1, chain, argc, copy) CORE_SITE_##name,
#include "hooks/sites.def"
#undef SITE
    CORE_SITE_COUNT
};

/* Sites an extension declares in its manifest for code no core row covers. They
 * follow the core rows in site index order, are always exclusive, and only the
 * kinds a single aligned store can patch are allowed: CALL, VTABLE and WORD. */
#define CORE_RAW_SITES 8
#define CORE_SITE_TOTAL (CORE_SITE_COUNT + CORE_RAW_SITES)
_Static_assert(CORE_SITE_TOTAL <= 256, "site index is a byte");

enum core_raw_result {
    CORE_RAW_OK,
    CORE_RAW_MISMATCH, /* the word at the address is not the declared one */
    CORE_RAW_OVERLAP,  /* a core site or another raw site covers the address */
    CORE_RAW_FULL,
    CORE_RAW_BAD,      /* kind, argument count or address not allowed */
};

struct core_site_row {
    uint32_t address;
    uint32_t original;
    uint32_t word0;
    uint32_t word1;
    uint8_t kind;
    uint8_t chain;
    uint8_t argc;
    uint8_t pad;
};

/* Owned by whoever binds it; the core keeps the pointer until unbind. For WORD
 * sites `handler` is the replacement word. */
struct core_hook {
    uint8_t site;
    uint8_t mode;
    int16_t priority; /* lower runs first */
    void *handler;
};

/* Continuation handed to a handler. Opaque to callers. */
struct core_next {
    uint16_t site;
    uint16_t index;
};

extern const struct core_site_row core_sites[CORE_SITE_COUNT];
extern const uint32_t core_site_count;
extern uint32_t core_site_slots[CORE_SITE_COUNT];
/* FNV-1a of each row name; the id extensions bind by. */
extern const uint32_t core_site_ids[CORE_SITE_COUNT];
/* Each site's stub, 0 for word sites: what a patched site branches to. */
extern const uint32_t core_site_stubs[CORE_SITE_COUNT];

/* Claims a raw site for `owner`. On success *site is its index; on CORE_RAW_OVERLAP
 * *site is the covering core site's index, or CORE_SITE_TOTAL for a raw one. */
int core_raw_claim(uint32_t owner, uint32_t address, uint8_t kind, uint32_t expected,
                   uint8_t argc, uint32_t *site);
/* Unbinds anything left on the site and frees it two grace ticks later, when no
 * task can still be inside its stub. */
void core_raw_release(uint32_t site);
/* The argc column of the core row with id `id` (core_site_ids), -1 when there is
 * none or its kind takes no filter. */
int core_site_argc(uint32_t id);
/* Owner of the raw site whose stub contains `address`, 0 when none. */
uint32_t core_raw_owner_of(uint32_t address);

void core_sites_init(void);
int core_hook_bind(const struct core_hook *hook);
/* Never fails for lack of memory: once it returns OK no new call reaches the
 * hook, and its storage may be retired. */
int core_hook_unbind(const struct core_hook *hook);
/* The bound hook on `site` with this handler, or NULL. */
const struct core_hook *core_hook_find(uint32_t site, const void *handler);
/* Continues a chain: the four register arguments, Apple's stack arguments and a
 * PUSH register when the site has them. */
uint32_t core_next_call7(const struct core_next *next, uint32_t a, uint32_t b, uint32_t c,
                         uint32_t d, uint32_t e, uint32_t f, uint32_t g);
#define core_next_call6(next, a, b, c, d, e, f) core_next_call7(next, a, b, c, d, e, f, 0)
#define core_next_call(next, a, b, c, d) core_next_call6(next, a, b, c, d, 0, 0)
/* Frees handler arrays retired since the last call. Only at the grace point. */
void core_grace_tick(void);

uint32_t core_read_ttbr(void);
uint32_t core_irq_save(void);
void core_irq_restore(uint32_t cpsr);
void core_cache_sync(const void *start, const void *end);
void core_patch_word(uint32_t address, uint32_t value);

/* What the boot-time MMU probe saw: the translation table base and the section
 * descriptor covering OSOS. Read with the emulator, never by code. */
struct core_mmu_probe {
    uint32_t ttbr;
    uint32_t descriptor;
    uint32_t alias; /* 0, or DRAM_UNCACHED_ALIAS when the cached map is read-only */
};
extern struct core_mmu_probe core_mmu_probe;

#endif
