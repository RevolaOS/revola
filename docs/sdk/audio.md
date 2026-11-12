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
