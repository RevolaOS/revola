/* SPDX-License-Identifier: GPL-3.0-only */

/* One native UI timer drives the core's grace point: Apple's event timers post a
 * timer event to the queue of the task that armed them and deliver it through
 * the recipient's vtable, so the handler runs on the UI task like every other
 * event. The core's recipient is a bare object whose vtable has only the event
 * slot. Each tick re-arms the timer (Timer_Start arms once). */

#include "loader/loader.h"
#include "osos-2.0.5.h"
#include "settings.h"
#include "hooks/sites.h"
#include "timer.h"
#include <stddef.h>

#define TICK_MS 1000

static uint32_t timer[EventTimer_bytes / 4];
static void *vtable[(EventRecipient_event_method / 4) + 1];
static struct {
    void *const *vtable;
} recipient;

uint32_t core_timer_ticks;
void core_selftest_tick(void);

/* Extension timers: each is its own recipient, a native timer and the
 * callback. A stopped timer stays allocated for two grace ticks, because an
 * event it posted before the stop may still be in the queue. */
struct rv_timer {
    void *const *vtable; /* the recipient's: only the event slot */
    uint32_t timer[EventTimer_bytes / 4];
    struct rv_timer *next;
    struct rv_extension_record *owner;
    void (*callback)(void *arg); /* NULL once stopped */
    void *arg;
    uint8_t repeat;
    uint8_t retiring; /* grace ticks left before it is freed, once stopped */
};

static struct rv_timer *timers;
static void *timer_vtable[(EventRecipient_event_method / 4) + 1];

static int on_timer(struct rv_timer *t, uint8_t *event)
{
    if (*(uint32_t *)(event + Event_kind) != Event_Timer)
        return 0;
    void (*callback)(void *) = t->callback;
    if (callback && t->repeat) {
        Timer_Start(t->timer);
    } else if (!t->retiring) {
        t->callback = NULL;
        t->retiring = 2;
    }
    if (callback)
        callback(t->arg);
    return 1;
}

struct rv_timer *core_timer_start(struct rv_extension_record *owner, uint32_t ms, int repeat,
                                  void (*callback)(void *arg), void *arg)
{
    if (!callback || !ms)
        return NULL;
    struct rv_timer *t = core_heap_allocate(owner, sizeof *t);
    if (!t)
        return NULL;
    timer_vtable[EventRecipient_event_method / 4] = (void *)on_timer;
    t->vtable = timer_vtable;
    t->owner = owner;
    t->callback = callback;
    t->arg = arg;
    t->repeat = repeat != 0;
    t->next = timers;
    timers = t;
    UI_InitializeEventTimer(t, t->timer, ms, 0);
    Timer_SetPeriod(t->timer, ms);
    Timer_Start(t->timer);
    return t;
}

void core_timer_stop(struct rv_extension_record *owner, struct rv_timer *timer)
{
    for (struct rv_timer *t = timers; t; t = t->next)
        if (t == timer && t->owner == owner && !t->retiring) {
            Timer_Stop(t->timer);
            t->callback = NULL;
            t->retiring = 2;
        }
}

void core_timer_stop_code(struct rv_extension_record *owner, const void *start, const void *end)
{
    for (struct rv_timer *t = timers; t; t = t->next)
        if (t->owner == owner && (!start || ((const void *)t->callback >= start &&
                                             (const void *)t->callback < end)))
            core_timer_stop(owner, t);
}

static void free_stopped(void)
{
    for (struct rv_timer **link = &timers; *link;) {
        struct rv_timer *t = *link;
        if (t->retiring && --t->retiring == 0) {
            *link = t->next;
            core_heap_free(t->owner, t, sizeof *t);
        } else {
            link = &t->next;
        }
    }
}

static int on_event(void *self, uint8_t *event)
{
    (void)self;
    if (*(uint32_t *)(event + Event_kind) != Event_Timer)
        return 0;
    core_timer_ticks++;
    free_stopped();
    core_grace_tick();
    core_loader_tick();
    core_settings_tick();
#ifdef CORE_SELFTEST
    core_selftest_tick();
#endif
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
