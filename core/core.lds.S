/* SPDX-License-Identifier: GPL-3.0-only */

#include "layout.h"
#include "osos-2.0.5.h"

SECTIONS {
    . = CORE_BASE;
    __core_start = .;
    __core_file_offset = CORE_FILE_OFFSET;
    __osos_load_offset = OSOS_RUNTIME_FILE_OFFSET;
    .text : { *(.text*) *(.rodata*) }
    .data : { *(.data*) }
    . = ALIGN(4);
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }
    __core_end = .;
    __core_limit = ALIGN(0x1000);
    ASSERT(__core_limit <= CORE_BASE + CORE_BYTES, "Core exceeds reserved memory")
    .core.patch 0 (INFO) : { *(.core.patch) }
    /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) }
}
