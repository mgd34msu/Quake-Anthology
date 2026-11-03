/*
 * Port of src/audio/mixer.ts (id Software snd_mix.c/snd_dma.c/
 * snd_mem.c). C accommodations:
 *  - AudioMixer is a heap object (NULL from create only on OOM;
 *    invalid arguments abort with the TS RangeError/Error text, as
 *    elsewhere). Allocation failure inside voice admission/painting
 *    aborts, as in music.c, since the TS bool returns are admission
 *    results. mix writes frames*2 int16 into a caller buffer.
 *  - `number` fields the TS integer-validates (entity, channel,
 *    channel volume, frame number, timestamps) stay double so the
 *    fractional/NaN aborts reproduce; counts the mixer holds as
 *    exact integers (paint/sound/raw clocks) are int64_t.
 *  - Closures (milliseconds, random, geometry transmission, voice
 *    observer/allocator, ConsoleOutput.print, MixerSoundMemory)
 *    become a function pointer plus ctx. TS defaults (capacity 96,
 *    outputChannels 2, entityCapacity 1024, loop volumes 127/90,
 *    ambient level 0.3 / fade 100, static key 0) are passed
 *    explicitly by callers, as in music.h/streams.h.
 *  - Sound identity (same-sound scans, loop merging) is pointer
 *    identity of the borrowed qa_pcm_sound, as TS `===` on the
 *    PcmSound object. Sounds and assets are borrowed throughout.
 * GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_MIXER_H
#define QA_AUDIO_MIXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/audio/types.h"
#include "qa/audio/wav.h"
#include "qa/contracts/math.h"
#include "qa/core/cvars/index.h"

/* TS SOUND_TIME_EPOCH. */
#define QA_SOUND_TIME_EPOCH 1073741824LL
/* Raw ring capacity in frames (TS rawCapacity); the raw sample
 * pointer spans 2 * QA_AUDIO_MIXER_RAW_CAPACITY int32 values. */
#define QA_AUDIO_MIXER_RAW_CAPACITY 16384

typedef enum qa_voice_origin_kind {
  QA_VOICE_ORIGIN_LOCAL,
  QA_VOICE_ORIGIN_FIXED,
  QA_VOICE_ORIGIN_ENTITY
} qa_voice_origin_kind;
typedef struct qa_voice_origin {
  qa_voice_origin_kind kind;
  qa_vec3 position; /* fixed */
  double entity;    /* entity */
} qa_voice_origin;

typedef struct qa_start_sound_options {
  double entity;
  double channel; /* startSound/startLocalSound; the shared
                     entry points ignore it (TS Omit<..., "channel">) */
  qa_voice_origin origin;
  double volume;
} qa_start_sound_options;

/* startQ1Sound / startQ2Sound options. */
typedef struct qa_source_sound_options {
  double entity;
  qa_voice_origin origin;
  double volume;
  double attenuation;
  bool has_delay_seconds; /* Q2 */
  double delay_seconds;
  bool has_server_milliseconds; /* Q2 */
  double server_milliseconds;
} qa_source_sound_options;

typedef struct qa_frame_looping_sound_options {
  double entity;
  qa_vec3 origin;
  qa_vec3 velocity;
  double frame_number;
  bool has_volume; /* TS default 127 when absent */
  double volume;
} qa_frame_looping_sound_options;

typedef struct qa_real_looping_sound_options {
  double entity;
  qa_vec3 origin;
  qa_vec3 velocity;
  bool has_volume; /* TS default 90 when absent */
  double volume;
} qa_real_looping_sound_options;

/* setSourceLoopSounds entry (family is "q1" | "q2" in TS). */
typedef struct qa_source_loop_sound {
  qa_sound_family family;
  double entity;
  const qa_pcm_sound *sound;
  qa_vec3 origin;
  double volume;
  bool has_attenuation; /* TS ?? 1 when absent */
  double attenuation;
} qa_source_loop_sound;

typedef struct qa_sound_paint_range {
  double start_frame;
  double end_frame;
} qa_sound_paint_range;

typedef struct qa_mixer_stereo_volume {
  double left;
  double right;
} qa_mixer_stereo_volume;

