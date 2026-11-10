/* SPDX-License-Identifier: GPL-3.0-only */

/* One-line declarations over the structs of rv_api.h: a row, screen, entry or
 * setting each, with counts taken from the arrays. `rows`, `labels` and
 * `paragraphs` must be arrays, not pointers, because RV_COUNT measures them.
 * Macros with `...` take trailing designated overrides, for example
 * RV_LIST("planets", "Planets", rows, .layout = RV_LAYOUT_SPLIT) or
 * RV_CANVAS("clock", "Clock", draw, input, .opening = RV_OPENING_FROM_PANE). */

#ifndef RV_UI_H
#define RV_UI_H
#include "rv_api.h"

#define RV_COUNT(array) (sizeof(array) / sizeof((array)[0]))

/* An opaque colour from its components. */
#define RV_RGB(r, g, b) ((rv_color)0xff000000u | (rv_color)(r) << 16 | (rv_color)(g) << 8 | (rv_color)(b))

/* Rows of a list screen or of an entry. */
#define RV_PUSH(label_, screen_) {.label = (label_), .kind = RV_ROW_PUSH, .screen = (screen_)}
#define RV_TOGGLE(label_, key_) {.label = (label_), .kind = RV_ROW_TOGGLE, .key = (key_)}
#define RV_ACTION(label_, fn_) {.label = (label_), .kind = RV_ROW_ACTION, .action = (fn_)}
#define RV_SETTING(label_, key_) {.label = (label_), .kind = RV_ROW_SETTING, .key = (key_)}
#define RV_STOCK(label_, row_) {.label = (label_), .kind = RV_ROW_STOCK, .stock = (row_)}
/* A row that shows text_() as its label and does nothing when chosen. */
#define RV_LIVE(label_, text_) {.label = (label_), .kind = RV_ROW_ACTION, .text = (text_)}

/* Screens. */
#define RV_LIST(name_, title_, rows_, ...)                                                      \
    {.name = (name_), .title = (title_), .kind = RV_SCREEN_LIST,                               \
     .list = {(rows_), RV_COUNT(rows_)} __VA_OPT__(, ) __VA_ARGS__}
#define RV_TEXT(name_, title_, paragraphs_, ...)                                                \
    {.name = (name_), .title = (title_), .kind = RV_SCREEN_TEXT,                               \
     .text = {(paragraphs_), RV_COUNT(paragraphs_)} __VA_OPT__(, ) __VA_ARGS__}
#define RV_CHOICE(name_, title_, key_, labels_, ...)                                             \
    {.name = (name_), .title = (title_), .kind = RV_SCREEN_CHOICE,                             \
     .choice = {(key_), 0, (labels_), RV_COUNT(labels_)} __VA_OPT__(, ) __VA_ARGS__}
#define RV_MULTI_CHOICE(name_, title_, key_, labels_, ...)                                       \
    {.name = (name_), .title = (title_), .kind = RV_SCREEN_MULTI_CHOICE,                       \
     .choice = {(key_), 0, (labels_), RV_COUNT(labels_)} __VA_OPT__(, ) __VA_ARGS__}
#define RV_SLIDER(name_, title_, key_, labels_, ...)                                             \
    {.name = (name_), .title = (title_), .kind = RV_SCREEN_SLIDER,                             \
     .choice = {(key_), 0, (labels_), RV_COUNT(labels_)} __VA_OPT__(, ) __VA_ARGS__}
#define RV_CANVAS(name_, title_, draw_, input_, ...)                                             \
    {.name = (name_), .title = (title_), .kind = RV_SCREEN_CANVAS,                             \
     .canvas = {(draw_), (input_)} __VA_OPT__(, ) __VA_ARGS__}
#define RV_RUNTIME_LIST(name_, title_, count_, row_, select_, ...)                                \
    {.name = (name_), .title = (title_), .kind = RV_SCREEN_RUNTIME_LIST,                       \
     .runtime.count = (count_), .runtime.row = (row_), .runtime.select = (select_)            \
     __VA_OPT__(, ) __VA_ARGS__}

/* An entry: a row in a menu, shown by default. `row` is one of the row macros;
 * .caption and .bitmap fill the Settings preview pane, .visible = 0 hides a
 * Main or Music row until Settings turns it on. */
#define RV_ENTRY(in_, after_, row_, ...)                                                        \
    {.in = (in_), .visible = 1, .after = (after_), .row = row_ __VA_OPT__(, ) __VA_ARGS__}

/* A submenu shared by id: RV_CONTAINER("settings.sound", "settings", "Sound"). */
#define RV_CONTAINER(id_, parent_, label_) {.id = (id_), .parent = (parent_), .label = (label_)}

/* Settings. The labelled ones open their screen from an RV_SETTING row. */
#define RV_SETTING_FLAG(key_, label_, fallback_)                                                \
    {.key = (key_), .type = RV_SETTING_BOOL, .fallback = (fallback_), .maximum = 1,            \
     .label = (label_)}
#define RV_SETTING_NUMBER(key_, label_, fallback_, minimum_, maximum_)                            \
    {.key = (key_), .type = RV_SETTING_INT, .fallback = (fallback_), .minimum = (minimum_),     \
     .maximum = (maximum_), .label = (label_)}
#define RV_SETTING_LABELLED(key_, label_, fallback_, labels_, style_, maximum_)                  \
    {.key = (key_), .type = RV_SETTING_INT, .style = (style_), .fallback = (fallback_),        \
     .maximum = (maximum_), .label = (label_), .labels = (labels_),                            \
     .label_count = RV_COUNT(labels_)}
#define RV_SETTING_CHOICE(key_, label_, fallback_, labels_)                                      \
    RV_SETTING_LABELLED(key_, label_, fallback_, labels_, RV_STYLE_CHOICE, RV_COUNT(labels_) - 1)
#define RV_SETTING_SLIDER(key_, label_, fallback_, labels_)                                      \
    RV_SETTING_LABELLED(key_, label_, fallback_, labels_, RV_STYLE_SLIDER, RV_COUNT(labels_) - 1)
#define RV_SETTING_MULTI(key_, label_, fallback_, labels_)                                       \
    RV_SETTING_LABELLED(key_, label_, fallback_, labels_, RV_STYLE_MULTI,                        \
                        (int32_t)((1u << RV_COUNT(labels_)) - 1))

/* The descriptor's arrays with their counts: `RV_HOOKS(hooks), RV_SCREENS(screens)`. */
#define RV_HOOKS(array) .hooks = (array), .hook_count = RV_COUNT(array)
#define RV_SETTINGS(array) .settings = (array), .setting_count = RV_COUNT(array)
#define RV_CONTAINERS(array) .containers = (array), .container_count = RV_COUNT(array)
#define RV_SCREENS(array) .screens = (array), .screen_count = RV_COUNT(array)
#define RV_ENTRIES(array) .entries = (array), .entry_count = RV_COUNT(array)

#endif
