/* Port of src/platform/audio.ts (SDL2 backend). GPL-2.0-or-later. */
#include "qa/platform/audio.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "qa/foundation/support.h"

/* TS `throw new Error(msg)`, as in src/text/atlas.c. */
static noreturn void qa_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}

static void required_hint(const char *name, const char *value) {
  if (SDL_SetHint(name, value) == SDL_TRUE) return;
  const char *current = SDL_GetHint(name);
  if (current == NULL || strcmp(current, value) != 0)
    qa_error("SDL_SetHint %s: %s", name, SDL_GetError());
}

/* SDL_InitSubSystem AUDIO failure is SdlAudioUnavailableError. */
static bool initialize(char *error, size_t error_size) {
  SDL_SetMainReady();
  required_hint("SDL_NO_SIGNAL_HANDLERS", "1");
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
    if (error != NULL && error_size != 0)
      snprintf(error, error_size, "SDL_InitSubSystem AUDIO: %s", SDL_GetError());
    return false;
  }
  return true;
}

char **qa_sdl_audio_output_device_names(size_t *out_count) {
  if (!initialize(NULL, 0)) qa_error("SDL_InitSubSystem AUDIO: %s", SDL_GetError());
  int count = SDL_GetNumAudioDevices(0);
  char **names = NULL;
  size_t total = 0;
  if (count > 0) {
    names = (char **)calloc((size_t)count, sizeof *names);
    if (names == NULL) {
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      return NULL;
    }
    for (int index = 0; index < count; index++) {
      const char *name = SDL_GetAudioDeviceName(index, 0);
      if (name == NULL || name[0] == '\0') {
        const char *message = SDL_GetError();
        qa_sdl_audio_device_names_free(names, total);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        qa_error("SDL_GetAudioDeviceName: %s", message);
      }
      size_t length = strlen(name);
      names[total] = (char *)malloc(length + 1);
      if (names[total] == NULL) {
        qa_sdl_audio_device_names_free(names, total);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return NULL;
      }
      memcpy(names[total], name, length + 1);
      total++;
    }
  }
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
  *out_count = total;
  return names;
}

void qa_sdl_audio_device_names_free(char **names, size_t count) {
  if (names == NULL) return;
  for (size_t i = 0; i < count; i++) free(names[i]);
  free(names);
}

/* min(32768, max(64, 2 ** ceil(log2(frames)))); frames in 1..32768. */
static int64_t requested_frames(int64_t buffer_frames) {
  int64_t frames = 1;
  while (frames < buffer_frames) frames *= 2;
  if (frames < 64) frames = 64;
  if (frames > 32768) frames = 32768;
  return frames;
}

static bool is_integer(double value) {
  return isfinite(value) && floor(value) == value;
}

