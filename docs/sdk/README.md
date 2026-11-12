# RevolaOS SDK

An extension is a set of modules built against one revola release and packed into one `.rvx` file. The SDK is the headers, linker script, Make include and packer an extension needs; nothing in it reaches into the core.

| Page | Contents |
|---|---|
| [Modules](modules.md) | Entry point, descriptor, module triggers, build rules, when to use ARM |
| [User interface](ui.md) | Declaring screens, rows, settings and entries; canvases; resource limits |
| [Menu placement](menus.md) | Putting a row into a stock menu |
| [Settings](settings.md) | Reading, writing and reacting to settings |
| [Files](files.md) | The extension's data folder |
| [Assets](assets.md) | Bitmaps and data packed into the `.rvx` |
| [Raw sites](raw-sites.md) | Hooking code no core site covers |
| [Audio effects](audio.md) | The output PCM site and the effect stages |
| [Canvas kit](canvas.md) | Palette, plots, value cells and Select/Menu input for an editor canvas |
| [Media items](media.md) | Reading the items the context menus hand out and the player's |

## Files of the SDK

| File | Contents |
|---|---|
| `rv_api.h` | The `rv_api` table the core passes to a module's entry, the descriptor it returns, hook, setting, screen, row and entry structs, `RV_API_MAJOR` and `RV_API_MINOR` |
| `rv_ui.h` | One-line declarations over those structs: `RV_ROW_PUSH`, `RV_SCREEN_LIST`, `RV_MENU_ENTRY`, `RV_SETTING_CHOICE` and the rest |
| `rv_libc.h`, `rv_libc.c` | The C library `rv.mk` links into every module; a module carries only what it calls |
| `rv_canvas.h`, `rv_canvas.c` | The [canvas kit](canvas.md), linked like `rv_libc` |
| `rv_math.h`, `rv_math.c` | Double precision without a math library, for filter designs and the curves the editors draw: `rv_exp`, `rv_log`, `rv_sin_cos`, `rv_sqrt`, `rv_decibels`; linked like `rv_libc` |
| `rv_audio.h` | What an [audio effect](audio.md) shares with the others: the output format check, `rv_fracmul`, `rv_saturate16` and the handoff of a set of values to the pump task |
| `rv_media.h` | [Media items](media.md): the item, the player and a song model's rows |
| `rv_sites.h` | `RV_SITE_*` ids of every hook site in this core, generated from the site table |
| `rv_menus.h` | The stock rows of the Main, Music, Extras and Settings menus, generated from the symbols header; what an entry anchors on |
| `rv_firmware.h`, `rv_firmware.mk` | `RV_FIRMWARE_ID` of the Apple firmware the symbols describe; the loader refuses a module built for another |
| `osos-2.0.5.h`, `osos-2.0.5.ld` | Apple functions, data, constants and struct offsets by name; modules call Apple directly |
| `rvx.lds` | Module layout: linked at 0, text, rodata, data and bss in one segment |
| `rv.mk` | Make include |
| `rvx-pack`, `manifest.py`, `bitmap.py` | Pack `manifest.toml`, the module ELFs and the assets into the `.rvx`; Python 3.11+ and the `arm-elf-eabi-` binutils |
| `docs/` | These pages |

`rv_libc.h` provides `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strcmp`, `rv_format` (printf into a buffer), `rv_format_duration` ("3:07"), `rv_format_int` ("100 %"), `rv_format_tenths` ("-0.5 dB"), `rv_format_hz` ("1.5k Hz") and `rv_copy_utf8` (a copy that never cuts inside a UTF-8 sequence). `api->format_number` and `api->format_size` write "12,345" and "6.0 KB".

## A first extension

`manifest.toml` names the extension and its modules. The format is described at the top of `manifest.py`.

```toml
id = "hello"
name = "Hello"
description = "Adds a Hello page to Settings."
version = "0.1.0"
author = "You"
license = "GPL-3.0-only"
api = "1.42"                 # RV_API_MAJOR.RV_API_MINOR of the SDK you build against

[[module]]
name = "main"
trigger = "always"
```

An installed extension is off until the listener turns it on in Settings > Extensions. Only the first-party extensions listed in the core's `core/loader/bundled.def` start on.

`Makefile` sets the id, the modules in the manifest's order and each module's sources, then includes `rv.mk`:

```make
ID := hello
MODULES := main
main_SOURCES := main.c
include $(SDK)/rv.mk
```

`main.c` exports `rv_module_entry`, keeps the API table and its handle, and returns a descriptor:

```c
#include "rv_api.h"
#include "rv_menus.h"
#include "rv_ui.h"

static const struct rv_api *api;
static rv_extension_handle self;

static const char *const about[] = {"Hello from an extension."};

static const struct rv_screen screens[] = {
    RV_SCREEN_TEXT("about", "Hello", about),
};

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_LEGAL, RV_ROW_PUSH("Hello", "about"),
                  .caption = "Says hello"),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension, RV_SCREENS(screens), RV_ENTRIES(entries),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    return &extension;
}
```

```sh
make SDK=/path/to/revola-sdk
```

Copy `build/hello.rvx` to `/Revola/Extensions/hello.rvx` on the iPod, or onto an emulator disk with `ipodvm disk --copy`. Settings gets a Hello row after Legal. The fixture extensions under `sdk/examples/` of the revola repository show hooks, settings, two modules and every screen kind.
