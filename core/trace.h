/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_TRACE_H
#define CORE_TRACE_H
#include "rv_api.h"
#include <stdint.h>

/* Tracepoints: the core reports its own work (rv_trace_kind) to the probe a
 * debugger installed through rv_debug_info; nothing when none is. A tracepoint
 * reads the slot once, and an unloaded module's image is freed a grace tick
 * later, so a probe cleared in its module's unload is never entered after its
 * code is gone. */
extern void (*volatile core_trace_probe)(uint32_t kind, uint32_t argument);

static inline void core_trace(uint32_t kind, uint32_t argument)
{
    void (*probe)(uint32_t, uint32_t) = core_trace_probe;
    if (probe)
        probe(kind, argument);
}

#endif