/* S_SpatializeOrigin, including the source mono-output branch. */
qa_mixer_stereo_volume qa_spatialize_sound_origin(qa_vec3 position,
                                                  qa_vec3 listener_origin,
                                                  qa_axis listener_axis,
                                                  double volume, int channels);

/** TS MixerVoiceEvent: AudioVoiceEvent without the seat. */
typedef struct qa_mixer_voice_event {
  qa_audio_voice_event_kind kind;
  double voice_id;
  const qa_sound_asset *sound; /* start */
  double output_sample;
  double sample_rate;            /* start */
  double source_offset_seconds;  /* start */
  qa_audio_voice_stop_reason reason; /* stop */
} qa_mixer_voice_event;

typedef double (*qa_audio_milliseconds_fn)(void *ctx);
typedef double (*qa_audio_random_fn)(void *ctx);
typedef double (*qa_mixer_transmission_fn)(void *ctx, qa_vec3 position);
typedef void (*qa_mixer_voice_observer_fn)(void *ctx,
                                           const qa_mixer_voice_event *event);
typedef double (*qa_mixer_voice_id_fn)(void *ctx);
/* TS ConsoleOutput.print. */
typedef void (*qa_mixer_print_fn)(const char *text, void *ctx);

typedef struct qa_audio_mixer qa_audio_mixer;

/* TS AudioMixer constructor. NULL only on OOM. */
qa_audio_mixer *qa_audio_mixer_create(double output_rate,
                                      qa_audio_milliseconds_fn milliseconds,
                                      void *milliseconds_ctx, double capacity,
                                      int output_channels,
                                      double entity_capacity);
void qa_audio_mixer_destroy(qa_audio_mixer *mixer);

int64_t qa_audio_mixer_output_rate(const qa_audio_mixer *mixer);
int64_t qa_audio_mixer_capacity(const qa_audio_mixer *mixer);
int qa_audio_mixer_output_channels(const qa_audio_mixer *mixer);
int64_t qa_audio_mixer_sample_clock(const qa_audio_mixer *mixer);
int64_t qa_audio_mixer_sound_clock(const qa_audio_mixer *mixer);
bool qa_audio_mixer_playback_enabled(const qa_audio_mixer *mixer);
void qa_audio_mixer_set_playback_enabled(qa_audio_mixer *mixer, bool enabled);
int64_t qa_audio_mixer_raw_end(const qa_audio_mixer *mixer);

void qa_audio_mixer_bind_sound_cvars(qa_audio_mixer *mixer,
                                     qa_cvar_registry *cvars);
void qa_audio_mixer_bind_console_output(qa_audio_mixer *mixer,
                                        qa_mixer_print_fn print, void *ctx);
/* observer NULL clears it; allocate_id NULL keeps the previous
 * allocator (the TS `allocateId !== undefined` test). */
void qa_audio_mixer_set_voice_observer(qa_audio_mixer *mixer,
                                       qa_mixer_voice_observer_fn observer,
                                       void *observer_ctx,
                                       qa_mixer_voice_id_fn allocate_id,
                                       void *allocate_ctx);
/* fn NULL clears the transmission (TS null). */
void qa_audio_mixer_set_geometry_transmission(
    qa_audio_mixer *mixer, qa_mixer_transmission_fn transmission, void *ctx);

typedef struct qa_mixer_channel_volume {
  const qa_pcm_sound *sound;
  double left;
  double right;
} qa_mixer_channel_volume;
/* Malloc'd snapshot of the TS channelVolumes() generator, in slot
 * order; caller frees the array. NULL only on OOM. */
qa_mixer_channel_volume *qa_audio_mixer_channel_volumes(
    const qa_audio_mixer *mixer, size_t *out_count);

void qa_audio_mixer_select_time(qa_audio_mixer *mixer, double sound_time,
                                double paint_time);
/* Returns the rebased sound time. */
double qa_audio_mixer_rebase_time(qa_audio_mixer *mixer, double delivered_time);
void qa_audio_mixer_set_listener(qa_audio_mixer *mixer, double entity,
                                 qa_vec3 origin, qa_axis axis);
/* TS entityActive. */
bool qa_audio_mixer_entity_active(const qa_audio_mixer *mixer, double entity);
void qa_audio_mixer_update_entity_position(qa_audio_mixer *mixer, double entity,
                                           qa_vec3 origin);
