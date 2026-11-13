/* SPDX-License-Identifier: GPL-3.0-only */

/* Extension loader. Each extension is one file /Revola/Extensions/<id>.rvx holding
 * its manifest, modules and assets (loader/rvx.h). At boot the enabled ones are
 * read into the Apple heap, relocated in place and entered through the rv_api
 * table; their descriptor's hooks are bound in order and a collision unloads the
 * extension with the site recorded for Settings > Extensions. A disabled
 * extension is never read past its manifest and keeps no record: zero bytes.
 *
 * Resident assets are read with the main module into one block and stay there
 * as stored until it unloads. Other assets are found by streaming the rest of
 * the catalog when asked for.
 *
 * Every block the loader hands out carries an 8-byte header naming its owner, so
 * the record's live and peak counters are exact. Images and hook arrays are not
 * freed on unload but retired: the dispatcher publishes the new handler list at
 * once, and the block is freed two grace ticks later, when nothing can still be
 * inside it. */

#include "loader/data.h"
#include "loader/file.h"
#include "loader/loader.h"
#include "recovery.h"
#include "loader/lz.h"
#include "ui/lists.h"
#include "ui/menus.h"
#include "ui/resources.h"
#include "osos-2.0.5.h"
#include "panic/panic.h"
#include "timer.h"
#include "trace.h"
#include "rv_api.h"
#include "loader/rvx.h"
#include "ui/screens.h"
#include "ui/gfx.h"
#include "settings.h"
#include "hooks/sites.h"
#include "ui/bitmaps.h"
#include "ui/format.h"
#include "ui/panes.h"
#include "storage.h"
#include <stddef.h>

#define ROOT "/Revola/Extensions"
#define SUFFIX ".rvx"
#define MANIFEST_MAGIC 0x464d5652u /* RVMF */
#define MANIFEST_VERSION 3
#define MANIFEST_MAX CORE_MANIFEST_MAX
#define MAX_MODULES 15
#define IDLE_TICKS 10 /* grace ticks a screen module stays loaded with no screen open */
#define RELOC_CHUNK 32
#define CATALOG_CHUNK 8
#define PATH_MAX_BYTES 96
#define MAX_ENTRIES 0xffff
#define MAX_PAYLOAD (16u << 20) /* keeps sums of payload sizes far from overflow */

/* The highest preload class read into RAM with the main module, from Optimize
 * For: Auto preloads the optional assets on a hard disk, where a read costs a
 * spin-up, and leaves them on flash. */
static uint8_t preload_limit = RVX_PRELOAD_ALWAYS;
static int preload_refresh_pending;

static uint8_t chosen_preload_limit(void)
{
    int32_t mode = core_settings_get_int(CORE_SETTINGS_OWNER_CORE, CORE_KEY_OPTIMIZE,
                                         CORE_OPTIMIZE_AUTO);
    if (mode == CORE_OPTIMIZE_AUTO)
        mode = core_storage_is_flash(ATA_Identify) ? CORE_OPTIMIZE_MEMORY
                                                   : CORE_OPTIMIZE_PERFORMANCE;
    return mode == CORE_OPTIMIZE_PERFORMANCE ? RVX_PRELOAD_OPTIONAL : RVX_PRELOAD_ALWAYS;
}

struct manifest_header {
    uint32_t magic;
    uint32_t version;
    uint32_t api;
    uint32_t flags;
    uint32_t module_count;
    uint32_t site_count;
    uint32_t strings_bytes;
    uint16_t text[CORE_MANIFEST_TEXTS];
    uint16_t content; /* first of content_count consecutive folder names */
    uint32_t content_count;
};

struct manifest_module {
    uint16_t name;
    uint8_t trigger; /* 0 always, 1 screen, 2 manual */
    uint8_t screen_count;
    uint16_t screens; /* first of screen_count consecutive strings */
};

/* A raw hook site; kind is a core_site_kind. */
struct manifest_site {
    uint32_t address;
    uint32_t expected;
    uint8_t kind;
    uint8_t argc;
    uint16_t name;
};

enum { TRIGGER_ALWAYS, TRIGGER_SCREEN, TRIGGER_MANUAL };

struct core_module {
    void *image;
    uint32_t bytes;
    const struct rv_descriptor *descriptor; /* NULL for a module that returned none */
    struct core_hook *hooks;                /* core-owned copies of its descriptor's hooks */
    uint8_t loaded : 1;
    uint8_t unload_pending : 1;  /* module_unload asked while a screen was open */
    uint8_t unload_notified : 1; /* its unload callback ran ahead of the unload */
    uint8_t idle;                /* ticks without an open screen */
    uint8_t screens_open;        /* its screens on the navigation stack */
};

/* Header of every loader block. `owner` becomes the retire link once retired. */
struct block {
    union {
        struct rv_extension_record *owner;
        struct block *next;
    };
    uint32_t bytes;
};

struct rv_extension_record *core_extensions;
uint8_t core_loader_core_only;
uint32_t core_loader_crashed;

static struct block *retiring; /* unlinked since the last tick */
static struct block *retired;  /* unlinked before the last tick; freed at the next */

static uint32_t fnv1a(const char *text)
{
    return core_settings_owner(text);
}

/* Whether the extension `owner` is on before the listener has set it: the
 * first-party ones in loader/bundled.def are, every other extension is off. */
static int default_on(uint32_t owner)
{
    static const char *const bundled[] = {
#define BUNDLED(id) id,
#include "loader/bundled.def"
#undef BUNDLED
    };
    for (uint32_t i = 0; i < sizeof bundled / sizeof *bundled; i++)
        if (fnv1a(bundled[i]) == owner)
            return 1;
    return 0;
}

static int read_all(void *handle, void *buffer, uint32_t bytes)
{
    uint32_t got = 0;
    return core_file_read(handle, buffer, bytes, &got) && got == bytes;
}

static void copy(void *to, const void *from, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes; i++)
        ((uint8_t *)to)[i] = ((const uint8_t *)from)[i];
}

static uint32_t length(const char *text)
{
    uint32_t n = 0;
    while (text[n])
        n++;
    return n;
}

static int same_name(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = core_loader_lower(*a), y = core_loader_lower(*b);
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
}

uint32_t core_heap_bytes;

void core_loader_charge(struct rv_extension_record *owner, int32_t bytes)
{
    if (!owner) {
        core_heap_bytes += (uint32_t)bytes;
        return;
    }
    owner->live += (uint32_t)bytes;
    if (owner->live > owner->peak)
        owner->peak = owner->live;
}

uint32_t core_heap_cost(uint32_t bytes)
{
    uint32_t block = (((bytes + 3) & ~3u) + 8 + 7) & ~7u;
    return block < 16 ? 16 : block;
}

void *core_heap_allocate(struct rv_extension_record *owner, uint32_t bytes)
{
    void *memory = Heap_AllocateZeroed(bytes);
    if (memory)
        core_loader_charge(owner, (int32_t)core_heap_cost(bytes));
    return memory;
}

void core_heap_free(struct rv_extension_record *owner, void *memory, uint32_t bytes)
{
    if (!memory)
        return;
    core_loader_charge(owner, -(int32_t)core_heap_cost(bytes));
    Heap_Free(memory);
}

void core_heap_move(struct rv_extension_record *from, struct rv_extension_record *to,
                    uint32_t bytes)
{
    core_loader_charge(from, -(int32_t)core_heap_cost(bytes));
    core_loader_charge(to, (int32_t)core_heap_cost(bytes));
}

/* Owned allocation: an 8-byte header before the block, counted on the owner. */
static void *allocate(struct rv_extension_record *owner, uint32_t bytes)
{
    struct block *block = Heap_AllocateZeroed(sizeof *block + bytes);
    if (!block)
        return NULL;
    block->owner = owner;
    block->bytes = bytes;
    core_loader_charge(owner, (int32_t)bytes);
    return block + 1;
}

static struct block *header_of(void *memory)
{
    return (struct block *)memory - 1;
}

static void release(struct rv_extension_record *owner, void *memory)
{
    struct block *block = header_of(memory);
    if (block->owner != owner)
        return;
    core_loader_charge(owner, -(int32_t)block->bytes);
    Heap_Free(block);
}

/* Code or handler storage: freed two ticks later, see the file comment. */
static void retire(void *memory)
{
    struct block *block = header_of(memory);
    core_loader_charge(block->owner, -(int32_t)block->bytes);
    block->next = retiring;
    retiring = block;
}

/* Manifest access: header, sites, modules, strings. Offsets were bounds-checked
 * when the manifest was read. */
static const struct manifest_site *site_record(const struct rv_extension_record *record,
                                               uint32_t index)
{
    return (const struct manifest_site *)(record->manifest + sizeof(struct manifest_header)) +
           index;
}

static const struct manifest_module *module_record(const struct rv_extension_record *record,
                                                   uint32_t index)
{
    const struct manifest_header *header = (const struct manifest_header *)record->manifest;
    return (const struct manifest_module *)(record->manifest + sizeof *header +
                                            header->site_count * sizeof(struct manifest_site)) +
           index;
}

static uint32_t tables_bytes(const struct manifest_header *header)
{
    return sizeof *header + header->module_count * sizeof(struct manifest_module) +
           header->site_count * sizeof(struct manifest_site);
}

static const char *strings(const uint8_t *buffer)
{
    return (const char *)buffer + tables_bytes((const struct manifest_header *)buffer);
}

static const char *text(const struct rv_extension_record *record, uint16_t offset)
{
    return strings(record->manifest) + offset;
}

/* A content folder name inside the string table: a capital, then 1 to 15
 * lower-case letters or digits, none of the core's own folders. */
