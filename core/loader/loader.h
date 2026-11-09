/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_LOADER_H
#define CORE_LOADER_H
#include "osos-2.0.5.h"
#include <stdint.h>

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
    CORE_EXTENSION_NO_DESCRIPTOR,
    CORE_EXTENSION_ID_COLLISION, /* its resource id block is held by another extension */
};

/* One per enabled extension found at boot or enabled since, heap-allocated. */
struct rv_extension_record {
    struct rv_extension_record *next;
    uint32_t owner;      /* FNV-1a of the manifest id; the settings owner */
    uint8_t *manifest;   /* manifest.bin as read; strings and module records live here */
    uint32_t manifest_bytes;
    char directory[33];  /* the directory entry as the scan returned it */
    uint8_t failure;
    uint8_t module_count;
    uint8_t pending_unload;
    uint32_t conflict_site; /* site id of the collided claim, 0 otherwise */
    uint32_t live;          /* bytes owned now: images plus api->alloc blocks */
    uint32_t peak;
    uint32_t hooks_bound;
    const struct rv_extension *descriptor;
    struct core_hook *hooks; /* core-owned copies of the descriptor's hooks */
    struct core_module *modules;
};

/* Reads the store state and loads every enabled extension. Runs once the disk
 * is readable, after the settings store loaded. */
void core_loader_boot(void);
/* Settings > Extensions: writes the enabled flag; loads now or unloads at the
 * next grace tick. `directory` is the entry name on disk. */
int core_loader_set_enabled(const char *directory, int enabled);
/* Grace point: frees retired images, unloads what was disabled, ages idle
 * screen modules. */
void core_loader_tick(void);
/* Screen-trigger modules: called when a declared screen is pushed or popped. */
void core_loader_screen_pushed(const char *screen);
void core_loader_screen_popped(const char *screen);
/* Owner of the extension whose image contains `address`, 0 when none. */
uint32_t core_loader_owner_of(uint32_t address);
/* Resident bytes of every loaded extension together. */
uint32_t core_loader_total_bytes(void);

/* One installed extension as Settings > Extensions lists it. */
struct core_installed {
    char directory[33];
    char name[23];
    uint32_t owner;
};
/* Scans the directory and reads each manifest's name; entries found. */
uint32_t core_loader_installed(struct core_installed *out, uint32_t capacity);
/* The record of an enabled extension by owner, NULL when disabled or absent. */
struct rv_extension_record *core_loader_find(uint32_t owner);

extern struct rv_extension_record *core_extensions;
extern uint8_t core_loader_core_only;
/* Owner disabled by the last crash, 0 when none; shown by Settings > Extensions. */
extern uint32_t core_loader_crashed;

#endif
