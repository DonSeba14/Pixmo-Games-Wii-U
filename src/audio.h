// Sonidos de interfaz y música ambiental sintetizados en tiempo real.
#pragma once

#include "common.h"

enum {
    SFX_MOVE,
    SFX_TAB,
    SFX_SELECT,
    SFX_BACK,
    SFX_OPEN,
    SFX_TOGGLE_ON,
    SFX_TOGGLE_OFF,
    SFX_LAUNCH,
    SFX_ERROR,
    SFX_FAV,
    SFX_COUNT
};

bool audio_init(void);
void audio_shutdown(void);
void audio_play(int sfx);
void audio_play_pitch(int sfx, float pitch);
void audio_set(bool sfx_on, bool music_on);
void audio_fade_music(float target, float seconds); // 0..1 volume multiplier
// Song from the SD card that is playing: returns a number that grows every
// time a new song starts (0 = none yet) and copies its title.
int  audio_song(char *title, size_t n);
