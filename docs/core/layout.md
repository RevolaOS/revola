# Layout and boot

The core is one firmware payload linked at `CORE_BASE`, patched into Apple's 2.0.5 image by the installer and started from inside Apple's UI task. Everything under `core/` is compiled into it.

## Directories

The main files; helpers sit next to them.

| Path | Contents |
|---|---|
| `core/boot.c` | Install-time patches |
| `core/init.c` | Boot order |
| `core/settings.c` | Settings store |
| `core/timer.c` | Grace timer |
| `core/selftest.c` | Dispatcher and loader self-test, compiled in with `--selftest` |
| `core/cache.S`, `core/irq.S` | Cache maintenance and interrupt masking around patches |
| `core/core.lds.S` | Linker script of the payload |
| `core/assets/` | Bitmaps compiled into the core |
| `core/panic/` | Exception entry points, panic screen, backlight polling, sad iPod bitmap |
| `core/hooks/` | Hook site table and dispatcher: `sites.def`, `sites.c`, `stubs.S` |
| `core/loader/` | Extension loader and lifecycle (`loader.c`), `.rvx` layout (`rvx.h`), lz decoder, file access |
| `core/ui/` | Screens, lists, menus, canvases and the Settings pages, see below |
| `core/recovery.c` | Crash record in reserved IRAM and the report written at the next boot |
| `core/storage.c` | File access for the core |
| `core/memory/` | Memory rules: `memory.c`, `artwork.c`, `trackcount.c` |
| `sdk/` | What extensions build against, see [the SDK](../sdk/README.md) |
| `firmware/` | Pinned Apple fingerprints (`apple.json`) and the symbols linker script and header copied from the reversing repository |
| `boot/` | Companion loader (`companion/`), Rockbox bootloader patch and the pinned Rockbox commit |
| `tools/` | Recipe builder, `rvx-pack` (`rvx.py`), manifest compiler, build and release scripts, tests |

## `core/ui/`

| File | Contents |
|---|---|
| `screens.c`, `clones.c` | Screen registry and clones of stock screens |
| `lists.c` | List screens, full width or beside the Settings preview pane |
| `indexed.c` | List rows expanded by index |
| `templates.c` | Templates built at boot |
| `canvas.c` | Canvas view and slider input |
| `gfx.c` | The drawing table canvases draw with |
| `controllers.c` | Controller vtable copies |
| `menus.c` | Menu rows |
| `panes.c` | The Settings preview pane |
| `toggles.c` | Main Menu and Music Menu settings rows |
| `properties.c` | Settings properties and actions |
| `bitmaps.c` | Bitmap decoding |
| `resources.c`, `registry.c`, `bindings.c` | Resource ids, the screen registry and the hook bindings of core screens |
| `icons.c`, `empty.c`, `format.c`, `context.c`, `screenmenus.c` | Stock icons, the empty-list message, value formatting, `rv_context`, context menus |
| `info.c`, `extensions.c`, `usage.c` | RevolaOS Info, Settings > Extensions and Memory Usage |

## Boot order

1. The bootloader reads `/Revola/Firmware/revola.bin` and `/Revola/Firmware/loader.bin` from the data partition and starts Apple's firmware with the core in place.
2. The install-time recipe has patched only what must hold before the core runs: the heap-base literals, the panic entries and the one call that bootstraps `core_boot`.
3. `core_boot` (`core/init.c`) runs inside Apple's UI task after the other tasks exist. It writes the entry sites, binds the preferences hook and the settings store, and registers the core's screens and menu rows.
4. `core_preferences_init`, the first point where the disk is readable, runs in this order: settings store, crash record, memory rules, grace timer, RevolaOS Info and Extensions screens, then `core_loader_boot`, which loads every enabled extension.

Apple symbols: `firmware/osos-2.0.5.ld` is passed to the linker with `--just-symbols`, so C code calls Apple functions by the names declared in `firmware/osos-2.0.5.h`. Both files are copied from the reversing repository and never edited here.
