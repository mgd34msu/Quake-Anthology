/* Port of src/audio/engine.ts. See include/qa/audio/engine.h.
 * GPL-2.0-or-later. */
#include "qa/audio/engine.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "qa/audio/reverb.h"
#include "qa/foundation/bounded_queue.h"
#include "qa/foundation/strings.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

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
static char *xcopy_text(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text) + 1;
  char *copy = xmalloc(length);
  memcpy(copy, text, length);
  return copy;
}

static bool is_safe_integer(double value) {
  return isfinite(value) && trunc(value) == value &&
         fabs(value) <= (double)QA_MAX_SAFE_INTEGER;
}
static bool owner_equal(bool left_has, qa_provider_id left, bool right_has,
                        qa_provider_id right) {
  if (left_has != right_has) return false;
  if (!left_has) return true;
  return left != NULL && right != NULL && strcmp(left, right) == 0;
}
static bool provider_equal(qa_provider_id left, qa_provider_id right) {
  return owner_equal(left != NULL, left, right != NULL, right);
}
static bool selected(qa_audio_audience audience, qa_seat_id seat) {
  return audience.kind == QA_AUDIO_AUDIENCE_WORLD ||
         qa_same_seat(audience.seat, seat);
}

/* TS shares outputStream across a transfer; the wrapper keeps that
 * alias with a refcount and deep copies nowhere. */
typedef struct output_stream_box {
  qa_raw_audio_stream stream;
  int refs;
} output_stream_box;

static output_stream_box *output_stream_new(int64_t rate) {
  output_stream_box *box = xcalloc(1, sizeof *box);
  qa_raw_audio_stream_init(&box->stream, rate);
  box->refs = 1;
  return box;
}
static void output_stream_release(output_stream_box *box) {
  if (box == NULL) return;
  if (--box->refs == 0) {
    qa_raw_audio_stream_destroy(&box->stream);
    free(box);
  }
}

typedef struct loop_entry {
  int64_t key;
  qa_loop_sound loop;
} loop_entry;

typedef struct seat_audio seat_audio;
struct qa_unified_audio {
  int64_t sample_rate;
  qa_unified_audio_options options;
  double max_actors;
  int64_t next_voice_id;
  struct observer_entry {
    int64_t id;
    qa_unified_audio_voice_observer_fn fn;
    void *ctx;
  } *observers;
  size_t observer_count;
  size_t observer_capacity;
  int64_t next_subscription;
  qa_unified_audio_geometry_fn geometry;
  void *geometry_ctx;
  seat_audio **seats;
  size_t seat_count;
  size_t seat_capacity;
  struct round_entry {
    qa_seat_id seat;
    qa_audio_mixer *mixer;
  } *round_mixers;
  size_t round_count;
  size_t round_capacity;
  struct actor_entry {
    qa_actor_id actor;
    bool has_owner;
    qa_provider_id owner; /* borrowed */
    bool retired;         /* TS null slot */
  } *actors;
  size_t actor_count;
  size_t actor_capacity;
  int64_t *free_actor_entities; /* TS freeActorEntities: popped from the end */
  size_t free_actor_count;
  size_t free_actor_capacity;
  struct alias_entry {
    int64_t entity;
    int64_t *aliases;
    size_t count;
    size_t capacity;
  } *aliases;
  size_t alias_count;
  size_t alias_capacity;
  bool doppler_enabled;
  struct position_entry {
    int64_t entity;
    qa_vec3 position;
  } *positions;
  size_t position_count;
  size_t position_capacity;
  struct stream_bus {
    char *id;
    qa_audio_stream_target target;
    qa_raw_audio_stream stream;
  } *streams;
  size_t stream_count;
  size_t stream_capacity;
  struct music_bus {
    char *id;
    qa_audio_stream_target target;
    qa_music_player *player; /* borrowed */
  } *music;
  size_t music_count;
  size_t music_capacity;
  bool has_device;
  qa_sdl_audio_device device;
  bool has_detached;
  bool detached_playing;
  int64_t detached_buffer_frames;
  char *detached_device_name; /* owned or NULL */
  int16_t *queued_pcm;
  size_t queued_count;
  qa_audio_output_format format;
  output_stream_box *output_stream;
  int64_t output_source_frame;
  bool closed;
  bool paused;
  double effects_gain;
  int64_t frame;
  bool output_started;
  bool output_handoff_pending;
  bool has_previous_pump_frame;
  int64_t previous_pump_frame;
  qa_bounded_queue pump_intervals;
  double next_removal_check; /* TS nextRemovalCheck (performance.now ms) */
  qa_string_table *loop_owners;
  struct loop_key_entry {
    uint32_t owner_id;
    int64_t packed;
    int64_t key;
  } *loop_keys;
  size_t loop_key_count;
  size_t loop_key_capacity;
  int64_t next_loop_key;
};

struct seat_audio {
  qa_audio_listener listener;
  qa_audio_mixer *mixer;
  qa_stereo_reverb reverb;
  qa_underwater_filter underwater;
  bool has_environment;
  qa_environment_reverb environment;
  loop_entry *loops;
  size_t loop_count;
  size_t loop_capacity;
  qa_unified_audio *engine;
};

static void check_open(qa_unified_audio *audio) {
  if (audio->closed) plain_error("Audio engine closed");
}

static void seat_loops_set(seat_audio *state, int64_t key,
                           const qa_loop_sound *loop) {
  for (size_t i = 0; i < state->loop_count; i++) {
    if (state->loops[i].key == key) {
      state->loops[i].loop = *loop;
      return;
    }
  }
  if (state->loop_count == state->loop_capacity) {
    state->loop_capacity =
        state->loop_capacity == 0 ? 8 : state->loop_capacity * 2;
    state->loops = xrealloc(state->loops,
                            state->loop_capacity * sizeof *state->loops);
  }
  state->loops[state->loop_count].key = key;
  state->loops[state->loop_count].loop = *loop;
  state->loop_count++;
}
static const qa_loop_sound *seat_loops_get(const seat_audio *state,
                                           int64_t key) {
  for (size_t i = 0; i < state->loop_count; i++)
    if (state->loops[i].key == key) return &state->loops[i].loop;
  return NULL;
}
static void seat_loops_delete(seat_audio *state, size_t index) {
  memmove(&state->loops[index], &state->loops[index + 1],
          (state->loop_count - index - 1) * sizeof *state->loops);
  state->loop_count--;
}
static void seat_loops_delete_key(seat_audio *state, int64_t key) {
  for (size_t i = 0; i < state->loop_count; i++) {
    if (state->loops[i].key == key) {
      seat_loops_delete(state, i);
      return;
    }
  }
}

static void positions_set(qa_unified_audio *audio, int64_t entity,
                          qa_vec3 position) {
  for (size_t i = 0; i < audio->position_count; i++) {
    if (audio->positions[i].entity == entity) {
      audio->positions[i].position = position;
      return;
    }
  }
  if (audio->position_count == audio->position_capacity) {
    audio->position_capacity =
        audio->position_capacity == 0 ? 16 : audio->position_capacity * 2;
    audio->positions = xrealloc(audio->positions, audio->position_capacity *
                                                      sizeof *audio->positions);
  }
  audio->positions[audio->position_count].entity = entity;
  audio->positions[audio->position_count].position = position;
  audio->position_count++;
}
static bool positions_get(qa_unified_audio *audio, int64_t entity,
                          qa_vec3 *out) {
  for (size_t i = 0; i < audio->position_count; i++) {
    if (audio->positions[i].entity == entity) {
      *out = audio->positions[i].position;
      return true;
    }
  }
  return false;
}

static struct alias_entry *alias_entry_for(qa_unified_audio *audio,
                                           int64_t entity, bool create) {
  for (size_t i = 0; i < audio->alias_count; i++)
    if (audio->aliases[i].entity == entity) return &audio->aliases[i];
  if (!create) return NULL;
  if (audio->alias_count == audio->alias_capacity) {
    audio->alias_capacity =
        audio->alias_capacity == 0 ? 16 : audio->alias_capacity * 2;
    audio->aliases = xrealloc(audio->aliases,
                              audio->alias_capacity * sizeof *audio->aliases);
  }
  struct alias_entry *entry = &audio->aliases[audio->alias_count++];
  memset(entry, 0, sizeof *entry);
  entry->entity = entity;
  return entry;
}
static void alias_push(struct alias_entry *entry, int64_t entity) {
  if (entry->count == entry->capacity) {
    entry->capacity = entry->capacity == 0 ? 4 : entry->capacity * 2;
    entry->aliases =
        xrealloc(entry->aliases, entry->capacity * sizeof *entry->aliases);
  }
  entry->aliases[entry->count++] = entity;
}

/* TS actorEntityKey equality: session, slot, generation and owner. */
static int64_t engine_entity(qa_unified_audio *audio, qa_actor_id actor,
                             bool has_owner, qa_provider_id owner) {
  for (size_t i = 0; i < audio->actor_count; i++) {
    if (!audio->actors[i].retired && qa_same_actor(audio->actors[i].actor, actor) &&
        owner_equal(audio->actors[i].has_owner, audio->actors[i].owner,
                    has_owner, owner))
      return (int64_t)i + 1;
  }
  bool has_original = has_owner;
  int64_t original = has_owner ? engine_entity(audio, actor, false, NULL) : 0;
  int64_t entity;
  if (audio->free_actor_count != 0) {
    entity = audio->free_actor_entities[--audio->free_actor_count];
  } else {
    if ((double)(audio->actor_count + 1) >= audio->max_actors)
      qa_range_error("Audio actor capacity exhausted");
    if (audio->actor_count == audio->actor_capacity) {
      audio->actor_capacity =
          audio->actor_capacity == 0 ? 16 : audio->actor_capacity * 2;
      audio->actors =
          xrealloc(audio->actors, audio->actor_capacity * sizeof *audio->actors);
    }
    audio->actor_count++;
    entity = (int64_t)audio->actor_count;
  }
  struct actor_entry *slot = &audio->actors[entity - 1];
  slot->actor = actor;
  slot->has_owner = has_owner;
  slot->owner = owner;
  slot->retired = false;
  if (!has_original) {
    struct alias_entry *entry = alias_entry_for(audio, entity, true);
    entry->count = 0;
    alias_push(entry, entity);
  } else {
    struct alias_entry *entry = alias_entry_for(audio, original, true);
    alias_push(entry, entity);
    qa_vec3 position;
    if (positions_get(audio, original, &position)) {
      positions_set(audio, entity, position);
      for (size_t i = 0; i < audio->seat_count; i++)
        qa_audio_mixer_update_entity_position(audio->seats[i]->mixer,
                                              (double)entity, position);
    }
  }
  return entity;
}

