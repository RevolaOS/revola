/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_TEMPLATES_H
#define CORE_TEMPLATES_H
#include <stdint.h>

#define CORE_MAX_PARAGRAPHS 8
/* View type of the canvas view: 'rvcv'. */
#define CORE_VIEW_CANVAS 0x72766376u

/* Templates the core built at boot, 0 when one could not be built. Their views
 * follow the template id: the text page's paragraphs from +1, the slider's bar
 * at +1 and its value text at +2, the canvas view at +1. */
extern uint32_t core_template_text;
extern uint32_t core_template_slider;
extern uint32_t core_template_canvas;
extern uint32_t core_template_empty;

/* Template `template`'s enter style when the layout it enters from has kind
 * `kind`, -1 for none. */
int16_t core_templates_enter_style(uint32_t template, uint32_t kind);

/* Builds the templates and binds the template enumeration hook; the canvas
 * view type must be registered first. */
void core_templates_init(void *bank);

#endif
