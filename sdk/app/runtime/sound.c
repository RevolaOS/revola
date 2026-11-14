/* SPDX-License-Identifier: GPL-3.0-only */

/* Sound effects on the host's SoundEffect objects. The host keeps only the
 * pointer to the samples and reads them from a mixer voice while a sound
 * plays, so a block loaded here lives until the sound is freed, and a buffer
 * the app gave stays the app's. The host cannot clear a sound's next, so a
 * freed sound that a living sound still reaches through next links stays
 * with the host, playing when its turn comes, and goes when nothing reaches
 * it any more. Destroying a sound detaches its voices, so a free does not
 * stop first: the host's stop runs along the whole next ring. */

#include "osos-2.0.5.h"
#include "platform.h"

struct rv_app_sound {
    int handle;
    uint8_t *owned; /* The file block of a loaded sound. */
    const struct rv_app_sound *next;
    uint8_t state; /* 0 free, LIVE, or FREED while a sound still names it. */
};

enum { FREE, LIVE, FREED };

_Static_assert(RV_APP_SOUND_VOLUME_MAX == GameSound_VolumeMax, "volume");
_Static_assert(RV_APP_SOUND_PAN_MAX == GameSound_PanMax, "pan");
_Static_assert(RV_APP_SOUND_SPEED_MIN == GameSound_SpeedMin, "speed");
_Static_assert(RV_APP_SOUND_SPEED_MAX == GameSound_SpeedMax, "speed");
_Static_assert(RV_APP_SOUND_RATE_MIN == GameSound_SampleRateMin, "rate");
_Static_assert(RV_APP_SOUND_RATE_MAX == GameSound_SampleRateMax, "rate");

static struct rv_app_sound sounds[32];
#define COUNT (sizeof sounds / sizeof sounds[0])

static int live(const struct rv_app_sound *sound)
{
    return sound && sound->state == LIVE;
}

static int format_ok(uint32_t rate, uint32_t channels, uint32_t bits)
{
    return rate >= RV_APP_SOUND_RATE_MIN && rate <= RV_APP_SOUND_RATE_MAX &&
           (channels == 1 || channels == 2) && (bits == 8 || bits == 16);
}

struct rv_app_sound *rv_app_sound_create(const void *pcm, size_t bytes, uint32_t rate,
                                         uint32_t channels, uint32_t bits)
{
    if (!pcm || !bytes || !format_ok(rate, channels, bits) || bytes % (channels * bits / 8) ||
        bytes > UINT32_MAX)
        return NULL;
    struct rv_app_sound *sound = NULL;
    for (unsigned i = 0; i < COUNT && !sound; ++i)
        if (sounds[i].state == FREE)
            sound = &sounds[i];
    if (!sound)
        return NULL;
    int handle = GameSound_Create();
    if (handle < 0)
        return NULL;
    GameSound_SetData(handle, pcm);
    GameSound_SetByteCount(handle, (uint32_t)bytes);
    GameSound_SetSampleRate(handle, rate);
    GameSound_SetChannels(handle, channels);
    GameSound_SetBitsPerSample(handle, bits);
    sound->handle = handle;
    sound->owned = NULL;
    sound->next = NULL;
    sound->state = LIVE;
    return sound;
}

static uint32_t le16(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
    return le16(p) | le16(p + 2) << 16;
}

static int tag(const uint8_t *p, const char *id)
{
    return p[0] == id[0] && p[1] == id[1] && p[2] == id[2] && p[3] == id[3];
}

/* The PCM of a RIFF WAVE in `file`: the data chunk's bytes, with the format
 * chunk's rate, channels and bits; 0 for anything but plain PCM
 * (WAVE_FORMAT_PCM, 1). 8-bit samples are stored unsigned and flipped to the
 * signed form the host mixes. */
static uint8_t *wav_pcm(uint8_t *file, uint32_t size, uint32_t *bytes, uint32_t *rate,
                        uint32_t *channels, uint32_t *bits)
{
    if (size < 12 || !tag(file, "RIFF") || !tag(file + 8, "WAVE"))
        return NULL;
    uint8_t *pcm = NULL;
    int format = 0;
    *bytes = 0;
    for (uint32_t at = 12; at + 8 <= size;) {
        uint8_t *chunk = file + at + 8;
        uint32_t length = le32(file + at + 4);
        if (length > size - at - 8)
            length = size - at - 8;
        if (tag(file + at, "fmt ") && length >= 16) {
            format = le16(chunk) == 1;
            *channels = le16(chunk + 2);
            *rate = le32(chunk + 4);
            *bits = le16(chunk + 14);
        } else if (tag(file + at, "data")) {
            pcm = chunk;
            *bytes = length;
        }
        at += 8 + length + (length & 1);
    }
    if (!format || !pcm || !*bytes)
        return NULL;
    if (*bits == 8)
        for (uint32_t i = 0; i < *bytes; ++i)
            pcm[i] ^= 0x80;
    return pcm;
}

