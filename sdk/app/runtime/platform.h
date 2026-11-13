/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef RV_APP_PLATFORM_H
#define RV_APP_PLATFORM_H

#include "rv_app.h"

/* The path Apple's legacy file API takes: a volume and UTF-8 text. */
struct rv_app_path {
    uint16_t volume;
    char text[258];
};

struct rv_app_eapp_frame_input;
void *rv_app_platform_begin_frame(const struct rv_app_eapp_frame_input *input,
                                  struct rv_app_framebuffer *framebuffer);
void rv_app_platform_present(void *surface);
void rv_app_files_close_all(void);
void rv_app_directories_close_all(void);
/* RV_APP_ROOT, the package id and `name` into `path`; -1 when the name leaves
 * the package, "" is accepted only with allow_root. */
int rv_app_make_path(struct rv_app_path *path, const char *name, int allow_root);

#endif