static void positions_delete(qa_unified_audio *audio, int64_t entity) {
  for (size_t i = 0; i < audio->position_count; i++) {
    if (audio->positions[i].entity != entity) continue;
    memmove(&audio->positions[i], &audio->positions[i + 1],
            (audio->position_count - i - 1) * sizeof *audio->positions);
    audio->position_count--;
    return;
  }
}

static bool actor_kept(const qa_actor_id *live, size_t live_count,
                       qa_unified_audio *audio, qa_actor_id actor) {
  for (size_t i = 0; i < live_count; i++)
    if (qa_same_actor(live[i], actor)) return true;
  for (size_t i = 0; i < audio->seat_count; i++)
    if (audio->seats[i]->listener.has_actor &&
        qa_same_actor(audio->seats[i]->listener.actor, actor))
      return true;
  return false;
}

/* TS retireAbsentActors: an unowned actor absent from `live` and from every
 * listener frees its entity and its owner aliases once no loop or voice of
 * any seat can still be heard from them. */
void qa_unified_audio_retire_absent_actors(qa_unified_audio *audio,
                                           const qa_actor_id *live,
                                           size_t live_count) {
  check_open(audio);
  for (size_t index = 0; index < audio->actor_count; index++) {
    struct actor_entry *entry = &audio->actors[index];
    if (entry->retired || entry->has_owner ||
        actor_kept(live, live_count, audio, entry->actor))
      continue;
    int64_t own = (int64_t)index + 1;
    struct alias_entry *aliases = alias_entry_for(audio, own, false);
    const int64_t *entities = aliases != NULL ? aliases->aliases : &own;
    size_t entity_count = aliases != NULL ? aliases->count : 1;
    bool heard = false;
    for (size_t s = 0; s < audio->seat_count && !heard; s++) {
      seat_audio *state = audio->seats[s];
      for (size_t l = 0; l < state->loop_count && !heard; l++)
        heard = qa_same_actor(state->loops[l].loop.actor, entry->actor);
      for (size_t e = 0; e < entity_count && !heard; e++)
        heard = qa_audio_mixer_entity_active(state->mixer, (double)entities[e]);
    }
    if (heard) continue;
    for (size_t e = 0; e < entity_count; e++) {
      int64_t entity = entities[e];
      audio->actors[entity - 1].retired = true;
      positions_delete(audio, entity);
      if (audio->free_actor_count == audio->free_actor_capacity) {
        audio->free_actor_capacity =
            audio->free_actor_capacity == 0 ? 16 : audio->free_actor_capacity * 2;
        audio->free_actor_entities =
            xrealloc(audio->free_actor_entities,
                     audio->free_actor_capacity * sizeof *audio->free_actor_entities);
      }
      audio->free_actor_entities[audio->free_actor_count++] = entity;
    }
    if (aliases != NULL) {
      free(aliases->aliases);
      size_t at = (size_t)(aliases - audio->aliases);
      memmove(&audio->aliases[at], &audio->aliases[at + 1],
              (audio->alias_count - at - 1) * sizeof *audio->aliases);
      audio->alias_count--;
    }
  }
}

static int64_t engine_loop_key(qa_unified_audio *audio, qa_sound_family family,
                               int64_t entity, qa_provider_id owner) {
  uint32_t owner_id =
      owner == NULL ? 0 : qa_string_table_intern(audio->loop_owners, owner);
  int64_t packed = entity * 4 + (family == QA_SOUND_FAMILY_Q1 ? 0
                                 : family == QA_SOUND_FAMILY_Q2 ? 1
                                                                : 2);
  for (size_t i = 0; i < audio->loop_key_count; i++) {
    if (audio->loop_keys[i].owner_id == owner_id &&
        audio->loop_keys[i].packed == packed)
      return audio->loop_keys[i].key;
  }
  if (audio->loop_key_count == audio->loop_key_capacity) {
    audio->loop_key_capacity =
        audio->loop_key_capacity == 0 ? 16 : audio->loop_key_capacity * 2;
    audio->loop_keys = xrealloc(audio->loop_keys, audio->loop_key_capacity *
                                                      sizeof *audio->loop_keys);
  }
  int64_t key = audio->next_loop_key++;
  audio->loop_keys[audio->loop_key_count].owner_id = owner_id;
  audio->loop_keys[audio->loop_key_count].packed = packed;
  audio->loop_keys[audio->loop_key_count].key = key;
  audio->loop_key_count++;
  return key;
}

static seat_audio *find_seat(qa_unified_audio *audio, qa_seat_id seat) {
  for (size_t i = 0; i < audio->seat_count; i++)
    if (qa_same_seat(audio->seats[i]->listener.seat, seat))
      return audio->seats[i];
  return NULL;
}
static seat_audio *seat_or_throw(qa_unified_audio *audio, qa_seat_id seat) {
  seat_audio *state = find_seat(audio, seat);
  if (state == NULL) plain_error("Unknown audio seat");
  return state;
}

static void seat_voice_observer(void *ctx,
                                const qa_mixer_voice_event *mixer_event) {
  seat_audio *state = ctx;
  qa_unified_audio *audio = state->engine;
  qa_audio_voice_event event = {0};
  event.kind = mixer_event->kind;
  event.seat = state->listener.seat;
  event.voice_id = mixer_event->voice_id;
  event.sound = mixer_event->sound;
  event.output_sample = mixer_event->output_sample;
  event.sample_rate = mixer_event->sample_rate;
  event.source_offset_seconds = mixer_event->source_offset_seconds;
  event.reason = mixer_event->reason;
  for (size_t i = 0; i < audio->observer_count; i++)
    audio->observers[i].fn(audio->observers[i].ctx, &event);
}
static double engine_allocate_voice_id(void *ctx) {
  qa_unified_audio *audio = ctx;
  return (double)(++audio->next_voice_id);
}
static double seat_geometry(void *ctx, qa_vec3 position) {
  seat_audio *state = ctx;
  return state->engine->geometry(state->engine->geometry_ctx,
                                 &state->listener, position);
}
static void bind_geometry(qa_unified_audio *audio, seat_audio *state) {
  if (audio->geometry == NULL)
    qa_audio_mixer_set_geometry_transmission(state->mixer, NULL, NULL);
  else
    qa_audio_mixer_set_geometry_transmission(state->mixer, seat_geometry,
                                             state);
}

static uint32_t classify_zero(const char *text, void *ctx) {
  (void)text;
  (void)ctx;
  return 0;
}

qa_unified_audio *qa_unified_audio_create(
    const qa_unified_audio_options *options) {
  double sample_rate = options->has_sample_rate ? options->sample_rate : 44100;
  qa_audio_output_format format;
  if (options->has_output_format) {
    format = qa_audio_output_format_validated(
        options->output_format.sample_rate, options->output_format.channels,
        options->output_format.sample_bits);
  } else {
    if (!is_safe_integer(sample_rate) || sample_rate < 8000 ||
        sample_rate > 192000)
      qa_range_error("Audio output requires 8000\xe2\x80\x93"
                     "192000 Hz, 1 or 2 channels, and 8 or 16 bits");
    format.sample_rate = (int64_t)sample_rate;
    format.channels = 2;
    format.sample_bits = 16;
  }
  qa_unified_audio *audio = calloc(1, sizeof *audio);
  if (audio == NULL) return NULL;
  audio->output_stream = calloc(1, sizeof *audio->output_stream);
  if (audio->output_stream != NULL) {
    qa_raw_audio_stream_init(&audio->output_stream->stream,
                             format.sample_rate);
    audio->output_stream->refs = 1;
  }
  audio->loop_owners = qa_string_table_create(classify_zero, NULL);
  bool queue_ok =
      qa_bounded_queue_init(&audio->pump_intervals, 8, sizeof(double));
  if (audio->output_stream == NULL || audio->loop_owners == NULL || !queue_ok) {
    if (audio->output_stream != NULL) {
      qa_raw_audio_stream_destroy(&audio->output_stream->stream);
      free(audio->output_stream);
    }
    if (audio->loop_owners != NULL) qa_string_table_destroy(audio->loop_owners);
    if (queue_ok) qa_bounded_queue_destroy(&audio->pump_intervals);
    free(audio);
    return NULL;
  }
  if (!is_safe_integer(sample_rate) || sample_rate < 8000 ||
      sample_rate > 192000)
    qa_range_error("Invalid audio output rate");
  audio->sample_rate = (int64_t)sample_rate;
  audio->options = *options;
  audio->max_actors = options->has_max_actors ? options->max_actors : 65536;
  audio->format = format;
  audio->doppler_enabled = true;
  audio->effects_gain = 0.7;
  audio->next_subscription = 1;
  audio->next_loop_key = 1;
  return audio;
}

