# Assets

`[[asset]]` tables in `manifest.toml` put files into the `.rvx`. A `.png` becomes a bitmap, anything else data.

| Key | Meaning |
|---|---|
| `name` | how the extension refers to it, `[a-z0-9_-]{1,31}` |
| `file` | the source file |
| `preload` | when the asset is read into RAM, as stored, with the main module: `always`, for what a hook needs without disk access, such as a small table; `optional` (the default), when Settings > Extensions > Optimize prefers speed over memory; `never` (the default with `compress = false`), read from the file when asked for, for what is large or rarely used |
| `pane` | what the bitmap is for a menu pane, an entry's `.bitmap` or `.badge`: `"icon"`, a Settings icon like the stock ones, a 78 x 121 coverage mask the system draws in its own colour, taken from the PNG's alpha, or its gray level when it is opaque grayscale; `"picture"`, a colour picture of one of the pane sizes, 119 x 119, 142 x 87, 130 x 128, 146 x 110 or 132 x 145, which picks the pane's shape (see [Menu placement](menus.md)); `"badge"`, the 20 x 16 badge before the 146 x 110 pane's count line. The packer rejects another size |
| `compress = false` | data only: stored raw, so `asset_read` can seek in it, and `preload` must be `never`; bitmaps are always compressed |

| Call | |
|---|---|
| `api->asset_load(self, name, &bytes)` | a decoded copy, freed with `api->free` |
| `api->asset_read(self, name, offset, out, bytes)` | part of an asset stored with `compress = false` |
| `api->asset_open(self, name, &file)` | such an asset, with `preload = "never"`, as a file handle for `file_read`, `file_seek` and `file_close`, offsets from its start; for streaming through a large one |
| `RV_BITMAP("name")` | a bitmap for `gfx->bitmap` and entry `.bitmap` fields |

The core decodes only the pane picture shown and keeps a canvas's bitmaps decoded while the canvas is open, so `gfx->bitmap` costs a decode on the first draw only. Everything decoded is counted against the extension.

## Asset provider

A theme that replaces artwork can replace every extension's bitmap assets too, Settings icons and canvas bitmaps alike, with `api->asset_set_provider(self, &provider)`. The provider is a `struct rv_asset_provider` of three optional functions the core calls with the owner extension's id hash and the asset's name hash (the core's 32-bit FNV-1a of the strings; the core's own bitmaps are owned by the hash of `"revola"`).

Before the core decodes a bitmap it asks `replacement` whether the theme has one for that asset and how it is stored (`struct rv_asset_stream`: raw or one lz stream in the package format, its stored and decoded sizes), then `replacement_read` for the stored bytes, and decodes them itself into a whole BMap with its 28-byte header, as it would the extension's own.

When the theme has none, the core decodes the extension's own bitmap and offers it to `adjust`, which may alter it in place.

A replacement is counted to the holder's memory, not the asset owner's. One extension holds the provider at a time; NULL releases it, as does unloading the module or freeing the block that holds the table. Setting or releasing it decodes the bitmaps on screen again.

The Appearance extension is its one holder: a theme ships extension bitmaps under `extensions/<id>/<asset>.png`, which it reads from the theme file when asked, and trims the reflection of the Settings icons it does not replace when the theme asks for flat icons. An extension does nothing for this; its assets, `RV_BITMAP` entries and `gfx->bitmap` calls stay as they are.
