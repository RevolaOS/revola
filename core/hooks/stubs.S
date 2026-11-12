/* SPDX-License-Identifier: GPL-3.0-only */

/* Per-site stubs generated from sites.def. A patched Apple site reaches its stub,
 * which hands the site's root continuation to whatever the slot holds: the single
 * handler, a chain entry, or the original. Call and vtable stubs are the BL target
 * and the vtable pointer; entry stubs are the target of the two-word jump written
 * at the Apple function, and the replay stub runs the displaced words. */

#include "osos-2.0.5.h"

.syntax unified
.cpu arm926ej-s
.arm

#define CALL 0
#define VTABLE 1
#define ENTRY 2
#define ENTRY_NORETURN 3
#define WORD 4
#define INSERT 5
#define BL(address, callee) 0
#define REPLAY(name) 0
#define STACK(words) ((words) << 4)
#define COPY(argument, reg) ((reg) << (4 * (argument)))

.set index, 0

.macro next_in_register count, root
.if \count == 0
    ldr r0, =\root
.elseif \count == 1
    ldr r1, =\root
.elseif \count == 2
    ldr r2, =\root
.else
    ldr r3, =\root
.endif
.endm

/* Each nibble of `copy` names a register moved into r0, r1, r2 or r3 first, as an
 * assembled mov since the register is an expression. next in r<argc> for up to three
 * arguments; a frame pushes it as the first stack argument for four, below copies of
 * Apple's own stack arguments (one or two), with the pushed registers chosen to keep
 * sp 8-byte aligned at the call. */
.macro site_stub name, kind, address, word0, word1, args, copy
.set argc, \args & 15
.set stack, \args >> 4
.if \kind == WORD && \copy
.error "\name: WORD site cannot copy"
.endif
.if \kind != WORD
.text
.balign 4
core_stub_\name:
.if \kind == INSERT
    push {r0-r3, r12, lr}
.endif
.set k, 0
.rept 4
.set reg, ((\copy) >> (4 * k)) & 15
.if reg
.if reg < 4 || reg > 11
.error "\name: COPY register must be r4-r11"
.endif
.if k >= argc
.error "\name: COPY argument beyond argc"
.endif
    .word 0xe1a00000 | (k << 12) | reg /* mov r<k>, r<reg> */
.endif
.set k, k + 1
.endr
.if \kind == INSERT
    next_in_register argc, core_root_\name
    ldr r12, =core_site_slots + 4 * index
    ldr r12, [r12]
    blx r12
    pop {r0-r3, r12, pc}
.elseif argc < 4
    next_in_register argc, core_root_\name
    ldr r12, =core_site_slots + 4 * index
    ldr pc, [r12]
.elseif stack == 0
    push {r4, r5, lr}
    ldr r4, =core_root_\name
    push {r4}
    ldr r12, =core_site_slots + 4 * index
    ldr r12, [r12]
    blx r12
    add sp, sp, #4
    pop {r4, r5, pc}
.elseif stack == 1
    push {r4, r5, r6, lr}
    ldr r4, [sp, #16]
    ldr r5, =core_root_\name
    push {r4, r5}
    ldr r12, =core_site_slots + 4 * index
    ldr r12, [r12]
    blx r12
    add sp, sp, #8
    pop {r4, r5, r6, pc}
.else
    push {r4, r5, lr}
    ldr r4, [sp, #12]
    ldr r5, [sp, #16]
    push {r4, r5}
    ldr r4, =core_root_\name
    push {r4}
    ldr r12, =core_site_slots + 4 * index
    ldr r12, [r12]
    blx r12
    add sp, sp, #12
    pop {r4, r5, pc}
.endif
.ltorg
.if \kind == ENTRY
.global core_replay_\name
core_replay_\name:
    .word \word0
    .word \word1
    ldr pc, [pc, #-4]
    .word \address + 8
.endif
.section .rodata
.balign 4
core_root_\name:
    .short index
    .short 0xffff
.endif
.set index, index + 1
.endm

#define SITE(name, address, kind, original, word0, word1, chain, argc, copy)                    \
    site_stub name, kind, address, word0, word1, argc, copy
#include "hooks/sites.def"
#undef SITE

/* Stub addresses by site index, zero for word sites. */
.section .rodata
.balign 4
.global core_site_stubs
core_site_stubs:
.macro site_entry name, kind
.if \kind == WORD
    .word 0
.else
    .word core_stub_\name
.endif
.endm
#define SITE(name, address, kind, original, word0, word1, chain, argc, copy) site_entry name, kind
#include "hooks/sites.def"
#undef SITE

/* Site ids in table order: FNV-1a of the row name, written by the build. */
.section .rodata
.balign 4
.global core_site_ids
core_site_ids:
#include "site_ids.inc"

.bss
.balign 4
.global core_site_slots
core_site_slots:
    .space 4 * index
