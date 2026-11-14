/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/tests/files_test.c by lgreitis. */

/* Host stand-ins for the file worker and the save store that files.c,
 * directory.c, request.c and save.c drive, and the rules they must keep: a
 * request completes only when the game's task yields, its outputs arrive in
 * the host's order, and it stays allocated until a frame hands it back. */

#include "osos-2.0.5.h"
#include "platform.h"
#include "rv_app.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const struct rv_app rv_app_application = {.package_id = "test-game"};

/* The heap: one arena that does not cross a 4 GiB boundary, so the 32-bit
 * word a request carries for its buffer leads back to the pointer, and a
 * count of the blocks alive. Nothing is reused. */
#define ARENA_BYTES (1 << 20)
static uint8_t *arena;
static size_t arena_used;
static unsigned live_blocks;

/* The buffers the tests read and write through requests, the two blocks alive
 * from the start; lit() stages a literal for a write. */
static char *text, *staged;
#define LIVE(blocks) (live_blocks == 2 + (blocks))

static const char *lit(const char *literal)
{
    return strcpy(staged, literal);
}

static void *test_pointer(uint32_t word)
{
    uintptr_t pointer = ((uintptr_t)arena & ~(uintptr_t)0xffffffffu) | word;
    assert(pointer >= (uintptr_t)arena && pointer < (uintptr_t)arena + ARENA_BYTES);
    return (void *)pointer;
}

void *rv_app_alloc(size_t size)
{
    size = (size + 15) & ~(size_t)15;
    assert(arena_used + size <= ARENA_BYTES);
    void *block = arena + arena_used;
    arena_used += size;
    ++live_blocks;
    return block;
}

void rv_app_free(void *pointer)
{
    if (pointer)
        --live_blocks;
}

/* The package and its data folder as two small file systems. */
struct fake_file {
    const char *name;
    char data[64];
    unsigned size;
    unsigned used;
};

static const struct fake_file package_files[] = {
    {"roms/Example.NES", "NESDATA", 7, 1},
    {"roms/sub/Deep.NES", "DEEP", 4, 1},
    {"readme.txt", "hi", 2, 1},
};
static struct fake_file data_files[8];
static char data_names[8][64];

struct fake_folder {
    const char *name;
    const char *entries[4];
    unsigned char directory[4];
    unsigned count;
    int truncated; /* Listed as the first 500 of more entries. */
};

static const struct fake_folder package_folders[] = {
    {"", {"readme.txt", "roms"}, {0, 1}, 2, 0},
    {"roms", {".", "..", "Example.NES", "sub"}, {1, 1, 0, 1}, 4, 0},
    {"roms/sub", {"Deep.NES"}, {0}, 1, 0},
    {"big", {"a", "b"}, {0, 0}, 2, 1},
};
#define FOLDERS (sizeof package_folders / sizeof package_folders[0])

static struct fake_file *find_data(const char *name)
{
    for (unsigned i = 0; i < 8; ++i)
        if (data_files[i].used && !strcmp(data_files[i].name, name))
            return &data_files[i];
    return NULL;
}

static struct fake_file *create_data(const char *name)
{
    unsigned i = 0;
    while (data_files[i].used)
        ++i;
    strcpy(data_names[i], name);
    data_files[i].name = data_names[i];
    data_files[i].size = 0;
    data_files[i].used = 1;
    return &data_files[i];
}

/* Open files and directory lists of the worker; a list without a folder is
 * one the host built for a listing that failed. */
struct fake_handle {
    const struct fake_file *package;
    struct fake_file *data;
    unsigned position;
    int used;
};
static struct fake_handle handles[16];
static struct {
    const struct fake_folder *folder;
    unsigned next;
    int used;
} lists[8];

static unsigned open_handles(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < 16; ++i)
        n += handles[i].used;
    return n;
}

static unsigned open_lists(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < 8; ++i)
        n += lists[i].used;
    return n;
}

/* The one request queued and not performed yet, with what the Queue entry
 * copied, and the completed list as the host links it. */
static struct {
    struct GameFileRequest *request;
    uint32_t kind;
    char path[300];
    int read_only;
} pending;
static struct GameFileRequest *completed_head, *completed_tail;
static unsigned yields, queued_deletes, queued_opens;

