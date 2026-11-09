# RevolaOS SDK

Headers, linker script, tools and Make include for building extensions against one revola release. Everything an extension needs is in this directory; nothing reaches into the core.

| File | Contents |
|---|---|
| `rv_api.h` | the `rv_api` table the core passes to a module's entry, the descriptor it returns, hook, setting, screen, row and entry declarations; `RV_API_MAJOR`/`RV_API_MINOR` |
| `rv_ui.h` | one-line declarations of rows, screens, entries and settings over those structs (`RV_PUSH`, `RV_LIST`, `RV_ENTRY`, `RV_SETTING_CHOICE`) |
| `rv_sites.h` | `RV_SITE_*` ids of every hook site in this core, generated from the core's site table |
| `rv_menus.h` | the stock rows of the Main, Music, Extras and Settings menus (`RV_SETTINGS_LEGAL`), generated from the symbols header; what an entry anchors on |
| `rv_firmware.h`, `rv_firmware.mk` | `RV_FIRMWARE_ID` of the Apple firmware the symbols describe; the loader refuses a module built for another |
| `osos-2.0.5.h`, `osos-2.0.5.ld` | Apple functions, data, constants and struct offsets by name; modules call Apple directly |
| `rvx.lds` | module layout: linked at 0, text, rodata, data and bss in one segment |
| `rvx-pack` | ELF to `.rvx` (Python 3.11+, needs the `arm-elf-eabi-` binutils) |
| `manifest` | `manifest.toml` to `manifest.bin` |
| `rv.mk` | Make include: set `ID`, `MODULES` and `<module>_SOURCES`, include it, run `make` |

A module exports `rvx_entry` (see `rv_entry_fn`). The main module returns a `struct rv_extension`; its hooks are bound in order and a conflict fails the load with the site named in Settings > Extensions. Install the output directory as `/Revola/Extensions/<id>/`.

## Declaring UI

An extension describes its UI; the core draws it as stock screens. The descriptor lists settings, screens and entries, each one line with `rv_ui.h`:

| Declaration | What the user sees |
|---|---|
| `RV_SETTING_FLAG`, `RV_SETTING_CHOICE`, `RV_SETTING_SLIDER`, `RV_SETTING_MULTI` | a setting with its labels; an `RV_SETTING(label, key)` row shows its value and flips it or opens its screen |
| `RV_LIST(name, title, rows)` of `RV_PUSH`, `RV_ACTION`, `RV_SETTING`, `RV_TOGGLE` rows | a menu, full width or beside the Settings preview pane as the screen it opens from decides (`.layout` overrides) |
| `RV_CHOICE`, `RV_MULTI_CHOICE`, `RV_SLIDER` | a screen over a setting with a title of its own |
| `RV_TEXT(name, title, paragraphs)` | a text page |
| `RV_RUNTIME_LIST(name, title, count, row, select)` | rows from callbacks, with an inline or stacked detail |
| `RV_CANVAS(name, title, draw, input)` | the extension draws with the `rv_gfx` table it is given (fills, text in the stock fonts, colour roles themes can change) and reads wheel and key input |
| `RV_ENTRY(menu, after, row)` | a row in `"main"`, `"music"`, `"extras"`, `"settings"`, a long-press menu (`"nowplaying"`, `"song"`, `"album"`, `"artist"`) or a container, after a stock row from `rv_menus.h`; under Settings `.caption` and `.bitmap` fill the preview pane |

Screen names are local to the extension; `api->ui_push(self, name)` opens one from a row, choice or input callback. Labels, paragraphs and bitmaps are read from the module while a screen is open, never copied. Nothing about the firmware's own resources appears in a UI declaration; the symbols header is for hooks.

For code no core site covers, the manifest may declare raw sites (`[[site]]`, described at the top of `manifest`) and bind them as `RV_RAW_SITE(n)`. They are exclusive and checked against the running firmware at load. Ask for a core site when one would serve others too.

Build rules: Thumb, `-mlong-calls`, `-fno-pic -fno-jump-tables`, no C library; only `R_ARM_ABS32` relocations may remain, which rvx-pack checks. Memory from `api->alloc` is counted against the extension and shown in Settings > Extensions.