int64_t qa_unified_audio_sample_rate(const qa_unified_audio *audio) {
  return audio->sample_rate;
}
qa_audio_output_format qa_unified_audio_output_format(
    const qa_unified_audio *audio) {
  return audio->format;
}
int64_t qa_unified_audio_sample_clock(const qa_unified_audio *audio) {
  return audio->frame;
}
int64_t qa_unified_audio_queued_frames(qa_unified_audio *audio) {
  return audio->has_device ? qa_sdl_audio_queued_frames(&audio->device) : 0;
}
const char *qa_unified_audio_selected_output(const qa_unified_audio *audio) {
  if (audio->has_device) return audio->device.device_name;
  return audio->has_detached ? audio->detached_device_name : NULL;
}

static int64_t slice_start(size_t length, int64_t argument) {
  if (argument < 0) {
    int64_t start = (int64_t)length + argument;
    return start > 0 ? start : 0;
  }
  return argument < (int64_t)length ? argument : (int64_t)length;
}

/* The pendingOutput slice, as a malloc'd copy (NULL at count 0). */
static int16_t *pending_output_copy(qa_unified_audio *audio, size_t *out) {
  int64_t samples = audio->has_device
                        ? qa_sdl_audio_queued_frames(&audio->device) * 2
                        : (int64_t)audio->queued_count;
  int64_t start =
      slice_start(audio->queued_count, (int64_t)audio->queued_count - samples);
  size_t count = audio->queued_count - (size_t)start;
  *out = count;
  if (count == 0) return NULL;
  int16_t *copy = xmalloc(count * sizeof *copy);
  memcpy(copy, audio->queued_pcm + start, count * sizeof *copy);
  return copy;
}

int16_t *qa_unified_audio_pending_output(qa_unified_audio *audio,
                                         size_t *out_count) {
  return pending_output_copy(audio, out_count);
}

qa_unified_audio_output_state qa_unified_audio_output_state_of(
    qa_unified_audio *audio) {
  if (audio->closed) return QA_UNIFIED_AUDIO_CLOSED;
  if (!audio->has_device) return QA_UNIFIED_AUDIO_DETACHED;
  return qa_sdl_audio_state_of(&audio->device) == QA_SDL_AUDIO_PLAYING
             ? QA_UNIFIED_AUDIO_PLAYING
             : QA_UNIFIED_AUDIO_PAUSED;
}

const char *qa_unified_audio_output_state_name(
    qa_unified_audio_output_state state) {
  switch (state) {
    case QA_UNIFIED_AUDIO_DETACHED: return "detached";
    case QA_UNIFIED_AUDIO_PAUSED: return "paused";
    case QA_UNIFIED_AUDIO_PLAYING: return "playing";
    case QA_UNIFIED_AUDIO_CLOSED: return "closed";
  }
  return "detached";
}

bool qa_unified_audio_output_configuration_get(
    qa_unified_audio *audio, qa_unified_audio_output_configuration *out) {
  if (!audio->has_device) return false;
  out->sample_rate = audio->device.sample_rate;
  out->channels = audio->device.channels;
  out->sample_bits = audio->device.sample_bits;
  out->device_name = audio->device.device_name;
  out->buffer_frames = audio->device.buffer_frames;
  out->maximum_queued_frames = audio->device.max_queued_frames;
  return true;
}

qa_audio_voice_clock qa_unified_audio_voice_clock(qa_unified_audio *audio) {
  double queued = audio->has_device
                      ? (double)qa_sdl_audio_queued_frames(&audio->device)
                      : audio->has_detached
                            ? (double)audio->queued_count / 2
                            : 0;
  double sample = (double)audio->frame -
                  ceil(queued * (double)audio->sample_rate /
                       (double)audio->format.sample_rate);
  qa_audio_voice_clock clock = {0};
  clock.output_sample = sample > 0 ? sample : 0;
  clock.sample_rate = (double)audio->sample_rate;
  clock.paused = audio->paused;
  return clock;
}

char **qa_unified_audio_output_device_names(qa_unified_audio *audio,
                                            size_t *out_count) {
  check_open(audio);
  return qa_sdl_audio_output_device_names(out_count);
}

int64_t qa_unified_audio_observe_voices(
    qa_unified_audio *audio, qa_unified_audio_voice_observer_fn observer,
    void *ctx) {
  check_open(audio);
  for (size_t i = 0; i < audio->observer_count; i++)
    if (audio->observers[i].fn == observer && audio->observers[i].ctx == ctx)
      return audio->observers[i].id;
  if (audio->observer_count == audio->observer_capacity) {
    audio->observer_capacity =
        audio->observer_capacity == 0 ? 4 : audio->observer_capacity * 2;
    audio->observers = xrealloc(
        audio->observers, audio->observer_capacity * sizeof *audio->observers);
  }
  int64_t id = audio->next_subscription++;
  audio->observers[audio->observer_count].id = id;
  audio->observers[audio->observer_count].fn = observer;
  audio->observers[audio->observer_count].ctx = ctx;
  audio->observer_count++;
  return id;
}

void qa_unified_audio_unobserve_voices(qa_unified_audio *audio,
                                       int64_t subscription) {
  for (size_t i = 0; i < audio->observer_count; i++) {
    if (audio->observers[i].id == subscription) {
      memmove(&audio->observers[i], &audio->observers[i + 1],
              (audio->observer_count - i - 1) * sizeof *audio->observers);
      audio->observer_count--;
      return;
    }
  }
}

void qa_unified_audio_set_geometry_transmission(
    qa_unified_audio *audio, qa_unified_audio_geometry_fn geometry,
    void *ctx) {
  check_open(audio);
  audio->geometry = geometry;
  audio->geometry_ctx = ctx;
  for (size_t i = 0; i < audio->seat_count; i++)
    bind_geometry(audio, audio->seats[i]);
}

static void seat_destroy(seat_audio *state, bool destroy_mixer) {
  if (destroy_mixer) qa_audio_mixer_destroy(state->mixer);
  qa_stereo_reverb_destroy(&state->reverb);
  free(state->loops);
  free(state);
}

void qa_unified_audio_set_listeners(qa_unified_audio *audio,
                                    const qa_audio_listener *listeners,
                                    size_t count) {
  check_open(audio);
  for (size_t index = 0; index < count; index++) {
    for (size_t prior = 0; prior < index; prior++)
      if (qa_same_seat(listeners[prior].seat, listeners[index].seat))
        plain_error("Duplicate audio seat");
    if (!isfinite(listeners[index].gain) || listeners[index].gain < 0)
      qa_range_error("Invalid listener gain");
  }
  for (size_t index = audio->seat_count; index-- > 0;) {
    seat_audio *state = audio->seats[index];
    bool retained = false;
    for (size_t i = 0; i < count; i++)
      if (qa_same_seat(listeners[i].seat, state->listener.seat))
        retained = true;
    if (!retained) {
      qa_audio_mixer_stop_all(state->mixer);
      seat_destroy(state, true);
      memmove(&audio->seats[index], &audio->seats[index + 1],
              (audio->seat_count - index - 1) * sizeof *audio->seats);
      audio->seat_count--;
    }
  }
  for (size_t i = 0; i < count; i++) {
    const qa_audio_listener *listener = &listeners[i];
    seat_audio *state = find_seat(audio, listener->seat);
    if (state == NULL) {
      qa_audio_mixer *mixer = NULL;
      for (size_t r = 0; r < audio->round_count; r++) {
        if (qa_same_seat(audio->round_mixers[r].seat, listener->seat)) {
          mixer = audio->round_mixers[r].mixer;
          memmove(&audio->round_mixers[r], &audio->round_mixers[r + 1],
                  (audio->round_count - r - 1) * sizeof *audio->round_mixers);
          audio->round_count--;
          break;
        }
      }
      if (mixer == NULL) {
        mixer = qa_audio_mixer_create(
            (double)audio->sample_rate, audio->options.milliseconds,
            audio->options.milliseconds_ctx, 96, 2, audio->max_actors);
        if (mixer == NULL) abort(); /* OOM */
      }
      state = xcalloc(1, sizeof *state);
      state->listener = *listener;
      state->mixer = mixer;
      state->engine = audio;
      if (!qa_stereo_reverb_init(&state->reverb, audio->sample_rate))
        abort(); /* OOM */
      qa_underwater_filter_init(&state->underwater, audio->sample_rate);
      qa_audio_mixer_set_effects_volume(mixer, audio->effects_gain);
      qa_audio_mixer_set_doppler_enabled(mixer, audio->doppler_enabled);
      qa_audio_mixer_select_time(mixer, (double)audio->frame,
                                 (double)audio->frame);
      for (size_t p = 0; p < audio->position_count; p++)
        qa_audio_mixer_update_entity_position(
            mixer, (double)audio->positions[p].entity,
            audio->positions[p].position);
      qa_audio_mixer_set_voice_observer(mixer, seat_voice_observer, state,
                                        engine_allocate_voice_id, audio);
      bind_geometry(audio, state);
      if (audio->seat_count == audio->seat_capacity) {
        audio->seat_capacity =
            audio->seat_capacity == 0 ? 4 : audio->seat_capacity * 2;
        audio->seats =
            xrealloc(audio->seats, audio->seat_capacity * sizeof *audio->seats);
      }
      audio->seats[audio->seat_count++] = state;
    }
    state->listener = *listener;
    int64_t entity =
        listener->has_actor
            ? engine_entity(audio, listener->actor, false, NULL)
            : 0;
    qa_audio_mixer_set_listener(state->mixer, (double)entity, listener->origin,
                                listener->axis);
    if (state->has_environment)
      qa_environment_reverb_update(
          &state->environment, listener->origin,
          audio->options.milliseconds(audio->options.milliseconds_ctx));
  }
  for (size_t r = 0; r < audio->round_count; r++)
    qa_audio_mixer_destroy(audio->round_mixers[r].mixer);
  audio->round_count = 0;
}

