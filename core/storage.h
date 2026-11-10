/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_STORAGE_H
#define CORE_STORAGE_H
#include <stdint.h>

/* 1 for a flash adapter, 0 for a hard disk or when `identify` (the 256 words of
 * ATA IDENTIFY, ATA_Identify) is empty. */
int core_storage_is_flash(const uint16_t *identify);

#endif
