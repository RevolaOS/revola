# Files

Each extension has a data folder, `/Revola/Data/<id>/`. Paths cannot leave it, and the core closes what an extension left open when it unloads.

An extension that shows files a listener copies onto the iPod names the folder in its manifest, `content = "Themes"` (or a list of folders), and reads `/Revola/Themes/` through `api->content_open` and `api->content_list` with names that start with the folder, such as `Themes/flat.rvt`. A content folder is read-only and has one owner: a second enabled extension naming it does not load, and Settings > Extensions lists it as Conflict. `api->firmware_open` opens the booted firmware image, `/Revola/Firmware/revola.bin`, which is where a theme reads the stock artwork from. Handles from `content_open` and `firmware_open`, and from `asset_open` in [Assets](assets.md), take `file_read`, `file_seek` and `file_close` like a data file.

Anything else on the disk is reached through Apple's file functions in the symbols header.

| Call | |
|---|---|
| `api->file_open(self, name, mode, &file)` | opens a path inside the folder for reading or writing; writing creates the folders above it |
| `api->file_read`, `api->file_write`, `api->file_seek`, `api->file_close` | work on the handle |
| `api->file_delete` | removes a file |
| `api->folder_list` | lists a folder |
| `api->content_open(self, name, &file)` | opens a file of a content folder for reading |
| `api->content_list(self, name, each, arg)` | lists a content folder, or a folder inside it |
| `api->firmware_open(self, &file)` | opens the booted firmware image for reading |
