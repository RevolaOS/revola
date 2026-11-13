/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/files.c by lgreitis. */

#include "osos-2.0.5.h"
#include "platform.h"

#ifndef RV_APP_ROOT
#error "RV_APP_ROOT names the folder the package is installed under"
#endif
#ifndef RV_APP_DATA_ROOT
#error "RV_APP_DATA_ROOT names the folder the package's data lives under"
#endif

#define FORK_DATA 0x64617461u /* 'data' */

struct rv_app_file {
    void *handle;
    uint32_t size;
    uint32_t position;
    uint8_t writable;
};

static struct rv_app_file files[12];

/* `root`, the package id and `name` into `path`, with the package's rules. */
static int make_path(struct rv_app_path *path, const char *root, const char *name,
                     int allow_root)
{
    path->volume = 0;
    unsigned i = 0;
    for (; root[i]; ++i)
        path->text[i] = root[i];

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

int rv_app_make_path(struct rv_app_path *path, const char *name, int allow_root)
{
    return make_path(path, RV_APP_ROOT, name, allow_root);
}

static struct rv_app_file *open_path(struct rv_app_path *path, enum rv_app_file_mode mode)
{
    for (unsigned slot = 0; slot < sizeof(files) / sizeof(files[0]); ++slot) {
        struct rv_app_file *file = &files[slot];
        if (file->handle)
            continue;

        if (File_OpenLegacyPath(path, FORK_DATA, mode, &file->handle) != 0) {
            file->handle = NULL;
            return NULL;
        }
        if (File_GetLegacyHandleSize(file->handle, &file->size) != 0) {
            rv_app_file_close(file);
            return NULL;
        }
        file->position = 0;
        file->writable = mode == RV_APP_FILE_WRITE;
        return file;
    }

    return NULL;
}

struct rv_app_file *rv_app_file_open(const char *name)
{
    struct rv_app_path path;
    if (make_path(&path, RV_APP_ROOT, name, 0) < 0)
        return NULL;
    return open_path(&path, RV_APP_FILE_READ);
}

/* Creates the folder above the file in `path`, which make_path built: the
 * root ends in a slash, so there is one. */
static void make_parents(struct rv_app_path *path)
{
    char *slash = NULL;
    for (char *c = path->text; *c; ++c)
        if (*c == '/')
            slash = c;
    *slash = 0;
    Directory_CreateWithParents(path->text, 0);
    *slash = '/';
}

struct rv_app_file *rv_app_data_open(const char *name, enum rv_app_file_mode mode)
{
    struct rv_app_path path;
    if ((mode != RV_APP_FILE_READ && mode != RV_APP_FILE_WRITE) ||
        make_path(&path, RV_APP_DATA_ROOT, name, 0) < 0)
        return NULL;
    if (mode == RV_APP_FILE_WRITE) {
        make_parents(&path);
        File_DeleteLegacyPath(&path);
    }
    return open_path(&path, mode);
}

int rv_app_data_delete(const char *name)
{
    struct rv_app_path path;
    if (make_path(&path, RV_APP_DATA_ROOT, name, 0) < 0)
        return -1;
    return File_DeleteLegacyPath(&path) == 0 ? 0 : -1;
}

int rv_app_file_write(struct rv_app_file *file, const void *data, size_t size)
{
    uint32_t written = 0;

    if (!file || !file->handle || !file->writable || (!data && size))
        return -1;
    if (!size)
        return 0;
    if (File_WriteLegacyHandle(file->handle, data, size, 1, &written) != 0 || written != size)
        return -1;
    file->position += written;
    if (file->position > file->size)
        file->size = file->position;
    return (int)written;
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
