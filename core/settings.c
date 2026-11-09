/* SPDX-License-Identifier: GPL-3.0-only */

/* Settings store: /Revola/settings.a and settings.b hold the same record stream;
 * a flush writes the file that was not loaded, with the generation advanced, so an
 * interrupted write leaves the other copy intact and the newer valid one wins at
 * boot. Apple's file API has open, read, write and close only, so there is no
 * rename and no size query; the header carries the length.
 *
 * File: header {magic, version, length, generation, fnv1a of the records}, then
 * records {owner, key, type, size, data} padded to a word. Unknown records are
 * carried through a rewrite. Adapted from RepriseOS payload/store.c (d3d7b4d). */

#include "osos-2.0.5.h"
#include "settings.h"
#include "sites.h"
#include <stddef.h>

#define MAGIC 0x54535652u /* RVST */
#define VERSION 1
#define FORK_DATA 0x64617461u /* 'data' */
#define MODE_READ 1
#define MODE_WRITE 2
#define MIN_CAPACITY 256

struct header {
    uint32_t magic;
    uint32_t version;
    uint32_t length;
    uint32_t generation;
    uint32_t checksum;
};

struct record {
    uint32_t owner;
    uint16_t key;
    uint8_t type;
    uint8_t size;
};

struct path {
    uint16_t volume;
    char text[258];
};

static const char *const files[2] = {"/Revola/settings.a", "/Revola/settings.b"};

static uint8_t *store; /* header followed by records */
static uint32_t capacity;
static uint32_t generation;
static uint8_t current; /* file index the store was loaded from or last written to */
static uint8_t dirty;

#ifdef CORE_SELFTEST
/* Per file: open result, read result, bytes got, header validity, body validity. */
struct core_settings_debug {
    int32_t open[2], read[2];
    uint32_t got[2], head_ok[2], body_ok[2], length[2], generation[2];
} core_settings_debug;
#define DEBUG(field, index, value) (core_settings_debug.field[index] = (value))
#else
#define DEBUG(field, index, value) ((void)0)
#endif

static struct header *header(void)
{
    return (struct header *)store;
}

static uint32_t fnv1a(const uint8_t *data, uint32_t bytes, uint32_t hash)
{
    while (bytes--)
        hash = (hash ^ *data++) * 16777619u;
    return hash;
}

uint32_t core_settings_owner(const char *id)
{
    uint32_t hash = 2166136261u;
    while (*id)
        hash = (hash ^ (uint8_t)*id++) * 16777619u;
    return hash;
}

static uint32_t padded(uint32_t size)
{
    return (size + 3) & ~3u;
}

static uint32_t checksum(const uint8_t *data, uint32_t length)
{
    return fnv1a(data + sizeof(struct header), length - sizeof(struct header), 2166136261u);
}

static void copy(void *to, const void *from, uint32_t bytes)
{
    uint8_t *out = to;
    const uint8_t *in = from;
    while (bytes--)
        *out++ = *in++;
}

static void move_down(uint8_t *to, const uint8_t *from, uint32_t bytes)
{
    while (bytes--)
        *to++ = *from++;
}

static int ensure(uint32_t needed)
{
    if (needed <= capacity)
        return 1;
    uint32_t grown = capacity ? capacity : MIN_CAPACITY;
    while (grown < needed)
        grown *= 2;
    uint8_t *larger = Heap_AllocateZeroed(grown);
    if (!larger)
        return 0;
    if (store) {
        copy(larger, store, header()->length);
        Heap_Free(store);
    }
    store = larger;
    capacity = grown;
    return 1;
}

static void reset_store(void)
{
    header()->magic = MAGIC;
    header()->version = VERSION;
    header()->length = sizeof(struct header);
    header()->generation = 0;
}

/* Walks records; returns the offset of the matching record or the stream end. */
static uint32_t find(uint32_t owner, uint16_t key)
{
    uint32_t at = sizeof(struct header), end = header()->length;
    while (at + sizeof(struct record) <= end) {
        const struct record *record = (const struct record *)(store + at);
        if (record->owner == owner && record->key == key)
            return at;
        at += sizeof(struct record) + padded(record->size);
    }
    return end;
}

