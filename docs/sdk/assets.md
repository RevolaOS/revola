# Assets

`[[asset]]` tables in `manifest.toml` put files into the `.rvx`. A `.png` becomes a bitmap, anything else data.

| Key | Meaning |
|---|---|
| `name` | how the extension refers to it, `[a-z0-9_-]{1,31}` |
| `file` | the source file |
| `resident = true` | in RAM, as stored, while the extension is loaded, so using it needs no disk access; for what a screen needs at once, such as icons and small tables |
| `menu = true` | a menu bitmap, such as an entry's `.bitmap` in the Settings pane: like the stock icons, a 78 x 121 coverage mask the system draws in its own colour, taken from the PNG's alpha, or its gray level when it is opaque grayscale |
| `compress = false` | data only: stored raw, so `asset_read` can seek in it; bitmaps are always compressed |

| Call | |
|---|---|
| `api->asset_load(self, name, &bytes)` | a decoded copy, freed with `api->free` |
| `api->asset_read(self, name, offset, out, bytes)` | part of an asset stored with `compress = false` |
| `RV_BITMAP("name")` | a bitmap for `gfx->bitmap` and entry `.bitmap` fields |

The core decodes only the menu bitmap shown and keeps a canvas's bitmaps decoded while the canvas is open, so `gfx->bitmap` costs a decode on the first draw only. Everything decoded is counted against the extension.
