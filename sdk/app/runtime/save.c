/* SPDX-License-Identifier: GPL-3.0-only */

/* Apple's save store: four slots the host keeps in memory and writes to the
 * data folder at the next system save or the next app launch. The host never
 * loads a slot from disk, so a slot that is empty when opened is filled here
 * from the file, kept on the heap until the first write hands the contents
 * to the host. A slot the host still holds from an earlier run of this boot
 * is used as it is. */

#include "osos-2.0.5.h"
#include "platform.h"

struct rv_app_save {
    unsigned references; /* Opens not closed yet; the entry is valid while nonzero. */
    uint32_t size;
    uint8_t *loaded; /* The file's contents while the host's slot is empty. */
};

/* Indexed by the host's slot. */
static struct rv_app_save saves[GameSave_MaxSlots];

/* The contents the host holds in `slot`, or 0 for an empty slot. */
static uint32_t host_size(unsigned slot)
{
    uint8_t chunk[256];
    uint32_t size = 0;
    int whence = FileSeek_Start;
    for (;;) {
        int count = sizeof chunk;
        if (GameSaveFile_Read(slot, chunk, &count, whence, 0) != FileStatus_Ok || count <= 0)
            return size;
        size += count;
        whence = FileSeek_Current;
    }
}

/* The data file `name` into a heap block, or NULL with size 0; a missing file
 * is not the caller's failure. */
static uint8_t *load_file(const char *name, uint32_t *size)
{
    *size = 0;
    uint32_t status = rv_app_file_error_status;
    struct rv_app_file *file = rv_app_data_open(name, RV_APP_FILE_READ);
    rv_app_file_error_status = status;
    if (!file)
        return NULL;
    uint8_t *data = NULL;
    uint32_t bytes = rv_app_file_size(file);
    if (bytes) {
        data = rv_app_alloc(bytes);
        if (data && rv_app_file_read(file, data, bytes) == (int)bytes)
            *size = bytes;
        else {
            rv_app_free(data);
            data = NULL;
        }
    }
    rv_app_file_close(file);
    return data;
}

/* Records a failed save store call's status and says whether it failed. */
static int failed(int status)
{
    if (status != FileStatus_Ok)
        rv_app_file_error_status = status;
    return status != FileStatus_Ok;
}

struct rv_app_save *rv_app_save_open(const char *name)
{
    name = rv_app_check_name(name, 0);
    int slot = -1;
    if (!name || failed(GameSaveFile_Open(GameDirectory_UserData, name, &slot)))
        return NULL;
    if (slot < 0 || slot >= GameSave_MaxSlots) {
        GameSaveFile_Close(slot);
        return NULL;
    }
    struct rv_app_save *save = &saves[slot];
    if (!save->references) {
        save->size = host_size(slot);
        save->loaded = save->size ? NULL : load_file(name, &save->size);
    }
    ++save->references;
    return save;
}

int rv_app_save_read(struct rv_app_save *save, void *buffer, size_t size, uint32_t offset)
{
    if (!save || !save->references || (!buffer && size) || size > INT32_MAX)
        return -1;
    if (save->loaded || !save->size) {
        if (offset >= save->size)
            return 0;
        if (size > save->size - offset)
            size = save->size - offset;
        memcpy(buffer, save->loaded + offset, size);
        return (int)size;
    }
    int count = (int)size;
    int status =
        GameSaveFile_Read((uint32_t)(save - saves), buffer, &count, FileSeek_Start, offset);
    if (status == GameSaveStatus_EndOfData)
        return 0;
    return failed(status) ? -1 : count;
}

int rv_app_save_write(struct rv_app_save *save, const void *data, size_t size)
{
    if (!save || !save->references || (!data && size) || size > GameSave_MaxBytes ||
        failed(GameSaveFile_Write((uint32_t)(save - saves), data, (int)size)))
        return -1;
    rv_app_free(save->loaded);
    save->loaded = NULL;
    save->size = size;
    return 0;
}

uint32_t rv_app_save_size(const struct rv_app_save *save)
{
    return save->size;
}

void rv_app_save_close(struct rv_app_save *save)
{
    if (!save || !save->references)
        return;
    GameSaveFile_Close((uint32_t)(save - saves));
    if (--save->references)
        return;
    rv_app_free(save->loaded);
    save->loaded = NULL;
    save->size = 0;
}

void rv_app_saves_close_all(void)
{
    for (unsigned i = 0; i < GameSave_MaxSlots; ++i)
        while (saves[i].references)
            rv_app_save_close(&saves[i]);
}
