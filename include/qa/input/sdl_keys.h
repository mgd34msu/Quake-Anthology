/*
 * Port of src/input/sdl-keys.ts: unix/linux_glimp.c XLateKey over
 * SDL keycodes, plus Sys_XTimeToSysTime's 30 ms correction.
 * GPL-2.0-or-later.
 */
#ifndef QA_INPUT_SDL_KEYS_H
#define QA_INPUT_SDL_KEYS_H

#include <stdbool.h>
#include <stdint.h>

/** TS sdlGameKey; modifiers default to 0 in the TS. */
double qa_sdl_game_key(int32_t keycode, int32_t modifiers);
double qa_sdl_event_time(double timestamp, double ticks, double now, bool subframe);

#endif