void qa_audio_mixer_set_effects_volume(qa_audio_mixer *mixer, double volume);
void qa_audio_mixer_set_music_volume(qa_audio_mixer *mixer, double volume);
void qa_audio_mixer_set_doppler_enabled(qa_audio_mixer *mixer, bool enabled);

/* source_name NULL is the TS null. */
bool qa_audio_mixer_start_local_sound(qa_audio_mixer *mixer,
                                      const qa_pcm_sound *sound,
                                      double channel, const char *source_name);
bool qa_audio_mixer_start_sound(qa_audio_mixer *mixer,
                                const qa_pcm_sound *sound,
                                qa_start_sound_options options,
                                const char *source_name);
bool qa_audio_mixer_start_shared_sound(qa_audio_mixer *mixer,
                                       const qa_pcm_sound *sound,
                                       qa_start_sound_options options,
                                       qa_sound_channel_command command,
                                       const char *source_name,
                                       const qa_sound_asset *asset);
bool qa_audio_mixer_start_q1_sound(qa_audio_mixer *mixer,
                                   const qa_pcm_sound *sound,
                                   qa_source_sound_options options,
                                   qa_sound_channel_command command,
                                   qa_audio_random_fn random, void *random_ctx,
                                   const qa_sound_asset *asset);
bool qa_audio_mixer_start_q2_sound(qa_audio_mixer *mixer,
                                   const qa_pcm_sound *sound,
                                   qa_source_sound_options options,
                                   qa_sound_channel_command command,
                                   const qa_sound_asset *asset);

bool qa_audio_mixer_add_static_sound(qa_audio_mixer *mixer,
                                     const qa_pcm_sound *sound,
                                     qa_vec3 origin, double volume,
                                     double attenuation, double key);
void qa_audio_mixer_remove_static_sound(qa_audio_mixer *mixer, double key);
/* Only keys 0 and 1 are read, as in TS; short arrays read absent. */
void qa_audio_mixer_update_ambient(qa_audio_mixer *mixer,
                                   const qa_pcm_sound *const *sounds,
                                   size_t sound_count, const double *levels,
                                   size_t level_count, double elapsed_seconds,
                                   double level, double fade);
void qa_audio_mixer_set_source_loop_sounds(qa_audio_mixer *mixer,
                                           const qa_source_loop_sound *entries,
                                           size_t count);
void qa_audio_mixer_update_looping_sound(
    qa_audio_mixer *mixer, const qa_pcm_sound *sound,
    qa_frame_looping_sound_options options);
void qa_audio_mixer_update_real_looping_sound(
    qa_audio_mixer *mixer, const qa_pcm_sound *sound,
    qa_real_looping_sound_options options);
void qa_audio_mixer_clear_looping_sounds(qa_audio_mixer *mixer, bool kill_all);
void qa_audio_mixer_stop_looping_sound(qa_audio_mixer *mixer, double entity);
void qa_audio_mixer_stop_channel(qa_audio_mixer *mixer, double entity,
                                 double channel);
/* channel NULL is the TS null; otherwise a SharedSoundChannel text. */
void qa_audio_mixer_stop_shared_channel(qa_audio_mixer *mixer, double entity,
                                        const char *channel);
void qa_audio_mixer_stop_entity(qa_audio_mixer *mixer, double entity);
void qa_audio_mixer_stop_all(qa_audio_mixer *mixer);
void qa_audio_mixer_clear_sound_buffer(qa_audio_mixer *mixer);
void qa_audio_mixer_clear_raw(qa_audio_mixer *mixer);
void qa_audio_mixer_reset_raw_to_sound_time(qa_audio_mixer *mixer);
void qa_audio_mixer_stop_raw(qa_audio_mixer *mixer);
void qa_audio_mixer_queue_raw(qa_audio_mixer *mixer, const qa_pcm_sound *sound,
                              double volume);

/* TS mix(number): direct consumption. out holds frames*2 samples. */
void qa_audio_mixer_mix(qa_audio_mixer *mixer, double frames, int16_t *out);
/* TS mix(SoundPaintRange): paints without advancing sound time. */
void qa_audio_mixer_mix_range(qa_audio_mixer *mixer,
                              qa_sound_paint_range range, int16_t *out);
bool qa_audio_mixer_scan_channel_starts(qa_audio_mixer *mixer);

#endif
