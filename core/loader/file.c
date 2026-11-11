/* SPDX-License-Identifier: GPL-3.0-only */

#include "loader/file.h"
#include "osos-2.0.5.h"

#define FORK_DATA 0x64617461u /* 'data' */

struct path {
    uint16_t volume;
    char text[258];
};

static void path_from(struct path *path, const char *text)
{
    path->volume = 0;
    uint32_t i = 0;
    for (; text[i] && i < sizeof path->text - 1; i++)
        path->text[i] = text[i];
    path->text[i] = 0;
}

int core_file_open_status(const char *text, int mode, void **handle)
{
    struct path path;
    path_from(&path, text);
    return File_OpenLegacyPath(&path, FORK_DATA, mode, handle);
}

int core_file_open(const char *text, int mode, void **handle)
{
    return core_file_open_status(text, mode, handle) == 0;
}

int core_file_delete(const char *text)
{
    struct path path;
    path_from(&path, text);
    return File_DeleteLegacyPath(&path) == 0;
}

void core_file_list(const char *folder, void (*each)(const char *name, int is_folder, void *arg),
                    void *arg)
{
    uint32_t iterator_words[DirectoryIterator_bytes / 4];
    uint32_t path_words[NativePath_bytes / 4];
    struct DirectoryIterator *iterator = (struct DirectoryIterator *)iterator_words;
    struct NativePath *path = (struct NativePath *)path_words;
    uint8_t is_folder = 0;
    NativePath_InitEmpty(path);
    DirectoryIterator_Init(iterator, folder, 0, 0);
    while (DirectoryIterator_Next(iterator, path, &is_folder))
        each(NativeString_GetUtf8((struct NativeString *)path), is_folder, arg);
    DirectoryIterator_Destroy(iterator);
    NativePath_Destroy(path);
}

int core_file_read_status(void *handle, void *buffer, uint32_t bytes, uint32_t *got)
{
    *got = 0;
    return File_ReadLegacyHandle(handle, buffer, bytes, 0, got);
}

int core_file_read(void *handle, void *buffer, uint32_t bytes, uint32_t *got)
{
    return core_file_read_status(handle, buffer, bytes, got) == 0;
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

int core_file_create_directory(const char *path)
{
    return Directory_CreateWithParents(path, 0) == 0;
}