void qa_unified_audio_set_effects_volume(qa_unified_audio *audio, double gain) {
  if (!isfinite(gain) || gain < 0) qa_range_error("Invalid effects gain");
  audio->effects_gain = gain;
  for (size_t i = 0; i < audio->seat_count; i++)
    qa_audio_mixer_set_effects_volume(audio->seats[i]->mixer, gain);
}

void qa_unified_audio_set_doppler_enabled(qa_unified_audio *audio,
                                          bool enabled) {
  check_open(audio);
  audio->doppler_enabled = enabled;
  for (size_t i = 0; i < audio->seat_count; i++)
    qa_audio_mixer_set_doppler_enabled(audio->seats[i]->mixer, enabled);
}

void qa_unified_audio_update_actor(qa_unified_audio *audio, qa_actor_id actor,
                                   qa_vec3 origin) {
  check_open(audio);
  int64_t entity = engine_entity(audio, actor, false, NULL);
  struct alias_entry *aliases = alias_entry_for(audio, entity, false);
  if (aliases == NULL) return;
  size_t count = aliases->count;
  int64_t *list = xmalloc(count * sizeof *list);
  memcpy(list, aliases->aliases, count * sizeof *list);
  for (size_t i = 0; i < count; i++) {
    positions_set(audio, list[i], origin);
    for (size_t s = 0; s < audio->seat_count; s++)
      qa_audio_mixer_update_entity_position(audio->seats[s]->mixer,
                                            (double)list[i], origin);
  }
  free(list);
}

void qa_unified_audio_update_q3_seat_actor(qa_unified_audio *audio,
                                           qa_seat_id seat, qa_actor_id actor,
                                           qa_vec3 origin,
                                           qa_provider_id owner) {
  check_open(audio);
  seat_audio *state = seat_or_throw(audio, seat);
  int64_t entity = engine_entity(audio, actor, owner != NULL, owner);
  qa_audio_mixer_update_entity_position(state->mixer, (double)entity, origin);
}

static qa_voice_origin engine_origin(qa_unified_audio *audio,
                                     qa_sound_origin origin,
                                     qa_sound_family family,
                                     qa_provider_id owner) {
  qa_voice_origin result = {0};
  switch (origin.kind) {
    case QA_SOUND_ORIGIN_LOCAL:
      result.kind = QA_VOICE_ORIGIN_LOCAL;
      return result;
    case QA_SOUND_ORIGIN_FIXED:
      result.kind = QA_VOICE_ORIGIN_FIXED;
      result.position = origin.position;
      return result;
    case QA_SOUND_ORIGIN_ACTOR:
      result.kind = QA_VOICE_ORIGIN_ENTITY;
      result.entity = (double)engine_entity(
          audio, origin.actor,
          family == QA_SOUND_FAMILY_Q3 && owner != NULL,
          family == QA_SOUND_FAMILY_Q3 ? owner : NULL);
      return result;
  }
  return result;
}

int64_t qa_unified_audio_play(qa_unified_audio *audio,
                              const qa_play_sound *request) {
  check_open(audio);
  if (!isfinite(request->volume) || request->volume < 0 ||
      request->volume > 1)
    qa_range_error("Sound volume must be 0..1");
  qa_sound_channel_command command =
      qa_source_sound_channel(request->family, request->channel);
  bool has_owner = request->family == QA_SOUND_FAMILY_Q3 &&
                   request->owner != NULL;
  qa_provider_id owner = has_owner ? request->owner : NULL;
  qa_voice_origin origin =
      engine_origin(audio, request->origin, request->family, request->owner);
  int64_t entity = request->has_actor
                       ? engine_entity(audio, request->actor, has_owner, owner)
                       : -1;
  int64_t playing = 0;
  for (size_t i = 0; i < audio->seat_count; i++) {
    seat_audio *state = audio->seats[i];
    if (!selected(request->audience, state->listener.seat)) continue;
    int64_t local = state->listener.has_actor
                        ? engine_entity(audio, state->listener.actor,
                                        has_owner, owner)
                        : 0;
    bool personal = has_owner && request->has_actor &&
                    state->listener.has_actor &&
                    qa_same_actor(request->actor, state->listener.actor);
    qa_voice_origin seat_origin = origin;
    if (personal) {
      memset(&seat_origin, 0, sizeof seat_origin);
      seat_origin.kind = QA_VOICE_ORIGIN_LOCAL;
    }
    double options_entity =
        (double)(origin.kind == QA_VOICE_ORIGIN_LOCAL ? local : entity);
    bool accepted = false;
    if (request->family == QA_SOUND_FAMILY_Q3) {
      qa_start_sound_options options = {0};
      options.entity = options_entity;
      options.channel = (double)request->channel;
      options.origin = seat_origin;
      options.volume = trunc(request->volume * 127);
      accepted = qa_audio_mixer_start_shared_sound(
          state->mixer, &request->sound->pcm, options, command,
          request->sound->name, request->sound);
    } else {
      qa_source_sound_options options = {0};
      options.entity = options_entity;
      options.origin = seat_origin;
      options.volume = request->volume;
      options.attenuation = request->attenuation;
      options.has_delay_seconds = request->has_delay_seconds;
      options.delay_seconds = request->delay_seconds;
      options.has_server_milliseconds = request->has_server_milliseconds;
      options.server_milliseconds = request->server_milliseconds;
      accepted = request->family == QA_SOUND_FAMILY_Q1
                     ? qa_audio_mixer_start_q1_sound(
                           state->mixer, &request->sound->pcm, options,
                           command, audio->options.random,
                           audio->options.random_ctx, request->sound)
                     : qa_audio_mixer_start_q2_sound(state->mixer,
                                                     &request->sound->pcm,
                                                     options, command,
                                                     request->sound);
    }
    if (accepted) playing++;
  }
  if (playing > 0 && audio->options.on_sound != NULL)
    audio->options.on_sound(audio->options.on_sound_ctx, request);
  return playing;
}

void qa_unified_audio_stop_sound(qa_unified_audio *audio, qa_actor_id actor,
                                 const char *channel,
                                 qa_audio_audience audience) {
  int64_t entity = engine_entity(audio, actor, false, NULL);
  for (size_t i = 0; i < audio->seat_count; i++) {
    seat_audio *state = audio->seats[i];
    if (selected(audience, state->listener.seat))
      qa_audio_mixer_stop_shared_channel(state->mixer, (double)entity,
                                         channel);
  }
}

static qa_vec3 loop_position(qa_unified_audio *audio,
                             const qa_loop_sound *request,
                             const qa_audio_listener *listener) {
  if (request->origin.kind == QA_SOUND_ORIGIN_LOCAL) return listener->origin;
  if (request->origin.kind == QA_SOUND_ORIGIN_FIXED)
    return request->origin.position;
  qa_vec3 position;
  int64_t entity =
      engine_entity(audio, request->origin.actor, false, NULL);
  if (!positions_get(audio, entity, &position))
    plain_error("Loop requires an actor position");
  return position;
}

static void q3_loop(qa_unified_audio *audio, seat_audio *state,
                    const qa_loop_sound *request, qa_vec3 origin) {
  int64_t entity = engine_entity(audio, request->actor,
                                 request->owner != NULL, request->owner);
  if (request->lifetime == QA_LOOP_LIFETIME_FRAME) {
    qa_frame_looping_sound_options options = {0};
    options.entity = (double)entity;
    options.origin = origin;
    options.velocity = request->velocity;
    options.frame_number = request->frame_number;
    options.has_volume = true;
    options.volume = trunc(request->volume * 127);
    qa_audio_mixer_update_looping_sound(state->mixer, &request->sound->pcm,
                                        options);
  } else {
    qa_real_looping_sound_options options = {0};
    options.entity = (double)entity;
    options.origin = origin;
    options.velocity = request->velocity;
    options.has_volume = true;
    options.volume = trunc(request->volume * 90);
    qa_audio_mixer_update_real_looping_sound(state->mixer,
                                             &request->sound->pcm, options);
  }
}

void qa_unified_audio_loop(qa_unified_audio *audio,
                           const qa_loop_sound *request) {
  check_open(audio);
  int64_t entity = engine_entity(audio, request->actor, false, NULL);
  int64_t key =
      engine_loop_key(audio, request->family, entity, request->owner);
  for (size_t i = 0; i < audio->seat_count; i++) {
    seat_audio *state = audio->seats[i];
    if (!selected(request->audience, state->listener.seat)) continue;
    qa_vec3 origin = loop_position(audio, request, &state->listener);
    seat_loops_set(state, key, request);
    if (request->family == QA_SOUND_FAMILY_Q3)
      q3_loop(audio, state, request, origin);
  }
}

void qa_unified_audio_begin_loop_frame(qa_unified_audio *audio) {
  for (size_t i = 0; i < audio->seat_count; i++) {
    seat_audio *state = audio->seats[i];
    qa_audio_mixer_clear_looping_sounds(state->mixer, false);
    size_t at = 0;
    while (at < state->loop_count) {
      if (state->loops[at].loop.lifetime == QA_LOOP_LIFETIME_FRAME)
        seat_loops_delete(state, at);
      else
        at++;
    }
  }
}

