/* SPDX-License-Identifier: GPL-3.0-only */

.syntax unified
.cpu arm926ej-s
.arm
.text

/* core_irq_save: mask IRQ and FIQ, return the previous CPSR for core_irq_restore.
 * Single core, in-order memory: masking is the only barrier a publish needs. */
.global core_irq_save
.type core_irq_save,%function
core_irq_save:
    mrs r0, cpsr
    orr r1, r0, #0xc0
    msr cpsr_c, r1
    bx lr
.size core_irq_save, .-core_irq_save

.global core_irq_restore
.type core_irq_restore,%function
core_irq_restore:
    msr cpsr_c, r0
    bx lr
.size core_irq_restore, .-core_irq_restore

/* core_read_ttbr: translation table base register (CP15 c2), not reachable from Thumb. */
.global core_read_ttbr
.type core_read_ttbr,%function
core_read_ttbr:
    mrc p15, 0, r0, c2, c0, 0
    bx lr
.size core_read_ttbr, .-core_read_ttbr
