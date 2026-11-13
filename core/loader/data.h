/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_DATA_H
#define CORE_DATA_H
#include <stdint.h>

struct rv_extension_record;
struct rv_file;

/* api->file_* and folder_list for `owner`, in its data folder. */
int core_data_open(struct rv_extension_record *owner, const char *name, int mode,
                   struct rv_file **out);
int core_data_read(struct rv_extension_record *owner, struct rv_file *file, void *out,
                   uint32_t bytes, uint32_t *got);
int core_data_write(struct rv_extension_record *owner, struct rv_file *file, const void *data,
                    uint32_t bytes);
int core_data_seek(struct rv_extension_record *owner, struct rv_file *file, uint32_t offset);
void core_data_close(struct rv_extension_record *owner, struct rv_file *file);
int core_data_delete(struct rv_extension_record *owner, const char *name);
int core_data_list(struct rv_extension_record *owner, const char *name,
                   void (*each)(const char *entry, int is_folder, void *arg), void *arg);
/* api->content_open and content_list: read-only, in the folders under /Revola/
 * the owner's manifest names; `name` starts with the folder. */
int core_data_content_open(struct rv_extension_record *owner, const char *name,
                           struct rv_file **out);
int core_data_content_list(struct rv_extension_record *owner, const char *name,
                           void (*each)(const char *entry, int is_folder, void *arg), void *arg);
/* api->firmware_open: the booted image, /Revola/Firmware/revola.bin, read-only. */
int core_data_firmware_open(struct rv_extension_record *owner, struct rv_file **out);
/* An open handle as a file of `owner` that reads `bytes` from `base`, for
 * api->asset_open; the handle is the owner's to close through core_data_close
 * from here on, and is left open when the result is not RV_OK. */
int core_data_adopt(struct rv_extension_record *owner, void *handle, uint32_t base,
                    uint32_t bytes, struct rv_file **out);
/* Closes every file `owner` left open: before it unloads. */
void core_data_close_all(struct rv_extension_record *owner);

#endif
