# revola

Core firmware for RevolaOS: the patches that make Apple's iPod Classic 2.0.5 firmware load extensions, the extension loader and lifecycle, the settings store, the UI runtime and the SDK that extensions build against.

Supported devices: iPod Classic 6G, 6.5G and 7G on Apple firmware 2.0.5. FAT32 storage.

## Layout

| Path | Contents |
|---|---|
| `core/` | Firmware payload linked at `CORE_BASE`: install-time patches (`boot.c`), boot order (`init.c`), settings store (`settings.c`), grace timer (`timer.c`), panic screen (`panic.c`) |
| `core/hooks/` | Hook site table and dispatcher (`sites.def`, `sites.c`, `stubs.S`) |
| `core/loader/` | Extension loader and lifecycle (`loader.c`, `containers.c`), file access |
| `core/ui/` | Screen registry and clones (`screens.c`, `clones.c`), list screens full width or beside the Settings preview pane (`lists.c`) and their rows expanded by index (`indexed.c`), templates built at boot (`templates.c`), canvas view and slider input (`canvas.c`), the drawing table canvases draw with (`gfx.c`), controller vtable copies (`controllers.c`), menu rows (`menus.c`) and the Settings preview pane (`panes.c`), Main and Music Menu settings rows (`toggles.c`), Settings properties and actions (`properties.c`), resource helpers, About and Extensions rows |
| `core/memory/` | Memory rules (`memory.c`, `artwork.c`, `trackcount.c`) |
| `sdk/` | What extensions build against: `rv_api.h` and the `rv_ui.h` declaration helpers, the module linker script, the Make include, the fixture extensions under `examples/` |
| `firmware/` | Pinned Apple fingerprints (`apple.json`) and the symbols linker script and header from the reversing repository |
| `boot/` | Companion loader (`companion/`), Rockbox bootloader patches and the pinned Rockbox commit |
| `tools/` | Recipe builder, `rvx-pack` (`rvx.py`), manifest compiler, build and release scripts, tests |

## Build

Requires Python 3.11+, Rockbox's `arm-elf-eabi-` GCC 9.5.0 (`firmware/apple.json` pins the version),, decrypted 2.0.5 inputs produced by the installer tooling, and the `revola` CLI from the installer repository on PATH (`cargo build --release -p revola-cli`), which applies the recipe.

```sh
python3 tools/build.py osos --inputs /path/to/firmware-2.0.5 --cross-prefix /path/to/arm-elf-eabi-
python3 tools/build.py loader-recipe
cd tools && python3 -m unittest discover -s tests
```

`osos` writes `build/osos.json`, `build/osos.data` and the assembled `build/osos-cfw.bin`; `osos-recipe` stops before assembly and needs no Apple inputs, so it skips the checks of the site table against the image. `--selftest` compiles the dispatcher and loader self-test into the core (results in `core_selftest`, read with `ipodvm peek`). `examples` builds the fixture extensions in `sdk/examples` against a staged SDK into `build/examples/<id>/<id>.rvx`; `sdk` writes the SDK tarball. Boot the image in the emulator with `ipodvm launch --firmware build/osos-cfw.bin --disk <test disk>` from this directory; `ipodvm disk --copy` puts extension files on a disk clone.

The build prints the core's resident bytes; the link fails above `CORE_BYTES` (96 KiB) in `core/layout.h`.

Apple symbols: `firmware/osos-2.0.5.ld` is passed to the linker with `--just-symbols`, so C code calls Apple functions by the names declared in `firmware/osos-2.0.5.h`. Both files are copied from the reversing repository; never edit them here.

## Hook sites

Apple code runs from DRAM, so the core patches it at boot. `core/hooks/sites.def` lists every site the core can patch: address, kind (`CALL`, `VTABLE`, `ENTRY`, `ENTRY_NORETURN`, `WORD`, or `INSERT` for a call written over an ordinary instruction, where the stub preserves r0-r3 and r12), what an unbound site does, the Apple words expected there, whether several handlers may chain, and the argument count. Each site has one pointer slot and one stub in the core. Call and vtable sites are patched when the first handler binds and restored when the last unbinds; entry sites are written once by `core_boot` and fall through to a replay stub while unbound. `core_boot` runs inside Apple's UI task after the other tasks exist, so a two-word entry write is only safe for a function no started task can be executing at that moment; a site is declared `ENTRY` only with that argument. Every write runs with IRQ and FIQ masked and is followed by `core_cache_sync`.

A handler receives the Apple arguments followed by `next` (in `r[argc]`, or as the first stack argument for four-argument sites, ahead of up to two arguments Apple itself passes on the stack). A `filter` continues with `core_next_call`; a `replace` handler is exclusive and a second claim is reported as a conflict. With one handler bound the slot holds the handler itself; with more it holds a chain entry that calls them in priority order. Handler lists are published with a single store and the retired list is freed at the next grace tick.