static int valid(const uint8_t *data, uint32_t length)
{
    const struct header *h = (const struct header *)data;
    if (h->magic != MAGIC || h->version != VERSION || h->length != length || length % 4)
        return 0;
    if (h->checksum != checksum(data, length))
        return 0;
    uint32_t at = sizeof(struct header);
    while (at < length) {
        if (at + sizeof(struct record) > length)
            return 0;
        at += sizeof(struct record) + padded(((const struct record *)(data + at))->size);
    }
    return at == length;
}

static void set_path(struct path *path, const char *text)
{
    path->volume = 0;
    uint32_t i = 0;
    for (; text[i] && i < sizeof path->text - 1; i++)
        path->text[i] = text[i];
    path->text[i] = 0;
}

static int read_file(uint8_t index, uint8_t *buffer, uint32_t bytes, uint32_t *got)
{
    struct path path;
    void *handle;
    set_path(&path, files[index]);
    int opened = File_OpenLegacyPath(&path, FORK_DATA, MODE_READ, &handle);
    DEBUG(open, index, opened);
    if (opened)
        return 0;
    int result = File_ReadLegacyHandle(handle, buffer, bytes, 0, got);
    DEBUG(read, index, result);
    DEBUG(got, index, *got);
    File_CloseLegacyHandle(handle);
    return result == 0;
}

static int write_file(const char *name, const uint8_t *buffer, uint32_t bytes)
{
    struct path path;
    void *handle;
    uint32_t written = 0;
    set_path(&path, name);
    if (File_OpenLegacyPath(&path, FORK_DATA, MODE_WRITE, &handle))
        return 0;
    int result = File_WriteLegacyHandle(handle, buffer, bytes, 1, &written);
    File_CloseLegacyHandle(handle);
    return result == 0 && written == bytes;
}

/* Loads one copy into `store` when it is newer than what is loaded. */
static void load_file(uint8_t index, uint8_t *scratch, uint32_t scratch_bytes)
{
    struct header h;
    uint32_t got = 0;
    if (!read_file(index, (uint8_t *)&h, sizeof h, &got) || got != sizeof h)
        return;
    DEBUG(length, index, h.length);
    DEBUG(generation, index, h.generation);
    if (h.magic != MAGIC || h.length < sizeof h || h.length > 0x10000)
        return;
    DEBUG(head_ok, index, 1);
    if (store && h.generation <= generation && header()->length)
        return;
    uint8_t *buffer = scratch;
    if (h.length > scratch_bytes) {
        buffer = Heap_AllocateZeroed(h.length);
        if (!buffer)
            return;
    }
    int ok = read_file(index, buffer, h.length, &got) && got == h.length &&
             valid(buffer, h.length);
    DEBUG(body_ok, index, ok);
    if (ok && ensure(h.length)) {
        copy(store, buffer, h.length);
        generation = h.generation;
        current = index;
    }
    if (buffer != scratch)
        Heap_Free(buffer);
}

void core_settings_load(void)
{
    uint8_t scratch[512];
    if (!ensure(MIN_CAPACITY))
        return;
    reset_store();
    header()->length = 0; /* marks "nothing loaded" for load_file */
    load_file(0, scratch, sizeof scratch);
    load_file(1, scratch, sizeof scratch);
    if (!header()->length) {
        reset_store();
        current = 1; /* the first flush writes settings.a */
    }
    dirty = 0;
}

uint32_t core_settings_get(uint32_t owner, uint16_t key, void *out, uint32_t out_capacity)
{
    if (!store)
        return 0;
    uint32_t at = find(owner, key);
    if (at == header()->length)
        return 0;
    const struct record *record = (const struct record *)(store + at);
    uint32_t bytes = record->size < out_capacity ? record->size : out_capacity;
    copy(out, store + at + sizeof *record, bytes);
    return bytes;
}

