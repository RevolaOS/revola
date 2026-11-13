/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/game.lds by lgreitis. */

/* Apple's eApp host maps an image at 0x18000000 and loads at most 1 MiB; code,
 * data and bss share it. The header comes first. */

ENTRY(rv_app_start)

MEMORY
{
    image (rwx) : ORIGIN = 0x18000000, LENGTH = 1M
}

SECTIONS
{
    .eapp :
    {
        KEEP(*(.eapp))
    } > image

    .text :
    {
        *(.text.startup)
        *(.text*)
        *(.rodata*)
    } > image

    .ARM.extab :
    {
        *(.ARM.extab*)
    } > image

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > image

    .data :
    {
        *(.data*)
    } > image

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > image

    ASSERT(SIZEOF(.eapp) == 0x28, "eapp header size")
    ASSERT(__bss_end <= ORIGIN(image) + LENGTH(image), "image and bss exceed 1 MiB")

    /DISCARD/ :
    {
        *(.comment)
        *(.ARM.attributes)
    }
}
