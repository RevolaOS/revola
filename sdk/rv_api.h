/* SPDX-License-Identifier: GPL-3.0-only */

/* The core services an extension may call, and the structs it declares itself
 * with. Within a major version the rv_api and rv_gfx tables only grow at the
 * end: a module built against an older minor finds every entry it knows at the
 * same offset and tests a newer one with RV_HAS before calling it. The structs
 * an extension fills in (descriptor, hooks, settings, screens, rows, entries)
 * keep their layout. The manifest's `api` names the minor a module needs, and
 * an older core leaves the extension off. The major changes when an entry or
 * a layout changes meaning. Apple functions are not here; modules call them
 * directly through the symbols header and linker script of the OSOS build
 * named by RV_FIRMWARE_ID. */

#ifndef RV_API_H
#define RV_API_H
#include <stdint.h>

#define RV_API_MAJOR 1
#define RV_API_MINOR 35
#define RV_API_VERSION ((RV_API_MAJOR << 16) | RV_API_MINOR)

/* Whether a table with a leading `size` (rv_api, rv_gfx, rv_debug_info) has
 * `field`: RV_HAS(api, format_size). */
#define RV_HAS(table, field)                                                                   \
    ((table)->size >= __builtin_offsetof(__typeof__(*(table)), field) + sizeof((table)->field))

/* Compiles a function as ARM. Needed for DSP instructions that exist only in ARM
 * state and for calls into Apple routines that return with `mov pc, lr`; the
 * call must not become a tail call, or the routine returns to the Thumb caller in
 * ARM state. */
#define RV_ARM __attribute__((target("arm"), noinline, optimize("no-optimize-sibling-calls")))

/* The loader's record of the extension; passed back to every call. */
typedef struct rv_extension_record *rv_extension_handle;
struct rv_timer;
struct rv_file;

enum rv_file_mode { RV_FILE_READ = 1, RV_FILE_WRITE = 2 };

enum rv_result {
    RV_OK,
    RV_CONFLICT,   /* a replace handler meets another handler, or the site is exclusive */
    RV_NO_MEMORY,
    RV_NOT_BOUND,
    RV_BAD_SITE,   /* no site with that id in this core */
    RV_BAD_ARGUMENT,
    RV_NOT_FOUND,
    RV_ALREADY_BOUND, /* this extension already has the handler at that site */
};

enum rv_hook_mode {
    RV_HOOK_MODE_FILTER,  /* receives `next`; may continue through api->hook_next or short-circuit */
    RV_HOOK_MODE_REPLACE, /* exclusive; never continues */
};

/* A handler receives the Apple arguments followed by `next` (in the register
 * after the last argument for sites with at most three register arguments, as
 * the first stack argument for four), then any arguments Apple passes on the
 * stack. Site ids are RV_SITE_* from rv_sites.h, or RV_RAW_SITE(n) for the n-th
 * [[site]] of the extension's manifest. The struct must live as long as the
 * binding. Word sites use REPLACE mode and the uint32_t word member instead of
 * a handler. The core writes the instruction directly; it is never called. */
#define RV_RAW_SITE(index) (0xffffff00u | (index))
struct rv_hook {
    uint32_t site;
    union {
        void *handler;
        uint32_t word;
    };
    int16_t priority; /* lower runs first */
    uint8_t mode;     /* rv_hook_mode */
    uint8_t reserved;
};

enum rv_setting_type {
    RV_SETTING_TYPE_BOOL = 1,
    RV_SETTING_TYPE_INT = 2,
    RV_SETTING_TYPE_STRING = 3,
    RV_SETTING_TYPE_BLOB = 4,
};

/* The key the core keeps in every extension's settings for its enabled flag; an
 * extension never writes it. RV_SETTING_KEY_ALL is what setting_changed
 * receives after Reset All Settings emptied the store. */
#define RV_SETTING_KEY_ENABLED 0
#define RV_SETTING_KEY_ALL 0xffff

