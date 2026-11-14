/* SPDX-License-Identifier: GPL-3.0-only */

/* The iPod's player through the host's Audio module. A command is an event
 * the host's UI task applies later; the host predicts the playback state and
 * the track count at once and answers the rest from the player, so those lag
 * until the UI task has run. The host snapshots the player's settings at
 * launch and restores them when the app ends, except the volume and an EQ
 * set with set_eq. */

#include "osos-2.0.5.h"
#include "rv_app.h"
#include "rv_app_abi.h"

_Static_assert(RV_APP_MUSIC_STOPPED == GamePlaybackState_Stopped, "stopped");
_Static_assert(RV_APP_MUSIC_PLAYING == GamePlaybackState_Playing, "playing");
_Static_assert(RV_APP_MUSIC_PAUSED == GamePlaybackState_Paused, "paused");
_Static_assert(RV_APP_MUSIC_REPEAT_OFF == GameRepeatMode_Off, "repeat off");
_Static_assert(RV_APP_MUSIC_REPEAT_ONE == GameRepeatMode_One, "repeat one");
_Static_assert(RV_APP_MUSIC_REPEAT_ALL == GameRepeatMode_All, "repeat all");
_Static_assert(RV_APP_MUSIC_SHUFFLE_OFF == GamePlaybackOrder_Off, "shuffle off");
_Static_assert(RV_APP_MUSIC_SHUFFLE_SONGS == GamePlaybackOrder_Songs, "shuffle songs");
_Static_assert(RV_APP_MUSIC_SHUFFLE_ALBUMS == GamePlaybackOrder_Albums, "shuffle albums");
_Static_assert(RV_APP_MUSIC_VOLUME_MAX == GameAudio_VolumeMax, "volume");
_Static_assert(RV_APP_BUTTON_NEXT == GameButton_Next && RV_APP_BUTTON_PREVIOUS == GameButton_Previous &&
                   RV_APP_BUTTON_PLAY == GameButton_Play,
               "buttons");
_Static_assert(RV_APP_INPUT_RELEASE == GameButtonEvent_Release, "release");

enum rv_app_music_state rv_app_music_state(void)
{
    return (enum rv_app_music_state)GameAudio_GetPlaybackState();
}

int rv_app_music_has_playlist(void)
{
    return GameAudio_HasPlaybackList() != 0;
}

void rv_app_music_play_pause(void)
{
    GameAudio_TogglePlayPause(0);
}

void rv_app_music_stop(void)
{
    GameAudio_Stop();
}

void rv_app_music_next(void)
{
    GameAudio_NextTrack();
}

void rv_app_music_previous(void)
{
    GameAudio_PreviousTrack();
}

void rv_app_music_play_track(uint32_t index)
{
    if (index < GameAudio_GetEntryCount())
        GameAudio_PlayIndex(index, 0, 0);
}

void rv_app_music_seek(uint32_t milliseconds)
{
    GameAudio_Seek(milliseconds);
}

void rv_app_music_scan_forward(void)
{
    GameAudio_StartScanForward();
}

void rv_app_music_scan_backward(void)
{
    GameAudio_StartScanBackward();
}

void rv_app_music_scan_stop(void)
{
    GameAudio_StopScan();
}

int rv_app_music_scanning(void)
{
    if (GameAudio_IsScanningForward())
        return 1;
    return GameAudio_IsScanningBackward() ? -1 : 0;
}

uint32_t rv_app_music_position(void)
{
    return GameAudio_GetPosition();
}

uint32_t rv_app_music_duration(void)
{
    return GameAudio_GetPosition() + GameAudio_GetRemainingPosition();
}

uint32_t rv_app_music_track_index(void)
{
    return GameAudio_GetCurrentIndex();
}

uint32_t rv_app_music_track_count(void)
{
    return GameAudio_GetEntryCount();
}

enum rv_app_music_repeat rv_app_music_repeat_mode(void)
{
    return (enum rv_app_music_repeat)GameAudio_GetRepeatMode();
}

void rv_app_music_set_repeat_mode(enum rv_app_music_repeat mode)
{
    GameAudio_SetRepeatMode((uint8_t)mode);
}

enum rv_app_music_shuffle rv_app_music_shuffle_mode(void)
{
    return (enum rv_app_music_shuffle)GameAudio_GetPlaybackOrder();
}

void rv_app_music_set_shuffle_mode(enum rv_app_music_shuffle mode)
{
    GameAudio_SetPlaybackOrder((uint8_t)mode);
}

uint32_t rv_app_music_volume(void)
{
    return GameAudio_GetVolume();
}

uint32_t rv_app_music_volume_limit(void)
{
    return GameAudio_GetVolumeLimit();
}

void rv_app_music_set_volume(uint32_t volume)
{
    uint32_t limit = GameAudio_GetVolumeLimit();
    GameAudio_SetVolume(volume < limit ? volume : limit);
}

uint32_t rv_app_music_eq(void)
{
    return GameAudio_GetEQMenuIndex();
}

void rv_app_music_set_eq_until_exit(uint32_t index)
{
    GameAudio_SetEQMenuIndexTemporary(index);
}

void rv_app_music_set_eq(uint32_t index)
{
    GameAudio_SetEQMenuIndex(index);
}

uint32_t rv_app_music_speed(void)
{
    return (uint32_t)GameAudio_GetSpeed();
}

void rv_app_music_set_speed(uint32_t per_mille)
{
    if (per_mille >= RV_APP_MUSIC_SPEED_MIN && per_mille <= RV_APP_MUSIC_SPEED_MAX)
        GameAudio_SetSpeed((int)per_mille);
}

int32_t rv_app_music_gain(void)
{
    return GameAudio_GetGain();
}

void rv_app_music_set_gain(int32_t gain)
{
    if (gain < -RV_APP_MUSIC_GAIN_MAX)
        gain = -RV_APP_MUSIC_GAIN_MAX;
    else if (gain > RV_APP_MUSIC_GAIN_MAX)
        gain = RV_APP_MUSIC_GAIN_MAX;
    GameAudio_SetGain(gain);
}

void rv_app_music_pass_button(const struct rv_app_input_event *event)
{
    if (!event || event->type != RV_APP_INPUT_RELEASE ||
        (event->button != RV_APP_BUTTON_PLAY && event->button != RV_APP_BUTTON_NEXT &&
         event->button != RV_APP_BUTTON_PREVIOUS))
        return;
    struct rv_app_eapp_input_event native = {
        .button = event->button,
        .type = event->type,
        .timestamp = event->timestamp_ms,
    };
    GameInput_ForwardButtonEvent((const struct GameInputEvent *)&native);
}