static int queue(uint32_t type, struct GameFileRequest *request, uint32_t kind, const char *path)
{
    assert(request && request->next == 0 && !pending.request);
    assert(request->result == RV_APP_REQUEST_PENDING && request->size == RV_APP_REQUEST_PENDING);
    if (request->type != type)
        return 0;
    assert(path == NULL || strlen(path) < sizeof pending.path);
    pending.request = request;
    pending.kind = kind;
    strcpy(pending.path, path ? path : "");
    return 1;
}

int GameFile_QueueOpen(uint32_t kind, const char *path, int read_only,
                       struct GameFileRequest *request)
{
    ++queued_opens;
    pending.read_only = read_only;
    return queue(GameFileRequest_Open, request, kind, path);
}

int GameFile_QueueClose(struct GameFileRequest *request)
{
    return queue(GameFileRequest_Close, request, 0, NULL);
}

int GameFile_QueueTransfer(struct GameFileRequest *request)
{
    assert(request->type == GameFileRequest_Read || request->type == GameFileRequest_Write ||
           request->type == GameFileRequest_Seek);
    return queue(request->type, request, 0, NULL);
}

int GameFile_QueueDelete(uint32_t kind, const char *path, struct GameFileRequest *request)
{
    ++queued_deletes;
    return queue(GameFileRequest_Delete, request, kind, path);
}

int GameDirectory_QueueList(uint32_t kind, const char *path, uint32_t create,
                            struct GameFileRequest *request)
{
    assert(!create);
    return queue(GameFileRequest_ListDirectory, request, kind, path);
}

static void complete(struct GameFileRequest *request)
{
    /* The worker links through the previous tail; it never clears next. */
    if (completed_tail)
        completed_tail->next = (uint32_t)(uintptr_t)request;
    else
        completed_head = request;
    completed_tail = request;
}

/* The worker's turn: performs the pending request, writing the outputs in
 * the host's order: an open's handle, result and size; a read's count then
 * result; a listing's result and size, then the handle of the list the host
 * built whatever the result, unless it had no slot for it. */
static void perform(void)
{
    struct GameFileRequest *r = pending.request;
    pending.request = NULL;
    struct fake_handle *h = NULL;
    if (r->type == GameFileRequest_Read || r->type == GameFileRequest_Write ||
        r->type == GameFileRequest_Seek || r->type == GameFileRequest_Close) {
        assert(r->handle < 16 && handles[r->handle].used);
        h = &handles[r->handle];
    }
    switch (r->type) {
    case GameFileRequest_Open: {
        unsigned slot = 0;
        while (slot < 16 && handles[slot].used)
            ++slot;
        assert(slot < 16);
        const struct fake_file *package = NULL;
        struct fake_file *data = NULL;
        /* The host takes the handle first and keeps it when the open fails. */
        handles[slot] = (struct fake_handle){NULL, NULL, 0, 1};
        r->handle = slot;
        if (pending.kind == GameDirectory_Package) {
            for (unsigned i = 0; i < 3; ++i)
                if (!strcmp(package_files[i].name, pending.path))
                    package = &package_files[i];
            if (!package || !pending.read_only) {
                r->result = package ? FileStatus_NotPermitted : FileStatus_NotFound;
                return complete(r);
            }
        } else {
            assert(pending.kind == GameDirectory_UserData);
            data = find_data(pending.path);
            if (!data && pending.read_only) {
                r->result = FileStatus_NotFound;
                return complete(r);
            }
            if (!data)
                data = create_data(pending.path);
        }
        handles[slot] = (struct fake_handle){package, data, 0, 1};
        r->result = FileStatus_Ok;
        r->size = package ? package->size : data->size;
        return complete(r);
    }
    case GameFileRequest_Close:
        h->used = 0;
        r->result = FileStatus_Ok;
        return complete(r);
    case GameFileRequest_Read: {
        const struct fake_file *file = h->package ? h->package : h->data;
        unsigned n = r->length;
        if (n > file->size - h->position)
            n = file->size - h->position;
        memcpy(test_pointer(r->buffer), file->data + h->position, n);
        h->position += n;
        r->transferred = n;
        r->result = FileStatus_Ok;
        return complete(r);
    }
    case GameFileRequest_Write:
        assert(h->data && h->position + r->length <= sizeof h->data->data);
        memcpy(h->data->data + h->position, test_pointer(r->buffer), r->length);
        h->position += r->length;
        if (h->position > h->data->size)
            h->data->size = h->position;
        r->transferred = r->length;
        r->result = FileStatus_Ok;
        r->size = h->data->size;
        return complete(r);
    case GameFileRequest_Seek:
        assert(r->whence == FileSeek_Start && r->offset >= 0);
        h->position = r->offset;
        r->result = FileStatus_Ok;
        return complete(r);
    case GameFileRequest_Delete: {
        struct fake_file *data = find_data(pending.path);
        if (pending.kind != GameDirectory_UserData)
            r->result = FileStatus_NotPermitted;
        else if (!data)
            r->result = FileStatus_NotFound;
        else {
            data->used = 0;
            r->result = FileStatus_Ok;
        }
        return complete(r);
    }
    case GameFileRequest_ListDirectory: {
        assert(pending.kind == GameDirectory_Package);
        const struct fake_folder *folder = NULL;
        for (unsigned i = 0; i < FOLDERS; ++i)
            if (!strcmp(package_folders[i].name, pending.path))
                folder = &package_folders[i];
        r->result = !folder ? FileStatus_NotFound
                    : folder->truncated ? FileStatus_ListTruncated
                                        : FileStatus_Ok;
        r->size = folder ? folder->count : 0;
        unsigned slot = 0;
        while (slot < 8 && lists[slot].used)
            ++slot;
        if (slot == 8) {
            r->result = FileStatus_NoSlot;
            return complete(r);
        }
        lists[slot].folder = folder;
        lists[slot].next = 0;
        lists[slot].used = 1;
        r->handle = slot;
        return complete(r);
    }
    default:
        assert(!"request type");
    }
}