static int content_folder_valid(const char *folder, const char *end)
{
    static const char *const core_folders[] = {"Extensions", "Data", "Settings", "Firmware",
                                               "Crashes"};
    uint32_t n = 0;
    if (folder >= end || *folder < 'A' || *folder > 'Z')
        return 0;
    for (const char *c = folder + 1; c < end && *c; c++, n++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9')))
            return 0;
    if (n < 1 || n > 15)
        return 0;
    for (uint32_t i = 0; i < sizeof core_folders / sizeof *core_folders; i++)
        if (same_name(folder, core_folders[i]))
            return 0;
    return 1;
}

static enum core_extension_failure manifest_valid(const uint8_t *buffer, uint32_t bytes)
{
    const struct manifest_header *header = (const struct manifest_header *)buffer;
    if (bytes < offsetof(struct manifest_header, text) || header->magic != MANIFEST_MAGIC)
        return CORE_EXTENSION_UNREADABLE;
    if (header->version != MANIFEST_VERSION || (header->api >> 16) != RV_API_MAJOR)
        return CORE_EXTENSION_INCOMPATIBLE;
    if (header->module_count < 1 || header->module_count > MAX_MODULES ||
        header->site_count > CORE_RAW_SITES || !header->strings_bytes ||
        header->strings_bytes > bytes || buffer[bytes - 1] != 0)
        return CORE_EXTENSION_UNREADABLE;
    uint32_t table = tables_bytes(header);
    if (bytes != table + header->strings_bytes)
        return CORE_EXTENSION_UNREADABLE;
    for (uint32_t i = 0; i < CORE_MANIFEST_TEXTS; i++)
        if (header->text[i] >= header->strings_bytes)
            return CORE_EXTENSION_UNREADABLE;
    if (header->content_count > CORE_MANIFEST_CONTENT_MAX ||
        (header->content_count && header->content >= header->strings_bytes))
        return CORE_EXTENSION_UNREADABLE;
    const char *folder = (const char *)buffer + table + header->content;
    for (uint32_t i = 0; i < header->content_count; i++) {
        if (!content_folder_valid(folder, (const char *)buffer + bytes))
            return CORE_EXTENSION_UNREADABLE;
        folder += length(folder) + 1;
    }
    const struct manifest_site *sites = (const struct manifest_site *)(buffer + sizeof *header);
    for (uint32_t i = 0; i < header->site_count; i++)
        if (sites[i].name >= header->strings_bytes)
            return CORE_EXTENSION_UNREADABLE;
    const struct manifest_module *modules = (const struct manifest_module *)(sites + header->site_count);
    if (modules[0].trigger != TRIGGER_ALWAYS)
        return CORE_EXTENSION_UNREADABLE;
    for (uint32_t i = 0; i < header->module_count; i++)
        if (modules[i].name >= header->strings_bytes ||
            modules[i].screens >= header->strings_bytes || modules[i].trigger > TRIGGER_MANUAL)
            return CORE_EXTENSION_UNREADABLE;
    if (!buffer[table + header->text[CORE_MANIFEST_ID]])
        return CORE_EXTENSION_UNREADABLE;
    return CORE_EXTENSION_OK;
}

/* Opens ROOT "/" file SUFFIX and reads its header; 0 when either fails. */
static int open_extension(const char *file, void **handle, struct rvx_header *header,
                          uint8_t *failure)
{
    char path[PATH_MAX_BYTES];
    uint32_t n = 0, got = 0;
    if (failure)
        *failure = CORE_EXTENSION_UNREADABLE;
    const char *parts[3] = {ROOT "/", file, SUFFIX};
    for (uint32_t p = 0; p < 3; p++)
        for (const char *c = parts[p]; *c; c++) {
            if (n >= PATH_MAX_BYTES - 1)
                return 0;
            path[n++] = *c;
        }
    path[n] = 0;
    if (!core_file_open(path, CORE_FILE_READ, handle))
        return 0;
    if (core_file_read(*handle, header, sizeof *header, &got) && got == sizeof *header &&
        header->magic == RVX_MAGIC) {
        if (header->format == RVX_FORMAT && (header->api >> 16) == RV_API_MAJOR &&
            header->firmware == CORE_FIRMWARE_ID)
            return 1;
        if (failure)
            *failure = CORE_EXTENSION_INCOMPATIBLE;
    }
    core_file_close(*handle);
    return 0;
}

/* The manifest of `file` into `buffer`; its size, or 0 when it is unreadable. */
static uint32_t read_manifest(const char *file, uint8_t *buffer, uint8_t *failure)
{
    void *handle;
    struct rvx_header header;
    uint32_t got = 0;
    if (!open_extension(file, &handle, &header, failure))
        return 0;
    int ok = header.manifest_bytes <= MANIFEST_MAX &&
             core_file_read(handle, buffer, header.manifest_bytes, &got) &&
             got == header.manifest_bytes;
    core_file_close(handle);
    enum core_extension_failure result = ok ? manifest_valid(buffer, got)
                                           : CORE_EXTENSION_UNREADABLE;
    if (failure)
        *failure = (uint8_t)result;
    return result == CORE_EXTENSION_OK ? got : 0;
}

/* Catalog */

static uint32_t catalog_offset(const struct rv_extension_record *record)
{
    return sizeof(struct rvx_header) + ((record->manifest_bytes + 3) & ~3u);
}

/* Checks an entry before anything trusts it; offsets past the end of the file
 * show up as short reads. `previous` is the preload class of the entry before
 * it, 0 for a module: the assets are sorted by class. */
static int entry_valid(const struct rv_extension_record *record, const struct rvx_entry *e,
                       uint32_t index, uint8_t previous)
{
    uint32_t payloads = catalog_offset(record) + record->entry_count * sizeof *e;
    int kind = index < record->module_count
                   ? e->kind == RVX_MODULE && e->codec == RVX_RAW && e->preload == RVX_PRELOAD_ALWAYS
                   : (e->kind == RVX_DATA || e->kind == RVX_BITMAP) &&
                         e->preload <= RVX_PRELOAD_NEVER && e->preload >= previous;
    return kind && e->codec <= RVX_LZ && (e->codec == RVX_LZ || e->stored == e->bytes) &&
           e->offset % 4 == 0 && e->offset >= payloads && e->stored <= MAX_PAYLOAD &&
           e->bytes <= MAX_PAYLOAD;
}

/* Streams catalog entries [index, end) through the stack. */
struct catalog {
    void *handle;
    const struct rv_extension_record *record;
    uint32_t index, end, have, at;
    uint8_t previous; /* the preload class of the last entry read */
    struct rvx_entry chunk[CATALOG_CHUNK];
};

static int catalog_start(struct catalog *c, const struct rv_extension_record *record,
                         void *handle, uint32_t first, uint32_t end)
{
    c->handle = handle;
    c->record = record;
    c->index = first;
    c->end = end;
    c->have = c->at = 0;
    c->previous = RVX_PRELOAD_ALWAYS;
    return core_file_seek(handle, catalog_offset(record) + first * sizeof c->chunk[0]);
}

/* The next valid entry, or NULL at the end and for a bad or unreadable one;
 * c->index == c->end tells the two apart. */
static const struct rvx_entry *catalog_next(struct catalog *c)
{
    if (c->index == c->end)
        return NULL;
    if (c->at == c->have) {
        uint32_t n = c->end - c->index < CATALOG_CHUNK ? c->end - c->index : CATALOG_CHUNK;
        if (!read_all(c->handle, c->chunk, n * sizeof c->chunk[0]))
            return NULL;
        c->have = n;
        c->at = 0;
    }
    const struct rvx_entry *e = &c->chunk[c->at];
    if (!entry_valid(c->record, e, c->index, c->previous))
        return NULL;
    c->previous = e->preload;
    c->at++;
    c->index++;
    return e;
}

static int read_entry(void *handle, const struct rv_extension_record *record, uint32_t index,
                      struct rvx_entry *out)
{
    struct catalog c;
    const struct rvx_entry *e =
        catalog_start(&c, record, handle, index, index + 1) ? catalog_next(&c) : NULL;
    if (e)
        *out = *e;
    return e != NULL;
}

static void drop_preloaded(struct rv_extension_record *record)
{
    if (record->preloaded)
        retire(record->preloaded);
    record->preloaded = NULL;
    record->preloaded_count = 0;
}

/* The asset provider's stored bitmap for entry `e` of `record`, when it has one
 * that is no larger than the room for the entry's own. */
static int provided_stream(const struct rv_extension_record *record, const struct rvx_entry *e,
                           struct rv_asset_stream *stream)
{
    return e->kind == RVX_BITMAP && core_bitmaps_replacement(record->owner, e->name, stream) &&
           stream->codec <= RV_ASSET_LZ &&
           (stream->codec == RV_ASSET_LZ || stream->stored == stream->bytes) &&
           stream->stored <= MAX_PAYLOAD && stream->bytes <= MAX_PAYLOAD;
}

/* Reads the asset entries up to the preload limit, then their payloads in file
 * order, into one block: the entries with offsets rebased onto it, then the
 * payloads word-aligned. A bitmap the asset provider replaces carries the
 * provider's stored bitmap instead, marked CORE_PRELOADED_PROVIDED; when the
 * provider fails to deliver it, the extension's own. A block that does not
 * fit or cannot be read leaves the assets on disk; the load itself is fine. */
static int load_preloaded(struct rv_extension_record *record, void *handle)
{
    uint32_t first = record->module_count, count = 0, bytes = 0;
    struct catalog c;
    const struct rvx_entry *e;
    struct rv_asset_stream stream;
    if (!catalog_start(&c, record, handle, first, record->entry_count))
        return 0;
    while ((e = catalog_next(&c)) && e->preload <= preload_limit) {
        uint32_t stored = e->stored;
        if (provided_stream(record, e, &stream) && stream.stored > stored)
            stored = stream.stored;
        bytes += (stored + 3) & ~3u;
        count++;
    }
    if (!e && c.index != c.end)
        return 0;
    if (!count)
        return 1;
    bytes += count * sizeof(struct rvx_entry);
    struct rvx_entry *block = allocate(record, bytes);
    if (!block)
        return 1;
    uint32_t at = count * sizeof *block;
    int ok = core_file_seek(handle, catalog_offset(record) + first * sizeof *block) &&
             read_all(handle, block, count * sizeof *block);
    for (uint32_t i = 0; ok && i < count; i++) {
        struct rvx_entry *r = &block[i];
        uint8_t *payload = (uint8_t *)block + at;
        ok = entry_valid(record, r, first + i, i ? block[i - 1].preload : RVX_PRELOAD_ALWAYS) &&
             r->stored <= bytes - at;
        if (ok && provided_stream(record, r, &stream) && stream.stored <= bytes - at &&
            core_bitmaps_replacement_read(record->owner, r->name, payload, stream.stored)) {
            r->codec = stream.codec;
            r->stored = stream.stored;
            r->bytes = stream.bytes;
            r->reserved = CORE_PRELOADED_PROVIDED;
        } else if (ok) {
            ok = core_file_seek(handle, r->offset) && read_all(handle, payload, r->stored);
        }
        r->offset = at;
        at += (r->stored + 3) & ~3u;
    }
    if (!ok) {
        release(record, block);
        return 1;
    }
    record->preloaded = block;
    record->preloaded_count = count;
    return 1;
}

/* Reads the preloaded block of a loaded extension again, for the current limit
 * and provider; a file that is gone or changed leaves its assets on disk. The
 * old block is retired, since a decode may still read it. */
static void reload_preloaded(struct rv_extension_record *record)
{
    struct rvx_entry *old = record->preloaded;
    struct rvx_header file;
    void *handle;
    record->preloaded = NULL;
    record->preloaded_count = 0;
    if (open_extension(record->file, &handle, &file, NULL)) {
        if (file.manifest_bytes == record->manifest_bytes &&
            file.entry_count == record->entry_count && file.body_hash == record->body_hash)
            load_preloaded(record, handle);
        core_file_close(handle);
    }
    if (old)
        retire(old);
}

void core_loader_preload_refresh(void)
{
    preload_refresh_pending = 1;
}

static const struct rvx_entry *preloaded_entry(const struct rv_extension_record *record,
                                               uint32_t name)
{
    for (uint32_t i = 0; i < record->preloaded_count; i++)
        if (record->preloaded[i].name == name)
            return &record->preloaded[i];
    return NULL;
}

/* Opens the file and finds asset `name` in its catalog; the handle stays open
 * on success. */
static int find_on_disk(const struct rv_extension_record *record, uint32_t name, void **handle,
                        struct rvx_entry *out)
{
    struct rvx_header file;
    struct catalog c;
    const struct rvx_entry *e;
    if (!open_extension(record->file, handle, &file, NULL))
        return 0;
    if (file.manifest_bytes == record->manifest_bytes && file.entry_count == record->entry_count &&
        catalog_start(&c, record, *handle, record->module_count, record->entry_count))
        while ((e = catalog_next(&c)))
            if (e->name == name) {
                *out = *e;
                return 1;
            }
    core_file_close(*handle);
    return 0;
}

const void *core_loader_preloaded(const struct rv_extension_record *record, uint32_t name,
                                  const struct rvx_entry **entry)
{
    const struct rvx_entry *e = preloaded_entry(record, name);
    if (!e)
        return NULL;
    *entry = e;
    return (const uint8_t *)record->preloaded + e->offset;
}

static void *load_asset(struct rv_extension_record *record, uint32_t name, uint32_t *bytes,
                        uint8_t *kind)
{
    const struct rvx_entry *r;
    const uint8_t *memory = core_loader_preloaded(record, name, &r);
    struct rvx_entry entry;
    struct core_lz_input in;
    if (memory && (r->reserved & CORE_PRELOADED_PROVIDED))
        memory = NULL;
    void *handle = NULL;
    int ok = 1;
    if (memory) {
        entry = *r;
        core_lz_from_memory(&in, memory, entry.stored);
    } else {
        if (!find_on_disk(record, name, &handle, &entry))
            return NULL;
        ok = core_file_seek(handle, entry.offset);
        core_lz_from_file(&in, handle, entry.stored);
    }
    uint8_t *out = ok ? allocate(record, entry.bytes) : NULL;
    if (out) {
        if (entry.codec == RVX_LZ)
            ok = core_lz_decode(&in, out, entry.bytes);
        else if (memory)
            copy(out, memory, entry.bytes);
        else
            ok = read_all(handle, out, entry.bytes);
    }
    if (handle)
        core_file_close(handle);
    if (out && !ok) {
        release(record, out);
        return NULL;
    }
    if (out) {
        *bytes = entry.bytes;
        *kind = entry.kind;
    }
    return out;
}

void *core_loader_asset(struct rv_extension_record *record, uint32_t name, uint32_t *bytes,
                        uint8_t *kind)
{
    core_trace(RV_TRACE_ASSET_BEGIN, record->owner);
    void *block = load_asset(record, name, bytes, kind);
    core_trace(RV_TRACE_ASSET_END, record->owner);
    return block;
}

void *core_loader_block(uint32_t bytes)
{
    return allocate(NULL, bytes);
}

void *core_loader_allocate(struct rv_extension_record *record, uint32_t bytes)
{
    return allocate(record, bytes);
}

void core_loader_retire(void *block)
{
    retire(block);
}

void core_loader_free(struct rv_extension_record *record, void *block)
{
    release(record, block);
}

/* rv_api */

static void release_raw_sites(struct rv_extension_record *record)
{
    while (record->raw_count)
        core_raw_release(record->raw_sites[--record->raw_count]);
}

static struct rv_extension_record *valid_handle(rv_extension_handle self)
{
    for (struct rv_extension_record *r = core_extensions; r; r = r->next)
        if (r == self)
            return r;
    return NULL;
}

static int site_index(const struct rv_extension_record *record, uint32_t id)
{
    if ((id & ~0xffu) == RV_RAW_SITE(0))
        return (id & 0xff) < record->raw_count ? record->raw_sites[id & 0xff] : -1;
    for (uint32_t i = 0; i < CORE_SITE_COUNT; i++)
        if (core_site_ids[i] == id)
            return (int)i;
    return -1;
}

/* Claims the manifest's raw sites in order; on failure releases what it claimed
 * and leaves the reason and the site on the record. */
static int claim_raw_sites(struct rv_extension_record *record)
{
    const struct manifest_header *header = (const struct manifest_header *)record->manifest;
    for (uint32_t i = 0; i < header->site_count; i++) {
        const struct manifest_site *site = site_record(record, i);
        uint32_t index;
        int claimed = core_raw_claim(record->owner, site->address, site->kind, site->expected,
                                     site->argc, &index);
        if (claimed == CORE_RAW_OK) {
            record->raw_sites[record->raw_count++] = (uint8_t)index;
            continue;
        }
        record->conflict_site = RV_RAW_SITE(i);
        if (claimed == CORE_RAW_OVERLAP && index < CORE_SITE_COUNT)
            record->conflict_site = core_site_ids[index];
        record->failure = claimed == CORE_RAW_MISMATCH ? CORE_EXTENSION_SITE_MISMATCH
                          : claimed == CORE_RAW_OVERLAP ? CORE_EXTENSION_CONFLICT
                          : claimed == CORE_RAW_FULL    ? CORE_EXTENSION_NO_MEMORY
                                                        : CORE_EXTENSION_BAD_MANIFEST;
        release_raw_sites(record);
        return 0;
    }
    return 1;
}

int core_loader_shares_content(const struct rv_extension_record *a,
                               const struct rv_extension_record *b)
{
    const char *mine, *theirs;
    for (uint32_t i = 0; (mine = core_loader_manifest_content(a->manifest, i)); i++)
        for (uint32_t j = 0; (theirs = core_loader_manifest_content(b->manifest, j)); j++)
            if (same_name(mine, theirs))
                return 1;
    return 0;
}

/* A content folder has one owner: the first extension to load with it. A
 * second one fails as a conflict, with no site to name. */
static int claim_content(struct rv_extension_record *record)
{
    for (const struct rv_extension_record *r = core_extensions; r; r = r->next)
        if (r != record && r->modules[0].loaded && core_loader_shares_content(record, r)) {
            record->failure = CORE_EXTENSION_CONFLICT;
            record->conflict_site = 0;
            release_raw_sites(record);
            return 0;
        }
    return 1;
}

static int result_of(int core_result)
{
    switch (core_result) {
    case CORE_HOOK_OK:
        return RV_OK;
    case CORE_HOOK_CONFLICT:
        return RV_CONFLICT;
    case CORE_HOOK_NO_MEMORY:
        return RV_NO_MEMORY;
    case CORE_HOOK_NOT_BOUND:
        return RV_NOT_BOUND;
    default:
        return RV_BAD_SITE;
    }
}

/* Declarations are checked once at load, so a mistake fails the load instead
 * of misbehaving later. */
static int valid_row(const struct rv_row *row)
{
    switch (row->kind) {
    case RV_ROW_KIND_PUSH:
        return row->label && row->screen;
    case RV_ROW_KIND_SETTING:
        return row->label && row->key != RV_SETTING_KEY_ENABLED && row->key != RV_SETTING_KEY_ALL;
    case RV_ROW_KIND_ACTION:
        return row->label && row->action;
    case RV_ROW_KIND_INFO:
        return row->label != NULL;
    default:
        return 0;
    }
}

static int valid_screen(const struct rv_screen *s)
{
    if (!s->id || !s->title || s->layout > RV_LAYOUT_SPLIT || s->transition > RV_TRANSITION_FROM_PANE)
        return 0;
    if (s->context_count) {
        if (!s->context || s->context_count > CORE_MAX_CONTEXT_ROWS ||
            (s->kind != RV_SCREEN_KIND_LIST && s->kind != RV_SCREEN_KIND_DYNAMIC_LIST))
            return 0;
        for (uint32_t i = 0; i < s->context_count; i++)
            if (!valid_row(&s->context[i].row) || (s->context[i].row.kind != RV_ROW_KIND_ACTION &&
                                                   s->context[i].row.kind != RV_ROW_KIND_PUSH))
                return 0;
    }
    switch (s->kind) {
    case RV_SCREEN_KIND_LIST:
        if (!s->list.rows && s->list.count)
            return 0;
        for (uint32_t i = 0; i < s->list.count; i++)
            if (!valid_row(&s->list.rows[i]))
                return 0;
        return 1;
    case RV_SCREEN_KIND_CHOICE:
    case RV_SCREEN_KIND_SLIDER:
    case RV_SCREEN_KIND_MULTI_CHOICE:
        return s->choice.labels && s->choice.count &&
               (s->kind != RV_SCREEN_KIND_MULTI_CHOICE || s->choice.count <= 32);
    case RV_SCREEN_KIND_TEXT:
        return s->text.paragraphs || !s->text.count;
    case RV_SCREEN_KIND_CANVAS:
        return s->canvas.draw != NULL;
    case RV_SCREEN_KIND_DYNAMIC_LIST:
        return s->dynamic.count && s->dynamic.row && s->dynamic.detail <= RV_DETAIL_BELOW_SUBMENU;
    default:
        return 0;
    }
}

/* The main module declares everything; another module only its hooks,
 * screens and unload. */
static int valid_descriptor(const struct rv_descriptor *d, uint32_t module)
{
    if (d->size != sizeof *d || (module && (d->setting_count || d->entry_count)) ||
        (!d->hooks && d->hook_count) || (!d->settings && d->setting_count) ||
        (!d->screens && d->screen_count) || (!d->entries && d->entry_count))
        return 0;
    for (uint32_t i = 0; i < d->hook_count; i++)
        if (d->hooks[i].mode > RV_HOOK_MODE_REPLACE || d->hooks[i].reserved)
            return 0;
    for (uint32_t i = 0; i < d->setting_count; i++) {
        const struct rv_setting *setting = &d->settings[i];
        if (setting->key == RV_SETTING_KEY_ENABLED || setting->key == RV_SETTING_KEY_ALL ||
            setting->type < RV_SETTING_TYPE_BOOL || setting->type > RV_SETTING_TYPE_BLOB ||
            setting->style > RV_SETTING_STYLE_MULTI ||
            (setting->style == RV_SETTING_STYLE_MULTI && setting->label_count > 32))
            return 0;
        for (uint32_t j = 0; j < i; j++)
            if (d->settings[j].key == setting->key)
                return 0;
    }
    for (uint32_t i = 0; i < d->screen_count; i++) {
        if (!valid_screen(&d->screens[i]))
            return 0;
        for (uint32_t j = 0; j < i; j++)
            if (fnv1a(d->screens[j].id) == fnv1a(d->screens[i].id))
                return 0;
    }
    for (uint32_t i = 0; i < d->entry_count; i++) {
        const struct rv_menu_entry *e = &d->entries[i];
        const struct rv_setting *shown_by = NULL;
        for (uint32_t j = 0; e->shown_by && j < d->setting_count; j++)
            if (d->settings[j].key == e->shown_by)
                shown_by = &d->settings[j];
        if (e->menu > RV_MENU_VIDEO_PLAYLIST_CONTEXT || !valid_row(&e->row) ||
            (e->shown_by && (!shown_by || shown_by->type != RV_SETTING_TYPE_BOOL)))
            return 0;
    }
    return 1;
}

/* Binds `count` hooks into core-owned copies; on failure unbinds what it bound
 * and leaves the reason on the record. */
static int bind_hooks(struct rv_extension_record *record, struct core_module *module,
                      const struct rv_hook *hooks, uint32_t count)
{
    if (!count)
        return 1;
    struct core_hook *copies = allocate(record, count * sizeof *copies);
    if (!copies) {
        record->failure = CORE_EXTENSION_NO_MEMORY;
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        int site = site_index(record, hooks[i].site);
        if (site < 0) {
            record->failure = CORE_EXTENSION_BAD_SITE;
            record->conflict_site = hooks[i].site;
        } else {
            copies[i].site = (uint8_t)site;
            copies[i].mode = hooks[i].mode == RV_HOOK_MODE_REPLACE ? CORE_HOOK_REPLACE : CORE_HOOK_FILTER;
            copies[i].priority = hooks[i].priority;
            copies[i].handler = hooks[i].handler;
            int bound = core_hook_bind(&copies[i]);
            if (bound == CORE_HOOK_OK)
                continue;
            record->failure = bound == CORE_HOOK_CONFLICT ? CORE_EXTENSION_CONFLICT
                                                         : CORE_EXTENSION_NO_MEMORY;
            record->conflict_site = hooks[i].site;
        }
        while (i--)
            core_hook_unbind(&copies[i]);
        retire(copies);
        return 0;
    }
    module->hooks = copies;
    record->hooks_bound += count;
    return 1;
}

static int api_hook_bind(rv_extension_handle self, const struct rv_hook *hook)
{
    struct rv_extension_record *record = valid_handle(self);
    int site = record ? site_index(record, hook->site) : -1;
    if (site < 0)
        return record ? RV_BAD_SITE : RV_BAD_ARGUMENT;
    const struct core_hook *bound_copy = core_hook_find((uint32_t)site, hook->handler);
    if (bound_copy && header_of((void *)bound_copy)->owner == record)
        return RV_ALREADY_BOUND;
    struct core_hook *copy = allocate(record, sizeof *copy);
    if (!copy)
        return RV_NO_MEMORY;
    copy->site = (uint8_t)site;
    copy->mode = hook->mode == RV_HOOK_MODE_REPLACE ? CORE_HOOK_REPLACE : CORE_HOOK_FILTER;
    copy->priority = hook->priority;
    copy->handler = hook->handler;
    int bound = core_hook_bind(copy);
    if (bound != CORE_HOOK_OK) {
        release(record, copy);
        return result_of(bound);
    }
    record->hooks_bound++;
    return RV_OK;
}

static int api_hook_unbind(rv_extension_handle self, const struct rv_hook *hook)
{
    struct rv_extension_record *record = valid_handle(self);
    int site = record ? site_index(record, hook->site) : -1;
    if (site < 0)
        return RV_BAD_ARGUMENT;
    /* The core's copy is found through the dispatcher by handler identity. */
    const struct core_hook *copy = core_hook_find((uint32_t)site, hook->handler);
    if (!copy || header_of((void *)copy)->owner != record)
        return RV_NOT_BOUND;
    int result = core_hook_unbind(copy);
    if (result == CORE_HOOK_OK) {
        retire((void *)copy);
        record->hooks_bound--;
    }
    return result_of(result);
}

static uint32_t api_hook_next(const void *next, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                         uint32_t e, uint32_t f)
{
    return core_next_call6(next, a, b, c, d, e, f);
}

static uint32_t api_hook_next7(const void *next, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                               uint32_t e, uint32_t f, uint32_t g)
{
    return core_next_call7(next, a, b, c, d, e, f, g);
}

static int core_key(uint16_t key)
{
    return key == RV_SETTING_KEY_ENABLED || key == RV_SETTING_KEY_ALL;
}

static int32_t api_setting_int(rv_extension_handle self, uint16_t key)
{
    struct rv_extension_record *record = valid_handle(self);
    return record ? core_screens_setting_value(record, key) : 0;
}

static int api_setting_set_int(rv_extension_handle self, uint16_t key, int32_t value)
{
    struct rv_extension_record *record = valid_handle(self);
    if (!record || core_key(key))
        return RV_BAD_ARGUMENT;
    const struct rv_setting *setting = core_screens_setting(record, key);
    if (setting && setting->style == RV_SETTING_STYLE_MULTI)
        value = (int32_t)((uint32_t)value & (uint32_t)setting->maximum); /* a mask of 32 is -1 */
    else if (setting && value < setting->minimum)
        value = setting->minimum;
    else if (setting && value > setting->maximum)
        value = setting->maximum;
    if (core_settings_set_int(record->owner, key, value))
        return RV_NO_MEMORY;
    core_screens_setting_changed(record, key);
    return RV_OK;
}

static uint32_t api_setting_read(rv_extension_handle self, uint16_t key, void *out,
                                 uint32_t capacity)
{
    struct rv_extension_record *record = valid_handle(self);
    return record && !core_key(key) ? core_settings_get(record->owner, key, out, capacity) : 0;
}

static int api_setting_write(rv_extension_handle self, uint16_t key, uint8_t type,
                             const void *data, uint32_t bytes)
{
    struct rv_extension_record *record = valid_handle(self);
    if (!record || core_key(key) || bytes > 255)
        return RV_BAD_ARGUMENT;
    if (core_settings_set(record->owner, key, type, data, bytes))
        return RV_NO_MEMORY;
    core_screens_setting_changed(record, key);
    return RV_OK;
}

const struct rv_descriptor *core_loader_descriptor(const struct rv_extension_record *record)
{
    return record ? record->modules[0].descriptor : NULL;
}

void core_loader_setting_changed(struct rv_extension_record *record, uint16_t key)
{
    if (!record) {
        if (key == CORE_KEY_OPTIMIZE)
            core_loader_preload_refresh();
        return;
    }
    const struct rv_descriptor *d = core_loader_descriptor(record);
    if (d && d->setting_changed)
        d->setting_changed(key);
}

void core_loader_settings_reset(void)
{
    for (struct rv_extension_record *r = core_extensions; r; r = r->next)
        core_loader_setting_changed(r, RV_SETTING_KEY_ALL);
    core_loader_preload_refresh();
}

static void *api_alloc(rv_extension_handle self, uint32_t bytes)
{
    struct rv_extension_record *record = valid_handle(self);
    return record ? allocate(record, bytes) : NULL;
}

static void api_free(rv_extension_handle self, void *block)
{
    struct rv_extension_record *record = valid_handle(self);
    if (!record || !block || header_of(block)->owner != record)
        return;
    const uint8_t *end = (uint8_t *)block + header_of(block)->bytes;
    core_gfx_release_range(record, block, end);
    core_bitmaps_release_range(record, block, end);
    release(record, block);
}

static int load_module(struct rv_extension_record *record, uint32_t index);
static void unload_module(struct rv_extension_record *record, uint32_t index);

static int api_module_load(rv_extension_handle self, uint32_t module)
{
    struct rv_extension_record *record = valid_handle(self);
    if (!record || module == 0 || module >= record->module_count ||
        module_record(record, module)->trigger != TRIGGER_MANUAL)
        return RV_BAD_ARGUMENT;
    record->modules[module].unload_pending = 0;
    if (record->modules[module].loaded)
        return RV_OK;
    return load_module(record, module) ? RV_OK : RV_NOT_FOUND;
}

static int api_asset_set_provider(rv_extension_handle self,
                                  const struct rv_asset_provider *provider)
{
    struct rv_extension_record *record = valid_handle(self);
    return record ? core_bitmaps_provider(record, provider) : RV_BAD_ARGUMENT;
}

static const void *api_module_service(rv_extension_handle self, uint32_t module)
{
    struct rv_extension_record *record = valid_handle(self);
    if (!record || module >= record->module_count || !record->modules[module].loaded ||
        !record->modules[module].descriptor)
        return NULL;
    return record->modules[module].descriptor->service;
}

static void api_module_unload(rv_extension_handle self, uint32_t module)
{
    struct rv_extension_record *record = valid_handle(self);
    if (!record || !module || module >= record->module_count ||
        module_record(record, module)->trigger != TRIGGER_MANUAL)
        return;
    if (record->modules[module].screens_open)
        record->modules[module].unload_pending = 1; /* the grace tick unloads it */
    else
        unload_module(record, module);
}


static int api_ui_push(rv_extension_handle self, const char *screen, void *arg)
{
    struct rv_extension_record *record = valid_handle(self);
    if (!record || !screen)
        return RV_BAD_ARGUMENT;
    struct core_screen *s = core_loader_screen(record, screen);
    if (!s)
        return RV_NOT_FOUND;
    void *from = core_screens_caller ? core_screens_caller : core_screens_top_controller();
    return core_screens_push(s, from, arg) ? RV_OK : RV_NO_MEMORY;
}

static void api_ui_pop(rv_extension_handle self)
{
    struct rv_extension_record *record = valid_handle(self);
    if (record)
        core_screens_pop(record);
}

static void api_ui_refresh(rv_extension_handle self, const char *screen)
{
    struct rv_extension_record *record = valid_handle(self);
    struct core_screen *s = record ? core_screens_find(record, screen) : NULL;
    if (s)
        core_screens_refresh(s);
}

static void api_ui_select(rv_extension_handle self, const char *screen, uint32_t row)
{
    struct rv_extension_record *record = valid_handle(self);
    struct core_screen *s = record ? core_screens_find(record, screen) : NULL;
    if (s)
        core_screens_select(s, row);
}

static uint32_t api_string_resource(rv_extension_handle self, const char *text)
{
    struct rv_extension_record *record = valid_handle(self);
    if (!record || !text)
        return 0;
    uint32_t id = core_resource_take(core_resource_owner(record), 1);
    if (!id)
        return 0;
    uint32_t n = 0;
    while (text[n])
        n++;
    core_resource_set(Resource_String, id, text, n + 1);
    return id;
}

static void api_ui_invalidate(rv_extension_handle self)
{
    struct rv_extension_record *record = valid_handle(self);
    if (record)
        core_screens_invalidate(record, NULL);
}

static void *api_asset_load(rv_extension_handle self, const char *name, uint32_t *bytes)
{
    struct rv_extension_record *record = valid_handle(self);
    uint32_t size = 0;
    uint8_t kind;
    void *block = record && name ? core_loader_asset(record, fnv1a(name), &size, &kind) : NULL;
    if (block && bytes)
        *bytes = size;
    return block;
}

static int api_asset_read(rv_extension_handle self, const char *name, uint32_t offset, void *out,
                          uint32_t bytes, uint32_t *got)
{
    struct rv_extension_record *record = valid_handle(self);
    if (!record || !name || !out || !got)
        return RV_BAD_ARGUMENT;
    *got = 0;
    uint32_t hash = fnv1a(name);
    const struct rvx_entry *r;
    const uint8_t *memory = core_loader_preloaded(record, hash, &r);
    struct rvx_entry entry;
    void *handle = NULL;
    if (memory && (r->reserved & CORE_PRELOADED_PROVIDED))
        memory = NULL;
    if (memory)
        entry = *r;
    else if (!find_on_disk(record, hash, &handle, &entry))
        return RV_NOT_FOUND;
    int result = RV_OK;
    if (entry.codec != RVX_RAW || offset > entry.bytes) {
        result = RV_BAD_ARGUMENT;
    } else {
        uint32_t n = entry.bytes - offset < bytes ? entry.bytes - offset : bytes;
        if (memory)
            copy(out, memory + offset, n);
        else if (!core_file_seek(handle, entry.offset + offset) || !read_all(handle, out, n))
            result = RV_NOT_FOUND;
        if (result == RV_OK)
            *got = n;
    }
    if (handle)
        core_file_close(handle);
    return result;
}

static struct rv_debug_info debug = {
    sizeof debug,
    CORE_BUILD_ID,
    &core_extensions,
    offsetof(struct rv_extension_record, next),
    offsetof(struct rv_extension_record, owner),
    offsetof(struct rv_extension_record, file),
    offsetof(struct rv_extension_record, live),
    offsetof(struct rv_extension_record, peak),
    offsetof(struct rv_extension_record, failure),
    0,
    core_loader_set_enabled,
    core_site_argc,
    &core_trace_probe,
};

/* MainHeap is set while Apple boots, after this table is linked. */
static const struct rv_debug_info *api_debug_info(void)
{
    debug.heap = MainHeap;
    return &debug;
}

const char *core_loader_last_log;
void (*volatile core_trace_probe)(uint32_t kind, uint32_t argument);

static void api_log(rv_extension_handle self, const char *line)
{
    (void)self;
    core_loader_last_log = line;
}

static uint32_t api_format_number(char *out, uint32_t capacity, uint32_t value)
{
    return out && capacity ? (uint32_t)(core_format_number(out, out + capacity, value) - out) : 0;
}

static uint32_t api_format_size(char *out, uint32_t capacity, uint32_t bytes)
{
    return out && capacity ? (uint32_t)(core_format_size(out, out + capacity, bytes) - out) : 0;
}

static void api_ui_invalidate_rect(rv_extension_handle self, const struct rv_rect *rect)
{
    struct rv_extension_record *record = valid_handle(self);
    if (record && rect)
        core_screens_invalidate(record, rect);
}

static int api_file_open(rv_extension_handle self, const char *name, int mode,
                         struct rv_file **file)
{
    if (!file)
        return RV_BAD_ARGUMENT;
    *file = NULL;
    struct rv_extension_record *record = valid_handle(self);
    return record ? core_data_open(record, name, mode, file) : RV_BAD_ARGUMENT;
}

static int api_file_read(rv_extension_handle self, struct rv_file *file, void *out,
                         uint32_t bytes, uint32_t *got)
{
    if (!got)
        return RV_BAD_ARGUMENT;
    *got = 0;
    struct rv_extension_record *record = valid_handle(self);
    return record ? core_data_read(record, file, out, bytes, got) : RV_BAD_ARGUMENT;
}

static int api_file_write(rv_extension_handle self, struct rv_file *file, const void *data,
                          uint32_t bytes)
{
    struct rv_extension_record *record = valid_handle(self);
    return record ? core_data_write(record, file, data, bytes) : RV_BAD_ARGUMENT;
}

static int api_file_seek(rv_extension_handle self, struct rv_file *file, uint32_t offset)
{
    struct rv_extension_record *record = valid_handle(self);
    return record ? core_data_seek(record, file, offset) : RV_BAD_ARGUMENT;
}

static void api_file_close(rv_extension_handle self, struct rv_file *file)
{
    struct rv_extension_record *record = valid_handle(self);
    if (record)
        core_data_close(record, file);
}

static int api_file_delete(rv_extension_handle self, const char *name)
{
    struct rv_extension_record *record = valid_handle(self);
    return record ? core_data_delete(record, name) : RV_BAD_ARGUMENT;
}

static int api_folder_list(rv_extension_handle self, const char *name,
                           void (*each)(const char *entry, int is_folder, void *arg), void *arg)
{
    struct rv_extension_record *record = valid_handle(self);
    return record ? core_data_list(record, name, each, arg) : RV_BAD_ARGUMENT;
}

static int api_content_open(rv_extension_handle self, const char *name, struct rv_file **file)
{
    if (!file)
        return RV_BAD_ARGUMENT;
    *file = NULL;
    struct rv_extension_record *record = valid_handle(self);
    return record ? core_data_content_open(record, name, file) : RV_BAD_ARGUMENT;
}

static int api_firmware_open(rv_extension_handle self, struct rv_file **file)
{
    if (!file)
        return RV_BAD_ARGUMENT;
    *file = NULL;
    struct rv_extension_record *record = valid_handle(self);
    return record ? core_data_firmware_open(record, file) : RV_BAD_ARGUMENT;
}

static int api_content_list(rv_extension_handle self, const char *name,
                            void (*each)(const char *entry, int is_folder, void *arg), void *arg)
{
    struct rv_extension_record *record = valid_handle(self);
    return record ? core_data_content_list(record, name, each, arg) : RV_BAD_ARGUMENT;
}

static int api_asset_open(rv_extension_handle self, const char *name, struct rv_file **file)
{
    if (!file)
        return RV_BAD_ARGUMENT;
    *file = NULL;
    struct rv_extension_record *record = valid_handle(self);
    if (!record || !name)
        return RV_BAD_ARGUMENT;
    struct rvx_entry entry;
    void *handle;
    uint32_t hash = fnv1a(name);
    if (!find_on_disk(record, hash, &handle, &entry))
        return RV_NOT_FOUND;
    int result = entry.codec == RVX_RAW && entry.preload == RVX_PRELOAD_NEVER
                     ? core_data_adopt(record, handle, entry.offset, entry.bytes, file)
                     : RV_BAD_ARGUMENT;
    if (result != RV_OK)
        core_file_close(handle);
    return result;
}

static int api_ui_set_colors(rv_extension_handle self, const rv_color *roles)
{
    struct rv_extension_record *record = valid_handle(self);
    return record ? core_gfx_colors(record, roles) : RV_BAD_ARGUMENT;
}

#define VIEW_TREE_DEPTH 16 /* deeper than any stock screen; bounds the walk of a broken tree */

/* A view moved to its own compositor layer keeps its pixels until its own
 * window is dirtied, so a change to colours or artwork dirties every view in
 * the tree. Only container views (their find slot is View_FindViewById) carry
 * the child array: a vtable word, the count and the pointer to the children. */
static void invalidate_tree(uint8_t *view, uint32_t depth)
{
    View_InvalidateRect(view, (const int32_t *)(view + View_bounds));
    void **vtable = RV_FIELD(view, 0, void **);
    if (depth > VIEW_TREE_DEPTH ||
        vtable[View_find_view_method / 4] != (void *)ADDR_View_FindViewById)
        return;
    uint32_t count = RV_FIELD(view, View_child_count, uint32_t);
    uint8_t **children = RV_FIELD(view, View_child_items, uint8_t **);
    for (uint32_t i = 0; children && i < count; i++)
        if (children[i])
            invalidate_tree(children[i], depth + 1);
}

static void api_ui_redraw(rv_extension_handle self)
{
    void *root = valid_handle(self) ? UI_GetRootView() : NULL;
    if (root)
        invalidate_tree(root, 0);
}

static struct rv_timer *api_timer_start(rv_extension_handle self, uint32_t ms, int repeat,
                                        void (*callback)(void *arg), void *arg)
{
    struct rv_extension_record *record = valid_handle(self);
    return record ? core_timer_start(record, ms, repeat, callback, arg) : NULL;
}

static void api_timer_stop(rv_extension_handle self, struct rv_timer *timer)
{
    struct rv_extension_record *record = valid_handle(self);
    if (record && timer)
        core_timer_stop(record, timer);
}

static const struct rv_api api = {
    .size = sizeof api,
    .version = RV_API_VERSION,
    .hook_bind = api_hook_bind,
    .hook_unbind = api_hook_unbind,
    .hook_next = api_hook_next,
    .setting_int = api_setting_int,
    .setting_set_int = api_setting_set_int,
    .setting_read = api_setting_read,
    .setting_write = api_setting_write,
    .settings_flush = core_settings_flush,
    .alloc = api_alloc,
    .free = api_free,
    .module_load = api_module_load,
    .module_unload = api_module_unload,
    .log = api_log,
    .ui_push = api_ui_push,
    .ui_pop = api_ui_pop,
    .ui_refresh = api_ui_refresh,
    .ui_invalidate = api_ui_invalidate,
    .debug_info = api_debug_info,
    .asset_load = api_asset_load,
    .asset_read = api_asset_read,
    .format_number = api_format_number,
    .format_size = api_format_size,
    .timer_start = api_timer_start,
    .timer_stop = api_timer_stop,
    .ui_invalidate_rect = api_ui_invalidate_rect,
    .file_open = api_file_open,
    .file_read = api_file_read,
    .file_write = api_file_write,
    .file_seek = api_file_seek,
    .file_close = api_file_close,
    .file_delete = api_file_delete,
    .folder_list = api_folder_list,
    .irq_save = core_irq_save,
    .irq_restore = core_irq_restore,
    .ui_select = api_ui_select,
    .string_resource = api_string_resource,
    .content_open = api_content_open,
    .content_list = api_content_list,
    .asset_open = api_asset_open,
    .ui_set_colors = api_ui_set_colors,
    .ui_redraw = api_ui_redraw,
    .hook_next7 = api_hook_next7,
    .module_service = api_module_service,
    .asset_set_provider = api_asset_set_provider,
    .firmware_open = api_firmware_open,
};

/* Modules */

static const struct rv_descriptor *enter(struct rv_extension_record *record, void *image,
                                        uint32_t entry)
{
    rv_module_entry_fn fn = (rv_module_entry_fn)((uint32_t)image + entry);
    uint32_t outer = core_panic_extension;
    core_panic_extension = record->owner;
    const struct rv_descriptor *descriptor = fn(&api, record);
    core_panic_extension = outer;
    return descriptor;
}

/* Reads, relocates and enters one module; the main module brings the preloaded
 * assets. The relocation table is consumed in chunks through the stack, so a
 * module costs text + data + bss and no more. */
static int load_module_now(struct rv_extension_record *record, uint32_t index)
{
    struct core_module *module = &record->modules[index];
    if ((((const struct manifest_header *)record->manifest)->api & 0xffff) > RV_API_MINOR) {
        record->failure = CORE_EXTENSION_TOO_NEW;
        return 0;
    }
    struct rvx_header file;
    struct rvx_entry entry;
    struct rvx_module header;
    void *handle;
    record->failure = CORE_EXTENSION_BAD_MODULE;
    if (!open_extension(record->file, &handle, &file, NULL))
        return 0;
    if (index == 0) {
        record->entry_count = file.entry_count;
        record->body_hash = file.body_hash;
    }
    int ok = (file.api >> 16) == RV_API_MAJOR && file.firmware == CORE_FIRMWARE_ID &&
             file.manifest_bytes == record->manifest_bytes &&
             file.entry_count == record->entry_count && file.entry_count <= MAX_ENTRIES &&
             file.entry_count >= record->module_count &&
             read_entry(handle, record, index, &entry) &&
             core_file_seek(handle, entry.offset) && read_all(handle, &header, sizeof header) &&
             (header.text_bytes | header.data_bytes) % 4 == 0 &&
             header.entry < header.text_bytes && header.text_bytes <= entry.stored &&
             header.data_bytes <= entry.stored && header.reloc_count <= entry.stored / 4 &&
             header.bss_bytes <= MAX_PAYLOAD &&
             sizeof header + header.text_bytes + header.data_bytes + 4 * header.reloc_count ==
                 entry.stored;
    uint32_t code_bytes = header.text_bytes + header.data_bytes;
    uint8_t *image = NULL;
    if (ok) {
        image = allocate(record, code_bytes + header.bss_bytes);
        if (!image)
            record->failure = CORE_EXTENSION_NO_MEMORY;
        ok = image && read_all(handle, image, code_bytes);
    }
    for (uint32_t done = 0; ok && done < header.reloc_count; done += RELOC_CHUNK) {
        uint32_t offsets[RELOC_CHUNK];
        uint32_t count = header.reloc_count - done;
        if (count > RELOC_CHUNK)
            count = RELOC_CHUNK;
        ok = read_all(handle, offsets, count * 4);
        for (uint32_t i = 0; ok && i < count; i++) {
            if (offsets[i] % 4 || offsets[i] > code_bytes - 4)
                ok = 0;
            else
                *(uint32_t *)(image + offsets[i]) += (uint32_t)image;
        }
    }
    if (ok && index == 0)
        ok = load_preloaded(record, handle);
    core_file_close(handle);
    if (!ok) {
        if (image)
            release(record, image);
        return 0;
    }
    if (index == 0 && (!claim_raw_sites(record) || !claim_content(record))) {
        release(record, image);
        drop_preloaded(record);
        return 0;
    }
    core_cache_sync(image, image + code_bytes);
    module->image = image;
    module->bytes = code_bytes + header.bss_bytes;
    module->loaded = 1;
    module->idle = 0;
    const struct rv_descriptor *descriptor = enter(record, image, header.entry);
    if ((index == 0 && !descriptor) || (descriptor && !valid_descriptor(descriptor, index))) {
        record->failure = CORE_EXTENSION_BAD_DESCRIPTOR;
        unload_module(record, index);
        return 0;
    }
    if (!descriptor) {
        record->failure = CORE_EXTENSION_OK;
        return 1;
    }
    module->descriptor = descriptor;
    if (!bind_hooks(record, module, descriptor->hooks, descriptor->hook_count)) {
        unload_module(record, index);
        return 0;
    }
    int registered =
        core_screens_register(record, index, descriptor->screens, descriptor->screen_count);
    if (index == 0) {
        uint32_t synthesized =
            core_screens_from_settings(descriptor->settings, descriptor->setting_count, NULL);
        if (registered == RV_OK && synthesized) {
            record->setting_screens = allocate(record, synthesized * sizeof(struct rv_screen));
            if (!record->setting_screens) {
                registered = RV_NO_MEMORY;
            } else {
                core_screens_from_settings(descriptor->settings, descriptor->setting_count,
                                           record->setting_screens);
                registered =
                    core_screens_register(record, 0, record->setting_screens, synthesized);
            }
        }
        if (registered == RV_OK)
            registered = core_menus_register(record, descriptor->entries, descriptor->entry_count);
    }
    if (registered != RV_OK) {
        unload_module(record, index);
        record->failure = CORE_EXTENSION_NO_MEMORY;
        return 0;
    }
    record->failure = CORE_EXTENSION_OK;
    /* The entry ran before its declarations were known; now they apply. */
    if (index == 0)
        core_loader_setting_changed(record, RV_SETTING_KEY_ALL);
    return 1;
}

static int load_module(struct rv_extension_record *record, uint32_t index)
{
    core_trace(RV_TRACE_LOAD_BEGIN, record->owner);
    int loaded = load_module_now(record, index);
    core_trace(RV_TRACE_LOAD_END, record->owner);
    return loaded;
}

/* The module's unload callback, once; the extension's other modules are still
 * loaded when main's runs, so it may use their services. */
static void notify_unload(struct rv_extension_record *record, uint32_t index)
{
    struct core_module *module = &record->modules[index];
    const struct rv_descriptor *descriptor = module->descriptor;
    if (!module->loaded || module->unload_notified || !descriptor || !descriptor->unload)
        return;
    module->unload_notified = 1;
    uint32_t outer = core_panic_extension;
    core_panic_extension = record->owner;
    descriptor->unload();
    core_panic_extension = outer;
}

static void unload_module(struct rv_extension_record *record, uint32_t index)
{
    struct core_module *module = &record->modules[index];
    if (!module->loaded)
        return;
    core_trace(RV_TRACE_UNLOAD, record->owner);
    core_timer_stop_code(record, index ? module->image : NULL,
                         (uint8_t *)module->image + module->bytes);
    const struct rv_descriptor *descriptor = module->descriptor;
    notify_unload(record, index);
    module->unload_notified = 0;
    core_gfx_release_range(record, index ? module->image : NULL,
                           (uint8_t *)module->image + module->bytes);
    if (index)
        core_bitmaps_release_range(record, module->image, (uint8_t *)module->image + module->bytes);
    else
        core_data_close_all(record);
    if (index == 0 && descriptor) {
        int parked = core_menus_release(record);
        core_screens_release(record);
        if (record->setting_screens) {
            release(record, record->setting_screens);
            record->setting_screens = NULL;
        }
        core_resource_release(record->owner, parked);
    } else if (descriptor) {
        core_screens_retire_module(record, index);
    }
    if (module->hooks) {
        for (uint32_t i = 0; i < descriptor->hook_count; i++)
            core_hook_unbind(&module->hooks[i]);
        retire(module->hooks);
        module->hooks = NULL;
        record->hooks_bound -= descriptor->hook_count;
    }
    module->descriptor = NULL;
    if (index == 0) {
        release_raw_sites(record);
        core_bitmaps_release(record);
        drop_preloaded(record);
    }
    retire(module->image);
    module->image = NULL;
    module->loaded = 0;
    module->unload_pending = 0;
}

/* Extensions */

static struct rv_extension_record *find_file(const char *file)
{
    for (struct rv_extension_record *r = core_extensions; r; r = r->next)
        if (same_name(r->file, file))
            return r;
    return NULL;
}

/* Reads the manifest and, when the extension is enabled, builds its record and
 * loads the main module. Returns the record, or NULL for disabled and failed
 * manifests (nothing is kept for either). */
static struct rv_extension_record *load_extension(const char *file)
{
    uint32_t words[MANIFEST_MAX / 4];
    uint8_t *buffer = (uint8_t *)words;
    uint32_t bytes = read_manifest(file, buffer, NULL);
    if (!bytes || length(file) >= sizeof ((struct rv_extension_record *)0)->file)
        return NULL;
    const struct manifest_header *header = (const struct manifest_header *)buffer;
    uint32_t owner = fnv1a(strings(buffer) + header->text[CORE_MANIFEST_ID]);
    if (owner == core_loader_crashed ||
        !core_settings_get_int(owner, RV_SETTING_KEY_ENABLED, default_on(owner)))
        return NULL;
    uint32_t record_bytes = sizeof(struct rv_extension_record) +
                            header->module_count * sizeof(struct core_module) + bytes;
    struct rv_extension_record *record = allocate(NULL, record_bytes);
    if (!record)
        return NULL;
    header_of(record)->owner = record;
    core_loader_charge(NULL, -(int32_t)record_bytes);
    record->live = record->peak = record_bytes;
    record->owner = owner;
    record->module_count = (uint8_t)header->module_count;
    record->modules = (struct core_module *)(record + 1);
    record->manifest = (uint8_t *)(record->modules + header->module_count);
    record->manifest_bytes = bytes;
    for (uint32_t i = 0; i < bytes; i++)
        record->manifest[i] = buffer[i];
    for (uint32_t i = 0; file[i]; i++)
        record->file[i] = file[i];
    record->next = core_extensions;
    core_extensions = record;
    load_module(record, 0); /* a failure stays on the record for the screen */
    return record;
}

static void unload_extension(struct rv_extension_record *record)
{
    notify_unload(record, 0);
    for (uint32_t i = record->module_count; i--;)
        unload_module(record, i);
    struct rv_extension_record **link = &core_extensions;
    while (*link && *link != record)
        link = &(*link)->next;
    if (*link)
        *link = record->next;
    retire(record);
}

/* An extension file while a scan's list lives: the name without SUFFIX and
 * what pick read from its manifest. */
struct found {
    char file[sizeof ((struct rv_extension_record *)0)->file];
    uint8_t duplicate; /* another file with the same id loads instead */
    uint8_t failure;
    uint8_t named; /* the file is named after the id */
    uint32_t owner;
};

/* The length of `name` without SUFFIX when it names an extension file, else 0. */
static uint32_t stem_of(const char *name, uint8_t is_directory)
{
    uint32_t stem = name ? length(name) : 0;
    if (is_directory || stem <= sizeof SUFFIX - 1 || name[0] == '.')
        return 0;
    stem -= sizeof SUFFIX - 1;
    return stem < sizeof ((struct found *)0)->file && same_name(name + stem, SUFFIX) ? stem : 0;
}

struct listing {
    struct found *list;
    uint32_t capacity;
    uint32_t count;
};

static void list_file(const char *name, int is_folder, void *arg)
{
    struct listing *l = arg;
    uint32_t stem = stem_of(name, (uint8_t)is_folder);
    if (!stem)
        return;
    if (l->count < l->capacity) {
        copy(l->list[l->count].file, name, stem);
        l->list[l->count].file[stem] = 0;
    }
    l->count++;
}

/* Walks ROOT, copies the first `capacity` extension files into `list` and
 * returns how many it saw. */
static uint32_t list_files(struct found *list, uint32_t capacity)
{
    struct listing l = {list, capacity, 0};
    core_file_list(ROOT, list_file, &l);
    return l.count;
}

/* The wheel interrupt keeps the physical button mask current in WheelState;
 * reading it touches no hardware. The left button is Previous whatever the
 * orientation flag says, because the mask is the raw packet. */
static uint32_t held_buttons(void)
{
    return *(volatile uint32_t *)((uint8_t *)&WheelState + WheelState_buttons);
}

/* Sets `duplicate` on every file whose manifest id another file also carries
 * and that should not load: the file named after the id wins, else the first
 * found. A renamed copy beside a fresh install loads once. `buffer` holds
 * MANIFEST_MAX bytes. */
static void pick(struct found *list, uint32_t count, uint8_t *buffer)
{
    for (uint32_t i = 0; i < count; i++) {
        struct found *f = &list[i];
        read_manifest(f->file, buffer, &f->failure);
        if (f->failure != CORE_EXTENSION_OK)
            continue;
        const struct manifest_header *header = (const struct manifest_header *)buffer;
        const char *id = strings(buffer) + header->text[CORE_MANIFEST_ID];
        f->owner = fnv1a(id);
        f->named = (uint8_t)same_name(f->file, id);
    }
    for (uint32_t i = 0; i < count; i++)
        for (uint32_t j = 0; j < count && list[i].failure == CORE_EXTENSION_OK && !list[i].duplicate; j++)
            if (j != i && list[j].failure == CORE_EXTENSION_OK && list[j].owner == list[i].owner &&
                (list[j].named > list[i].named || (list[j].named == list[i].named && j < i)))
                list[i].duplicate = 1;
}

/* The extension files, all collected before any is opened, duplicates picked;
 * NULL when the heap refuses the list. A first pass counts them, so the list
 * is as long as the folder. Free it with release(NULL, list). */
static struct found *scan(uint32_t *count, uint8_t *buffer)
{
    uint32_t capacity = list_files(NULL, 0);
    struct found *list = allocate(NULL, capacity * sizeof *list);
    if (!list)
        return NULL;
    *count = list_files(list, capacity);
    if (*count > capacity)
        *count = capacity;
    pick(list, *count, buffer);
    return list;
}

/* Loads the listed files, the bundled extensions first so that a claim both a
 * bundled and another extension make, a content folder or a site, goes to the
 * bundled one whatever the file names; `only_new` skips files with a record. */
static void load_found(const struct found *list, uint32_t count, int only_new)
{
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t i = 0; i < count; i++)
            if (!list[i].duplicate && default_on(list[i].owner) == !pass &&
                !(only_new && find_file(list[i].file)))
                load_extension(list[i].file);
}

