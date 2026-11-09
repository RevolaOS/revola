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
#define BL(address, callee) 0
#define REPLAY(name) 0

.set index, 0

/* next in r<argc> for up to three arguments; a frame pushes it as the first stack
 * argument for four (push {r4, r5, lr} plus the word keeps sp 8-byte aligned). */
.macro site_stub name, kind, address, word0, word1, argc, copy
.if \kind != WORD
.text
.balign 4
core_stub_\name:
.if \copy
    mov r(\argc - 1), r\copy
.endif
.if \argc < 4
    ldr r\argc, =core_root_\name
    ldr r12, =core_site_slots + 4 * index
    ldr pc, [r12]
.else
    push {r4, r5, lr}
    ldr r4, =core_root_\name
    push {r4}
    ldr r12, =core_site_slots + 4 * index
    ldr r12, [r12]
    blx r12
    add sp, sp, #4
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
#include "sites.def"
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
#include "sites.def"
#undef SITE

.bss
.balign 4
.global core_site_slots
core_site_slots:
    .space 4 * index
