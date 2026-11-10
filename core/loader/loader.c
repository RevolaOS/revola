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

#include "loader/file.h"
#include "loader/loader.h"
#include "loader/lz.h"
#include "ui/menus.h"
#include "ui/resources.h"
#include "osos-2.0.5.h"
#include "panic.h"
#include "rv_api.h"
#include "loader/rvx.h"
#include "ui/screens.h"
#include "settings.h"
#include "hooks/sites.h"
#include "storage.h"
#include "ui/bitmaps.h"
#include "ui/format.h"
#include <stddef.h>

#define ROOT "/Revola/Extensions"
#define SUFFIX ".rvx"
#define MANIFEST_MAGIC 0x464d5652u /* RVMF */
#define MANIFEST_VERSION 2
#define MANIFEST_ENABLED 1
#define MANIFEST_MAX CORE_MANIFEST_MAX
#define MAX_MODULES 15
#define IDLE_TICKS 10 /* grace ticks a screen module stays loaded with no screen open */
#define RELOC_CHUNK 32
#define CATALOG_CHUNK 8
#define PATH_MAX_BYTES 96
#define MAX_ENTRIES 0xffff
#define MAX_PAYLOAD (16u << 20) /* keeps sums of payload sizes far from overflow */

/* Experiment, off: on flash, leave resident assets on disk like the others, to
 * measure whether reading them on request is fast enough there. */
#ifndef CORE_FLASH_ASSETS_ON_DISK
#define CORE_FLASH_ASSETS_ON_DISK 0
#endif

struct manifest_header {
    uint32_t magic;
    uint32_t version;
    uint32_t api;
    uint32_t flags;
    uint32_t module_count;
    uint32_t site_count;
    uint32_t strings_bytes;
    uint16_t text[6]; /* id, name, description, version, author, license */
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
    uint8_t loaded;
    uint8_t idle;         /* ticks without an open screen */
    uint8_t screens_open; /* declared screens on the navigation stack */
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
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z')
            x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z')
            y += 'a' - 'A';
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

static int manifest_valid(const uint8_t *buffer, uint32_t bytes)
{
    const struct manifest_header *header = (const struct manifest_header *)buffer;
    if (bytes < sizeof *header || header->magic != MANIFEST_MAGIC ||
        header->version != MANIFEST_VERSION || header->module_count < 1 ||
        header->module_count > MAX_MODULES || header->site_count > CORE_RAW_SITES ||
        (header->api >> 16) != RV_API_MAJOR)
        return 0;
    uint32_t table = tables_bytes(header);
    if (bytes != table + header->strings_bytes || !header->strings_bytes ||
        buffer[bytes - 1] != 0)
        return 0;
    for (uint32_t i = 0; i < 6; i++)
        if (header->text[i] >= header->strings_bytes)
            return 0;
    const struct manifest_site *sites = (const struct manifest_site *)(buffer + sizeof *header);
    for (uint32_t i = 0; i < header->site_count; i++)
        if (sites[i].name >= header->strings_bytes)
            return 0;
    const struct manifest_module *modules = (const struct manifest_module *)(sites + header->site_count);
    if (modules[0].trigger != TRIGGER_ALWAYS)
        return 0;
    for (uint32_t i = 0; i < header->module_count; i++)
        if (modules[i].name >= header->strings_bytes ||
            modules[i].screens >= header->strings_bytes || modules[i].trigger > TRIGGER_MANUAL)
            return 0;
    return buffer[table + header->text[0]] != 0; /* id present */
}

/* Opens ROOT "/" file SUFFIX and reads its header; 0 when either fails. */
static int open_extension(const char *file, void **handle, struct rvx_header *header)
{
    char path[PATH_MAX_BYTES];
    uint32_t n = 0, got = 0;
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
        header->magic == RVX_MAGIC && header->format == RVX_FORMAT)
        return 1;
    core_file_close(*handle);
    return 0;
}

