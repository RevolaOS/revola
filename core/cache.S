/* SPDX-License-Identifier: GPL-3.0-only */

.syntax unified
.cpu arm926ej-s
.arm
.text

/* core_cache_sync(start, end): make code written through the data cache visible
 * to instruction fetch. ARM926EJ-S: clean D-cache lines by address, drain the
 * write buffer, invalidate the whole I-cache. */
.global core_cache_sync
.type core_cache_sync,%function
core_cache_sync:
    bic r0, r0, #31
1:
    mcr p15, 0, r0, c7, c10, 1
    add r0, r0, #32
    cmp r0, r1
    blo 1b
    mov r0, #0
    mcr p15, 0, r0, c7, c10, 4
    mcr p15, 0, r0, c7, c5, 0
    bx lr
.size core_cache_sync, .-core_cache_sync