struct rv_app_sound *rv_app_sound_load(const char *path)
{
    struct rv_app_file *file = rv_app_file_open(path);
    if (!file)
        return NULL;
    uint32_t size = rv_app_file_size(file);
    uint8_t *block = size ? rv_app_alloc(size) : NULL;
    int read = block ? rv_app_file_read(file, block, size) : -1;
    rv_app_file_close(file);
    struct rv_app_sound *sound = NULL;
    if (read == (int)size) {
        uint32_t bytes = 0, rate = 0, channels = 0, bits = 0;
        uint8_t *pcm = wav_pcm(block, size, &bytes, &rate, &channels, &bits);
        if (pcm)
            sound = rv_app_sound_create(pcm, bytes, rate, channels, bits);
    }
    if (sound)
        sound->owned = block;
    else
        rv_app_free(block);
    return sound;
}

static void destroy(struct rv_app_sound *sound)
{
    GameSound_Destroy(sound->handle);
    rv_app_free(sound->owned);
    memset(sound, 0, sizeof *sound);
}

/* Destroys the freed sounds no live sound reaches through next links. */
static void collect(void)
{
    uint8_t reached[COUNT] = {0};
    for (unsigned i = 0; i < COUNT; ++i)
        for (const struct rv_app_sound *s = sounds[i].state == LIVE ? sounds[i].next : NULL;
             s && !reached[s - sounds]; s = s->next)
            reached[s - sounds] = 1;
    for (unsigned i = 0; i < COUNT; ++i)
        if (sounds[i].state == FREED && !reached[i])
            destroy(&sounds[i]);
}

void rv_app_sound_free(struct rv_app_sound *sound)
{
    if (!live(sound))
        return;
    sound->state = FREED;
    collect();
}

void rv_app_sounds_free_all(void)
{
    for (unsigned i = 0; i < COUNT; ++i)
        if (sounds[i].state != FREE)
            GameSound_Stop(sounds[i].handle);
    for (unsigned i = 0; i < COUNT; ++i)
        if (sounds[i].state != FREE)
            destroy(&sounds[i]);
}

void rv_app_sound_play(struct rv_app_sound *sound)
{
    if (!live(sound))
        return;
    GameSound_Stop(sound->handle);
    GameSound_Play(sound->handle);
}

void rv_app_sound_stop(struct rv_app_sound *sound)
{
    if (live(sound))
        GameSound_Stop(sound->handle);
}

void rv_app_sound_pause(struct rv_app_sound *sound)
{
    if (live(sound))
        GameSound_Pause(sound->handle);
}

void rv_app_sound_resume(struct rv_app_sound *sound)
{
    if (live(sound))
        GameSound_Resume(sound->handle);
}

int rv_app_sound_playing(const struct rv_app_sound *sound)
{
    return live(sound) && GameSound_IsPlaying(sound->handle);
}

uint32_t rv_app_sound_position(const struct rv_app_sound *sound)
{
    return live(sound) ? GameSound_GetVoicePosition(sound->handle) : 0;
}

void rv_app_sound_set_volume(struct rv_app_sound *sound, uint32_t volume)
{
    if (live(sound))
        GameSound_SetVolume(sound->handle, volume);
}

void rv_app_sound_set_pan(struct rv_app_sound *sound, int32_t pan)
{
    if (live(sound))
        GameSound_SetPan(sound->handle, pan);
}

void rv_app_sound_set_speed(struct rv_app_sound *sound, uint32_t per_mille)
{
    if (live(sound))
        GameSound_SetSpeed(sound->handle, per_mille);
}

void rv_app_sound_set_play_count(struct rv_app_sound *sound, uint32_t count)
{
    if (live(sound))
        GameSound_SetPlayCount(sound->handle, count);
}

void rv_app_sound_set_lead_in_ms(struct rv_app_sound *sound, uint32_t milliseconds)
{
    if (live(sound))
        GameSound_SetLeadInMilliseconds(sound->handle, milliseconds);
}

void rv_app_sound_set_tail_ms(struct rv_app_sound *sound, uint32_t milliseconds)
{
    if (live(sound))
        GameSound_SetTailMilliseconds(sound->handle, milliseconds);
}

void rv_app_sound_set_next(struct rv_app_sound *sound, struct rv_app_sound *next)
{
    if (!live(sound) || !live(next))
        return;
    GameSound_SetNext(sound->handle, next->handle);
    sound->next = next;
    collect();
}

void rv_app_sound_set_music_duck(struct rv_app_sound *sound, uint32_t level,
                                 uint32_t milliseconds)
{
    if (live(sound))
        GameSound_SetMusicDuck(sound->handle, level, milliseconds);
}
