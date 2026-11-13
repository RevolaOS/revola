/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/tests/files_test.c by lgreitis. */

/* Host stand-ins for the Apple file and directory functions files.c and
 * directory.c call, and the rules they must keep. */

#include "platform.h"
#include "rv_app.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

const struct rv_app rv_app_application = {.package_id = "test-game"};
static unsigned opened, closed, strings, native_opens;
static char opened_path[258], expected[300], long_name[300];
static const char *result_name;

struct DirectoryIterator;
struct NativePath;
struct NativeString;

struct DirectoryIterator *DirectoryIterator_Init(struct DirectoryIterator *iterator,
                                                 const char *path, uint8_t recursive,
                                                 uint32_t volume)
{
    assert(recursive == 1 && volume == 0);
    strcpy(opened_path, path);
    *(uint32_t *)iterator = 0;
    ++opened;
    return iterator;
}

int DirectoryIterator_Next(struct DirectoryIterator *iterator, struct NativePath *path,
                           uint8_t *is_directory)
{
    (void)path;
    unsigned index = (*(uint32_t *)iterator)++;
    *is_directory = index == 1;
    result_name = index == 0 ? "roms/Example.NES" : index == 1 ? "roms" : long_name;
    return index < 3;
}

void DirectoryIterator_Destroy(struct DirectoryIterator *iterator)
{
    (void)iterator;
    ++closed;
}

struct NativePath *NativePath_InitEmpty(struct NativePath *path)
{
    ++strings;
    return path;
}

char *NativeString_GetUtf8(struct NativeString *string)
{
    (void)string;
    return (char *)result_name;
}

struct NativePath *NativePath_Destroy(struct NativePath *path)
{
    --strings;
    return path;
}

int File_OpenLegacyPath(void *path, uint32_t fork, int mode, void **handle)
{
    const struct rv_app_path *legacy = path;
    assert(legacy->volume == 0 && fork == 0x64617461u && mode == 1);
    strcpy(opened_path, legacy->text);
    *handle = &native_opens;
    ++native_opens;
    return 0;
}

int File_GetLegacyHandleSize(void *handle, uint32_t *size)
{
    (void)handle;
    *size = 0;
    return 0;
}

int File_ReadLegacyHandle(void *handle, void *buffer, uint32_t bytes, uint32_t flags,
                          uint32_t *read)
{
    (void)handle;
    (void)buffer;
    (void)bytes;
    (void)flags;
    *read = 0;
    return 0;
}

int File_SeekLegacyHandle(void *handle, uint64_t offset)
{
    (void)handle;
    (void)offset;
    return 0;
}

int File_CloseLegacyHandle(void *handle)
{
    (void)handle;
    --native_opens;
    return 0;
}

static const char *under_root(const char *root, const char *tail)
{
    strcpy(expected, root);
    strcat(expected, tail);
    return expected;
}

int main(int argc, char **argv)
{
    assert(argc == 2 && !strcmp(argv[1], RV_APP_ROOT));
    const char *root = argv[1];
    const char *invalid[] = {NULL,   "/root", "../other", "roms/../../other",
                             "a//b", "roms/", "a/./b",    "a\\b",
                             "a:b"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!rv_app_directory_open(invalid[i], 1));
        assert(!rv_app_file_open(invalid[i]));
    }
    assert(!opened && !native_opens);
    struct rv_app_directory *dirs[4];
    for (unsigned i = 0; i < 4; ++i)
        assert((dirs[i] = rv_app_directory_open("", 1)));
    assert(!strcmp(opened_path, under_root(root, "test-game")));
    assert(!rv_app_directory_open("", 1));
    struct rv_app_directory_entry entry;
    assert(rv_app_directory_read(dirs[0], &entry) == 1);
    assert(!strcmp(entry.name, "roms/Example.NES") && !entry.is_directory);
    assert(rv_app_directory_read(dirs[0], &entry) == 1 && entry.is_directory);
    memset(long_name, 'x', sizeof(long_name) - 1);
    assert(rv_app_directory_read(dirs[0], &entry) == -1 && !strings);
    assert(rv_app_directory_read(dirs[0], &entry) == 0 && !entry.name[0] && !strings);
    rv_app_directory_close(dirs[0]);
    rv_app_directory_close(dirs[0]);
    assert(closed == 1);
    assert(rv_app_directory_read(dirs[0], &entry) == -1);
    assert((dirs[0] = rv_app_directory_open("./roms", 1)));
    assert(!strcmp(opened_path, under_root(root, "test-game/roms")));
    rv_app_directories_close_all();
    assert(opened == closed && !strings);
    assert(!rv_app_file_open(""));
    struct rv_app_file *file = rv_app_file_open("./roms/Example.NES");
    assert(file && !strcmp(opened_path, under_root(root, "test-game/roms/Example.NES")));
    rv_app_files_close_all();
    assert(!native_opens);
    struct rv_app_path path;
    char name[258] = {0};
    unsigned prefix = strlen(under_root(root, "test-game/"));
    memset(name, 'x', 257 - prefix);
    assert(rv_app_make_path(&path, name, 0) == 0 && strlen(path.text) == 257);
    name[257 - prefix] = 'x';
    assert(rv_app_make_path(&path, name, 0) == -1);
    assert(!rv_app_directory_open(name, 1));
    printf("%s: path boundaries, directory lifecycle and entry copies pass\n", root);
    return 0;
}