/* How an RV_ROW_SETTING row shows an int setting with `labels`. */
enum rv_setting_style {
    RV_SETTING_STYLE_NONE,   /* no row of its own */
    RV_SETTING_STYLE_CHOICE, /* a list of the labels; the value is the chosen index */
    RV_SETTING_STYLE_SLIDER, /* a bar over the labels; the value is the position */
    RV_SETTING_STYLE_MULTI,  /* a checkmark per label; bit n of the value is label n */
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
    const char *const *labels; /* one per value; at most 32 for RV_SETTING_STYLE_MULTI */
    uint32_t label_count;
};

enum rv_row_kind {
    RV_ROW_KIND_PUSH,    /* opens `screen` */
    RV_ROW_KIND_ACTION,  /* calls `action` */
    RV_ROW_KIND_SETTING, /* the bool or int setting `key`: shows its value; flips a bool, opens
                            the screen of an int with labels */
    RV_ROW_KIND_INFO,    /* shows `label` or `text`; does nothing when chosen */
};

/* What every UI callback receives: where it runs. `controller` is the Apple
 * controller of the screen or menu, for Apple calls. `arg` is what ui_push
 * passed for this screen, NULL for a screen a row opened. In a context menu's
 * action, `item` is the MediaItem (struct MediaItem of the symbols header) of
 * the song, episode, audiobook or video the menu opened on, NULL for an album
 * or artist, and `row` the model row the menu opened on (-1 for an album or
 * artist list's "All" row). A screen the menu's row or action opens keeps both
 * for every callback until it closed, and so does a screen opened above it;
 * elsewhere `item` is NULL and `row` -1. A row that opens a screen from a
 * context menu follows the menu with no frame of the screen underneath. Valid
 * during the callback only. */
struct rv_context {
    rv_extension_handle self;
    void *controller;
    void *arg;
    const void *item;
    int32_t row;
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
    void (*action)(const struct rv_context *ctx);
    const char *(*text)(const struct rv_context *ctx);
};

/* A row of a screen's long-press menu. `available` is asked each time the menu
 * opens; 0 leaves the row out, NULL always shows it. Its callbacks and the
 * row's get ctx->row, the row highlighted when the menu opened. */
struct rv_context_row {
    struct rv_row row;
    int (*available)(const struct rv_context *ctx);
};

enum rv_screen_kind {
    RV_SCREEN_KIND_LIST,         /* rows from `list` */
    RV_SCREEN_KIND_CHOICE,       /* one checkmark among `choice.labels`; stores the index in
                               the int setting `choice.key` and pops */
    RV_SCREEN_KIND_SLIDER,       /* a bar over `choice.labels`; wheel and Previous/Next change
                               the int setting `choice.key` */
    RV_SCREEN_KIND_TEXT,         /* the paragraphs of `text` */
    RV_SCREEN_KIND_CANVAS,       /* the extension draws and reads input */
    RV_SCREEN_KIND_DYNAMIC_LIST, /* rows from callbacks, for data known only at run time */
    RV_SCREEN_KIND_MULTI_CHOICE, /* a checkmark per label; bit n of the int setting `choice.key`
                               is label n (at most 32), Select flips it */
};

/* How a list screen (list, choice, multi-choice, dynamic list) is laid out.
 * Left at AUTO it follows the screen it opens from: split under Settings or a
 * split list, full width elsewhere, as the stock menus do. */
enum rv_layout {
    RV_LAYOUT_AUTO,
    RV_LAYOUT_FULL,  /* the list over the whole screen */
    RV_LAYOUT_SPLIT, /* the list on the left, the Settings preview pane on the right */
};

/* How a slider or canvas screen opens. */
enum rv_transition {
    RV_TRANSITION_SLIDE,     /* slides in from the right, as Settings > About */
    RV_TRANSITION_FROM_PANE, /* opened from a split list, it starts where the preview pane is
                             and slides into place, as Extras > Stopwatch; from anywhere
                             else as RV_TRANSITION_SLIDE. The whole screen moves, so this
                             suits a screen whose picture continues what the pane showed */
};

/* A bitmap asset of the extension by name, from a PNG in manifest.toml:
 * `.bitmap = RV_BITMAP("eq-icon")`. Menu bitmaps (an entry's .bitmap) are
 * assets with `menu = true`: 78 x 121 coverage masks the system draws in its own
 * colour, like the stock Settings icons; the core decodes only the one shown. */
