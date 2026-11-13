/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/tests/files_test.c by lgreitis. */

/* Host stand-ins for the host path service and the Apple file and directory
 * functions files.c and directory.c call, and the rules they must keep. */

#include "osos-2.0.5.h"
#include "platform.h"
#include "rv_app.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

const struct rv_app rv_app_application = {.package_id = "test-game"};
static unsigned opened, closed, strings, native_opens, open_mode, created, deleted;
static char opened_path[258], expected[300], long_name[300], created_path[258],
    deleted_path[258], written[64];
static unsigned written_bytes;
static const char *result_name;

struct DirectoryIterator;
struct NativePath;
struct NativeString;

/* The host's folders of the package "test-game": the package root and its
 * user data folder, as the stock host lays them out. */
#define PACKAGE_ROOT "iPod_Control/games_RO/test-game/"
#define DATA_ROOT "iPod_Control/gamedata_RW/test-game/"
static unsigned built;

int GameSystem_GetPath(uint32_t directory_kind, char *buffer, int *capacity,
                       const char *relative)
{
    assert(directory_kind == GameDirectory_Package || directory_kind == GameDirectory_UserData);
    assert(relative && *relative != '/');
    const char *root = directory_kind == GameDirectory_Package ? PACKAGE_ROOT : DATA_ROOT;
    unsigned length = strlen(root) + strlen(relative);
    ++built;
    if (*capacity < (int)length)
        return -1;
    memcpy(buffer, root, strlen(root));
    memcpy(buffer + strlen(root), relative, strlen(relative));
    /* The host copies the capacity, not the length: no terminator. */
    memset(buffer + length, 'X', *capacity - length);
    *capacity = length;
    return (int)length;
}

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
    assert(legacy->volume == 0 && fork == 0x64617461u && (mode == 1 || mode == 2));
    strcpy(opened_path, legacy->text);
    open_mode = mode;
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

int File_WriteLegacyHandle(void *handle, const void *buffer, uint32_t bytes, uint32_t flags,
                           uint32_t *out_written)
{
    (void)handle;
    assert(flags == 1 && written_bytes + bytes <= sizeof(written));
    memcpy(written + written_bytes, buffer, bytes);
    written_bytes += bytes;
    *out_written = bytes;
    return 0;
}

int File_DeleteLegacyPath(void *path)
{
    const struct rv_app_path *legacy = path;
    assert(legacy->volume == 0);
    strcpy(deleted_path, legacy->text);
    ++deleted;
    return 0;
}

int Directory_CreateWithParents(const char *path, uint32_t volume)
{
    assert(volume == 0);
    strcpy(created_path, path);
    ++created;
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

int main(void)
{
    const char *root = PACKAGE_ROOT;
    const char *invalid[] = {NULL,   "/root", "../other", "roms/../../other",
                             "a//b", "roms/", "a/./b",    "a\\b",
                             "a:b"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!rv_app_directory_open(invalid[i], 1));
        assert(!rv_app_file_open(invalid[i]));
        assert(!rv_app_data_open(invalid[i], RV_APP_FILE_READ));
        assert(!rv_app_data_open(invalid[i], RV_APP_FILE_WRITE));
        assert(rv_app_data_delete(invalid[i]) == -1);
    }
    assert(!opened && !native_opens && !created && !deleted && !built);
    struct rv_app_directory *dirs[4];
    for (unsigned i = 0; i < 4; ++i)
        assert((dirs[i] = rv_app_directory_open("", 1)));
    assert(!strcmp(opened_path, under_root(root, "")));
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
    assert(!strcmp(opened_path, under_root(root, "roms")));
    rv_app_directories_close_all();
    assert(opened == closed && !strings);
    assert(!rv_app_file_open(""));
    struct rv_app_file *file = rv_app_file_open("./roms/Example.NES");
    assert(file && !strcmp(opened_path, under_root(root, "roms/Example.NES")));
    rv_app_files_close_all();
    assert(!native_opens);

    /* The data folder: reads create nothing, writes create the folder and
     * replace the file, and only a handle opened for writing takes writes. */
    assert(!rv_app_data_open("", RV_APP_FILE_READ) && !rv_app_data_open("scores", 3));
    file = rv_app_data_open("scores", RV_APP_FILE_READ);
    assert(file && open_mode == 1 && !created && !deleted);
    assert(!strcmp(opened_path, DATA_ROOT "scores"));
    assert(rv_app_file_write(file, "x", 1) == -1);
    rv_app_file_close(file);
    file = rv_app_data_open("saves/game", RV_APP_FILE_WRITE);
    assert(file && open_mode == 2 && created == 1 && deleted == 1);
    assert(!strcmp(opened_path, DATA_ROOT "saves/game"));
    assert(!strcmp(created_path, DATA_ROOT "saves"));
    assert(!strcmp(deleted_path, opened_path));
    assert(rv_app_file_write(file, "abc", 3) == 3 && rv_app_file_write(file, "", 0) == 0);
    assert(rv_app_file_write(file, "de", 2) == 2 && written_bytes == 5);
    assert(!memcmp(written, "abcde", 5));
    assert(rv_app_file_size(file) == 5 && rv_app_file_position(file) == 5);
    assert(rv_app_file_seek(file, 2) == 0 && rv_app_file_position(file) == 2);
    rv_app_file_close(file);
    assert(!native_opens);
    assert(rv_app_data_delete("saves/game") == 0 && deleted == 2);
    assert(!strcmp(deleted_path, DATA_ROOT "saves/game"));
    assert(rv_app_data_delete("") == -1 && deleted == 2);

    struct rv_app_path path;
    char name[258] = {0};
    unsigned prefix = strlen(root);
    memset(name, 'x', 257 - prefix);
    assert(rv_app_make_path(&path, GameDirectory_Package, name, 0) == 0 &&
           strlen(path.text) == 257);
    name[257 - prefix] = 'x';
    assert(rv_app_make_path(&path, GameDirectory_Package, name, 0) == -1);
    assert(!rv_app_directory_open(name, 1));
    printf("path boundaries, directory and file lifecycle and the data folder pass\n");
    return 0;
}
