/* SPDX-License-Identifier: GPL-3.0-only */

/* One native UI timer drives the core's grace point: Apple's event timers post a
 * timer event to the queue of the task that armed them and deliver it through
 * the recipient's vtable, so the handler runs on the UI task like every other
 * event. The core's recipient is a bare object whose vtable has only the event
 * slot. Each tick re-arms the timer (Timer_Start arms once). */

#include "osos-2.0.5.h"
#include "settings.h"
#include "sites.h"
#include "timer.h"

#define TICK_MS 1000

static uint32_t timer[EventTimer_bytes / 4];
static void *vtable[(EventRecipient_event_method / 4) + 1];
static struct {
    void *const *vtable;
} recipient;

uint32_t core_timer_ticks;

static int on_event(void *self, uint8_t *event)
{
    (void)self;
    if (*(uint32_t *)(event + Event_kind) != Event_Timer)
        return 0;
    core_timer_ticks++;
    core_grace_tick();
    core_settings_tick();
    Timer_Start(timer);
    return 1;
}

/* From UI-task code: the timer takes the calling task's event queue. */
void core_timer_init(void)
{
    vtable[EventRecipient_event_method / 4] = (void *)on_event;
    recipient.vtable = vtable;
    UI_InitializeEventTimer(&recipient, timer, TICK_MS, 0);
    Timer_SetPeriod(timer, TICK_MS);
    Timer_Start(timer);
}