struct rv_bitmap {
    const char *asset;
};
#define RV_BITMAP(name) (&(const struct rv_bitmap){.asset = (name)})

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

/* What a canvas draws with. Valid inside the draw callback only. Grows like
 * rv_api: test a newer entry with RV_HAS. */
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

/* RV_INPUT_HOLD: the hold switch moved; `delta` is 1 when it locked, 0 when it
 * unlocked. What the input callback returns for it is ignored. */
enum rv_input_kind { RV_INPUT_WHEEL, RV_INPUT_PRESS, RV_INPUT_RELEASE, RV_INPUT_HOLD };
enum rv_button { RV_BUTTON_CENTER, RV_BUTTON_MENU, RV_BUTTON_PREVIOUS, RV_BUTTON_NEXT, RV_BUTTON_PLAY };

struct rv_input {
    uint8_t kind;
    uint8_t button; /* rv_button for presses and releases */
    int16_t delta;  /* wheel steps, signed */
};

/* Where a dynamic list row shows its detail. */
enum rv_detail {
    RV_DETAIL_BESIDE,        /* on the right of the label, as a setting's value */
    RV_DETAIL_BELOW,         /* a second line under the label */
    RV_DETAIL_BELOW_SUBMENU, /* a second line, and the row shows it opens a screen */
};

/* A stock status symbol at the trailing edge of a dynamic-list row. */
enum rv_row_status {
    RV_ROW_STATUS_NONE,
    RV_ROW_STATUS_PLAYING,
};

/* A screen the core registers for the extension. `id` is what RV_ROW_PUSH,
 * ui_push and a module's manifest trigger call it, local to the extension.
 * Labels and text are read from the module while the screen is open; nothing
 * is copied. */
struct rv_screen {
    const char *id;
    const char *title;
    uint8_t kind;
    uint8_t layout;     /* rv_layout, list kinds only */
    uint8_t transition; /* rv_transition, slider and canvas only */
    uint8_t reserved[1];
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
            void (*draw)(const struct rv_context *ctx, const struct rv_gfx *gfx, int width,
                         int height);
            int (*input)(const struct rv_context *ctx, const struct rv_input *input);
        } canvas;
        struct {
            uint32_t (*count)(const struct rv_context *ctx);
            /* Row `index`; a detail text goes to *detail (NULL for none). */
            const char *(*row)(const struct rv_context *ctx, uint32_t index,
                               const char **detail);
            int (*select)(const struct rv_context *ctx, uint32_t index); /* 1 pops the screen */
            /* The row highlighted when the screen opens; NULL keeps the list's
             * own, which may be a row of the last list shown. */
            uint32_t (*initial)(const struct rv_context *ctx);
            uint8_t detail;                /* rv_detail: where the detail goes */
            /* NULL shows no status; normal and selected artwork follows the row. */
            enum rv_row_status (*status)(const struct rv_context *ctx, uint32_t index);
            const char *empty; /* centered text while count is zero, NULL for none */
        } dynamic;
    };
    /* A list in the split layout shows these in the preview pane beside the
     * title: a line of text and an icon on the system's box; NULL shows
     * nothing. */
    const char *caption;
    const struct rv_bitmap *bitmap;
    /* After the screen was pushed, and after it closed for good: for a canvas
     * after the last draw of its slide-out, so what it draws with can be freed
     * there. Either may be NULL. Not called when the extension unloads under
     * an open screen; unload covers that. */
    void (*opened)(const struct rv_context *ctx);
    void (*closed)(const struct rv_context *ctx);
    /* List and dynamic-list screens: the rows a long press of Select offers,
     * at most 16, before the system's Cancel row. */
    const struct rv_context_row *context;
    uint32_t context_count;
};

/* The menus an entry can add a row to. The context menus are the ones a long
 * press of Select opens on Now Playing or on a row of a media list: songs
 * (song, playlist and Genius lists), albums, artists, podcast and iTunes U
 * episodes, audiobooks, movies, TV episodes and video playlist entries. Movies
 * and TV episodes have no stock context menu; the core opens one for the
 * extensions' rows. Every menu but album and artist hands its action the
 * item (ctx->item). */
