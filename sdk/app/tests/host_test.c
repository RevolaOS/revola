/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/tests/files_test.c by lgreitis. */

/* Host stand-ins for the file worker, the save store and the sound objects
 * that files.c, directory.c, request.c, save.c and sound.c drive, and the
 * rules they must keep: a request completes only when the game's task
 * yields, its outputs arrive in the host's order, and it stays allocated
 * until a frame hands it back; a sound's samples stay the app's memory while
 * the host plays them. */

#include "osos-2.0.5.h"
#include "platform.h"
#include "runtime.h"
#include "rv_app.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void record_input(const struct rv_app_input_event *event);
const struct rv_app rv_app_application = {.package_id = "test-game", .input = record_input};

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

/* The two WAVs are written by wav_files() before the checks. */
static struct fake_file package_files[] = {
    {"roms/Example.NES", "NESDATA", 7, 1},
    {"roms/sub/Deep.NES", "DEEP", 4, 1},
    {"readme.txt", "hi", 2, 1},
    {"sounds/beep.wav", "", 0, 1},
    {"sounds/float.wav", "", 0, 1},
    {"sounds/short.wav", "RIFF", 4, 1},
    {"sounds/cut.wav", "", 0, 1},
};

static void put32(char *p, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        p[i] = (char)(v >> (8 * i));
}

/* A WAV with `format`, 8-bit mono 8000 Hz, `bytes` of samples declared as
 * `declared` in the data chunk; `list` puts a three-byte LIST chunk (padded
 * to four) before the format chunk. */
static unsigned wav(char *out, unsigned format, const uint8_t *samples, unsigned bytes,
                    unsigned declared, int list)
{
    memcpy(out, "RIFF", 4);
    put32(out + 4, 36 + bytes);
    memcpy(out + 8, "WAVE", 4);
    char *p = out + 12;
    if (list) {
        memcpy(p, "LIST", 4);
        put32(p + 4, 3);
        memcpy(p + 8, "abc", 4);
        p += 12;
    }
    memcpy(p, "fmt ", 4);
    put32(p + 4, 16);
    put32(p + 8, format | 1 << 16); /* format, channels 1 */
    put32(p + 12, 8000);
    put32(p + 16, 8000);
    put32(p + 20, 1 | 8 << 16); /* block align 1, bits 8 */
    memcpy(p + 24, "data", 4);
    put32(p + 28, declared);
    memcpy(p + 32, samples, bytes);
    return (unsigned)(p + 32 + bytes - out);
}

static void wav_files(void)
{
    static const uint8_t samples[] = {0x80, 0xff, 0x00, 0x7f};
    package_files[3].size = wav(package_files[3].data, 1, samples, 4, 4, 1);
    package_files[4].size = wav(package_files[4].data, 3, samples, 4, 4, 0);
    package_files[6].size = wav(package_files[6].data, 1, samples, 4, 1000, 0);
}
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
            for (unsigned i = 0; i < sizeof package_files / sizeof package_files[0]; ++i)
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

/* The host's sound objects: a table indexed by the handle, with the fields
 * Create initialises, a voice that Play attaches and a natural end releases
 * (end_sound()) without touching the state. */
struct fake_sound {
    const void *data;
    uint32_t byte_count, sample_rate, channels, bits, volume, speed, play_count;
    uint32_t lead_in_ms, tail_ms, duck_level, duck_ms;
    int32_t pan;
    int next; /* -1 for none. */
    uint8_t state, attached, used;
    unsigned plays, stops;
};
static struct fake_sound host_sounds[40];
static unsigned host_sound_count, sound_create_failures;

static struct fake_sound *sound_at(int handle)
{
    if (handle < 0 || handle >= (int)(sizeof host_sounds / sizeof host_sounds[0]) ||
        !host_sounds[handle].used)
        return NULL;
    return &host_sounds[handle];
}

int GameSound_Create(void)
{
    if (sound_create_failures) {
        --sound_create_failures;
        return -1;
    }
    for (unsigned i = 0; i < sizeof host_sounds / sizeof host_sounds[0]; ++i)
        if (!host_sounds[i].used) {
            host_sounds[i] = (struct fake_sound){.sample_rate = 44100,
                                                 .channels = 1,
                                                 .bits = 16,
                                                 .volume = 0x7fff,
                                                 .speed = 1000,
                                                 .play_count = 1,
                                                 .duck_level = 0x7fff,
                                                 .next = -1,
                                                 .used = 1};
            ++host_sound_count;
            return (int)i;
        }
    return -1;
}

