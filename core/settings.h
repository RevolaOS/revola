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

uint32_t core_settings_owner(const char *id);
void core_settings_load(void);
/* Bytes copied, 0 when the record is absent. Longer records are truncated. */
uint32_t core_settings_get(uint32_t owner, uint16_t key, void *out, uint32_t capacity);
int core_settings_set(uint32_t owner, uint16_t key, uint8_t type, const void *data,
                      uint32_t size);
void core_settings_reset_owner(uint32_t owner);
/* Writes the store if it changed since the last write. */
void core_settings_flush(void);
/* Binds the flush points: before light sleep, after Apple's save-all, and the
 * wipe on Reset All Settings. */
void core_settings_bind(void);

int32_t core_settings_get_int(uint32_t owner, uint16_t key, int32_t fallback);
int core_settings_set_int(uint32_t owner, uint16_t key, int32_t value);

#endif
