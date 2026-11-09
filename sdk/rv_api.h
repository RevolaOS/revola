/* SPDX-License-Identifier: GPL-3.0-only */

/* The core services an extension may call. The table is append-only: a module
 * built against an older minor version finds every function it knows at the same
 * offset, and checks `size` before using a newer one. The major version changes
 * when an existing entry changes meaning. Apple functions are not here; modules
 * call them directly through the symbols header and linker script of the OSOS
 * build named by RV_FIRMWARE_ID. */

#ifndef RV_API_H
#define RV_API_H
#include <stdint.h>

#define RV_API_MAJOR 1
#define RV_API_MINOR 0
#define RV_API_VERSION ((RV_API_MAJOR << 16) | RV_API_MINOR)

/* The loader's record of the extension; passed back to every call. */
typedef struct rv_extension_record *rv_handle;

enum rv_result {
    RV_OK,
    RV_CONFLICT,   /* a replace handler meets another handler, or the site is exclusive */
    RV_NO_MEMORY,
    RV_NOT_BOUND,
    RV_BAD_SITE,   /* no site with that id in this core */
    RV_BAD_ARGUMENT,
    RV_NOT_FOUND,
};

enum rv_hook_mode {
    RV_HOOK_FILTER,  /* receives `next`; may continue through api->next or short-circuit */
    RV_HOOK_REPLACE, /* exclusive; never continues */
};

/* A handler receives the Apple arguments followed by `next` (in the register
 * after the last argument for sites with at most three register arguments, as
 * the first stack argument for four), then any arguments Apple passes on the
 * stack. Site ids are RV_SITE_* from rv_sites.h. The struct must live as long
 * as the binding. */
struct rv_hook {
    uint32_t site;
    uint8_t mode;
    uint8_t reserved;
    int16_t priority; /* lower runs first */
    void *handler;
};

enum rv_setting_type {
    RV_SETTING_BOOL = 1,
    RV_SETTING_INT = 2,
    RV_SETTING_STRING = 3,
    RV_SETTING_BLOB = 4,
};

/* Key 0 is the enabled flag the core keeps for every extension. */
#define RV_KEY_ENABLED 0

/* A declared setting: the core applies `fallback` when the store has no record,
 * clears the record on Reset All Settings, and renders it from `label`. */
struct rv_setting {
    uint16_t key;
    uint8_t type;
    uint8_t reserved;
    int32_t fallback;
    int32_t minimum;
    int32_t maximum;
    const char *label;
};

/* A submenu under a native root, shared with every other extension that
 * declares the same id. Ids are stable names such as "settings.sound"; the
 * parent is a root ("settings") or another container. */
struct rv_container {
    const char *id;
    const char *parent;
    const char *label;
};

/* Returned by the module entry; everything it points to lives as long as the
 * module is loaded. */
struct rv_extension {
    uint32_t size; /* sizeof(struct rv_extension) when built */
    const struct rv_hook *hooks;
    uint32_t hook_count;
    const struct rv_setting *settings;
    uint32_t setting_count;
    const struct rv_container *containers;
    uint32_t container_count;
    void (*unload)(void); /* before the hooks are unbound; may be NULL */
};

struct rv_api {
    uint32_t size;    /* bytes of this table */
    uint32_t version; /* RV_API_VERSION of the core */

    /* Bind beyond the descriptor; unbind only what hook_bind bound (the
     * descriptor's hooks are unbound by the loader). */
    int (*hook_bind)(rv_handle self, const struct rv_hook *hook);
    int (*hook_unbind)(rv_handle self, const struct rv_hook *hook);
    /* Continue a filter chain; unused trailing arguments are 0. */
    uint32_t (*next)(const void *next, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                     uint32_t e, uint32_t f);

    /* Bytes copied, 0 when absent. Values are at most 255 bytes. */
    uint32_t (*setting_get)(rv_handle self, uint16_t key, void *out, uint32_t capacity);
    int (*setting_set)(rv_handle self, uint16_t key, uint8_t type, const void *data,
                       uint32_t bytes);
    void (*settings_flush)(void);

    /* Zeroed memory from the Apple heap, counted against the extension. */
    void *(*alloc)(rv_handle self, uint32_t bytes);
    void (*free)(rv_handle self, void *block);

    /* Manual-trigger modules by their index in the manifest. */
    int (*module_load)(rv_handle self, uint32_t module);
    void (*module_unload)(rv_handle self, uint32_t module);

    int (*container_declare)(rv_handle self, const struct rv_container *container);
    void (*container_release)(rv_handle self, const struct rv_container *container);

    /* Kept for the emulator; nothing on the device shows it. */
    void (*log)(rv_handle self, const char *text);
};

/* Every module exports this. The entry of the main module returns the
 * descriptor; on-demand modules may return NULL. */
typedef const struct rv_extension *(*rv_entry_fn)(const struct rv_api *api, rv_handle self);

#endif
