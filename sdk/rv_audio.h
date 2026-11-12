/* SPDX-License-Identifier: GPL-3.0-only */

/* What an audio effect shares with the others: the output format behind the
 * PCM site, the fixed-point steps of a sample loop, and the handoff of a set
 * of values from the task that chose them to the pump task that plays them. */

#ifndef RV_AUDIO_H
#define RV_AUDIO_H
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_libc.h"

/* The output of the AudioEffectsContext a filter on
 * RV_SITE_AudioOutputQueue_Get_call receives as its third argument: 1 when
 * it is 16-bit stereo, with *rate its sample rate. */
static inline int rv_audio_stereo16(const void *context, uint32_t *rate)
{
    *rate = RV_FIELD(context, AudioEffectsContext_output_rate, uint32_t);
    return RV_FIELD(context, AudioEffectsContext_output_bits, uint32_t) == 16 &&
           RV_FIELD(context, AudioEffectsContext_output_channels, uint32_t) == 2;
}

/* a times b, both Q31. One smull in ARM, a library call in Thumb. */
static inline int32_t rv_fracmul(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 31);
}

static inline int16_t rv_saturate16(int32_t sample)
{
    return sample > 32767 ? 32767 : sample < -32768 ? -32768 : (int16_t)sample;
}

/* A set of values left for the pump task. The task that chose them copies
 * them into `next` with interrupts masked and moves the generation; the pump
 * task, which alone reads `taken`, copies `next` into `playing` once it sees
 * the generation move, and again when it moved under the copy. */
struct rv_handoff {
    volatile uint32_t generation;
    uint32_t taken;
};

static inline void rv_handoff_publish(const struct rv_api *api, struct rv_handoff *handoff,
                                      void *next, const void *set, uint32_t bytes)
{
    uint32_t state = api->irq_save();
    memcpy(next, set, bytes);
    handoff->generation++;
    api->irq_restore(state);
}

static inline int rv_handoff_pending(const struct rv_handoff *handoff)
{
    return handoff->taken != handoff->generation;
}

static inline void rv_handoff_take(struct rv_handoff *handoff, void *playing, const void *next,
                                   uint32_t bytes)
{
    uint32_t seen;
    do {
        seen = handoff->generation;
        memcpy(playing, next, bytes);
    } while (seen != handoff->generation);
    handoff->taken = seen;
}

#endif
