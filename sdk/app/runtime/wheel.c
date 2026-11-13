/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/wheel.c by lgreitis. */

#include "rv_app.h"

static const uint8_t directions[8] = {
    RV_APP_DIRECTION_UP,    RV_APP_DIRECTION_UP | RV_APP_DIRECTION_RIGHT,
    RV_APP_DIRECTION_RIGHT, RV_APP_DIRECTION_RIGHT | RV_APP_DIRECTION_DOWN,
    RV_APP_DIRECTION_DOWN,  RV_APP_DIRECTION_DOWN | RV_APP_DIRECTION_LEFT,
    RV_APP_DIRECTION_LEFT,  RV_APP_DIRECTION_LEFT | RV_APP_DIRECTION_UP,
};

uint8_t rv_app_wheel_directions(const struct rv_app_wheel *wheel, uint8_t top_position)
{
    if (!wheel || !wheel->touched)
        return 0;
    unsigned position = (uint8_t)(top_position - wheel->position + 16);
    return directions[position / 32];
}
