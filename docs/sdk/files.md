# Files

Each extension has a data folder, `/Revola/Data/<id>/`. Paths cannot leave it, and the core closes what an extension left open when it unloads. Anything else on the disk is reached through Apple's file functions in the symbols header.

| Call | |
|---|---|
| `api->file_open(self, name, mode, &file)` | opens a path inside the folder for reading or writing; writing creates the folders above it |
| `api->file_read`, `api->file_write`, `api->file_seek`, `api->file_close` | work on the handle |
| `api->file_delete` | removes a file |
| `api->folder_list` | lists a folder |
