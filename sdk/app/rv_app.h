/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/include/game_api.h by lgreitis. */

#ifndef RV_APP_H
#define RV_APP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum rv_app_button {
    RV_APP_BUTTON_MENU = 1,
    RV_APP_BUTTON_CENTER = 2,
    RV_APP_BUTTON_NEXT = 3,
    RV_APP_BUTTON_PREVIOUS = 4,
    RV_APP_BUTTON_PLAY = 5,
};

enum rv_app_input_type {
    RV_APP_INPUT_RELEASE = 1,
    RV_APP_INPUT_PRESS = 2,
};

struct rv_app_framebuffer {
    uint16_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
};

struct rv_app_input_event {
    uint8_t button;
    uint8_t type;
    uint32_t timestamp_ms;
};

/* RGB565 pixels, borrowed for one frame call and presented on return. */
struct rv_app_frame {
    struct rv_app_framebuffer framebuffer;
};

/* The app: its package folder name under the root rv_app.mk builds it for
 * (/Revola/Apps/ or /Revola/Games/), without slashes, and its callbacks.
 * Play, Next and Previous releases go to the iPod's music control after the
 * input callback, as in Apple's games, unless the app handles them itself. */
struct rv_app {
    const char *package_id;
    void (*init)(void);
    void (*input)(const struct rv_app_input_event *event);
    void (*frame)(const struct rv_app_frame *frame);
    void (*shutdown)(void);
    uint8_t handles_music_buttons;
};

extern const struct rv_app rv_app_application;

/* Ends the app after the current callback returns. */
void rv_app_request_exit(void);

/* Blocks of the eApp heap; the host frees them all at shutdown. */
void *rv_app_alloc(size_t size);
void *rv_app_realloc(void *pointer, size_t size);
void rv_app_free(void *pointer);
uint32_t rv_app_ticks_ms(void);
void rv_app_sleep_ms(uint32_t milliseconds);

/* xorshift32 from a fixed state until seeded; seed 0 is treated as that state. */
void rv_app_random_seed(uint32_t seed);
uint32_t rv_app_random(void);

struct rv_app_wheel {
    int32_t delta;    /* Positive clockwise; consumed by each read. */
    uint8_t position; /* right = 0, up = 64, left = 128, down = 192. */
    uint8_t touched;
};

struct rv_app_wheel rv_app_read_wheel(void);

enum rv_app_direction {
    RV_APP_DIRECTION_UP = 1,
    RV_APP_DIRECTION_RIGHT = 2,
    RV_APP_DIRECTION_DOWN = 4,
    RV_APP_DIRECTION_LEFT = 8,
};

#define RV_APP_WHEEL_TOP_POSITION 64

/* Eight touch sectors; a diagonal combines two direction bits; zero when untouched.
 * top_position is the wheel position read as Up, RV_APP_WHEEL_TOP_POSITION. */
uint8_t rv_app_wheel_directions(const struct rv_app_wheel *wheel, uint8_t top_position);

/* 1 while the Hold switch is on, else 0. Reads the switch over the power
 * controller's bus each call, so ask when it matters, not every frame. */
int rv_app_hold_locked(void);

/* Folders of the package, "" for its root, with subfolders when recursive is
 * nonzero. Four may be open at once. Read returns 1 with an entry, 0 at the end
 * or for a missing folder, -1 for a bad argument or a name that does not fit.
 * Names are relative to the folder opened. Shutdown closes what is left open. */
struct rv_app_directory;

struct rv_app_directory_entry {
    char name[256];
    uint8_t is_directory;
};
struct rv_app_directory *rv_app_directory_open(const char *path, int recursive);
int rv_app_directory_read(struct rv_app_directory *directory,
                          struct rv_app_directory_entry *entry);
void rv_app_directory_close(struct rv_app_directory *directory);

/* Files of the package, read only, by path inside it; ".." is refused. Twelve
 * may be open at once. Open returns NULL on failure, read the bytes read or -1,
 * seek 0 or -1. Shutdown closes what is left open. */
struct rv_app_file;
struct rv_app_file *rv_app_file_open(const char *path);
int rv_app_file_read(struct rv_app_file *file, void *buffer, size_t size);
int rv_app_file_seek(struct rv_app_file *file, uint32_t offset);
uint32_t rv_app_file_size(const struct rv_app_file *file);
uint32_t rv_app_file_position(const struct rv_app_file *file);
void rv_app_file_close(struct rv_app_file *file);

/* Files of the package's data folder, /Revola/Data/Games/<id>/ for a game and
 * /Revola/Data/Apps/<id>/ for an app under RevolaOS, by path inside it with
 * the package's path rules. Writing creates the folder and replaces the file;
 * the handle takes write, seek and close, and the bytes are on disk when it
 * is closed. Delete returns 0, -1 on failure. */