/* The manifest of `file` into `buffer`; its size, or 0 when it is unreadable. */
static uint32_t read_manifest(const char *file, uint8_t *buffer)
{
    void *handle;
    struct rvx_header header;
    uint32_t got = 0;
    if (!open_extension(file, &handle, &header))
        return 0;
    int ok = header.manifest_bytes <= MANIFEST_MAX &&
             core_file_read(handle, buffer, header.manifest_bytes, &got) &&
             got == header.manifest_bytes;
    core_file_close(handle);
    return ok && manifest_valid(buffer, got) ? got : 0;
}

/* Catalog */

static uint32_t catalog_offset(const struct rv_extension_record *record)
{
    return sizeof(struct rvx_header) + ((record->manifest_bytes + 3) & ~3u);
}

/* Checks an entry before anything trusts it; offsets past the end of the file
 * show up as short reads. */
static int entry_valid(const struct rv_extension_record *record, const struct rvx_entry *e,
                       uint32_t index)
{
    uint32_t payloads = catalog_offset(record) + record->entry_count * sizeof *e;
    int kind = index < record->module_count ? e->kind == RVX_MODULE && e->codec == RVX_RAW
                                            : e->kind == RVX_DATA || e->kind == RVX_BITMAP;
    return kind && e->codec <= RVX_LZ && (e->codec == RVX_LZ || e->stored == e->bytes) &&
           e->offset % 4 == 0 && e->offset >= payloads && e->stored <= MAX_PAYLOAD &&
           e->bytes <= MAX_PAYLOAD;
}

