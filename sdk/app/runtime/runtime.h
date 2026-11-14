/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef RV_APP_RUNTIME_H
#define RV_APP_RUNTIME_H

#include "rv_app_abi.h"

/* The entries the eApp header names: init (after startup.S cleared bss),
 * shutdown and frame. */
void rv_app_runtime_init(void);
void rv_app_runtime_shutdown(void);
void rv_app_runtime_frame(const struct rv_app_eapp_frame_input *input,
                          struct rv_app_eapp_frame_output *output);
/* One of a frame's events: to the app's input callback, then a music
 * button's release to the host unless the app handles those. */
void rv_app_runtime_input(const struct rv_app_eapp_input_event *native);

#endif
