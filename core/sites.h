/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_SITES_H
#define CORE_SITES_H
#include <stdint.h>

/* A handler is called with the Apple arguments followed by `next`: in r[argc] when
 * the site takes at most three register arguments, as the first stack argument when
 * it takes four. A filter may call core_next_call to continue; a replace handler
 * never does and excludes every other handler on the site. */
enum core_site_kind {
    CORE_SITE_CALL,
    CORE_SITE_VTABLE,
    CORE_SITE_ENTRY,
    CORE_SITE_ENTRY_NORETURN,
    CORE_SITE_WORD,
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
#define SITE(name, address, kind, original, word0, word1, chain, argc) CORE_SITE_##name,
#include "sites.def"
#undef SITE
    CORE_SITE_COUNT
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

void core_sites_init(void);
int core_hook_bind(const struct core_hook *hook);
int core_hook_unbind(const struct core_hook *hook);
uint32_t core_next_call(const struct core_next *next, uint32_t a, uint32_t b, uint32_t c,
                        uint32_t d);
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