qa_sdl_audio_open_result qa_sdl_audio_device_open(const qa_sdl_audio_options *options,
                                                  qa_sdl_audio_device *out,
                                                  char *error, size_t error_size) {
  memset(out, 0, sizeof *out);
  double sample_bits_option = options->sample_bits == 0 ? 16 : options->sample_bits;
  double buffer_frames_option =
      options->buffer_frames == 0 ? 1024 : options->buffer_frames;
  if (!is_integer(options->sample_rate) || options->sample_rate < 8000 ||
      options->sample_rate > 192000)
    qa_error("Audio sample rate must be an integer in 8000..192000 Hz");
  if (options->channels != 1 && options->channels != 2)
    qa_error("Audio channels must be mono or stereo");
  if (sample_bits_option != 8 && sample_bits_option != 16)
    qa_error("Audio sample bits must be 8 or 16");
  if (options->device_name != NULL && options->device_name[0] == '\0')
    qa_error("Audio device name must be nonempty and contain no NUL");
  if (!is_integer(buffer_frames_option) || buffer_frames_option < 1 ||
      buffer_frames_option > 32768 ||
      buffer_frames_option > (double)QA_MAX_SAFE_INTEGER)
    qa_error("Audio buffer frames must be an integer in 1..32768");
  int64_t sample_rate = (int64_t)options->sample_rate;
  int channels = (int)options->channels;
  int sample_bits = (int)sample_bits_option;
  int64_t buffer_frames = (int64_t)buffer_frames_option;

  if (!initialize(error, error_size)) return QA_SDL_AUDIO_OPEN_UNAVAILABLE;

  SDL_AudioSpec desired;
  SDL_AudioSpec obtained;
  memset(&desired, 0, sizeof desired);
  memset(&obtained, 0, sizeof obtained);
  SDL_AudioFormat format = sample_bits == 16 ? AUDIO_S16SYS : AUDIO_U8;
  desired.freq = (int)sample_rate;
  desired.format = format;
  desired.channels = (Uint8)channels;
  desired.samples = (Uint16)requested_frames(buffer_frames);
  if (options->device_name != NULL) (void)SDL_GetNumAudioDevices(0);
  SDL_AudioDeviceID device =
      SDL_OpenAudioDevice(options->device_name, 0, &desired, &obtained, 0);
  if (device == 0) {
    if (error != NULL && error_size != 0)
      snprintf(error, error_size, "SDL_OpenAudioDevice: %s", SDL_GetError());
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return QA_SDL_AUDIO_OPEN_UNAVAILABLE;
  }
  if (obtained.freq != (int)sample_rate || obtained.format != format ||
      obtained.channels != (Uint8)channels) {
    SDL_CloseAudioDevice(device);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    qa_error("SDL changed the requested audio format");
  }
  if (obtained.samples == 0 ||
      obtained.size !=
          (Uint32)obtained.samples * (Uint32)channels * (Uint32)(sample_bits / 8) ||
      obtained.silence != (Uint8)(sample_bits == 16 ? 0 : 128)) {
    SDL_CloseAudioDevice(device);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    qa_error("SDL returned an invalid audio buffer specification");
  }

  /* Both adapters must expose exact input-byte counts while paused. */
  uint8_t probe8[16];
  int16_t probe16[16];
  size_t probe_bytes = (size_t)channels * 8u * (size_t)(sample_bits / 8);
  const void *probe = probe8;
  if (sample_bits == 16) {
    memset(probe16, 0, sizeof probe16);
    probe = probe16;
  } else {
    memset(probe8, 128, sizeof probe8);
  }
  if (SDL_QueueAudio(device, probe, (Uint32)probe_bytes) < 0) {
    const char *message = SDL_GetError();
    SDL_CloseAudioDevice(device);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    qa_error("SDL_QueueAudio: %s", message);
  }
  Uint32 queued = SDL_GetQueuedAudioSize(device);
  SDL_ClearQueuedAudio(device);
  if (queued != (Uint32)probe_bytes || SDL_GetQueuedAudioSize(device) != 0) {
    if (error != NULL && error_size != 0)
      snprintf(error, error_size,
               "SDL audio driver cannot report exact queued input frames at "
               "this sample rate");
    SDL_CloseAudioDevice(device);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return QA_SDL_AUDIO_OPEN_UNAVAILABLE;
  }

  uint64_t frequency = SDL_GetPerformanceFrequency();
  if (frequency == 0) {
    SDL_CloseAudioDevice(device);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    qa_error("SDL returned an invalid performance counter frequency");
  }
  out->device = device;
  out->sample_rate = sample_rate;
  out->channels = channels;
  out->sample_bits = sample_bits;
  out->buffer_frames = (int64_t)obtained.samples;
  out->max_queued_frames = sample_rate * 2;
  out->clock_frequency = frequency;
  out->elapsed_ticks = 0;
  out->playing = false;
  out->playing_since = 0;
  if (options->device_name != NULL) {
    size_t length = strlen(options->device_name);
    out->device_name = (char *)malloc(length + 1);
    if (out->device_name == NULL) {
      SDL_CloseAudioDevice(device);
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      out->device = 0;
      if (error != NULL && error_size != 0)
        snprintf(error, error_size, "out of memory");
      return QA_SDL_AUDIO_OPEN_FAILED;
    }
    memcpy(out->device_name, options->device_name, length + 1);
  }
  return QA_SDL_AUDIO_OPEN_OK;
}

static uint32_t opened(qa_sdl_audio_device *device) {
  if (device->device == 0) qa_error("SDL audio device is closed");
  return device->device;
}