enum rv_app_file_mode { RV_APP_FILE_READ = 1, RV_APP_FILE_WRITE = 2 };
struct rv_app_file *rv_app_data_open(const char *name, enum rv_app_file_mode mode);
int rv_app_file_write(struct rv_app_file *file, const void *data, size_t size);
int rv_app_data_delete(const char *name);

/* The host's status of the last file, data, folder or save call that failed,
 * a FileStatus_* or GameSaveStatus_* value of osos-2.0.5.h; 0 while nothing
 * failed. */
uint32_t rv_app_file_error(void);

/* Apple's save store, for an app with little to keep: a file of the data
 * folder whose contents the host holds in memory and writes at the next
 * system save or app launch, not when the app exits. Four slots exist on the
 * device and the slots hold 100 KiB together. Open loads the file; write
 * replaces the contents; read copies from `offset` and returns the bytes
 * copied, 0 past the end, -1 on error. Shutdown closes what is left open. */
struct rv_app_save;
struct rv_app_save *rv_app_save_open(const char *name);
int rv_app_save_read(struct rv_app_save *save, void *buffer, size_t size, uint32_t offset);
int rv_app_save_write(struct rv_app_save *save, const void *data, size_t size);
uint32_t rv_app_save_size(const struct rv_app_save *save);
void rv_app_save_close(struct rv_app_save *save);

/* Sound effects: PCM samples the host mixes over the music on one of eight
 * voices; the oldest voice is taken when all eight play. A sample is 8-bit
 * signed or 16-bit little-endian PCM, 1 or 2 channels, 8000..48000 Hz. The
 * host reads the sample memory while the sound plays: a buffer given to
 * create stays the app's and must outlive the sound, and any sound that
 * names it as next; load keeps its own copy. Thirty-two sounds may exist at
 * once. Free stops the sound unless another names it as next. Shutdown
 * stops and frees what is left. */
struct rv_app_sound;

#define RV_APP_SOUND_VOLUME_MAX 0x7fff
#define RV_APP_SOUND_PAN_MAX 0x7fff /* -max is left, max right. */
#define RV_APP_SOUND_SPEED_MIN 500  /* Per mille. */
#define RV_APP_SOUND_SPEED_MAX 2000
#define RV_APP_SOUND_RATE_MIN 8000 /* Hz. */
#define RV_APP_SOUND_RATE_MAX 48000

/* A PCM WAV of the package (format 1, 8 or 16 bits, 1 or 2 channels,
 * 8000..48000 Hz); NULL when missing, not plain PCM or out of range. */
struct rv_app_sound *rv_app_sound_load(const char *path);
/* `bytes` of PCM at `pcm`, whole frames; NULL for a format the host refuses
 * or when no sound is free. */
struct rv_app_sound *rv_app_sound_create(const void *pcm, size_t bytes, uint32_t rate,
                                         uint32_t channels, uint32_t bits);
void rv_app_sound_free(struct rv_app_sound *sound);

/* Play starts the sound from the beginning, stopping a run still playing,
 * with the properties set so far. */
void rv_app_sound_play(struct rv_app_sound *sound);
void rv_app_sound_stop(struct rv_app_sound *sound);
void rv_app_sound_pause(struct rv_app_sound *sound);
void rv_app_sound_resume(struct rv_app_sound *sound);
/* 1 while a voice renders the sound; a paused sound is not playing. */
int rv_app_sound_playing(const struct rv_app_sound *sound);
/* Frames played of the current pass: 0 before the first play and after a
 * natural end; stale after a stop. */
uint32_t rv_app_sound_position(const struct rv_app_sound *sound);

/* The host's defaults: volume RV_APP_SOUND_VOLUME_MAX, pan 0, speed 1000,
 * play count 1, no lead-in, tail or next, music duck off. Ranges are the
 * host's and are not checked. */
void rv_app_sound_set_volume(struct rv_app_sound *sound, uint32_t volume);
void rv_app_sound_set_pan(struct rv_app_sound *sound, int32_t pan);
void rv_app_sound_set_speed(struct rv_app_sound *sound, uint32_t per_mille);
/* 0 loops until stop, n plays n times. */
void rv_app_sound_set_play_count(struct rv_app_sound *sound, uint32_t count);
/* Silence before the sound (added to the music duck's milliseconds) and
 * after it, before `next`. */
void rv_app_sound_set_lead_in_ms(struct rv_app_sound *sound, uint32_t milliseconds);
void rv_app_sound_set_tail_ms(struct rv_app_sound *sound, uint32_t milliseconds);
/* `next` plays after the sound and its tail. The host cannot unchain, so a
 * next that is freed stays, and plays, until the last sound naming it is
 * freed. */
