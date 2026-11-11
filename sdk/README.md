# RevolaOS SDK

Headers, linker script, tools and Make include for building extensions against one revola release. Everything an extension needs is in this directory; nothing reaches into the core.

| File | Contents |
|---|---|
| `rv_api.h` | the `rv_api` table the core passes to a module's entry, the descriptor it returns, hook, setting, screen, row and entry declarations; `RV_API_MAJOR`/`RV_API_MINOR` |
| `rv_ui.h` | one-line declarations of rows, screens, entries and settings over those structs (`RV_ROW_PUSH`, `RV_SCREEN_LIST`, `RV_MENU_ENTRY`, `RV_SETTING_CHOICE`) |
| `rv_libc.h`, `rv_libc.c` | the C library rv.mk links into every module: `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strcmp`, `rv_format` (printf into a buffer) and `rv_format_duration` ("3:07"); a module carries only what it calls |
| `rv_sites.h` | `RV_SITE_*` ids of every hook site in this core, generated from the core's site table |
| `rv_menus.h` | the stock rows of the Main, Music, Extras and Settings menus (`RV_SETTINGS_LEGAL`), generated from the symbols header; what an entry anchors on |
| `rv_firmware.h`, `rv_firmware.mk` | `RV_FIRMWARE_ID` of the Apple firmware the symbols describe; the loader refuses a module built for another |
| `osos-2.0.5.h`, `osos-2.0.5.ld` | Apple functions, data, constants and struct offsets by name; modules call Apple directly |
| `rvx.lds` | module layout: linked at 0, text, rodata, data and bss in one segment |
| `rvx-pack`, `manifest.py`, `bitmap.py` | `manifest.toml`, the module ELFs and the assets to the extension's one `.rvx` (Python 3.11+, needs the `arm-elf-eabi-` binutils); `manifest.toml` is described at the top of `manifest.py` |
| `rv.mk` | Make include: set `ID`, `MODULES` (in the manifest's order) and `<module>_SOURCES` (and `<module>_ARM_SOURCES`), include it, run `make` |

A module exports `rv_module_entry` (see `rv_module_entry_fn`). The main module returns a `struct rv_descriptor`; its hooks are bound in order and a conflict fails the load with the site named in Settings > Extensions. Install `build/<id>.rvx` as `/Revola/Extensions/<id>.rvx`.

## Declaring UI

An extension describes its UI; the core draws it as stock screens. The descriptor lists hooks, settings, screens and entries, each one line with `rv_ui.h`. A macro is named after what it builds: `RV_ROW_PUSH` builds a row of kind `RV_ROW_KIND_PUSH`, `RV_SCREEN_LIST` a screen of kind `RV_SCREEN_KIND_LIST`.

| Declaration | What the user sees |
|---|---|
| `RV_HOOK_FILTER(site, fn)`, `RV_HOOK_FILTER_AT(site, fn, priority)`, `RV_HOOK_REPLACE(site, fn)` | nothing directly: a handler on a hook site |
| `RV_HOOK_WORD(site, word)` | nothing directly: an exclusive instruction replacement at a word site |
| `RV_SETTING_BOOL`, `RV_SETTING_INT`, `RV_SETTING_CHOICE`, `RV_SETTING_SLIDER`, `RV_SETTING_MULTI` | a setting with its labels; an `RV_ROW_SETTING(label, key)` row shows its value and flips it or opens its screen |
| `RV_SCREEN_LIST(id, title, rows)` of `RV_ROW_PUSH`, `RV_ROW_ACTION`, `RV_ROW_SETTING`, `RV_ROW_INFO` rows | a menu, full width or beside the Settings preview pane as the screen it opens from decides (`.layout` overrides) |
| `RV_SCREEN_CHOICE`, `RV_SCREEN_MULTI_CHOICE`, `RV_SCREEN_SLIDER` | a screen over a setting with a title of its own; a labelled setting's `RV_ROW_SETTING` row opens one the core makes |
| `RV_SCREEN_TEXT(id, title, paragraphs)` | a text page |
| `RV_SCREEN_DYNAMIC_LIST(id, title, count, row, select)` | rows from callbacks, with an inline or stacked detail |
| `RV_SCREEN_CANVAS(id, title, draw, input)` | the extension draws with the `rv_gfx` table it is given (fills, text in the stock fonts, colour roles themes can change, wrapped paragraphs, list rows, BMaps whole, in part or tiled, and the Settings > About card the core's Usage page uses) and reads wheel and key input |
| `RV_SCREEN_CONTEXT(rows)` of `RV_CONTEXT_ROW(row, .available = predicate)` | a long-press menu on a list or dynamic list; action and push rows receive the highlighted index in `ctx->row`; unavailable rows are hidden and Cancel closes the menu |
| `RV_MENU_ENTRY(menu, after, row)` | a row in `RV_MENU_MAIN`, `RV_MENU_MUSIC`, `RV_MENU_EXTRAS`, `RV_MENU_SETTINGS` or a context menu (`RV_MENU_NOW_PLAYING_CONTEXT`, `RV_MENU_SONG_CONTEXT`, `RV_MENU_ALBUM_CONTEXT`, `RV_MENU_ARTIST_CONTEXT`, `RV_MENU_PODCAST_EPISODE_CONTEXT`), after a stock row from `rv_menus.h`; under Settings `.caption` and `.bitmap` fill the preview pane, `.shown_by` names a bool setting that shows the row (Settings > Main Menu and Music Menu flip it) |

Every UI callback (row actions and text, canvas draw and input, dynamic list rows, a screen's `.opened` and `.closed`) receives a `struct rv_context`: the extension, the Apple controller, the argument the screen was pushed with, and in a context menu's action the song (`item`) and model row the menu opened on. Screen ids are local to the extension; `api->ui_push(self, id, arg)` opens one from any code on the UI task, `ui_pop` closes the extension's own top screen. A canvas's `.closed` runs after the last draw of its slide-out, so it is where what the canvas draws with is freed. A canvas also gets `RV_INPUT_HOLD` when the hold switch moves and may redraw part of itself with `ui_invalidate_rect`; a dynamic list opens on the row `.dynamic.initial` returns. `ui_select(self, id, row)` highlights a dynamic-list row, clamped to the current count, immediately or when a covered screen becomes active again. An empty list has no selection. Its optional `.dynamic.status(ctx, index)` returns `RV_ROW_STATUS_NONE` or `RV_ROW_STATUS_PLAYING`; the core supplies the stock normal and selected speaker icons. Set `.dynamic.empty` to show a centered message when the row count is zero; `ui_refresh` updates it with the rows. An entry's `.available` decides each time a context menu opens whether its row is offered there, and `.caption_text` and `.bitmap_for` give the Settings pane's caption and icon as they are now. Screens and entries are limited only by the heap and by the extension's 4,096 resource ids: a list takes 18 + 2 per row, a choice, multi-choice or dynamic list 20, plus five ids and one per context row when a screen has a context menu, a text page 8 + one per paragraph, a slider 10, a canvas 8, a menu entry 2 (3 under Settings). A load that runs out shows No memory in Settings > Extensions. An unloaded extension's screens and records are freed. Labels and paragraphs are read from the module while a screen is open, never copied. Nothing about the firmware's own resources appears in a UI declaration; the symbols header is for hooks.

## Menu placement

Include `rv_menus.h` and choose an `after` constant from the menu receiving the entry. To offer a Music item on the Main Menu as well, declare both entries, as Apple does for Search. Give each entry its own `shown_by` bool setting so the two visibility choices remain independent.

```c
static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_MUSIC, RV_MUSIC_AUDIOBOOKS, RV_ROW_PUSH("Queue", "queue"),
                  .shown_by = KEY_IN_MUSIC),
    RV_MENU_ENTRY(RV_MENU_MAIN, RV_MAIN_AUDIOBOOKS, RV_ROW_PUSH("Queue", "queue"),
                  .shown_by = KEY_IN_MAIN),
};

static const struct rv_setting settings[] = {
    RV_SETTING_BOOL(KEY_IN_MUSIC, "Queue in Music", 1),
    RV_SETTING_BOOL(KEY_IN_MAIN, "Queue in Main Menu", 0),
};
```

The example requires a `queue` screen and two distinct, nonzero setting keys. Register the entries and settings in the extension descriptor.

Settings > Main Menu places an entry after its Main Menu anchor and uses that stock row’s indentation. `RV_MAIN_AUDIOBOOKS` places Queue under Music, immediately after Audiobooks and before Search. An `after` value of `0` uses the menu’s default end anchor; on Main this is Sleep, so the editor displays the entry at root level. The Music declaration alone does not create a Main Menu entry.

For an Extras item offered on Main, likewise declare both entries and use the corresponding anchors, such as `RV_EXTRAS_STOPWATCH` and `RV_MAIN_STOPWATCH`. The Main Menu editor also has stock Video anchors, such as `RV_MAIN_MOVIES`; the SDK currently has no `RV_MENU_VIDEOS` destination.

## Settings

Declare settings in the descriptor and read them with `api->setting_int(self, key)`, which applies the declared fallback as the Settings rows do; `api->setting_set_int` clamps to the declared range, stores, redraws the rows that show the setting and calls the descriptor's `setting_changed(key)`. That callback also runs when the user changes a setting in a row, a choice or a slider, and with `RV_SETTING_KEY_ALL` once the main module has loaded and after Reset All Settings emptied the store, so an extension that keeps a copy (DSP coefficients, a cached flag) builds and refreshes it in one place. The main module's entry runs before its declarations are known, so settings read there get no fallback. `setting_read` and `setting_write` move raw bytes, for strings and blobs. An entry's `.shown_by` names a declared bool setting: the row shows while it is on, and under Main and Music the Main Menu and Music Menu settings flip it.

## Files

Each extension has a data folder, `/Revola/Data/<id>/`. `api->file_open` opens a path inside it for reading or writing (writing creates the folders above it), `file_read`, `file_write`, `file_seek` and `file_close` work on the handle, `file_delete` removes a file and `folder_list` lists a folder. Paths cannot leave the folder, and the core closes what an extension left open when it unloads. Anything else on the disk is reached through Apple's file functions in the symbols header.

## Assets

`[[asset]]` tables in `manifest.toml` put files into the `.rvx`: a `.png` becomes a bitmap, anything else data. Find them by name with `api->asset_load` (a decoded copy, freed with `api->free`) and `api->asset_read` (part of an asset stored with `compress = false`). Draw a bitmap with `RV_BITMAP("name")`.

| Key | Meaning |
|---|---|
| `resident = true` | in RAM, as stored, while the extension is loaded: no disk access to use it. For what a screen needs at once, such as icons and small tables |
| `menu = true` | a menu bitmap, such as an entry's `.bitmap` in the Settings pane: like the stock icons, a 78 x 121 coverage mask the system draws in its own colour, from the PNG's alpha (or its gray level when it is opaque grayscale) |
| `compress = false` | data only: stored raw, so `asset_read` can seek in it. Bitmaps are always compressed |

The core decodes only the menu bitmap shown and keeps a canvas's bitmaps decoded while the canvas is open, so `gfx->bitmap` costs a decode on the first draw only. Everything decoded is counted against the extension.

For code no core site covers, the manifest may declare raw sites (`[[site]]`, described at the top of `manifest.py`) and bind them as `RV_RAW_SITE(n)`. They are exclusive and checked against the running firmware at load. Ask for a core site when one would serve others too.

Build rules: Thumb, `-mlong-calls`, `-fno-pic -fno-jump-tables`, no C library beyond `rv_libc.h` (`api->format_number` and `api->format_size` write "12,345" and "6.0 KB"); only `R_ARM_ABS32` relocations may remain, which rvx-pack checks. Memory from `api->alloc` is counted against the extension and shown in Settings > Extensions.

Thumb is the default because it is denser, and RAM comes first. Use ARM in two cases. First, DSP and hot loops: the ARMv5TE multiply-accumulate and saturating instructions (SMULxy, SMLAxy, QADD) exist only in ARM state. Second, calls into Apple routines that return with `mov pc, lr`: such a return always lands in ARM state, so a Thumb caller crashes. The call must come from an ARM function and must not be a tail call. Mark single functions `RV_ARM`, or list whole files in `<module>_ARM_SOURCES`; those files are built with `-marm -fno-optimize-sibling-calls`.
