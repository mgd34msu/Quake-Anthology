/*
 * Port of src/audio/engine.ts (UnifiedAudio). C accommodations:
 *  - The engine is a heap object (NULL from create only on OOM;
 *    invalid arguments abort with the TS RangeError/Error text;
 *    later allocation failure aborts, as in music.c). TS getters
 *    become the qa_unified_audio_* accessors below; pendingOutput
 *    and mix write caller/free'd buffers as noted per function.
 *  - Options closures (milliseconds, random, onSound, geometry,
 *    voice observers, the environment trace) become fn+ctx. The
 *    prepareOutputTransfer closure becomes a small transfer record
 *    plus qa_unified_audio_transfer_commit.
 *  - SdlAudioUnavailableError is the platform port's UNAVAILABLE
 *    result, so the opening calls return qa_sdl_audio_open_result
 *    instead of throwing (QA_SDL_AUDIO_OPEN_FAILED is OOM inside
 *    the opening path); every other device failure aborts inside
 *    the platform layer exactly as there. This inside select keeps
 *    the TS catch/retry dance, and its AggregateError outcome
 *    prints that text and aborts, like any other uncaught Error.
 *  - `number` arguments the TS validates as numbers (sample rate,
 *    maxActors, loop frame fields through qa_loop_sound, mix/pump
 *    counts) are double or already-double contract fields where a
 *    fractional abort must reproduce; plain counts are int64_t.
 *  - MusicPlayer values are borrowed qa_music_player structs (the
 *    engine closes but never destroys them), as in music.h.
 * GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_ENGINE_H
#define QA_AUDIO_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/audio/environments.h"
#include "qa/audio/mixer.h"
#include "qa/audio/music.h"
#include "qa/audio/output.h"
#include "qa/audio/types.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/persistence/value.h"
#include "qa/platform/audio.h"

typedef void (*qa_unified_audio_on_sound_fn)(void *ctx,
                                             const qa_play_sound *sound);
typedef double (*qa_unified_audio_geometry_fn)(void *ctx,
                                               const qa_audio_listener *listener,
                                               qa_vec3 position);
typedef void (*qa_unified_audio_voice_observer_fn)(
    void *ctx, const qa_audio_voice_event *event);

typedef struct qa_unified_audio_options {
  bool has_sample_rate; /* TS sampleRate ?? 44100 */
  double sample_rate;
  bool has_output_format; /* TS outputFormat ?? default at sampleRate */
  qa_audio_output_format output_format;
  qa_audio_milliseconds_fn milliseconds; /* required */
  void *milliseconds_ctx;
  qa_audio_random_fn random; /* required */
  void *random_ctx;
  bool has_max_actors; /* TS maxActors ?? 65536 */
  double max_actors;
  qa_unified_audio_on_sound_fn on_sound; /* NULL when absent */
  void *on_sound_ctx;
} qa_unified_audio_options;

typedef enum qa_unified_audio_output_state {
  QA_UNIFIED_AUDIO_DETACHED,
  QA_UNIFIED_AUDIO_PAUSED,
  QA_UNIFIED_AUDIO_PLAYING,
  QA_UNIFIED_AUDIO_CLOSED
} qa_unified_audio_output_state;
const char *qa_unified_audio_output_state_name(
    qa_unified_audio_output_state state);

typedef struct qa_unified_audio_output_configuration {
  int64_t sample_rate;
  int64_t channels;
  int64_t sample_bits;
  const char *device_name; /* borrowed; NULL for the default device */
  int64_t buffer_frames;
  int64_t maximum_queued_frames;
} qa_unified_audio_output_configuration;

typedef struct qa_unified_audio qa_unified_audio;

/* NULL only on OOM. */
qa_unified_audio *qa_unified_audio_create(
    const qa_unified_audio_options *options);
/* TS close()/[Symbol.dispose]; destroy also frees everything. */
void qa_unified_audio_close(qa_unified_audio *audio);
void qa_unified_audio_destroy(qa_unified_audio *audio);

int64_t qa_unified_audio_sample_rate(const qa_unified_audio *audio);
qa_audio_output_format qa_unified_audio_output_format(
    const qa_unified_audio *audio);
int64_t qa_unified_audio_sample_clock(const qa_unified_audio *audio);
int64_t qa_unified_audio_queued_frames(qa_unified_audio *audio);
const char *qa_unified_audio_selected_output(const qa_unified_audio *audio);
/* Malloc'd copy of pendingOutput; caller frees. */
int16_t *qa_unified_audio_pending_output(qa_unified_audio *audio,
                                         size_t *out_count);
qa_unified_audio_output_state qa_unified_audio_output_state_of(
    qa_unified_audio *audio);
/* False is the TS null. */
bool qa_unified_audio_output_configuration_get(
    qa_unified_audio *audio,
    qa_unified_audio_output_configuration *out);
qa_audio_voice_clock qa_unified_audio_voice_clock(qa_unified_audio *audio);
/* Platform names; free with qa_sdl_audio_device_names_free. */
char **qa_unified_audio_output_device_names(qa_unified_audio *audio,
                                            size_t *out_count);

/* Returns a subscription handle (>= 1) for unobserve. A repeated
 * fn+ctx pair returns its existing handle (TS Set semantics). */
int64_t qa_unified_audio_observe_voices(
    qa_unified_audio *audio, qa_unified_audio_voice_observer_fn observer,
    void *ctx);
void qa_unified_audio_unobserve_voices(qa_unified_audio *audio,
                                       int64_t subscription);
/* fn NULL clears the geometry transmission (TS null). */
void qa_unified_audio_set_geometry_transmission(
    qa_unified_audio *audio, qa_unified_audio_geometry_fn geometry,
    void *ctx);