void GameSystem_Sleep(uint32_t milliseconds)
{
    assert(milliseconds == 0);
    ++yields;
    if (pending.request)
        perform();
}

int GameDirectory_ReadEntry(int handle, char *name, uint32_t capacity, uint8_t *is_directory)
{
    assert(handle >= 0 && handle < 8 && lists[handle].used);
    const struct fake_folder *folder = lists[handle].folder;
    unsigned i = lists[handle].next;
    if (!folder || i >= folder->count)
        return GameDirectoryStatus_Last;
    assert(strlen(folder->entries[i]) < capacity);
    strcpy(name, folder->entries[i]);
    *is_directory = folder->directory[i];
    lists[handle].next = i + 1;
    return i + 1 == folder->count ? GameDirectoryStatus_Last : GameDirectoryStatus_More;
}

void GameDirectory_ReleaseList(int handle)
{
    assert(handle >= 0 && handle < 8 && lists[handle].used);
    lists[handle].used = 0;
}

/* The save store: four slots held in memory, written at a flush. */
static struct {
    char path[64];
    char data[64];
    int bytes;
    int position;
    unsigned references;
    int dirty;
} slots[GameSave_MaxSlots];

int GameSaveFile_Open(uint32_t kind, const char *path, int *out_slot)
{
    if (kind == GameDirectory_Package)
        return FileStatus_NotPermitted;
    for (int i = 0; i < GameSave_MaxSlots; ++i)
        if (slots[i].path[0] && !strcmp(slots[i].path, path)) {
            ++slots[i].references;
            *out_slot = i;
            return FileStatus_Ok;
        }
    for (int i = 0; i < GameSave_MaxSlots; ++i)
        if (!slots[i].path[0]) {
            strcpy(slots[i].path, path);
            slots[i].references = 1;
            *out_slot = i;
            return FileStatus_Ok;
        }
    return FileStatus_NoSlot;
}

int GameSaveFile_Close(uint32_t slot)
{
    assert(slot < GameSave_MaxSlots && slots[slot].path[0] && slots[slot].references);
    if (--slots[slot].references == 0 && !slots[slot].dirty)
        memset(&slots[slot], 0, sizeof slots[slot]);
    return FileStatus_Ok;
}