/* The body hash in the header of `file`, 0 when it cannot be opened. */
static uint32_t body_hash_of(const char *file)
{
    void *handle;
    struct rvx_header header;
    if (!open_extension(file, &handle, &header, NULL))
        return 0;
    core_file_close(handle);
    return header.body_hash;
}

/* The host may have added, removed or replaced files. Unloads every extension
 * whose file is gone, lost to a duplicate or changed, then loads the enabled
 * files that have no record. A changed file loads into a new id block. */
static void rescan(void)
{
    uint32_t words[MANIFEST_MAX / 4];
    uint32_t count = 0;
    struct found *list = scan(&count, (uint8_t *)words);
    if (!list)
        return;
    for (struct rv_extension_record *r = core_extensions, *next; r; r = next) {
        next = r->next;
        uint32_t i = 0;
        while (i < count && !same_name(list[i].file, r->file))
            i++;
        int changed = i < count && body_hash_of(r->file) != r->body_hash;
        if (i < count && !list[i].duplicate && !changed)
            continue;
        uint32_t owner = r->owner;
        unload_extension(r);
        if (changed)
            core_resource_retire(owner);
    }
    load_found(list, count, 1);
    release(NULL, list);
}

/* Disk mode ended: rescan before the main menu comes back. */
static uint32_t disk_mode_exit(const struct core_next *next)
{
    rescan();
    return core_next_call(next, 0, 0, 0, 0);
}

