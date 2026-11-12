# Extension loader

An extension is one file, `/Revola/Extensions/<id>.rvx`. Installing, sharing and removing it is copying or deleting that file. `core/loader/loader.c` reads the files, loads modules into the Apple heap, binds their hooks and unloads them again.

## The `.rvx` file

`tools/rvx.py` (the SDK's `rvx-pack`) reads `manifest.toml` through `tools/manifest.py` and writes the manifest, every module and every asset into one file. The layout is in `core/loader/rvx.h`, in the order the core reads it.

| Part | Contents |
|---|---|
| Header | format, core API major, firmware build id; the loader refuses any mismatch |
| Manifest | id, name, description, version, author, license, API, enabled flag, stability, note, modules, raw sites, assets; the format is described at the top of `tools/manifest.py` |
| Catalog | one entry per module and asset: module i is entry i, resident assets follow, then the rest |
| Payloads | module images with their relocation tables, then the assets |

A module image is read into one block of the Apple heap, text, data and bss in a row. The relocation table is consumed through the stack in chunks, so a loaded module costs text + data + bss plus an 8-byte owner header. Every block the loader or `api->alloc` hands out carries that header, which is what makes the resident and peak figures exact.

## Boot scan

`core_loader_boot` runs in `core_preferences_init` after the settings store loaded. It lists the `.rvx` files once, in a heap block as long as the folder, freed when the scan ends, reads each manifest and keeps nothing for a disabled extension. For an enabled one it allocates a record holding the manifest and loads the main module. The module's entry returns the descriptor, whose hooks are bound in order. A collision unbinds what was bound, drops the image and leaves the failure and the site id on the record for Settings > Extensions.

## Identity

The FAT file name is only used to build paths. Identity is the manifest id, and its hash is the settings owner, because a short name may come back in upper case. Renaming a file keeps its settings. Of several files with the same id, the one named after it loads, else the first found; Settings > Extensions lists the others as Duplicate.

## Lifecycle

| Module trigger | Loads | Unloads |
|---|---|---|
| `always` (the main module) | while the extension is enabled | when it is disabled |
| `manual` | `api->module_load` | `api->module_unload` |
| `screen` | when a push looks up one of the screen ids its manifest entry lists (`core_loader_screen`) | ten ticks after the last of its screens was popped |

Every module may return a descriptor. The core binds and registers a non-main module's hooks and screens when it loads and drops them when it unloads, keeping its screen records stale so the next load reuses their ids. Screen lookups and open counts are per extension.

Disabling marks the record; the next grace tick unloads it. Code and hook storage are retired and freed two ticks after unbind, when no task can still be inside them. Images are owned by the Apple heap, so a reload reuses the same addresses and the heap totals return to where they were. The self-test checks this.

## Rescan after disk mode

When USB disk mode ends (site `DiskModeExit_PopToMainScreen_call`, before the main menu returns), the core scans `/Revola/Extensions/` again. An extension whose file is gone, is now a duplicate or has changed (the header's body hash differs) unloads; enabled files without a record load. A changed extension loads into a new id block, because Apple keeps `ITEM` tables and built screens by id. The old block stays unused until the next boot.

## Assets

Assets are named bytes in the file: bitmaps, converted from PNG by `tools/bitmap.py`, and data. Bitmaps are always lz-compressed (`core/loader/lz.c`, 4 KB window). Data is compressed unless the manifest says `compress = false`; anything lz cannot shrink is stored raw.

| Asset | Where it lives |
|---|---|
| resident | read into one block with the main module, as stored, so using it needs no disk access |
| other | found by streaming the catalog through the stack when asked for |

`api->asset_load` decodes a copy; `api->asset_read` reads part of a raw one. Bitmaps are decoded in `core/ui/bitmaps.c`. A menu bitmap (the stock Settings pane icon format, a 78 x 121 coverage mask) decodes into one block that lives until Apple asks for the next one, because Apple keeps drawing the last pane bitmap. Canvas bitmaps decode into blocks held while their canvas is open. Every block is counted to its extension.
