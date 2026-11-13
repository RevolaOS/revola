/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/directory.c by lgreitis. */

#include "osos-2.0.5.h"
#include "platform.h"

struct rv_app_directory {
    uint32_t iterator[DirectoryIterator_bytes / 4];
    int active;
    unsigned root_length;
};
static struct rv_app_directory directories[4];

struct rv_app_directory *rv_app_directory_open(const char *name, int recursive)
{
    struct rv_app_path path;
    if (rv_app_make_path(&path, GameDirectory_Package, name, 1) < 0)
        return NULL;
    for (unsigned i = 0; i < 4; ++i) {
        struct rv_app_directory *directory = &directories[i];
        if (!directory->active) {
            DirectoryIterator_Init((struct DirectoryIterator *)directory->iterator, path.text,
                                   recursive != 0, 0);
            directory->root_length = 0;
            while (path.text[directory->root_length])
                ++directory->root_length;
            directory->active = 1;
            return directory;
        }
    }
    return NULL;
}

int rv_app_directory_read(struct rv_app_directory *directory,
                          struct rv_app_directory_entry *entry)
{
    if (!directory || !directory->active || !entry)
        return -1;
    uint32_t path_words[NativePath_bytes / 4];
    struct NativePath *path = (struct NativePath *)path_words;
    NativePath_InitEmpty(path);
    entry->name[0] = 0;
    entry->is_directory = 0;
    int result = DirectoryIterator_Next((struct DirectoryIterator *)directory->iterator, path,
                                        &entry->is_directory);
    if (result) {
        const char *name = NativeString_GetUtf8((struct NativeString *)path);
        unsigned i = 0;
        while (name[i] && i + 1 < sizeof(entry->name)) {
            entry->name[i] = name[i];
            ++i;
        }
        entry->name[i] = 0;
        result = name[i] || directory->root_length + 1 + i >=
                                sizeof(((struct rv_app_path *)0)->text)
                     ? -1
                     : 1;
    }
    NativePath_Destroy(path);
    return result;
}

void rv_app_directory_close(struct rv_app_directory *directory)
{
    if (directory && directory->active) {
        DirectoryIterator_Destroy((struct DirectoryIterator *)directory->iterator);
        directory->active = 0;
    }
}

void rv_app_directories_close_all(void)
{
    for (unsigned i = 0; i < 4; ++i)
        rv_app_directory_close(&directories[i]);
}
