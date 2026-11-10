/* SPDX-License-Identifier: GPL-3.0-only */

/* The screen registry: one record per declared screen, holding its ids from
 * the owner's block. The core's own screens are records without an owner. */

#include "ui/screens.h"
#include "loader/loader.h"
#include "ui/clones.h"
#include "osos-2.0.5.h"
#include "ui/resources.h"
#include "settings.h"
#include <stddef.h>

/* Records in one heap block per registration of an owner, freed when the
 * owner unloads. The core's own blocks stay for the boot: a retired core screen
 * keeps its ids as a stale record for its next declaration. */
struct screen_set {
    struct screen_set *next;
    uint32_t key; /* core_resource_owner of the owner */
    uint32_t count;
    uint32_t bytes;
    struct core_screen screens[];
};

static struct screen_set *sets;

#define EACH_SCREEN(s)                                                                    \
    for (struct screen_set *set_ = sets; set_; set_ = set_->next)                          \
        for (struct core_screen *s = set_->screens; s < set_->screens + set_->count; s++)

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

/* The owner's stale record of a screen with this name and size, or NULL. */
static struct core_screen *stale_record(uint32_t key, uint32_t name, uint32_t ids)
{
    for (struct screen_set *set = sets; set; set = set->next)
        for (uint32_t i = 0; set->key == key && i < set->count; i++)
            if (set->screens[i].stale && set->screens[i].ids == ids &&
                set->screens[i].name == name)
                return &set->screens[i];
    return NULL;
}

/* What tells a screen's record apart across reloads: its id, or for the
 * screens settings make, which have none, the title. */
static uint32_t screen_key(const struct rv_screen *d)
{
    return core_settings_owner(d->id ? d->id : d->title);
}

int core_screens_register(struct rv_extension_record *owner, uint8_t module,
                          const struct rv_screen *screens, uint32_t count)
{
    uint32_t key = core_resource_owner(owner), fresh = 0;
    for (uint32_t n = 0; n < count; n++)
        fresh += !stale_record(key, screen_key(&screens[n]),
                               core_clone_ids(&screens[n]));
    struct screen_set *set = NULL;
    if (fresh) {
        uint32_t bytes = sizeof *set + fresh * sizeof set->screens[0];
        set = core_heap_allocate(owner, bytes);
        if (!set)
            return RV_NO_MEMORY;
        set->key = key;
        set->bytes = bytes;
        struct screen_set **tail = &sets;
        while (*tail)
            tail = &(*tail)->next;
        *tail = set;
    }
    for (uint32_t n = 0; n < count; n++) {
        const struct rv_screen *d = &screens[n];
        uint32_t name = screen_key(d), ids = core_clone_ids(d);
        struct core_screen *s = stale_record(key, name, ids);
        if (!s) {
            uint32_t id = core_resource_take(key, ids);
            if (!id)
                return RV_NO_MEMORY;
            s = &set->screens[set->count++];
            s->name = name;
            s->id = id;
            s->ids = ids;
        }
        s->owner = owner;
        s->declaration = d;
        s->stale = 0;
        s->module = module;
    }
    return RV_OK;
}

void core_screens_release(struct rv_extension_record *owner)
{
    uint32_t key = core_resource_owner(owner);
    if (!owner)
        return;
    core_screens_abandon(owner);
    for (struct screen_set **link = &sets; *link;) {
        struct screen_set *set = *link;
        if (set->key != key) {
            link = &set->next;
            continue;
        }
        *link = set->next;
        core_heap_free(owner, set, set->bytes);
    }
}

void core_screens_retire_module(struct rv_extension_record *owner, uint8_t module)
{
    uint32_t key = core_resource_owner(owner);
    for (struct screen_set *set = sets; set; set = set->next)
        for (uint32_t i = 0; set->key == key && i < set->count; i++)
            if (set->screens[i].declaration && set->screens[i].module == module) {
                set->screens[i].declaration = NULL;
                set->screens[i].stale = 1;
            }
}

struct core_screen *core_screens_find(struct rv_extension_record *owner, const char *name)
{
    EACH_SCREEN(s)
        if (s->declaration && s->declaration->id && s->owner == owner &&
            same(s->declaration->id, name))
            return s;
    return NULL;
}

const struct rv_setting *core_screens_setting(struct rv_extension_record *owner, uint16_t key)
{
    const struct rv_descriptor *d = core_loader_descriptor(owner);
    for (uint32_t i = 0; d && i < d->setting_count; i++)
        if (d->settings[i].key == key)
            return &d->settings[i];
    return NULL;
}

int32_t core_screens_setting_value(struct rv_extension_record *owner, uint16_t key)
{
    const struct rv_setting *setting = core_screens_setting(owner, key);
    return core_settings_get_int(owner ? owner->owner : CORE_SETTINGS_OWNER_CORE, key,
                                 setting ? setting->fallback : 0);
}

/* A setting with labels and a style edits as a screen of that style, titled
 * by its label. It has no id: only its setting's rows open it. */
static uint8_t setting_screen_kind(const struct rv_setting *setting)
{
    if (setting->type != RV_SETTING_TYPE_INT || !setting->labels || !setting->label_count)
        return 0xff;
    switch (setting->style) {
    case RV_SETTING_STYLE_CHOICE:
        return RV_SCREEN_KIND_CHOICE;
    case RV_SETTING_STYLE_SLIDER:
        return RV_SCREEN_KIND_SLIDER;
    case RV_SETTING_STYLE_MULTI:
        return RV_SCREEN_KIND_MULTI_CHOICE;
    default:
        return 0xff;
    }
}

uint32_t core_screens_from_settings(const struct rv_setting *settings, uint32_t count,
                                    struct rv_screen *out)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t kind = setting_screen_kind(&settings[i]);
        if (kind == 0xff)
            continue;
        if (out) {
            struct rv_screen *s = &out[n];
            s->title = settings[i].label;
            s->kind = kind;
            s->layout = RV_LAYOUT_AUTO;
            s->choice.key = settings[i].key;
            s->choice.labels = settings[i].labels;
            s->choice.count = settings[i].label_count;
        }
        n++;
    }
    return n;
}

struct core_screen *core_screens_find_setting(struct rv_extension_record *owner, uint16_t key)
{
    EACH_SCREEN(s) {
        const struct rv_screen *d = s->declaration;
        if (d && s->owner == owner && d->kind != RV_SCREEN_KIND_LIST && d->kind != RV_SCREEN_KIND_TEXT &&
            d->kind != RV_SCREEN_KIND_CANVAS && d->kind != RV_SCREEN_KIND_DYNAMIC_LIST && d->choice.key == key)
            return s;
    }
    return NULL;
}

struct core_screen *core_screens_of_id(uint32_t id)
{
    for (struct screen_set *set = sets; set; set = set->next) {
        if (!set->count || id - set->screens[0].id >= CORE_RESOURCE_BLOCK_BYTES)
            continue; /* another owner's block */
        for (uint32_t i = 0; i < set->count; i++) {
            struct core_screen *s = &set->screens[i];
            if (s->declaration && id - s->id < s->ids)
                return s;
        }
    }
    return NULL;
}

void core_screens_handler_name(char *out, uint32_t item)
{
    static const char digits[] = "0123456789abcdef";
    out[0] = 'R';
    out[1] = 'v';
    for (unsigned i = 0; i < 8; i++)
        out[2 + i] = digits[(item >> (28 - 4 * i)) & 15];
    out[10] = 0;
}

