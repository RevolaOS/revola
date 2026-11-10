/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_CLONES_H
#define CORE_CLONES_H
#include "rv_api.h"
#include "ui/resources.h"
#include <stdint.h>

struct core_screen;

#define CORE_MAX_LIST_ROWS 16
/* Transient buffer for one resource at a time; a list's ITEM table may need more. */
#define CORE_CLONE_SCRATCH_BYTES 1536

/* Sources for `count` consecutive views of a template, applied to the instance
 * that uses the template so the views are rebound on every display. */
struct view_sources {
    uint32_t view;   /* first view id */
    uint32_t source; /* first source id */
    uint32_t count;
};

/* A layout clone: the stock layout's instances with the title view's source,
 * the item table and one template or VLyt replaced. */
struct layout_clone {
    uint32_t stock;
    uint32_t layout;     /* id of the clone */
    uint32_t title_view; /* status bar view whose source becomes `title` */
    uint32_t title;
    uint32_t items;      /* ITEM table for the list binding, 0 to keep */
    uint32_t from, to;   /* template or VLyt id replaced, 0 for none; a new VLyt id needs a
                            TEVT stored under it, or loading the layout's events aborts */
    uint32_t drop;       /* template whose instances are removed, 0 for none */
    const struct view_sources *extra; /* sources for the views of template `to` */
};

/* Ids a screen of this declaration takes from its owner's block. */
uint32_t core_clone_ids(const struct rv_screen *declaration);
/* Clones a stock layout: every SSin list and the VCvs of instances whose
 * bindings change, with ids from *next. 0 when a stock resource is missing or
 * does not fit the scratch. */
int core_clone_layout(const struct layout_clone *clone, uint32_t *next, uint8_t *scratch,
                      uint32_t scratch_bytes);
/* The screen's own resources: SCST from `controller_screen`, SLst from
 * `layouts_screen` with the first layout replaced, CEVT from `events`. */
int core_clone_screen(uint32_t screen, uint32_t layout, uint32_t controller_screen,
                      uint32_t layouts_screen, const void *events, uint32_t event_bytes,
                      uint8_t *scratch, uint32_t scratch_bytes);
/* Layout events: Menu pops the screen, holding it returns to the main menu. */
void core_clone_layout_events(struct core_writer *w);
/* Store the clones a screen is made of under its ids; 0 when a stock resource
 * is missing or the heap is full. Lists and indexed lists are in lists.c. */
int core_clone_text(struct core_screen *screen);
int core_clone_slider(struct core_screen *screen);
int core_clone_canvas(struct core_screen *screen);
/* Reads an event stream into a loaded layout's event map. */
void core_clone_read_events(void *layout, const void *stream, uint32_t bytes);
void core_clones_init(void *bank);

#endif
