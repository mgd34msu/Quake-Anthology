/* Port of src/audio/mixer.ts. See include/qa/audio/mixer.h.
 * GPL-2.0-or-later. */
#include "qa/audio/mixer.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/audio/source_paint.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

#define SOUND_FULLVOLUME 80
#define RAW_CAPACITY QA_AUDIO_MIXER_RAW_CAPACITY
#define PAINTBUFFER_SIZE 4096
#define SND_CHUNK_SIZE 1024

static double fround_(double value) { return (double)(float)value; }
static const double sound_attenuate = 0.0008f; /* fround staged below */

static noreturn void plain_error(const char *format, ...) {
  char message[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof message, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}

static void *xmalloc(size_t size) {
  void *value = malloc(size == 0 ? 1 : size);
  if (value == NULL) abort(); /* OOM has no TS counterpart */
  return value;
}
static void *xcalloc(size_t count, size_t size) {
  void *value = calloc(count == 0 ? 1 : count, size);
  if (value == NULL) abort();
  return value;
}
static void *xrealloc(void *ptr, size_t size) {
  void *value = realloc(ptr, size == 0 ? 1 : size);
  if (value == NULL) abort();
  return value;
}

/* JS String(number) for the values these messages interpolate:
 * integral values print as plain integers, others shortest-roundtrip. */
static void fmt_num(char *out, size_t size, double value) {
  qa_net_js_number(value, out, size);
}

static bool is_integer(double value) {
  return isfinite(value) && trunc(value) == value;
}
static bool is_safe_integer(double value) {
  return is_integer(value) && fabs(value) <= (double)QA_MAX_SAFE_INTEGER;
}
static bool int32_is(double value) {
  return (double)qa_int32(value) == value;
}

static void require_positive_integer(double value, const char *name) {
  if (!is_safe_integer(value) || value <= 0)
    qa_range_error("%s must be a positive safe integer", name);
}
static void require_entity(double entity, double limit) {
  if (!is_safe_integer(entity) || entity < 0 || entity >= limit) {
    char text[64];
    fmt_num(text, sizeof text, limit - 1);
    qa_range_error("entity must be an integer from 0 through %s", text);
  }
}
static void require_channel(double channel) {
  if (!is_integer(channel) || !int32_is(channel))
    qa_range_error("channel must be a signed 32-bit integer");
}
static void require_frame_number(double frame_number) {
  if (!is_safe_integer(frame_number) || !int32_is(frame_number))
    qa_range_error("frame number must be a signed 32-bit integer");
}
static void require_channel_volume(double volume) {
  if (!is_integer(volume) || volume < 0 || volume > 255)
    qa_range_error("channel volume must be an integer from 0 through 255");
}
static void require_gain(double gain, const char *name) {
  if (!isfinite(gain) || gain < 0)
    qa_range_error("%s must be a finite nonnegative number", name);
}

/* ---- Prepared sounds (shared, refcounted; TS object identity) ---- */

typedef struct prepared_sound {
  int refs;
  const qa_pcm_sound *sound;
  double step256;
  double output_frames;
  double *doppler_sums;
  size_t doppler_sums_length;
} prepared_sound;

static prepared_sound *prepared_new(const qa_pcm_sound *sound) {
  prepared_sound *prepared = xcalloc(1, sizeof *prepared);
  prepared->refs = 1;
  prepared->sound = sound;
  return prepared;
}
static prepared_sound *prepared_retain(prepared_sound *prepared) {
  prepared->refs++;
  return prepared;
}
static void prepared_release(prepared_sound *prepared) {
  if (prepared == NULL) return;
  if (--prepared->refs == 0) {
    free(prepared->doppler_sums);
    free(prepared);
  }
}
static double prepared_frames(const prepared_sound *prepared) {
  return prepared->output_frames;
}

typedef enum voice_role {
  ROLE_EFFECT,
  ROLE_STATIC,
  ROLE_AMBIENT,
  ROLE_ENTITY_LOOP
} voice_role;

typedef struct voice_policy {
  double attenuation;
  double distance_offset;
  double stereo_scale;
  bool unattenuated_mono;
  bool has_loop_start;
  double loop_start;
  bool has_gain_limit;
  double gain_limit;
  voice_role role;
  double key;
} voice_policy;

typedef enum voice_notification {
  NOTIFY_PENDING,
  NOTIFY_STARTED,
  NOTIFY_STOPPED
} voice_notification;
typedef enum voice_start_kind {
  START_SCHEDULED,
  START_PENDING,
  START_STARTED
} voice_start_kind;

typedef struct one_shot_voice {
  bool occupied;
  double voice_id;
  const qa_sound_asset *asset;
  voice_notification notification;
  prepared_sound *prepared;
  double entity;
  bool has_channel;
  char channel[64];
  bool has_policy;
  voice_policy policy;
  qa_voice_origin origin;
  double volume;
  qa_mixer_stereo_volume stereo;
  voice_start_kind start_kind;
  int64_t start_sample;
  int64_t start_order;
  int64_t allocated_at;
} one_shot_voice;

typedef struct loop_voice {
  prepared_sound *prepared;
  double entity;
  qa_vec3 velocity;
  double volume;
  bool persistent; /* false is TS "frame" */
  bool active;
  bool doppler;
  double doppler_scale;
  double old_doppler_scale;
  double frame_number;
} loop_voice;

typedef struct loop_mix {
  prepared_sound *prepared;
  double left_volume;
  double right_volume;
  bool doppler;
  double doppler_scale;
  double old_doppler_scale;
} loop_mix;

typedef struct transmission_entry {
  qa_vec3 position;
  double gain;
} transmission_entry;

struct qa_audio_mixer {
  int64_t output_rate;
  int64_t capacity;
  int output_channels;
  int64_t entity_capacity;
  qa_audio_milliseconds_fn milliseconds;
  void *milliseconds_ctx;
  double effects_volume; /* binary32 value */
  bool doppler_enabled;
  bool enabled;
  double next_voice;
  qa_mixer_voice_observer_fn voice_observer;
  void *voice_observer_ctx;
  qa_mixer_voice_id_fn allocate_voice_id; /* NULL: ++next_voice */
  void *allocate_voice_id_ctx;
  qa_mixer_transmission_fn transmission;
  void *transmission_ctx;
  transmission_entry *transmission_cache;
  size_t transmission_cache_count;
  size_t transmission_cache_capacity;
  double listener_entity;
  qa_vec3 listener_origin;
  qa_axis listener_axis;
  qa_vec3 *entity_positions;
  one_shot_voice *voices;
  size_t voice_count;
  size_t voice_capacity;
  size_t *free_channels;
  size_t free_count;
  size_t free_capacity;
  loop_voice *loops;
  size_t loop_count;
  size_t loop_capacity;
  loop_mix *loop_channels;
  size_t loop_channel_count;
  size_t loop_channel_capacity;
  int32_t raw_samples[RAW_CAPACITY * 2];
  int64_t painted_time;
  int64_t sound_time;
  int64_t raw_end_time;
  double source_begin_offset;
  int64_t source_schedule_order;
  qa_cvar_registry *sound_cvars; /* borrowed */
  qa_mixer_print_fn console_print;
  void *console_ctx;
};

static double allocate_voice_id(qa_audio_mixer *mixer) {
  if (mixer->allocate_voice_id != NULL)
    return mixer->allocate_voice_id(mixer->allocate_voice_id_ctx);
  return ++mixer->next_voice;
}

static int64_t allocation_time(qa_audio_mixer *mixer) {
  double time = mixer->milliseconds(mixer->milliseconds_ctx);
  if (!is_integer(time) || !int32_is(time))
    qa_range_error("sound allocation clock requires signed-int milliseconds");
  return (int64_t)time;
}

static void voice_started(qa_audio_mixer *mixer, one_shot_voice *voice) {
  if (voice->notification != NOTIFY_PENDING ||
      voice->start_kind != START_STARTED || voice->asset == NULL)
    return;
  voice->notification = NOTIFY_STARTED;
  if (mixer->voice_observer == NULL) return;
  double offset = (double)(mixer->painted_time - voice->start_sample);
  qa_mixer_voice_event event = {0};
  event.kind = QA_AUDIO_VOICE_EVENT_START;
  event.voice_id = voice->voice_id;
  event.sound = voice->asset;
  event.output_sample = (double)mixer->painted_time;
  event.sample_rate = (double)mixer->output_rate;
  event.source_offset_seconds =
      (offset > 0 ? offset : 0) / (double)mixer->output_rate;
  mixer->voice_observer(mixer->voice_observer_ctx, &event);
}

static void voice_stopped_at(qa_audio_mixer *mixer, one_shot_voice *voice,
                             qa_audio_voice_stop_reason reason,
                             int64_t sample) {
  if (voice->notification != NOTIFY_STARTED) return;
  voice->notification = NOTIFY_STOPPED;
  if (mixer->voice_observer == NULL) return;
  qa_mixer_voice_event event = {0};
  event.kind = QA_AUDIO_VOICE_EVENT_STOP;
  event.voice_id = voice->voice_id;
  event.output_sample = (double)sample;
  event.reason = reason;
  mixer->voice_observer(mixer->voice_observer_ctx, &event);
}
static void voice_stopped(qa_audio_mixer *mixer, one_shot_voice *voice,
                          qa_audio_voice_stop_reason reason) {
  voice_stopped_at(mixer, voice, reason, mixer->painted_time);
}

static void free_push(qa_audio_mixer *mixer, size_t index) {
  if (mixer->free_count == mixer->free_capacity) {
    mixer->free_capacity = mixer->free_capacity == 0 ? 8 : mixer->free_capacity * 2;
    mixer->free_channels = xrealloc(mixer->free_channels,
                                    mixer->free_capacity * sizeof *mixer->free_channels);
  }
  mixer->free_channels[mixer->free_count++] = index;
}

static void free_channel_reason(qa_audio_mixer *mixer, size_t index,
                                qa_audio_voice_stop_reason reason) {
  one_shot_voice *voice = &mixer->voices[index];
  if (voice->occupied) {
    voice_stopped(mixer, voice, reason);
    prepared_release(voice->prepared);
  }
  memset(voice, 0, sizeof *voice);
  free_push(mixer, index);
}
static void free_channel(qa_audio_mixer *mixer, size_t index) {
  free_channel_reason(mixer, index, QA_AUDIO_VOICE_STOP_STOPPED);
}

/* Takes a voice slot: pops the free stack or grows voices by one,
 * exactly as the TS index assignment past the array end. */
static size_t take_channel(qa_audio_mixer *mixer, bool *had_free) {
  if (mixer->free_count > 0) {
    *had_free = true;
    return mixer->free_channels[--mixer->free_count];
  }
  *had_free = false;
  if (mixer->voice_count == mixer->voice_capacity) {
    mixer->voice_capacity =
        mixer->voice_capacity == 0 ? 8 : mixer->voice_capacity * 2;
    mixer->voices = xrealloc(mixer->voices,
                             mixer->voice_capacity * sizeof *mixer->voices);
  }
  size_t index = mixer->voice_count++;
  memset(&mixer->voices[index], 0, sizeof mixer->voices[index]);
  return index;
}

/* Source MAX_CHANNELS (TS MAXIMUM_VOICES). */
#define MAXIMUM_VOICES 128

/* TS acquireChannel: a free slot, a new slot below the voice limit, or the
 * oldest ordinary effect (the listener's own sounds only for the listener),
 * stopped as "replaced". False refuses the new sound. */
static bool acquire_channel(qa_audio_mixer *mixer, double entity,
                            size_t *out_index, bool *out_reused) {
  bool had_free = false;
  if (mixer->free_count > 0 ||
      (double)mixer->voice_count <
          (mixer->capacity > MAXIMUM_VOICES ? (double)mixer->capacity : MAXIMUM_VOICES)) {
    *out_index = take_channel(mixer, &had_free);
    *out_reused = had_free;
    return true;
  }
  int64_t victim = -1;
  double oldest = INFINITY;
  for (size_t i = 0; i < mixer->voice_count; i++) {
    const one_shot_voice *voice = &mixer->voices[i];
    if (!voice->occupied ||
        (voice->entity == mixer->listener_entity && entity != mixer->listener_entity))
      continue;
    if (voice->has_policy && voice->policy.role != ROLE_EFFECT) continue;
    if ((double)voice->allocated_at < oldest) {
      oldest = (double)voice->allocated_at;
      victim = (int64_t)i;
    }
  }
  if (victim < 0) return false;
  free_channel_reason(mixer, (size_t)victim, QA_AUDIO_VOICE_STOP_REPLACED);
  mixer->free_count--; /* TS freeChannels.pop() of the slot just freed */
  *out_index = (size_t)victim;
  *out_reused = true;
  return true;
}

static void raw_debug_print(qa_audio_mixer *mixer, const char *text) {
  if (mixer->sound_cvars == NULL) return;
  qa_cvar_snapshot snapshot;
  if (!qa_cvar_registry_get(mixer->sound_cvars, "developer", &snapshot))
    return;
  double value = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  if (value == 0) return;
  if (mixer->console_print == NULL)
    plain_error("Raw sound diagnostics require console output");
  mixer->console_print(text, mixer->console_ctx);
}

static void reset_channels(qa_audio_mixer *mixer) {
  for (size_t i = 0; i < mixer->voice_count; i++) {
    one_shot_voice *voice = &mixer->voices[i];
    if (voice->occupied) {
      voice_stopped(mixer, voice, QA_AUDIO_VOICE_STOP_STOPPED);
      prepared_release(voice->prepared);
    }
    memset(voice, 0, sizeof *voice);
  }
  mixer->free_count = 0;
  for (size_t i = 0; i < mixer->voice_count; i++) free_push(mixer, i);
  raw_debug_print(mixer, "Channel memory manager started\n");
}

static double diagnostic_setting(qa_audio_mixer *mixer, const char *name) {
  if (mixer->sound_cvars == NULL) return 0;
  qa_cvar_snapshot snapshot;
  if (!qa_cvar_registry_get(mixer->sound_cvars, name, &snapshot))
    plain_error("Missing sound cvar %s", name);
  double value = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  return value;
}

static void validate_sound(const qa_pcm_sound *sound, bool allow_stereo) {
  require_positive_integer((double)sound->sample_rate, "PCM sample rate");
  if (allow_stereo) {
    if (!is_safe_integer((double)sound->frame_count) || sound->frame_count < 0)
      qa_range_error("PCM frame count must be a nonnegative safe integer");
  } else {
    require_positive_integer((double)sound->frame_count, "PCM frame count");
  }
  if (sound->channels != 1 && sound->channels != 2)
    qa_range_error("PCM channel count must be one or two");
  if (!allow_stereo && sound->channels != 1)
    qa_range_error("sound effects must be mono, matching Quake S_LoadSound");
  double expected = (double)sound->frame_count * (double)sound->channels;
  if (!is_safe_integer(expected) || (double)sound->sample_count != expected) {
    char text[64];
    fmt_num(text, sizeof text, expected);
    qa_range_error("PCM has %zu samples, expected %s", sound->sample_count,
                   text);
  }
  if (sound->loop_start != -1 &&
      (sound->loop_start < 0 || sound->loop_start >= sound->frame_count))
    qa_range_error("PCM loop start is outside the sound");
}

static prepared_sound *prepare_sound(qa_audio_mixer *mixer,
                                     const qa_pcm_sound *sound) {
  prepared_sound *prepared = prepared_new(sound);
  double scale = fround_((double)sound->sample_rate / (double)mixer->output_rate);
  double frames = trunc(fround_(fround_((double)sound->frame_count) / scale));
  if (!is_safe_integer(frames) || frames < 0)
    qa_range_error("resampled PCM frame count is outside the supported range");
  prepared->step256 = trunc(scale * 256);
  prepared->output_frames = frames;
  return prepared;
}

qa_mixer_stereo_volume qa_spatialize_sound_origin(qa_vec3 position,
                                                  qa_vec3 listener_origin,
                                                  qa_axis listener_axis,
                                                  double volume, int channels) {
  qa_vec3 source = qa_sub3(position, listener_origin);
  double distance = qa_length3(source);
  qa_vec3 direction = qa_normalize3(source);
  double pan = -qa_dot3(direction, listener_axis.vectors[1]);
  double beyond = distance - SOUND_FULLVOLUME;
  double distance_beyond = beyond > 0 ? fround_(beyond) : 0;
  if (distance_beyond < 0) distance_beyond = 0;
  double distance_loss = fround_(distance_beyond * fround_(sound_attenuate));
  double right_scale =
      channels == 1 ? 1 : qa_js_max(0, fround_(0.5 * (1 + pan)));
  double left_scale = channels == 1 ? 1 : qa_js_max(0, fround_(0.5 * (1 - pan)));
  double right =
      qa_js_max(0, trunc(fround_(volume * fround_((1 - distance_loss) * right_scale))));
  double left =
      qa_js_max(0, trunc(fround_(volume * fround_((1 - distance_loss) * left_scale))));
  qa_mixer_stereo_volume result = {left, right};
  return result;
}

/* Math.max(0, Math.fround(distance - SOUND_FULLVOLUME)) above: the
 * fround runs before the max in TS; folding it into the branch is
 * identical for every double (fround of a negative stays <= 0). */

static bool vec_key_equal(qa_vec3 a, qa_vec3 b) {
  return (a.x == b.x || (isnan(a.x) && isnan(b.x))) &&
         (a.y == b.y || (isnan(a.y) && isnan(b.y))) &&
         (a.z == b.z || (isnan(a.z) && isnan(b.z)));
}

static qa_mixer_stereo_volume transmit(qa_audio_mixer *mixer, qa_vec3 position,
                                       qa_mixer_stereo_volume volume) {
  if (mixer->transmission == NULL ||
      (volume.left == 0 && volume.right == 0))
    return volume;
  double gain = 0;
  bool found = false;
  for (size_t i = 0; i < mixer->transmission_cache_count; i++) {
    if (vec_key_equal(mixer->transmission_cache[i].position, position)) {
      gain = mixer->transmission_cache[i].gain;
      found = true;
      break;
    }
  }
  if (!found) {
    gain = mixer->transmission(mixer->transmission_ctx, position);
    if (mixer->transmission_cache_count == mixer->transmission_cache_capacity) {
      mixer->transmission_cache_capacity =
          mixer->transmission_cache_capacity == 0
              ? 16
              : mixer->transmission_cache_capacity * 2;
      mixer->transmission_cache = xrealloc(
          mixer->transmission_cache,
          mixer->transmission_cache_capacity *
              sizeof *mixer->transmission_cache);
    }
    mixer->transmission_cache[mixer->transmission_cache_count++] =
        (transmission_entry){position, gain};
  }
  if (gain == 1) return volume;
  qa_mixer_stereo_volume result = {trunc(volume.left * gain),
                                   trunc(volume.right * gain)};
  return result;
}

static qa_vec3 position_for_entity(qa_audio_mixer *mixer, double entity) {
  if (!is_integer(entity) || entity < 0 ||
      entity >= (double)mixer->entity_capacity) {
    char text[64];
    fmt_num(text, sizeof text, entity);
    plain_error("Missing source sound position for entity %s", text);
  }
  return mixer->entity_positions[(size_t)entity];
}

static qa_vec3 resolve_origin(qa_audio_mixer *mixer, qa_voice_origin origin) {
  switch (origin.kind) {
    case QA_VOICE_ORIGIN_LOCAL: return mixer->listener_origin;
    case QA_VOICE_ORIGIN_FIXED: return origin.position;
    case QA_VOICE_ORIGIN_ENTITY:
      require_entity(origin.entity, (double)mixer->entity_capacity);
      return position_for_entity(mixer, origin.entity);
  }
  return mixer->listener_origin;
}

static qa_mixer_stereo_volume spatialize_origin(qa_audio_mixer *mixer,
                                                qa_vec3 position,
                                                double volume) {
  return transmit(mixer, position,
                  qa_spatialize_sound_origin(position, mixer->listener_origin,
                                             mixer->listener_axis, volume,
                                             mixer->output_channels));
}

static qa_mixer_stereo_volume spatialize(qa_audio_mixer *mixer, double entity,
                                         qa_voice_origin origin, double volume) {
  if (origin.kind == QA_VOICE_ORIGIN_LOCAL || entity == mixer->listener_entity) {
    qa_mixer_stereo_volume result = {volume, volume};
    return result;
  }
  return spatialize_origin(mixer, resolve_origin(mixer, origin), volume);
}

static qa_mixer_stereo_volume policy_spatialize(qa_audio_mixer *mixer,
                                                const one_shot_voice *voice) {
  if (voice->has_policy && voice->policy.role == ROLE_AMBIENT)
    return voice->stereo;
  if (voice->origin.kind == QA_VOICE_ORIGIN_LOCAL ||
      voice->entity == mixer->listener_entity) {
    qa_mixer_stereo_volume result = {voice->volume, voice->volume};
    return result;
  }
  qa_vec3 position = resolve_origin(mixer, voice->origin);
  qa_vec3 delta = qa_sub3(position, mixer->listener_origin);
  double distance = qa_length3(delta);
  double pan =
      distance == 0 ? 0 : -qa_dot3(delta, mixer->listener_axis.vectors[1]) / distance;
  if (!voice->has_policy)
    plain_error("Source spatialization requires a voice policy");
  const voice_policy *policy = &voice->policy;
  double beyond = distance - policy->distance_offset;
  double gain = voice->volume * (1 - (beyond > 0 ? beyond : 0) * policy->attenuation);
  bool mono = mixer->output_channels == 1 ||
              (policy->unattenuated_mono && policy->attenuation == 0);
  qa_mixer_stereo_volume volume;
  volume.left = qa_js_max(0, trunc(gain * (mono ? 1 : policy->stereo_scale * (1 - pan))));
  volume.right = qa_js_max(0, trunc(gain * (mono ? 1 : policy->stereo_scale * (1 + pan))));
  return policy->attenuation == 0 ? volume : transmit(mixer, position, volume);
}

static void replace_channel(qa_audio_mixer *mixer, double entity,
                            qa_sound_channel_command command,
                            bool cancel_scheduled,
                            qa_audio_voice_stop_reason reason) {
  for (size_t i = 0; i < mixer->voice_count; i++) {
    const one_shot_voice *voice = &mixer->voices[i];
    if (!voice->occupied) continue;
    if (!(cancel_scheduled || voice->start_kind != START_SCHEDULED)) continue;
    if (voice->entity != entity) continue;
    if (voice->has_policy && voice->policy.role != ROLE_EFFECT) continue;
    bool match = command.kind == QA_SOUND_CHANNEL_REPLACE_ACTOR ||
                 (command.kind == QA_SOUND_CHANNEL_CHANNEL &&
                  voice->has_channel &&
                  strcmp(voice->channel, command.channel) == 0);
    if (!match) continue;
    free_channel_reason(mixer, i, reason);
    if (command.kind == QA_SOUND_CHANNEL_REPLACE_ACTOR) return;
  }
}

qa_audio_mixer *qa_audio_mixer_create(double output_rate,
                                      qa_audio_milliseconds_fn milliseconds,
                                      void *milliseconds_ctx, double capacity,
                                      int output_channels,
                                      double entity_capacity) {
  require_positive_integer(output_rate, "output rate");
  require_positive_integer(capacity, "channel capacity");
  require_positive_integer(entity_capacity, "entity capacity");
  qa_audio_mixer *mixer = calloc(1, sizeof *mixer);
  if (mixer == NULL) return NULL;
  mixer->entity_positions =
      calloc((size_t)entity_capacity, sizeof *mixer->entity_positions);
  mixer->voices = calloc((size_t)capacity, sizeof *mixer->voices);
  mixer->free_channels = malloc((size_t)capacity * sizeof *mixer->free_channels);
  if (mixer->entity_positions == NULL || mixer->voices == NULL ||
      mixer->free_channels == NULL) {
    free(mixer->entity_positions);
    free(mixer->voices);
    free(mixer->free_channels);
    free(mixer);
    return NULL;
  }
  mixer->output_rate = (int64_t)output_rate;
  mixer->capacity = (int64_t)capacity;
  mixer->output_channels = output_channels;
  mixer->entity_capacity = (int64_t)entity_capacity;
  mixer->milliseconds = milliseconds;
  mixer->milliseconds_ctx = milliseconds_ctx;
  mixer->effects_volume = (double)0.8f;
  mixer->doppler_enabled = true;
  mixer->enabled = true;
  mixer->listener_origin = qa_math_vec3(0, 0, 0);
  mixer->listener_axis.vectors[0] = qa_math_vec3(1, 0, 0);
  mixer->listener_axis.vectors[1] = qa_math_vec3(0, 1, 0);
  mixer->listener_axis.vectors[2] = qa_math_vec3(0, 0, 1);
  mixer->voice_count = (size_t)capacity;
  mixer->voice_capacity = (size_t)capacity;
  mixer->free_capacity = (size_t)capacity;
  for (size_t i = 0; i < (size_t)capacity; i++) mixer->free_channels[i] = i;
  mixer->free_count = (size_t)capacity;
  return mixer;
}

static void clear_loop_channels(qa_audio_mixer *mixer) {
  for (size_t i = 0; i < mixer->loop_channel_count; i++)
    prepared_release(mixer->loop_channels[i].prepared);
  mixer->loop_channel_count = 0;
}

static void clear_loops(qa_audio_mixer *mixer) {
  for (size_t i = 0; i < mixer->loop_count; i++)
    prepared_release(mixer->loops[i].prepared);
  mixer->loop_count = 0;
}

void qa_audio_mixer_destroy(qa_audio_mixer *mixer) {
  if (mixer == NULL) return;
  for (size_t i = 0; i < mixer->voice_count; i++)
    if (mixer->voices[i].occupied)
      prepared_release(mixer->voices[i].prepared);
  clear_loop_channels(mixer);
  clear_loops(mixer);
  free(mixer->voices);
  free(mixer->free_channels);
  free(mixer->loops);
  free(mixer->loop_channels);
  free(mixer->entity_positions);
  free(mixer->transmission_cache);
  free(mixer);
}

int64_t qa_audio_mixer_output_rate(const qa_audio_mixer *mixer) {
  return mixer->output_rate;
}
int64_t qa_audio_mixer_capacity(const qa_audio_mixer *mixer) {
  return mixer->capacity;
}
int qa_audio_mixer_output_channels(const qa_audio_mixer *mixer) {
  return mixer->output_channels;
}
int64_t qa_audio_mixer_sample_clock(const qa_audio_mixer *mixer) {
  return mixer->painted_time;
}
int64_t qa_audio_mixer_sound_clock(const qa_audio_mixer *mixer) {
  return mixer->sound_time;
}
bool qa_audio_mixer_playback_enabled(const qa_audio_mixer *mixer) {
  return mixer->enabled;
}
void qa_audio_mixer_set_playback_enabled(qa_audio_mixer *mixer, bool enabled) {
  mixer->enabled = enabled;
}
int64_t qa_audio_mixer_raw_end(const qa_audio_mixer *mixer) {
  return mixer->raw_end_time;
}
void qa_audio_mixer_bind_sound_cvars(qa_audio_mixer *mixer,
                                     qa_cvar_registry *cvars) {
  mixer->sound_cvars = cvars;
}
void qa_audio_mixer_bind_console_output(qa_audio_mixer *mixer,
                                        qa_mixer_print_fn print, void *ctx) {
  mixer->console_print = print;
  mixer->console_ctx = ctx;
}
void qa_audio_mixer_set_voice_observer(qa_audio_mixer *mixer,
                                       qa_mixer_voice_observer_fn observer,
                                       void *observer_ctx,
                                       qa_mixer_voice_id_fn allocate_id,
                                       void *allocate_ctx) {
  mixer->voice_observer = observer;
  mixer->voice_observer_ctx = observer_ctx;
  if (allocate_id != NULL) {
    mixer->allocate_voice_id = allocate_id;
    mixer->allocate_voice_id_ctx = allocate_ctx;
  }
}
void qa_audio_mixer_set_geometry_transmission(
    qa_audio_mixer *mixer, qa_mixer_transmission_fn transmission, void *ctx) {
  mixer->transmission = transmission;
  mixer->transmission_ctx = ctx;
  mixer->transmission_cache_count = 0;
}

qa_mixer_channel_volume *qa_audio_mixer_channel_volumes(
    const qa_audio_mixer *mixer, size_t *out_count) {
  size_t count = 0;
  for (size_t i = 0; i < mixer->voice_count; i++) {
    const one_shot_voice *voice = &mixer->voices[i];
    if (voice->occupied && voice->start_kind != START_SCHEDULED &&
        !(voice->stereo.left == 0 && voice->stereo.right == 0))
      count++;
  }
  qa_mixer_channel_volume *out = NULL;
  if (count > 0) {
    out = malloc(count * sizeof *out);
    if (out == NULL) {
      *out_count = 0;
      return NULL;
    }
  }
  size_t at = 0;
  for (size_t i = 0; i < mixer->voice_count; i++) {
    const one_shot_voice *voice = &mixer->voices[i];
    if (!voice->occupied || voice->start_kind == START_SCHEDULED ||
        (voice->stereo.left == 0 && voice->stereo.right == 0))
      continue;
    out[at].sound = voice->prepared->sound;
    out[at].left = voice->stereo.left;
    out[at].right = voice->stereo.right;
    at++;
  }
  *out_count = count;
  return out;
}

void qa_audio_mixer_select_time(qa_audio_mixer *mixer, double sound_time,
                                double paint_time) {
  if (!is_safe_integer(sound_time) || sound_time < (double)mixer->sound_time ||
      !is_safe_integer(paint_time))
    qa_range_error(
        "sound time must advance monotonically and paint time must be a "
        "safe integer");
  mixer->sound_time = (int64_t)sound_time;
  mixer->painted_time = (int64_t)paint_time;
}

double qa_audio_mixer_rebase_time(qa_audio_mixer *mixer,
                                  double delivered_time) {
  if (!is_safe_integer(delivered_time) ||
      delivered_time <= (double)mixer->sound_time ||
      delivered_time < (double)QA_SOUND_TIME_EPOCH)
    qa_range_error(
        "sound epoch rebase requires advancing delivery beyond a complete "
        "epoch");
  double offset =
      trunc(delivered_time / (double)QA_SOUND_TIME_EPOCH) *
      (double)QA_SOUND_TIME_EPOCH;
  double paint_time = (double)mixer->painted_time - offset;
  if (!is_safe_integer(paint_time))
    qa_range_error("rebased sound paint time must be a safe integer");
  mixer->sound_time = (int64_t)(delivered_time - offset);
  mixer->painted_time = (int64_t)paint_time;
  return (double)mixer->sound_time;
}

static int compare_loop_indices(const void *left, const void *right,
                                void *ctx) {
  const loop_voice *loops = ctx;
  double a = loops[*(const size_t *)left].entity;
  double b = loops[*(const size_t *)right].entity;
  return a < b ? -1 : a > b ? 1 : 0;
}

static void collect_loop_mixes(qa_audio_mixer *mixer) {
  clear_loop_channels(mixer);
  size_t active_count = 0;
  for (size_t i = 0; i < mixer->loop_count; i++)
    if (mixer->loops[i].active) active_count++;
  size_t *sorted = xmalloc(active_count * sizeof *sorted);
  size_t at = 0;
  for (size_t i = 0; i < mixer->loop_count; i++)
    if (mixer->loops[i].active) sorted[at++] = i;
  /* Insertion sort by entity (entities are unique loop keys). */
  for (size_t i = 1; i < active_count; i++) {
    size_t value = sorted[i];
    size_t j = i;
    while (j > 0 && compare_loop_indices(&value, &sorted[j - 1],
                                         mixer->loops) < 0) {
      sorted[j] = sorted[j - 1];
      j--;
    }
    sorted[j] = value;
  }
  bool *merged = xcalloc(active_count, sizeof *merged);
  for (size_t i = 0; i < active_count; i++) {
    if (merged[i]) continue;
    loop_voice *loop = &mixer->loops[sorted[i]];
    qa_mixer_stereo_volume volume =
        spatialize_origin(mixer, position_for_entity(mixer, loop->entity),
                          loop->volume);
    double left = volume.left, right = volume.right;
    for (size_t j = i + 1; j < active_count; j++) {
      loop_voice *candidate = &mixer->loops[sorted[j]];
      if (candidate->doppler ||
          candidate->prepared->sound != loop->prepared->sound)
        continue;
      merged[j] = true;
      qa_mixer_stereo_volume candidate_volume = spatialize_origin(
          mixer, position_for_entity(mixer, candidate->entity),
          candidate->volume);
      left += candidate_volume.left;
      right += candidate_volume.right;
    }
    if (left == 0 && right == 0) continue;
    if (mixer->loop_channel_count == mixer->loop_channel_capacity) {
      mixer->loop_channel_capacity =
          mixer->loop_channel_capacity == 0 ? 8
                                            : mixer->loop_channel_capacity * 2;
      mixer->loop_channels = xrealloc(
          mixer->loop_channels,
          mixer->loop_channel_capacity * sizeof *mixer->loop_channels);
    }
    loop_mix *mix = &mixer->loop_channels[mixer->loop_channel_count++];
    mix->prepared = prepared_retain(loop->prepared);
    mix->left_volume = qa_js_min(255, left);
    mix->right_volume = qa_js_min(255, right);
    mix->doppler = loop->doppler;
    mix->doppler_scale = loop->doppler_scale;
    mix->old_doppler_scale = loop->old_doppler_scale;
  }
  free(sorted);
  free(merged);
}

void qa_audio_mixer_set_listener(qa_audio_mixer *mixer, double entity,
                                 qa_vec3 origin, qa_axis axis) {
  mixer->transmission_cache_count = 0;
  if (!mixer->enabled) return;
  if (!is_integer(entity) || !int32_is(entity))
    qa_range_error("listener entity must be a signed 32-bit integer");
  mixer->listener_entity = entity;
  mixer->listener_origin = qa_math_vec3(origin.x, origin.y, origin.z);
  for (int i = 0; i < 3; i++)
    mixer->listener_axis.vectors[i] = qa_math_vec3(
        axis.vectors[i].x, axis.vectors[i].y, axis.vectors[i].z);
  for (size_t i = 0; i < mixer->voice_count; i++) {
    one_shot_voice *voice = &mixer->voices[i];
    if (voice->occupied && voice->start_kind != START_SCHEDULED)
      voice->stereo = voice->has_policy
                          ? policy_spatialize(mixer, voice)
                          : spatialize(mixer, voice->entity, voice->origin,
                                       voice->volume);
  }
  collect_loop_mixes(mixer);
}

/* TS entityActive: an active loop keyed by the entity, or any live voice. */
bool qa_audio_mixer_entity_active(const qa_audio_mixer *mixer, double entity) {
  for (size_t i = 0; i < mixer->loop_count; i++)
    if (mixer->loops[i].entity == entity && mixer->loops[i].active) return true;
  for (size_t i = 0; i < mixer->voice_count; i++)
    if (mixer->voices[i].occupied && mixer->voices[i].entity == entity) return true;
  return false;
}

void qa_audio_mixer_update_entity_position(qa_audio_mixer *mixer, double entity,
                                           qa_vec3 origin) {
  require_entity(entity, (double)mixer->entity_capacity);
  mixer->entity_positions[(size_t)entity] =
      qa_math_vec3(origin.x, origin.y, origin.z);
}

void qa_audio_mixer_set_effects_volume(qa_audio_mixer *mixer, double volume) {
  double gain = trunc(fround_(fround_(volume) * 255));
  if (!is_integer(gain) || gain < -2147483648.0 || gain > 2147483647.0)
    qa_range_error(
        "effects volume has an undefined signed-int gain conversion");
  mixer->effects_volume = fround_(volume);
}

void qa_audio_mixer_set_music_volume(qa_audio_mixer *mixer, double volume) {
  (void)mixer; /* TS keeps only the validation now that queueMusic is gone. */
  require_gain(volume, "music volume");
}

void qa_audio_mixer_set_doppler_enabled(qa_audio_mixer *mixer, bool enabled) {
  mixer->doppler_enabled = enabled;
}

bool qa_audio_mixer_start_shared_sound(qa_audio_mixer *mixer,
                                       const qa_pcm_sound *sound,
                                       qa_start_sound_options options,
                                       qa_sound_channel_command command,
                                       const char *source_name,
                                       const qa_sound_asset *asset) {
  if (!mixer->enabled) return false;
  validate_sound(sound, false);
  if (options.origin.kind == QA_VOICE_ORIGIN_FIXED) {
    if (!is_integer(options.entity) || !int32_is(options.entity))
      qa_range_error("fixed-origin entity must be a signed 32-bit integer");
  } else if (!is_integer(options.entity) || options.entity < 0 ||
             options.entity > (double)mixer->entity_capacity) {
    char text[64];
    fmt_num(text, sizeof text, (double)mixer->entity_capacity);
    qa_range_error("entity must be an integer from 0 through %s", text);
  }
  require_channel_volume(options.volume);
  prepared_sound *prepared = prepare_sound(mixer, sound);
  if (diagnostic_setting(mixer, "s_show") == 1) {
    if (source_name == NULL || mixer->console_print == NULL)
      plain_error(
          "Sound start diagnostics require a registered name and console "
          "output");
    char text[1024];
    snprintf(text, sizeof text, "%lld : %s\n",
             (long long)mixer->painted_time, source_name);
    mixer->console_print(text, mixer->console_ctx);
    if (!mixer->enabled)
      plain_error("Sound playback ended during start diagnostics");
  }
  int64_t time = allocation_time(mixer);
  int64_t same_count = 0;
  for (size_t i = 0; i < mixer->voice_count; i++) {
    const one_shot_voice *voice = &mixer->voices[i];
    if (!voice->occupied || voice->has_policy ||
        voice->entity != options.entity || voice->prepared->sound != sound)
      continue;
    if (qa_int32((double)(time - voice->allocated_at)) < 50) {
      prepared_release(prepared);
      return false;
    }
    same_count++;
  }
  int64_t allowed = options.entity == mixer->listener_entity ? 8 : 4;
  if (same_count > allowed) {
    prepared_release(prepared);
    return false;
  }
  replace_channel(mixer, options.entity, command, false,
                  QA_AUDIO_VOICE_STOP_REPLACED);
  size_t index = 0;
  bool reused = false;
  if (!acquire_channel(mixer, options.entity, &index, &reused)) {
    prepared_release(prepared);
    return false;
  }
  int64_t allocated_at = reused ? allocation_time(mixer) : time;
  one_shot_voice *voice = &mixer->voices[index];
  memset(voice, 0, sizeof *voice);
  voice->occupied = true;
  voice->voice_id = allocate_voice_id(mixer);
  voice->asset = asset;
  voice->notification = NOTIFY_PENDING;
  voice->prepared = prepared;
  voice->entity = options.entity;
  if (command.kind == QA_SOUND_CHANNEL_CHANNEL) {
    voice->has_channel = true;
    snprintf(voice->channel, sizeof voice->channel, "%s", command.channel);
  }
  voice->origin = options.origin;
  if (options.origin.kind == QA_VOICE_ORIGIN_FIXED)
    voice->origin.position = qa_math_vec3(options.origin.position.x,
                                           options.origin.position.y,
                                           options.origin.position.z);
  voice->volume = options.volume;
  voice->stereo.left = options.volume;
  voice->stereo.right = options.volume;
  voice->start_kind = START_PENDING;
  voice->allocated_at = allocated_at;
  return true;
}

bool qa_audio_mixer_start_sound(qa_audio_mixer *mixer,
                                const qa_pcm_sound *sound,
                                qa_start_sound_options options,
                                const char *source_name) {
  if (!mixer->enabled) return false;
  require_channel(options.channel);
  return qa_audio_mixer_start_shared_sound(
      mixer, sound, options,
      qa_source_sound_channel(QA_SOUND_FAMILY_Q3, (int64_t)options.channel),
      source_name, NULL);
}

bool qa_audio_mixer_start_local_sound(qa_audio_mixer *mixer,
                                      const qa_pcm_sound *sound,
                                      double channel, const char *source_name) {
  qa_start_sound_options options = {0};
  options.entity = mixer->listener_entity;
  options.channel = channel;
  options.origin.kind = QA_VOICE_ORIGIN_ENTITY;
  options.origin.entity = mixer->listener_entity;
  options.volume = 127;
  return qa_audio_mixer_start_sound(mixer, sound, options, source_name);
}

typedef struct scheduled_start {
  int64_t sample;
  int64_t order;
} scheduled_start;

static bool admit_source_sound(qa_audio_mixer *mixer, const qa_pcm_sound *sound,
                               double entity, qa_voice_origin origin,
                               double volume, double attenuation,
                               qa_sound_channel_command command,
                               voice_policy policy, qa_audio_random_fn random,
                               void *random_ctx,
                               const scheduled_start *scheduled,
                               const qa_sound_asset *asset) {
  if (!mixer->enabled) return false;
  validate_sound(sound, false);
  if (sound->channels != 1 || sound->frame_count < 1)
    plain_error("Source effects require nonempty mono PCM");
  if (!isfinite(volume) || volume < 0 || !isfinite(attenuation) ||
      attenuation < 0)
    qa_range_error("Invalid source sound gain or attenuation");
  double ratio = (double)sound->sample_rate / (double)mixer->output_rate;
  prepared_sound *prepared = prepared_new(sound);
  prepared->step256 = ratio * 256;
  prepared->output_frames = trunc((double)sound->frame_count / ratio);
  if (prepared->output_frames < 1)
    plain_error("Sound resamples to zero frames");
  bool has_marker = policy.has_loop_start || sound->loop_start != -1;
  double marker = policy.has_loop_start
                      ? policy.loop_start
                      : trunc((double)sound->loop_start / ratio);
  if (has_marker && (marker < 0 || marker >= prepared->output_frames))
    qa_range_error("Sound loop outside PCM");
  double offset = 0;
  if (random != NULL) {
    bool same_frame = false;
    for (size_t i = 0; i < mixer->voice_count; i++) {
      const one_shot_voice *voice = &mixer->voices[i];
      if (voice->occupied && voice->has_policy &&
          voice->policy.role == ROLE_EFFECT &&
          voice->prepared->sound == sound &&
          voice->start_kind == START_STARTED &&
          voice->start_sample == mixer->painted_time)
        same_frame = true;
    }
    if (same_frame) {
      double value = random(random_ctx);
      if (!is_integer(value) || value < 0)
        qa_range_error(
            "Sound random source must return a nonnegative integer");
      double span = trunc(0.1 * (double)mixer->output_rate);
      if (span < 1) span = 1;
      offset = qa_js_min(prepared->output_frames - 1, fmod(value, span));
    }
  }
  one_shot_voice voice = {0};
  voice.occupied = true;
  voice.voice_id = allocate_voice_id(mixer);
  voice.asset = asset;
  voice.notification = NOTIFY_PENDING;
  voice.prepared = prepared;
  voice.entity = entity;
  if (command.kind == QA_SOUND_CHANNEL_CHANNEL) {
    voice.has_channel = true;
    snprintf(voice.channel, sizeof voice.channel, "%s", command.channel);
  }
  voice.origin = origin;
  voice.volume = trunc(volume * 255);
  if (scheduled != NULL) {
    voice.start_kind = START_SCHEDULED;
    voice.start_sample = scheduled->sample;
    voice.start_order = scheduled->order;
  } else {
    voice.start_kind = START_STARTED;
    voice.start_sample = policy.role == ROLE_ENTITY_LOOP
                             ? 0
                             : mixer->painted_time - (int64_t)offset;
  }
  voice.allocated_at = allocation_time(mixer);
  policy.has_loop_start = has_marker;
  policy.loop_start = marker;
  voice.has_policy = true;
  voice.policy = policy;
  if (scheduled == NULL) voice.stereo = policy_spatialize(mixer, &voice);
  if (scheduled == NULL && policy.role == ROLE_EFFECT &&
      voice.stereo.left == 0 && voice.stereo.right == 0) {
    prepared_release(prepared);
    return false;
  }
  if (scheduled == NULL)
    replace_channel(mixer, entity, command, false, QA_AUDIO_VOICE_STOP_REPLACED);
  size_t index = 0;
  bool reused = false;
  if (!acquire_channel(mixer, voice.entity, &index, &reused)) {
    prepared_release(prepared);
    return false;
  }
  mixer->voices[index] = voice;
  voice_started(mixer, &mixer->voices[index]);
  return true;
}

bool qa_audio_mixer_start_q1_sound(qa_audio_mixer *mixer,
                                   const qa_pcm_sound *sound,
                                   qa_source_sound_options options,
                                   qa_sound_channel_command command,
                                   qa_audio_random_fn random, void *random_ctx,
                                   const qa_sound_asset *asset) {
  voice_policy policy = {0};
  policy.attenuation = options.attenuation / 1000;
  policy.stereo_scale = 1;
  policy.role = ROLE_EFFECT;
  return admit_source_sound(mixer, sound, options.entity, options.origin,
                            options.volume, options.attenuation, command,
                            policy, random, random_ctx, NULL, asset);
}

bool qa_audio_mixer_start_q2_sound(qa_audio_mixer *mixer,
                                   const qa_pcm_sound *sound,
                                   qa_source_sound_options options,
                                   qa_sound_channel_command command,
                                   const qa_sound_asset *asset) {
  if (!mixer->enabled) return false;
  double delay = options.has_delay_seconds ? options.delay_seconds : 0;
  double server = (options.has_server_milliseconds
                       ? options.server_milliseconds
                       : (double)mixer->painted_time * 1000 /
                             (double)mixer->output_rate) *
                  0.001 * (double)mixer->output_rate;
  if (!isfinite(delay) || !isfinite(server))
    qa_range_error("Invalid source sound timestamp");
  double offset = mixer->source_begin_offset;
  double begin = trunc(server + offset);
  if (begin < (double)mixer->painted_time) {
    begin = (double)mixer->painted_time;
    offset = trunc(begin - server);
  } else if (begin > (double)mixer->painted_time +
                         0.3 * (double)mixer->output_rate) {
    begin = trunc((double)mixer->painted_time +
                  0.1 * (double)mixer->output_rate);
    offset = trunc(begin - server);
  } else {
    offset -= 10;
  }
  begin = delay == 0 ? (double)mixer->painted_time
                     : trunc(begin + delay * (double)mixer->output_rate);
  if (!is_safe_integer(begin))
    qa_range_error("Sound deadline is outside the shared clock");
  voice_policy policy = {0};
  policy.attenuation =
      options.attenuation * (options.attenuation == 3 ? 0.001 : 0.0005);
  policy.distance_offset = 80;
  policy.stereo_scale = 0.5;
  policy.unattenuated_mono = true;
  policy.role = ROLE_EFFECT;
  scheduled_start scheduled = {(int64_t)begin, mixer->source_schedule_order};
  bool accepted = admit_source_sound(mixer, sound, options.entity,
                                     options.origin, options.volume,
                                     options.attenuation, command, policy,
                                     NULL, NULL, &scheduled, asset);
  if (accepted) {
    mixer->source_begin_offset = offset;
    mixer->source_schedule_order++;
  }
  return accepted;
}

static qa_sound_channel_command auto_command(void) {
  qa_sound_channel_command command = {0};
  command.kind = QA_SOUND_CHANNEL_AUTO;
  return command;
}

bool qa_audio_mixer_add_static_sound(qa_audio_mixer *mixer,
                                     const qa_pcm_sound *sound,
                                     qa_vec3 origin, double volume,
                                     double attenuation, double key) {
  if (sound->loop_start == -1)
    plain_error("Static sound requires a WAV loop marker");
  qa_voice_origin voice_origin = {0};
  voice_origin.kind = QA_VOICE_ORIGIN_FIXED;
  voice_origin.position = origin;
  voice_policy policy = {0};
  policy.attenuation = attenuation / 64000;
  policy.stereo_scale = 1;
  policy.role = ROLE_STATIC;
  policy.key = key;
  return admit_source_sound(mixer, sound, -1, voice_origin, volume / 255,
                            attenuation, auto_command(), policy, NULL, NULL,
                            NULL, NULL);
}

void qa_audio_mixer_remove_static_sound(qa_audio_mixer *mixer, double key) {
  for (size_t i = 0; i < mixer->voice_count; i++) {
    const one_shot_voice *voice = &mixer->voices[i];
    if (voice->occupied && voice->has_policy &&
        voice->policy.role == ROLE_STATIC && voice->policy.key == key)
      free_channel(mixer, i);
  }
}

static int64_t find_ambient(qa_audio_mixer *mixer, double key) {
  for (size_t i = 0; i < mixer->voice_count; i++) {
    const one_shot_voice *voice = &mixer->voices[i];
    if (voice->occupied && voice->has_policy &&
        voice->policy.role == ROLE_AMBIENT && voice->policy.key == key)
      return (int64_t)i;
  }
  return -1;
}

void qa_audio_mixer_update_ambient(qa_audio_mixer *mixer,
                                   const qa_pcm_sound *const *sounds,
                                   size_t sound_count, const double *levels,
                                   size_t level_count, double elapsed_seconds,
                                   double level, double fade) {
  if (!mixer->enabled) return;
  for (int key = 0; key < 2; key++) {
    const qa_pcm_sound *sound =
        (size_t)key < sound_count ? sounds[key] : NULL;
    bool has_amount = (size_t)key < level_count;
    double amount = has_amount ? levels[key] : 0;
    int64_t index = find_ambient(mixer, key);
    if (sound == NULL || !has_amount || level == 0) {
      if (index >= 0) free_channel(mixer, (size_t)index);
      continue;
    }
    if (index < 0 || mixer->voices[index].prepared->sound != sound) {
      if (index >= 0) free_channel(mixer, (size_t)index);
      qa_voice_origin origin = {0};
      origin.kind = QA_VOICE_ORIGIN_LOCAL;
      voice_policy policy = {0};
      policy.stereo_scale = 1;
      policy.has_loop_start = true;
      policy.loop_start = 0;
      policy.role = ROLE_AMBIENT;
      policy.key = key;
      (void)admit_source_sound(mixer, sound, -1, origin, 0, 0, auto_command(),
                               policy, NULL, NULL, NULL, NULL);
      index = find_ambient(mixer, key);
    }
    if (index < 0) plain_error("Ambient voice admission failed");
    one_shot_voice *voice = &mixer->voices[index];
    double target = level * amount < 8 ? 0 : level * amount;
    double current = voice->stereo.left;
    double step = qa_js_max(0, elapsed_seconds * fade);
    double gain = current < target ? qa_js_min(target, current + step)
                                   : qa_js_max(target, current - step);
    voice->stereo.left = gain;
    voice->stereo.right = gain;
  }
}

void qa_audio_mixer_set_source_loop_sounds(qa_audio_mixer *mixer,
                                           const qa_source_loop_sound *entries,
                                           size_t count) {
  for (size_t i = 0; i < mixer->voice_count; i++) {
    const one_shot_voice *voice = &mixer->voices[i];
    if (voice->occupied && voice->has_policy &&
        voice->policy.role == ROLE_ENTITY_LOOP)
      free_channel(mixer, i);
  }
  for (size_t i = 0; i < count; i++) {
    const qa_source_loop_sound *entry = &entries[i];
    double attenuation = entry->has_attenuation ? entry->attenuation : 1;
    bool q1 = entry->family == QA_SOUND_FAMILY_Q1;
    qa_voice_origin origin = {0};
    origin.kind = QA_VOICE_ORIGIN_FIXED;
    origin.position = entry->origin;
    voice_policy policy = {0};
    policy.attenuation = attenuation * (q1 ? 0.001 : 0.003);
    policy.distance_offset = q1 ? 0 : 80;
    policy.stereo_scale = q1 ? 1 : 0.5;
    policy.unattenuated_mono = !q1;
    policy.has_loop_start = true;
    policy.loop_start = 0;
    policy.has_gain_limit = !q1;
    policy.gain_limit = 255;
    policy.role = ROLE_ENTITY_LOOP;
    policy.key = entry->entity;
    (void)admit_source_sound(mixer, entry->sound, entry->entity, origin,
                             entry->volume, attenuation, auto_command(), policy,
                             NULL, NULL, NULL, NULL);
  }
}

static loop_voice *find_loop(qa_audio_mixer *mixer, double entity) {
  for (size_t i = 0; i < mixer->loop_count; i++)
    if (mixer->loops[i].entity == entity) return &mixer->loops[i];
  return NULL;
}

static void set_loop(qa_audio_mixer *mixer, loop_voice loop) {
  loop_voice *existing = find_loop(mixer, loop.entity);
  if (existing != NULL) {
    prepared_release(existing->prepared);
    *existing = loop;
    return;
  }
  if (mixer->loop_count == mixer->loop_capacity) {
    mixer->loop_capacity = mixer->loop_capacity == 0 ? 8 : mixer->loop_capacity * 2;
    mixer->loops = xrealloc(mixer->loops,
                            mixer->loop_capacity * sizeof *mixer->loops);
  }
  mixer->loops[mixer->loop_count++] = loop;
}

static bool doppler_cvar_enabled(qa_audio_mixer *mixer) {
  if (mixer->sound_cvars == NULL) return mixer->doppler_enabled;
  qa_cvar_snapshot snapshot;
  if (!qa_cvar_registry_get(mixer->sound_cvars, "s_doppler", &snapshot))
    plain_error("Missing sound cvar s_doppler");
  double value = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  return mixer->doppler_enabled && value != 0;
}

void qa_audio_mixer_update_looping_sound(
    qa_audio_mixer *mixer, const qa_pcm_sound *sound,
    qa_frame_looping_sound_options options) {
  if (!mixer->enabled) return;
  validate_sound(sound, false);
  require_entity(options.entity, (double)mixer->entity_capacity);
  require_frame_number(options.frame_number);
  double volume = options.has_volume ? options.volume : 127;
  require_channel_volume(volume);
  prepared_sound *prepared = prepare_sound(mixer, sound);
  if (prepared_frames(prepared) == 0)
    qa_range_error("loop sound has length 0");
  loop_voice *previous = find_loop(mixer, options.entity);
  bool has_previous = previous != NULL;
  double previous_frame = has_previous ? previous->frame_number : 0;
  qa_vec3 origin = qa_math_vec3(options.origin.x, options.origin.y,
                                options.origin.z);
  qa_vec3 velocity = qa_math_vec3(options.velocity.x, options.velocity.y,
                                  options.velocity.z);
  mixer->entity_positions[(size_t)options.entity] = origin;
  bool doppler = false;
  double doppler_scale = 1, old_doppler_scale = 1;
  if (doppler_cvar_enabled(mixer) && qa_dot3(velocity, velocity) > 0) {
    doppler = true;
    qa_vec3 listener_position =
        position_for_entity(mixer, mixer->listener_entity);
    qa_vec3 before = qa_sub3(listener_position, origin);
    qa_vec3 after = qa_sub3(listener_position, qa_add3(origin, velocity));
    double distance_before = qa_dot3(before, before);
    double distance_after = qa_dot3(after, after);
    /* The source resets dopplerScale to 1 before reading it into
     * oldDopplerScale, so the old scale is 1 whenever this runs. */
    if (has_previous &&
        (double)qa_int32(previous_frame + 1) == options.frame_number)
      old_doppler_scale = 1;
    doppler_scale =
        fround_(distance_after / fround_(distance_before * fround_(100)));
    if (!isfinite(doppler_scale)) doppler_scale = 1;
    if (doppler_scale <= 1) doppler = false;
  }
  loop_voice loop = {0};
  loop.prepared = prepared;
  loop.entity = options.entity;
  loop.velocity = velocity;
  loop.volume = volume;
  loop.persistent = false;
  loop.active = true;
  loop.doppler = doppler;
  loop.doppler_scale = doppler_scale;
  loop.old_doppler_scale = old_doppler_scale;
  loop.frame_number = options.frame_number;
  set_loop(mixer, loop);
}

void qa_audio_mixer_update_real_looping_sound(
    qa_audio_mixer *mixer, const qa_pcm_sound *sound,
    qa_real_looping_sound_options options) {
  if (!mixer->enabled) return;
  validate_sound(sound, false);
  require_entity(options.entity, (double)mixer->entity_capacity);
  double volume = options.has_volume ? options.volume : 90;
  require_channel_volume(volume);
  prepared_sound *prepared = prepare_sound(mixer, sound);
  if (prepared_frames(prepared) == 0)
    qa_range_error("loop sound has length 0");
  loop_voice *previous = find_loop(mixer, options.entity);
  loop_voice loop = {0};
  loop.prepared = prepared;
  loop.entity = options.entity;
  loop.velocity = qa_math_vec3(options.velocity.x, options.velocity.y,
                               options.velocity.z);
  loop.volume = volume;
  loop.persistent = true;
  loop.active = true;
  loop.doppler_scale = previous != NULL ? previous->doppler_scale : 1;
  loop.old_doppler_scale = previous != NULL ? previous->old_doppler_scale : 1;
  loop.frame_number = previous != NULL ? previous->frame_number : 0;
  mixer->entity_positions[(size_t)options.entity] =
      qa_math_vec3(options.origin.x, options.origin.y, options.origin.z);
  set_loop(mixer, loop);
}

void qa_audio_mixer_clear_looping_sounds(qa_audio_mixer *mixer, bool kill_all) {
  for (size_t i = 0; i < mixer->loop_count; i++) {
    loop_voice *loop = &mixer->loops[i];
    if (kill_all || !loop->persistent || prepared_frames(loop->prepared) == 0)
      loop->active = false;
  }
  clear_loop_channels(mixer);
}

void qa_audio_mixer_stop_looping_sound(qa_audio_mixer *mixer, double entity) {
  require_entity(entity, (double)mixer->entity_capacity);
  loop_voice *loop = find_loop(mixer, entity);
  if (loop != NULL) loop->active = false;
}

void qa_audio_mixer_stop_channel(qa_audio_mixer *mixer, double entity,
                                 double channel) {
  if (!is_integer(channel)) qa_range_error("Invalid source sound channel");
  replace_channel(
      mixer, entity,
      qa_source_sound_channel(QA_SOUND_FAMILY_Q3, (int64_t)channel), false,
      QA_AUDIO_VOICE_STOP_STOPPED);
}

void qa_audio_mixer_stop_shared_channel(qa_audio_mixer *mixer, double entity,
                                        const char *channel) {
  if (channel == NULL) {
    for (size_t i = 0; i < mixer->voice_count; i++) {
      const one_shot_voice *voice = &mixer->voices[i];
      if (voice->occupied && voice->entity == entity && !voice->has_channel &&
          (!voice->has_policy || voice->policy.role == ROLE_EFFECT)) {
        free_channel(mixer, i);
        return;
      }
    }
    return;
  }
  qa_sound_channel_command command = {0};
  command.kind = QA_SOUND_CHANNEL_CHANNEL;
  snprintf(command.channel, sizeof command.channel, "%s", channel);
  replace_channel(mixer, entity, command, true, QA_AUDIO_VOICE_STOP_STOPPED);
}

void qa_audio_mixer_stop_entity(qa_audio_mixer *mixer, double entity) {
  require_entity(entity, (double)mixer->entity_capacity);
  for (size_t i = 0; i < mixer->voice_count; i++)
    if (mixer->voices[i].occupied && mixer->voices[i].entity == entity)
      free_channel(mixer, i);
  qa_audio_mixer_stop_looping_sound(mixer, entity);
}

void qa_audio_mixer_clear_raw(qa_audio_mixer *mixer) {
  mixer->raw_end_time = mixer->painted_time;
}
void qa_audio_mixer_reset_raw_to_sound_time(qa_audio_mixer *mixer) {
  mixer->raw_end_time = mixer->sound_time;
}
void qa_audio_mixer_stop_raw(qa_audio_mixer *mixer) {
  mixer->raw_end_time = 0;
}

static void fill_positions_zero(qa_audio_mixer *mixer) {
  memset(mixer->entity_positions, 0,
         (size_t)mixer->entity_capacity * sizeof *mixer->entity_positions);
}

void qa_audio_mixer_stop_all(qa_audio_mixer *mixer) {
  reset_channels(mixer);
  mixer->source_begin_offset = 0;
  mixer->source_schedule_order = 0;
  clear_loops(mixer);
  clear_loop_channels(mixer);
  fill_positions_zero(mixer);
  qa_audio_mixer_clear_raw(mixer);
}

void qa_audio_mixer_clear_sound_buffer(qa_audio_mixer *mixer) {
  clear_loops(mixer);
  clear_loop_channels(mixer);
  fill_positions_zero(mixer);
  reset_channels(mixer);
  mixer->raw_end_time = 0;
}

/* ---- Raw streaming ---- */

static double checked_sample(const qa_pcm_sound *sound, int64_t index) {
  if (index < 0 || index >= (int64_t)sound->sample_count) {
    char at[64], total[64];
    fmt_num(at, sizeof at, (double)index);
    fmt_num(total, sizeof total, (double)sound->sample_count);
    plain_error("PCM sample index %s is outside %s samples", at, total);
  }
  return sound->samples[index];
}

static int32_t imul32(double left, double right) {
  return (int32_t)((uint32_t)qa_int32(left) * (uint32_t)qa_int32(right));
}

static int32_t raw_integer_volume(double volume) {
  double integer = trunc(fround_(256 * fround_(volume)));
  if (!is_integer(integer) || !int32_is(integer))
    qa_range_error("raw volume has an undefined signed-int gain conversion");
  return (int32_t)integer;
}

static void begin_raw_write(qa_audio_mixer *mixer) {
  if (mixer->raw_end_time < mixer->sound_time) {
    char text[160];
    snprintf(text, sizeof text, "S_RawSamples: resetting minimum: %lld < %lld\n",
             (long long)mixer->raw_end_time, (long long)mixer->sound_time);
    raw_debug_print(mixer, text);
    mixer->raw_end_time = mixer->sound_time;
  }
}

static void raw_overflow_check(qa_audio_mixer *mixer) {
  if (mixer->raw_end_time > mixer->sound_time + RAW_CAPACITY) {
    char text[160];
    snprintf(text, sizeof text, "S_RawSamples: overflowed %lld > %lld\n",
             (long long)mixer->raw_end_time, (long long)mixer->sound_time);
    raw_debug_print(mixer, text);
  }
}

static int64_t raw_output_frames(qa_audio_mixer *mixer,
                                 const qa_pcm_sound *sound) {
  double scale = fround_((double)sound->sample_rate / (double)mixer->output_rate);
  if (sound->channels == 2 && scale == 1) return sound->frame_count;
  double output = ceil((double)sound->frame_count / scale);
  if (!is_safe_integer(output))
    qa_range_error("resampled raw PCM frame count is outside the supported range");
  while (output > 0 &&
         trunc(fround_((output - 1) * scale)) >= (double)sound->frame_count)
    output--;
  while (trunc(fround_(output * scale)) < (double)sound->frame_count) {
    output++;
    if (!is_safe_integer(output))
      qa_range_error(
          "resampled raw PCM frame count is outside the supported range");
  }
  return (int64_t)output;
}

static void append_raw(qa_audio_mixer *mixer, const qa_pcm_sound *sound,
                       int32_t integer_volume, int64_t count) {
  if (!is_safe_integer((double)mixer->raw_end_time + (double)count))
    qa_range_error("raw PCM end frame is outside the supported range");
  double scale = fround_((double)sound->sample_rate / (double)mixer->output_rate);
  for (int64_t offset = 0; offset < count; offset++) {
    int64_t source_frame = sound->channels == 2 && scale == 1
                               ? offset
                               : (int64_t)trunc(fround_((double)offset * scale));
    int64_t destination = mixer->raw_end_time & (RAW_CAPACITY - 1);
    mixer->raw_end_time += 1;
    int64_t source_index = source_frame * sound->channels;
    double left = checked_sample(sound, source_index);
    double right =
        sound->channels == 1 ? left : checked_sample(sound, source_index + 1);
    mixer->raw_samples[destination * 2] = imul32(left, integer_volume);
    mixer->raw_samples[destination * 2 + 1] = imul32(right, integer_volume);
  }
}

void qa_audio_mixer_queue_raw(qa_audio_mixer *mixer, const qa_pcm_sound *sound,
                              double volume) {
  if (!mixer->enabled) return;
  validate_sound(sound, true);
  require_gain(volume, "raw volume");
  int32_t integer_volume = raw_integer_volume(volume);
  begin_raw_write(mixer);
  append_raw(mixer, sound, integer_volume, raw_output_frames(mixer, sound));
  raw_overflow_check(mixer);
}

/* ---- Painting ---- */

static double checked_paint(const double *paint, size_t length, int64_t index) {
  if (index < 0 || index >= (int64_t)length) {
    char at[64], total[64];
    fmt_num(at, sizeof at, (double)index);
    fmt_num(total, sizeof total, (double)length);
    plain_error("paint index %s is outside %s values", at, total);
  }
  return paint[index];
}
static void add_paint(double *paint, size_t length, int64_t index,
                      double contribution) {
  paint[index] = checked_paint(paint, length, index) + contribution;
}
static void add_float_paint(double *paint, size_t length, int64_t index,
                            double contribution) {
  paint[index] = checked_paint(paint, length, index) + trunc(contribution);
}
static double checked_raw(qa_audio_mixer *mixer, int64_t index) {
  if (index < 0 || index >= RAW_CAPACITY * 2) {
    char at[64];
    fmt_num(at, sizeof at, (double)index);
    plain_error("raw sample index %s is outside %d values", at,
                RAW_CAPACITY * 2);
  }
  return mixer->raw_samples[index];
}

static double effect_sample(prepared_sound *prepared, int64_t output_frame) {
  int64_t source_frame =
      (int64_t)trunc((double)output_frame * prepared->step256 / 256);
  return checked_sample(prepared->sound, source_frame);
}

static void paint_effect(double *paint, size_t length, int64_t output_frame,
                         double sample, qa_mixer_stereo_volume volume,
                         double effects_gain) {
  add_paint(paint, length, output_frame * 2,
            floor(sample * (volume.left * effects_gain) / 256));
  add_paint(paint, length, output_frame * 2 + 1,
            floor(sample * (volume.right * effects_gain) / 256));
}

static double doppler_sample(prepared_sound *prepared, int64_t chunk,
                             int64_t sample_offset) {
  int64_t output_frame =
      chunk * SND_CHUNK_SIZE + (sample_offset & (SND_CHUNK_SIZE - 1));
  if ((double)output_frame >= prepared_frames(prepared)) return 0;
  return effect_sample(prepared, output_frame);
}

static void paint_wide_doppler_loop(qa_audio_mixer *mixer, double *paint,
                                    size_t length, int64_t output_frame,
                                    int64_t count, int64_t source_offset,
                                    const loop_mix *loop, double effects_gain) {
  (void)mixer;
  prepared_sound *prepared = loop->prepared;
  int64_t frames = (int64_t)prepared_frames(prepared);
  int64_t period =
      (int64_t)ceil((double)frames / SND_CHUNK_SIZE) * SND_CHUNK_SIZE;
  if (prepared->doppler_sums == NULL ||
      prepared->doppler_sums_length != (size_t)(period + 1)) {
    free(prepared->doppler_sums);
    prepared->doppler_sums = xcalloc((size_t)(period + 1), sizeof(double));
    prepared->doppler_sums_length = (size_t)(period + 1);
    double total = 0;
    for (int64_t frame = 0; frame < period; frame++) {
      if (frame < frames) total += effect_sample(prepared, frame);
      prepared->doppler_sums[frame + 1] = total;
    }
  }
  const double *sums = prepared->doppler_sums;
  double cycles = floor(loop->doppler_scale / (double)period);
  double remainder = fmod(loop->doppler_scale, (double)period);
  double cycle_samples = cycles * (double)period;
  double cycle_total = cycles * sums[period];
  double offset = fmod((double)source_offset, (double)period);
  for (int64_t index = 0; index < count; index++) {
    double end = offset + remainder;
    int64_t first = (int64_t)trunc(offset);
    int64_t last = (int64_t)trunc(end);
    if (first < 0 || first > period || last < 0 || last - period > period ||
        (last > period ? last - period : last) < 0)
      qa_range_error("Doppler range exceeds prepared samples");
    double tail = sums[last < period ? last : period] - sums[first] +
                  (last > period ? sums[last - period] : 0);
    double average =
        (cycle_total + tail) / (cycle_samples + (double)(last - first));
    add_float_paint(paint, length, (output_frame + index) * 2,
                    average * loop->left_volume * effects_gain / 256);
    add_float_paint(paint, length, (output_frame + index) * 2 + 1,
                    average * loop->right_volume * effects_gain / 256);
    offset = fmod(end, (double)period);
  }
}

static void paint_doppler_loop(qa_audio_mixer *mixer, double *paint,
                               size_t length, int64_t output_frame,
                               int64_t count, int64_t source_offset,
                               const loop_mix *loop, double effects_gain) {
  if (loop->doppler_scale > SND_CHUNK_SIZE) {
    paint_wide_doppler_loop(mixer, paint, length, output_frame, count,
                            source_offset, loop, effects_gain);
    return;
  }
  int64_t scaled_offset =
      (int64_t)trunc(fround_(fround_((double)source_offset) *
                             loop->old_doppler_scale));
  int64_t chunk_count =
      (int64_t)ceil(prepared_frames(loop->prepared) / SND_CHUNK_SIZE);
  int64_t chunk = scaled_offset < 0
                      ? 0
                      : (scaled_offset / SND_CHUNK_SIZE) % chunk_count;
  float offset = (float)(scaled_offset < 0
                             ? scaled_offset
                             : scaled_offset % SND_CHUNK_SIZE);
  float left_volume = (float)(loop->left_volume * effects_gain);
  float right_volume = (float)(loop->right_volume * effects_gain);
  for (int64_t index = 0; index < count; index++) {
    int64_t first = (int64_t)trunc((double)offset);
    offset = (float)((double)offset + loop->doppler_scale);
    int64_t last = (int64_t)trunc((double)offset);
    float sample_total = 0;
    for (int64_t source = first; source < last; source++) {
      if (source == SND_CHUNK_SIZE) {
        chunk = (chunk + 1) % chunk_count;
        offset = (float)((double)offset - SND_CHUNK_SIZE);
      }
      sample_total = (float)((double)sample_total +
                             doppler_sample(loop->prepared, chunk, source));
    }
    float divisor = (float)(256 * (double)(last - first));
    float left = (float)(((float)((double)sample_total * (double)left_volume)) /
                         (double)divisor);
    float right =
        (float)(((float)((double)sample_total * (double)right_volume)) /
                (double)divisor);
    add_float_paint(paint, length, (output_frame + index) * 2, left);
    add_float_paint(paint, length, (output_frame + index) * 2 + 1, right);
  }
}

static void paint_loop(qa_audio_mixer *mixer, double *paint, size_t length,
                       int64_t frames, const loop_mix *loop,
                       double effects_gain) {
  int64_t output_frame = 0;
  while (output_frame < frames) {
    int64_t output_frames = (int64_t)prepared_frames(loop->prepared);
    int64_t absolute = mixer->painted_time + output_frame;
    int64_t sample_offset = absolute % output_frames;
    int64_t remaining = output_frames - sample_offset;
    int64_t count = frames - output_frame < remaining ? frames - output_frame
                                                      : remaining;
    if (!mixer->doppler_enabled || !loop->doppler ||
        loop->doppler_scale == 1) {
      for (int64_t index = 0; index < count; index++) {
        if (sample_offset + index < 0)
          qa_range_error("loop sound reached an invalid negative sample access");
        double sample = effect_sample(loop->prepared, sample_offset + index);
        qa_mixer_stereo_volume volume = {loop->left_volume, loop->right_volume};
        paint_effect(paint, length, output_frame + index, sample, volume,
                     effects_gain);
      }
    } else {
      paint_doppler_loop(mixer, paint, length, output_frame, count,
                         sample_offset, loop, effects_gain);
    }
    output_frame += count;
  }
}

static void paint_raw(qa_audio_mixer *mixer, double *paint, int64_t frames) {
  int64_t stop = mixer->painted_time + frames < mixer->raw_end_time
                     ? mixer->painted_time + frames
                     : mixer->raw_end_time;
  for (int64_t absolute = mixer->painted_time; absolute < stop; absolute++) {
    int64_t output_frame = absolute - mixer->painted_time;
    int64_t raw_index = absolute & (RAW_CAPACITY - 1);
    paint[output_frame * 2] = checked_raw(mixer, raw_index * 2);
    paint[output_frame * 2 + 1] = checked_raw(mixer, raw_index * 2 + 1);
  }
}

bool qa_audio_mixer_scan_channel_starts(qa_audio_mixer *mixer) {
  bool new_samples = false;
  for (size_t i = 0; i < mixer->voice_count; i++) {
    one_shot_voice *voice = &mixer->voices[i];
    if (!voice->occupied) continue;
    if (voice->start_kind == START_PENDING) {
      voice->start_kind = START_STARTED;
      voice->start_sample = mixer->painted_time;
      voice_started(mixer, voice);
      new_samples = true;
    } else if (voice->start_kind == START_STARTED &&
               (!voice->has_policy || !voice->policy.has_loop_start) &&
               (double)voice->start_sample +
                       prepared_frames(voice->prepared) <=
                   (double)mixer->painted_time) {
      int64_t end = voice->start_sample +
                    (int64_t)prepared_frames(voice->prepared);
      voice_stopped_at(mixer, voice, QA_AUDIO_VOICE_STOP_ENDED, end);
      free_channel_reason(mixer, i, QA_AUDIO_VOICE_STOP_ENDED);
    }
  }
  return new_samples;
}

static void issue_scheduled_sounds(qa_audio_mixer *mixer) {
  size_t due_count = 0;
  for (size_t i = 0; i < mixer->voice_count; i++) {
    const one_shot_voice *voice = &mixer->voices[i];
    if (voice->occupied && voice->start_kind == START_SCHEDULED &&
        voice->start_sample <= mixer->painted_time)
      due_count++;
  }
  if (due_count == 0) return;
  size_t *due = xmalloc(due_count * sizeof *due);
  size_t at = 0;
  for (size_t i = 0; i < mixer->voice_count; i++) {
    const one_shot_voice *voice = &mixer->voices[i];
    if (voice->occupied && voice->start_kind == START_SCHEDULED &&
        voice->start_sample <= mixer->painted_time)
      due[at++] = i;
  }
  /* Sample ascending, then schedule order descending (orders unique). */
  for (size_t i = 1; i < due_count; i++) {
    size_t value = due[i];
    size_t j = i;
    while (j > 0) {
      const one_shot_voice *a = &mixer->voices[value];
      const one_shot_voice *b = &mixer->voices[due[j - 1]];
      bool before = a->start_sample < b->start_sample ||
                    (a->start_sample == b->start_sample &&
                     a->start_order > b->start_order);
      if (!before) break;
      due[j] = due[j - 1];
      j--;
    }
    due[j] = value;
  }
  for (size_t d = 0; d < due_count; d++) {
    size_t index = due[d];
    one_shot_voice *voice = &mixer->voices[index];
    if (!voice->occupied || voice->start_kind != START_SCHEDULED) continue;
    if (voice->has_channel) {
      qa_sound_channel_command command = {0};
      command.kind = QA_SOUND_CHANNEL_CHANNEL;
      snprintf(command.channel, sizeof command.channel, "%s", voice->channel);
      double entity = voice->entity;
      replace_channel(mixer, entity, command, false,
                      QA_AUDIO_VOICE_STOP_REPLACED);
      voice = &mixer->voices[index];
      if (!voice->occupied || voice->start_kind != START_SCHEDULED) continue;
    }
    voice->start_kind = START_STARTED;
    voice->start_sample = mixer->painted_time;
    voice->stereo = policy_spatialize(mixer, voice);
    voice_started(mixer, voice);
  }
  free(due);
}

static void mix_internal(qa_audio_mixer *mixer, bool direct,
                         double request, qa_sound_paint_range range,
                         int16_t *out) {
  double start_frame =
      direct ? (double)mixer->painted_time : range.start_frame;
  double frames_value =
      direct ? request : range.end_frame - range.start_frame;
  if (!is_safe_integer(start_frame))
    qa_range_error("paint start frame must be a safe integer");
  if (!is_safe_integer(frames_value) || frames_value < 0)
    qa_range_error("mix frame count must be a nonnegative safe integer");
  double sample_count = frames_value * 2;
  if (!is_safe_integer(sample_count)) qa_range_error("mix output is too large");
  double end_value = start_frame + frames_value;
  if (!is_safe_integer(end_value))
    qa_range_error("paint end frame is outside the supported range");
  if (direct && end_value < (double)mixer->sound_time)
    qa_range_error("direct consumption cannot rewind sound time");
  int64_t start = (int64_t)start_frame;
  int64_t frames = (int64_t)frames_value;
  int64_t end = (int64_t)end_value;
  if (frames > 0) memset(out, 0, (size_t)frames * 2 * sizeof *out);
  mixer->painted_time = start;
  (void)qa_audio_mixer_scan_channel_starts(mixer);
  double effects_gain =
      trunc(fround_(fround_(mixer->effects_volume) * 255));
  while (mixer->painted_time < end) {
    issue_scheduled_sounds(mixer);
    int64_t count = end - mixer->painted_time;
    if (count > PAINTBUFFER_SIZE) count = PAINTBUFFER_SIZE;
    for (size_t i = 0; i < mixer->voice_count; i++) {
      const one_shot_voice *voice = &mixer->voices[i];
      if (voice->occupied && voice->start_kind == START_SCHEDULED) {
        int64_t until = voice->start_sample - mixer->painted_time;
        if (until < count) count = until;
      }
    }
    size_t paint_length = (size_t)count * 2;
    double *paint = xcalloc(paint_length, sizeof *paint);
    paint_raw(mixer, paint, count);
    bool *merged = xcalloc(mixer->voice_count, sizeof *merged);
    for (size_t i = 0; i < mixer->voice_count; i++) {
      one_shot_voice *voice = &mixer->voices[i];
      if (!voice->occupied || voice->start_kind == START_SCHEDULED ||
          merged[i])
        continue;
      if (voice->start_kind == START_PENDING)
        plain_error("Channel scan left a pending sound");
      qa_mixer_stereo_volume stereo = voice->stereo;
      if (voice->has_policy && voice->policy.has_gain_limit) {
        double limit = voice->policy.gain_limit;
        double left = stereo.left, right = stereo.right;
        merged[i] = true;
        for (size_t j = 0; j < mixer->voice_count; j++) {
          const one_shot_voice *candidate = &mixer->voices[j];
          if (!candidate->occupied || merged[j] ||
              candidate->start_kind != START_STARTED ||
              !candidate->has_policy || !candidate->policy.has_gain_limit ||
              candidate->policy.gain_limit != limit ||
              candidate->prepared->sound != voice->prepared->sound)
            continue;
          merged[j] = true;
          left += candidate->stereo.left;
          right += candidate->stereo.right;
        }
        stereo.left = qa_js_min(limit, left);
        stereo.right = qa_js_min(limit, right);
      }
      if (stereo.left == 0 && stereo.right == 0) continue;
      int64_t first_offset = mixer->painted_time - voice->start_sample;
      double output_frames = prepared_frames(voice->prepared);
      for (int64_t frame = 0; frame < count; frame++) {
        int64_t sound_frame = first_offset + frame;
        if (voice->has_policy && voice->policy.has_loop_start &&
            (double)sound_frame >= output_frames) {
          int64_t loop_start = (int64_t)voice->policy.loop_start;
          int64_t total = (int64_t)output_frames;
          sound_frame = loop_start + (sound_frame - total) % (total - loop_start);
        }
        if (sound_frame < 0 || (double)sound_frame >= output_frames) continue;
        double sample = effect_sample(voice->prepared, sound_frame);
        paint_effect(paint, paint_length, frame, sample, stereo, effects_gain);
      }
    }
    free(merged);
    for (size_t i = 0; i < mixer->loop_channel_count; i++) {
      const loop_mix *loop = &mixer->loop_channels[i];
      paint_loop(mixer, paint, paint_length, count, loop, effects_gain);
    }
    if (diagnostic_setting(mixer, "s_testsound") != 0) {
      for (int64_t frame = 0; frame < count; frame++) {
        double sample =
            trunc(sin((double)(mixer->painted_time + frame) * 0.1) * 20000 *
                  256);
        paint[frame * 2] = sample;
        paint[frame * 2 + 1] = sample;
      }
    }
    int64_t output_offset = (mixer->painted_time - start) * 2;
    qa_write_linear_blast_stereo16_f64(
        paint, paint_length, out + output_offset,
        (size_t)((frames * 2) - output_offset), (int64_t)paint_length);
    free(paint);
    mixer->painted_time += count;
    for (size_t i = 0; i < mixer->voice_count; i++) {
      one_shot_voice *voice = &mixer->voices[i];
      if (voice->occupied && voice->start_kind == START_STARTED &&
          (!voice->has_policy || !voice->policy.has_loop_start) &&
          (double)voice->start_sample + prepared_frames(voice->prepared) <=
              (double)mixer->painted_time)
        voice_stopped_at(
            mixer, voice, QA_AUDIO_VOICE_STOP_ENDED,
            voice->start_sample + (int64_t)prepared_frames(voice->prepared));
    }
  }
  if (direct) mixer->sound_time = end;
}

void qa_audio_mixer_mix(qa_audio_mixer *mixer, double frames, int16_t *out) {
  qa_sound_paint_range unused = {0, 0};
  mix_internal(mixer, true, frames, unused, out);
}

void qa_audio_mixer_mix_range(qa_audio_mixer *mixer,
                              qa_sound_paint_range range, int16_t *out) {
  mix_internal(mixer, false, 0, range, out);
}