int GameSaveFile_Read(uint32_t slot, void *buffer, int *byte_count, int whence, uint32_t offset)
{
    assert(slot < GameSave_MaxSlots && slots[slot].path[0]);
    int position = whence == FileSeek_Start ? (int)offset
                   : whence == FileSeek_Current ? slots[slot].position + (int)offset
                                                : -1;
    if (position < 0)
        return GameSaveStatus_BadOffset;
    if (position >= slots[slot].bytes)
        return GameSaveStatus_EndOfData;
    if (*byte_count > slots[slot].bytes - position)
        *byte_count = slots[slot].bytes - position;
    memcpy(buffer, slots[slot].data + position, *byte_count);
    slots[slot].position = position + *byte_count;
    return FileStatus_Ok;
}

int GameSaveFile_Write(uint32_t slot, const void *buffer, int byte_count)
{
    assert(slot < GameSave_MaxSlots && slots[slot].path[0]);
    if (byte_count > (int)sizeof slots[slot].data)
        return GameSaveStatus_TooLarge;
    memcpy(slots[slot].data, buffer, byte_count);
    slots[slot].bytes = byte_count;
    slots[slot].position = 0;
    slots[slot].dirty = 1;
    return FileStatus_Ok;
}

/* The host's flush: dirty slots to the data folder, freed when unreferenced. */
static void flush_saves(void)
{
    for (int i = 0; i < GameSave_MaxSlots; ++i) {
        if (!slots[i].path[0] || !slots[i].dirty)
            continue;
        struct fake_file *file = find_data(slots[i].path);
        if (!file)
            file = create_data(slots[i].path);
        memcpy(file->data, slots[i].data, slots[i].bytes);
        file->size = slots[i].bytes;
        slots[i].dirty = 0;
        if (!slots[i].references)
            memset(&slots[i], 0, sizeof slots[i]);
    }
}

static void check_names(void)
{
    const char *invalid[] = {NULL,   "/root", "../other", "roms/../../other",
                             "a//b", "roms/", "a/./b",    "a\\b",
                             "a:b"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!rv_app_directory_open(invalid[i], 1));
        assert(!rv_app_file_open(invalid[i]));
        assert(!rv_app_data_open(invalid[i], RV_APP_FILE_READ));
        assert(!rv_app_data_open(invalid[i], RV_APP_FILE_WRITE));
        assert(rv_app_data_delete(invalid[i]) == -1);
        assert(!rv_app_save_open(invalid[i]));
    }
    assert(!rv_app_file_open("") && !rv_app_data_open("", RV_APP_FILE_READ));
    assert(!rv_app_data_open("scores", 3) && !rv_app_save_open(""));
    assert(!yields && LIVE(0) && !queued_opens);
}

static void check_files(void)
{
    /* A refused open leaves the host's handle behind; the runtime closes it. */
    assert(!rv_app_file_open("missing") && yields == 2);
    assert(rv_app_file_error() == FileStatus_NotFound && open_handles() == 0);
    struct rv_app_file *file = rv_app_file_open("./roms/Example.NES");
    assert(file && !strcmp(pending.path, "roms/Example.NES") && pending.read_only);
    assert(yields == 3);
    assert(rv_app_file_size(file) == 7 && rv_app_file_position(file) == 0);
    assert(rv_app_file_read(file, text, 3) == 3 && !memcmp(text, "NES", 3));
    assert(rv_app_file_position(file) == 3);
    assert(rv_app_file_seek(file, 5) == 0 && rv_app_file_seek(file, 8) == -1);
    assert(rv_app_file_read(file, text, 10) == 2 && !memcmp(text, "TA", 2));
    assert(rv_app_file_read(file, text, 10) == 0 && rv_app_file_read(file, NULL, 1) == -1);
    assert(rv_app_file_write(file, lit("x"), 1) == -1);
    assert(open_handles() == 1);
    rv_app_file_close(file);
    rv_app_file_close(file);
    assert(open_handles() == 0);

    struct rv_app_file *many[12];
    for (unsigned i = 0; i < 12; ++i)
        assert((many[i] = rv_app_file_open("readme.txt")));
    assert(!rv_app_file_open("readme.txt") && open_handles() == 12);
    rv_app_files_close_all();
    assert(open_handles() == 0 && LIVE(0));
}

