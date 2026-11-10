/* SPDX-License-Identifier: GPL-3.0-only */

/* The row records behind core/ui/menus.h, shared by the files that build what
 * Apple reads from them. */

#ifndef CORE_MENUROWS_H
#define CORE_MENUROWS_H
#include "loader/loader.h"
#include "rv_api.h"
#include "ui/menus.h"
#include <stdint.h>

/* The root of a row in a container rather than a menu. */
#define CORE_ROOT_CONTAINER RV_ROOT_COUNT

struct row {
    struct rv_extension_record *owner;
    const struct rv_entry *entry; /* NULL while stale: the owner's image is gone */
    uint32_t item;                /* resource id; item + 1 is the title source or label string */
    uint32_t label;               /* FNV-1a of the label, to find the row again after a reload */
    uint32_t after;               /* the stock row it follows; 0 for a context menu or container */
    uint32_t container;           /* FNV-1a of the container it is in, for CORE_ROOT_CONTAINER */
    /* Heap, kept by Apple: the ROW_BYTES of a menu row, built when Apple builds
     * the table, or the "Rv" handler name a context row's action points at. */
    void *data;
    uint8_t root;
    uint8_t index; /* the entry's index in its descriptor, its visibility bit */
    uint8_t stale;
};

/* Rows in one heap block per registration of an owner, freed when the owner
 * unloads. The core's own blocks stay for the boot: a retired core row is a
 * stale record found again when its entry registers again. */
struct row_set {
    struct row_set *next;
    uint32_t key; /* core_resource_owner of the owner */
    uint32_t count;
    struct row rows[];
};

extern struct row_set *core_menu_sets;

#define EACH_ROW(r)                                                                       \
    for (struct row_set *set_ = core_menu_sets; set_; set_ = set_->next)                   \
        for (struct row *r = set_->rows; r < set_->rows + set_->count; r++)

/* Whether a row in a container, a row that opens one or a stock row shows
 * (ui/submenus.c). */
int core_submenus_shown(const struct row *r);
/* 1 when `data` is the stock Settings row `id` in Apple's Settings table and a
 * shown stock row in a container claims it. */
int core_submenus_moves(uint32_t id, const void *data);

/* 1 when the screen's event table carries the bindings of the root's rows. */
int core_menus_serves(uint8_t root, uint32_t screen_id);
/* The bindings of the rows, added when Apple loads a screen's events. */
void core_bindings_init(void);

#endif
