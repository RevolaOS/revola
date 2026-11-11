/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_FILE_H
#define CORE_FILE_H
#include <stdint.h>

/* Apple's file API on the data volume: open, read, seek, write, close and directory
 * creation. Paths are "/Dir/name"; a missing file fails to open. */
#define CORE_FILE_READ 1
#define CORE_FILE_WRITE 2

int core_file_open(const char *path, int mode, void **handle);
/* Native legacy status: zero success, otherwise a FileError_* or I/O error.
 * Use when absence must be distinguished from an unreadable existing file. */
int core_file_open_status(const char *path, int mode, void **handle);
/* 1 on success; *got is the byte count read, short at the end of the file. */
int core_file_read(void *handle, void *buffer, uint32_t bytes, uint32_t *got);
/* Preserves FileError_EndOfFile and the partial byte count. */
int core_file_read_status(void *handle, void *buffer, uint32_t bytes, uint32_t *got);
/* Moves to `offset` bytes from the start; 1 on success. */
int core_file_seek(void *handle, uint32_t offset);
int core_file_write(void *handle, const void *buffer, uint32_t bytes);
void core_file_close(void *handle);
/* 1 when the file is gone afterwards. */
int core_file_delete(const char *path);
/* Calls `each` with the name of every entry of `folder`, none when it is
 * missing. */
void core_file_list(const char *folder, void (*each)(const char *name, int is_folder, void *arg),
                    void *arg);
/* Creates the directory and any missing parents; 1 when it exists afterwards. */
int core_file_create_directory(const char *path);

#endif
