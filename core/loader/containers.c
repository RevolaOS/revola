/* SPDX-License-Identifier: GPL-3.0-only */

/* Shared menu containers: a submenu that any number of extensions declare by
 * id, under a menu or another container. A declared container is a list screen
 * of the core with a row in its parent; ui/submenus.c fills the screen with the
 * entries that name the container and shows the row while one of them shows.
 * A record lives on the heap while the container is declared. */

#include "loader/containers.h"
#include "osos-2.0.5.h"
#include "settings.h"
#include "ui/menus.h"
#include "ui/screens.h"
#include <stddef.h>

#define MAX_CONTAINERS 8

static struct core_container *containers[MAX_CONTAINERS];

struct core_container *core_containers_find(uint32_t id)
{
    for (uint32_t i = 0; i < MAX_CONTAINERS; i++)
        if (containers[i] && containers[i]->id == id)
            return containers[i];
    return NULL;
}

static void copy(char *out, const char *text, uint32_t bytes)
{
    uint32_t i = 0;
    for (; text[i] && i < bytes - 1; i++)
        out[i] = text[i];
    out[i] = 0;
}

void core_containers_changed(void)
{
    for (uint32_t i = 0; i < MAX_CONTAINERS; i++)
        if (containers[i])
            containers[i]->dirty = 1;
}

int core_containers_declare(struct rv_extension_record *owner, const struct rv_container *container)
{
    (void)owner;
    if (!container->id || !container->parent || !container->label)
        return RV_BAD_ARGUMENT;
    uint32_t id = core_settings_owner(container->id);
    uint32_t parent = core_settings_owner(container->parent);
    struct core_container *c = core_containers_find(id);
    if (c) {
        if (c->parent != parent)
            return RV_CONFLICT;
        c->count++;
        return RV_OK;
    }
    uint32_t slot = 0;
    while (slot < MAX_CONTAINERS && containers[slot])
        slot++;
    if (slot == MAX_CONTAINERS)
        return RV_NO_MEMORY;
    c = Heap_AllocateZeroed(sizeof *c);
    if (!c)
        return RV_NO_MEMORY;
    c->id = id;
    c->parent = parent;
    c->count = 1;
    c->dirty = 1;
    copy(c->name, container->id, sizeof c->name);
    copy(c->in, container->parent, sizeof c->in);
    copy(c->label, container->label, sizeof c->label);
    c->entry.in = c->in;
    c->entry.visible = 1;
    c->entry.row.label = c->label;
    c->entry.row.kind = RV_ROW_PUSH;
    c->entry.row.screen = c->name;
    c->screen.name = c->name;
    c->screen.title = c->label;
    c->screen.kind = RV_SCREEN_LIST;
    if (core_screens_register(NULL, &c->screen, 1) != RV_OK ||
        core_menus_register(NULL, &c->entry, 1) != RV_OK) {
        core_screens_retire(&c->screen);
        Heap_Free(c);
        return RV_NO_MEMORY;
    }
    containers[slot] = c;
    core_menus_changed();
    return RV_OK;
}

void core_containers_release(struct rv_extension_record *owner, const struct rv_container *container)
{
    (void)owner;
    struct core_container *c = container->id ? core_containers_find(core_settings_owner(container->id)) : NULL;
    if (!c || --c->count)
        return;
    for (uint32_t i = 0; i < MAX_CONTAINERS; i++)
        if (containers[i] == c)
            containers[i] = NULL;
    core_menus_retire(&c->entry);
    core_screens_retire(&c->screen);
    Heap_Free(c);
    core_menus_changed();
}
