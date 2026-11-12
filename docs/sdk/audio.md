# Audio effects

An audio effect filters the output PCM at one core site, `RV_SITE_AudioOutputQueue_Get_call`, after Apple's EQ. It applies to everything the iPod plays. Bind with `RV_HOOK_FILTER_AT` and the priority of the effect's stage, so the order is fixed whichever effects are enabled.

| Stage | Priority |
|---|---|
| `RV_AUDIO_STAGE_CROSSFEED` | 100 |
| `RV_AUDIO_STAGE_TONE` | 200 |
| `RV_AUDIO_STAGE_BASS_ENHANCEMENT` | 300 |
| `RV_AUDIO_STAGE_FATIGUE_REDUCTION` | 400 |
| `RV_AUDIO_STAGE_SURROUND` | 500 |
| `RV_AUDIO_STAGE_CHANNELS` | 600 |
| `RV_AUDIO_STAGE_COMPRESSOR` | 700 |
| `RV_AUDIO_STAGE_DITHER` | 900 |

```c
static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER_AT(RV_SITE_AudioOutputQueue_Get_call, on_output, RV_AUDIO_STAGE_COMPRESSOR),
};
```

The stream is 16-bit stereo, which is everything the iPod plays, at the output sample rate of 8 to 48 kHz. An effect whose mode is Off should pass the buffer through and cost nothing per sample. Loops that run per sample are built as ARM: a 32 by 32 to 64 bit multiply is one `smull` there and a library call in Thumb; see [Modules](modules.md).

## Shared helpers

`rv_audio.h` holds what the effects have in common, as inline functions.

| Call | |
|---|---|
| `rv_audio_stereo16(context, &rate)` | 1 when the output behind the site's third argument is 16-bit stereo, with `rate` its sample rate |
| `rv_fracmul(a, b)` | a times b, both Q31 |
| `rv_saturate16(sample)` | clamped to S16 |
| `struct rv_handoff`, `rv_handoff_publish(api, &handoff, &next, set, bytes)`, `rv_handoff_pending(&handoff)`, `rv_handoff_take(&handoff, &playing, &next, bytes)` | a set of values the UI task leaves for the pump task: `publish` copies it into `next` with interrupts masked and moves the generation; the pump task calls `take` when `pending` says the generation moved, which copies `next` into `playing` and again when it moved under the copy |

```c
static uint32_t on_output(uint32_t samples, uint32_t frames, uint32_t context, const void *next)
{
    uint32_t rate;
    if (frames && rv_audio_stereo16((const void *)context, &rate))
        effect_process((int16_t *)samples, frames, rate);
    return api->hook_next(next, samples, frames, context, 0, 0, 0);
}
```

Filter designs and the curves an editor draws use `rv_math.h`: `rv_exp`, `rv_log`, `rv_sin_cos`, `rv_sqrt` and `rv_decibels` in double precision without a math library, for the UI task only.
