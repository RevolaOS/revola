/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/files.c by lgreitis. */

#include "osos-2.0.5.h"
#include "platform.h"

#ifndef RV_APP_ROOT
#error "RV_APP_ROOT names the folder the package is installed under"
#endif

#define FORK_DATA 0x64617461u /* 'data' */

struct rv_app_file {
    void *handle;
    uint32_t size;
    uint32_t position;
};

static struct rv_app_file files[12];

int rv_app_make_path(struct rv_app_path *path, const char *name, int allow_root)
{
    *path = (struct rv_app_path){0, RV_APP_ROOT};

    unsigned i = sizeof(RV_APP_ROOT) - 1;
    const char *id = rv_app_application.package_id;
    if (!id || !*id || *id == '.')
        return -1;

    for (; *id; ++id) {
        if (*id == '/' || *id == '\\' || *id == ':' || i >= sizeof(path->text) - 2)
            return -1;
        path->text[i++] = *id;
    }
    if (!name)
        return -1;
    while (name[0] == '.' && name[1] == '/')
        name += 2;
    if (!*name) {
        path->text[i] = 0;
        return allow_root ? 0 : -1;
    }
    if (*name == '/')
        return -1;
    path->text[i++] = '/';

    const char *component = name;
    for (; *name; ++name) {
        if (name == component &&
            (*name == '.' && (name[1] == '/' || name[1] == 0 ||
                              (name[1] == '.' && (name[2] == '/' || name[2] == 0)))))
            return -1;
        if (*name == '\\' || *name == ':' || i >= sizeof(path->text) - 1)
            return -1;
        if (*name == '/') {
            if (name == component || !name[1])
                return -1;
            component = name + 1;
        }
        path->text[i++] = *name;
    }
    path->text[i] = 0;

    return 0;
}

struct rv_app_file *rv_app_file_open(const char *name)
{
    struct rv_app_path path;
    if (rv_app_make_path(&path, name, 0) < 0)
        return NULL;

    for (unsigned slot = 0; slot < sizeof(files) / sizeof(files[0]); ++slot) {
        struct rv_app_file *file = &files[slot];
        if (file->handle)
            continue;

        if (File_OpenLegacyPath(&path, FORK_DATA, 1, &file->handle) != 0) {
            file->handle = NULL;
            return NULL;
        }
        if (File_GetLegacyHandleSize(file->handle, &file->size) != 0) {
            rv_app_file_close(file);
            return NULL;
        }
        file->position = 0;
        return file;
    }

    return NULL;
}

int rv_app_file_read(struct rv_app_file *file, void *buffer, size_t size)
{
    uint32_t read = 0;

    if (!file || !file->handle || (!buffer && size))
        return -1;
    if (size > file->size - file->position)
        size = file->size - file->position;
    if (!size)
        return 0;

    int result = File_ReadLegacyHandle(file->handle, buffer, size, 0, &read);
    if (read > size)
        return -1;
    file->position += read;
    return read ? (int)read : (result ? -1 : 0);
}

int rv_app_file_seek(struct rv_app_file *file, uint32_t offset)
{
    if (!file || !file->handle || offset > file->size ||
        File_SeekLegacyHandle(file->handle, offset) != 0)
        return -1;
    file->position = offset;
    return 0;
}

uint32_t rv_app_file_size(const struct rv_app_file *file)
{
    return file->size;
}

uint32_t rv_app_file_position(const struct rv_app_file *file)
{
    return file->position;
}

void rv_app_file_close(struct rv_app_file *file)
{
    if (file && file->handle) {
        File_CloseLegacyHandle(file->handle);
        file->handle = NULL;
    }
}

void rv_app_files_close_all(void)
{
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
        rv_app_file_close(&files[i]);
}
