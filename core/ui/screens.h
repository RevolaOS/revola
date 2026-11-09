/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_SCREENS_H
#define CORE_SCREENS_H
#include "loader/loader.h"
#include "rv_api.h"
#include "hooks/sites.h"

/* One registered screen: a declaration of its owner and the ids the clones use.
 * The owner is NULL for the core's own screens. A stale record outlives its
 * owner's image, so it keeps the name hash instead of the declaration and is
 * reused when the same owner declares the screen again in this boot. */
struct core_screen {
    struct rv_extension_record *owner;
    const struct rv_screen *declaration; /* NULL while stale */
    uint32_t name;                       /* FNV-1a of the declaration's name */
    uint32_t id;                         /* first resource id: the screen; the rest follow */
    uint32_t ids;                        /* ids used from `id`; 0 for a free record */
    uint16_t open;                       /* times on the navigation stack */
    uint8_t built;
    uint8_t stale;
};
/* First row item id (list), paragraph source id (text) or bar source id (slider). */
#define CORE_SCREEN_ROWS(s) ((s)->id + 8)

/* Registers the descriptor's screens; RV_NO_MEMORY when the table or the
 * owner's id block is full, RV_CONFLICT when another owner holds the block. */
int core_screens_register(struct rv_extension_record *owner, const struct rv_screen *screens,
                          uint32_t count);
void core_screens_release(struct rv_extension_record *owner);
/* The screen named `name` of `owner`, or NULL. */
struct core_screen *core_screens_find(struct rv_extension_record *owner, const char *name);
/* The screen whose ids include `id`, or NULL. */
struct core_screen *core_screens_of_id(uint32_t id);
/* The open screen of a controller the runtime pushed, or NULL. */
struct core_screen *core_screens_of_controller(void *controller);
/* The most recently pushed open screen of a kind, or NULL. */
struct core_screen *core_screens_top(uint8_t kind);
/* Opens a screen on top of `controller`'s screen; loads its module first. */
int core_screens_push(struct core_screen *screen, void *controller);
/* Pops the top screen. */
void core_screens_pop(void);
/* Redraws an open canvas. */
void core_screens_invalidate(void);
/* The Settings model, as last seen on the property slot. */
void *core_screens_settings_model(void);
/* Tells the Settings model a property's text changed, so its views redraw. */
void core_screens_notify(uint32_t property);
/* Redraws rows whose text may have changed. */
void core_screens_refresh(struct core_screen *screen);
/* "Rv" + item in hex into out[11]: the handler name a row's binding carries. */
void core_screens_handler_name(char *out, uint32_t item);
/* Runs the row behind a handler name; 0 when the name is not one. */
uint32_t core_screens_action(void *controller, const char *name, uint32_t argument);
/* The same as a filter on an action slot. */
uint32_t core_screens_action_filter(uint32_t controller, uint32_t name, uint32_t argument,
                                    const struct core_next *next);

void core_screens_init(void *bank);

#endif
