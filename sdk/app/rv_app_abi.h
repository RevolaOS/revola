/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/include/osos_game_abi.h by lgreitis. */

#ifndef RV_APP_ABI_H
#define RV_APP_ABI_H

#include "osos-2.0.5.h"
#include <stddef.h>
#include <stdint.h>

/* What Apple's eApp host reads from an image and hands to its frame entry. The
 * values are the symbols header's; the structs give them C names. */

enum rv_app_eapp_slot {
    RV_APP_EAPP_SLOT_INIT = 0,
    RV_APP_EAPP_SLOT_SHUTDOWN = 1,
    RV_APP_EAPP_SLOT_FRAME = 4,
    RV_APP_EAPP_SLOT_COUNT = 5,
};

struct rv_app_eapp_header {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t reserved;
    uint32_t imports;
    uint32_t entries[RV_APP_EAPP_SLOT_COUNT];
};

struct rv_app_eapp_input_event {
    uint8_t button;
    uint8_t type;
    uint8_t reserved[2];
    uint32_t value;
    uint32_t next;
};

struct rv_app_eapp_frame_input {
    uint8_t state;
    uint8_t reserved_01[EAppFrameInput_events - 1];
    uint32_t events;
    uint8_t reserved_34[EAppFrameInput_pixels - EAppFrameInput_events - 4];
    uint32_t pixels;
    uint8_t reserved_3c[EAppFrameInput_bytes - EAppFrameInput_pixels - 4];
};

struct rv_app_eapp_frame_output {
    uint8_t state;
    uint8_t reserved[EAppFrameOutput_bytes - 1];
};

_Static_assert(sizeof(struct rv_app_eapp_header) == EAppHeader_bytes, "eapp header");
_Static_assert(offsetof(struct rv_app_eapp_header, imports) == EAppHeader_imports,
               "eapp imports");
_Static_assert(offsetof(struct rv_app_eapp_header, entries) == EAppHeader_entries,
               "eapp entries");
_Static_assert(sizeof(struct rv_app_eapp_input_event) == EAppInputEvent_bytes, "event");
_Static_assert(offsetof(struct rv_app_eapp_input_event, value) == EAppInputEvent_value,
               "event value");
_Static_assert(offsetof(struct rv_app_eapp_input_event, next) == EAppInputEvent_next,
               "event next");
_Static_assert(sizeof(struct rv_app_eapp_frame_input) == EAppFrameInput_bytes, "frame in");
_Static_assert(sizeof(struct rv_app_eapp_frame_output) == EAppFrameOutput_bytes,
               "frame out");

#endif
