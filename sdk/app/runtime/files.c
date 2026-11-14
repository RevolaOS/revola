/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/files.c by lgreitis. */

#include "osos-2.0.5.h"
#include "platform.h"

struct rv_app_file {
    uint32_t handle;
    uint32_t size;
    uint32_t position;
    uint8_t open;
    uint8_t writable;
};

static struct rv_app_file files[12];

/* 0 when `name` stays inside a folder: no leading slash, no empty, "." or ".."
 * component, no trailing slash, no backslash or colon. */
static int name_is_inside(const char *name)
{
    if (*name == '/')
        return -1;
    const char *component = name;
    for (; *name; ++name) {
        if (name == component &&
            (*name == '.' && (name[1] == '/' || name[1] == 0 ||
                              (name[1] == '.' && (name[2] == '/' || name[2] == 0)))))
            return -1;
        if (*name == '\\' || *name == ':')
            return -1;
        if (*name == '/') {
            if (name == component || !name[1])
                return -1;
            component = name + 1;
        }
    }
    return 0;
}

const char *rv_app_check_name(const char *name, int allow_root)
{
    if (!name)
        return NULL;
    while (name[0] == '.' && name[1] == '/')
        name += 2;
    if (!*name)
        return allow_root ? name : NULL;
    return name_is_inside(name) < 0 ? NULL : name;
}

static void close_handle(uint32_t handle)
{
    struct GameFileRequest *request = rv_app_request_new(GameFileRequest_Close);
    request->handle = handle;
    rv_app_request_finish(request, GameFile_QueueClose(request), 0);
}

/* The host's file `name` of the package folder `directory_kind`; the host
 * creates a missing file and its folders when read_only is 0. */
static struct rv_app_file *open_name(uint32_t directory_kind, const char *name, int read_only)
{
    struct rv_app_file *file = NULL;
    for (unsigned slot = 0; slot < sizeof(files) / sizeof(files[0]) && !file; ++slot)
        if (!files[slot].open)
            file = &files[slot];
    if (!file)
        return NULL;

    struct GameFileRequest *request = rv_app_request_new(GameFileRequest_Open);
    if (rv_app_request_finish(request,
                              GameFile_QueueOpen(directory_kind, name, read_only, request),
                              RV_APP_REQUEST_SIZE | RV_APP_REQUEST_HANDLE) != FileStatus_Ok) {
        /* Left open, the handle blocks a later write open of the same path
         * with a sharing conflict the host reports as success with garbage
         * outputs. */
        if (request->handle != RV_APP_REQUEST_PENDING)
            close_handle(request->handle);
        return NULL;
    }
    file->handle = request->handle;
    file->size = request->size;
    file->position = 0;
    file->open = 1;
    file->writable = !read_only;
    return file;
}

struct rv_app_file *rv_app_file_open(const char *name)
{
    name = rv_app_check_name(name, 0);
    return name ? open_name(GameDirectory_Package, name, 1) : NULL;
}

static int delete_name(uint32_t directory_kind, const char *name)
{
    struct GameFileRequest *request = rv_app_request_new(GameFileRequest_Delete);
    return rv_app_request_finish(request, GameFile_QueueDelete(directory_kind, name, request),
                                 0) == FileStatus_Ok
               ? 0
               : -1;
}

struct rv_app_file *rv_app_data_open(const char *name, enum rv_app_file_mode mode)
{
    name = rv_app_check_name(name, 0);
    if (!name || (mode != RV_APP_FILE_READ && mode != RV_APP_FILE_WRITE))
        return NULL;
    /* Whether the host truncates a file opened for writing is not known, so
     * the old file goes first; a missing one is not the caller's failure. */
    if (mode == RV_APP_FILE_WRITE) {
        uint32_t status = rv_app_file_error_status;
        delete_name(GameDirectory_UserData, name);
        rv_app_file_error_status = status;
    }
    return open_name(GameDirectory_UserData, name, mode == RV_APP_FILE_READ);
}

int rv_app_data_delete(const char *name)
{
    name = rv_app_check_name(name, 0);
    return name ? delete_name(GameDirectory_UserData, name) : -1;
}

/* A read, write or seek on the file's handle; the request's transferred count,
 * or -1 when the host refused. A read reports the count, a write the count
 * and the file's size, a seek only the result. */
static int transfer(struct rv_app_file *file, uint32_t type, const void *buffer, uint32_t length,
                    uint32_t offset)
{
    struct GameFileRequest *request = rv_app_request_new(type);
    request->handle = file->handle;
    request->buffer = (uint32_t)(uintptr_t)buffer;
    request->length = length;
    request->offset = (int32_t)offset;
    request->whence = FileSeek_Start;
    unsigned outputs = type == GameFileRequest_Read    ? RV_APP_REQUEST_TRANSFERRED
                       : type == GameFileRequest_Write ? RV_APP_REQUEST_TRANSFERRED |
                                                             RV_APP_REQUEST_SIZE
                                                       : 0;
    if (rv_app_request_finish(request, GameFile_QueueTransfer(request), outputs) != FileStatus_Ok)
        return -1;
    return type == GameFileRequest_Seek ? 0 : (int)request->transferred;
}

int rv_app_file_write(struct rv_app_file *file, const void *data, size_t size)
{
    if (!file || !file->open || !file->writable || (!data && size) || size > INT32_MAX)
        return -1;
    if (!size)
        return 0;
    int written = transfer(file, GameFileRequest_Write, data, size, 0);
    if (written != (int)size)
        return -1;
    file->position += written;
    if (file->position > file->size)
        file->size = file->position;
    return written;
}

int rv_app_file_read(struct rv_app_file *file, void *buffer, size_t size)
{
    if (!file || !file->open || (!buffer && size))
        return -1;
    if (size > file->size - file->position)
        size = file->size - file->position;
    if (!size)
        return 0;
    int read = transfer(file, GameFileRequest_Read, buffer, size, 0);
    if (read < 0 || (size_t)read > size)
        return -1;
    file->position += read;
    return read;
}

int rv_app_file_seek(struct rv_app_file *file, uint32_t offset)
{
    if (!file || !file->open || offset > file->size || offset > INT32_MAX ||
        transfer(file, GameFileRequest_Seek, NULL, 0, offset) < 0)
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
    if (!file || !file->open)
        return;
    file->open = 0;
    close_handle(file->handle);
}

void rv_app_files_close_all(void)
{
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
        rv_app_file_close(&files[i]);
}
