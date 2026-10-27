/*
 * Port of src/platform/audio.ts: SdlAudioDevice over SDL2 queued
 * audio. C accommodations: the build links SDL2 directly, so only
 * the TypeScript SDL2 backend is ported (its SDL3-first selection
 * and QUAKE_SDL3_LIBRARY override have no counterpart);
 * SdlAudioUnavailableError becomes the UNAVAILABLE open result,
 * because an unavailable device leaves source SNDDMA_Init
 * unstarted rather than crashing; other Error throws print and
 * abort like the rest of the port. TS `number` option fields stay
 * double where validation is observable. GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_AUDIO_H
#define QA_PLATFORM_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_sdl_audio_options {
  double sample_rate;
  double channels;      /* 1 or 2 */
  double sample_bits;   /* 8 or 16; 0 selects the default 16 */
  const char *device_name; /* NULL selects the default device */
  double buffer_frames; /* advisory input frames; 0 selects 1024 */
} qa_sdl_audio_options;

typedef enum qa_sdl_audio_open_result {
  QA_SDL_AUDIO_OPEN_OK,
  QA_SDL_AUDIO_OPEN_UNAVAILABLE, /* SdlAudioUnavailableError */
  QA_SDL_AUDIO_OPEN_FAILED,
} qa_sdl_audio_open_result;

typedef enum qa_sdl_audio_state {
  QA_SDL_AUDIO_PAUSED,
  QA_SDL_AUDIO_PLAYING,
  QA_SDL_AUDIO_CLOSED,
} qa_sdl_audio_state;

typedef struct qa_sdl_audio_device {
  uint32_t device; /* SDL_AudioDeviceID; 0 when closed */
  int64_t sample_rate;
  int channels; /* 1 or 2 */
  int sample_bits; /* 8 or 16 */
  char *device_name; /* owned copy; NULL for the default device */
  int64_t buffer_frames;
  int64_t max_queued_frames;
  uint64_t clock_frequency;
  uint64_t elapsed_ticks;
  bool playing;
  uint64_t playing_since;
} qa_sdl_audio_device;

/* Malloc'd names; free with qa_sdl_audio_device_names_free. NULL only on OOM. */
char **qa_sdl_audio_output_device_names(size_t *out_count);
void qa_sdl_audio_device_names_free(char **names, size_t count);

qa_sdl_audio_open_result qa_sdl_audio_device_open(const qa_sdl_audio_options *options,
                                                  qa_sdl_audio_device *out,
                                                  char *error, size_t error_size);
int64_t qa_sdl_audio_queued_frames(qa_sdl_audio_device *device);
/* Logical playing time in frames, including empty-queue silence. */
int64_t qa_sdl_audio_playback_frames(qa_sdl_audio_device *device);
/* samples16 for a 16-bit device, bytes8 for an 8-bit one; pass NULL
 * for the other pointer and the sample count in the matching unit. */
void qa_sdl_audio_queue16(qa_sdl_audio_device *device, const int16_t *samples,
                          size_t count);
void qa_sdl_audio_queue8(qa_sdl_audio_device *device, const uint8_t *samples,
                         size_t count);
void qa_sdl_audio_clear(qa_sdl_audio_device *device);
void qa_sdl_audio_pause(qa_sdl_audio_device *device);
void qa_sdl_audio_resume(qa_sdl_audio_device *device);
void qa_sdl_audio_start(qa_sdl_audio_device *device);
qa_sdl_audio_state qa_sdl_audio_state_of(const qa_sdl_audio_device *device);
void qa_sdl_audio_close(qa_sdl_audio_device *device);

#endif
