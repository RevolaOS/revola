/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef RV_APP_PLATFORM_H
#define RV_APP_PLATFORM_H

#include "rv_app.h"
#include "rv_app_abi.h"

/* memory.c's, for a build without a C library. */
void *memcpy(void *destination, const void *source, size_t bytes);
void *memset(void *destination, int value, size_t bytes);

void *rv_app_platform_begin_frame(const struct rv_app_eapp_frame_input *input,
                                  struct rv_app_framebuffer *framebuffer);
void rv_app_platform_present(void *surface);
void rv_app_files_close_all(void);
void rv_app_directories_close_all(void);

/* `name` without leading "./", or NULL when it leaves its folder: a leading
 * slash, an empty, "." or ".." component, a trailing slash, a backslash or a
 * colon. "" is accepted only with allow_root. */
const char *rv_app_check_name(const char *name, int allow_root);

/* A request of `type` for the host's file worker, with the outputs preset to
 * RV_APP_REQUEST_PENDING so a wait can see them arrive. The request is spent
 * once finished. */
#define RV_APP_REQUEST_PENDING 0xffffffffu
struct GameFileRequest *rv_app_request_new(uint32_t type);
/* Waits for the request's result when `queued` (what the host's Queue entry
 * returned) is nonzero and then, on success, for every output in `outputs`,
 * all the fields the operation writes; returns the result, or
 * FileStatus_NoSlot when the request was not queued. After a failure with
 * RV_APP_REQUEST_HANDLE among the outputs, a handle other than
 * RV_APP_REQUEST_PENDING is one the host registered anyway and the caller
 * closes or releases. */
enum rv_app_request_output {
    RV_APP_REQUEST_SIZE = 1,
    RV_APP_REQUEST_HANDLE = 2,
    RV_APP_REQUEST_TRANSFERRED = 4,
};
int rv_app_request_finish(struct GameFileRequest *request, int queued, unsigned outputs);
/* What rv_app_file_error reports; set by a failed request. */
extern uint32_t rv_app_file_error_status;

#endif