int GameSound_Destroy(int handle)
{
    struct fake_sound *s = sound_at(handle);
    if (!s)
        return 0;
    memset(s, 0, sizeof *s);
    --host_sound_count;
    return 1;
}

/* The host writes a state along the whole next ring. */
static void set_ring_state(struct fake_sound *s, uint8_t state)
{
    for (struct fake_sound *r = s;;) {
        r->state = state;
        if (r->next < 0 || r->next == s - host_sounds)
            return;
        r = &host_sounds[r->next];
    }
}

int GameSound_Play(int handle)
{
    struct fake_sound *s = sound_at(handle);
    if (!s)
        return 0;
    assert(s->data && s->byte_count);
    s->attached = 1;
    set_ring_state(s, GameSoundState_Playing);
    ++s->plays;
    return 1;
}

int GameSound_Stop(int handle)
{
    struct fake_sound *s = sound_at(handle);
    if (!s)
        return 0;
    set_ring_state(s, GameSoundState_Stopped);
    ++s->stops;
    return 1;
}

int GameSound_Pause(int handle)
{
    struct fake_sound *s = sound_at(handle);
    if (s)
        s->state = GameSoundState_Paused;
    return s != NULL;
}

int GameSound_Resume(int handle)
{
    struct fake_sound *s = sound_at(handle);
    if (s)
        s->state = GameSoundState_Playing;
    return s != NULL;
}

int GameSound_IsPlaying(int handle)
{
    struct fake_sound *s = sound_at(handle);
    return s && s->attached && s->state == GameSoundState_Playing;
}

uint32_t GameSound_GetVoicePosition(int handle)
{
    struct fake_sound *s = sound_at(handle);
    return s && s->attached ? 123 : 0;
}

static void end_sound(int handle)
{
    sound_at(handle)->attached = 0;
}

#define SOUND_SETTER(name, type, field)             \
    int GameSound_Set##name(int handle, type value) \
    {                                               \
        struct fake_sound *s = sound_at(handle);    \
        if (s)                                      \
            s->field = value;                       \
        return s != NULL;                           \
    }
SOUND_SETTER(ByteCount, uint32_t, byte_count)
SOUND_SETTER(SampleRate, uint32_t, sample_rate)
SOUND_SETTER(Channels, uint32_t, channels)
SOUND_SETTER(BitsPerSample, uint32_t, bits)
SOUND_SETTER(Volume, uint32_t, volume)
SOUND_SETTER(Pan, int32_t, pan)
SOUND_SETTER(Speed, uint32_t, speed)
SOUND_SETTER(PlayCount, uint32_t, play_count)
SOUND_SETTER(LeadInMilliseconds, uint32_t, lead_in_ms)
SOUND_SETTER(TailMilliseconds, uint32_t, tail_ms)

int GameSound_SetData(int handle, const void *samples)
{
    struct fake_sound *s = sound_at(handle);
    if (!s || !samples)
        return 0;
    s->data = samples;
    return 1;
}

int GameSound_SetNext(int handle, int next)
{
    struct fake_sound *s = sound_at(handle);
    if (!s || !sound_at(next))
        return 0;
    s->next = next;
    return 1;
}

int GameSound_SetMusicDuck(int handle, uint32_t level, uint32_t milliseconds)
{
    struct fake_sound *s = sound_at(handle);
    if (!s)
        return 0;
    s->duck_level = level;
    s->duck_ms = milliseconds;
    return 1;
}


/* The music player and the bridge in front of it: a command is a queued
 * event ui_task_runs() applies; the bridge predicts the playback state and
 * caches a setting as soon as its command is queued, and answers the rest
 * from the player. Buttons the host forwards are logged. */
struct fake_player {
    uint8_t state, has_list;
    uint32_t index, count, position, remaining, repeat, order, volume, limit, eq, eq_saved;
    int speed, scanning, eq_restore;
    int32_t gain;
};
static struct fake_player player;
static int predicted_state = -1;
static int cache_repeat = -1, cache_order = -1, cache_eq = -1, cache_speed, cache_gain = -40000;