void qa_unified_audio_end_loop_frame(qa_unified_audio *audio) {
  for (size_t i = 0; i < audio->seat_count; i++) {
    seat_audio *state = audio->seats[i];
    size_t count = state->loop_count;
    qa_source_loop_sound *entries = xmalloc(count * sizeof *entries);
    qa_sound_family *families = xmalloc(count * sizeof *families);
    size_t total = 0;
    for (size_t j = 0; j < count; j++) {
      const qa_loop_sound *loop = &state->loops[j].loop;
      qa_vec3 origin = loop_position(audio, loop, &state->listener);
      entries[total].family = loop->family;
      entries[total].entity =
          (double)engine_entity(audio, loop->actor, false, NULL);
      entries[total].sound = &loop->sound->pcm;
      entries[total].origin = origin;
      entries[total].volume = loop->volume;
      entries[total].has_attenuation = true;
      entries[total].attenuation = loop->attenuation;
      families[total] = loop->family;
      total++;
    }
    size_t kept = 0;
    for (size_t j = 0; j < total; j++) {
      if (families[j] == QA_SOUND_FAMILY_Q1 ||
          families[j] == QA_SOUND_FAMILY_Q2) {
        entries[kept] = entries[j];
        entries[kept].family =
            families[j] == QA_SOUND_FAMILY_Q1 ? QA_SOUND_FAMILY_Q1
                                              : QA_SOUND_FAMILY_Q2;
        kept++;
      }
    }
    qa_audio_mixer_set_source_loop_sounds(state->mixer, entries, kept);
    free(entries);
    free(families);
    int64_t entity = state->listener.has_actor
                         ? engine_entity(audio, state->listener.actor, false,
                                         NULL)
                         : 0;
    qa_audio_mixer_set_listener(state->mixer, (double)entity,
                                state->listener.origin, state->listener.axis);
  }
}

void qa_unified_audio_stop_loop(qa_unified_audio *audio, qa_actor_id actor,
                                qa_audio_audience audience,
                                qa_provider_id owner) {
  int64_t entity = engine_entity(audio, actor, false, NULL);
  for (size_t i = 0; i < audio->seat_count; i++) {
    seat_audio *state = audio->seats[i];
    if (!selected(audience, state->listener.seat)) continue;
    size_t at = 0;
    while (at < state->loop_count) {
      const qa_loop_sound *loop = &state->loops[at].loop;
      if (qa_same_actor(loop->actor, actor) &&
          provider_equal(loop->owner, owner)) {
        qa_sound_family family = loop->family;
        seat_loops_delete(state, at);
        if (family == QA_SOUND_FAMILY_Q3)
          qa_audio_mixer_stop_looping_sound(
              state->mixer,
              (double)engine_entity(audio, actor, owner != NULL, owner));
      } else {
        at++;
      }
    }
    if (owner == NULL)
      qa_audio_mixer_stop_looping_sound(state->mixer, (double)entity);
  }
  qa_unified_audio_end_loop_frame(audio);
}

void qa_unified_audio_clear_q3_seat_loops(qa_unified_audio *audio,
                                          qa_seat_id seat, bool kill_all,
                                          qa_provider_id owner) {
  seat_audio *state = seat_or_throw(audio, seat);
  size_t at = 0;
  while (at < state->loop_count) {
    const qa_loop_sound *loop = &state->loops[at].loop;
    if (loop->family == QA_SOUND_FAMILY_Q3 &&
        provider_equal(loop->owner, owner) &&
        loop->audience.kind == QA_AUDIO_AUDIENCE_SEAT &&
        qa_same_seat(loop->audience.seat, seat) &&
        (kill_all || loop->lifetime == QA_LOOP_LIFETIME_FRAME)) {
      qa_actor_id actor = loop->actor;
      seat_loops_delete(state, at);
      qa_audio_mixer_stop_looping_sound(
          state->mixer,
          (double)engine_entity(audio, actor, owner != NULL, owner));
    } else {
      at++;
    }
  }
}

void qa_unified_audio_stop_q3_seat_loop(qa_unified_audio *audio, qa_seat_id seat,
                                        qa_actor_id actor,
                                        qa_provider_id owner) {
  seat_audio *state = seat_or_throw(audio, seat);
  int64_t entity = engine_entity(audio, actor, owner != NULL, owner);
  int64_t key = engine_loop_key(
      audio, QA_SOUND_FAMILY_Q3, engine_entity(audio, actor, false, NULL),
      owner);
  const qa_loop_sound *loop = seat_loops_get(state, key);
  if (loop != NULL && loop->audience.kind == QA_AUDIO_AUDIENCE_WORLD) return;
  if (loop != NULL && loop->audience.kind == QA_AUDIO_AUDIENCE_SEAT &&
      qa_same_seat(loop->audience.seat, seat))
    seat_loops_delete_key(state, key);
  qa_audio_mixer_stop_looping_sound(state->mixer, (double)entity);
}

void qa_unified_audio_release_q3_seat_owner(qa_unified_audio *audio,
                                            qa_seat_id seat,
                                            qa_provider_id owner) {
  qa_unified_audio_clear_q3_seat_loops(audio, seat, true, owner);
  seat_audio *state = seat_or_throw(audio, seat);
  for (size_t i = 0; i < audio->actor_count; i++) {
    if (audio->actors[i].has_owner &&
        provider_equal(audio->actors[i].owner, owner))
      qa_audio_mixer_stop_entity(state->mixer, (double)i + 1);
  }
}

bool qa_unified_audio_add_static_sound(qa_unified_audio *audio, qa_seat_id seat,
                                       const qa_sound_asset *sound,
                                       qa_vec3 origin, double volume,
                                       double attenuation, double key) {
  return qa_audio_mixer_add_static_sound(seat_or_throw(audio, seat)->mixer,
                                         &sound->pcm, origin, volume,
                                         attenuation, key);
}

void qa_unified_audio_remove_static_sound(qa_unified_audio *audio,
                                          qa_seat_id seat, double key) {
  qa_audio_mixer_remove_static_sound(seat_or_throw(audio, seat)->mixer, key);
}

void qa_unified_audio_update_ambient(qa_unified_audio *audio, qa_seat_id seat,
                                     const qa_sound_asset *const *sounds,
                                     size_t sound_count, const double *levels,
                                     size_t level_count,
                                     double elapsed_seconds, double level,
                                     double fade) {
  seat_audio *state = seat_or_throw(audio, seat);
  const qa_pcm_sound **pcms = xmalloc(sound_count * sizeof *pcms);
  for (size_t i = 0; i < sound_count; i++) pcms[i] = &sounds[i]->pcm;
  qa_audio_mixer_update_ambient(state->mixer, pcms, sound_count, levels,
                                level_count, elapsed_seconds, level, fade);
  free(pcms);
}

void qa_unified_audio_set_environment(
    qa_unified_audio *audio, qa_seat_id seat,
    const qa_reverb_environment *environments, size_t environment_count,
    qa_audio_trace_query_fn trace, void *trace_ctx) {
  seat_audio *state = seat_or_throw(audio, seat);
  qa_environment_reverb_init(&state->environment, environments,
                             environment_count, trace, trace_ctx);
  state->has_environment = true;
  qa_stereo_reverb_reset(&state->reverb);
  qa_environment_reverb_update(
      &state->environment, state->listener.origin,
      audio->options.milliseconds(audio->options.milliseconds_ctx));
}

static int64_t find_stream(qa_unified_audio *audio, const char *id) {
  for (size_t i = 0; i < audio->stream_count; i++)
    if (strcmp(audio->streams[i].id, id) == 0) return (int64_t)i;
  return -1;
}
static int64_t find_music(qa_unified_audio *audio, const char *id) {
  for (size_t i = 0; i < audio->music_count; i++)
    if (strcmp(audio->music[i].id, id) == 0) return (int64_t)i;
  return -1;
}

void qa_unified_audio_queue_stream(qa_unified_audio *audio,
                                   qa_audio_stream_target target,
                                   const qa_stream_pcm *chunk) {
  check_open(audio);
  int64_t at = find_stream(audio, target.id);
  if (at < 0) {
    if (audio->stream_count == audio->stream_capacity) {
      audio->stream_capacity =
          audio->stream_capacity == 0 ? 4 : audio->stream_capacity * 2;
      audio->streams = xrealloc(
          audio->streams, audio->stream_capacity * sizeof *audio->streams);
    }
    at = (int64_t)audio->stream_count++;
    audio->streams[at].id = xcopy_text(target.id);
    qa_raw_audio_stream_init(&audio->streams[at].stream, audio->sample_rate);
  }
  audio->streams[at].target = target;
  audio->streams[at].target.id = audio->streams[at].id;
  if (!qa_raw_audio_stream_queue(&audio->streams[at].stream, chunk)) abort();
}

qa_save_value *qa_unified_audio_capture_stream_checkpoint(
    qa_unified_audio *audio, const char *id) {
  check_open(audio);
  int64_t at = find_stream(audio, id);
  if (at < 0) return NULL;
  qa_save_value *value =
      qa_raw_audio_stream_capture_checkpoint(&audio->streams[at].stream);
  if (value == NULL) abort(); /* OOM; NULL here is only the TS null */
  return value;
}

void qa_unified_audio_restore_stream_checkpoint(qa_unified_audio *audio,
                                                qa_audio_stream_target target,
                                                const qa_save_value *value) {
  check_open(audio);
  if (find_stream(audio, target.id) >= 0)
    plain_error("PCM restore requires an unused stream lane");
  if (value != NULL) {
    qa_raw_audio_stream stream;
    if (!qa_raw_audio_stream_restore_checkpoint(&stream, value,
                                                audio->sample_rate))
      abort(); /* OOM */
    if (audio->stream_count == audio->stream_capacity) {
      audio->stream_capacity =
          audio->stream_capacity == 0 ? 4 : audio->stream_capacity * 2;
      audio->streams = xrealloc(
          audio->streams, audio->stream_capacity * sizeof *audio->streams);
    }
    size_t at = audio->stream_count++;
    audio->streams[at].id = xcopy_text(target.id);
    audio->streams[at].target = target;
    audio->streams[at].target.id = audio->streams[at].id;
    audio->streams[at].stream = stream;
  }
}

