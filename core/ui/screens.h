/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_SCREENS_H
#define CORE_SCREENS_H
#include "loader/loader.h"
#include "rv_api.h"
#include "hooks/sites.h"

/* One registered screen: a declaration of its owner and the ids the clones use.
 * The owner is NULL for the core's own screens. A retired core screen stays as
 * a stale record, which keeps the name hash instead of the declaration and is
 * reused when the screen is declared again in this boot. */
struct core_screen {
    struct rv_extension_record *owner;
    const struct rv_screen *declaration; /* NULL while stale */
    uint32_t name;                       /* FNV-1a of the declaration's name */
    uint32_t id;                         /* first resource id: the screen; the rest follow */
    uint32_t ids;                        /* ids used from `id`; 0 for a free record */
    uint16_t open;                       /* times on the navigation stack */
    uint8_t built;                       /* CORE_BUILT_* bits */
    uint8_t split;                       /* the layout of the last push, list kinds */
    uint8_t stale;
    uint8_t module;                      /* the owner's module that declares it */
};
/* A list screen has clones for both layouts, built on first use. */
#define CORE_BUILT_FULL 1
#define CORE_BUILT_SPLIT 2
/* First row item id (list), paragraph source id (text) or bar source id (slider). */
#define CORE_SCREEN_ROWS(s) ((s)->id + 8)

/* Registers the screens a module of `owner` declares; RV_NO_MEMORY when the
 * heap or the owner's id block is full. */
int core_screens_register(struct rv_extension_record *owner, uint8_t module,
                          const struct rv_screen *screens, uint32_t count);
/* An on-demand module unloads: its screens become stale records that keep
 * their ids for its next load. The loader unloads a module only while none of
 * them is open. */
void core_screens_retire_module(struct rv_extension_record *owner, uint8_t module);
/* Frees the owner's records; its open controllers stay up without a screen. */
void core_screens_release(struct rv_extension_record *owner);
/* Detaches the owner's open screens from their controllers; returns how many. */
uint32_t core_screens_abandon(struct rv_extension_record *owner);
/* Controllers still up whose screen's owner unloaded. */
uint32_t core_screens_orphans(void);
/* The screen named `name` of `owner`, or NULL. */
struct core_screen *core_screens_find(struct rv_extension_record *owner, const char *name);
/* The screen of `owner` that edits the setting `key`, or NULL. */
struct core_screen *core_screens_find_setting(struct rv_extension_record *owner, uint16_t key);
/* The declared setting `key` of `owner`; NULL when undeclared or for the core. */
const struct rv_setting *core_screens_setting(struct rv_extension_record *owner, uint16_t key);
/* The setting's int value, its declared fallback when the store has none. */
int32_t core_screens_setting_value(struct rv_extension_record *owner, uint16_t key);
/* The screens RV_ROW_SETTING rows open: one per int setting with a style and
 * labels, written to `out` when it is not NULL. Returns how many. */
uint32_t core_screens_from_settings(const struct rv_setting *settings, uint32_t count,
                                    struct rv_screen *out);
/* Redraws every open row that shows the setting `key` of `owner`. */
void core_screens_setting_changed(struct rv_extension_record *owner, uint16_t key);
/* The screen whose ids include `id`, or NULL. */
struct core_screen *core_screens_of_id(uint32_t id);
/* The open screen of a controller the runtime pushed, or NULL. */
struct core_screen *core_screens_of_controller(void *controller);
/* The most recently pushed open screen of a kind, or NULL. */
struct core_screen *core_screens_top(uint8_t kind);
/* The canvas just popped, while its leaving copy may still be drawn; NULL once
 * another screen is pushed, its closed callback ran, or while another canvas is
 * open. */
struct core_screen *core_screens_popped_canvas(void);
/* Pushes `screen` through `controller`'s push method, which lands on top of
 * the navigator whichever controller it is; `arg` is ctx->arg. */
int core_screens_push(struct core_screen *screen, void *controller, void *arg);
/* The ctx of a callback of `screen`: its owner, controller and push arg. */
void core_screens_context(const struct core_screen *screen, struct rv_context *ctx);
/* Runs the closed callback of the screen popped last, if it still waits:
 * after a popped canvas's last draw, at the next push and at the grace tick. */
void core_screens_closed(void);
/* Grace point: runs a closed callback that waited a whole tick for its draw. */
void core_screens_tick(void);
/* 1 the first time it is asked for a pushed controller, 0 after: an activation
 * that is the screen opening, not a return to it. */
int core_screens_first_activation(void *controller);
/* The navigator's top controller. */
void *core_screens_top_controller(void);
/* The controller whose row, choice or input callback is running; NULL outside
 * one. A split list opened from it takes its layout. */
extern void *core_screens_caller;
/* Pops the top screen when `owner` declared it. */
void core_screens_pop(struct rv_extension_record *owner);
/* Redraws the canvas on top, or `rect` of it, when `owner` declared it. */
void core_screens_invalidate(struct rv_extension_record *owner, const struct rv_rect *rect);
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

/* The label a row of `owner` shows now, on `screen` (NULL for a menu entry):
 * its text callback's, else its label. */
const char *core_row_label(const struct rv_row *row, struct rv_extension_record *owner,
                           const struct core_screen *screen);

void core_screens_init(void *bank);

#endif
