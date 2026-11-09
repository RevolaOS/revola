/* SPDX-License-Identifier: GPL-3.0-only */

/* Extension loader. Each directory under /Revola/Extensions holds a manifest.bin
 * and one .rvx per module. At boot the enabled ones are read into the Apple heap,
 * relocated in place and entered through the rv_api table; their descriptor's
 * hooks are bound in order and a collision unloads the extension with the site
 * recorded for Settings > Extensions. A disabled extension is never read past
 * its manifest and keeps no record: zero bytes.
 *
 * Every block the loader hands out carries an 8-byte header naming its owner, so
 * the record's live and peak counters are exact. Images and hook arrays are not
 * freed on unload but retired: the dispatcher publishes the new handler list at
 * once, and the block is freed two grace ticks later, when nothing can still be
 * inside it. */

#include "loader/file.h"
#include "loader/loader.h"
#include "ui/menus.h"
#include "osos-2.0.5.h"
#include "panic.h"
#include "rv_api.h"
#include "loader/rvx.h"
#include "ui/screens.h"
#include "settings.h"
#include "hooks/sites.h"
#include <stddef.h>

#define ROOT "/Revola/Extensions"
#define MANIFEST_FILE "manifest.bin"
#define MANIFEST_MAGIC 0x464d5652u /* RVMF */
#define MANIFEST_VERSION 1
#define MANIFEST_ENABLED 1
#define MANIFEST_MAX 1024
#define MAX_MODULES 15
#define MAX_DIRECTORIES 16
#define IDLE_TICKS 10 /* grace ticks a screen module stays loaded with no screen open */
#define RELOC_CHUNK 32
#define PATH_MAX_BYTES 96

struct manifest_header {
    uint32_t magic;
    uint32_t version;
    uint32_t api;
    uint32_t flags;
    uint32_t module_count;
    uint32_t strings_bytes;
    uint16_t text[6]; /* id, name, description, version, author, license */
};

struct manifest_module {
    uint16_t name;
    uint16_t file;
    uint8_t trigger; /* 0 always, 1 screen, 2 manual */
    uint8_t screen_count;
    uint16_t screens; /* first of screen_count consecutive strings */
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

/* Owned allocation: an 8-byte header before the block, counted on the owner. */
static void *allocate(struct rv_extension_record *owner, uint32_t bytes)
{
    struct block *block = Heap_AllocateZeroed(sizeof *block + bytes);
    if (!block)
        return NULL;
    block->owner = owner;
    block->bytes = bytes;
    if (owner) {
        owner->live += bytes;
        if (owner->live > owner->peak)
            owner->peak = owner->live;
    }
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
    owner->live -= block->bytes;
    Heap_Free(block);
}

/* Code or handler storage: freed two ticks later, see the file comment. */
static void retire(void *memory)
{
    struct block *block = header_of(memory);
    if (block->owner)
        block->owner->live -= block->bytes;
    block->next = retiring;
    retiring = block;
}

/* Manifest access; offsets were bounds-checked when the manifest was read. */
static const struct manifest_module *module_record(const struct rv_extension_record *record,
                                                   uint32_t index)
{
    return (const struct manifest_module *)(record->manifest + sizeof(struct manifest_header)) +
           index;
}

static const char *strings(const uint8_t *buffer)
{
    const struct manifest_header *header = (const struct manifest_header *)buffer;
    return (const char *)buffer + sizeof *header + header->module_count * sizeof(struct manifest_module);
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
        header->module_count > MAX_MODULES || (header->api >> 16) != RV_API_MAJOR)
        return 0;
    uint32_t table = sizeof *header + header->module_count * sizeof(struct manifest_module);
    if (bytes != table + header->strings_bytes || !header->strings_bytes ||
        buffer[bytes - 1] != 0)
        return 0;
    for (uint32_t i = 0; i < 6; i++)
        if (header->text[i] >= header->strings_bytes)
            return 0;
    const struct manifest_module *modules = (const struct manifest_module *)(buffer + sizeof *header);
    if (modules[0].trigger != TRIGGER_ALWAYS)
        return 0;
    for (uint32_t i = 0; i < header->module_count; i++)
        if (modules[i].name >= header->strings_bytes || modules[i].file >= header->strings_bytes ||
            modules[i].screens >= header->strings_bytes || modules[i].trigger > TRIGGER_MANUAL)
            return 0;
    return buffer[table + header->text[0]] != 0; /* id present */
}

/* ROOT "/" directory "/" file; 0 when it does not fit. */
static int build_path(char *path, const char *directory, const char *file)
{
    uint32_t n = 0;
    const char *parts[4] = {ROOT, directory, file, NULL};
    for (uint32_t p = 0; parts[p]; p++) {
        if (p)
            path[n++] = '/';
        for (const char *c = parts[p]; *c; c++) {
            if (n >= PATH_MAX_BYTES - 1)
                return 0;
            path[n++] = *c;
        }
    }
    path[n] = 0;
    return 1;
}