void qa_unified_audio_pause_stream(qa_unified_audio *audio, const char *id,
                                   bool paused) {
  int64_t at = find_stream(audio, id);
  if (at >= 0) audio->streams[at].stream.paused = paused;
}

void qa_unified_audio_stop_stream(qa_unified_audio *audio, const char *id) {
  int64_t at = find_stream(audio, id);
  if (at < 0) return;
  qa_raw_audio_stream_destroy(&audio->streams[at].stream);
  free(audio->streams[at].id);
  memmove(&audio->streams[at], &audio->streams[at + 1],
          (audio->stream_count - (size_t)at - 1) * sizeof *audio->streams);
  audio->stream_count--;
}

void qa_unified_audio_attach_music(qa_unified_audio *audio,
                                   qa_audio_stream_target target,
                                   qa_music_player *player) {
  if (player->output_rate != audio->sample_rate)
    plain_error("Music output rate differs from device mix");
  int64_t at = find_music(audio, target.id);
  if (at >= 0 && audio->music[at].player != player)
    qa_music_player_close(audio->music[at].player);
  if (at < 0) {
    if (audio->music_count == audio->music_capacity) {
      audio->music_capacity =
          audio->music_capacity == 0 ? 4 : audio->music_capacity * 2;
      audio->music = xrealloc(audio->music,
                              audio->music_capacity * sizeof *audio->music);
    }
    at = (int64_t)audio->music_count++;
    audio->music[at].id = xcopy_text(target.id);
  }
  audio->music[at].target = target;
  audio->music[at].target.id = audio->music[at].id;
  audio->music[at].player = player;
}

void qa_unified_audio_stop_music(qa_unified_audio *audio, const char *id) {
  int64_t at = find_music(audio, id);
  if (at < 0) return;
  qa_music_player_close(audio->music[at].player);
  free(audio->music[at].id);
  memmove(&audio->music[at], &audio->music[at + 1],
          (audio->music_count - (size_t)at - 1) * sizeof *audio->music);
  audio->music_count--;
}

void qa_unified_audio_update_music(qa_unified_audio *audio) {
  for (size_t i = 0; i < audio->music_count; i++)
    qa_music_player_update(audio->music[i].player);
}

static double audience_gain(qa_unified_audio *audio,
                            qa_audio_audience audience) {
  if (audience.kind == QA_AUDIO_AUDIENCE_WORLD) return 1;
  seat_audio *state = find_seat(audio, audience.seat);
  return state != NULL ? state->listener.gain : 0;
}

static void add_i16(double *output, const int16_t *input, size_t count,
                    double gain) {
  for (size_t i = 0; i < count; i++) output[i] += (double)input[i] * gain;
}
static void add_f64(double *output, const double *input, size_t count,
                    double gain) {
  for (size_t i = 0; i < count; i++) output[i] += input[i] * gain;
}

void qa_unified_audio_mix(qa_unified_audio *audio, double frames_value,
                          int16_t *out) {
  check_open(audio);
  if (!is_safe_integer(frames_value) || frames_value < 0 ||
      frames_value > (double)audio->sample_rate * 2)
    qa_range_error("Mix request exceeds two seconds");
  int64_t frames = (int64_t)frames_value;
  size_t count = (size_t)frames * 2;
  double *output = xcalloc(count, sizeof *output);
  if (audio->paused) {
    if (count > 0) memset(out, 0, count * sizeof *out);
    free(output);
    return;
  }
  for (size_t i = 0; i < audio->seat_count; i++) {
    seat_audio *state = audio->seats[i];
    double *seat = xcalloc(count, sizeof *seat);
    int16_t *mixed = xmalloc(count * sizeof *mixed);
    qa_audio_mixer_mix(state->mixer, frames_value, mixed);
    add_i16(seat, mixed, count, 1);
    free(mixed);
    const qa_efx_reverb_params *params =
        state->has_environment
            ? qa_environment_reverb_params(&state->environment)
            : NULL;
    if (params != NULL)
      qa_stereo_reverb_process(&state->reverb, seat, count, params);
    if (state->listener.underwater)
      qa_underwater_filter_process(&state->underwater, seat, count, 0.25);
    else
      qa_underwater_filter_reset(&state->underwater);
    add_f64(output, seat, count, state->listener.gain);
    free(seat);
  }
  for (size_t i = 0; i < audio->stream_count; i++) {
    double *mixed = xcalloc(count, sizeof *mixed);
    qa_raw_audio_stream_mix(&audio->streams[i].stream, frames,
                            audio->streams[i].target.gain, NULL, NULL, mixed);
    add_f64(output, mixed, count,
            audience_gain(audio, audio->streams[i].target.audience));
    free(mixed);
  }
  for (size_t i = 0; i < audio->music_count; i++) {
    double *mixed = xcalloc(count, sizeof *mixed);
    qa_music_player_mix(audio->music[i].player, frames, mixed);
    add_f64(output, mixed, count,
            audio->music[i].target.gain *
                audience_gain(audio, audio->music[i].target.audience));
    free(mixed);
  }
  audio->frame += frames;
  for (size_t i = 0; i < count; i++) {
    double value = output[i];
    double clamped = value < -32768 ? -32768 : value > 32767 ? 32767 : value;
    out[i] = isnan(clamped) ? 0 : (int16_t)trunc(clamped);
  }
  free(output);
}

/* Malloc'd mix() result for the device path; count is frames*2. */
static int16_t *mix_new(qa_unified_audio *audio, int64_t frames) {
  int16_t *samples = xmalloc((size_t)frames * 2 * sizeof *samples);
  qa_unified_audio_mix(audio, (double)frames, samples);
  return samples;
}

typedef struct output_refill {
  qa_unified_audio *audio;
  int64_t frames;
  int16_t *samples; /* owned scratch, replaced per refill */
} output_refill;

static bool output_refill_chunk(void *ctx, qa_stream_pcm *out) {
  output_refill *refill = ctx;
  qa_unified_audio *audio = refill->audio;
  int64_t source_sample = audio->output_source_frame;
  double needed = ceil((double)refill->frames * (double)audio->sample_rate /
                       (double)audio->format.sample_rate);
  int64_t source_frames = needed > 1 ? (int64_t)needed : 1;
  free(refill->samples);
  refill->samples = mix_new(audio, source_frames);
  audio->output_source_frame += source_frames;
  memset(out, 0, sizeof *out);
  out->samples_s16 = refill->samples;
  out->sample_count = (size_t)source_frames * 2;
  out->channels = 2;
  out->sample_rate = audio->sample_rate;
  out->source_sample = source_sample;
  out->reset_stream = false;
  return true;
}

static int16_t *mix_output(qa_unified_audio *audio, int64_t frames) {
  if (audio->format.sample_rate == audio->sample_rate &&
      !qa_raw_audio_stream_initialized(&audio->output_stream->stream)) {
    audio->output_source_frame += frames;
    return mix_new(audio, frames);
  }
  double *mixed = xmalloc((size_t)frames * 2 * sizeof *mixed);
  output_refill refill = {audio, frames, NULL};
  qa_raw_audio_stream_mix(&audio->output_stream->stream, frames, 1,
                          output_refill_chunk, &refill, mixed);
  free(refill.samples);
  int16_t *samples = xmalloc((size_t)frames * 2 * sizeof *samples);
  for (int64_t i = 0; i < frames * 2; i++)
    samples[i] = qa_js_to_int16(mixed[i]); /* Int16Array.from: ToInt16, NaN to 0 */
  free(mixed);
  return samples;
}

static void queue_encoded(qa_sdl_audio_device *device, const int16_t *samples,
                          size_t count, qa_audio_output_format format) {
  qa_encoded_output_pcm encoded;
  if (!qa_encode_output_pcm(samples, count, format, &encoded)) abort();
  if (encoded.is_u8)
    qa_sdl_audio_queue8(device, encoded.samples_u8, encoded.sample_count);
  else
    qa_sdl_audio_queue16(device, encoded.samples_s16, encoded.sample_count);
  qa_encoded_output_pcm_dispose(&encoded);
}

qa_sdl_audio_open_result qa_unified_audio_open_device(
    qa_unified_audio *audio, const char *device_name, double buffer_frames) {
  check_open(audio);
  if (audio->has_device) plain_error("Audio output already open");
  qa_sdl_audio_options options = {0};
  options.sample_rate = (double)audio->format.sample_rate;
  options.channels = (double)audio->format.channels;
  options.sample_bits = (double)audio->format.sample_bits;
  options.device_name = device_name;
  options.buffer_frames = buffer_frames;
  qa_sdl_audio_device device;
  char error[256];
  qa_sdl_audio_open_result result =
      qa_sdl_audio_device_open(&options, &device, error, sizeof error);
  if (result == QA_SDL_AUDIO_OPEN_OK) {
    audio->device = device;
    audio->has_device = true;
    free(audio->detached_device_name);
    audio->detached_device_name = NULL;
    audio->has_detached = false;
  }
  return result;
}

qa_unified_audio_transfer qa_unified_audio_prepare_output_transfer(
    qa_unified_audio *audio, qa_unified_audio *next) {
  check_open(audio);
  check_open(next);
  if (next->has_device || next->has_detached)
    plain_error("Replacement audio already owns an output");
  if (next->sample_rate != audio->sample_rate)
    plain_error("Replacement audio requires the same device sample rate");
  qa_unified_audio_transfer transfer = {audio, next};
  return transfer;
}

