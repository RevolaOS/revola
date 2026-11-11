/* SPDX-License-Identifier: GPL-3.0-only */

/* Each extension's data folder, /Revola/Data/<id>/, and the files it opened
 * there. Names are paths inside the folder; the core keeps every open file on
 * a list so it can close them when the extension unloads. */

#include "loader/data.h"
#include "loader/file.h"
#include "loader/loader.h"
#include "rv_api.h"
#include <stddef.h>

#define ROOT "/Revola/Data/"
#define PATH_BYTES 128

struct rv_file {
    struct rv_file *next;
    struct rv_extension_record *owner;
    void *handle;
};

static struct rv_file *files;

/* ROOT, the owner's id, "/" and `name` into `path`; 0 for an empty name, a
 * leading "/", a ".." step or a path too long. `name` may be "" when
 * `folder` is set: the data folder itself. */
static int path_of(struct rv_extension_record *owner, const char *name, int folder,
                   char *path)
{
    const char *id = core_loader_manifest_text(owner->manifest, CORE_MANIFEST_ID);
    const char *parts[] = {ROOT, id, "/", name};
    uint32_t n = 0;
    if (!name || (!*name && !folder) || *name == '/')
        return 0;
    for (const char *c = name; *c; c++)
        if (c[0] == '.' && c[1] == '.' && (c == name || c[-1] == '/') &&
            (c[2] == '/' || !c[2]))
            return 0;
    for (uint32_t p = 0; p < 4; p++)
        for (const char *c = parts[p]; *c; c++) {
            if (n >= PATH_BYTES - 1)
                return 0;
            path[n++] = *c;
        }
    path[n] = 0;
    return 1;
}

/* Creates the folders above the file at `path`. */
static void make_parents(char *path)
{
    char *slash = NULL;
    for (char *c = path; *c; c++)
        if (*c == '/')
            slash = c;
    if (!slash)
        return;
    *slash = 0;
    core_file_create_directory(path);
    *slash = '/';
}

int core_data_open(struct rv_extension_record *owner, const char *name, int mode,
                   struct rv_file **out)
{
    char path[PATH_BYTES];
    *out = NULL;
    if ((mode != RV_FILE_READ && mode != RV_FILE_WRITE) || !path_of(owner, name, 0, path))
        return RV_BAD_ARGUMENT;
    struct rv_file *f = core_heap_allocate(owner, sizeof *f);
    if (!f)
        return RV_NO_MEMORY;
    if (mode == RV_FILE_WRITE)
        make_parents(path);
    if (!core_file_open(path, mode == RV_FILE_WRITE ? CORE_FILE_WRITE : CORE_FILE_READ,
                        &f->handle)) {
        core_heap_free(owner, f, sizeof *f);
        return RV_NOT_FOUND;
    }
    f->owner = owner;
    f->next = files;
    files = f;
    *out = f;
    return RV_OK;
}

/* The owner's open file, or NULL when `file` is not one. */
static struct rv_file *owned(struct rv_extension_record *owner, struct rv_file *file)
{
    for (struct rv_file *f = files; f; f = f->next)
        if (f == file && f->owner == owner)
            return f;
    return NULL;
}

int core_data_read(struct rv_extension_record *owner, struct rv_file *file, void *out,
                   uint32_t bytes, uint32_t *got)
{
    struct rv_file *f = owned(owner, file);
    *got = 0;
    if (!f || !out)
        return RV_BAD_ARGUMENT;
    return core_file_read(f->handle, out, bytes, got) ? RV_OK : RV_NOT_FOUND;
}

int core_data_write(struct rv_extension_record *owner, struct rv_file *file, const void *data,
                    uint32_t bytes)
{
    struct rv_file *f = owned(owner, file);
    if (!f || !data)
        return RV_BAD_ARGUMENT;
    return core_file_write(f->handle, data, bytes) ? RV_OK : RV_NOT_FOUND;
}

int core_data_seek(struct rv_extension_record *owner, struct rv_file *file, uint32_t offset)
{
    struct rv_file *f = owned(owner, file);
    if (!f)
        return RV_BAD_ARGUMENT;
    return core_file_seek(f->handle, offset) ? RV_OK : RV_NOT_FOUND;
}

void core_data_close(struct rv_extension_record *owner, struct rv_file *file)
{
    for (struct rv_file **link = &files; *link; link = &(*link)->next) {
        struct rv_file *f = *link;
        if (f != file || f->owner != owner)
            continue;
        *link = f->next;
        core_file_close(f->handle);
        core_heap_free(owner, f, sizeof *f);
        return;
    }
}

void core_data_close_all(struct rv_extension_record *owner)
{
    for (struct rv_file **link = &files; *link;) {
        struct rv_file *f = *link;
        if (f->owner != owner) {
            link = &f->next;
            continue;
        }
        *link = f->next;
        core_file_close(f->handle);
        core_heap_free(owner, f, sizeof *f);
    }
}

int core_data_delete(struct rv_extension_record *owner, const char *name)
{
    char path[PATH_BYTES];
    if (!path_of(owner, name, 0, path))
        return RV_BAD_ARGUMENT;
    return core_file_delete(path) ? RV_OK : RV_NOT_FOUND;
}

int core_data_list(struct rv_extension_record *owner, const char *name,
                   void (*each)(const char *entry, int is_folder, void *arg), void *arg)
{
    char path[PATH_BYTES];
    if (!each || !path_of(owner, name, 1, path))
        return RV_BAD_ARGUMENT;
    core_file_list(path, each, arg);
    return RV_OK;
}