static void check_directories(void)
{
    struct rv_app_directory *dirs[4];
    struct rv_app_directory_entry entry;
    for (unsigned i = 0; i < 4; ++i)
        assert((dirs[i] = rv_app_directory_open("", 1)));
    assert(!rv_app_directory_open("", 1) && open_lists() == 4);
    rv_app_directory_close(dirs[1]);
    rv_app_directory_close(dirs[2]);
    rv_app_directory_close(dirs[3]);

    /* Depth first, names relative to the folder opened, "." and ".." skipped. */
    const char *walk[] = {"readme.txt", "roms", "roms/Example.NES", "roms/sub",
                          "roms/sub/Deep.NES"};
    for (unsigned i = 0; i < 5; ++i) {
        assert(rv_app_directory_read(dirs[0], &entry) == 1);
        assert(!strcmp(entry.name, walk[i]));
        assert(entry.is_directory == (i == 1 || i == 3));
    }
    assert(rv_app_directory_read(dirs[0], &entry) == 0 && !entry.name[0]);
    assert(rv_app_directory_read(dirs[0], &entry) == 0 && open_lists() == 0);
    rv_app_directory_close(dirs[0]);
    assert(rv_app_directory_read(dirs[0], &entry) == -1);

    assert((dirs[0] = rv_app_directory_open("./roms", 0)));
    assert(!strcmp(pending.path, "roms"));
    assert(rv_app_directory_read(dirs[0], &entry) == 1 && !strcmp(entry.name, "Example.NES"));
    assert(rv_app_directory_read(dirs[0], &entry) == 1 && !strcmp(entry.name, "sub"));
    assert(entry.is_directory && open_lists() == 1);
    assert(rv_app_directory_read(dirs[0], &entry) == 0 && open_lists() == 0);
    rv_app_directory_close(dirs[0]);

    assert((dirs[0] = rv_app_directory_open("roms", 1)));
    assert(rv_app_directory_read(dirs[0], &entry) == 1 && !strcmp(entry.name, "Example.NES"));
    assert(rv_app_directory_read(dirs[0], &entry) == 1 && !strcmp(entry.name, "sub"));
    assert(rv_app_directory_read(dirs[0], &entry) == 1 && !strcmp(entry.name, "sub/Deep.NES"));
    assert(open_lists() == 2);
    rv_app_directories_close_all();
    assert(open_lists() == 0);

    /* A missing folder opens and reads nothing; the list the host built for
     * it is given back at once. */
    assert((dirs[0] = rv_app_directory_open("nope", 1)));
    assert(open_lists() == 0 && rv_app_file_error() == FileStatus_NotFound);
    assert(rv_app_directory_read(dirs[0], &entry) == 0);
    rv_app_directory_close(dirs[0]);

    /* A listing the host cut at 500 entries reads what it holds. */
    assert((dirs[0] = rv_app_directory_open("big", 0)) && open_lists() == 1);
    assert(rv_app_directory_read(dirs[0], &entry) == 1 && !strcmp(entry.name, "a"));
    assert(rv_app_directory_read(dirs[0], &entry) == 1 && !strcmp(entry.name, "b"));
    assert(rv_app_directory_read(dirs[0], &entry) == 0 && open_lists() == 0);
    rv_app_directory_close(dirs[0]);
    assert(LIVE(0));
}

static void check_data(void)
{
    assert(!rv_app_data_open("scores", RV_APP_FILE_READ) && queued_deletes == 0);
    struct rv_app_file *file = rv_app_data_open("saves/game", RV_APP_FILE_WRITE);
    assert(file && queued_deletes == 1 && !pending.read_only);
    assert(!strcmp(pending.path, "saves/game") && pending.kind == GameDirectory_UserData);
    assert(rv_app_file_write(file, lit("abc"), 3) == 3 && rv_app_file_write(file, "", 0) == 0);
    assert(rv_app_file_write(file, lit("de"), 2) == 2);
    assert(rv_app_file_size(file) == 5 && rv_app_file_position(file) == 5);
    assert(rv_app_file_seek(file, 2) == 0 && rv_app_file_position(file) == 2);
    assert(rv_app_file_read(file, text, 10) == 3 && !memcmp(text, "cde", 3));
    rv_app_file_close(file);
    assert(find_data("saves/game") && find_data("saves/game")->size == 5);

    file = rv_app_data_open("saves/game", RV_APP_FILE_READ);
    assert(file && rv_app_file_size(file) == 5 && rv_app_file_write(file, "x", 1) == -1);
    assert(rv_app_file_read(file, text, 16) == 5 && !memcmp(text, "abcde", 5));
    rv_app_file_close(file);
    /* Writing again replaces the file: the old one is deleted first. */
    file = rv_app_data_open("saves/game", RV_APP_FILE_WRITE);
    assert(file && queued_deletes == 2 && rv_app_file_size(file) == 0);
    rv_app_file_close(file);
    assert(rv_app_data_delete("saves/game") == 0 && !find_data("saves/game"));
    assert(rv_app_data_delete("saves/game") == -1);
    assert(rv_app_file_error() == FileStatus_NotFound && open_handles() == 0);
}