void qa_unified_audio_transfer_commit(qa_unified_audio_transfer transfer) {
  qa_unified_audio *from = transfer.from;
  qa_unified_audio *to = transfer.to;
  to->device = from->device;
  to->has_device = from->has_device;
  to->has_detached = from->has_detached;
  to->detached_playing = from->detached_playing;
  to->detached_buffer_frames = from->detached_buffer_frames;
  free(to->detached_device_name);
  to->detached_device_name = from->detached_device_name;
  free(to->queued_pcm);
  to->queued_pcm = from->queued_pcm;
  to->queued_count = from->queued_count;
  to->format = from->format;
  output_stream_release(to->output_stream);
  to->output_stream = from->output_stream;
  to->output_stream->refs++;
  to->output_source_frame = from->output_source_frame;
  to->paused = from->paused;
  to->output_started = from->output_started;
  to->has_previous_pump_frame = false;
  qa_bounded_queue_clear(&to->pump_intervals);
  to->output_handoff_pending = true;
  memset(&from->device, 0, sizeof from->device);
  from->has_device = false;
  from->has_detached = false;
  from->detached_device_name = NULL;
  from->queued_pcm = NULL;
  from->queued_count = 0;
  from->has_previous_pump_frame = false;
  qa_bounded_queue_clear(&from->pump_intervals);
}

static void set_queued_pending(qa_unified_audio *audio) {
  size_t count = 0;
  int16_t *pending = pending_output_copy(audio, &count);
  free(audio->queued_pcm);
  audio->queued_pcm = pending;
  audio->queued_count = count;
}

void qa_unified_audio_detach_output(qa_unified_audio *audio) {
  check_open(audio);
  if (!audio->has_device) return;
  free(audio->detached_device_name);
  audio->detached_device_name = xcopy_text(audio->device.device_name);
  audio->detached_playing =
      qa_sdl_audio_state_of(&audio->device) == QA_SDL_AUDIO_PLAYING;
  audio->detached_buffer_frames = audio->device.buffer_frames;
  audio->has_detached = true;
  qa_sdl_audio_pause(&audio->device);
  set_queued_pending(audio);
  qa_sdl_audio_close(&audio->device);
  memset(&audio->device, 0, sizeof audio->device);
  audio->has_device = false;
  audio->has_previous_pump_frame = false;
  qa_bounded_queue_clear(&audio->pump_intervals);
}

/* selectOutput's open closure: open, then queue the retained PCM. */
static qa_sdl_audio_open_result open_with_pcm(const char *name,
                                              qa_audio_output_format format,
                                              bool has_buffer,
                                              int64_t buffer_frames,
                                              const int16_t *pcm, size_t count,
                                              qa_sdl_audio_device *out) {
  qa_sdl_audio_options options = {0};
  options.sample_rate = (double)format.sample_rate;
  options.channels = (double)format.channels;
  options.sample_bits = (double)format.sample_bits;
  options.device_name = name;
  options.buffer_frames = has_buffer ? (double)buffer_frames : 0;
  char error[256];
  qa_sdl_audio_open_result result =
      qa_sdl_audio_device_open(&options, out, error, sizeof error);
  if (result != QA_SDL_AUDIO_OPEN_OK) return result;
  queue_encoded(out, pcm, count, format);
  return QA_SDL_AUDIO_OPEN_OK;
}

static bool format_equal(qa_audio_output_format a, qa_audio_output_format b) {
  return a.sample_rate == b.sample_rate && a.channels == b.channels &&
         a.sample_bits == b.sample_bits;
}
static bool name_equal(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}

qa_sdl_audio_open_result qa_unified_audio_select_output(
    qa_unified_audio *audio, const char *device_name,
    const qa_audio_output_format *requested, bool restart) {
  check_open(audio);
  qa_audio_output_format format =
      requested != NULL
          ? qa_audio_output_format_validated(requested->sample_rate,
                                             requested->channels,
                                             requested->sample_bits)
          : qa_audio_output_format_validated(audio->format.sample_rate,
                                             audio->format.channels,
                                             audio->format.sample_bits);
  qa_audio_output_format old_format = audio->format;
  bool had_previous = audio->has_device;
  char *previous_name =
      had_previous ? xcopy_text(audio->device.device_name) : NULL;
  int64_t previous_buffer = had_previous ? audio->device.buffer_frames : 0;
  if (!restart && had_previous &&
      name_equal(previous_name, device_name) &&
      format_equal(format, old_format)) {
    free(previous_name);
    return QA_SDL_AUDIO_OPEN_OK;
  }
  bool playing = had_previous
                     ? qa_sdl_audio_state_of(&audio->device) ==
                           QA_SDL_AUDIO_PLAYING
                     : audio->has_detached && audio->detached_playing;
  if (had_previous) qa_sdl_audio_pause(&audio->device);
  set_queued_pending(audio);
  size_t retained_count = audio->queued_count;
  int16_t *retained = xmalloc(retained_count * sizeof *retained);
  if (retained_count > 0)
    memcpy(retained, audio->queued_pcm, retained_count * sizeof *retained);
  int16_t *converted = NULL;
  size_t converted_count = 0;
  if (!qa_resample_queued_pcm(retained, retained_count, old_format.sample_rate,
                              format.sample_rate, &converted,
                              &converted_count)) {
    free(retained);
    free(previous_name);
    return QA_SDL_AUDIO_OPEN_FAILED;
  }
  bool has_buffer = had_previous || audio->has_detached;
  int64_t old_buffer =
      had_previous ? previous_buffer : audio->detached_buffer_frames;
  qa_sdl_audio_device replacement;
  memset(&replacement, 0, sizeof replacement);
  qa_sdl_audio_open_result result = open_with_pcm(
      device_name, format, has_buffer, old_buffer, converted,
      converted_count, &replacement);
  if (result != QA_SDL_AUDIO_OPEN_OK) {
    if (result != QA_SDL_AUDIO_OPEN_UNAVAILABLE || !had_previous) {
      if (playing && had_previous) qa_sdl_audio_resume(&audio->device);
      free(converted);
      free(retained);
      free(previous_name);
      return result;
    }
    /* Single-output drivers require releasing the old device first. */
    qa_sdl_audio_close(&audio->device);
    memset(&audio->device, 0, sizeof audio->device);
    audio->has_device = false;
    result = open_with_pcm(device_name, format, has_buffer, old_buffer,
                           converted, converted_count, &replacement);
    if (result != QA_SDL_AUDIO_OPEN_OK) {
      qa_sdl_audio_open_result selection = result;
      qa_sdl_audio_device restored;
      memset(&restored, 0, sizeof restored);
      qa_sdl_audio_open_result restore = open_with_pcm(
          previous_name, old_format, has_buffer, old_buffer, retained,
          retained_count, &restored);
      audio->has_previous_pump_frame = false;
      qa_bounded_queue_clear(&audio->pump_intervals);
      free(converted);
      free(retained);
      if (restore == QA_SDL_AUDIO_OPEN_OK) {
        audio->device = restored;
        audio->has_device = true;
        if (playing && !audio->paused) qa_sdl_audio_resume(&audio->device);
        free(previous_name);
        return selection;
      }
      free(audio->detached_device_name);
      audio->detached_device_name = previous_name;
      audio->detached_playing = playing;
      audio->detached_buffer_frames = previous_buffer;
      audio->has_detached = true;
      /* TS throws AggregateError; pump's removal fallback catches it. */
      qa_raise("AggregateError", 0, 0, "Audio output selection and recovery failed");
    }
  }
  if (had_previous && audio->has_device) {
    qa_sdl_audio_close(&audio->device);
    memset(&audio->device, 0, sizeof audio->device);
  }
  audio->device = replacement;
  audio->has_device = true;
  free(audio->detached_device_name);
  audio->detached_device_name = NULL;
  audio->has_detached = false;
  audio->format = format;
  free(audio->queued_pcm);
  audio->queued_pcm = converted;
  audio->queued_count = converted_count;
  if (format.sample_rate != old_format.sample_rate) {
    output_stream_box *next = xcalloc(1, sizeof *next);
    if (!qa_raw_audio_stream_with_output_rate(&audio->output_stream->stream,
                                              format.sample_rate,
                                              &next->stream)) {
      free(next);
      abort(); /* OOM */
    }
    next->refs = 1;
    output_stream_release(audio->output_stream);
    audio->output_stream = next;
  }
  audio->has_previous_pump_frame = false;
  qa_bounded_queue_clear(&audio->pump_intervals);
  if (playing && !audio->paused) qa_sdl_audio_resume(&audio->device);
  free(retained);
  free(previous_name);
  return QA_SDL_AUDIO_OPEN_OK;
}

/* TS performance.now(): milliseconds on a monotonic clock. */
static double performance_now(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1e6;
}

/* SdlAudioDevice.removed: a named output no longer listed by SDL. */
static bool device_removed(qa_sdl_audio_device *device) {
  return qa_sdl_audio_removed(device);
}

/* TS outputRemoved: checked at most once a second for a named output. */
static bool output_removed(qa_unified_audio *audio) {
  if (audio->device.device_name == NULL) return false;
  double now = performance_now();
  if (now < audio->next_removal_check) return false;
  audio->next_removal_check = now + 1000;
  return device_removed(&audio->device);
}

typedef struct removal_fallback {
  qa_unified_audio *audio;
  qa_sdl_audio_open_result result;
} removal_fallback;

static void removal_fallback_run(void *raw) {
  removal_fallback *job = raw;
  job->result = qa_unified_audio_select_output(job->audio, NULL, &job->audio->format, true);
}

static void longest_visit(const void *value, size_t index, void *ctx) {
  (void)index;
  double *longest = ctx;
  if (*(const double *)value > *longest) *longest = *(const double *)value;
}

