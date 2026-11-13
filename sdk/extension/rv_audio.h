/* SPDX-License-Identifier: GPL-3.0-only */

/* What an audio effect shares with the others: the output format behind the
 * PCM site, the fixed-point steps of a sample loop, the handoff of a set of
 * values from the task that chose them to the pump task that plays them, and
 * the fade from the state that played to the one that takes over. */

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

/* A state that takes over at a buffer edge steps the output, which clicks.
 * An effect keeps the state that played as a second instance and fades its
 * output into the new one's over RV_FADE_FRAMES frames, in chunks of
 * RV_FADE_CHUNK_FRAMES. `lead` frames before the fade play the old state alone
 * while both run, for a new state that must fill (a delay line) before it is
 * heard. A new fade restarts from what plays; `remaining = 0` cancels one. */
enum { RV_FADE_SHIFT = 7, RV_FADE_FRAMES = 1 << RV_FADE_SHIFT, RV_FADE_CHUNK_FRAMES = 32 };

struct rv_fade {
    uint32_t remaining; /* lead frames at weight 0, then RV_FADE_FRAMES of blend */
};

/* Runs `state` on `frames` interleaved stereo S16 frames in place. */
typedef void (*rv_fade_run_fn)(void *state, int16_t *samples, uint32_t frames);

static inline void rv_fade_start(struct rv_fade *fade, uint32_t lead)
{
    fade->remaining = RV_FADE_FRAMES + lead;
}

/* Runs `playing` over the buffer; while a fade lasts, `fading` too, and mixes
 * the two. The weight runs 1 to RV_FADE_FRAMES so the last faded frame is the
 * new state alone and joins the rest without a step. */
static inline void rv_fade_run(struct rv_fade *fade, rv_fade_run_fn run, void *playing,
                               void *fading, int16_t *samples, uint32_t frames)
{
    while (fade->remaining && frames) {
        int16_t previous[RV_FADE_CHUNK_FRAMES * 2];
        uint32_t chunk = frames < RV_FADE_CHUNK_FRAMES ? frames : RV_FADE_CHUNK_FRAMES;
        if (chunk > fade->remaining)
            chunk = fade->remaining;
        memcpy(previous, samples, chunk * 2 * sizeof *samples);
        run(fading, previous, chunk);
        run(playing, samples, chunk);
        for (uint32_t i = 0; i < chunk * 2; i += 2) {
            int32_t weight = fade->remaining > RV_FADE_FRAMES
                                 ? 0
                                 : (int32_t)(RV_FADE_FRAMES + 1 - fade->remaining);
            for (uint32_t n = i; n < i + 2; n++)
                samples[n] = (int16_t)((previous[n] * (RV_FADE_FRAMES - weight) +
                                        samples[n] * weight) >>
                                       RV_FADE_SHIFT);
            fade->remaining--;
        }
        frames -= chunk;
        samples += chunk * 2;
    }
    run(playing, samples, frames);
}

#endif