static const struct core_hook disk_mode_exit_hook = {
    CORE_SITE_DiskModeExit_PopToMainScreen_call, CORE_HOOK_FILTER, 0, disk_mode_exit};

void core_loader_boot(void)
{
    core_loader_crashed = (uint32_t)core_settings_get_int(CORE_SETTINGS_OWNER_CORE, CORE_KEY_CRASHED, 0);
    preload_limit = chosen_preload_limit();
    if (held_buttons() & WheelButton_Previous) {
        core_loader_core_only = 1;
        return;
    }
    uint32_t words[MANIFEST_MAX / 4];
    uint32_t count = 0;
    struct found *list = scan(&count, (uint8_t *)words);
    uint32_t crashed = core_recovery_owner();
    if (crashed && list) {
        uint32_t i = 0;
        for (; i < count; i++)
            if (list[i].failure == CORE_EXTENSION_OK && !list[i].duplicate && list[i].owner == crashed)
                break;
        if (i < count) {
            int persisted = core_loader_crashed == crashed &&
                !core_settings_get_int(crashed, RV_SETTING_KEY_ENABLED, default_on(crashed));
            core_settings_set_int(crashed, RV_SETTING_KEY_ENABLED, 0);
            core_settings_set_int(CORE_SETTINGS_OWNER_CORE, CORE_KEY_CRASHED, (int32_t)crashed);
            core_loader_crashed = crashed;
            core_settings_flush();
            if (persisted && core_recovery_log_status == 2)
                core_recovery_clear();
        }
    } else if (!crashed && core_recovery_log_status == 2) {
        core_recovery_clear();
    }

    load_found(list, count, 0);
    if (list)
        release(NULL, list);
    core_hook_bind(&disk_mode_exit_hook);
}

