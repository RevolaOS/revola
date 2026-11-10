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
/* Closes every file `owner` left open: before it unloads. */
void core_data_close_all(struct rv_extension_record *owner);

#endif