Extensions name a site by id, the FNV-1a hash of its `sites.def` row name; the build writes the table of ids next to the site table and the same values into the SDK's `rv_sites.h`, so a row may move without breaking built extensions.

Raw sites: an extension's manifest may declare sites of its own (`[[site]]`: address, kind, expected word, argument count), bound as `RV_RAW_SITE(n)`. Only `CALL`, `VTABLE` and `WORD` with at most four register arguments and no stack arguments are allowed, and each is exclusive. The loader claims them before entering the main module: the address must lie in the OSOS image below the heap, hold the expected word, and not fall inside a core site or another extension's raw site. A refusal leaves `Site mismatch`, `Conflict` (naming the core site when one covers the address) or `No memory` on the record. The core keeps a pool of eight raw sites after the core rows; each has a 36-byte stub in the core's bss, written at the claim, that holds its own slot and root continuation and records its owner for the panic attribution. A released site is reusable two grace ticks later. First-party extensions use core sites only; raw sites are for code no core row covers yet.

## Grace tick

One native UI event timer (`core/timer.c`) fires every second on the UI task. The tick frees retired handler lists and extension images, unloads what was disabled, ages idle on-demand modules and writes a dirty settings store once the disk is powered anyway. The recipient is a core object whose vtable has only the event slot; the timer re-arms itself on every delivery.

The build tool reads the table from the linked core, rejects entry sites whose displaced words use `pc`, are conditional, branch, or are the target of a branch at `entry+4`, checks every expected word against the pinned image when Apple inputs are present, and records the same expectations in the recipe for the installer.

The install-time recipe keeps only what must hold before the core runs: the heap-base literals, the panic entries and the one call that bootstraps `core_boot`.

## Settings store

`/Revola/Settings/system.a` and `system.b` hold the same record stream, for the core and every extension: a header `{magic RVST, version, length, generation, FNV-1a of the records}` and records `{owner hash32, key16, type8, size8, data}` padded to a word. The owner is the FNV-1a hash of the extension id (`core` for the core), so there is no key registry. At boot the newer valid file wins; a flush writes the other file with the generation advanced, so an interrupted write never destroys the last good copy. The buffer is sized from the file and regrown through Apple's allocator. Unknown records survive a rewrite. Values are at most 255 bytes.

The store is loaded in `core_preferences_init`, the first point where the disk is readable. The first flush of a boot creates `/Revola/Settings` and any missing parent through `Directory_CreateWithParents`. Flush points: before light sleep (both `System_Sleep` calls), after Apple's own save-all (USB disk mode entry among its callers), three grace ticks after the last change if `Disk_GetPowerState` reports the disk powered, and `core_settings_flush` on request. Reset All Settings empties the store. Key 0 of every extension's owner is its enabled flag, and key 0xffff says which of its Main and Music rows are shown.

## Extension loader

An extension is one file, `/Revola/Extensions/<id>.rvx`: installing, sharing and removing it is copying or deleting that file. `tools/rvx.py` (the `rvx-pack` of the SDK) reads `manifest.toml` through `tools/manifest.py` and writes the manifest, every module and every asset into the file; the layout is in `core/loader/rvx.h`, in the order the core reads it. The header names the format, the core API and the firmware build id; the loader refuses any mismatch. After the manifest comes one catalog of every module and asset: module i is entry i, the resident assets follow, then the rest. The image is read into one block of the Apple heap, text, data and bss in a row; the relocation table is consumed through the stack in chunks, so a loaded module costs exactly text + data + bss plus an 8-byte owner header. Every block the loader or `api->alloc` hands out carries that header, which is what makes the resident and peak figures exact.

`core_loader_boot` runs in `core_preferences_init` after the store loaded. It lists the `.rvx` files once (at most 16), reads each manifest, and keeps nothing for a disabled extension. For an enabled one it allocates a record holding the manifest and loads the main module: entry returns the descriptor, whose hooks are bound in order. A collision unbinds what was bound, drops the image and leaves the failure and the site id on the record for Settings > Extensions. The FAT file name is only used to build paths; identity is the manifest id and its hash is the settings owner, because a short name may come back in upper case. Renaming a file therefore keeps its settings; of several files with the same id the one named after it loads (else the first found), and Settings > Extensions lists the others as Duplicate.