int core_loader_set_enabled(const char *file, int enabled)
{
    struct rv_extension_record *record = find_file(file);
    uint32_t owner;
    if (record) {
        owner = record->owner;
    } else {
        uint32_t words[MANIFEST_MAX / 4];
        uint8_t *buffer = (uint8_t *)words;
        if (!read_manifest(file, buffer, NULL))
            return RV_NOT_FOUND;
        const struct manifest_header *header = (const struct manifest_header *)buffer;
        owner = fnv1a(strings(buffer) + header->text[CORE_MANIFEST_ID]);
    }
    if (core_settings_set_int(owner, RV_SETTING_KEY_ENABLED, enabled != 0))
        return RV_NO_MEMORY;
    if (enabled && owner == core_loader_crashed) {
        if (core_settings_set_int(CORE_SETTINGS_OWNER_CORE, CORE_KEY_CRASHED, 0))
            return RV_NO_MEMORY;
        if (core_recovery_owner() == owner)
            core_recovery_clear();
        core_loader_crashed = 0;
    }
    if (enabled && !record)
        return load_extension(file) ? RV_OK : RV_NOT_FOUND;
    if (record)
        record->pending_unload = !enabled;
    return RV_OK;
}

void core_loader_tick(void)
{
    for (struct block *block = retired, *next; block; block = next) {
        next = block->next;
        Heap_Free(block);
    }
    retired = retiring;
    retiring = NULL;
    core_resource_tick();

    for (struct rv_extension_record *r = core_extensions, *next; r; r = next) {
        next = r->next;
        if (r->pending_unload) {
            unload_extension(r);
            continue;
        }
        for (uint32_t i = 1; i < r->module_count; i++) {
            struct core_module *module = &r->modules[i];
            if (!module->loaded || module->screens_open)
                continue;
            if (module->unload_pending)
                unload_module(r, i);
            else if (module_record(r, i)->trigger == TRIGGER_SCREEN && ++module->idle >= IDLE_TICKS)
                unload_module(r, i);
        }
    }
    if (preload_refresh_pending) {
        preload_refresh_pending = 0;
        preload_limit = chosen_preload_limit();
        for (struct rv_extension_record *r = core_extensions; r; r = r->next)
            if (r->modules[0].loaded && !r->pending_unload)
                reload_preloaded(r);
        core_panes_refresh();
    }
}

