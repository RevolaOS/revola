/* SPDX-License-Identifier: GPL-3.0-only */

/* Each extension's data folder, /Revola/Data/<id>/, the content folders its
 * manifest names under /Revola/, the booted firmware image, and the files it
 * opened there. Names are paths inside a folder; a content name starts with
 * the folder ("Themes/x").
 * The core keeps every open file on a list so it can close them when the
 * extension unloads. A file may be a window of a larger one, as an asset in
 * the extension's own .rvx is: reads and seeks stay inside it. */

#include "loader/data.h"
#include "loader/file.h"
#include "loader/loader.h"
#include "rv_api.h"
#include <stddef.h>

#define ROOT "/Revola/Data/"
#define CONTENT_ROOT "/Revola/"
#define FIRMWARE "/Revola/Firmware/revola.bin"
#define PATH_BYTES 128

struct rv_file {
    struct rv_file *next;
    struct rv_extension_record *owner;
    void *handle;
    uint32_t base, end, at; /* the window in the underlying file, and the position */
};

#define WHOLE_FILE 0xffffffffu /* `end` of a file that is not a window */

static struct rv_file *files;

/* 0 unless `name` is clean: not empty (except for the data folder itself when
 * `folder` is set), no leading "/", no backslash, no ".." step. */
static int clean(const char *name, int folder)
{
    if (!name || (!*name && !folder) || *name == '/')
        return 0;
    for (const char *c = name; *c; c++)
        if (*c == '\\' || (c[0] == '.' && c[1] == '.' && (c == name || c[-1] == '/') &&
                            (c[2] == '/' || !c[2])))
            return 0;
    return 1;
}

/* 1 when the first step of `name` is `folder`, case-insensitively, since FAT
 * returns names in either case. */
static int first_step_is(const char *name, const char *folder)
{
    while (*folder && core_loader_lower(*name) == core_loader_lower(*folder))
        name++, folder++;
    return !*folder && (!*name || *name == '/');
}

/* The first step of `name` is one of the owner's content folders. */
static int in_content_folder(struct rv_extension_record *owner, const char *name)
{
    const char *folder;
    for (uint32_t i = 0; (folder = core_loader_manifest_content(owner->manifest, i)); i++)
        if (first_step_is(name, folder))
            return 1;
    return 0;
}

/* `parts` joined into `path`; 0 when too long. */
static int join(const char *const *parts, uint32_t count, char *path)
{
    uint32_t n = 0;
    for (uint32_t p = 0; p < count; p++)
        for (const char *c = parts[p]; *c; c++) {
            if (n >= PATH_BYTES - 1)
                return 0;
            path[n++] = *c;
        }
    path[n] = 0;
    return 1;
}

/* ROOT, the owner's id, "/" and `name` into `path`; 0 for a bad name. `name`
 * may be "" when `folder` is set: the data folder itself. */
static int data_path(struct rv_extension_record *owner, const char *name, int folder, char *path)
{
    const char *id = core_loader_manifest_text(owner->manifest, CORE_MANIFEST_ID);
    const char *parts[] = {ROOT, id, "/", name};
    return clean(name, folder) && join(parts, 4, path);
}

/* CONTENT_ROOT and `name` into `path`; 0 for a bad name or a folder the
 * manifest does not name. */
static int content_path(struct rv_extension_record *owner, const char *name, char *path)
{
    const char *parts[] = {CONTENT_ROOT, name};
    return clean(name, 0) && in_content_folder(owner, name) && join(parts, 2, path);
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

int core_data_adopt(struct rv_extension_record *owner, void *handle, uint32_t base,
                    uint32_t bytes, struct rv_file **out)
{
    struct rv_file *f = core_heap_allocate(owner, sizeof *f);
    *out = NULL;
    if (!f)
        return RV_NO_MEMORY;
    if (base && !core_file_seek(handle, base)) {
        core_heap_free(owner, f, sizeof *f);
        return RV_NOT_FOUND;
    }
    f->owner = owner;
    f->handle = handle;
    f->base = f->at = base;
    f->end = base + bytes;
    f->next = files;
    files = f;
    *out = f;
    return RV_OK;
}

/* Opens `path` as a whole file of `owner`. */
static int open_path(struct rv_extension_record *owner, const char *path, int mode,
                     struct rv_file **out)
{
    void *handle;
    if (!core_file_open(path, mode, &handle))
        return RV_NOT_FOUND;
    int result = core_data_adopt(owner, handle, 0, WHOLE_FILE, out);
    if (result != RV_OK)
        core_file_close(handle);
    return result;
}

int core_data_open(struct rv_extension_record *owner, const char *name, int mode,
                   struct rv_file **out)
{
    char path[PATH_BYTES];
    *out = NULL;
    if ((mode != RV_FILE_READ && mode != RV_FILE_WRITE) || !data_path(owner, name, 0, path))
        return RV_BAD_ARGUMENT;
    if (mode == RV_FILE_WRITE)
        make_parents(path);
    return open_path(owner, path, mode == RV_FILE_WRITE ? CORE_FILE_WRITE : CORE_FILE_READ, out);
}

int core_data_content_open(struct rv_extension_record *owner, const char *name,
                           struct rv_file **out)
{
    char path[PATH_BYTES];
    *out = NULL;
    if (!content_path(owner, name, path))
        return RV_BAD_ARGUMENT;
    return open_path(owner, path, CORE_FILE_READ, out);
}

int core_data_firmware_open(struct rv_extension_record *owner, struct rv_file **out)
{
    return open_path(owner, FIRMWARE, CORE_FILE_READ, out);
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
    if (bytes > f->end - f->at)
        bytes = f->end - f->at;
    if (!bytes)
        return RV_OK;
    if (!core_file_read(f->handle, out, bytes, got))
        return RV_NOT_FOUND;
    f->at += *got;
    return RV_OK;
}

int core_data_write(struct rv_extension_record *owner, struct rv_file *file, const void *data,
                    uint32_t bytes)
{
    struct rv_file *f = owned(owner, file);
    if (!f || !data || f->end != WHOLE_FILE)
        return RV_BAD_ARGUMENT;
    if (!core_file_write(f->handle, data, bytes))
        return RV_NOT_FOUND;
    f->at += bytes;
    return RV_OK;
}

int core_data_seek(struct rv_extension_record *owner, struct rv_file *file, uint32_t offset)
{
    struct rv_file *f = owned(owner, file);
    if (!f || offset > f->end - f->base)
        return RV_BAD_ARGUMENT;
    if (!core_file_seek(f->handle, f->base + offset))
        return RV_NOT_FOUND;
    f->at = f->base + offset;
    return RV_OK;
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
    if (!data_path(owner, name, 0, path))
        return RV_BAD_ARGUMENT;
    return core_file_delete(path) ? RV_OK : RV_NOT_FOUND;
}

int core_data_list(struct rv_extension_record *owner, const char *name,
                   void (*each)(const char *entry, int is_folder, void *arg), void *arg)
{
    char path[PATH_BYTES];
    if (!each || !data_path(owner, name, 1, path))
        return RV_BAD_ARGUMENT;
    core_file_list(path, each, arg);
    return RV_OK;
}

int core_data_content_list(struct rv_extension_record *owner, const char *name,
                           void (*each)(const char *entry, int is_folder, void *arg), void *arg)
{
    char path[PATH_BYTES];
    if (!each || !content_path(owner, name, path))
        return RV_BAD_ARGUMENT;
    core_file_list(path, each, arg);
    return RV_OK;
}
