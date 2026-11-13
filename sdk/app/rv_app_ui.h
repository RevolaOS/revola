/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/include/game_ui.h by lgreitis. */

#ifndef RV_APP_UI_H
#define RV_APP_UI_H

#include "rv_app.h"

#ifdef __cplusplus
extern "C" {
#endif

struct rv_app_menu_item {
    const char *label;
    const char *value;
};

struct rv_app_menu {
    const char *title;
    const struct rv_app_menu_item *items;
    unsigned count;
    unsigned selected;
};

enum rv_app_menu_action {
    RV_APP_MENU_NONE,
    RV_APP_MENU_SELECT,
    RV_APP_MENU_BACK,
};

enum rv_app_menu_action rv_app_menu_input(struct rv_app_menu *menu,
                                          const struct rv_app_input_event *event);
void rv_app_menu_scroll(struct rv_app_menu *menu, const struct rv_app_wheel *wheel);
void rv_app_menu_draw(const struct rv_app_menu *menu, const struct rv_app_framebuffer *fb);
void rv_app_clear(const struct rv_app_framebuffer *fb, uint16_t color);
/* A filled rectangle, clipped to the framebuffer. */
void rv_app_fill_rect(const struct rv_app_framebuffer *fb, int x, int y, int width, int height,
                      uint16_t color);

/* The files with an extension under a folder of the package ("" for its root)
 * and its subfolders, sorted by name; the extension's case does not matter and
 * hidden paths are skipped. Open returns 0, -1 on failure, -2 for more than 64
 * files. The picker owns its labels and paths until it is closed. Zero it first;
 * close it before opening it again. */
struct rv_app_file_picker {
    struct rv_app_menu menu;
    struct rv_app_menu_item items[64];
};

int rv_app_file_picker_open(struct rv_app_file_picker *picker, const char *title,
                            const char *directory, const char *extension);
void rv_app_file_picker_close(struct rv_app_file_picker *picker);

#ifdef __cplusplus
}
#endif
#endif
