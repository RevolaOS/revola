/* SPDX-License-Identifier: GPL-3.0-only */

/* The C library of an extension: rv.mk links rv_libc.c into every module and
 * drops what a module does not call. GCC emits calls to memcpy, memmove,
 * memset and memcmp for struct copies and initialisers even in freestanding
 * code, so those four are always available under their standard names. */

#ifndef RV_LIBC_H
#define RV_LIBC_H
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *to, const void *from, size_t bytes);
void *memmove(void *to, const void *from, size_t bytes);
void *memset(void *to, int value, size_t bytes);
int memcmp(const void *a, const void *b, size_t bytes);
size_t strlen(const char *text);
int strcmp(const char *a, const char *b);

/* printf into `out`, NUL-terminated within `capacity`, cut to fit; the length
 * written. Conversions: %d %i %u %x %X %c %s %%, with the flags - and 0, a
 * width and, for %s, a precision (either may be *). int and long are both 32
 * bits; uint32_t is unsigned long here, so it takes %lu or %lx. Anything else
 * is copied as written. */
uint32_t rv_format(char *out, uint32_t capacity, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
uint32_t rv_vformat(char *out, uint32_t capacity, const char *format, va_list args);

/* A duration as a track time: "3:07" under an hour, "1:02:03" from one. */
uint32_t rv_format_duration(char *out, uint32_t capacity, uint32_t seconds);

#endif