enum music_command {
    MUSIC_TOGGLE = 1,
    MUSIC_STOP,
    MUSIC_NEXT,
    MUSIC_PREVIOUS,
    MUSIC_PLAY_INDEX,
    MUSIC_SEEK,
    MUSIC_SCAN_FORWARD,
    MUSIC_SCAN_BACKWARD,
    MUSIC_SCAN_STOP,
    MUSIC_REPEAT,
    MUSIC_ORDER,
    MUSIC_EQ,
    MUSIC_EQ_TEMPORARY,
    MUSIC_SPEED,
    MUSIC_GAIN,
};
static struct {
    enum music_command command;
    uint32_t value;
} music_queue[16];
static unsigned music_queued, speed_calls;
static struct {
    uint8_t button, type;
} forwarded[8];
static unsigned forwarded_count;

static int predicted_or_player_state(void)
{
    return predicted_state != -1 ? predicted_state : player.state;
}

static void post_music(enum music_command command, uint32_t value)
{
    assert(music_queued < sizeof music_queue / sizeof music_queue[0]);
    music_queue[music_queued].command = command;
    music_queue[music_queued++].value = value;
    if (command == MUSIC_TOGGLE) {
        int state = predicted_or_player_state();
        if (state == GamePlaybackState_Playing)
            predicted_state = GamePlaybackState_Paused;
        else if (state == GamePlaybackState_Paused)
            predicted_state = GamePlaybackState_Playing;
    } else if (command == MUSIC_STOP) {
        predicted_state = GamePlaybackState_Stopped;
    } else if (command == MUSIC_PLAY_INDEX) {
        predicted_state = GamePlaybackState_Playing;
    }
}

static void ui_task_runs(void)
{
    for (unsigned i = 0; i < music_queued; ++i) {
        uint32_t value = music_queue[i].value;
        switch (music_queue[i].command) {
        case MUSIC_TOGGLE:
            if (!player.has_list)
                break;
            if (player.state == GamePlaybackState_Playing)
                player.state = GamePlaybackState_Paused;
            else
                player.state = GamePlaybackState_Playing;
            break;
        case MUSIC_STOP:
            player.state = GamePlaybackState_Stopped;
            player.has_list = 0;
            player.count = 0;
            cache_repeat = cache_order = 0;
            break;
        case MUSIC_NEXT:
            ++player.index;
            break;
        case MUSIC_PREVIOUS:
            --player.index;
            break;
        case MUSIC_PLAY_INDEX:
            player.index = value;
            player.state = GamePlaybackState_Playing;
            break;
        case MUSIC_SEEK:
            player.position = value;
            break;
        case MUSIC_SCAN_FORWARD:
            player.scanning = 1;
            break;
        case MUSIC_SCAN_BACKWARD:
            player.scanning = -1;
            break;
        case MUSIC_SCAN_STOP:
            player.scanning = 0;
            break;
        case MUSIC_REPEAT:
            player.repeat = value;
            break;
        case MUSIC_ORDER:
            player.order = value;
            break;
        case MUSIC_EQ:
        case MUSIC_EQ_TEMPORARY:
            player.eq = value;
            break;
        case MUSIC_SPEED:
            player.speed = (int)value;
            break;
        case MUSIC_GAIN:
            player.gain = (int32_t)value;
            break;
        }
    }
    music_queued = 0;
    predicted_state = -1;
}

uint8_t GameAudio_GetPlaybackState(void)
{
    return (uint8_t)predicted_or_player_state();
}

int GameAudio_HasPlaybackList(void)
{
    return player.has_list;
}

void GameAudio_TogglePlayPause(uint8_t wait)
{
    assert(wait == 0);
    post_music(MUSIC_TOGGLE, 0);
}

void GameAudio_Stop(void)
{
    post_music(MUSIC_STOP, 0);
}

void GameAudio_NextTrack(void)
{
    post_music(MUSIC_NEXT, 0);
}

void GameAudio_PreviousTrack(void)
{
    post_music(MUSIC_PREVIOUS, 0);
}

uint32_t GameAudio_GetEntryCount(void)
{
    return player.count;
}