static void remove_at(uint32_t at)
{
    const struct record *record = (const struct record *)(store + at);
    uint32_t bytes = sizeof *record + padded(record->size);
    uint32_t end = header()->length;
    move_down(store + at, store + at + bytes, end - at - bytes);
    header()->length = end - bytes;
    dirty = 1;
}

int core_settings_set(uint32_t owner, uint16_t key, uint8_t type, const void *data,
                      uint32_t size)
{
    if (!store || size > 255)
        return -1;
    uint32_t at = find(owner, key);
    if (at != header()->length) {
        struct record *record = (struct record *)(store + at);
        if (record->size == size && record->type == type) {
            uint8_t *old = store + at + sizeof *record;
            const uint8_t *in = data;
            for (uint32_t i = 0; i < size; i++) {
                if (old[i] != in[i]) {
                    old[i] = in[i];
                    dirty = 1;
                }
            }
            return 0;
        }
        remove_at(at);
    }
    uint32_t bytes = sizeof(struct record) + padded(size);
    if (!ensure(header()->length + bytes))
        return -1;
    struct record *record = (struct record *)(store + header()->length);
    record->owner = owner;
    record->key = key;
    record->type = type;
    record->size = (uint8_t)size;
    uint8_t *out = (uint8_t *)(record + 1);
    copy(out, data, size);
    for (uint32_t i = size; i < padded(size); i++)
        out[i] = 0;
    header()->length += bytes;
    dirty = 1;
    return 0;
}

void core_settings_reset_owner(uint32_t owner)
{
    if (!store)
        return;
    uint32_t at = sizeof(struct header);
    while (at < header()->length) {
        const struct record *record = (const struct record *)(store + at);
        if (record->owner == owner)
            remove_at(at);
        else
            at += sizeof *record + padded(record->size);
    }
}

void core_settings_flush(void)
{
    if (!store || !dirty)
        return;
    uint8_t target = current ^ 1;
    header()->generation = generation + 1;
    header()->checksum = checksum(store, header()->length);
    if (!write_file(files[target], store, header()->length))
        return; /* stays dirty; the next flush point retries */
    generation = header()->generation;
    current = target;
    dirty = 0;
}

int32_t core_settings_get_int(uint32_t owner, uint16_t key, int32_t fallback)
{
    int32_t value;
    return core_settings_get(owner, key, &value, sizeof value) == sizeof value ? value
                                                                             : fallback;
}

int core_settings_set_int(uint32_t owner, uint16_t key, int32_t value)
{
    return core_settings_set(owner, key, CORE_SETTING_INT, &value, sizeof value);
}

static uint32_t flush_then_sleep(uint32_t mode, const struct core_next *next)
{
    core_settings_flush();
    return core_next_call(next, mode, 0, 0, 0);
}

static uint32_t save_all(uint32_t preferences, const struct core_next *next)
{
    uint32_t result = core_next_call(next, preferences, 0, 0, 0);
    core_settings_flush();
    return result;
}

static uint32_t reset_all(uint32_t model, const struct core_next *next)
{
    uint32_t result = core_next_call(next, model, 0, 0, 0);
    if (store && header()->length > sizeof(struct header)) {
        header()->length = sizeof(struct header);
        dirty = 1;
        core_settings_flush();
    }
    return result;
}

static const struct core_hook hooks[] = {
    {CORE_SITE_Sleep_Idle_call, CORE_HOOK_FILTER, 0, flush_then_sleep},
    {CORE_SITE_Sleep_Request_call, CORE_HOOK_FILTER, 0, flush_then_sleep},
    {CORE_SITE_Preferences_FlushIfDirty_call, CORE_HOOK_FILTER, 0, save_all},
    {CORE_SITE_SettingsModel_ResetAllSettings_call, CORE_HOOK_FILTER, 0, reset_all},
};

void core_settings_bind(void)
{
    for (uint32_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        core_hook_bind(&hooks[i]);
}
