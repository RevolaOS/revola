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
 * (/Revola/Apps/ or /Revola/Games/), without slashes, and its callbacks. */
struct rv_app {
    const char *package_id;
    void (*init)(void);
    void (*input)(const struct rv_app_input_event *event);
    void (*frame)(const struct rv_app_frame *frame);
    void (*shutdown)(void);
};

extern const struct rv_app rv_app_application;

/* Clipped 5x7 text in the SDK font: digits, upper-case letters, '-', '+' and ':';
 * lower-case letters are drawn in upper case, other characters are skipped. */
void rv_app_draw_text(const struct rv_app_framebuffer *framebuffer, unsigned x, unsigned y,
                      const char *text, uint16_t color);
/* The same text with every font pixel `scale` pixels wide and high: a glyph
 * cell is 6 x 7 font pixels. */
void rv_app_draw_text_scaled(const struct rv_app_framebuffer *framebuffer, unsigned x,
                             unsigned y, const char *text, uint16_t color, unsigned scale);

/* Ends the app after the current callback returns. */
void rv_app_request_exit(void);

/* Blocks of the eApp heap; the host frees them all at shutdown. */
void *rv_app_alloc(size_t size);
void *rv_app_realloc(void *pointer, size_t size);
void rv_app_free(void *pointer);
uint32_t rv_app_ticks_ms(void);
void rv_app_sleep_ms(uint32_t milliseconds);

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

#ifdef __cplusplus
}
#endif

#endif