uint32_t GameAudio_GetCurrentIndex(void)
{
    return player.index;
}

void GameAudio_PlayIndex(uint32_t index, uint8_t flag, uint32_t option)
{
    assert(flag == 0 && option == 0);
    post_music(MUSIC_PLAY_INDEX, index);
}

void GameAudio_Seek(uint32_t milliseconds)
{
    post_music(MUSIC_SEEK, milliseconds);
}

void GameAudio_StartScanForward(void)
{
    post_music(MUSIC_SCAN_FORWARD, 0);
}

void GameAudio_StartScanBackward(void)
{
    post_music(MUSIC_SCAN_BACKWARD, 0);
}

void GameAudio_StopScan(void)
{
    post_music(MUSIC_SCAN_STOP, 0);
}

int GameAudio_IsScanningForward(void)
{
    return player.scanning > 0;
}

int GameAudio_IsScanningBackward(void)
{
    return player.scanning < 0;
}

uint32_t GameAudio_GetPosition(void)
{
    return player.position;
}

uint32_t GameAudio_GetRemainingPosition(void)
{
    return player.remaining;
}

uint32_t GameAudio_GetRepeatMode(void)
{
    if (cache_repeat == -1)
        cache_repeat = (int)player.repeat;
    return (uint32_t)cache_repeat & 0xff;
}

void GameAudio_SetRepeatMode(uint8_t mode)
{
    if (!player.has_list)
        return;
    cache_repeat = mode;
    post_music(MUSIC_REPEAT, mode);
}

uint32_t GameAudio_GetPlaybackOrder(void)
{
    if (cache_order == -1)
        cache_order = (int)player.order;
    return (uint32_t)cache_order & 0xff;
}

void GameAudio_SetPlaybackOrder(uint8_t order)
{
    if (!player.has_list)
        return;
    cache_order = order;
    post_music(MUSIC_ORDER, order);
}

uint32_t GameAudio_GetVolume(void)
{
    return player.volume;
}

uint32_t GameAudio_GetVolumeLimit(void)
{
    return player.limit;
}

void GameAudio_SetVolume(uint32_t volume)
{
    player.volume = volume > GameAudio_VolumeMax ? GameAudio_VolumeMax : volume;
}

uint32_t GameAudio_GetEQMenuIndex(void)
{
    if (cache_eq == -1)
        cache_eq = (int)player.eq;
    return (uint32_t)cache_eq;
}

void GameAudio_SetEQMenuIndexTemporary(uint32_t index)
{
    if (!player.has_list)
        return;
    cache_eq = (int)index;
    player.eq_restore = 1;
    post_music(MUSIC_EQ_TEMPORARY, index);
}

void GameAudio_SetEQMenuIndex(uint32_t index)
{
    if (!player.has_list)
        return;
    cache_eq = (int)index;
    player.eq_saved = index;
    post_music(MUSIC_EQ, index);
}

int GameAudio_GetSpeed(void)
{
    if (cache_speed == 0)
        cache_speed = player.speed;
    return cache_speed;
}

int GameAudio_SetSpeed(int speed)
{
    ++speed_calls;
    if (speed < 500 || speed > 2000 || !player.has_list)
        return 0;
    cache_speed = speed;
    post_music(MUSIC_SPEED, (uint32_t)speed);
    return 1;
}

int32_t GameAudio_GetGain(void)
{
    if (cache_gain == -40000)
        cache_gain = player.gain;
    return cache_gain;
}

void GameAudio_SetGain(int32_t gain)
{
    assert(gain >= -0x7fff && gain <= 0x7fff);
    cache_gain = gain;
    post_music(MUSIC_GAIN, (uint32_t)gain);
}

/* The host acts on releases of Play, Next and Previous; a Menu release
 * would leave the game, so it is logged too and must never arrive. */
void GameInput_ForwardButtonEvent(const struct GameInputEvent *event)
{
    const struct rv_app_eapp_input_event *native = (const void *)event;
    assert(forwarded_count < sizeof forwarded / sizeof forwarded[0]);
    forwarded[forwarded_count].button = native->button;
    forwarded[forwarded_count++].type = native->type;
    if (native->type != GameButtonEvent_Release)
        return;
    if (native->button == GameButton_Play)
        post_music(MUSIC_TOGGLE, 0);
    else if (native->button == GameButton_Next)
        post_music(MUSIC_NEXT, 0);
    else if (native->button == GameButton_Previous)
        post_music(MUSIC_PREVIOUS, 0);
}