enum rv_menu {
    RV_MENU_MAIN,
    RV_MENU_MUSIC,
    RV_MENU_EXTRAS,
    RV_MENU_SETTINGS,
    RV_MENU_NOW_PLAYING_CONTEXT,
    RV_MENU_SONG_CONTEXT,
    RV_MENU_ALBUM_CONTEXT,
    RV_MENU_ARTIST_CONTEXT,
    RV_MENU_PODCAST_EPISODE_CONTEXT,
    RV_MENU_ITUNES_U_EPISODE_CONTEXT,
    RV_MENU_AUDIOBOOK_CONTEXT,
    RV_MENU_MOVIE_CONTEXT,
    RV_MENU_TV_EPISODE_CONTEXT,
    RV_MENU_VIDEO_PLAYLIST_CONTEXT,
};

/* A row the extension adds to `menu`. `after` is a stock row of that menu from
 * rv_menus.h (RV_SETTINGS_LEGAL), 0 for the end. Under Settings the preview
 * pane shows the label, `caption` as the line below and `bitmap` as the icon
 * while the row is highlighted; either may be NULL, and the pane then shows
 * nothing in its place. The icon sits on the box the system draws behind stock
 * Settings icons unless `plain_icon` is set. With `shown_by`, the key of a
 * declared bool setting, the row shows while that setting is on; under Main
 * and Music, Settings > Main Menu and Music Menu list the row and flip it.
 * Without, the row always shows. */
struct rv_menu_entry {
    uint8_t menu;       /* rv_menu */
    uint8_t plain_icon; /* 1: the pane shows `bitmap` without the system's box behind it */
    uint16_t shown_by;  /* a bool setting key, 0 for always shown */
    uint32_t after;
    struct rv_row row;
    const char *caption;
    const struct rv_bitmap *bitmap;
    /* When set, the pane's caption and icon each time the row is highlighted,
     * instead of `caption` and `bitmap`. */
    const char *(*caption_text)(const struct rv_context *ctx);
    const struct rv_bitmap *(*bitmap_for)(const struct rv_context *ctx);
    /* Context menus: asked each time the menu opens, with the item and row it
     * opened on; 0 leaves the row out. NULL always shows it. */
    int (*available)(const struct rv_context *ctx);
};

/* What the core reports to a debugger's trace probe, with the argument. */
enum rv_trace_kind {
    RV_TRACE_FLUSH_BEGIN = 1, /* the settings store's bytes; the write starts */
    RV_TRACE_FLUSH_END,       /* 1 when written, 0 when the store stays dirty */
    RV_TRACE_LOAD_BEGIN,      /* an extension's owner; one of its modules loads */
    RV_TRACE_LOAD_END,        /* the owner; its record's failure says how it went */
    RV_TRACE_UNLOAD,          /* the owner; one of its modules unloads */
    RV_TRACE_ASSET_BEGIN,     /* the owner; an asset is read and decoded */
    RV_TRACE_ASSET_END,       /* the owner */
    RV_TRACE_DECODE_BEGIN,    /* bytes the lz decoder produces, for an asset or a core icon */
    RV_TRACE_DECODE_END,      /* 1 when the stream was whole */
};

/* Where a debugger finds core state in memory, and what it may do that an
 * extension may not. The record offsets are into the loader's per-extension
 * record; `file` is the NUL-terminated file name without .rvx, at most 32
 * bytes, `failure` a byte, the others words. */
struct rv_debug_info {
    uint32_t size;        /* bytes of this record */
    uint32_t core_build;  /* the core's git revision, 8 hex digits */
    const void *extensions; /* address of the pointer to the first record */
    uint16_t next, owner, file, live, peak, failure;
    const void *heap;     /* Apple's MainHeap */
    /* Turns the extension in /Revola/Extensions/<file>.rvx on or off as Settings >
     * Extensions does. UI task. */
    int (*set_enabled)(const char *file, int enabled);
    /* The arguments a filter on core site `site` (an RV_SITE_* id) receives:
     * registers in bits 0-3, stack words in bits 4-5. -1 for an unknown id and
     * for sites that take no filter (word, inserted and no-return sites). */
    int (*site_argc)(uint32_t site);
    /* The core's trace probe slot. A debugger stores a probe here, which the core
     * calls at its tracepoints with an rv_trace_kind and its argument, and clears
     * it before its module unloads; a tracepoint reads the slot once. */
    void (*volatile *trace)(uint32_t kind, uint32_t argument);
};

