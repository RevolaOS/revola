/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_USAGE_H
#define CORE_USAGE_H

/* Registers the Usage page of Settings > Extensions. */
void core_usage_register(void);
/* "3 active": the extensions running, for the Usage row; returns `out`. */
const char *core_usage_active(char *out, const char *end);

#endif
