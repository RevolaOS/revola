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
#define RV_API_MINOR 11
#define RV_API_VERSION ((RV_API_MAJOR << 16) | RV_API_MINOR)

/* Compiles a function as ARM. Needed for DSP instructions that exist only in ARM
 * state and for calls into Apple routines that return with `mov pc, lr`; the
 * call must not become a tail call, or the routine returns to the Thumb caller in
 * ARM state. */
#define RV_ARM __attribute__((target("arm"), noinline, optimize("no-optimize-sibling-calls")))

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

/* How an RV_ROW_SETTING row shows an int setting with `labels`. */
enum rv_setting_style {
    RV_STYLE_NONE,   /* no row of its own */
    RV_STYLE_CHOICE, /* a list of the labels; the value is the chosen index */
    RV_STYLE_SLIDER, /* a bar over the labels; the value is the position */
    RV_STYLE_MULTI,  /* a checkmark per label; bit n of the value is label n */
};

/* A declared setting: the core applies `fallback` when the store has no record,
 * clears the record on Reset All Settings, and renders it from `label`. An
 * RV_ROW_SETTING row shows a bool as On or Off and flips it; an int with
 * `labels` shows the chosen label and opens a screen of `style` titled `label`. */
struct rv_setting {
    uint16_t key;
    uint8_t type;
    uint8_t style; /* rv_setting_style */
    int32_t fallback;
    int32_t minimum;
    int32_t maximum;
    const char *label;
    const char *const *labels; /* one per value; at most 32 for RV_STYLE_MULTI */
    uint32_t label_count;
};

/* A submenu of a menu, shared with every other extension that declares the
 * same id. Ids are stable names such as "settings.sound"; the parent is a menu
 * name (see rv_entry) or another container's id. The first declaration fixes
 * the parent and the label. The submenu's row appears in its parent while an
 * entry that names the container shows, and goes with the last one. */
struct rv_container {
    const char *id;
    const char *parent;
    const char *label;
};

enum rv_row_kind {
    RV_ROW_PUSH,   /* opens `screen` */
    RV_ROW_TOGGLE, /* flips the bool setting `key`; the row shows On or Off */
    RV_ROW_ACTION, /* calls `action` */
    RV_ROW_STOCK,  /* a stock Settings row (an RV_SETTINGS_* row of rv_menus.h in `stock`,
                      any but Legal) moved here with its stock behaviour; only inside a
                      container under Settings, and back in Settings when the entry goes */
    RV_ROW_SETTING, /* the declared setting `key`: shows its value; flips a bool, opens the
                       screen of an int with labels */
};

/* One row of a list screen or an entry in a menu. `text`, when set, gives the
 * label each time the row is drawn, for a value that changes; `label` still
 * names the row and is what a context menu shows. */
