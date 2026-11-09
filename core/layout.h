/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/* The core sits above Apple's BSS; the heap starts at the linked end (__core_limit).
 * CORE_BYTES is the link-time budget (plan: 96 KiB) and the OSOS image growth. */
#define CORE_BASE 0x08b3c000
#define CORE_BYTES 0x00018000
#define CORE_FILE_OFFSET 0x00a24500
