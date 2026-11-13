/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/include/game_libc.h by lgreitis. */

#ifndef RV_APP_LIBC_H
#define RV_APP_LIBC_H

#include <stddef.h>

/* An app that links runtime/newlib_glue.c supplies these. The exit hook must
 * unwind to the current app callback: the host owns the task and its stack. */
void rv_app_libc_write(const char *text, size_t size);
_Noreturn void rv_app_libc_exit(int status);

#endif
