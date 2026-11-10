/* SPDX-License-Identifier: GPL-3.0-only */

/* Numbers and sizes as the core's pages show them; also api->format_number and
 * api->format_size. */

#include "ui/format.h"
#include "ui/resources.h"

char *core_format_number(char *out, const char *end, uint32_t value)
{
    char digits[14];
    int n = 0;
    do {
        if (n && n % 4 == 3)
            digits[n++] = ',';
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n && out < end - 1)
        *out++ = digits[--n];
    *out = 0;
    return out;
}

char *core_format_size(char *out, const char *end, uint32_t bytes)
{
    int mega = bytes >= 1024 * 1024;
    uint32_t unit = mega ? 1024 * 1024 : 1024;
    uint32_t tenths = (bytes * 10 + unit / 2) / unit; /* sizes below 400 MB */
    out = core_format_number(out, end, tenths / 10);
    char fraction[] = {'.', (char)('0' + tenths % 10), ' ', mega ? 'M' : 'K', 'B', 0};
    return core_put_text(out, end, fraction);
}
