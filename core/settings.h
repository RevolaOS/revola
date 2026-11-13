/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_SETTINGS_H
#define CORE_SETTINGS_H
#include <stdint.h>

/* One store for the core and every extension, namespaced by the FNV-1a hash of the
 * owner's id. Values are small (at most 255 bytes); anything larger belongs in the
 * owner's own file. */
enum core_setting_type {
    CORE_SETTING_BOOL = 1,
    CORE_SETTING_INT = 2,
    CORE_SETTING_STRING = 3,
    CORE_SETTING_BLOB = 4,
};

#define CORE_SETTINGS_OWNER_CORE 0xdd5e607eu /* core_settings_owner("core") */

/* Keys of the core's own settings. */
enum core_setting_key {
    CORE_KEY_CACHE_BUDGET = 1,    /* bool: size the block cache for the library and disk */
    CORE_KEY_READAHEAD_FLOOR = 2, /* bool: smaller read-ahead floor when a game must fit */
    CORE_KEY_POOL_SLOTS = 3,      /* bool: round fixed-pool slots to 8 bytes */
    CORE_KEY_GAME_BLOCKS = 4,     /* bool: grow the cache for a game and shrink after */
    CORE_KEY_POSITION_SCAN = 5,   /* bool: find list positions by scanning, no address index */
    CORE_KEY_ARTWORK_SHARE = 6,   /* bool: share identical artwork chains, drop duplicate images */
    CORE_KEY_CRASHED = 7,         /* int: owner of the extension the last crash disabled */
    CORE_KEY_OPTIMIZE = 8,        /* int: Settings > Extensions > Optimize, CORE_OPTIMIZE_ */
};

/* Whether optional modules and assets are kept with main: Auto decides by the
 * disk, Performance keeps them, Memory leaves them on disk. */
enum { CORE_OPTIMIZE_AUTO, CORE_OPTIMIZE_PERFORMANCE, CORE_OPTIMIZE_MEMORY };

uint32_t core_settings_owner(const char *id);
void core_settings_load(void);
/* Bytes copied, 0 when the record is absent. Longer records are truncated. */
uint32_t core_settings_get(uint32_t owner, uint16_t key, void *out, uint32_t capacity);
int core_settings_set(uint32_t owner, uint16_t key, uint8_t type, const void *data,
                      uint32_t size);
void core_settings_reset_owner(uint32_t owner);
/* Writes the store if it changed since the last write. */
void core_settings_flush(void);
/* Grace tick: writes a dirty store once the disk is powered anyway. */
void core_settings_tick(void);
/* Binds the flush points: before light sleep, after Apple's save-all, and the
 * wipe on Reset All Settings. */
void core_settings_bind(void);

int32_t core_settings_get_int(uint32_t owner, uint16_t key, int32_t fallback);
int core_settings_set_int(uint32_t owner, uint16_t key, int32_t value);

#endif