/* The module of `record` declaring `screen`, or 0. */
static uint32_t screen_module(const struct rv_extension_record *record, const char *screen)
{
    for (uint32_t i = 1; i < record->module_count; i++) {
        const struct manifest_module *m = module_record(record, i);
        const char *name = text(record, m->screens);
        for (uint32_t s = 0; s < m->screen_count; s++) {
            if (same_name(name, screen))
                return i;
            name += length(name) + 1;
        }
    }
    return 0;
}

struct core_screen *core_loader_screen(struct rv_extension_record *record, const char *id)
{
    struct core_screen *s = core_screens_find(record, id);
    uint32_t i = s || !record ? 0 : screen_module(record, id);
    if (i && !record->modules[i].loaded && load_module(record, i))
        s = core_screens_find(record, id);
    return s;
}

void core_loader_screen_pushed(struct rv_extension_record *record, uint8_t module)
{
    if (module && module < record->module_count) {
        record->modules[module].screens_open++;
        record->modules[module].idle = 0;
    }
}

void core_loader_screen_popped(struct rv_extension_record *record, uint8_t module)
{
    if (module && module < record->module_count && record->modules[module].screens_open)
        record->modules[module].screens_open--;
}

uint32_t core_loader_owner_of(uint32_t address)
{
    for (struct rv_extension_record *r = core_extensions; r; r = r->next)
        for (uint32_t i = 0; i < r->module_count; i++) {
            uint32_t image = (uint32_t)r->modules[i].image;
            if (image && address - image < r->modules[i].bytes)
                return r->owner;
        }
    return core_raw_owner_of(address);
}