/* Streams catalog entries [index, end) through the stack. */
struct catalog {
    void *handle;
    const struct rv_extension_record *record;
    uint32_t index, end, have, at;
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
    if (!entry_valid(c->record, e, c->index))
        return NULL;
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

static void drop_resident(struct rv_extension_record *record)
{
    if (record->resident)
        retire(record->resident);
    record->resident = NULL;
    record->resident_count = 0;
}

/* Reads the resident entries, then their payloads in file order, into one block:
 * the entries with offsets rebased onto it, then the payloads word-aligned. */
static int load_resident(struct rv_extension_record *record, void *handle, uint32_t count)
{
    if (CORE_FLASH_ASSETS_ON_DISK && core_storage_is_flash(ATA_Identify))
        count = 0;
    if (!count)
        return 1;
    uint32_t first = record->module_count, bytes = count * sizeof(struct rvx_entry);
    struct catalog c;
    const struct rvx_entry *e;
    if (!catalog_start(&c, record, handle, first, first + count))
        return 0;
    while ((e = catalog_next(&c)))
        bytes += (e->stored + 3) & ~3u;
    if (c.index != c.end)
        return 0;
    struct rvx_entry *block = allocate(record, bytes);
    if (!block) {
        record->failure = CORE_EXTENSION_NO_MEMORY;
        return 0;
    }
    uint32_t at = count * sizeof *block;
    int ok = core_file_seek(handle, catalog_offset(record) + first * sizeof *block) &&
             read_all(handle, block, count * sizeof *block);
    for (uint32_t i = 0; ok && i < count; i++) {
        struct rvx_entry *r = &block[i];
        ok = entry_valid(record, r, first + i) && r->stored <= bytes - at &&
             core_file_seek(handle, r->offset) && read_all(handle, (uint8_t *)block + at, r->stored);
        r->offset = at;
        at += (r->stored + 3) & ~3u;
    }
    if (!ok) {
        release(record, block);
        return 0;
    }
    record->resident = block;
    record->resident_count = count;
    return 1;
}

static const struct rvx_entry *resident_entry(const struct rv_extension_record *record,
                                              uint32_t name)
{
    for (uint32_t i = 0; i < record->resident_count; i++)
        if (record->resident[i].name == name)
            return &record->resident[i];
    return NULL;
}

/* Opens the file and finds asset `name` among the entries not held in RAM; the
 * handle stays open on success. */
static int find_on_disk(const struct rv_extension_record *record, uint32_t name, void **handle,
                        struct rvx_entry *out)
{
    struct rvx_header file;
    struct catalog c;
    const struct rvx_entry *e;
    if (!open_extension(record->file, handle, &file))
        return 0;
    if (file.manifest_bytes == record->manifest_bytes && file.entry_count == record->entry_count &&
        catalog_start(&c, record, *handle, record->module_count + record->resident_count,
                      record->entry_count))
        while ((e = catalog_next(&c)))
            if (e->name == name) {
                *out = *e;
                return 1;
            }
    core_file_close(*handle);
    return 0;
}

const void *core_loader_resident(const struct rv_extension_record *record, uint32_t name,
                                 const struct rvx_entry **entry)
{
    const struct rvx_entry *e = resident_entry(record, name);
    if (!e)
        return NULL;
    *entry = e;
    return (const uint8_t *)record->resident + e->offset;
}

void *core_loader_asset(struct rv_extension_record *record, uint32_t name, uint32_t *bytes,
                        uint8_t *kind)
{
    const struct rvx_entry *r;
    const uint8_t *memory = core_loader_resident(record, name, &r);
    struct rvx_entry entry;
    struct core_lz_input in;
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

void *core_loader_block(uint32_t bytes)
{
    return allocate(NULL, bytes);
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

static struct rv_extension_record *valid_handle(rv_handle self)
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
    case RV_ROW_PUSH:
        return row->label && row->screen;
    case RV_ROW_TOGGLE:
    case RV_ROW_SETTING:
        return row->label && row->key != RV_KEY_ENABLED && row->key != RV_KEY_MENU_VISIBILITY;
    case RV_ROW_ACTION:
        return row->label && (row->action || row->text);
    default:
        return 0;
    }
}

static int valid_screen(const struct rv_screen *s)
{
    if (!s->name || !s->title || s->layout > RV_LAYOUT_SPLIT || s->opening > RV_OPENING_FROM_PANE)
        return 0;
    switch (s->kind) {
    case RV_SCREEN_LIST:
        for (uint32_t i = 0; i < s->list.count; i++)
            if (!valid_row(&s->list.rows[i]))
                return 0;
        return s->list.rows || !s->list.count;
    case RV_SCREEN_CHOICE:
    case RV_SCREEN_SLIDER:
    case RV_SCREEN_MULTI_CHOICE:
        return s->choice.labels && s->choice.count &&
               (s->kind != RV_SCREEN_MULTI_CHOICE || s->choice.count <= 32);
    case RV_SCREEN_TEXT:
        return s->text.paragraphs || !s->text.count;
    case RV_SCREEN_CANVAS:
        return s->canvas.draw != NULL;
    case RV_SCREEN_RUNTIME_LIST:
        return s->runtime.count && s->runtime.row && s->runtime.detail <= RV_DETAIL_BELOW_SUBMENU;
    default:
        return 0;
    }
}

static int valid_descriptor(const struct rv_extension *d)
{
    for (uint32_t i = 0; i < d->hook_count; i++)
        if (d->hooks[i].mode > RV_HOOK_REPLACE || d->hooks[i].reserved)
            return 0;
    for (uint32_t i = 0; i < d->setting_count; i++) {
        const struct rv_setting *setting = &d->settings[i];
        if (setting->key == RV_KEY_ENABLED || setting->key == RV_KEY_MENU_VISIBILITY ||
            setting->type < RV_SETTING_BOOL || setting->type > RV_SETTING_BLOB ||
            setting->style > RV_STYLE_MULTI ||
            (setting->style == RV_STYLE_MULTI && setting->label_count > 32))
            return 0;
        for (uint32_t j = 0; j < i; j++)
            if (d->settings[j].key == setting->key)
                return 0;
    }
    for (uint32_t i = 0; i < d->screen_count; i++) {
        if (!valid_screen(&d->screens[i]))
            return 0;
        for (uint32_t j = 0; j < i; j++)
            if (fnv1a(d->screens[j].name) == fnv1a(d->screens[i].name))
                return 0;
    }
    for (uint32_t i = 0; i < d->entry_count; i++)
        if (!d->entries[i].in || !valid_row(&d->entries[i].row))
            return 0;
    return 1;
}

/* Binds `count` hooks into core-owned copies; on failure unbinds what it bound
 * and leaves the reason on the record. */
static int bind_hooks(struct rv_extension_record *record, const struct rv_hook *hooks,
                      uint32_t count)
{
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
            copies[i].mode = hooks[i].mode == RV_HOOK_REPLACE ? CORE_HOOK_REPLACE : CORE_HOOK_FILTER;
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
    record->hooks = copies;
    record->hooks_bound = count;
    return 1;
}

static int api_hook_bind(rv_handle self, const struct rv_hook *hook)
{
    struct rv_extension_record *record = valid_handle(self);
    int site = record ? site_index(record, hook->site) : -1;
    if (site < 0)
        return record ? RV_BAD_SITE : RV_BAD_ARGUMENT;
    struct core_hook *copy = allocate(record, sizeof *copy);
    if (!copy)
        return RV_NO_MEMORY;
    copy->site = (uint8_t)site;
    copy->mode = hook->mode == RV_HOOK_REPLACE ? CORE_HOOK_REPLACE : CORE_HOOK_FILTER;
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

static int api_hook_unbind(rv_handle self, const struct rv_hook *hook)
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

static uint32_t api_next(const void *next, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                         uint32_t e, uint32_t f)
{
    return core_next_call6(next, a, b, c, d, e, f);
}

static uint32_t api_setting_get(rv_handle self, uint16_t key, void *out, uint32_t capacity)
{
    struct rv_extension_record *record = valid_handle(self);
    return record ? core_settings_get(record->owner, key, out, capacity) : 0;
}

static int api_setting_set(rv_handle self, uint16_t key, uint8_t type, const void *data,
                           uint32_t bytes)
{
    struct rv_extension_record *record = valid_handle(self);
    if (!record || key == RV_KEY_ENABLED)
        return RV_BAD_ARGUMENT;
    return core_settings_set(record->owner, key, type, data, bytes) ? RV_NO_MEMORY : RV_OK;
}

static void *api_alloc(rv_handle self, uint32_t bytes)
{
    struct rv_extension_record *record = valid_handle(self);
    return record ? allocate(record, bytes) : NULL;
}

static void api_free(rv_handle self, void *block)
{
    struct rv_extension_record *record = valid_handle(self);
    if (record && block)
        release(record, block);
}

static int load_module(struct rv_extension_record *record, uint32_t index);
static void unload_module(struct rv_extension_record *record, uint32_t index);

static int api_module_load(rv_handle self, uint32_t module)
{
    struct rv_extension_record *record = valid_handle(self);
    if (!record || module == 0 || module >= record->module_count ||
        module_record(record, module)->trigger != TRIGGER_MANUAL)
        return RV_BAD_ARGUMENT;
    if (record->modules[module].loaded)
        return RV_OK;
    return load_module(record, module) ? RV_OK : RV_NOT_FOUND;
}

static void api_module_unload(rv_handle self, uint32_t module)
{
    struct rv_extension_record *record = valid_handle(self);
    if (record && module && module < record->module_count &&
        module_record(record, module)->trigger == TRIGGER_MANUAL)
        unload_module(record, module);
}


static int api_ui_push(rv_handle self, const char *screen)
{
    struct rv_extension_record *record = valid_handle(self);
    struct core_screen *s = record ? core_screens_find(record, screen) : NULL;
    if (!record || !core_screens_caller)
        return RV_BAD_ARGUMENT;
    if (!s)
        return RV_NOT_FOUND;
    return core_screens_push(s, core_screens_caller) ? RV_OK : RV_NO_MEMORY;
}

static void api_ui_pop(rv_handle self)
{
    if (valid_handle(self))
        core_screens_pop();
}

static void api_ui_refresh(rv_handle self, const char *screen)
{
    struct rv_extension_record *record = valid_handle(self);
    struct core_screen *s = record ? core_screens_find(record, screen) : NULL;
    if (s)
        core_screens_refresh(s);
}

static void api_ui_invalidate(rv_handle self)
{
    if (valid_handle(self))
        core_screens_invalidate();
}

static void *api_asset_load(rv_handle self, const char *name, uint32_t *bytes)
{
    struct rv_extension_record *record = valid_handle(self);
    uint32_t size = 0;
    uint8_t kind;
    void *block = record && name ? core_loader_asset(record, fnv1a(name), &size, &kind) : NULL;
    if (block && bytes)
        *bytes = size;
    return block;
}

static int api_asset_read(rv_handle self, const char *name, uint32_t offset, void *out,
                          uint32_t bytes, uint32_t *got)
{
    struct rv_extension_record *record = valid_handle(self);
    if (!record || !name || !out || !got)
        return RV_BAD_ARGUMENT;
    *got = 0;
    uint32_t hash = fnv1a(name);
    const struct rvx_entry *r;
    const uint8_t *memory = core_loader_resident(record, hash, &r);
    struct rvx_entry entry;
    void *handle = NULL;
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
    CORE_BUILD_ID,
    &core_extensions,
    offsetof(struct rv_extension_record, next),
    offsetof(struct rv_extension_record, owner),
    offsetof(struct rv_extension_record, file),
    offsetof(struct rv_extension_record, live),
    offsetof(struct rv_extension_record, peak),
    offsetof(struct rv_extension_record, failure),
    0,
};

/* MainHeap is set while Apple boots, after this table is linked. */
static const struct rv_debug_info *api_debug_info(void)
{
    debug.heap = MainHeap;
    return &debug;
}

const char *core_loader_last_log;

static void api_log(rv_handle self, const char *line)
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

static const struct rv_api api = {
    sizeof api,           RV_API_VERSION,     api_hook_bind,      api_hook_unbind,
    api_next,             api_setting_get,    api_setting_set,    core_settings_flush,
    api_alloc,            api_free,           api_module_load,    api_module_unload,
    api_log,              api_ui_push,        api_ui_pop,         api_ui_refresh,
    api_ui_invalidate,    api_debug_info,     api_asset_load,     api_asset_read,
    api_format_number,    api_format_size,
};

/* Modules */

static const struct rv_extension *enter(struct rv_extension_record *record, void *image,
                                        uint32_t entry)
{
    rv_entry_fn fn = (rv_entry_fn)((uint32_t)image + entry);
    core_panic_extension = record->owner;
    const struct rv_extension *descriptor = fn(&api, record);
    core_panic_extension = 0;
    return descriptor;
}

/* Reads, relocates and enters one module; the main module brings the resident
 * assets. The relocation table is consumed in chunks through the stack, so a
 * module costs text + data + bss and no more. */
static int load_module(struct rv_extension_record *record, uint32_t index)
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
    if (!open_extension(record->file, &handle, &file))
        return 0;
    if (index == 0) {
        record->entry_count = file.entry_count;
        record->body_hash = file.body_hash;
    }
    int ok = (file.api >> 16) == RV_API_MAJOR && file.firmware == CORE_FIRMWARE_ID &&
             file.manifest_bytes == record->manifest_bytes &&
             file.entry_count == record->entry_count && file.entry_count <= MAX_ENTRIES &&
             file.entry_count >= record->module_count &&
             file.resident_count <= file.entry_count - record->module_count &&
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
        ok = load_resident(record, handle, file.resident_count);
    core_file_close(handle);
    if (!ok) {
        if (image)
            release(record, image);
        return 0;
    }
    if (index == 0 && !claim_raw_sites(record)) {
        release(record, image);
        drop_resident(record);
        return 0;
    }
    core_cache_sync(image, image + code_bytes);
    module->image = image;
    module->bytes = code_bytes + header.bss_bytes;
    module->loaded = 1;
    module->idle = 0;
    const struct rv_extension *descriptor = enter(record, image, header.entry);
    if (index == 0) {
        if (!descriptor || descriptor->size != sizeof *descriptor ||
            !valid_descriptor(descriptor)) {
            record->failure = CORE_EXTENSION_BAD_DESCRIPTOR;
            unload_module(record, 0);
            return 0;
        }
        record->descriptor = descriptor;
        if (!bind_hooks(record, descriptor->hooks, descriptor->hook_count)) {
            record->descriptor = NULL;
            unload_module(record, 0);
            return 0;
        }
        int registered = core_screens_register(record, descriptor->screens, descriptor->screen_count);
        uint32_t synthesized =
            core_screens_from_settings(descriptor->settings, descriptor->setting_count, NULL);
        if (registered == RV_OK && synthesized) {
            record->setting_screens = allocate(record, synthesized * sizeof(struct rv_screen));
            if (!record->setting_screens) {
                registered = RV_NO_MEMORY;
            } else {
                core_screens_from_settings(descriptor->settings, descriptor->setting_count,
                                           record->setting_screens);
                registered = core_screens_register(record, record->setting_screens, synthesized);
            }
        }
        if (registered == RV_OK)
            registered = core_menus_register(record, descriptor->entries, descriptor->entry_count);
        if (registered != RV_OK) {
            unload_module(record, 0);
            record->failure = CORE_EXTENSION_NO_MEMORY;
            return 0;
        }
    }
    record->failure = CORE_EXTENSION_OK;
    return 1;
}

static void unload_module(struct rv_extension_record *record, uint32_t index)
{
    struct core_module *module = &record->modules[index];
    if (!module->loaded)
        return;
    if (index == 0 && record->descriptor) {
        const struct rv_extension *descriptor = record->descriptor;
        if (descriptor->unload) {
            core_panic_extension = record->owner;
            descriptor->unload();
            core_panic_extension = 0;
        }
        int parked = core_menus_release(record);
        core_screens_release(record);
        if (record->setting_screens) {
            release(record, record->setting_screens);
            record->setting_screens = NULL;
        }
        core_resource_release(record->owner, parked);
        for (uint32_t i = 0; i < descriptor->hook_count; i++)
            core_hook_unbind(&record->hooks[i]);
        retire(record->hooks);
        record->hooks = NULL;
        record->hooks_bound -= descriptor->hook_count;
        record->descriptor = NULL;
    }
    if (index == 0) {
        release_raw_sites(record);
        core_bitmaps_release(record);
        drop_resident(record);
    }
    retire(module->image);
    module->image = NULL;
    module->loaded = 0;
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
    uint32_t bytes = read_manifest(file, buffer);
    if (!bytes || length(file) >= sizeof ((struct rv_extension_record *)0)->file)
        return NULL;
    const struct manifest_header *header = (const struct manifest_header *)buffer;
    uint32_t owner = fnv1a(strings(buffer) + header->text[0]);
    if (!core_settings_get_int(owner, RV_KEY_ENABLED, header->flags & MANIFEST_ENABLED))
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
    uint8_t readable;
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

/* Walks ROOT, copies the first `capacity` extension files into `list` and
 * returns how many it saw. */
static uint32_t list_files(struct found *list, uint32_t capacity)
{
    uint32_t iterator_words[DirectoryIterator_bytes / 4];
    uint32_t path_words[NativePath_bytes / 4];
    struct DirectoryIterator *iterator = (struct DirectoryIterator *)iterator_words;
    struct NativePath *path = (struct NativePath *)path_words;
    uint8_t is_directory = 0;
    uint32_t count = 0;
    NativePath_InitEmpty(path);
    DirectoryIterator_Init(iterator, ROOT, 0, 0);
    while (DirectoryIterator_Next(iterator, path, &is_directory)) {
        const char *name = NativeString_GetUtf8((struct NativeString *)path);
        uint32_t stem = stem_of(name, is_directory);
        if (!stem)
            continue;
        if (count < capacity) {
            copy(list[count].file, name, stem);
            list[count].file[stem] = 0;
        }
        count++;
    }
    DirectoryIterator_Destroy(iterator);
    NativePath_Destroy(path);
    return count;
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
        f->readable = read_manifest(f->file, buffer) != 0;
        if (!f->readable)
            continue;
        const struct manifest_header *header = (const struct manifest_header *)buffer;
        const char *id = strings(buffer) + header->text[0];
        f->owner = fnv1a(id);
        f->named = (uint8_t)same_name(f->file, id);
    }
    for (uint32_t i = 0; i < count; i++)
        for (uint32_t j = 0; j < count && list[i].readable && !list[i].duplicate; j++)
            if (j != i && list[j].readable && list[j].owner == list[i].owner &&
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

/* The body hash in the header of `file`, 0 when it cannot be opened. */
static uint32_t body_hash_of(const char *file)
{
    void *handle;
    struct rvx_header header;
    if (!open_extension(file, &handle, &header))
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
    for (uint32_t i = 0; i < count; i++)
        if (!list[i].duplicate && !find_file(list[i].file))
            load_extension(list[i].file);
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
    uint32_t crashed = CORE_CRASH_SCRATCH;
    if (crashed) {
        CORE_CRASH_SCRATCH = 0;
        core_settings_set_int(crashed, RV_KEY_ENABLED, 0);
        core_settings_set_int(CORE_SETTINGS_OWNER_CORE, CORE_KEY_CRASHED, (int32_t)crashed);
    }
    core_loader_crashed = (uint32_t)core_settings_get_int(CORE_SETTINGS_OWNER_CORE, CORE_KEY_CRASHED, 0);
    if (held_buttons() & WheelButton_Previous) {
        core_loader_core_only = 1;
        return;
    }
    uint32_t words[MANIFEST_MAX / 4];
    uint32_t count = 0;
    struct found *list = scan(&count, (uint8_t *)words);
    for (uint32_t i = 0; i < count; i++)
        if (!list[i].duplicate)
            load_extension(list[i].file);
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
        if (!read_manifest(file, buffer))
            return RV_NOT_FOUND;
        const struct manifest_header *header = (const struct manifest_header *)buffer;
        owner = fnv1a(strings(buffer) + header->text[0]);
    }
    core_settings_set_int(owner, RV_KEY_ENABLED, enabled != 0);
    if (enabled && owner == core_loader_crashed) {
        core_loader_crashed = 0;
        core_settings_set_int(CORE_SETTINGS_OWNER_CORE, CORE_KEY_CRASHED, 0);
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
            if (!module->loaded || module->screens_open ||
                module_record(r, i)->trigger != TRIGGER_SCREEN)
                continue;
            if (++module->idle >= IDLE_TICKS)
                unload_module(r, i);
        }
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

void core_loader_screen_pushed(const char *screen)
{
    for (struct rv_extension_record *r = core_extensions; r; r = r->next) {
        uint32_t i = screen_module(r, screen);
        if (!i)
            continue;
        r->modules[i].screens_open++;
        r->modules[i].idle = 0;
        if (!r->modules[i].loaded)
            load_module(r, i);
    }
}

void core_loader_screen_popped(const char *screen)
{
    for (struct rv_extension_record *r = core_extensions; r; r = r->next) {
        uint32_t i = screen_module(r, screen);
        if (i && r->modules[i].screens_open)
            r->modules[i].screens_open--;
    }
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
    for (uint32_t i = 0; i < found; i++)
        count += list[i].readable;
    struct core_installed *entries = count ? core_heap_allocate(NULL, count * sizeof *entries) : NULL;
    for (uint32_t i = 0, n = 0; entries && i < found; i++) {
        if (!list[i].readable)
            continue;
        struct core_installed *entry = &entries[n++];
        copy(entry->file, list[i].file, sizeof entry->file);
        entry->owner = list[i].owner;
        entry->duplicate = list[i].duplicate;
        if (!read_manifest(list[i].file, buffer))
            continue; /* removed since the scan: the row stays, without a name */
        const struct manifest_header *header = (const struct manifest_header *)buffer;
        const char *name = strings(buffer) + header->text[1];
        for (uint32_t c = 0; name[c] && c < sizeof entry->name - 1; c++)
            entry->name[c] = name[c];
    }
    release(NULL, list);
    *out = entries;
    return entries ? count : 0;
}

uint32_t core_loader_manifest(const char *file, uint8_t *buffer)
{
    return read_manifest(file, buffer);
}

const char *core_loader_manifest_text(const uint8_t *manifest, uint32_t field)
{
    const struct manifest_header *header = (const struct manifest_header *)manifest;
    return field < 6 ? strings(manifest) + header->text[field] : "";
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
