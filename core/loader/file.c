/* SPDX-License-Identifier: GPL-3.0-only */

#include "loader/file.h"
#include "osos-2.0.5.h"

#define FORK_DATA 0x64617461u /* 'data' */

struct path {
    uint16_t volume;
    char text[258];
};

int core_file_open(const char *text, int mode, void **handle)
{
    struct path path;
    path.volume = 0;
    uint32_t i = 0;
    for (; text[i] && i < sizeof path.text - 1; i++)
        path.text[i] = text[i];
    path.text[i] = 0;
    return File_OpenLegacyPath(&path, FORK_DATA, mode, handle) == 0;
}

int core_file_read(void *handle, void *buffer, uint32_t bytes, uint32_t *got)
{
    *got = 0;
    return File_ReadLegacyHandle(handle, buffer, bytes, 0, got) == 0;
}

int core_file_seek(void *handle, uint32_t offset)
{
    return File_SeekLegacyHandle(handle, offset) == 0;
}

int core_file_write(void *handle, const void *buffer, uint32_t bytes)
{
    uint32_t written = 0;
    return File_WriteLegacyHandle(handle, buffer, bytes, 1, &written) == 0 && written == bytes;
}

void core_file_close(void *handle)
{
    File_CloseLegacyHandle(handle);
}
