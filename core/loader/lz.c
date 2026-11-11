/* SPDX-License-Identifier: GPL-3.0-only */

/* The lz decoder (format in loader/rvx.h). Matches copy from the output itself,
 * so decoding needs no window beyond `out`. */

#include "loader/lz.h"
#include "loader/file.h"
#include "trace.h"

void core_lz_from_memory(struct core_lz_input *in, const void *stream, uint32_t stored)
{
    in->file = 0;
    in->next = stream;
    in->have = stored;
    in->left = 0;
}

void core_lz_from_file(struct core_lz_input *in, void *file, uint32_t stored)
{
    in->file = file;
    in->have = 0;
    in->left = stored;
}

/* The next stream byte, or -1 at its end or on a read error. */
static int next_byte(struct core_lz_input *in)
{
    if (!in->have) {
        uint32_t n = in->left < sizeof in->chunk ? in->left : sizeof in->chunk, got = 0;
        if (!n || !core_file_read(in->file, in->chunk, n, &got) || got != n)
            return -1;
        in->next = in->chunk;
        in->have = n;
        in->left -= n;
    }
    in->have--;
    return *in->next++;
}

static int decode(struct core_lz_input *in, uint8_t *out, uint32_t bytes)
{
    uint32_t done = 0;
    while (done < bytes) {
        int flags = next_byte(in);
        if (flags < 0)
            return 0;
        for (uint32_t bit = 0; bit < 8 && done < bytes; bit++) {
            int high = next_byte(in);
            if (high < 0)
                return 0;
            if (!(flags >> bit & 1)) {
                out[done++] = (uint8_t)high;
                continue;
            }
            int low = next_byte(in);
            uint32_t length = (uint32_t)(high >> 4) + 3;
            uint32_t distance = ((uint32_t)(high & 15) << 8 | (uint32_t)low) + 1;
            for (int more = high >> 4 == 15 ? 255 : 0; more == 255; length += (uint32_t)more)
                if ((more = next_byte(in)) < 0)
                    return 0;
            if (low < 0 || distance > done || length > bytes - done)
                return 0;
            for (uint32_t end = done + length; done < end; done++)
                out[done] = out[done - distance];
        }
    }
    return !in->have && !in->left;
}

int core_lz_decode(struct core_lz_input *in, uint8_t *out, uint32_t bytes)
{
    core_trace(RV_TRACE_DECODE_BEGIN, bytes);
    int ok = decode(in, out, bytes);
    core_trace(RV_TRACE_DECODE_END, ok);
    return ok;
}