struct rv_row {
    const char *label;
    uint8_t kind;
    uint8_t reserved;
    uint16_t key;
    const char *screen;
    void (*action)(void);
    uint32_t stock;
    const char *(*text)(void);
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

/* How a list screen (list, choice, multi-choice, runtime list) is laid out.
 * Left at AUTO it follows the screen it opens from: split under Settings or a
 * split list, full width elsewhere, as the stock menus do. */
enum rv_layout {
    RV_LAYOUT_AUTO,
    RV_LAYOUT_FULL,  /* the list over the whole screen */
    RV_LAYOUT_SPLIT, /* the list on the left, the Settings preview pane on the right */
};

/* A bitmap asset of the extension by name, from a PNG in manifest.toml:
 * `.bitmap = RV_BITMAP("eq-icon")`. Menu bitmaps (an entry's .bitmap) are
 * assets with `menu = true`: 78 x 121 coverage masks the system draws in its own
 * colour, like the stock Settings icons; the core decodes only the one shown. */
struct rv_bitmap {
    const char *asset;
};
#define RV_BITMAP(name) (&(const struct rv_bitmap){(name)})

/* Drawing, for canvas screens. Coordinates count from the top left of the
 * canvas, below the title bar; the canvas is clipped for the extension. */
struct rv_rect {
    int16_t x, y, width, height;
};

typedef uint32_t rv_color; /* 0xAARRGGBB */

/* The stock colours of the list screens; a theme may change what a role is. */
enum rv_color_role {
    RV_COLOR_BACKGROUND,
    RV_COLOR_TEXT,
    RV_COLOR_HIGHLIGHT,      /* the selected row */
    RV_COLOR_HIGHLIGHT_TEXT, /* text on the selected row */
    RV_COLOR_DIM,            /* secondary text, such as a row's detail */
    RV_COLOR_PARAGRAPH,      /* the text of a text page */
    RV_COLOR_ROLE_COUNT,
};

/* The stock fonts: the title bar's, the text pages' and the preview pane's
 * large caption. */
enum rv_font { RV_FONT_TITLE, RV_FONT_TEXT, RV_FONT_LARGE, RV_FONT_COUNT };

/* The height of a row drawn by rv_gfx.row, as the stock list rows. */
#define RV_GFX_ROW_HEIGHT 25

/* What a canvas draws with. Valid inside the draw callback only. Append-only
 * like rv_api: check `size` before using a newer entry. */
struct rv_gfx {
    uint32_t size;
    rv_color (*color)(uint8_t role);
    void (*fill)(const struct rv_rect *rect, rv_color color);
    /* `y` is the top of the text line. */
    void (*text)(int x, int y, uint8_t font, rv_color color, const char *utf8);
    int (*measure)(uint8_t font, const char *utf8); /* width in pixels */
    int (*line_height)(uint8_t font);
    /* A bitmap asset with its top left at (x, y); nothing when it is missing.
     * The core decodes it on first use and keeps it until the canvas closes. */
    void (*bitmap)(int x, int y, const struct rv_bitmap *bitmap);
    /* A BMap the extension made or holds itself. */
    void (*bmap)(int x, int y, const void *bmap);
    /* `utf8` wrapped at spaces to `width` from (x, y), at most `lines` lines;
     * returns the y below the last. */
    int (*paragraph)(int x, int y, int width, uint8_t font, rv_color color, const char *utf8,
                     uint32_t lines);
    /* A list row RV_GFX_ROW_HEIGHT tall at `y` across `width`: the label left,
     * `value` (may be NULL) right, on the stock highlight when `highlighted`. */
    void (*row)(int y, int width, const char *label, const char *value, int highlighted);
    /* The top `rows` of a BMap at (x, y); a coverage mask draws in `tint`. */
    void (*bmap_part)(int x, int y, const void *bmap, int rows, rv_color tint);
    /* A BMap repeated across `width` columns from (x, y), the last copy cut to fit. */
    void (*bmap_tile)(int x, int y, int width, const void *bmap);
    /* A page in the style of Settings > About: the background, `icon` (a menu
     * bitmap of the extension, NULL for none) on the box of the Settings icons,
     * and `left` and `right` on either side of it. */
    void (*card)(int width, int height, const char *left, const char *right,
                 const struct rv_bitmap *icon);
};

enum rv_input_kind { RV_INPUT_WHEEL, RV_INPUT_PRESS, RV_INPUT_RELEASE };
enum rv_key { RV_KEY_CENTER, RV_KEY_MENU, RV_KEY_PREVIOUS, RV_KEY_NEXT, RV_KEY_PLAY };

struct rv_input {
    uint8_t kind;
    uint8_t key;   /* rv_key for presses and releases */
    int16_t delta; /* wheel steps, signed */
};

/* Where a runtime list row shows its detail. */
enum rv_detail {
    RV_DETAIL_BESIDE,        /* on the right of the label, as a setting's value */
    RV_DETAIL_BELOW,         /* a second line under the label */
    RV_DETAIL_BELOW_SUBMENU, /* a second line, and the row shows it opens a screen */
};

/* A screen the core registers for the extension. `name` is what RV_ROW_PUSH,
 * ui_push and a module's manifest trigger call it, local to the extension.
 * Labels and text are read from the module while the screen is open; nothing
 * is copied. */
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
            /* Draws the canvas, `width` by `height` pixels; input returns 1 when
             * consumed, 0 to leave the key to the system. */
            void (*draw)(const struct rv_gfx *gfx, int width, int height);
            int (*input)(const struct rv_input *input);
        } canvas;
        struct {
            uint32_t (*count)(void);
            /* Row `index`; a detail text goes to *detail (NULL for none). */
            const char *(*row)(uint32_t index, const char **detail);
            int (*select)(uint32_t index); /* returns 1 to pop the screen */
            uint8_t detail;                /* rv_detail: where the detail goes */
        } runtime;
    };
    /* A list in the split layout shows these in the preview pane beside the
     * title: a line of text and an icon on the system's box; NULL shows
     * nothing. */
    const char *caption;
    const struct rv_bitmap *bitmap;
};