/* What runtime.c needs besides the host: the display, which has no surface
 * here, and the app's input log. */
void *rv_app_platform_begin_frame(const struct rv_app_eapp_frame_input *input,
                                  struct rv_app_framebuffer *framebuffer)
{
    (void)input;
    (void)framebuffer;
    return NULL;
}

void rv_app_platform_present(void *surface)
{
    (void)surface;
}

static struct rv_app_input_event received[8];
static unsigned received_count;

static void record_input(const struct rv_app_input_event *event)
{
    assert(received_count < sizeof received / sizeof received[0]);
    received[received_count++] = *event;
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

static void check_sounds(void)
{
    static const int16_t frames[4] = {0, 1000, -1000, 0};
    struct rv_app_sound *tone;
    /* Formats the host's voice would refuse are refused here, in time. */
    assert(!rv_app_sound_create(NULL, 8, 44100, 1, 16));
    assert(!rv_app_sound_create(frames, 0, 44100, 1, 16));
    assert(!rv_app_sound_create(frames, 8, 7999, 1, 16));
    assert(!rv_app_sound_create(frames, 8, 48001, 1, 16));
    assert(!rv_app_sound_create(frames, 8, 44100, 3, 16));
    assert(!rv_app_sound_create(frames, 8, 44100, 1, 12));
    assert(!rv_app_sound_create(frames, 6, 44100, 2, 16)); /* A partial frame. */
    assert(host_sound_count == 0);
    assert((tone = rv_app_sound_create(frames, 8, 8000, 1, 16)));
    struct fake_sound *host = sound_at(0);
    assert(host && host->data == frames && host->byte_count == 8 && host->sample_rate == 8000);
    assert(host->channels == 1 && host->bits == 16 && host->volume == 0x7fff);
    rv_app_sound_free(tone);
    assert((tone = rv_app_sound_create(frames, 8, 48000, 2, 8)) && sound_at(0)->bits == 8);
    assert(sound_at(0)->channels == 2 && sound_at(0)->sample_rate == 48000 && LIVE(0));

    /* Play restarts: the host would take a second voice for a sound that
     * still plays, so a stop goes first. The natural end clears the voice,
     * not the state. */
    assert(!rv_app_sound_playing(tone) && rv_app_sound_position(tone) == 0);
    rv_app_sound_play(tone);
    assert(host->stops == 1 && host->plays == 1 && rv_app_sound_playing(tone));
    assert(rv_app_sound_position(tone) == 123);
    rv_app_sound_play(tone);
    assert(host->stops == 2 && host->plays == 2);
    rv_app_sound_pause(tone);
    assert(!rv_app_sound_playing(tone) && host->state == GameSoundState_Paused);
    rv_app_sound_resume(tone);
    assert(rv_app_sound_playing(tone));
    end_sound(0);
    assert(!rv_app_sound_playing(tone) && host->state == GameSoundState_Playing);
    rv_app_sound_play(tone);
    rv_app_sound_stop(tone);
    assert(!rv_app_sound_playing(tone) && host->state == GameSoundState_Stopped);

    rv_app_sound_set_volume(tone, 100);
    rv_app_sound_set_pan(tone, -RV_APP_SOUND_PAN_MAX);
    rv_app_sound_set_speed(tone, 1500);
    rv_app_sound_set_play_count(tone, 0);
    rv_app_sound_set_lead_in_ms(tone, 10);
    rv_app_sound_set_tail_ms(tone, 20);
    rv_app_sound_set_music_duck(tone, 0x1000, 250);
    assert(host->volume == 100 && host->pan == -0x7fff && host->speed == 1500);
    assert(host->play_count == 0 && host->lead_in_ms == 10 && host->tail_ms == 20);
    assert(host->duck_level == 0x1000 && host->duck_ms == 250);

    /* A WAV of the package: the block is the runtime's until the sound is
     * freed, 8-bit samples are flipped to signed. */
    struct rv_app_sound *beep = rv_app_sound_load("sounds/beep.wav");
    assert(beep && LIVE(1) && host_sound_count == 2 && open_handles() == 0);
    struct fake_sound *beep_host = sound_at(1);
    assert(beep_host->byte_count == 4 && beep_host->sample_rate == 8000);
    assert(beep_host->channels == 1 && beep_host->bits == 8);
    assert(!memcmp(beep_host->data, "\x00\x7f\x80\xff", 4));
    assert(!rv_app_sound_load("sounds/float.wav") && !rv_app_sound_load("sounds/short.wav"));
    assert(!rv_app_sound_load("missing.wav") && !rv_app_sound_load("readme.txt"));
    assert(LIVE(1) && host_sound_count == 2 && open_handles() == 0);
    /* A data chunk declared past the end of the file is cut to the file. */
    struct rv_app_sound *cut = rv_app_sound_load("sounds/cut.wav");
    assert(cut && sound_at(2)->byte_count == 4 && LIVE(2));
    rv_app_sound_free(cut);
    assert(LIVE(1) && host_sound_count == 2);

    /* NULL and freed handles are ignored everywhere. */
    rv_app_sound_set_next(tone, NULL);
    rv_app_sound_set_next(tone, cut);
    assert(host->next == -1);
    rv_app_sound_set_volume(NULL, 1);
    rv_app_sound_set_volume(cut, 1);
    rv_app_sound_pause(cut);
    rv_app_sound_resume(NULL);
    rv_app_sound_stop(NULL);
    assert(!rv_app_sound_playing(NULL) && rv_app_sound_position(cut) == 0);
    rv_app_sound_free(NULL);
    rv_app_sound_free(cut);

    /* A freed next stays with the host while a sound names it, and goes
     * when nothing reaches it any more. */
    rv_app_sound_set_next(tone, beep);
    assert(host->next == 1);
    rv_app_sound_free(beep);
    assert(host_sound_count == 2 && LIVE(1) && beep_host->used);
    rv_app_sound_play(beep); /* A freed sound no longer plays. */
    assert(!rv_app_sound_playing(beep) && beep_host->plays == 0);
    struct rv_app_sound *third = rv_app_sound_create(frames, 8, 8000, 1, 16);
    assert(third && sound_at(2) && host_sound_count == 3);
    rv_app_sound_set_next(tone, third);
    assert(host_sound_count == 2 && LIVE(0) && !beep_host->used);
    /* A ring: freeing one keeps it, and does not stop the other, whose host
     * stop would run along the ring. */
    rv_app_sound_set_next(third, tone);
    rv_app_sound_play(tone);
    rv_app_sound_free(third);
    assert(host_sound_count == 2 && rv_app_sound_playing(tone));
    assert(sound_at(2)->state == GameSoundState_Playing);
    rv_app_sound_free(tone);
    assert(host_sound_count == 0 && !sound_at(0) && !sound_at(2));
    rv_app_sound_free(tone);

    /* Thirty-two sounds at once; shutdown stops and frees them all. */
    struct rv_app_sound *many[32];
    for (unsigned i = 0; i < 32; ++i)
        assert((many[i] = rv_app_sound_create(frames, 8, 8000, 1, 16)));
    assert(!rv_app_sound_create(frames, 8, 8000, 1, 16) && host_sound_count == 32);
    rv_app_sound_play(many[5]);
    sound_create_failures = 1;
    rv_app_sound_free(many[0]);
    assert(!rv_app_sound_create(frames, 8, 8000, 1, 16) && host_sound_count == 31);
    assert((many[0] = rv_app_sound_create(frames, 8, 8000, 1, 16)) && host_sound_count == 32);
    rv_app_sounds_free_all();
    assert(host_sound_count == 0 && sound_at(5) == NULL);
    assert(LIVE(0));
}


static void check_music(void)
{
    player = (struct fake_player){.state = GamePlaybackState_Playing,
                                  .has_list = 1,
                                  .index = 2,
                                  .count = 5,
                                  .position = 1000,
                                  .remaining = 179000,
                                  .repeat = GameRepeatMode_All,
                                  .order = GamePlaybackOrder_Songs,
                                  .volume = 120,
                                  .limit = 200,
                                  .eq = 4,
                                  .speed = 1000};
    assert(rv_app_music_has_playlist() && rv_app_music_state() == RV_APP_MUSIC_PLAYING);
    assert(rv_app_music_position() == 1000 && rv_app_music_duration() == 180000);
    assert(rv_app_music_track_index() == 2 && rv_app_music_track_count() == 5);

    /* The state is predicted as soon as a command is queued; the player
     * follows when the UI task runs. */
    rv_app_music_play_pause();
    assert(rv_app_music_state() == RV_APP_MUSIC_PAUSED && player.state == GamePlaybackState_Playing);
    rv_app_music_play_pause();
    assert(rv_app_music_state() == RV_APP_MUSIC_PLAYING && music_queued == 2);
    ui_task_runs();
    assert(rv_app_music_state() == RV_APP_MUSIC_PLAYING && player.state == GamePlaybackState_Playing);
    /* Stop drops the playlist; play_pause then does nothing. */
    rv_app_music_stop();
    assert(rv_app_music_state() == RV_APP_MUSIC_STOPPED);
    ui_task_runs();
    assert(player.state == GamePlaybackState_Stopped && !rv_app_music_has_playlist());
    assert(rv_app_music_repeat_mode() == RV_APP_MUSIC_REPEAT_OFF);
    assert(rv_app_music_shuffle_mode() == RV_APP_MUSIC_SHUFFLE_OFF);
    rv_app_music_play_pause();
    ui_task_runs();
    assert(rv_app_music_state() == RV_APP_MUSIC_STOPPED && rv_app_music_track_count() == 0);
    player.has_list = 1;
    player.count = 5;
    rv_app_music_play_pause();
    ui_task_runs();
    assert(rv_app_music_state() == RV_APP_MUSIC_PLAYING);

    /* Position, track index and scanning lag until the UI task runs. */
    rv_app_music_next();
    assert(rv_app_music_track_index() == 2);
    ui_task_runs();
    assert(rv_app_music_track_index() == 3);
    rv_app_music_previous();
    ui_task_runs();
    assert(rv_app_music_track_index() == 2);
    rv_app_music_seek(5000);
    assert(rv_app_music_position() == 1000);
    ui_task_runs();
    assert(rv_app_music_position() == 5000);
    rv_app_music_scan_forward();
    assert(rv_app_music_scanning() == 0);
    ui_task_runs();
    assert(rv_app_music_scanning() == 1);
    rv_app_music_scan_backward();
    ui_task_runs();
    assert(rv_app_music_scanning() == -1);
    rv_app_music_scan_stop();
    ui_task_runs();
    assert(rv_app_music_scanning() == 0);

    /* A track past the end is dropped; a valid one plays. */
    rv_app_music_play_pause();
    ui_task_runs();
    rv_app_music_play_track(5);
    assert(music_queued == 0 && rv_app_music_state() == RV_APP_MUSIC_PAUSED);
    rv_app_music_play_track(4);
    assert(rv_app_music_state() == RV_APP_MUSIC_PLAYING && rv_app_music_track_index() == 2);
    ui_task_runs();
    assert(rv_app_music_track_index() == 4 && player.state == GamePlaybackState_Playing);

    /* Settings read back at once from the bridge's cache. */
    rv_app_music_set_repeat_mode(RV_APP_MUSIC_REPEAT_ONE);
    rv_app_music_set_shuffle_mode(RV_APP_MUSIC_SHUFFLE_ALBUMS);
    assert(rv_app_music_repeat_mode() == RV_APP_MUSIC_REPEAT_ONE);
    assert(rv_app_music_shuffle_mode() == RV_APP_MUSIC_SHUFFLE_ALBUMS);
    assert(player.repeat == GameRepeatMode_All && player.order == GamePlaybackOrder_Songs);
    ui_task_runs();
    assert(player.repeat == GameRepeatMode_One && player.order == GamePlaybackOrder_Albums);
    assert(rv_app_music_volume() == 120 && rv_app_music_volume_limit() == 200);
    rv_app_music_set_volume(RV_APP_MUSIC_VOLUME_MAX);
    assert(rv_app_music_volume() == 200);
    rv_app_music_set_volume(50);
    assert(rv_app_music_volume() == 50 && music_queued == 0);
    assert(rv_app_music_eq() == 4);
    rv_app_music_set_eq_until_exit(3);
    assert(rv_app_music_eq() == 3 && player.eq_restore && player.eq_saved == 0);
    rv_app_music_set_eq(2);
    assert(rv_app_music_eq() == 2 && player.eq_saved == 2);
    ui_task_runs();
    assert(player.eq == 2);
    assert(rv_app_music_speed() == 1000);
    rv_app_music_set_speed(RV_APP_MUSIC_SPEED_MIN - 1);
    rv_app_music_set_speed(RV_APP_MUSIC_SPEED_MAX + 1);
    assert(rv_app_music_speed() == 1000 && music_queued == 0 && speed_calls == 0);
    rv_app_music_set_speed(1500);
    assert(rv_app_music_speed() == 1500 && speed_calls == 1);
    assert(rv_app_music_gain() == 0);
    rv_app_music_set_gain(40000);
    assert(rv_app_music_gain() == RV_APP_MUSIC_GAIN_MAX);
    rv_app_music_set_gain(-40000);
    assert(rv_app_music_gain() == -RV_APP_MUSIC_GAIN_MAX);
    rv_app_music_set_gain(-5);
    ui_task_runs();
    assert(player.speed == 1500 && player.gain == -5);

    /* pass_button hands only Play, Next and Previous releases to the host. */
    struct rv_app_input_event event = {.button = RV_APP_BUTTON_PLAY, .type = RV_APP_INPUT_PRESS};
    rv_app_music_pass_button(&event);
    event.button = RV_APP_BUTTON_MENU;
    event.type = RV_APP_INPUT_RELEASE;
    rv_app_music_pass_button(&event);
    event.button = RV_APP_BUTTON_CENTER;
    rv_app_music_pass_button(&event);
    rv_app_music_pass_button(NULL);
    assert(forwarded_count == 0);
    event.button = RV_APP_BUTTON_PLAY;
    event.timestamp_ms = 77;
    rv_app_music_pass_button(&event);
    assert(forwarded_count == 1 && forwarded[0].button == GameButton_Play);
    assert(forwarded[0].type == GameButtonEvent_Release);
    assert(rv_app_music_state() == RV_APP_MUSIC_PAUSED);
    ui_task_runs();

    /* The runtime hands every event to the app, then the releases of Play,
     * Next and Previous to the host, never Menu, Center or a press. The
     * opt-out is a field of the const application and is checked in the
     * emulator. */
    uint8_t buttons[6] = {RV_APP_BUTTON_NEXT,   RV_APP_BUTTON_NEXT, RV_APP_BUTTON_MENU,
                          RV_APP_BUTTON_CENTER, RV_APP_BUTTON_PLAY, RV_APP_BUTTON_PREVIOUS};
    uint8_t types[6] = {RV_APP_INPUT_PRESS,   RV_APP_INPUT_RELEASE, RV_APP_INPUT_RELEASE,
                        RV_APP_INPUT_RELEASE, RV_APP_INPUT_RELEASE, RV_APP_INPUT_RELEASE};
    forwarded_count = 0;
    for (unsigned i = 0; i < 6; ++i) {
        struct rv_app_eapp_input_event native = {
            .button = buttons[i], .type = types[i], .timestamp = 10 * i};
        rv_app_runtime_input(&native);
    }
    assert(received_count == 6 && received[1].button == RV_APP_BUTTON_NEXT);
    assert(received[1].type == RV_APP_INPUT_RELEASE && received[1].timestamp_ms == 10);
    assert(received[3].button == RV_APP_BUTTON_CENTER);
    assert(forwarded_count == 3 && forwarded[0].button == GameButton_Next);
    assert(forwarded[1].button == GameButton_Play && forwarded[2].button == GameButton_Previous);
    assert(forwarded[0].type == GameButtonEvent_Release && music_queued == 3);
    assert(rv_app_music_state() == RV_APP_MUSIC_PLAYING); /* Paused before the loop. */
    ui_task_runs();
    assert(rv_app_music_track_index() == 4 && player.state == GamePlaybackState_Playing);
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
    wav_files();
    check_sounds();
    check_music();
    assert(!pending.request);
    printf("path rules, requests, files, folders, data files, saves, sounds and music pass\n");
    return 0;
}
