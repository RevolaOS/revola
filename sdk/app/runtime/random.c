/* SPDX-License-Identifier: GPL-3.0-only */

#include "rv_app.h"

/* xorshift32 never leaves zero, so zero stands in for the default seed. */
#define DEFAULT_SEED 0x2545f491u

static uint32_t state = DEFAULT_SEED;

void rv_app_random_seed(uint32_t seed)
{
    state = seed ? seed : DEFAULT_SEED;
}

uint32_t rv_app_random(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