void rv_app_sound_set_next(struct rv_app_sound *sound, struct rv_app_sound *next);
/* The music fades to `level` (0..RV_APP_SOUND_VOLUME_MAX) over
 * `milliseconds` of silence before the sound and comes back after it;
 * 0 ms leaves the music alone. */
void rv_app_sound_set_music_duck(struct rv_app_sound *sound, uint32_t level,
                                 uint32_t milliseconds);

/* Music: the iPod's player, through the host. A command is queued for the
 * host's UI task: state, track_count and the settings read back at once,
 * position, track_index and scanning report the old values until the UI
 * task has run. The volume takes effect at
 * once. Transport commands and the repeat, shuffle, EQ and speed setters do
 * nothing while no playlist is loaded. The host restores repeat, shuffle,
 * speed, gain and an EQ set until exit when the app ends; the volume and
 * set_eq stay. */
enum rv_app_music_state {
    RV_APP_MUSIC_STOPPED = 0,
    RV_APP_MUSIC_PLAYING = 1,
    RV_APP_MUSIC_PAUSED = 2,
};

enum rv_app_music_state rv_app_music_state(void);
/* 1 while a list is loaded, also when stopped. */
int rv_app_music_has_playlist(void);
void rv_app_music_play_pause(void);
/* Stop ends playback and drops the playlist: has_playlist is 0 afterwards
 * and play_pause does nothing until the user starts music again. The host
 * reads repeat and shuffle as off after a stop until they are set again. */
void rv_app_music_stop(void);
void rv_app_music_next(void);
void rv_app_music_previous(void);
/* Starts track `index` of the playlist, counted from 0; an index past the
 * end is dropped. */
void rv_app_music_play_track(uint32_t index);
/* Into the track; past its end the player moves on. */
void rv_app_music_seek(uint32_t milliseconds);
/* The press-and-hold fast forward and rewind; scan_stop resumes. */
void rv_app_music_scan_forward(void);
void rv_app_music_scan_backward(void);
void rv_app_music_scan_stop(void);
/* 1 forward, -1 backward, 0 not scanning. */
int rv_app_music_scanning(void);
uint32_t rv_app_music_position(void); /* Milliseconds into the track. */
uint32_t rv_app_music_duration(void); /* The position plus what is left. */
uint32_t rv_app_music_track_index(void); /* From 0. */
uint32_t rv_app_music_track_count(void);

enum rv_app_music_repeat {
    RV_APP_MUSIC_REPEAT_OFF = 0,
    RV_APP_MUSIC_REPEAT_ONE = 1,
    RV_APP_MUSIC_REPEAT_ALL = 2,
};

enum rv_app_music_shuffle {
    RV_APP_MUSIC_SHUFFLE_OFF = 0,
    RV_APP_MUSIC_SHUFFLE_SONGS = 1,
    RV_APP_MUSIC_SHUFFLE_ALBUMS = 2,
};

enum rv_app_music_repeat rv_app_music_repeat_mode(void);
void rv_app_music_set_repeat_mode(enum rv_app_music_repeat mode);
enum rv_app_music_shuffle rv_app_music_shuffle_mode(void);
void rv_app_music_set_shuffle_mode(enum rv_app_music_shuffle mode);

#define RV_APP_MUSIC_VOLUME_MAX 255

/* The volume on the iPod's scale, up to the user's volume limit; set_volume
 * clamps to the limit and is not restored at exit. */
uint32_t rv_app_music_volume(void);
uint32_t rv_app_music_volume_limit(void);
void rv_app_music_set_volume(uint32_t volume);
/* The index into the EQ list of Settings. set_eq_until_exit is undone
 * when the app ends; set_eq stays on the player, while the Settings list
 * keeps showing the user's own choice. */
uint32_t rv_app_music_eq(void);
void rv_app_music_set_eq_until_exit(uint32_t index);
void rv_app_music_set_eq(uint32_t index);

#define RV_APP_MUSIC_SPEED_MIN 500 /* Per mille; the audiobook speed. */
#define RV_APP_MUSIC_SPEED_MAX 2000
#define RV_APP_MUSIC_GAIN_MAX 32767 /* The host's unit. */

uint32_t rv_app_music_speed(void);
/* Outside the range the call is ignored. */
void rv_app_music_set_speed(uint32_t per_mille);
int32_t rv_app_music_gain(void);
/* Clamped to -RV_APP_MUSIC_GAIN_MAX..RV_APP_MUSIC_GAIN_MAX. */
void rv_app_music_set_gain(int32_t gain);

/* Hands a button to the music control the way the host does for Apple's
 * games: a Play release toggles play and pause, Next and Previous releases
 * skip; anything else is ignored. For an app that handles the music buttons
 * but passes some on. */
void rv_app_music_pass_button(const struct rv_app_input_event *event);

#ifdef __cplusplus
}
#endif

#endif