int64_t qa_unified_audio_pump(qa_unified_audio *audio, bool has_ahead,
                              double ahead_frames,
                              double measured_work_milliseconds) {
  check_open(audio);
  if (!audio->has_device) plain_error("Audio output is not open");
  if (audio->paused) return 0;
  if (output_removed(audio)) {
    /* SDL_AUDIODEVICEREMOVED: fall back to the system default, or stay
     * detached until snd_restart succeeds. A failed open is a TS throw
     * here, so it detaches too. */
    removal_fallback job = {audio, QA_SDL_AUDIO_OPEN_OK};
    qa_caught_error caught;
    if (!qa_try(removal_fallback_run, &job, &caught) || job.result != QA_SDL_AUDIO_OPEN_OK)
      qa_unified_audio_detach_output(audio);
    return 0;
  }
  if (!isfinite(measured_work_milliseconds) ||
      measured_work_milliseconds < 0)
    qa_range_error("Invalid measured audio frame work");
  qa_sdl_audio_device *device = &audio->device;
  double work_frames =
      ceil(measured_work_milliseconds * (double)device->sample_rate / 1000);
  bool initial_fill =
      audio->output_handoff_pending ||
      (!audio->output_started &&
       qa_sdl_audio_state_of(device) == QA_SDL_AUDIO_PAUSED &&
       qa_sdl_audio_queued_frames(device) == 0);
  int64_t playback_frame = qa_sdl_audio_playback_frames(device);
  int64_t interval = audio->has_previous_pump_frame
                         ? playback_frame - audio->previous_pump_frame
                         : 0;
  if (!initial_fill) {
    double value = (double)interval > work_frames ? (double)interval
                                                  : work_frames;
    qa_bounded_queue_push(&audio->pump_intervals, &value);
  }
  double longest = -INFINITY;
  qa_bounded_queue_for_each(&audio->pump_intervals, longest_visit, &longest);
  double target = has_ahead
                      ? ahead_frames
                      : qa_js_min((double)device->max_queued_frames,
                             initial_fill
                                 ? qa_js_max(ceil((double)device->sample_rate *
                                             0.2),
                                        (double)device->buffer_frames * 2)
                                 : qa_js_max(ceil((double)device->sample_rate *
                                             0.08),
                                        longest +
                                            (double)device->buffer_frames *
                                                2));
  if (!is_safe_integer(target) || target < 0 ||
      target > (double)device->max_queued_frames)
    qa_range_error("Invalid audio lookahead");
  audio->previous_pump_frame = playback_frame;
  audio->has_previous_pump_frame = true;
  {
    int64_t start = slice_start(
        audio->queued_count,
        (int64_t)audio->queued_count - qa_sdl_audio_queued_frames(device) * 2);
    if (start > 0) {
      memmove(audio->queued_pcm, audio->queued_pcm + start,
              (audio->queued_count - (size_t)start) * sizeof *audio->queued_pcm);
      audio->queued_count -= (size_t)start;
    }
  }
  int64_t frames = (int64_t)target - qa_sdl_audio_queued_frames(device);
  if (frames < 0) frames = 0;
  if (frames > 0) {
    int16_t *samples = mix_output(audio, frames);
    size_t sample_count = (size_t)frames * 2;
    queue_encoded(device, samples, sample_count, audio->format);
    /* Keep only the retained tail of old plus new samples:
     * keep = min(previous + samples, queuedFrames * 2). */
    int64_t queued_samples = qa_sdl_audio_queued_frames(device) * 2;
    size_t total = audio->queued_count + sample_count;
    size_t kept = queued_samples < 0 ? 0
                  : (size_t)queued_samples < total ? (size_t)queued_samples : total;
    size_t from_new = kept < sample_count ? kept : sample_count;
    int16_t *queued = xmalloc((kept == 0 ? 1 : kept) * sizeof *queued);
    if (from_new > 0)
      memcpy(queued + (kept - from_new), samples + (sample_count - from_new),
             from_new * sizeof *queued);
    if (kept > from_new)
      memcpy(queued, audio->queued_pcm + (audio->queued_count - (kept - from_new)),
             (kept - from_new) * sizeof *queued);
    free(samples);
    free(audio->queued_pcm);
    audio->queued_pcm = kept > 0 ? queued : NULL;
    if (kept == 0) free(queued);
    audio->queued_count = kept;
  }
  qa_sdl_audio_resume(device);
  audio->output_started = true;
  audio->output_handoff_pending = false;
  return frames;
}

void qa_unified_audio_pause(qa_unified_audio *audio, bool paused) {
  check_open(audio);
  audio->paused = paused;
  audio->has_previous_pump_frame = false;
  qa_bounded_queue_clear(&audio->pump_intervals);
  if (paused) {
    if (audio->has_device) qa_sdl_audio_pause(&audio->device);
  } else if (audio->has_device) {
    qa_sdl_audio_resume(&audio->device);
    audio->output_started = true;
  }
}

void qa_unified_audio_stop_all(qa_unified_audio *audio) {
  for (size_t i = 0; i < audio->seat_count; i++) {
    seat_audio *state = audio->seats[i];
    qa_audio_mixer_stop_all(state->mixer);
    state->loop_count = 0;
    qa_stereo_reverb_reset(&state->reverb);
    qa_underwater_filter_reset(&state->underwater);
  }
  for (size_t i = 0; i < audio->stream_count; i++) {
    qa_raw_audio_stream_destroy(&audio->streams[i].stream);
    free(audio->streams[i].id);
  }
  audio->stream_count = 0;
  for (size_t i = 0; i < audio->music_count; i++) {
    qa_music_player_close(audio->music[i].player);
    free(audio->music[i].id);
  }
  audio->music_count = 0;
  if (audio->has_device) qa_sdl_audio_clear(&audio->device);
  free(audio->queued_pcm);
  audio->queued_pcm = NULL;
  audio->queued_count = 0;
  output_stream_release(audio->output_stream);
  audio->output_stream = output_stream_new(audio->format.sample_rate);
  audio->output_source_frame = 0;
}

void qa_unified_audio_reset_round(qa_unified_audio *audio) {
  check_open(audio);
  qa_unified_audio_stop_all(audio);
  for (size_t i = 0; i < audio->seat_count; i++) {
    seat_audio *state = audio->seats[i];
    /* The retained mixer is re-bound on reuse; detach it from the
     * seat being freed so no callback can read freed state. */
    qa_audio_mixer_set_geometry_transmission(state->mixer, NULL, NULL);
    qa_audio_mixer_set_voice_observer(state->mixer, NULL, NULL, NULL, NULL);
    bool replaced = false;
    for (size_t r = 0; r < audio->round_count; r++) {
      if (qa_same_seat(audio->round_mixers[r].seat, state->listener.seat)) {
        qa_audio_mixer_destroy(audio->round_mixers[r].mixer);
        audio->round_mixers[r].mixer = state->mixer;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      if (audio->round_count == audio->round_capacity) {
        audio->round_capacity =
            audio->round_capacity == 0 ? 4 : audio->round_capacity * 2;
        audio->round_mixers = xrealloc(
            audio->round_mixers,
            audio->round_capacity * sizeof *audio->round_mixers);
      }
      audio->round_mixers[audio->round_count].seat = state->listener.seat;
      audio->round_mixers[audio->round_count].mixer = state->mixer;
      audio->round_count++;
    }
    seat_destroy(state, false);
  }
  audio->seat_count = 0;
  audio->actor_count = 0;
  for (size_t i = 0; i < audio->alias_count; i++) {
    free(audio->aliases[i].aliases);
    audio->aliases[i].aliases = NULL;
    audio->aliases[i].count = 0;
    audio->aliases[i].capacity = 0;
  }
  audio->alias_count = 0;
  audio->position_count = 0;
  audio->has_previous_pump_frame = false;
  qa_bounded_queue_clear(&audio->pump_intervals);
}

void qa_unified_audio_close(qa_unified_audio *audio) {
  if (audio->closed) return;
  qa_unified_audio_stop_all(audio);
  audio->closed = true;
  audio->geometry = NULL;
  audio->geometry_ctx = NULL;
  audio->observer_count = 0;
  for (size_t i = 0; i < audio->seat_count; i++)
    qa_audio_mixer_set_geometry_transmission(audio->seats[i]->mixer, NULL,
                                             NULL);
  for (size_t r = 0; r < audio->round_count; r++) {
    qa_audio_mixer_set_geometry_transmission(audio->round_mixers[r].mixer,
                                             NULL, NULL);
    qa_audio_mixer_destroy(audio->round_mixers[r].mixer);
  }
  audio->round_count = 0;
  if (audio->has_device) {
    qa_sdl_audio_close(&audio->device);
    memset(&audio->device, 0, sizeof audio->device);
    audio->has_device = false;
  }
  free(audio->detached_device_name);
  audio->detached_device_name = NULL;
  audio->has_detached = false;
}

void qa_unified_audio_destroy(qa_unified_audio *audio) {
  if (audio == NULL) return;
  if (!audio->closed) qa_unified_audio_close(audio);
  for (size_t i = 0; i < audio->seat_count; i++) seat_destroy(audio->seats[i], true);
  free(audio->seats);
  free(audio->round_mixers);
  free(audio->actors);
  free(audio->free_actor_entities);
  for (size_t i = 0; i < audio->alias_count; i++) free(audio->aliases[i].aliases);
  free(audio->aliases);
  free(audio->positions);
  free(audio->streams);
  free(audio->music);
  free(audio->observers);
  free(audio->loop_keys);
  free(audio->queued_pcm);
  free(audio->detached_device_name);
  output_stream_release(audio->output_stream);
  qa_string_table_destroy(audio->loop_owners);
  qa_bounded_queue_destroy(&audio->pump_intervals);
  free(audio);
}


