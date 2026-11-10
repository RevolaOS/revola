/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_FORMAT_H
#define CORE_FORMAT_H
#include <stdint.h>

/* Text into out..end, NUL-terminated; each returns the end. A number with
 * thousands separators; bytes as KB or MB with one decimal. */
char *core_format_number(char *out, const char *end, uint32_t value);
char *core_format_size(char *out, const char *end, uint32_t bytes);

#endif