Lifecycle: the main module stays loaded while the extension is enabled; a `manual` module loads and unloads through `api->module_load` and `module_unload`; a `screen` module loads when one of its declared screens is pushed (`core_loader_screen_pushed`) and unloads ten ticks after the last of them was popped. Disabling marks the record and the next tick unloads it; code and hook storage are retired and freed two ticks after unbind, when no task can still be inside them. Images are owned by the Apple heap, so a reload reuses the same addresses and the heap totals return to where they were (checked by the self-test). When USB disk mode ends (site `DiskModeExit_PopToMainScreen_call`, before the main menu returns), the core scans `/Revola/Extensions/` again: an extension whose file is gone, now a duplicate or changed (the header's body hash differs) unloads, and enabled files without a record load. A changed extension loads into a new id block, because Apple keeps `ITEM` tables and built screens by id; the old block stays unused until the next boot, and with every block taken the extension shows "Restart needed".

Assets are named bytes in the file: bitmaps (converted from PNG by `tools/bitmap.py`) and data. Bitmaps are always lz-compressed (`core/loader/lz.c`, 4 KB window); data is unless the manifest says `compress = false`, and anything lz cannot shrink is stored raw. Resident assets are read into one block with the main module and stay there as stored, so using them needs no disk access; the rest are found by streaming the catalog through the stack when asked for. `api->asset_load` decodes a copy, `api->asset_read` reads part of a raw one. Bitmaps are decoded in `core/ui/bitmaps.c`: a menu bitmap (the stock Settings pane icon format, a 78 x 121 coverage mask) into one block that lives until Apple asks for the next one, because Apple keeps drawing the last pane bitmap; canvas bitmaps into blocks held while their canvas is open. Every block is counted to its extension.

Failsafe: the panic screen writes the owner of the extension whose code was running (the explicit entry call, else the image holding the faulting address, link register or any saved word) to a word at the top of IRAM that the boot chain leaves alone, and the next boot disables that extension and records it for the Extensions screen. Holding Previous while RevolaOS boots starts core-only mode: the loader reads the wheel driver's button mask, which the wheel interrupt keeps current, and loads nothing.

## SDK

`sdk/` is what extensions build against; `python3 tools/build.py sdk` stages it with the generated `rv_sites.h`, `rv_menus.h` (the stock menu rows an entry can follow, from the symbols header's `RES_*Menu_*_Item` names), `rv_firmware.h`/`.mk`, the symbols header and linker script and rvx-pack with its two modules, and writes `build/revola-sdk-<version>.tar.gz`. `sdk/README.md` describes the files. `sdk/api.lock` records the API version and the hash of every shipped file; the build and the `sdk` workflow fail when a file under `sdk/` changes at the same version, so an API change is always a visible version bump (`RV_API_MINOR` for additions, `RV_API_MAJOR` when an entry changes meaning), followed by `python3 tools/sdklock.py update`.

## Menu containers

An extension declares a submenu it wants to contribute to by id and parent (`struct rv_container`); the core deduplicates by id, keeps the first label, and refuses a second declaration with another parent. While declared, a container is a list screen of the core (`core/loader/containers.c`, `core/ui/submenus.c`) with a row in its parent menu or container, and an entry names it in `rv_entry.in`. Its screen lists the shown entries that name it in registration order and is rebuilt on its next push after entries come or go; the row shows while one of those entries shows, nested containers included, and goes with the last. Entry rows keep their own ids, so their text and actions resolve as in any menu.

A stock Settings row can move into a container under Settings (`RV_ROW_STOCK`, any row but Legal, which anchors the About rows): while Apple builds the Settings table the core leaves that row out, and the container lists a copy of it with the stock row's `CEVT` bindings. The Settings list controller class runs the container's screens, so the stock handlers work there. The bindings' layout switch is dropped, because the stock layouts carry the title "Settings", and the container's layout events carry the stock Settings layouts' `controller.Goto*` events that some stock handlers post. The row returns to Settings when its entry goes.

## About and Extensions rows

Settings ends with four About rows after Legal (the RevolaOS version, the core's resident bytes, the heap's used and free bytes), then a meter row with the bytes extensions hold, then one row per installed extension with its state (On with its resident bytes, Off, Conflict, No memory, Failed, Off after crash). All are copies of the Legal row made while Apple builds the list, with titles bound to Settings model properties the core answers, so the text is current each time it is drawn. Selecting an extension row toggles it: the core adds a `list.pid.<row>.chosen` binding to the Settings screen's event table when Apple loads it, the binding names a handler the core's Settings action filter recognises, and the toggle notifies the model so the row and the meter redraw. The installed list is read from disk once, when Settings is first built, which happens at boot. TODO: a screen of its own with the detail page is part of the UI runtime.

## Memory rules

Apple's global memory policy is adjusted at boot, each rule behind a core setting (default on) read from the store in `core_preferences_init`; `core/memory/memory.c` carries the reasoning and the numbers. Ported from RepriseOS perf/memory.

| Setting | Rule |
|---|---|
| `CORE_KEY_CACHE_BUDGET` | Size the block cache after the library import as max(heap total - import cost - reserve, floor): 14/14 MiB on a hard disk as Apple does, 24/6 MiB on flash, detected from the ATA IDENTIFY data. |
| `CORE_KEY_READAHEAD_FLOOR` | Below a 14 MiB cache the media player's read-ahead minimum becomes max(cache - 8 MiB, 2 MiB) so a game's 8 MiB still fits beside it. |
| `CORE_KEY_POOL_SLOTS` | Fixed-pool slots round to 8 bytes instead of 16 (ArtworkDB thumbnail records: 40 instead of 48). |
| `CORE_KEY_GAME_BLOCKS` | When the eApp block manager asks for a game's blocks the cache first grows by what the other clients keep, leaving 1 MiB of heap; after the release the unassigned blocks above the import size go back to the heap. |
| `CORE_KEY_POSITION_SCAN` | List positions come from scanning the order instead of Apple's address index beside it (8 bytes per entry of a play-all queue). |
| `CORE_KEY_ARTWORK_SHARE` | While the ArtworkDB loads, an image whose thumbnail chain matches one already loaded shares that chain and its record goes back to the pool; tracks that named the dropped image get the first image's id (`core/memory/artwork.c`). |

The cache budget also releases the four sorted orders the import builds and never reads (4 bytes per track each). Two changes are not settings: the track count of media-list nodes is widened to 32 bits by sixty constant words in `core/memory/trackcount.c`, written by the recipe, so libraries over 65,535 tracks import; and the core's own layout of that node (tracks at 0x2c as a word, groups at 0x1e) lives next to those words, while the symbols file keeps Apple's.

The 32 KiB UI stack is not carried until stack-watermark evidence exists on the canvas and loader paths.

## What drives development

1. **RAM first.** 64 MiB in total, about 20 MiB of heap shared with Apple's music database. Every pull request reports resident and peak bytes from the emulator heap profile. An increase needs a stated reason. A disabled extension costs zero bytes. Reuse what exists before adding anything.
2. **Performance second.** No work in audio callbacks beyond the DSP itself. No redraw of unchanged pixels. No disk reads for things that can be computed. No per-draw checks when a one-time hook can decide. Hot paths bind directly; the dispatcher runs only when two handlers share a site.
3. **Optimise before merge.** Measure, remove what measurement does not justify, and stop when the next step costs more clarity than it saves. "Works" is not the bar.
4. **Complexity matches need.** No abstraction for one caller, no layer that only forwards. Defer anything with one user, delete anything with none. A person must be able to reason about the code.
5. **Clean structure.** One concern per directory. Extensions never reach into core internals. Apple addresses come only from the generated symbols header.
6. **Measure, do not guess.** Emulator heap profile for every RAM claim; hardware for audio, cache and storage behaviour.

## Core or extension

Something is core only if the loader, SDK or settings store needs it, if it must act before the loader can run, or if the boot cannot recover without it. Everything else is an extension, however small. One extension per concern. If one feature cannot work without another, it is a module of that extension, not a dependency.

## RevolaOS and RepriseOS

RevolaOS started from the groundwork of [RepriseOS](https://github.com/lgreitis/osos-lab) by lgreitis: the boot chain, the installer's device and bundle code, and the reverse engineering of Apple's 2.0.5 firmware. You can support lgreitis on [Ko-fi](https://ko-fi.com/lgreitis).

Both run Apple's own 2.0.5 firmware with additions, on the iPod Classic 6G, 6.5G and 7G, and both install from the desktop over DFU. They take different approaches:

- **RepriseOS** is a firmware image: what it contains is settled before it goes onto the iPod.
- **RevolaOS** is a small core that loads extensions on the iPod itself. You change your setup there, in Settings, at any time, and an extension that's off takes no memory. Extensions from anyone install next to the first-party ones without changing the OS.

| | RepriseOS | RevolaOS |
|---|---|---|
| Where features are chosen | Before installing | On the iPod, in Settings |
| Adding or removing a feature | Install a different firmware | Turn an installed extension on or off; when it's off it isn't loaded and uses no RAM, leaving more for your music library |
| Third-party features | Need a modified firmware | Copy the extension's folder to `/Revola/Extensions/`; the OS stays as it is |
| Seeing memory use | Not shown | Settings shows free memory and how much each extension uses |
| After a crash | Panic screen | The next boot turns off the extension that crashed; hold Previous to start without extensions |

## License

GPL-3.0-only. See COPYING and NOTICE.