int64_t qa_sdl_audio_queued_frames(qa_sdl_audio_device *device) {
  uint32_t bytes = SDL_GetQueuedAudioSize(opened(device));
  int64_t frame_bytes = (int64_t)device->channels * (device->sample_bits / 8);
  if ((int64_t)bytes % frame_bytes != 0)
    qa_error("SDL queued audio size is not frame aligned");
  return (int64_t)bytes / frame_bytes;
}

static uint64_t playing_ticks(qa_sdl_audio_device *device) {
  if (!device->playing) return device->elapsed_ticks;
  uint64_t now = SDL_GetPerformanceCounter();
  if (now < device->playing_since)
    qa_error("SDL performance counter moved backward");
  return device->elapsed_ticks + now - device->playing_since;
}

int64_t qa_sdl_audio_playback_frames(qa_sdl_audio_device *device) {
  (void)opened(device);
  __uint128_t frames =
      (__uint128_t)playing_ticks(device) * (uint64_t)device->sample_rate /
      device->clock_frequency;
  if (frames > (uint64_t)QA_MAX_SAFE_INTEGER)
    qa_range_error("SDL playback clock exceeds safe frame positions");
  return (int64_t)frames;
}

static void queue(qa_sdl_audio_device *device, const void *samples, size_t bytes,
                  size_t count) {
  uint32_t id = opened(device);
  if (count % (size_t)device->channels != 0)
    qa_error("Audio sample count is not channel aligned");
  int64_t frames = (int64_t)(count / (size_t)device->channels);
  if (qa_sdl_audio_queued_frames(device) + frames > device->max_queued_frames)
    qa_error("SDL audio queue exceeds the two-second limit");
  if (frames != 0 && SDL_QueueAudio(id, samples, (Uint32)bytes) < 0)
    qa_error("SDL_QueueAudio: %s", SDL_GetError());
}

void qa_sdl_audio_queue16(qa_sdl_audio_device *device, const int16_t *samples,
                          size_t count) {
  if (device->sample_bits != 16)
    qa_error("Audio samples do not match the device format");
  queue(device, samples, count * sizeof *samples, count);
}

void qa_sdl_audio_queue8(qa_sdl_audio_device *device, const uint8_t *samples,
                         size_t count) {
  if (device->sample_bits != 8)
    qa_error("Audio samples do not match the device format");
  queue(device, samples, count, count);
}

void qa_sdl_audio_clear(qa_sdl_audio_device *device) {
  SDL_ClearQueuedAudio(opened(device));
}

void qa_sdl_audio_pause(qa_sdl_audio_device *device) {
  uint32_t id = opened(device);
  if (!device->playing) return;
  SDL_PauseAudioDevice(id, 1);
  device->elapsed_ticks = playing_ticks(device);
  device->playing = false;
}

void qa_sdl_audio_resume(qa_sdl_audio_device *device) {
  uint32_t id = opened(device);
  if (device->playing) return;
  uint64_t now = SDL_GetPerformanceCounter();
  SDL_PauseAudioDevice(id, 0);
  device->playing_since = now;
  device->playing = true;
}

void qa_sdl_audio_start(qa_sdl_audio_device *device) { qa_sdl_audio_resume(device); }

/* TS get removed: true when a named output is no longer enumerated
 * (unplugged); the system default follows SDL's own rerouting. The open
 * device keeps the audio subsystem initialized. */
bool qa_sdl_audio_removed(qa_sdl_audio_device *device) {
  (void)opened(device);
  if (device->device_name == NULL) return false;
  int count = SDL_GetNumAudioDevices(0);
  for (int index = 0; index < count; index++) {
    const char *name = SDL_GetAudioDeviceName(index, 0);
    if (name == NULL || name[0] == '\0')
      qa_error("SDL_GetAudioDeviceName: %s", SDL_GetError());
    if (strcmp(name, device->device_name) == 0) return false;
  }
  return true;
}

qa_sdl_audio_state qa_sdl_audio_state_of(const qa_sdl_audio_device *device) {
  return device->device == 0 ? QA_SDL_AUDIO_CLOSED
                              : device->playing ? QA_SDL_AUDIO_PLAYING
                                                : QA_SDL_AUDIO_PAUSED;
}

void qa_sdl_audio_close(qa_sdl_audio_device *device) {
  if (device->device == 0) return;
  uint32_t id = device->device;
  device->device = 0;
  SDL_CloseAudioDevice(id);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
  free(device->device_name);
  device->device_name = NULL;
}