void qa_unified_audio_set_listeners(qa_unified_audio *audio,
                                    const qa_audio_listener *listeners,
                                    size_t count);
void qa_unified_audio_set_effects_volume(qa_unified_audio *audio, double gain);
void qa_unified_audio_set_doppler_enabled(qa_unified_audio *audio,
                                          bool enabled);
void qa_unified_audio_update_actor(qa_unified_audio *audio, qa_actor_id actor,
                                   qa_vec3 origin);
/* owner NULL is the TS undefined. */
void qa_unified_audio_update_q3_seat_actor(qa_unified_audio *audio,
                                           qa_seat_id seat, qa_actor_id actor,
                                           qa_vec3 origin,
                                           qa_provider_id owner);
int64_t qa_unified_audio_play(qa_unified_audio *audio,
                              const qa_play_sound *request);
/* channel NULL is the TS null; otherwise a SharedSoundChannel text. */
void qa_unified_audio_stop_sound(qa_unified_audio *audio, qa_actor_id actor,
                                 const char *channel,
                                 qa_audio_audience audience);
void qa_unified_audio_loop(qa_unified_audio *audio,
                           const qa_loop_sound *request);
void qa_unified_audio_begin_loop_frame(qa_unified_audio *audio);
void qa_unified_audio_end_loop_frame(qa_unified_audio *audio);
void qa_unified_audio_stop_loop(qa_unified_audio *audio, qa_actor_id actor,
                                qa_audio_audience audience,
                                qa_provider_id owner);
void qa_unified_audio_clear_q3_seat_loops(qa_unified_audio *audio,
                                          qa_seat_id seat, bool kill_all,
                                          qa_provider_id owner);
void qa_unified_audio_stop_q3_seat_loop(qa_unified_audio *audio, qa_seat_id seat,
                                        qa_actor_id actor,
                                        qa_provider_id owner);
void qa_unified_audio_release_q3_seat_owner(qa_unified_audio *audio,
                                            qa_seat_id seat,
                                            qa_provider_id owner);
/* key is passed explicitly (TS key = 0 default). */
bool qa_unified_audio_add_static_sound(qa_unified_audio *audio, qa_seat_id seat,
                                       const qa_sound_asset *sound,
                                       qa_vec3 origin, double volume,
                                       double attenuation, double key);
void qa_unified_audio_remove_static_sound(qa_unified_audio *audio,
                                          qa_seat_id seat, double key);
/* level/fade passed explicitly (TS 0.3/100 defaults). */
void qa_unified_audio_update_ambient(qa_unified_audio *audio, qa_seat_id seat,
                                     const qa_sound_asset *const *sounds,
                                     size_t sound_count, const double *levels,
                                     size_t level_count,
                                     double elapsed_seconds, double level,
                                     double fade);
void qa_unified_audio_set_environment(
    qa_unified_audio *audio, qa_seat_id seat,
    const qa_reverb_environment *environments, size_t environment_count,
    qa_audio_trace_query_fn trace, void *trace_ctx);

void qa_unified_audio_queue_stream(qa_unified_audio *audio,
                                   qa_audio_stream_target target,
                                   const qa_stream_pcm *chunk);
/* Owned checkpoint tree or NULL (TS null, including no such lane);
 * caller frees with qa_save_value_free. */
qa_save_value *qa_unified_audio_capture_stream_checkpoint(
    qa_unified_audio *audio, const char *id);
/* value NULL mirrors the TS null (no lane is created). */
void qa_unified_audio_restore_stream_checkpoint(qa_unified_audio *audio,
                                                qa_audio_stream_target target,
                                                const qa_save_value *value);
void qa_unified_audio_pause_stream(qa_unified_audio *audio, const char *id,
                                   bool paused);
void qa_unified_audio_stop_stream(qa_unified_audio *audio, const char *id);
void qa_unified_audio_attach_music(qa_unified_audio *audio,
                                   qa_audio_stream_target target,
                                   qa_music_player *player);
void qa_unified_audio_stop_music(qa_unified_audio *audio, const char *id);
void qa_unified_audio_update_music(qa_unified_audio *audio);

/* Writes frames*2 samples into out. */
void qa_unified_audio_mix(qa_unified_audio *audio, double frames,
                          int16_t *out);

/* buffer_frames 0 selects the platform default (TS ?? 1024). */
qa_sdl_audio_open_result qa_unified_audio_open_device(
    qa_unified_audio *audio, const char *device_name, double buffer_frames);

/* TS prepareOutputTransfer: validate now, move on commit. The
 * record is caller storage; committing twice repeats the move,
 * exactly as calling the TS closure twice would. */
typedef struct qa_unified_audio_transfer {
  qa_unified_audio *from;
  qa_unified_audio *to;
} qa_unified_audio_transfer;
qa_unified_audio_transfer qa_unified_audio_prepare_output_transfer(
    qa_unified_audio *audio, qa_unified_audio *next);
void qa_unified_audio_transfer_commit(qa_unified_audio_transfer transfer);

void qa_unified_audio_detach_output(qa_unified_audio *audio);
/* requested NULL mirrors the TS default (the current format). */
qa_sdl_audio_open_result qa_unified_audio_select_output(
    qa_unified_audio *audio, const char *device_name,
    const qa_audio_output_format *requested, bool restart);
/* has_ahead false mirrors the TS undefined lookahead. */
int64_t qa_unified_audio_pump(qa_unified_audio *audio, bool has_ahead,
                              double ahead_frames,
                              double measured_work_milliseconds);
void qa_unified_audio_pause(qa_unified_audio *audio, bool paused);
void qa_unified_audio_stop_all(qa_unified_audio *audio);
void qa_unified_audio_reset_round(qa_unified_audio *audio);

#endif
