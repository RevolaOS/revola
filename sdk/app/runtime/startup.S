/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/startup.S by lgreitis. */

.syntax unified
.cpu arm926ej-s
.arm

.section .text.startup,"ax",%progbits
.balign 4
.global rv_app_start
.type rv_app_start,%function
rv_app_start:
    /* The host owns the task and its stack; only bss needs clearing. */
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
1:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 1b
    b rv_app_runtime_init
.size rv_app_start, .-rv_app_start
.ltorg

.global rv_app_reserved_entry
.type rv_app_reserved_entry,%function
rv_app_reserved_entry:
    mov r0, #0
    bx lr
.size rv_app_reserved_entry, .-rv_app_reserved_entry

.section .note.GNU-stack,"",%progbits
