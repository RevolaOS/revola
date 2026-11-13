# Assets

`[[asset]]` tables in `manifest.toml` put files into the `.rvx`. A `.png` becomes a bitmap, anything else data.

| Key | Meaning |
|---|---|
| `name` | how the extension refers to it, `[a-z0-9_-]{1,31}` |
| `file` | the source file |
| `preload` | when the asset is read into RAM, as stored, with the main module: `always`, for what a hook needs without disk access, such as a small table; `optional` (the default for `menu = true`), when the core prefers speed over memory; `never` (the default otherwise), read when asked for |
| `menu = true` | a menu bitmap, such as an entry's `.bitmap` in the Settings pane: like the stock icons, a 78 x 121 coverage mask the system draws in its own colour, taken from the PNG's alpha, or its gray level when it is opaque grayscale |
| `compress = false` | data only: stored raw, so `asset_read` can seek in it; bitmaps are always compressed |

| Call | |
|---|---|
| `api->asset_load(self, name, &bytes)` | a decoded copy, freed with `api->free` |
| `api->asset_read(self, name, offset, out, bytes)` | part of an asset stored with `compress = false` |
| `api->asset_open(self, name, &file)` | such an asset, with `preload = "never"`, as a file handle for `file_read`, `file_seek` and `file_close`, offsets from its start; for streaming through a large one |
| `RV_BITMAP("name")` | a bitmap for `gfx->bitmap` and entry `.bitmap` fields |

The core decodes only the menu bitmap shown and keeps a canvas's bitmaps decoded while the canvas is open, so `gfx->bitmap` costs a decode on the first draw only. Everything decoded is counted against the extension.

## Asset provider

A theme that replaces artwork can replace every extension's bitmap assets too, Settings icons and canvas bitmaps alike, with `api->asset_set_provider(self, &provider)`. The provider is a `struct rv_asset_provider` of three optional functions the core calls with the owner extension's id hash and the asset's name hash (the core's 32-bit FNV-1a of the strings; the core's own bitmaps are owned by the hash of `"revola"`).

Before the core decodes a bitmap it asks `replacement_bytes` for the size of the theme's bitmap for that asset, 0 for none, then `replace` for the bitmap itself into a block of that size: a whole BMap with its 28-byte header, as the core would decode it.

When the theme has none, the core decodes the extension's own bitmap and offers it to `adjust`, which may alter it in place.

A replacement is counted to the holder's memory, not the asset owner's. One extension holds the provider at a time; NULL releases it, as does unloading the module or freeing the block that holds the table. Setting or releasing it decodes the bitmaps on screen again.

The Appearance extension is its one holder: a theme ships extension bitmaps under `extensions/<id>/<asset>.png`, which it reads from the theme file when asked, and trims the reflection of the Settings icons it does not replace when the theme asks for flat icons. An extension does nothing for this; its assets, `RV_BITMAP` entries and `gfx->bitmap` calls stay as they are.
