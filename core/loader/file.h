/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_FILE_H
#define CORE_FILE_H
#include <stdint.h>

/* Apple's legacy file API on the data volume: open, read, seek, write, close. Paths
 * are "/Dir/name"; a missing file fails to open. */
#define CORE_FILE_READ 1
#define CORE_FILE_WRITE 2

int core_file_open(const char *path, int mode, void **handle);
/* 1 on success; *got is the byte count read, short at the end of the file. */
int core_file_read(void *handle, void *buffer, uint32_t bytes, uint32_t *got);
/* Moves to `offset` bytes from the start; 1 on success. */
int core_file_seek(void *handle, uint32_t offset);
int core_file_write(void *handle, const void *buffer, uint32_t bytes);
void core_file_close(void *handle);

#endif
