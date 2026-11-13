/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/input.c by lgreitis. */

#include "osos-2.0.5.h"
#include "rv_app.h"

struct rv_app_wheel rv_app_read_wheel(void)
{
    int32_t delta = 0;
    uint32_t flags = 0;

    GameInput_ReadWheel(&delta, &flags);

    return (struct rv_app_wheel){
        .delta = delta,
        .position = flags & 0xff,
        .touched = (flags & WheelTouch_Down) != 0,
    };
}

int rv_app_hold_locked(void)
{
    uint8_t locked = 0;
    uint32_t size = sizeof locked;
    return GameSettings_GetValue("HoldSwitch", &locked, &size) == 0 && locked != 0;
}
