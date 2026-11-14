/* SPDX-License-Identifier: GPL-3.0-only */

/* Draws a greeting, the wheel direction, the time since start, how often it
 * ran, kept in Apple's save store, and what the music player does; Center
 * plays a tone computed at start, a wheel touch the beep.wav of the package;
 * Play, Next and Previous go to the music player; Menu exits. */

#include "rv_app_draw.h"

static char seconds[12], runs[12], track[26], times[20];
static struct rv_app_sound *tone, *beep;
static uint8_t touched;

/* 300 ms of a 441 Hz square wave, 16-bit mono 44100 Hz, in a heap block
 * that stays the app's while the sound exists. */
static struct rv_app_sound *make_tone(void)
{
    enum { RATE = 44100, FRAMES = RATE * 3 / 10, HALF_PERIOD = 50 };
    int16_t *pcm = rv_app_alloc(FRAMES * sizeof *pcm);
    if (!pcm)
        return NULL;
    for (unsigned i = 0; i < FRAMES; ++i)
        pcm[i] = (i / HALF_PERIOD) & 1 ? 6000 : -6000;
    struct rv_app_sound *sound = rv_app_sound_create(pcm, FRAMES * sizeof *pcm, RATE, 1, 16);
    if (!sound)
        rv_app_free(pcm);
    return sound;
}

static void init(void)
{
    tone = make_tone();
    beep = rv_app_sound_load("beep.wav");
    struct rv_app_save *save = rv_app_save_open("runs");
    if (!save)
        return;
    uint32_t count = 0;
    rv_app_save_read(save, &count, sizeof count, 0);
    ++count;
    rv_app_save_write(save, &count, sizeof count);
    rv_app_save_close(save);
    unsigned i = sizeof runs - 1;
    runs[i] = 0;
    do {
        runs[--i] = '0' + count % 10;
        count /= 10;
    } while (count && i);
    for (unsigned j = 0; (runs[j] = runs[i + j]); ++j)
        ;
}

static void input(const struct rv_app_input_event *event)
{
    if (event->type != RV_APP_INPUT_PRESS)
        return;
    if (event->button == RV_APP_BUTTON_MENU)
        rv_app_request_exit();
    else if (event->button == RV_APP_BUTTON_CENTER)
        rv_app_sound_play(tone);
}

/* Writes `value` in decimal at `out` and returns the end. */
static char *number(char *out, uint32_t value)
{
    char digits[11];
    unsigned i = 0;
    do {
        digits[i++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (i)
        *out++ = digits[--i];
    *out = 0;
    return out;
}

static char *minutes_seconds(char *out, uint32_t milliseconds)
{
    uint32_t total = milliseconds / 1000;
    out = number(out, total / 60);
    *out++ = ':';
    *out++ = '0' + total % 60 / 10;
    *out++ = '0' + total % 10;
    *out = 0;
    return out;
}

static void shutdown(void)
{
    rv_app_sound_free(tone);
    rv_app_sound_free(beep);
}

static void frame(const struct rv_app_frame *frame)
{
    const struct rv_app_framebuffer *fb = &frame->framebuffer;
    if (!fb->pixels)
        return;
    for (unsigned y = 0; y < fb->height; ++y)
        for (unsigned x = 0; x < fb->width; ++x)
            fb->pixels[y * fb->stride_pixels + x] = 0x0010;

    rv_app_draw_text(fb, 20, 20, "HELLO FROM REVOLAOS", 0xffff, 1);

    uint32_t ticks = rv_app_ticks_ms() / 1000;
    unsigned i = sizeof seconds - 1;
    seconds[i] = 0;
    do {
        seconds[--i] = '0' + ticks % 10;
        ticks /= 10;
    } while (ticks && i);
    rv_app_draw_text(fb, 20, 40, "SECONDS:", 0xffff, 1);
    rv_app_draw_text(fb, 74, 40, seconds + i, 0xffff, 1);

    struct rv_app_wheel wheel = rv_app_read_wheel();
    uint8_t direction = rv_app_wheel_directions(&wheel, RV_APP_WHEEL_TOP_POSITION);
    if (wheel.touched && !touched)
        rv_app_sound_play(beep);
    touched = wheel.touched;
    rv_app_draw_text(fb, 20, 60, "WHEEL:", 0xffff, 1);
    rv_app_draw_text(fb, 62, 60, direction & RV_APP_DIRECTION_UP ? "UP" : "", 0xffff, 1);
    rv_app_draw_text(fb, 80, 60, direction & RV_APP_DIRECTION_DOWN ? "DOWN" : "", 0xffff, 1);
    rv_app_draw_text(fb, 110, 60, direction & RV_APP_DIRECTION_LEFT ? "LEFT" : "", 0xffff, 1);
    rv_app_draw_text(fb, 140, 60, direction & RV_APP_DIRECTION_RIGHT ? "RIGHT" : "", 0xffff, 1);

    rv_app_draw_text(fb, 20, 80, "HOLD:", 0xffff, 1);
    rv_app_draw_text(fb, 56, 80, rv_app_hold_locked() ? "ON" : "OFF", 0xffff, 1);

    rv_app_draw_text(fb, 20, 100, "RUNS:", 0xffff, 1);
    rv_app_draw_text(fb, 56, 100, runs, 0xffff, 1);

    rv_app_draw_text(fb, 20, 120, "SOUND:", 0xffff, 1);
    rv_app_draw_text(fb, 62, 120,
                     rv_app_sound_playing(tone) || rv_app_sound_playing(beep) ? "ON" : "OFF",
                     0xffff, 1);

    enum rv_app_music_state state = rv_app_music_state();
    rv_app_draw_text(fb, 20, 140, "MUSIC:", 0xffff, 1);
    rv_app_draw_text(fb, 62, 140,
                     state == RV_APP_MUSIC_PLAYING  ? "PLAYING"
                     : state == RV_APP_MUSIC_PAUSED ? "PAUSED"
                                                    : "STOPPED",
                     0xffff, 1);
    if (rv_app_music_has_playlist()) {
        char *end = minutes_seconds(times, rv_app_music_position());
        *end++ = ' ';
        minutes_seconds(end, rv_app_music_duration());
        rv_app_draw_text(fb, 116, 140, times, 0xffff, 1);
        end = number(track, rv_app_music_track_index() + 1);
        *end++ = ' ';
        *end++ = 'O';
        *end++ = 'F';
        *end++ = ' ';
        number(end, rv_app_music_track_count());
        rv_app_draw_text(fb, 20, 160, "TRACK:", 0xffff, 1);
        rv_app_draw_text(fb, 62, 160, track, 0xffff, 1);
    }

    rv_app_draw_text(fb, 20, 200, "CENTER: TONE  WHEEL: BEEP", 0x8410, 1);
    rv_app_draw_text(fb, 20, 220, "MENU: EXIT", 0x8410, 1);
}

const struct rv_app rv_app_application = {
    .package_id = "hello",
    .init = init,
    .input = input,
    .frame = frame,
    .shutdown = shutdown,
};