static void check_saves(void)
{
    /* No file yet: the open looked for one and found nothing. */
    struct rv_app_save *save = rv_app_save_open("runs");
    assert(save && rv_app_save_size(save) == 0 && slots[0].references == 1);
    unsigned opens = queued_opens;
    assert(LIVE(0));
    assert(rv_app_save_read(save, text, 8, 0) == 0);
    assert(rv_app_save_write(save, "ab", 2) == 0 && rv_app_save_size(save) == 2);
    assert(slots[0].bytes == 2 && slots[0].dirty);
    assert(rv_app_save_read(save, text, 8, 1) == 1 && text[0] == 'b');
    assert(rv_app_save_read(save, text, 8, 2) == 0);
    assert(rv_app_save_open("runs") == save && slots[0].references == 2);
    rv_app_save_close(save);
    rv_app_save_close(save);
    rv_app_save_close(save);
    assert(slots[0].references == 0 && slots[0].dirty && LIVE(0));

    /* The host still holds the slot: its contents are found and used. */
    save = rv_app_save_open("runs");
    assert(save && rv_app_save_size(save) == 2 && LIVE(0) && queued_opens == opens);
    assert(rv_app_save_read(save, text, 8, 0) == 2 && !memcmp(text, "ab", 2));
    rv_app_save_close(save);
    flush_saves();
    assert(!slots[0].path[0] && find_data("runs") && find_data("runs")->size == 2);

    /* After the flush the slot is empty: the file is read from the disk. */
    save = rv_app_save_open("runs");
    assert(save && rv_app_save_size(save) == 2 && LIVE(1) && open_handles() == 0);
    assert(rv_app_save_read(save, text, 1, 1) == 1 && text[0] == 'b');
    assert(rv_app_save_write(save, "xyz", 3) == 0 && LIVE(0));
    assert(rv_app_save_read(save, text, 8, 0) == 3 && !memcmp(text, "xyz", 3));

    struct rv_app_save *more[3];
    for (unsigned i = 0; i < 3; ++i) {
        char name[8] = {'s', (char)('0' + i), 0};
        assert((more[i] = rv_app_save_open(name)));
    }
    assert(!rv_app_save_open("fifth") && rv_app_file_error() == FileStatus_NoSlot);
    rv_app_saves_close_all();
    assert(rv_app_save_read(save, text, 8, 0) == -1);
    for (unsigned i = 0; i < GameSave_MaxSlots; ++i)
        assert(slots[i].references == 0);
    assert(LIVE(0));

    /* The file a fresh save or a write open looks for first may be missing;
     * the error reported stays the caller's last failure. */
    assert((save = rv_app_save_open("fresh")) && rv_app_file_error() == FileStatus_NoSlot);
    rv_app_save_close(save);
    struct rv_app_file *file = rv_app_data_open("fresh.dat", RV_APP_FILE_WRITE);
    assert(file && rv_app_file_error() == FileStatus_NoSlot);
    rv_app_file_close(file);
    assert(LIVE(0) && open_handles() == 0);
}

int main(void)
{
    arena = malloc(ARENA_BYTES);
    assert(arena && ((uintptr_t)arena >> 32) == (((uintptr_t)arena + ARENA_BYTES) >> 32));
    text = rv_app_alloc(16);
    staged = rv_app_alloc(16);
    check_names();
    check_files();
    check_directories();
    check_data();
    check_saves();
    assert(!pending.request);
    printf("path rules, requests, files, folders, data files and saves pass\n");
    return 0;
}
