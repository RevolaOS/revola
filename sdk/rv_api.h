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
#define RV_API_MINOR 6
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
 * stack. Site ids are RV_SITE_* from rv_sites.h, or RV_RAW_SITE(n) for the n-th
 * [[site]] of the extension's manifest. The struct must live as long as the
 * binding. */
#define RV_RAW_SITE(index) (0xffffff00u | (index))
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

/* Keys the core keeps in every extension's settings: the enabled flag, and which
 * of its Main and Music rows are shown (bit n is entry n). */
#define RV_KEY_ENABLED 0
#define RV_KEY_MENU_VISIBILITY 0xffff

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

/* A submenu of a menu, shared with every other extension that declares the
 * same id. Ids are stable names such as "settings.sound"; the parent is a menu
 * name (see rv_entry) or another container's id. The core shows the submenu
 * while at least one entry names it. */
struct rv_container {
    const char *id;
    const char *parent;
    const char *label;
};

enum rv_row_kind {
    RV_ROW_PUSH,   /* opens `screen` */
    RV_ROW_TOGGLE, /* flips the bool setting `key`; the row shows On or Off */
    RV_ROW_ACTION, /* calls `action` */
    RV_ROW_STOCK,  /* a stock Settings row (an RV_SETTINGS_* row of rv_menus.h in `stock`)
                      moved here with its stock behaviour; only inside a container under
                      Settings */
};

/* One row of a list screen or an entry in a menu. `action` receives the
 * Apple controller the row was chosen on; context menu rows get the list or Now
 * Playing controller. */
struct rv_row {
    const char *label;
    uint8_t kind;
    uint8_t reserved;
    uint16_t key;
    const char *screen;
    int (*action)(void *controller);
    uint32_t stock;
};

enum rv_screen_kind {
    RV_SCREEN_LIST,         /* rows from `list` */
    RV_SCREEN_CHOICE,       /* one checkmark among `choice.labels`; stores the index in
                               the int setting `choice.key` and pops */
    RV_SCREEN_SLIDER,       /* a bar over `choice.labels`; wheel and Previous/Next change
                               the int setting `choice.key` */
    RV_SCREEN_TEXT,         /* the paragraphs of `text` */
    RV_SCREEN_CANVAS,       /* the extension draws and reads input */
    RV_SCREEN_RUNTIME_LIST, /* rows from callbacks, for data known only at run time */
    RV_SCREEN_MULTI_CHOICE, /* a checkmark per label; bit n of the int setting `choice.key`
                               is label n (at most 32), Select flips it */
};

/* How a list screen (list, choice, multi-choice, runtime list) is laid out. */
enum rv_layout {
    RV_LAYOUT_FULL,  /* the list over the whole screen */
    RV_LAYOUT_SPLIT, /* the list on the left, the Settings preview pane on the right */
};

enum rv_input_kind { RV_INPUT_WHEEL, RV_INPUT_PRESS, RV_INPUT_RELEASE };
enum rv_key { RV_KEY_CENTER, RV_KEY_MENU, RV_KEY_PREVIOUS, RV_KEY_NEXT, RV_KEY_PLAY };

struct rv_input {
    uint8_t kind;
    uint8_t key;   /* rv_key for presses and releases */
    int16_t delta; /* wheel steps, signed */
};

/* A screen the core registers for the extension. `name` is "<id>.<screen>",
 * the name a module's manifest trigger and RV_ROW_PUSH use. Labels, text and
 * bitmaps are read from the module while the screen is open; nothing is copied. */
struct rv_screen {
    const char *name;
    const char *title;
    uint8_t kind;
    uint8_t layout; /* rv_layout, list kinds only */
    uint8_t reserved[2];
    union {
        struct {
            const struct rv_row *rows;
            uint32_t count;
        } list;
        struct {
            uint16_t key;
            uint16_t reserved;
            const char *const *labels;
            uint32_t count;
        } choice;
        struct {
            const char *const *paragraphs;
            uint32_t count;
        } text;
        struct {
            /* Draw into `bounds` {top, left, bottom, right} with the Apple graphics
             * context; input returns 1 when consumed, 0 to leave the key to Apple. */
            void (*draw)(void *context, const int32_t *bounds);
            int (*input)(const struct rv_input *input);
        } canvas;
        struct {
            uint32_t (*count)(void);
            /* Row `index`; a detail text goes to *detail (NULL for none). */
            const char *(*row)(uint32_t index, const char **detail);
            int (*select)(uint32_t index); /* returns 1 to pop the screen */
            uint8_t stacked;               /* detail under the label instead of inline */
        } runtime;
    };
};

/* A row the extension adds to a menu. `in` names the menu: "main", "music",
 * "extras", "settings", the menu a long press of Select opens on Now Playing
 * "nowplaying" or on a song, album or artist row "song", "album" and "artist",
 * or a container id such as "settings.sound". `after` is a stock row of that menu from
 * rv_menus.h (RV_SETTINGS_LEGAL), 0 for the end. Under Settings the preview pane
 * shows the label, `caption` as the line below and `bitmap` as the icon while
 * the row is highlighted; `bitmap` is a BMap resource (BMap_* in the symbols
 * header). Either may be NULL, and the pane then shows nothing in its place.
 * Rows under Main and Music appear in Settings > Main Menu and Music Menu,
 * `visible` is their default there. */
struct rv_entry {
    const char *in;
    uint8_t visible;
    uint8_t reserved[3];
    uint32_t after;
    struct rv_row row;
    const char *caption;
    const void *bitmap;
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
    const struct rv_screen *screens;
    uint32_t screen_count;
    const struct rv_entry *entries;
    uint32_t entry_count;
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

    /* Screens by name. push opens one above the screen of `controller`, the
     * one an action received (its module loads first); pop closes the top
     * screen; refresh redraws rows whose text changed; invalidate redraws an
     * open canvas. */
    int (*ui_push)(rv_handle self, const char *screen, void *controller);
    void (*ui_pop)(rv_handle self);
    void (*ui_refresh)(rv_handle self, const char *screen);
    void (*ui_invalidate)(rv_handle self);
};

/* Every module exports this. The entry of the main module returns the
 * descriptor; on-demand modules may return NULL. */
typedef const struct rv_extension *(*rv_entry_fn)(const struct rv_api *api, rv_handle self);

#endif
