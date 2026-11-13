/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/time.c by lgreitis. */

#include "osos-2.0.5.h"
#include "rv_app.h"

static uint32_t last_us, elapsed_ms, remainder_us;
static int clock_started;

uint32_t rv_app_ticks_ms(void)
{
    uint32_t now = Clock_GetMicroseconds();
    if (!clock_started) {
        last_us = now;
        clock_started = 1;
    }

    uint32_t delta = now - last_us;
    last_us = now;
    elapsed_ms += delta / 1000;
    remainder_us += delta % 1000;
    elapsed_ms += remainder_us / 1000;
    remainder_us %= 1000;

    return elapsed_ms;
}

void rv_app_sleep_ms(uint32_t milliseconds)
{
    uint32_t start = rv_app_ticks_ms();

    do {
        Thread_Yield();
    } while (rv_app_ticks_ms() - start < milliseconds);
}
