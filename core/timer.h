/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CORE_TIMER_H
#define CORE_TIMER_H
#include <stdint.h>

struct rv_extension_record;
struct rv_timer;

/* Arms the one-second UI timer that is the core's grace point. */
void core_timer_init(void);
extern uint32_t core_timer_ticks;

/* api->timer_start and timer_stop: `callback(arg)` on the UI task after `ms`,
 * again every `ms` when `repeat`. Callers are on the UI task. */
struct rv_timer *core_timer_start(struct rv_extension_record *owner, uint32_t ms, int repeat,
                                  void (*callback)(void *arg), void *arg);
void core_timer_stop(struct rv_extension_record *owner, struct rv_timer *timer);
/* Stops the owner's timers whose callback lies in [start, end), or all of
 * them when `start` is NULL: before a module's image goes. */
void core_timer_stop_code(struct rv_extension_record *owner, const void *start, const void *end);

#endif
