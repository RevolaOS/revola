/* SPDX-License-Identifier: GPL-3.0-only */

/* One-line declarations over the structs of rv_api.h: a hook, row, screen,
 * entry or setting each, with counts taken from the arrays. Each macro is named
 * after what it builds (RV_ROW_PUSH builds a row of kind RV_ROW_KIND_PUSH).
 * `rows`, `labels` and `paragraphs` must be arrays, not pointers, because
 * RV_COUNT measures them. Macros with `...` take trailing designated overrides,
 * for example RV_SCREEN_LIST("planets", "Planets", rows, .layout = RV_LAYOUT_SPLIT)
 * or RV_SCREEN_CANVAS("clock", "Clock", draw, input,
 * .transition = RV_TRANSITION_FROM_PANE). */

#ifndef RV_UI_H
#define RV_UI_H
#include "rv_api.h"

#define RV_COUNT(array) (sizeof(array) / sizeof((array)[0]))

/* An opaque colour from its components. */
#define RV_RGB(r, g, b) ((rv_color)0xff000000u | (rv_color)(r) << 16 | (rv_color)(g) << 8 | (rv_color)(b))

/* Hooks. Filters on one site run by priority, lower first; 0 by default. */
#define RV_HOOK_FILTER(site_, fn_) {.site = (site_), .handler = (fn_), .mode = RV_HOOK_MODE_FILTER}
#define RV_HOOK_FILTER_AT(site_, fn_, priority_)                                                \
    {.site = (site_), .handler = (fn_), .priority = (priority_), .mode = RV_HOOK_MODE_FILTER}
#define RV_HOOK_REPLACE(site_, fn_) {.site = (site_), .handler = (fn_), .mode = RV_HOOK_MODE_REPLACE}

/* Rows of a list screen or of an entry. */
#define RV_ROW_PUSH(label_, screen_) {.label = (label_), .kind = RV_ROW_KIND_PUSH, .screen = (screen_)}
#define RV_ROW_ACTION(label_, fn_) {.label = (label_), .kind = RV_ROW_KIND_ACTION, .action = (fn_)}
#define RV_ROW_SETTING(label_, key_) {.label = (label_), .kind = RV_ROW_KIND_SETTING, .key = (key_)}
/* A row that shows text_() as its label and does nothing when chosen. */
#define RV_ROW_INFO(label_, text_) {.label = (label_), .kind = RV_ROW_KIND_INFO, .text = (text_)}

/* Screens. */
#define RV_SCREEN_LIST(id_, title_, rows_, ...)                                                 \
    {.id = (id_), .title = (title_), .kind = RV_SCREEN_KIND_LIST,                               \
     .list = {(rows_), RV_COUNT(rows_)} __VA_OPT__(, ) __VA_ARGS__}
#define RV_SCREEN_TEXT(id_, title_, paragraphs_, ...)                                           \
    {.id = (id_), .title = (title_), .kind = RV_SCREEN_KIND_TEXT,                               \
     .text = {(paragraphs_), RV_COUNT(paragraphs_)} __VA_OPT__(, ) __VA_ARGS__}
#define RV_SCREEN_CHOICE(id_, title_, key_, labels_, ...)                                       \
    {.id = (id_), .title = (title_), .kind = RV_SCREEN_KIND_CHOICE,                             \
     .choice = {(key_), 0, (labels_), RV_COUNT(labels_)} __VA_OPT__(, ) __VA_ARGS__}
#define RV_SCREEN_MULTI_CHOICE(id_, title_, key_, labels_, ...)                                 \
    {.id = (id_), .title = (title_), .kind = RV_SCREEN_KIND_MULTI_CHOICE,                       \
     .choice = {(key_), 0, (labels_), RV_COUNT(labels_)} __VA_OPT__(, ) __VA_ARGS__}
#define RV_SCREEN_SLIDER(id_, title_, key_, labels_, ...)                                       \
    {.id = (id_), .title = (title_), .kind = RV_SCREEN_KIND_SLIDER,                             \
     .choice = {(key_), 0, (labels_), RV_COUNT(labels_)} __VA_OPT__(, ) __VA_ARGS__}
#define RV_SCREEN_CANVAS(id_, title_, draw_, input_, ...)                                       \
    {.id = (id_), .title = (title_), .kind = RV_SCREEN_KIND_CANVAS,                             \
     .canvas = {(draw_), (input_)} __VA_OPT__(, ) __VA_ARGS__}
#define RV_SCREEN_DYNAMIC_LIST(id_, title_, count_, row_, select_, ...)                         \
    {.id = (id_), .title = (title_), .kind = RV_SCREEN_KIND_DYNAMIC_LIST,                       \
     .dynamic.count = (count_), .dynamic.row = (row_), .dynamic.select = (select_)            \
     __VA_OPT__(, ) __VA_ARGS__}

/* A row in `menu_` (an rv_menu) after the stock row `after_` (0 for the end).
 * `row_` is one of the row macros; .caption and .bitmap fill the Settings
 * preview pane, .shown_by names the bool setting that shows the row. */
#define RV_MENU_ENTRY(menu_, after_, row_, ...)                                                 \
    {.menu = (menu_), .after = (after_), .row = row_ __VA_OPT__(, ) __VA_ARGS__}

/* Settings. The labelled ones open their screen from an RV_ROW_SETTING row. */
#define RV_SETTING_BOOL(key_, label_, fallback_)                                                \
    {.key = (key_), .type = RV_SETTING_TYPE_BOOL, .fallback = (fallback_), .maximum = 1,       \
     .label = (label_)}
#define RV_SETTING_INT(key_, label_, fallback_, minimum_, maximum_)                             \
    {.key = (key_), .type = RV_SETTING_TYPE_INT, .fallback = (fallback_),                     \
     .minimum = (minimum_), .maximum = (maximum_), .label = (label_)}
#define RV_SETTING_LABELLED(key_, label_, fallback_, labels_, style_, maximum_)                 \
    {.key = (key_), .type = RV_SETTING_TYPE_INT, .style = (style_), .fallback = (fallback_),  \
     .maximum = (maximum_), .label = (label_), .labels = (labels_),                           \
     .label_count = RV_COUNT(labels_)}
#define RV_SETTING_CHOICE(key_, label_, fallback_, labels_)                                     \
    RV_SETTING_LABELLED(key_, label_, fallback_, labels_, RV_SETTING_STYLE_CHOICE,            \
                        RV_COUNT(labels_) - 1)
#define RV_SETTING_SLIDER(key_, label_, fallback_, labels_)                                     \
    RV_SETTING_LABELLED(key_, label_, fallback_, labels_, RV_SETTING_STYLE_SLIDER,            \
                        RV_COUNT(labels_) - 1)
#define RV_SETTING_MULTI(key_, label_, fallback_, labels_)                                      \
    RV_SETTING_LABELLED(key_, label_, fallback_, labels_, RV_SETTING_STYLE_MULTI,             \
                        (int32_t)(0xffffffffu >> (32 - RV_COUNT(labels_))))

/* The descriptor's arrays with their counts: `RV_HOOKS(hooks), RV_SCREENS(screens)`. */
#define RV_HOOKS(array) .hooks = (array), .hook_count = RV_COUNT(array)
#define RV_SETTINGS(array) .settings = (array), .setting_count = RV_COUNT(array)
#define RV_SCREENS(array) .screens = (array), .screen_count = RV_COUNT(array)
#define RV_ENTRIES(array) .entries = (array), .entry_count = RV_COUNT(array)

#endif