/* Returned by a module's entry; everything it points to lives as long as the
 * module is loaded. The main module's declares everything. A screen or
 * manual module's declares only its own hooks, screens and unload: the core
 * binds and registers them when it loads and drops them when it unloads, so
 * a screen module's screens, and the code they call, take no memory until one
 * of them is opened. Its screen ids are the ones its manifest entry lists. */
struct rv_descriptor {
    uint32_t size; /* sizeof(struct rv_descriptor) when built */
    const struct rv_hook *hooks;
    uint32_t hook_count;
    const struct rv_setting *settings;
    uint32_t setting_count;
    void (*unload)(void); /* before the hooks are unbound; may be NULL */
    /* After a setting changed, whether from a row, a choice or slider screen or
     * the extension's own write; RV_SETTING_KEY_ALL once the main module has
     * loaded and after Reset All Settings. UI task; may be NULL. */
    void (*setting_changed)(uint16_t key);
    const struct rv_screen *screens;
    uint32_t screen_count;
    const struct rv_menu_entry *entries;
    uint32_t entry_count;
};

struct rv_api {
    uint32_t size;    /* bytes of this table */
    uint32_t version; /* RV_API_VERSION of the core */

    /* Bind beyond the descriptor; unbind only what hook_bind bound (the
     * descriptor's hooks are unbound by the loader). A handler the extension
     * already has at the site returns RV_ALREADY_BOUND and binds nothing. */
    int (*hook_bind)(rv_extension_handle self, const struct rv_hook *hook);
    int (*hook_unbind)(rv_extension_handle self, const struct rv_hook *hook);
    /* Continue a filter chain; unused trailing arguments are 0. */
    uint32_t (*hook_next)(const void *next, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                     uint32_t e, uint32_t f);

    /* A bool or int setting: its value, the declared fallback when the store
     * has none. setting_set_int clamps to the declared minimum and maximum (0
     * and 1 for a bool; a multi-choice keeps only its labels' bits), stores,
     * redraws the rows that show it and calls setting_changed. Inside the main
     * module's entry the declarations are not known yet: no fallback, no
     * clamp, no callback. */
    int32_t (*setting_int)(rv_extension_handle self, uint16_t key);
    int (*setting_set_int)(rv_extension_handle self, uint16_t key, int32_t value);
    /* Any setting as bytes, for strings and blobs: setting_read copies at most
     * `capacity` bytes and returns how many, 0 when absent; setting_write
     * stores at most 255 bytes and calls setting_changed. Neither applies a
     * fallback. The core's keys are refused. */
    uint32_t (*setting_read)(rv_extension_handle self, uint16_t key, void *out, uint32_t capacity);
    int (*setting_write)(rv_extension_handle self, uint16_t key, uint8_t type, const void *data,
                         uint32_t bytes);
    void (*settings_flush)(void);

    /* Zeroed memory from the Apple heap, counted against the extension. */
    void *(*alloc)(rv_extension_handle self, uint32_t bytes);
    void (*free)(rv_extension_handle self, void *block);

    /* Manual-trigger modules by their index in the manifest. module_unload
     * waits until none of the module's screens is open. */
    int (*module_load)(rv_extension_handle self, uint32_t module);
    void (*module_unload)(rv_extension_handle self, uint32_t module);

    /* Kept for the emulator; nothing on the device shows it. */
    void (*log)(rv_extension_handle self, const char *text);

    /* Screens by id, from code on the UI task: rows, timers, hooks there.
     * ui_push opens one above the top screen, loading the screen's module
     * first; `arg` reaches the screen's callbacks as ctx->arg. RV_NOT_FOUND
     * for an unknown id. ui_pop closes the top screen when it is the
     * extension's; refresh redraws rows whose text changed; invalidate redraws
     * the canvas on top when it is the extension's. */
    int (*ui_push)(rv_extension_handle self, const char *screen, void *arg);
    void (*ui_pop)(rv_extension_handle self);
    void (*ui_refresh)(rv_extension_handle self, const char *screen);
    void (*ui_invalidate)(rv_extension_handle self);