static uint32_t read_manifest(const char *directory, uint8_t *buffer)
{
    char path[PATH_MAX_BYTES];
    void *handle;
    uint32_t got = 0;
    if (!build_path(path, directory, MANIFEST_FILE) ||
        !core_file_open(path, CORE_FILE_READ, &handle))
        return 0;
    int ok = core_file_read(handle, buffer, MANIFEST_MAX, &got);
    core_file_close(handle);
    return ok && manifest_valid(buffer, got) ? got : 0;
}

/* rv_api */

static struct rv_extension_record *valid_handle(rv_handle self)
{
    for (struct rv_extension_record *r = core_extensions; r; r = r->next)
        if (r == self)
            return r;
    return NULL;
}

static int site_index(uint32_t id)
{
    for (uint32_t i = 0; i < CORE_SITE_COUNT; i++)
        if (core_site_ids[i] == id)
            return (int)i;
    return -1;
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
        int site = site_index(hooks[i].site);
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
    int site = record ? site_index(hook->site) : -1;
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
    int site = record ? site_index(hook->site) : -1;
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

int core_containers_declare(struct rv_extension_record *owner, const struct rv_container *container);
void core_containers_release(struct rv_extension_record *owner, const struct rv_container *container);

static int api_container_declare(rv_handle self, const struct rv_container *container)
{
    struct rv_extension_record *record = valid_handle(self);
    return record ? core_containers_declare(record, container) : RV_BAD_ARGUMENT;
}

static void api_container_release(rv_handle self, const struct rv_container *container)
{
    struct rv_extension_record *record = valid_handle(self);
    if (record)
        core_containers_release(record, container);
}

static int api_ui_push(rv_handle self, const char *screen, void *controller)
{
    struct rv_extension_record *record = valid_handle(self);
    struct core_screen *s = record ? core_screens_find(record, screen) : NULL;
    if (!s || !controller)
        return RV_BAD_ARGUMENT;
    return core_screens_push(s, controller) ? RV_OK : RV_NO_MEMORY;
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

const char *core_loader_last_log;

static void api_log(rv_handle self, const char *line)
{
    (void)self;
    core_loader_last_log = line;
}

static const struct rv_api api = {
    sizeof api,           RV_API_VERSION,     api_hook_bind,      api_hook_unbind,
    api_next,             api_setting_get,    api_setting_set,    core_settings_flush,
    api_alloc,            api_free,           api_module_load,    api_module_unload,
    api_container_declare, api_container_release, api_log,
    api_ui_push,          api_ui_pop,            api_ui_refresh,     api_ui_invalidate,
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

/* Reads, relocates and enters one module. The relocation table is consumed in
 * chunks through the stack, so a module costs text + data + bss and no more. */
static int load_module(struct rv_extension_record *record, uint32_t index)
{
    struct core_module *module = &record->modules[index];
    char path[PATH_MAX_BYTES];
    struct rvx_header header;
    void *handle;
    uint32_t got = 0;
    record->failure = CORE_EXTENSION_BAD_MODULE;
    if (!build_path(path, record->directory, text(record, module_record(record, index)->file)) ||
        !core_file_open(path, CORE_FILE_READ, &handle))
        return 0;
    int ok = core_file_read(handle, &header, sizeof header, &got) && got == sizeof header &&
             header.magic == RVX_MAGIC && header.format == RVX_FORMAT &&
             (header.api >> 16) == RV_API_MAJOR && header.firmware == CORE_FIRMWARE_ID &&
             (header.text_bytes | header.data_bytes) % 4 == 0 &&
             header.entry < header.text_bytes;
    uint32_t code_bytes = header.text_bytes + header.data_bytes;
    uint8_t *image = NULL;
    if (ok) {
        image = allocate(record, code_bytes + header.bss_bytes);
        if (!image)
            record->failure = CORE_EXTENSION_NO_MEMORY;
        ok = image && core_file_read(handle, image, code_bytes, &got) && got == code_bytes;
    }
    for (uint32_t done = 0; ok && done < header.reloc_count; done += RELOC_CHUNK) {
        uint32_t offsets[RELOC_CHUNK];
        uint32_t count = header.reloc_count - done;
        if (count > RELOC_CHUNK)
            count = RELOC_CHUNK;
        ok = core_file_read(handle, offsets, count * 4, &got) && got == count * 4;
        for (uint32_t i = 0; ok && i < count; i++) {
            if (offsets[i] % 4 || offsets[i] > code_bytes - 4)
                ok = 0;
            else
                *(uint32_t *)(image + offsets[i]) += (uint32_t)image;
        }
    }
    core_file_close(handle);
    if (!ok) {
        if (image)
            release(record, image);
        return 0;
    }
    core_cache_sync(image, image + code_bytes);
    module->image = image;
    module->bytes = code_bytes + header.bss_bytes;
    module->loaded = 1;
    module->idle = 0;
    const struct rv_extension *descriptor = enter(record, image, header.entry);
    if (index == 0) {
        if (!descriptor || descriptor->size != sizeof *descriptor) {
            record->failure = CORE_EXTENSION_NO_DESCRIPTOR;
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
        if (registered == RV_OK)
            registered = core_menus_register(record, descriptor->entries, descriptor->entry_count);
        if (registered != RV_OK) {
            unload_module(record, 0);
            record->failure = registered == RV_CONFLICT ? CORE_EXTENSION_ID_COLLISION
                                                        : CORE_EXTENSION_NO_MEMORY;
            return 0;
        }
        for (uint32_t i = 0; i < descriptor->container_count; i++)
            core_containers_declare(record, &descriptor->containers[i]);
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
        for (uint32_t i = 0; i < descriptor->container_count; i++)
            core_containers_release(record, &descriptor->containers[i]);
        core_menus_release(record);
        core_screens_release(record);
        for (uint32_t i = 0; i < descriptor->hook_count; i++)
            core_hook_unbind(&record->hooks[i]);
        retire(record->hooks);
        record->hooks = NULL;
        record->hooks_bound -= descriptor->hook_count;
        record->descriptor = NULL;
    }
    retire(module->image);
    module->image = NULL;
    module->loaded = 0;
}

/* Extensions */

static struct rv_extension_record *find_directory(const char *directory)
{
    for (struct rv_extension_record *r = core_extensions; r; r = r->next)
        if (same_name(r->directory, directory))
            return r;
    return NULL;
}

/* Reads the manifest and, when the extension is enabled, builds its record and
 * loads the main module. Returns the record, or NULL for disabled and failed
 * manifests (nothing is kept for either). */
static struct rv_extension_record *load_extension(const char *directory)
{
    uint8_t buffer[MANIFEST_MAX];
    uint32_t bytes = read_manifest(directory, buffer);
    if (!bytes || length(directory) >= sizeof ((struct rv_extension_record *)0)->directory)
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
    record->live = record->peak = record_bytes;
    record->owner = owner;
    record->module_count = (uint8_t)header->module_count;
    record->modules = (struct core_module *)(record + 1);
    record->manifest = (uint8_t *)(record->modules + header->module_count);
    record->manifest_bytes = bytes;
    for (uint32_t i = 0; i < bytes; i++)
        record->manifest[i] = buffer[i];
    for (uint32_t i = 0; directory[i]; i++)
        record->directory[i] = directory[i];
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

/* Directory entries under ROOT, collected before any file is opened. */
static uint32_t scan(char names[][sizeof ((struct rv_extension_record *)0)->directory])
{
    uint32_t iterator_words[DirectoryIterator_bytes / 4];
    uint32_t path_words[NativePath_bytes / 4];
    struct DirectoryIterator *iterator = (struct DirectoryIterator *)iterator_words;
    struct NativePath *path = (struct NativePath *)path_words;
    uint8_t is_directory = 0;
    uint32_t count = 0;
    NativePath_InitEmpty(path);
    DirectoryIterator_Init(iterator, ROOT, 0, 0);
    while (count < MAX_DIRECTORIES && DirectoryIterator_Next(iterator, path, &is_directory)) {
        const char *name = NativeString_GetUtf8((struct NativeString *)path);
        if (!is_directory || !name || !name[0] || name[0] == '.' ||
            length(name) >= sizeof names[0])
            continue;
        uint32_t i = 0;
        for (; name[i]; i++)
            names[count][i] = name[i];
        names[count][i] = 0;
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
    char names[MAX_DIRECTORIES][sizeof ((struct rv_extension_record *)0)->directory];
    uint32_t count = scan(names);
    for (uint32_t i = 0; i < count; i++)
        load_extension(names[i]);
}

int core_loader_set_enabled(const char *directory, int enabled)
{
    struct rv_extension_record *record = find_directory(directory);
    uint32_t owner;
    if (record) {
        owner = record->owner;
    } else {
        uint8_t buffer[MANIFEST_MAX];
        if (!read_manifest(directory, buffer))
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
        return load_extension(directory) ? RV_OK : RV_NOT_FOUND;
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
    return 0;
}

struct rv_extension_record *core_loader_find(uint32_t owner)
{
    for (struct rv_extension_record *r = core_extensions; r; r = r->next)
        if (r->owner == owner)
            return r;
    return NULL;
}

uint32_t core_loader_installed(struct core_installed *out, uint32_t capacity)
{
    char names[MAX_DIRECTORIES][sizeof out->directory];
    uint8_t buffer[MANIFEST_MAX];
    uint32_t found = scan(names), count = 0;
    for (uint32_t i = 0; i < found && count < capacity; i++) {
        if (!read_manifest(names[i], buffer))
            continue;
        const struct manifest_header *header = (const struct manifest_header *)buffer;
        struct core_installed *entry = &out[count++];
        const char *name = strings(buffer) + header->text[1];
        uint32_t n = 0;
        for (; names[i][n]; n++)
            entry->directory[n] = names[i][n];
        entry->directory[n] = 0;
        for (n = 0; name[n] && n < sizeof entry->name - 1; n++)
            entry->name[n] = name[n];
        entry->name[n] = 0;
        entry->owner = fnv1a(strings(buffer) + header->text[0]);
    }
    return count;
}

uint32_t core_loader_total_bytes(void)
{
    uint32_t total = 0;
    for (struct rv_extension_record *r = core_extensions; r; r = r->next)
        total += r->live;
    return total;
}
