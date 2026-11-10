/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_CONTAINERS_H
#define CORE_CONTAINERS_H
#include "loader/loader.h"
#include "rv_api.h"
#include <stdint.h>

#define CORE_CONTAINER_ID_BYTES 32
#define CORE_CONTAINER_LABEL_BYTES 24

/* A declared container: the row that opens it in its parent and its screen,
 * both registered by the core while at least one extension declares it. */
struct core_container {
    uint32_t id;     /* FNV-1a of the id */
    uint32_t parent; /* FNV-1a of the parent's id or menu name */
    uint8_t count;   /* live declarations */
    uint8_t dirty;   /* its rows changed since its screen was built */
    char name[CORE_CONTAINER_ID_BYTES];   /* the id: the screen's name */
    char in[CORE_CONTAINER_ID_BYTES];     /* the parent: the row's rv_entry.in */
    char label[CORE_CONTAINER_LABEL_BYTES];
    struct rv_entry entry;
    struct rv_screen screen;
};

/* The first declaration of an id fixes its parent and label: a later one with
 * another parent is refused, another label is ignored. */
int core_containers_declare(struct rv_extension_record *owner, const struct rv_container *container);
void core_containers_release(struct rv_extension_record *owner, const struct rv_container *container);
/* The declared container with this id hash, or NULL. */
struct core_container *core_containers_find(uint32_t id);
/* Marks every container's screen for a rebuild on its next push. */
void core_containers_changed(void);

#endif
