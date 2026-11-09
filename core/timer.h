/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_TIMER_H
#define CORE_TIMER_H
#include <stdint.h>

/* Arms the one-second UI timer that is the core's grace point. */
void core_timer_init(void);
extern uint32_t core_timer_ticks;

#endif
