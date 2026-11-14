/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_TEMPLATES_H
#define CORE_TEMPLATES_H
#include <stdint.h>

#define CORE_MAX_PARAGRAPHS 12
/* View type of the canvas view: 'rvcv'. */
#define CORE_VIEW_CANVAS 0x72766376u

enum core_template {
    CORE_TEMPLATE_TEXT,
    CORE_TEMPLATE_SLIDER,
    CORE_TEMPLATE_CANVAS,
    CORE_TEMPLATE_EMPTY,
    CORE_TEMPLATES
};
/* The template, built on the first call; 0 when it could not be built. Its
 * views follow the template id: the text page's paragraphs from +1, the
 * slider's bar at +1 and its value text at +2, the canvas view at +1, the
 * empty message's text at +1. */
uint32_t core_template(enum core_template which);

/* A copy of a stock template under a new id, with the views of `second`, if
 * not 0, appended; the views take the ids after it in that order and keep
 * their sources. View `replaced` (-1 for none) becomes an image view in its
 * box. 0 when it could not be built. */
uint32_t core_templates_copy(uint32_t stock_template, uint32_t second, int replaced);

/* Template `template`'s enter style when the layout it enters from has kind
 * `kind`, -1 for none. */
int16_t core_templates_enter_style(uint32_t template, uint32_t kind);

void core_templates_init(void *bank);

#endif
