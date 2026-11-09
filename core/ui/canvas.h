/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_CANVAS_H
#define CORE_CANVAS_H
#include <stdint.h>

struct core_screen;

/* Makes the controller vtable canvas and slider screens use; 0 when the heap is
 * full. Called before their controller is created. */
int core_canvas_prepare(void);
/* Gives a created canvas or slider controller that vtable. */
void core_canvas_attach(void *controller);
/* Redraws the canvas view when a canvas is the top canvas screen. */
void core_canvas_invalidate(void);
/* Answers the slider's bar (Property_Control) and value text (Property_String);
 * 0 for anything else. */
int core_slider_property(struct core_screen *screen, uint32_t type, uint32_t id, uint32_t out);
/* Registers the canvas view type; before the templates are built. */
void core_canvas_init(void);

#endif
