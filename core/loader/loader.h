/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_LOADER_H
#define CORE_LOADER_H
#include "hooks/sites.h"
#include "osos-2.0.5.h"
#include <stdint.h>

struct rvx_entry;

/* Owner of the extension whose code was running at the last panic; survives a
 * warm reset so the next boot disables it. Below the companion's context word
 * at the top of IRAM; neither Apple's warm-boot descriptor (IRAM_BASE + 0x3ff00
 * to +0x3ff2f) nor the bootloader (code and stack low in IRAM1) use it. TODO:
 * confirm retention across a hardware reset. */
#define CORE_CRASH_SCRATCH (*(volatile uint32_t *)(IRAM_BASE + 0x3fff8))

/* Why an enabled extension is not running. */
enum core_extension_failure {
    CORE_EXTENSION_OK,
    CORE_EXTENSION_BAD_MANIFEST,
    CORE_EXTENSION_BAD_MODULE,  /* missing file, wrong format, API major or firmware */
    CORE_EXTENSION_NO_MEMORY,
    CORE_EXTENSION_CONFLICT,    /* a hook claim collided; conflict_site names the site */
    CORE_EXTENSION_BAD_SITE,    /* the module binds a site this core does not have */
    CORE_EXTENSION_BAD_DESCRIPTOR, /* none returned, or a declaration failed its check */
    CORE_EXTENSION_TOO_NEW,     /* the manifest asks for a newer API minor than this core's */
    CORE_EXTENSION_SITE_MISMATCH, /* a raw site's word differs; conflict_site names it */
};

/* One per enabled extension found at boot or enabled since, heap-allocated. */
struct rv_extension_record {
    struct rv_extension_record *next;
    uint32_t owner;      /* FNV-1a of the manifest id; the settings owner */
    uint8_t *manifest;   /* the manifest as read; strings and module records live here */
    uint32_t manifest_bytes;
    char file[33];       /* the file name as the scan returned it, without .rvx */
    uint8_t failure;
    uint8_t module_count;
    uint8_t pending_unload;
    uint32_t conflict_site; /* site id of the collided claim, 0 otherwise */
    uint8_t raw_count;      /* raw sites claimed for the main module */
    uint8_t raw_sites[CORE_RAW_SITES]; /* their site indices, RV_RAW_SITE(n) order */
    uint32_t live;          /* heap held for it now: its record, images, api->alloc blocks,
                               the core's records of its UI and Apple's copies of them */
    uint32_t peak;
    uint32_t hooks_bound;
    const struct rv_descriptor *descriptor;
    struct core_hook *hooks; /* core-owned copies of the descriptor's hooks */
    struct rv_screen *setting_screens; /* screens the declared settings open, or NULL */
    struct core_module *modules;
    /* While the main module is loaded: resident_count catalog entries with
     * offsets into this block, then their payloads as stored; NULL for none. */
    struct rvx_entry *resident;
    uint32_t resident_count;
    uint32_t entry_count; /* catalog entries in the file */
    uint32_t body_hash; /* the header's, read when the main module loaded */
};

/* Reads the store state and loads every enabled extension. Runs once the disk
 * is readable, after the settings store loaded. */
void core_loader_boot(void);
/* Settings > Extensions: writes the enabled flag; loads now or unloads at the
 * next grace tick. `file` is the name on disk without .rvx. */
int core_loader_set_enabled(const char *file, int enabled);
/* Grace point: frees retired images, unloads what was disabled, ages idle
 * screen modules. */
void core_loader_tick(void);
/* Screen-trigger modules: called when a declared screen is pushed or popped. */
void core_loader_screen_pushed(const char *screen);
void core_loader_screen_popped(const char *screen);
/* Owner of the extension whose image or raw site stub contains `address`, 0
 * when none. */
uint32_t core_loader_owner_of(uint32_t address);
/* Resident bytes of every loaded extension together. */
uint32_t core_loader_total_bytes(void);

/* One installed extension as Settings > Extensions lists it. */
struct core_installed {
    char file[33];
    char name[23];
    uint32_t owner;
    uint8_t duplicate; /* another file with this id loads instead */
};
/* Scans the extensions and reads each manifest's name into a heap list, one
 * entry per readable manifest; the entries, 0 with `*out` NULL when the heap
 * refuses. Free with core_heap_free(NULL, *out, count * sizeof **out). */
uint32_t core_loader_installed(struct core_installed **out);

/* Manifests: an installed extension's read into `buffer` (CORE_MANIFEST_MAX
 * bytes), 0 when unreadable; field 0 to 5 of one (id, name, description,
 * version, author, license); its flags (bit 0 enabled, then
 * CORE_MANIFEST_STABILITY). */
#define CORE_MANIFEST_MAX 1024
#define CORE_MANIFEST_STABILITY(flags) ((flags) >> 1 & 3) /* 0 unset, alpha, beta, stable */
uint32_t core_loader_manifest(const char *file, uint8_t *buffer);
const char *core_loader_manifest_text(const uint8_t *manifest, uint32_t field);
uint32_t core_loader_manifest_flags(const uint8_t *manifest);

/* The resident asset `name` (FNV-1a) of `record`: its payload as stored in RAM
 * and *entry its catalog entry, or NULL. */
const void *core_loader_resident(const struct rv_extension_record *record, uint32_t name,
                                 const struct rvx_entry **entry);
/* The asset `name` decoded into a block counted to `record`, *bytes its size and
 * *kind its RVX_ kind; NULL when missing, damaged or out of memory. Free it with
 * core_loader_retire. */
void *core_loader_asset(struct rv_extension_record *record, uint32_t name, uint32_t *bytes,
                        uint8_t *kind);
/* Zeroed memory in a loader block of the core, for core_loader_retire. */
void *core_loader_block(uint32_t bytes);
/* Frees a loader block two grace ticks from now, when Apple can no longer be
 * drawing from it. */
void core_loader_retire(void *block);
/* Frees a block of core_loader_asset now. */
void core_loader_free(struct rv_extension_record *record, void *block);
/* The record of an enabled extension by owner, NULL when disabled or absent. */
struct rv_extension_record *core_loader_find(uint32_t owner);

/* Heap the core holds for itself: its loader blocks, its own records and
 * Apple's copies of its resources. What exists only while an extension is
 * loaded is counted to that extension's live bytes instead. */
extern uint32_t core_heap_bytes;
/* Counts `bytes` (negative to give back) to `owner`, or to the core for NULL. */
void core_loader_charge(struct rv_extension_record *owner, int32_t bytes);
/* What Apple's heap takes for a request of `bytes`: rounded to 4, an 8-byte
 * header, rounded to 8, at least 16. */
uint32_t core_heap_cost(uint32_t bytes);
/* Zeroed heap counted to `owner` (the core for NULL); free with the same
 * owner and size, or move the count when the block changes hands. */
void *core_heap_allocate(struct rv_extension_record *owner, uint32_t bytes);
void core_heap_free(struct rv_extension_record *owner, void *memory, uint32_t bytes);
void core_heap_move(struct rv_extension_record *from, struct rv_extension_record *to,
                    uint32_t bytes);

extern struct rv_extension_record *core_extensions;
extern uint8_t core_loader_core_only;
/* Owner disabled by the last crash, 0 when none; shown by Settings > Extensions. */
extern uint32_t core_loader_crashed;

#endif