    const struct rv_debug_info *(*debug_info)(void);

    /* Assets from manifest.toml by name. asset_load returns the decoded asset in
     * a block from alloc, *bytes its size, or NULL when it is missing, damaged or
     * does not fit; free it with free. A resident asset is copied from RAM.
     * asset_read copies up to `bytes` from `offset` of an asset stored raw
     * (compress = false), *got the count; RV_BAD_ARGUMENT for a compressed one,
     * RV_NOT_FOUND when it is missing or unreadable. */
    void *(*asset_load)(rv_extension_handle self, const char *name, uint32_t *bytes);
    int (*asset_read)(rv_extension_handle self, const char *name, uint32_t offset, void *out,
                      uint32_t bytes, uint32_t *got);

    /* Text into `out`, NUL-terminated within `capacity`; the length. A number
     * with thousands separators ("12,345"); bytes as KB or MB with one decimal
     * ("6.0 KB"). */
    uint32_t (*format_number)(char *out, uint32_t capacity, uint32_t value);
    uint32_t (*format_size)(char *out, uint32_t capacity, uint32_t bytes);

    /* Calls `callback(arg)` on the UI task after `ms`, and every `ms` after
     * that when `repeat` is set; NULL when the heap is full. timer_stop ends
     * it; a one-shot timer ends by itself once it fired, and must not be
     * stopped after that. The core stops a module's timers before the module
     * unloads. Both are for the UI task. */
    struct rv_timer *(*timer_start)(rv_extension_handle self, uint32_t ms, int repeat,
                                    void (*callback)(void *arg), void *arg);
    void (*timer_stop)(rv_extension_handle self, struct rv_timer *timer);

    /* ui_invalidate for `rect` of the canvas, in its coordinates; the draw
     * callback runs as for ui_invalidate. */
    void (*ui_invalidate_rect)(rv_extension_handle self, const struct rv_rect *rect);

    /* Files in the extension's data folder, /Revola/Data/<id>/. `name` is a
     * path inside it ("presets/rock.bin") without "..". file_open with
     * RV_FILE_WRITE creates the folders above it and a missing file; to
     * replace a file, delete it first. file_read sets *got, short at the end.
     * folder_list calls `each` for every entry of a folder ("" for the data
     * folder), nothing when it is missing. The core closes what is still open
     * when the extension unloads. RV_BAD_ARGUMENT for a bad name or handle,
     * RV_NOT_FOUND when the file layer refuses. UI task. */
    int (*file_open)(rv_extension_handle self, const char *name, int mode, struct rv_file **file);
    int (*file_read)(rv_extension_handle self, struct rv_file *file, void *out, uint32_t bytes,
                     uint32_t *got);
    int (*file_write)(rv_extension_handle self, struct rv_file *file, const void *data,
                      uint32_t bytes);
    int (*file_seek)(rv_extension_handle self, struct rv_file *file, uint32_t offset);
    void (*file_close)(rv_extension_handle self, struct rv_file *file);
    int (*file_delete)(rv_extension_handle self, const char *name);
    int (*folder_list)(rv_extension_handle self, const char *name,
                       void (*each)(const char *entry, int is_folder, void *arg), void *arg);

    /* Masks IRQ and FIQ and returns the state before; irq_restore puts that
     * state back. For a handoff to an interrupt or audio handler that must not
     * see it half done, such as swapping a coefficient table; keep the masked
     * stretch to a few instructions. Any task. */
    uint32_t (*irq_save)(void);
    void (*irq_restore)(uint32_t state);

    /* Highlight a dynamic-list row, clamped to its current count. If covered
     * by another screen, apply on return. An empty list has no selection. UI task. */
    void (*ui_select)(rv_extension_handle self, const char *screen, uint32_t row);
};

/* Every module exports this as rv_module_entry. The main module returns its
 * descriptor; another module may return NULL when it declares nothing. */
typedef const struct rv_descriptor *(*rv_module_entry_fn)(const struct rv_api *api, rv_extension_handle self);

#endif
