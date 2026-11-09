/* SPDX-License-Identifier: GPL-3.0-only */

/* Shared menu containers: a submenu under a native root that any number of
 * extensions declare by id. The first declaration fixes the parent; a later one
 * with another parent is refused, a different label is ignored. TODO: the
 * container row is not yet materialised in the native menu (Settings > Extensions
 * is the first user). */

#include "loader/loader.h"
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "settings.h"

#define MAX_CONTAINERS 8
#define LABEL_BYTES 24

struct container {
    uint32_t id;     /* FNV-1a of the id, 0 when the slot is free */
    uint32_t parent; /* FNV-1a of the parent id */
    uint8_t count;   /* live declarations */
    char label[LABEL_BYTES];
};

static struct container containers[MAX_CONTAINERS];

static struct container *find(uint32_t id)
{
    for (uint32_t i = 0; i < MAX_CONTAINERS; i++)
        if (containers[i].id == id)
            return &containers[i];
    return 0;
}

int core_containers_declare(struct rv_extension_record *owner, const struct rv_container *container)
{
    (void)owner;
    if (!container->id || !container->parent || !container->label)
        return RV_BAD_ARGUMENT;
    uint32_t id = core_settings_owner(container->id);
    uint32_t parent = core_settings_owner(container->parent);
    struct container *slot = find(id);
    if (slot) {
        if (slot->parent != parent)
            return RV_CONFLICT;
        slot->count++;
        return RV_OK;
    }
    slot = find(0);
    if (!slot)
        return RV_NO_MEMORY;
    slot->id = id;
    slot->parent = parent;
    slot->count = 1;
    uint32_t i = 0;
    for (; container->label[i] && i < LABEL_BYTES - 1; i++)
        slot->label[i] = container->label[i];
    slot->label[i] = 0;
    return RV_OK;
}

void core_containers_release(struct rv_extension_record *owner, const struct rv_container *container)
{
    (void)owner;
    struct container *slot = container->id ? find(core_settings_owner(container->id)) : 0;
    if (slot && slot->count && !--slot->count)
        slot->id = 0;
}