/* A row the extension adds to a menu. `in` names the menu: "main", "music",
 * "extras", "settings", the menu a long press of Select opens on Now Playing
 * ("nowplaying") or on a song, album or artist row ("song", "album",
 * "artist"), or a container id such as "settings.sound". `after` is a stock
 * row of that menu from rv_menus.h (RV_SETTINGS_LEGAL), 0 for the end. Under
 * Settings the preview pane shows the label, `caption` as the line below and
 * `bitmap` as the icon while the row is highlighted; either may be NULL, and
 * the pane then shows nothing in its place. The icon sits on the box the
 * system draws behind stock Settings icons unless `plain_icon` is set. Rows
 * under Main and Music appear in Settings > Main Menu and Music Menu,
 * `visible` is their default there. */
struct rv_entry {
    const char *in;
    uint8_t visible;
    uint8_t plain_icon; /* 1: the pane shows `bitmap` without the system's box behind it */
    uint8_t reserved[2];
    uint32_t after;
    struct rv_row row;
    const char *caption;
    const struct rv_bitmap *bitmap;
};

/* Where a debugger finds core state in memory. The record offsets are into the
 * loader's per-extension record; `file` is the NUL-terminated file name without
 * .rvx, at most 32 bytes, `failure` a byte, the others words. */
struct rv_debug_info {
    uint32_t core_build;  /* the core's git revision, 8 hex digits */
    const void *extensions; /* address of the pointer to the first record */
    uint16_t next, owner, file, live, peak, failure;
    const void *heap;     /* Apple's MainHeap */
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

    /* Screens by name. push opens one above the screen whose row, choice or
     * input callback is running (the screen's module loads first; RV_NOT_FOUND
     * for an unknown name, RV_BAD_ARGUMENT outside a callback); pop closes the
     * top screen; refresh redraws rows whose text changed; invalidate redraws an
     * open canvas. */
    int (*ui_push)(rv_handle self, const char *screen);
    void (*ui_pop)(rv_handle self);
    void (*ui_refresh)(rv_handle self, const char *screen);
    void (*ui_invalidate)(rv_handle self);

    const struct rv_debug_info *(*debug_info)(void);

    /* Assets from manifest.toml by name. asset_load returns the decoded asset in
     * a block from alloc, *bytes its size, or NULL when it is missing, damaged or
     * does not fit; free it with free. A resident asset is copied from RAM.
     * asset_read copies up to `bytes` from `offset` of an asset stored raw
     * (compress = false), *got the count; RV_BAD_ARGUMENT for a compressed one,
     * RV_NOT_FOUND when it is missing or unreadable. */
    void *(*asset_load)(rv_handle self, const char *name, uint32_t *bytes);
    int (*asset_read)(rv_handle self, const char *name, uint32_t offset, void *out,
                      uint32_t bytes, uint32_t *got);

    /* Text into `out`, NUL-terminated within `capacity`; the length. A number
     * with thousands separators ("12,345"); bytes as KB or MB with one decimal
     * ("6.0 KB"). */
    uint32_t (*format_number)(char *out, uint32_t capacity, uint32_t value);
    uint32_t (*format_size)(char *out, uint32_t capacity, uint32_t bytes);
};

/* Every module exports this. The entry of the main module returns the
 * descriptor; on-demand modules may return NULL. */
typedef const struct rv_extension *(*rv_entry_fn)(const struct rv_api *api, rv_handle self);

#endif
