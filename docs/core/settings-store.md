# Settings store

`/Revola/Settings/system.a` and `system.b` hold the same record stream for the core and every extension. A flush writes the other file with the generation advanced, so the loader can reject an incomplete copy and use the other one. This does not guarantee FAT integrity or media durability across a forced reset.

## Format

| Part | Fields |
|---|---|
| Header | magic `RVST`, version, length, generation, FNV-1a of the records |
| Record | owner hash32, key16, type8, size8, data, padded to a word |

The owner is the FNV-1a hash of the extension id, `core` for the core, so there is no key registry. At boot the newer valid file wins. The buffer is sized from the file and regrown through Apple's allocator. Unknown records survive a rewrite. Values are at most 255 bytes.

## Loading and flushing

The store is loaded in `core_preferences_init`, the first point where the disk is readable. The first flush of a boot creates `/Revola/Settings` and any missing parent through `Directory_CreateWithParents`.

| Flush point | |
|---|---|
| Before light sleep | both `System_Sleep` calls |
| After Apple's own save-all | USB disk mode entry is among its callers |
| Three grace ticks after the last change | only if `Disk_GetPowerState` reports the disk powered |
| `core_settings_flush` | on request |

Reset All Settings empties the store.

## Reserved keys

| Key | Owner | Meaning |
|---|---|---|
| 0 | every extension | enabled; unset means on for the ids in `loader/bundled.def`, off otherwise |
| 0xffff | every extension | which of its Main and Music rows are shown |
| `CORE_KEY_*` | core | memory rules, crashed extension, Optimize For, see `core/settings.h` |
