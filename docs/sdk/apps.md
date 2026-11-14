# Apps and games

An app is a flat image Apple's eApp host loads into its own 1 MiB mapping and drives with a frame loop, the same host that runs the iPod games from iTunes. The SDK's `app/` directory holds the runtime an app links, `rv_app.h`, `rv_app.mk` and `eapp-pack`. A package is a folder with the image, a `Manifest.plist` and a 55 x 55 icon; the Homebrew extension lists it under Extras > Games from `/Revola/Games/<id>/` or under Extras > Apps from `/Revola/Apps/<id>/` and lets the host load it without an Apple signature. The runtime is linked into the image and imports the host's services the way Apple's games do, by module name through the image's import list; an extension is not involved while the app runs, and nothing in the image names a RevolaOS path or function. The same package runs from `iPod_Control/games_RO/<id>/` on a firmware that only skips the signature check.

Ported from the [RepriseOS game SDK](https://github.com/lgreitis/osos-lab/tree/main/game-sdk) by lgreitis.

## An app

`main.c` defines `rv_app_application`: the package id, the folder name under the root, and up to four callbacks. Every callback runs on the host's task; `frame` draws into the RGB565 buffer it is handed and returns.

```c
#include "rv_app.h"
#include "rv_app_ui.h"

static void input(const struct rv_app_input_event *event)
{
    if (event->button == RV_APP_BUTTON_MENU && event->type == RV_APP_INPUT_PRESS)
        rv_app_request_exit();
}

static void frame(const struct rv_app_frame *frame)
{
    rv_app_clear(&frame->framebuffer, 0);
    rv_app_draw_text(&frame->framebuffer, 20, 20, "HELLO", 0xffff, 1);
}

const struct rv_app rv_app_application = {
    .package_id = "hello", .input = input, .frame = frame,
};
```

`Makefile` names the package and includes `rv_app.mk`:

```make
ID := hello
NAME := Hello
KIND := game          # game for /Revola/Games/, app for /Revola/Apps/
SOURCES := main.c
ICON := icon.png
include $(SDK)/app/rv_app.mk
```

```sh
make SDK=/path/to/revola-sdk
```

`build/hello/` is the package. Copy it to `/Revola/Games/hello/` on the iPod (or `/Revola/Apps/hello/` for `KIND := app`), or onto an emulator disk with `ipodvm disk --copy`. The runtime asks the host where the package is, so the folder decides: `KIND` only names the root `eapp-pack --zip` lays the folder out under. Ids share one namespace with extensions.

| Variable | |
|---|---|
| `ID`, `NAME` | the folder name and what Extras lists |
| `KIND` | `game` or `app` |
| `SOURCES` | C sources, built freestanding without a C library |
| `ICON` | a 55 x 55 8-bit RGB or RGBA PNG, `eapp-pack` converts it |
| `HEAP_KIB` | the heap the host gives the app, 5120 by default |
| `FILES` | data files copied into the package, read by their names |

## What the runtime provides

| Area | Calls |
|---|---|
| Drawing | `frame->framebuffer`: 320 x 240 RGB565 pixels, borrowed for one `frame` call and shown when it returns; `rv_app_draw.h` draws on it, below |
| Buttons | `input` receives Menu, Center, Next, Previous and Play presses and releases with a millisecond timestamp |
| Wheel | `rv_app_read_wheel` (clockwise delta since the last read, position, touched), `rv_app_wheel_directions` (eight sectors as Up, Right, Down, Left bits) |
| Hold | `rv_app_hold_locked` |
| Heap | `rv_app_alloc`, `rv_app_realloc`, `rv_app_free` from the host's heap, at most 8 MiB per block, all freed when the app ends |
| Time | `rv_app_ticks_ms` since the first call, `rv_app_sleep_ms` |
| Random | `rv_app_random` (xorshift32, 32 bits per call), `rv_app_random_seed`; the stream starts the same every run until seeded, say from the first input's timestamp |
| Files | `rv_app_file_open`, `read`, `seek`, `size`, `position`, `close`: read only, paths inside the package, twelve open at once with the data files |
| Data | `rv_app_data_open` opens a path inside the package's data folder for reading or writing, `rv_app_file_write` on a handle opened for writing, `rv_app_data_delete`; writing creates the folder and replaces the file, and the bytes are on disk when the handle is closed. The host names the folder: `/Revola/Data/Games/<id>/` or `/Revola/Data/Apps/<id>/` under RevolaOS, where Homebrew redirects it, and Apple's `iPod_Control/gamedata_RW/<id>/` anywhere else |
| Saves | `rv_app_save_open`, `read`, `write`, `size`, `close`: Apple's save store for an app with little to keep, a file of the data folder whose contents the host holds in memory and writes at the next system save or app launch, not when the app exits. Four slots exist on the device, 100 KiB together; `write` replaces the contents. Anything larger, or that must be on disk when the app exits, is a data file |
| Folders | `rv_app_directory_open` (recursive or not), `read`, `close`: four open at once; entry names are relative to the folder opened, subfolders are walked depth first, a folder lists at most 500 entries |
| Errors | `rv_app_file_error`: the host's status of the last file, data, folder or save call that failed, as the `FileStatus_*` and `GameSaveStatus_*` values of the symbols header |
| Menus | `rv_app_ui.h`: `rv_app_menu_input`, `rv_app_menu_scroll`, `rv_app_menu_draw` draw a text list; `rv_app_file_picker_open` lists up to 64 files with an extension, sorted |
| Exit | `rv_app_request_exit` ends the app after the current callback |

TODO: audio.

## Drawing

`rv_app_draw.h` works on the framebuffer directly. Colours are RGB565. Every call clips to the framebuffer and takes coordinates outside it.

| Call | Draws |
|---|---|
| `rv_app_clear(fb, color)` | the whole framebuffer |
| `rv_app_fill_rect(fb, x, y, width, height, color)`, `rv_app_draw_rect(...)` | a filled rectangle, or its 1 px outline just inside |
| `rv_app_draw_pixel(fb, x, y, color)` | one pixel |
| `rv_app_draw_line(fb, x0, y0, x1, y1, color)` | a 1 px line, both ends included |
| `rv_app_fill_triangle(fb, x0, y0, x1, y1, x2, y2, color)` | a filled triangle |
| `rv_app_draw_circle(fb, cx, cy, radius, color)`, `rv_app_fill_circle(...)` | a 1 px circle, or a filled one |
| `rv_app_draw_round_rect(fb, x, y, width, height, radius, color)`, `rv_app_fill_round_rect(...)` | a rectangle with quarter-circle corners, outlined or filled |
| `rv_app_draw_bitmap(fb, x, y, &bitmap)` | a `struct rv_app_bitmap`: RGB565 pixels with a width, height and stride, and a `transparent` colour that is skipped (-1 for none) |
| `rv_app_draw_bitmap_part(fb, x, y, &bitmap, source_x, source_y, width, height)` | a part of the bitmap, for a sprite sheet |
| `rv_app_draw_mono_bitmap(fb, x, y, bits, width, height, color)` | set bits in `color`: one bit per pixel, most significant first, rows padded to a byte |
| `rv_app_draw_text(fb, x, y, text, color, scale)` | text in the SDK's 5 x 7 font, every font pixel `scale` pixels: digits, upper-case letters, `-`, `+`, `:`; lower case drawn as upper, other characters leave their cell empty |
| `rv_app_draw_text_in(fb, x, y, width, text, color, scale, align)` | the same text at the left, centre or right of the `width` pixels from `x` (`RV_APP_ALIGN_LEFT`, `CENTER`, `RIGHT`) |
| `rv_app_text_width(text, scale)` | the pixels a text spans: glyph cells 6 font pixels apart, no gap after the last |

## With a C library

An app that needs newlib compiles `runtime/newlib_glue.c` with its own sources and supplies `rv_app_libc_write` and `rv_app_libc_exit` from `rv_app_libc.h`. The glue maps `malloc` to the heap, `open` (read only) to the package's files, `write` on the standard streams to `rv_app_libc_write` and `exit` to `rv_app_libc_exit`, which must unwind to the current callback: the host owns the task and its stack. Such an app writes its own link rule with `RV_APP_ARCH_FLAGS`, `RV_APP_CFLAGS`, `RV_APP_LDFLAGS`, `$(RV_APP_STARTUP)` and `$(RV_APP_RUNTIME)`, links `-lgcc` and its libraries, runs `objcopy -O binary` and then `eapp-pack` with `--symbols` naming the SDK's `osos-2.0.5.h`, where the image checks come from.

## The image and the package

`runtime/rv_app.lds` links the image at the host's mapping, 1 MiB for code, data and bss, with the eApp header first. `runtime/imports.def` lists the host's modules and their functions in the host's order; `runtime/imports.S` turns it into the import list the header points at, one jump stub per function that the host fills in when it loads the image. Every host call goes through a stub, so the image holds no firmware address; an unresolved symbol at link time is a call that bypasses the import list.

Files and folders are requests to the host's file worker, which runs on its own task and hands finished requests back with a later frame. The runtime keeps the calls synchronous: it waits for a request's outputs, yielding to the worker in between. The requests are a small ring that is never freed, because the worker links a finished request to the next one after the fact (`runtime/request.c`). `eapp-pack` checks the header, the entries and the import list, converts the icon, writes `Manifest.plist` and the package folder; `--zip` adds a ZIP with the folder under `Revola/Games/<id>/` or `Revola/Apps/<id>/` for sharing. Its module, `eapp_package.py`, serves an app with a packaging script of its own.

`python3 app/tests/run.py` checks the package path rules, the request lifecycle and the file, folder and save store calls against stand-ins for the host.