struct rv_extension_record *core_loader_find(uint32_t owner)
{
    for (struct rv_extension_record *r = core_extensions; r; r = r->next)
        if (r->owner == owner)
            return r;
    return NULL;
}

uint32_t core_loader_installed(struct core_installed **out)
{
    uint32_t words[MANIFEST_MAX / 4];
    uint8_t *buffer = (uint8_t *)words;
    uint32_t found = 0, count = 0;
    struct found *list = scan(&found, buffer);
    *out = NULL;
    if (!list)
        return 0;
    count = found;
    struct core_installed *entries = count ? core_heap_allocate(NULL, count * sizeof *entries) : NULL;
    for (uint32_t i = 0; entries && i < found; i++) {
        struct core_installed *entry = &entries[i];
        copy(entry->file, list[i].file, sizeof entry->file);
        entry->duplicate = list[i].duplicate;
        if (!read_manifest(list[i].file, buffer, &entry->failure))
            continue;
        const struct manifest_header *header = (const struct manifest_header *)buffer;
        entry->owner = fnv1a(strings(buffer) + header->text[CORE_MANIFEST_ID]);
        const char *name = strings(buffer) + header->text[CORE_MANIFEST_NAME];
        for (uint32_t c = 0; name[c] && c < sizeof entry->name - 1; c++)
            entry->name[c] = name[c];
    }
    release(NULL, list);
    *out = entries;
    return entries ? count : 0;
}

uint32_t core_loader_manifest(const char *file, uint8_t *buffer, uint8_t *failure)
{
    return read_manifest(file, buffer, failure);
}

const char *core_loader_manifest_text(const uint8_t *manifest, enum core_manifest_text field)
{
    const struct manifest_header *header = (const struct manifest_header *)manifest;
    return field < CORE_MANIFEST_TEXTS ? strings(manifest) + header->text[field] : "";
}

const char *core_loader_manifest_content(const uint8_t *manifest, uint32_t index)
{
    const struct manifest_header *header = (const struct manifest_header *)manifest;
    if (index >= header->content_count)
        return NULL;
    const char *folder = strings(manifest) + header->content;
    while (index--)
        folder += length(folder) + 1;
    return folder;
}

uint32_t core_loader_manifest_flags(const uint8_t *manifest)
{
    return ((const struct manifest_header *)manifest)->flags;
}

uint32_t core_loader_total_bytes(void)
{
    uint32_t total = 0;
    for (struct rv_extension_record *r = core_extensions; r; r = r->next)
        total += r->live;
    return total;
}
