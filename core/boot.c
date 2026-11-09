/* SPDX-License-Identifier: GPL-3.0-only */

/* Install-time patches: everything that must hold before the core runs. */

#include "osos-2.0.5.h"
#include "patch.h"

extern char __core_limit[];

/* Apple's heap begins where the core ends. */
PATCH_POINTER(SITE_HeapRegionBase_literal, OSOS_NATIVE_HEAP_BASE, __core_limit);
PATCH_POINTER(SITE_RuntimeHeapBase_literal, OSOS_NATIVE_HEAP_BASE, __core_limit);

/* Exceptions and aborts render the panic screen instead of rebooting silently. */
PATCH_JUMP(SITE_Panic_entry, 0xe59f1010, 0xe3a00004, core_panic_entry);
PATCH_JUMP(SITE_Abort_entry, 0xe3a01000, 0xe92d4010, core_abort_entry);

/* The one code patch the recipe writes: Apple's resource override init is the
 * earliest point the core can run, so it bootstraps every runtime-written site. */
PATCH_CALL(SITE_ResourceBank_InitOverrides_call, ADDR_ResourceBank_InitOverrides, core_boot);
