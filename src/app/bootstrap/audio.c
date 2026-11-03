/* Port of src/app/bootstrap/audio.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/audio.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qa/app/bootstrap/audio/commands.h"
#include "qa/app/bootstrap/audio/menu.h"
#include "qa/app/bootstrap/audio/music.h"
#include "qa/app/bootstrap/audio/output_settings.h"
#include "qa/app/bootstrap/audio/playlist.h"
#include "qa/app/bootstrap/audio/q2_events.h"
#include "qa/audio/bank.h"
#include "qa/audio/environments.h"
#include "qa/audio/geometry.h"
#include "qa/content/catalog/index.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/weapons/types.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/content/q3/foundation/animation_config.h"
#include "qa/content/q3/presentation/character_resources.h"
#include "qa/core/game_numeric.h"
#include "qa/formats/q1_map/queries.h"
#include "qa/formats/q1_map/types.h"
#include "qa/formats/q2_map.h"
#include "qa/formats/q3_map/entities.h"
#include "qa/foundation/event_ring.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/js_parse_extra.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/network/common/js_number.h"

/* ---- small owned containers ---- */
static void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Out of memory"); return p; }
static void *xcalloc(size_t c, size_t n) { void *p = calloc(c ? c : 1, n ? n : 1); if (!p) qa_range_error("Out of memory"); return p; }
static void *xrealloc(void *p, size_t n) { void *r = realloc(p, n ? n : 1); if (!r) qa_range_error("Out of memory"); return r; }
static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *r = xmalloc(n); memcpy(r, s, n); return r; }
__attribute__((format(printf, 1, 2), nonnull(1))) static char *xformat(const char *format, ...) {
  va_list args; va_start(args, format);
  char probe[1];
  int n = vsnprintf(probe, sizeof probe, format, args); va_end(args);
  if (n < 0) qa_range_error("Out of memory");
  char *out = xmalloc((size_t)n + 1);
  va_start(args, format); vsnprintf(out, (size_t)n + 1, format, args); va_end(args);
  return out;
}
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static noreturn void plain_error(const char *format, ...) {
  char text[QA_CAUGHT_MESSAGE_CAPACITY];
  va_list args; va_start(args, format); vsnprintf(text, sizeof text, format, args); va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}

/* String-keyed map (TS Map<string, ...> / Set<string>). Keys owned. */
typedef struct map_entry { char *key; void *value; double number; } map_entry;
/* order keeps TS Map insertion order for iteration (keys are borrowed from
 * their entries, which never move their malloc'd key text). */
typedef struct str_map { map_entry *entries; size_t capacity, count; const char **order; } str_map;
static uint64_t hash_text(const char *text) {
  uint64_t h = 1469598103934665603ull;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) { h ^= *p; h *= 1099511628211ull; }
  return h;
}
static map_entry *map_find(const str_map *map, const char *key) {
  if (map->capacity == 0) return NULL;
  for (size_t i = (size_t)(hash_text(key) & (map->capacity - 1));; i = (i + 1) & (map->capacity - 1)) {
    map_entry *entry = &map->entries[i];
    if (entry->key == NULL) return NULL;
    if (strcmp(entry->key, key) == 0) return entry;
  }
}
static map_entry *map_insert(str_map *map, const char *key) {
  map_entry *found = map_find(map, key);
  if (found != NULL) return found;
  if ((map->count + 1) * 2 > map->capacity) {
    size_t capacity = map->capacity ? map->capacity * 2 : 16;
    map_entry *entries = xcalloc(capacity, sizeof *entries);
    for (size_t i = 0; i < map->capacity; i++) {
      if (map->entries[i].key == NULL) continue;
      size_t j = (size_t)(hash_text(map->entries[i].key) & (capacity - 1));
      while (entries[j].key != NULL) j = (j + 1) & (capacity - 1);
      entries[j] = map->entries[i];
    }
    free(map->entries); map->entries = entries; map->capacity = capacity;
  }
  size_t i = (size_t)(hash_text(key) & (map->capacity - 1));
  while (map->entries[i].key != NULL) i = (i + 1) & (map->capacity - 1);
  map->entries[i].key = xstrdup(key); map->entries[i].value = NULL; map->entries[i].number = 0;
  map->order = xrealloc(map->order, (map->count + 1) * sizeof *map->order);
  map->order[map->count] = map->entries[i].key;
  map->count++;
  return &map->entries[i];
}
static void map_clear(str_map *map, void (*free_value)(void *)) {
  for (size_t i = 0; i < map->capacity; i++) {
    if (map->entries[i].key == NULL) continue;
    if (free_value != NULL && map->entries[i].value != NULL) free_value(map->entries[i].value);
    free(map->entries[i].key);
  }
  free(map->entries); map->entries = NULL; map->capacity = 0; map->count = 0;
  free(map->order); map->order = NULL;
}

/* ---- records ---- */
typedef struct actor_audio {
  qa_actor_id actor;
  char *model;
  int underwater; /* -1 = TS null */
  bool has_chase; qa_actor_id chase;
  double pain_time;
} actor_audio;
static void free_actor_audio(void *value) { actor_audio *state = value; free(state->model); free(state); }
typedef struct static_audio {
  double key;
  bool has_owner; qa_presentation_owner owner;
  qa_audio_audience audience;
  const qa_sound_asset *sound;
  qa_vec3 origin;
  double volume, attenuation;
  qa_seat_id *seats; size_t seat_count, seat_cap;
} static_audio;
typedef struct loop_audio {
  qa_loop_sound loop;
  bool has_presentation_owner; qa_presentation_owner presentation_owner;
} loop_audio;
typedef struct ui_sound_record { qa_seat_id seat; const char *sound; } ui_sound_record;
typedef struct cgame_record {
  qa_content_id content; qa_seat_id seat; bool has_owner; qa_provider_id owner;
  qa_app_q3_seat_audio_operation *operations; size_t operation_count;
} cgame_record;
typedef struct track_list { char **tracks; size_t count; } track_list;
static void free_track_list(void *value) {
  track_list *list = value;
  for (size_t i = 0; i < list->count; i++) free(list->tracks[i]);
  free(list->tracks); free(list);
}
static void free_bank(void *value) { qa_sound_bank_destroy(value); }
typedef struct seat_scene { qa_seat_id seat; qa_scene_queries scene; } seat_scene;
typedef struct body_record { qa_actor_id actor; qa_vec3 origin; } body_record;
typedef struct environment_seat { qa_app_application_audio *audio; qa_seat_id seat; } environment_seat;
typedef struct retired_environment { qa_reverb_environment *definitions; size_t count; } retired_environment;
typedef struct music_fallback { qa_app_application_audio *audio; const char *alternate; } music_fallback;

struct qa_app_application_audio {
  qa_unified_audio *engine;
  qa_app_loaded_content *content;
  double (*now)(void *ctx); void *now_ctx;
  char *character_model;
  qa_app_audio_print_fn print; void *print_ctx;
  bool (*q3_team_game)(void *ctx); void *q3_team_game_ctx;
  str_map strings;   /* interned text */
  str_map playlists; /* content -> track_list */
  str_map banks;     /* content -> qa_sound_bank */
  qa_app_application_music *music;
  qa_game_random random;
  str_map actor_audio; /* "slot:generation" -> actor_audio */
  static_audio *statics; size_t static_count, static_cap;
  double next_static_key;
  str_map retired_owners;
  str_map owner_revisions; /* key -> number */
  bool has_music_owner; qa_presentation_owner music_owner;
  bool has_pause_owner; qa_presentation_owner pause_owner;
  double music_request;
  loop_audio *loops; size_t loop_count, loop_cap;
  str_map sounds;    /* key -> qa_sound_asset (NULL value = TS null) */
  str_map footsteps; /* content -> number (qa_q3_player_footsteps) */
  str_map warned;
  qa_event_ring ui_sounds; qa_event_cursor ui_sounds_cursor;
  qa_event_ring effect_sounds; qa_event_cursor effect_sounds_cursor;
  qa_event_ring cgame_frames; qa_event_cursor cgame_frames_cursor;
  qa_audio_listener *listeners; size_t listener_count;
  seat_scene *seat_scenes; size_t seat_scene_count;
  bool has_snapshot;
  qa_actor_id *snapshot_actors; size_t snapshot_actor_count;
  body_record *snapshot_bodies; size_t snapshot_body_count;
  bool has_geometry; qa_scene_queries geometry_scene; qa_numeric_profile numeric;
  bool geometry_enabled;
  bool has_environment;
  qa_reverb_environment *environment_definitions; size_t environment_definition_count;
  retired_environment *retired_environments; size_t retired_environment_count;
  environment_seat **environment_contexts; size_t environment_context_count;
  qa_seat_id *environment_seats; size_t environment_seat_count, environment_seat_cap;
  double volume;
  qa_cvar_registry *volume_cvars;
  qa_cvar_registry *output_cvars;
  bool closed;
  bool has_haptics; qa_app_audio_haptics haptics;
  music_fallback fallback;
  bool destroyed;
};

static const char *intern(qa_app_application_audio *audio, const char *text) {
  if (text == NULL) return NULL;
  return map_insert(&audio->strings, text)->key;
}
static void print_text(qa_app_application_audio *audio, const char *text) { audio->print(audio->print_ctx, text); }
static const qa_content_catalog *catalog_of(const qa_app_application_audio *audio) {
  return qa_app_loaded_content_catalog(audio->content);
}
static const qa_executable_recipe *recipe_of(const qa_app_application_audio *audio) {
  return qa_app_loaded_content_recipe(audio->content);
}
static const qa_catalog_product_expectation *product_of(const qa_app_application_audio *audio, qa_content_id content) {
  return &qa_content_catalog_product(catalog_of(audio), content)->expectation;
}
static qa_content_mounted *mounts_for(qa_app_application_audio *audio, qa_content_id content) {
  return (qa_content_mounted *)qa_app_loaded_content_for_content(audio->content, content);
}
static const char *family_name(qa_game_family family) {
  return family == QA_GAME_FAMILY_Q1 ? "q1" : family == QA_GAME_FAMILY_Q2 ? "q2" : "q3";
}
static qa_sound_family sound_family(qa_game_family family) {
  return family == QA_GAME_FAMILY_Q1 ? QA_SOUND_FAMILY_Q1 : family == QA_GAME_FAMILY_Q2 ? QA_SOUND_FAMILY_Q2 : QA_SOUND_FAMILY_Q3;
}
static char *owner_key(const qa_presentation_owner *owner) {
  char *key = qa_presentation_owner_key(owner);
  if (key == NULL) qa_range_error("Out of memory");
  return key;
}
static qa_presentation_owner interned_owner(qa_app_application_audio *audio, qa_presentation_owner owner) {
  owner.provider = intern(audio, owner.provider);
  return owner;
}
static bool owner_retired(const qa_app_application_audio *audio, const char *key) {
  return map_find(&audio->retired_owners, key) != NULL;
}
static bool owner_revision(const qa_app_application_audio *audio, const char *key, double *out) {
  const map_entry *entry = map_find(&audio->owner_revisions, key);
  if (entry == NULL) return false;
  *out = entry->number;
  return true;
}
static double revision_or(const qa_app_application_audio *audio, const char *key, double fallback) {
  double value;
  return owner_revision(audio, key, &value) ? value : fallback;
}
static int64_t channel_number(double channel) {
  if (!isfinite(channel) || channel != trunc(channel)) qa_range_error("Invalid source sound channel");
  return (int64_t)channel;
}
static void music_print(void *ctx, const char *text) { print_text(ctx, text); }

double qa_app_audio_now_milliseconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* ---- engine closures ---- */
static double engine_milliseconds(void *ctx) {
  qa_app_application_audio *audio = ctx;
  return trunc(audio->now(audio->now_ctx));
}
static double engine_random(void *ctx) {
  qa_app_application_audio *audio = ctx;
  return qa_game_random_rand(&audio->random);
}
static int64_t q2_random(void *ctx) {
  qa_app_application_audio *audio = ctx;
  return qa_game_random_rand(&audio->random);
}
static bool default_team_game(void *ctx) { (void)ctx; return false; }

static void free_cgame_record(cgame_record *record) { free(record->operations); }
static void reset_cgame_frames(qa_app_application_audio *audio) {
  size_t count = 0;
  cgame_record *records = qa_event_ring_drain_all(&audio->cgame_frames, &count);
  for (size_t i = 0; i < count; i++) free_cgame_record(&records[i]);
  free(records);
  qa_event_ring_reset(&audio->cgame_frames);
}
static void clear_statics(qa_app_application_audio *audio) {
  for (size_t i = 0; i < audio->static_count; i++) free(audio->statics[i].seats);
  audio->static_count = 0;
}

/* TS effectsVolume / musicVolume setters. */
static void set_cvar_number(qa_cvar_registry *cvars, const char *name, double value) {
  char text[40];
  qa_net_js_number(value, text, sizeof text);
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  if (qa_cvar_registry_set(cvars, name, text, false, &snapshot)) qa_cvar_read_free(&snapshot);
}

static void open_failure(qa_app_application_audio *audio, qa_sdl_audio_open_result result) {
  (void)audio;
  /* The engine port reports SdlAudioUnavailableError as a result code
   * and keeps the platform's message to itself. */
  plain_error(result == QA_SDL_AUDIO_OPEN_FAILED ? "Out of memory" : "Audio output is unavailable");
}

qa_app_application_audio *qa_app_application_audio_create(qa_app_loaded_content *content,
    double (*now)(void *ctx), void *now_ctx, double seed, const char *character_model,
    qa_app_audio_print_fn print, void *print_ctx, const qa_app_audio_options *options_in) {
  qa_app_audio_options options;
  if (options_in != NULL) options = *options_in; else memset(&options, 0, sizeof options);
  qa_app_application_audio *audio = calloc(1, sizeof *audio);
  if (audio == NULL) return NULL;
  audio->content = content;
  audio->now = now; audio->now_ctx = now_ctx;
  audio->character_model = xstrdup(character_model);
  audio->print = print; audio->print_ctx = print_ctx;
  audio->q3_team_game = options.q3_team_game != NULL ? options.q3_team_game : default_team_game;
  audio->q3_team_game_ctx = options.q3_team_game != NULL ? options.q3_team_game_ctx : NULL;
  audio->next_static_key = 1;
  audio->volume = 0.7;
  qa_game_random_init(&audio->random, (int64_t)seed);
  if (!qa_event_ring_init(&audio->ui_sounds, 1024, sizeof(ui_sound_record))
      || !qa_event_ring_init(&audio->effect_sounds, 8192, sizeof(qa_app_application_effect_sound))
      || !qa_event_ring_init(&audio->cgame_frames, 1024, sizeof(cgame_record))
      || !qa_event_ring_open_cursor(&audio->ui_sounds, &audio->ui_sounds_cursor, false)
      || !qa_event_ring_open_cursor(&audio->effect_sounds, &audio->effect_sounds_cursor, false)
      || !qa_event_ring_open_cursor(&audio->cgame_frames, &audio->cgame_frames_cursor, false))
    qa_range_error("Out of memory");
  qa_unified_audio_options engine_options;
  memset(&engine_options, 0, sizeof engine_options);
  engine_options.has_output_format = options.has_output_format;
  engine_options.output_format = options.output_format;
  engine_options.milliseconds = engine_milliseconds; engine_options.milliseconds_ctx = audio;
  engine_options.random = engine_random; engine_options.random_ctx = audio;
  audio->engine = qa_unified_audio_create(&engine_options);
  if (audio->engine == NULL) qa_range_error("Out of memory");
  qa_unified_audio_set_doppler_enabled(audio->engine, recipe_of(audio)->presentation.doppler.is_source);
  audio->music = qa_app_application_music_create(audio->engine, music_print, audio, QA_MUSIC_VOLUME_SOURCE,
                                                 options.music_controls, NULL, NULL);
  if (audio->music == NULL) qa_range_error("Out of memory");
  qa_app_application_audio_set_effects_volume(audio, options.has_effects_volume ? options.effects_volume : audio->volume);
  qa_app_application_audio_set_music_volume(audio, options.has_music_volume ? options.music_volume
      : qa_app_application_music_volume(audio->music));
  if (options.defer_output) return audio;
  const char *output_device = options.device_name;
  qa_sdl_audio_open_result result = qa_unified_audio_open_device(audio->engine, output_device, 0);
  if (result != QA_SDL_AUDIO_OPEN_OK) {
    if (output_device == NULL) { qa_app_application_audio_destroy(audio); open_failure(NULL, result); }
    char *text = xformat("Audio output %s unavailable: %s. Using system default.\n", output_device,
                         result == QA_SDL_AUDIO_OPEN_FAILED ? "Out of memory" : "Audio output is unavailable");
    print_text(audio, text); free(text);
    qa_sdl_audio_open_result fallback = qa_unified_audio_open_device(audio->engine, NULL, 0);
    if (fallback != QA_SDL_AUDIO_OPEN_OK) { qa_app_application_audio_destroy(audio); open_failure(NULL, fallback); }
  }
  return audio;
}

qa_unified_audio *qa_app_application_audio_engine(qa_app_application_audio *audio) { return audio->engine; }
qa_unified_audio_transfer qa_app_application_audio_prepare_output_transfer(qa_app_application_audio *audio,
    qa_app_application_audio *next) {
  return qa_unified_audio_prepare_output_transfer(audio->engine, next->engine);
}
qa_audio_output_format qa_app_application_audio_output_format(const qa_app_application_audio *audio) {
  return qa_unified_audio_output_format(audio->engine);
}
void qa_app_application_audio_bind_output_cvars(qa_app_application_audio *audio, qa_cvar_registry *cvars) {
  audio->output_cvars = cvars;
}
static char *selected_copy(const qa_app_application_audio *audio) {
  const char *selected = qa_unified_audio_selected_output(audio->engine);
  return selected == NULL ? NULL : xstrdup(selected);
}
void qa_app_application_audio_select_output_format(qa_app_application_audio *audio, qa_audio_output_format format) {
  char *selected = selected_copy(audio);
  qa_unified_audio_select_output(audio->engine, selected, &format, false);
  free(selected);
  if (audio->output_cvars != NULL) qa_app_write_audio_output_cvars(audio->output_cvars, qa_app_application_audio_output_format(audio));
}
void qa_app_application_audio_restart_output(qa_app_application_audio *audio) {
  char *selected = selected_copy(audio);
  qa_audio_output_format format = audio->output_cvars == NULL ? qa_app_application_audio_output_format(audio)
      : qa_app_read_audio_output_cvars(audio->output_cvars);
  qa_unified_audio_select_output(audio->engine, selected, &format, true);
  free(selected);
}
const char *qa_app_application_audio_selected_output(const qa_app_application_audio *audio) {
  return qa_unified_audio_selected_output(audio->engine);
}
char **qa_app_application_audio_output_device_names(qa_app_application_audio *audio, size_t *out_count) {
  return qa_unified_audio_output_device_names(audio->engine, out_count);
}
void qa_app_application_audio_select_output(qa_app_application_audio *audio, const char *device_name) {
  qa_unified_audio_select_output(audio->engine, device_name, NULL, false);
}
void qa_app_application_audio_detach_output(qa_app_application_audio *audio) {
  qa_unified_audio_detach_output(audio->engine);
}

uint8_t *qa_app_application_audio_load_haptic(void *ctx, qa_resource_request request, size_t *out_length) {
  qa_app_application_audio *audio = ctx;
  *out_length = 0;
  qa_content_mounted *mounts = mounts_for(audio, request.content);
  const qa_resolved_resource_reference *resource = NULL;
  if (!qa_content_mounted_resolve(mounts, request.path, &resource) || resource == NULL) return NULL;
  uint8_t *bytes = qa_content_mounted_read_reference(mounts, resource, out_length);
  if (bytes == NULL) qa_range_error("Out of memory");
  return bytes;
}
void qa_app_application_audio_bind_haptics(qa_app_application_audio *audio, qa_app_audio_haptics input) {
  audio->has_haptics = true; audio->haptics = input;
  input.bind_haptics(input.ctx, qa_app_application_audio_load_haptic, audio);
}

/* ---- geometry and environment ---- */
static const qa_scene_queries *scene_for(const qa_app_application_audio *audio, qa_seat_id seat) {
  for (size_t i = 0; i < audio->seat_scene_count; i++)
    if (qa_same_seat(audio->seat_scenes[i].seat, seat)) return &audio->seat_scenes[i].scene;
  return &audio->geometry_scene;
}
static qa_trace_policy shared_policy(void) {
  qa_trace_policy policy;
  memset(&policy, 0, sizeof policy);
  policy.kind = "q2"; policy.contents_mask = 3; policy.leaf_contents = "merged";
  return policy;
}
typedef struct geometry_trace { const qa_app_application_audio *audio; const qa_audio_listener *listener; } geometry_trace;
static qa_audio_geometry_hit geometry_trace_fn(qa_vec3 start, qa_vec3 end, void *ctx) {
  const geometry_trace *trace = ctx;
  const qa_scene_queries *scene = scene_for(trace->audio, trace->listener->seat);
  qa_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = start; query.end = end; query.shape.kind = "point";
  query.policy = shared_policy(); query.numeric = trace->audio->numeric;
  query.pass_actor = trace->listener->has_actor ? &trace->listener->actor : NULL;
  qa_trace_result result = scene->trace(scene->ctx, query);
  qa_audio_geometry_hit hit = {result.fraction, result.start_solid, result.all_solid};
  return hit;
}
static double geometry_transmission(void *ctx, const qa_audio_listener *listener, qa_vec3 position) {
  geometry_trace trace = {ctx, listener};
  return qa_geometry_transmission(listener->origin, position, geometry_trace_fn, &trace);
}
static void sync_geometry(qa_app_application_audio *audio) {
  bool enabled = (audio->volume_cvars == NULL ? 0 : qa_cvar_registry_variable_value(audio->volume_cvars, "s_geometryAcoustics")) != 0;
  if (enabled == audio->geometry_enabled) return;
  audio->geometry_enabled = enabled;
  qa_unified_audio_set_geometry_transmission(audio->engine, enabled && audio->has_geometry ? geometry_transmission : NULL,
                                             enabled && audio->has_geometry ? audio : NULL);
}
static qa_audio_trace environment_trace(qa_vec3 start, qa_vec3 end, qa_vec3 mins, qa_vec3 maxs, void *ctx) {
  const environment_seat *seat = ctx;
  const qa_app_application_audio *audio = seat->audio;
  const qa_audio_listener *current = NULL;
  for (size_t i = 0; i < audio->listener_count && current == NULL; i++)
    if (qa_same_seat(audio->listeners[i].seat, seat->seat)) current = &audio->listeners[i];
  if (current == NULL) plain_error("Reverb seat has no current listener");
  bool point = mins.x == 0 && mins.y == 0 && mins.z == 0 && maxs.x == 0 && maxs.y == 0 && maxs.z == 0;
  qa_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = start; query.end = end;
  query.shape.kind = point ? "point" : "box";
  if (!point) { query.shape.bounds.min = mins; query.shape.bounds.max = maxs; }
  query.policy = shared_policy(); query.numeric = audio->numeric;
  query.pass_actor = current->has_actor ? &current->actor : NULL;
  const qa_scene_queries *scene = scene_for(audio, current->seat);
  qa_trace_result result = scene->trace(scene->ctx, query);
  if (!is(result.kind, "q2")) plain_error("Audio trace did not honor its shared query policy");
  qa_audio_trace trace;
  trace.fraction = result.fraction; trace.end = result.end;
  /* TS result.surface?.material || null: an empty material is null too. */
  trace.material = result.surface != NULL && result.surface->material != NULL && result.surface->material[0] != '\0'
      ? result.surface->material : NULL;
  trace.sky = (qa_js_to_int32(result.surface == NULL ? 0 : result.surface->flags) & 4) != 0;
  return trace;
}
static void environment_warn(const char *message, void *ctx) { print_text(ctx, message); }
typedef struct environment_parse {
  qa_content_mounted *mounts; const qa_resolved_resource_reference *resource;
  qa_app_application_audio *audio;
  uint8_t *bytes; char *text;
  qa_reverb_environment *definitions; size_t count;
} environment_parse;
static void free_environment_parse(void *ctx) {
  environment_parse *parse = ctx;
  free(parse->bytes); parse->bytes = NULL; free(parse->text); parse->text = NULL;
}
static void parse_environment_file(void *ctx) {
  environment_parse *parse = ctx;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free_environment_parse, parse);
  size_t length = 0;
  parse->bytes = qa_content_mounted_read_reference(parse->mounts, parse->resource, &length);
  if (parse->bytes == NULL) qa_range_error("Out of memory");
  parse->text = xmalloc(length + 1);
  memcpy(parse->text, parse->bytes, length); parse->text[length] = '\0';
  if (!qa_parse_environments(parse->text, environment_warn, parse->audio, &parse->definitions, &parse->count))
    qa_range_error("Out of memory");
  qa_cleanup_pop(&cleanup);
  free_environment_parse(parse);
}
void qa_app_application_audio_prepare_environment(qa_app_application_audio *audio, const qa_scene_queries *scene) {
  const qa_executable_recipe *recipe = recipe_of(audio);
  const qa_provider_timing *timing = NULL;
  for (size_t i = 0; i < recipe->timing_count && timing == NULL; i++)
    if (is(recipe->timing[i].provider, recipe->engine_behavior.provider)) timing = &recipe->timing[i];
  if (timing == NULL) plain_error("Audio geometry has no numeric profile");
  audio->numeric = timing->numeric;
  audio->geometry_scene = *scene;
  audio->has_geometry = true;
  qa_unified_audio_set_geometry_transmission(audio->engine, audio->geometry_enabled ? geometry_transmission : NULL,
                                             audio->geometry_enabled ? audio : NULL);
  sync_geometry(audio);
  const qa_environment_selection *selection = &recipe->presentation.environment;
  if (selection->kind == QA_ENVIRONMENT_DISABLED) return;
  qa_resource_request request = selection->kind == QA_ENVIRONMENT_SELECTED ? selection->resource
      : (qa_resource_request){recipe->presentation.audio.content, "sound/default.environments"};
  qa_content_mounted *mounts = mounts_for(audio, request.content);
  const qa_resolved_resource_reference *resource = NULL;
  if (!qa_content_mounted_resolve(mounts, request.path, &resource) || resource == NULL) {
    if (selection->kind == QA_ENVIRONMENT_SELECTED)
      plain_error("Required environment resource is missing: %s/%s", request.content, request.path);
    return;
  }
  environment_parse parse = {mounts, resource, audio, NULL, NULL, NULL, 0};
  qa_caught_error caught;
  if (!qa_try(parse_environment_file, &parse, &caught)) {
    /* The default environment file is optional; a selected one is not. */
    if (selection->kind == QA_ENVIRONMENT_SELECTED || audio->closed) qa_rethrow(&caught);
    char *text = xformat("Sound environments unavailable: %s\n", caught.message);
    print_text(audio, text); free(text);
    return;
  }
  if (audio->closed) { qa_reverb_environments_free(parse.definitions, parse.count); return; }
  if (audio->has_environment) {
    /* Seats configured earlier keep borrowing the previous definitions. */
    audio->retired_environments = xrealloc(audio->retired_environments,
        (audio->retired_environment_count + 1) * sizeof *audio->retired_environments);
    audio->retired_environments[audio->retired_environment_count++] =
        (retired_environment){audio->environment_definitions, audio->environment_definition_count};
  }
  audio->environment_definitions = parse.definitions;
  audio->environment_definition_count = parse.count;
  audio->has_environment = true;
}

/* ---- volumes ---- */
static bool cvar_exists(qa_cvar_registry *cvars, const char *name) {
  qa_cvar_read read;
  memset(&read, 0, sizeof read);
  if (!qa_cvar_registry_find(cvars, name, &read)) return false;
  qa_cvar_read_free(&read);
  return true;
}
void qa_app_application_audio_bind_volume_cvars(qa_app_application_audio *audio, qa_cvar_registry *cvars) {
  if (!cvar_exists(cvars, "volume") || !cvar_exists(cvars, "bgmvolume")) return;
  audio->volume_cvars = cvars;
  qa_unified_audio_set_effects_volume(audio->engine, qa_app_application_audio_effects_volume(audio));
  qa_app_application_music_set_volume(audio->music, qa_app_application_audio_music_volume(audio));
}
qa_app_audio_music_preferences qa_app_application_audio_music_preferences(qa_app_application_audio *audio) {
  return qa_app_read_music_settings(audio->volume_cvars);
}
static bool music_shuffle(qa_app_application_audio *audio) {
  qa_app_audio_music_preferences preferences = qa_app_application_audio_music_preferences(audio);
  free(preferences.menu_track);
  return preferences.music_shuffle;
}
double qa_app_application_audio_effects_volume(const qa_app_application_audio *audio) {
  return audio->volume_cvars == NULL ? audio->volume
      : qa_js_max(0, qa_js_min(1, qa_cvar_registry_variable_value(audio->volume_cvars, "volume")));
}
void qa_app_application_audio_set_effects_volume(qa_app_application_audio *audio, double value) {
  if (audio->volume_cvars != NULL) set_cvar_number(audio->volume_cvars, "volume", value);
  qa_unified_audio_set_effects_volume(audio->engine, value);
  audio->volume = value;
}
double qa_app_application_audio_music_volume(const qa_app_application_audio *audio) {
  return audio->volume_cvars == NULL ? qa_app_application_music_volume(audio->music)
      : qa_js_max(0, qa_js_min(1, qa_cvar_registry_variable_value(audio->volume_cvars, "bgmvolume")));
}
void qa_app_application_audio_set_music_volume(qa_app_application_audio *audio, double value) {
  if (audio->volume_cvars != NULL) set_cvar_number(audio->volume_cvars, "bgmvolume", value);
  qa_app_application_music_set_volume(audio->music, value);
}

/* ---- queued records ---- */
void qa_app_application_audio_ui_sound(qa_app_application_audio *audio, const char *sound, qa_seat_id seat) {
  ui_sound_record record = {seat, intern(audio, sound)};
  qa_event_ring_admit(&audio->ui_sounds, &record);
}
void qa_app_application_audio_receive_effect_sounds(qa_app_application_audio *audio,
    const qa_app_application_effect_sound *sounds, size_t count) {
  if (count == 0) return;
  qa_app_application_effect_sound *copies = xmalloc(count * sizeof *copies);
  for (size_t i = 0; i < count; i++) {
    copies[i] = sounds[i];
    copies[i].content = intern(audio, sounds[i].content);
    copies[i].path = intern(audio, sounds[i].path);
    copies[i].playback_kind = intern(audio, sounds[i].playback_kind);
  }
  qa_event_ring_admit_all(&audio->effect_sounds, copies, count);
  free(copies);
}
void qa_app_application_audio_receive_cgame_frame(qa_app_application_audio *audio,
    const qa_app_q3_seat_audio_frame *frame) {
  cgame_record record;
  record.content = intern(audio, frame->content);
  record.seat = frame->seat;
  record.has_owner = frame->has_owner;
  record.owner = frame->has_owner ? intern(audio, frame->owner) : NULL;
  record.operation_count = frame->operation_count;
  record.operations = xmalloc(frame->operation_count * sizeof *record.operations);
  if (frame->operation_count != 0) memcpy(record.operations, frame->operations, frame->operation_count * sizeof *record.operations);
  for (size_t i = 0; i < record.operation_count; i++) {
    record.operations[i].play.owner = intern(audio, record.operations[i].play.owner);
    record.operations[i].loop.owner = intern(audio, record.operations[i].loop.owner);
  }
  if (qa_event_ring_append(&audio->cgame_frames, &record) == 0) {
    free(record.operations);
    qa_event_ring_admit(&audio->cgame_frames, &record); /* raises the TS capacity error */
  }
}

/* ---- sounds ---- */
static qa_sound_bank *bank_for(qa_app_application_audio *audio, qa_content_id content) {
  map_entry *existing = map_find(&audio->banks, content);
  if (existing != NULL) return existing->value;
  qa_content_mounted *mounts = mounts_for(audio, content);
  qa_sound_bank *bank = qa_sound_bank_create(mounts);
  if (bank == NULL) qa_range_error("Out of memory");
  map_insert(&audio->banks, content)->value = bank;
  return bank;
}
static actor_audio *actor_state(qa_app_application_audio *audio, qa_actor_id actor) {
  char key[64];
  snprintf(key, sizeof key, "%lld:%lld", (long long)actor.slot, (long long)actor.generation);
  map_entry *existing = map_find(&audio->actor_audio, key);
  if (existing != NULL) return existing->value;
  bool selected_q2 = product_of(audio, recipe_of(audio)->character.definition.content)->family == QA_GAME_FAMILY_Q2;
  actor_audio *state = xcalloc(1, sizeof *state);
  state->actor = actor;
  state->model = xstrdup(selected_q2 ? audio->character_model : "male");
  state->underwater = -1;
  map_insert(&audio->actor_audio, key)->value = state;
  return state;
}
static const qa_sound_asset *register_sound(qa_sound_bank *bank, const char *name, qa_sound_family family) {
  const qa_sound_asset *sound = NULL;
  if (!qa_sound_bank_register(bank, name, family, &sound)) qa_range_error("Out of memory");
  return sound;
}
static const qa_sound_asset *load_sound(qa_app_application_audio *audio, qa_content_id content, const char *path,
    qa_game_family family, const char *model, const char *fallback) {
  qa_sound_bank *bank = bank_for(audio, content);
  const qa_sound_asset *sound = NULL;
  if (path[0] != '*') sound = register_sound(bank, path, sound_family(family));
  else if (family == QA_GAME_FAMILY_Q2) {
    if (!qa_sound_bank_register_sexed_sound(bank, path, model, &sound)) qa_range_error("Out of memory");
  } else if (family == QA_GAME_FAMILY_Q3) {
    char *selected = xformat("player/%s/%s", model, path + 1);
    sound = register_sound(bank, selected, QA_SOUND_FAMILY_Q3);
    free(selected);
    if (sound == NULL) {
      char *alternate = xformat("player/%s/%s", fallback, path + 1);
      sound = register_sound(bank, alternate, QA_SOUND_FAMILY_Q3);
      free(alternate);
    }
  }
  if (sound == NULL) {
    char *key = xformat("%s/%s", content, path);
    if (map_find(&audio->warned, key) == NULL) {
      map_insert(&audio->warned, key);
      char *text = xformat("Sound unavailable: %s/%s\n", content, path);
      print_text(audio, text); free(text);
    }
    free(key);
  }
  return sound;
}
/* TS sound(content, path, family, actor = null, selectedModel?). */
static const qa_sound_asset *sound_for(qa_app_application_audio *audio, qa_content_id content, const char *path_in,
    qa_game_family family, const qa_actor_id *actor, const char *selected_model) {
  const char *path = strncmp(path_in, "sound/", 6) == 0 ? path_in + 6 : path_in;
  const char *model = selected_model != NULL ? selected_model
      : family == QA_GAME_FAMILY_Q2 ? (actor == NULL ? "male" : actor_state(audio, *actor)->model) : audio->character_model;
  const char *fallback = family == QA_GAME_FAMILY_Q3 && path[0] == '*'
      ? qa_app_q3_voice_fallback(catalog_of(audio), content, audio->q3_team_game(audio->q3_team_game_ctx)) : "";
  char *key = path[0] == '*' ? xformat("%s/%s/%s/%s/%s", content, family_name(family), path, model, fallback)
      : xformat("%s/%s/%s/", content, family_name(family), path);
  map_entry *prior = map_find(&audio->sounds, key);
  if (prior != NULL) { free(key); return prior->value; }
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, key);
  char *model_copy = xstrdup(model);
  qa_cleanup cleanup_model;
  qa_cleanup_push(&cleanup_model, free, model_copy);
  const qa_sound_asset *sound = load_sound(audio, content, path, family, model_copy, fallback);
  qa_cleanup_pop(&cleanup_model);
  qa_cleanup_pop(&cleanup);
  free(model_copy);
  map_insert(&audio->sounds, key)->value = (void *)sound;
  free(key);
  return sound;
}
void qa_app_application_audio_preload_sound(qa_app_application_audio *audio, qa_content_id content,
    const char *path, const char *selected_model) {
  sound_for(audio, content, path, product_of(audio, content)->family, NULL, selected_model);
}

typedef struct footstep_parse { const char *text; const char *source; qa_q3_player_footsteps footsteps; } footstep_parse;
static void parse_footsteps(void *ctx) {
  footstep_parse *parse = ctx;
  qa_q3_player_animation_config config;
  memset(&config, 0, sizeof config);
  qa_q3_parse_player_animation_config(parse->text, parse->source, NULL, &config);
  parse->footsteps = config.footsteps;
  qa_q3_player_animation_config_destroy(&config);
}
static qa_q3_player_footsteps q3_footsteps(qa_app_application_audio *audio, qa_content_id content) {
  map_entry *prior = map_find(&audio->footsteps, content);
  if (prior != NULL) return (qa_q3_player_footsteps)prior->number;
  qa_content_mounted *mounts = mounts_for(audio, content);
  char *path = xformat("models/players/%s/animation.cfg", audio->character_model);
  qa_content_opened_resource file;
  memset(&file, 0, sizeof file);
  bool found = qa_content_mounted_open(mounts, path, NULL, NULL, &file);
  free(path);
  if (!found) found = qa_content_mounted_open(mounts, "models/players/sarge/animation.cfg", NULL, NULL, &file);
  qa_q3_player_footsteps footsteps = QA_Q3_FOOTSTEPS_NORMAL;
  if (found) {
    char *text = xmalloc(file.byte_length + 1);
    memcpy(text, file.bytes, file.byte_length); text[file.byte_length] = '\0';
    free(file.bytes);
    footstep_parse parse = {text, file.reference != NULL ? file.reference->requested_path : NULL, QA_Q3_FOOTSTEPS_NORMAL};
    qa_caught_error caught;
    /* An unparsable authored config reports and falls back to default
     * footsteps, matching CG_ParseAnimationFile -- never a failed launch. */
    footsteps = qa_try(parse_footsteps, &parse, &caught) ? parse.footsteps : QA_Q3_FOOTSTEPS_NORMAL;
    free(text);
  }
  map_insert(&audio->footsteps, content)->number = footsteps;
  return footsteps;
}
void qa_app_application_audio_preload_character_footsteps(qa_app_application_audio *audio) {
  qa_content_id content = recipe_of(audio)->character.definition.content;
  if (product_of(audio, content)->family != QA_GAME_FAMILY_Q3) return;
  const char *selected = qa_q3_player_footsteps_name(q3_footsteps(audio, content));
  for (size_t i = 0; i < qa_q3_footstep_path_count; i++) {
    const char *kind = qa_q3_footstep_paths[i].key;
    if (!is(kind, selected) && !is(kind, "metal") && !is(kind, "splash")) continue;
    for (int index = 1; index <= 4; index++) {
      char *path = xformat("player/footsteps/%s%d.wav", qa_q3_footstep_paths[i].path, index);
      sound_for(audio, content, path, QA_GAME_FAMILY_Q3, NULL, NULL);
      free(path);
    }
  }
}

typedef struct haptics_call {
  qa_app_audio_haptics haptics; qa_content_id content; const char *sound;
  const qa_actor_id *actor; qa_audio_audience audience;
} haptics_call;
static void run_haptics(void *ctx) {
  haptics_call *call = ctx;
  qa_actor_id none; memset(&none, 0, sizeof none);
  call->haptics.sound_haptics(call->haptics.ctx, call->content, call->sound, call->actor != NULL,
                              call->actor != NULL ? *call->actor : none, call->audience);
}
static void play_haptics(qa_app_application_audio *audio, qa_content_id content, const qa_sound_asset *sound,
    const qa_actor_id *actor, qa_audio_audience audience) {
  if (!audio->has_haptics) return;
  haptics_call call = {audio->haptics, content, sound->name, actor, audience};
  qa_caught_error caught;
  if (!qa_try(run_haptics, &call, &caught) && !audio->closed) {
    char *text = xformat("Controller vibration unavailable: %s\n", caught.message);
    print_text(audio, text); free(text);
  }
}
static qa_audio_audience world_audience(void) { qa_audio_audience audience; memset(&audience, 0, sizeof audience); return audience; }
/* TS play(content, family, path, actor, origin, channel, volume, attenuation, delaySeconds = 0, audience = world, owner?). */
static void play_sound(qa_app_application_audio *audio, qa_content_id content, qa_game_family family, const char *path,
    const qa_actor_id *actor, const qa_vec3 *origin, double channel, double volume, double attenuation,
    double delay_seconds, qa_audio_audience audience, const qa_presentation_owner *owner) {
  char *key = owner_key(owner);
  double revision = revision_or(audio, key, 0);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, key);
  const qa_sound_asset *sound = sound_for(audio, content, path, family, actor, NULL);
  qa_cleanup_pop(&cleanup);
  bool stale = sound == NULL || audio->closed || owner_retired(audio, key) || revision_or(audio, key, 0) != revision;
  free(key);
  if (stale) return;
  play_haptics(audio, content, sound, actor, audience);
  qa_play_sound request;
  memset(&request, 0, sizeof request);
  request.sound = sound; request.family = sound_family(family);
  request.has_actor = actor != NULL; if (actor != NULL) request.actor = *actor;
  if (origin != NULL) { request.origin.kind = QA_SOUND_ORIGIN_FIXED; request.origin.position = *origin; }
  else if (actor == NULL) request.origin.kind = QA_SOUND_ORIGIN_LOCAL;
  else { request.origin.kind = QA_SOUND_ORIGIN_ACTOR; request.origin.actor = *actor; }
  request.audience = audience; request.channel = channel_number(channel);
  request.volume = volume; request.attenuation = attenuation;
  request.has_delay_seconds = true; request.delay_seconds = delay_seconds;
  qa_unified_audio_play(audio->engine, &request);
}

/* ---- music ---- */
static qa_app_music_source music_source(const qa_app_application_audio *audio, qa_content_id content) {
  const qa_catalog_product_expectation *product = product_of(audio, content);
  qa_app_music_source source = {content, product->family, product->edition, product->campaign};
  return source;
}
static qa_pcm_stream *open_fallback_music(void *ctx, const char *path) {
  music_fallback *fallback = ctx;
  qa_sound_bank *bank = bank_for(fallback->audio, fallback->alternate);
  qa_pcm_stream *stream = NULL;
  if (!qa_sound_bank_open_music(bank, path, fallback->alternate, &stream)) qa_range_error("Out of memory");
  return stream;
}
void qa_app_application_audio_play_music(qa_app_application_audio *audio, qa_content_id content,
    const char *track, const qa_presentation_owner *owner) {
  if (owner != NULL) {
    char *key = owner_key(owner);
    bool retired = owner_retired(audio, key);
    free(key);
    if (retired) return;
  }
  double request = ++audio->music_request;
  audio->has_music_owner = owner != NULL;
  if (owner != NULL) audio->music_owner = interned_owner(audio, *owner);
  qa_sound_bank *bank = bank_for(audio, content);
  qa_app_music_source source = music_source(audio, content);
  const char *alternate = NULL;
  bool has_alternate = qa_app_music_fallback(content, catalog_of(audio), &alternate);
  const char *const *tracks = NULL; size_t track_count = 0;
  if (source.family == QA_GAME_FAMILY_Q2) {
    map_entry *pending = map_find(&audio->playlists, content);
    if (pending == NULL) {
      track_list *list = xcalloc(1, sizeof *list);
      list->tracks = qa_app_mounted_music_tracks(mounts_for(audio, content), &list->count);
      if (list->tracks == NULL) { free(list); qa_range_error("Out of memory"); }
      pending = map_insert(&audio->playlists, content);
      pending->value = list;
    }
    const track_list *list = pending->value;
    tracks = (const char *const *)list->tracks; track_count = list->count;
  }
  if (audio->closed || request != audio->music_request) return;
  if (owner != NULL) {
    char *key = owner_key(owner);
    bool retired = owner_retired(audio, key);
    free(key);
    if (retired) return;
  }
  qa_app_open_music_track fallback = {NULL, NULL};
  if (has_alternate) {
    audio->fallback.audio = audio; audio->fallback.alternate = intern(audio, alternate);
    fallback.open = open_fallback_music; fallback.ctx = &audio->fallback;
  }
  qa_app_music_playlist playlist = {music_shuffle(audio), tracks, track_count};
  source.content = intern(audio, content);
  qa_app_application_music_play(audio->music, source, bank, track, fallback, &playlist);
}
typedef struct component_current {
  qa_app_application_audio *audio; double request; const char *key;
  bool (*active)(void *ctx); void *active_ctx;
} component_current;
static bool component_is_current(void *ctx) {
  const component_current *current = ctx;
  const qa_app_application_audio *audio = current->audio;
  return !audio->closed && current->request == audio->music_request && !owner_retired(audio, current->key)
      && current->active(current->active_ctx);
}
void qa_app_application_audio_play_component_media(qa_app_application_audio *audio,
    const qa_presentation_owner *owner, qa_content_id content,
    const qa_component_presentation_media_request *event, bool (*active)(void *ctx), void *active_ctx) {
  if (event->kind == QA_MEDIA_REQUEST_SHADER_REMAP) plain_error("Component media requests no shader remap audio");
  char *key = owner_key(owner);
  if (audio->closed || owner_retired(audio, key) || !active(active_ctx)) { free(key); return; }
  double request = ++audio->music_request;
  audio->has_music_owner = owner != NULL;
  if (owner != NULL) audio->music_owner = interned_owner(audio, *owner);
  qa_app_application_music_invalidate_pending(audio->music);
  component_current current = {audio, request, intern(audio, key), active, active_ctx};
  free(key);
  if (event->kind == QA_MEDIA_REQUEST_MUSIC_STOP) { qa_app_application_music_stop_playback(audio->music, "source"); return; }
  qa_sound_bank *bank = bank_for(audio, content);
  if (!component_is_current(&current)) return;
  qa_app_music_source source = music_source(audio, content);
  source.content = intern(audio, content);
  qa_app_application_music_play_tracks(audio->music, source, bank, event->intro, event->loop,
                                       component_is_current, &current);
}
static const char *world_entities(const qa_app_application_audio *audio) {
  qa_app_application_world world = qa_app_loaded_content_world(audio->content);
  if (is(world.kind, "q1-bsp")) return ((const qa_q1_map *)world.value)->world.entities;
  if (is(world.kind, "q2-bsp")) return ((const qa_q2_decoded_map *)world.value)->world.entities;
  if (is(world.kind, "q3-bsp")) return ((const qa_q3_world_geometry *)world.value)->entities;
  plain_error("Application content has no world");
}
void qa_app_application_audio_start_world_music(qa_app_application_audio *audio) {
  size_t count = 0;
  qa_q3_entity *entities = qa_q3_parse_entities(world_entities(audio), NULL, &count);
  const qa_q3_entity *world = NULL;
  for (size_t i = 0; i < count && world == NULL; i++)
    if (is(qa_q3_entity_get(&entities[i], "classname"), "worldspawn")) world = &entities[i];
  qa_content_id content = recipe_of(audio)->map.entities.content;
  const qa_catalog_product_expectation *product = product_of(audio, content);
  qa_app_world_key_value *pairs = xcalloc(world == NULL ? 1 : world->property_count, sizeof *pairs);
  size_t pair_count = world == NULL ? 0 : world->property_count;
  for (size_t i = 0; i < pair_count; i++) { pairs[i].key = world->properties[i].key; pairs[i].value = world->properties[i].value; }
  char *track = xstrdup(qa_app_world_music_track(pairs, pair_count, world != NULL, product->family, product->edition));
  free(pairs);
  qa_q3_entities_destroy(entities, count);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, track);
  qa_app_application_audio_play_music(audio, content, track, NULL);
  qa_cleanup_pop(&cleanup);
  free(track);
}

/* ---- loops ---- */
static void remove_loop_at(qa_app_application_audio *audio, size_t index) {
  memmove(&audio->loops[index], &audio->loops[index + 1], (audio->loop_count - index - 1) * sizeof *audio->loops);
  audio->loop_count--;
}
/* TS stopLoop(actor, audience = world, owner?). */
static void stop_loop(qa_app_application_audio *audio, qa_actor_id actor, qa_audio_audience audience, qa_provider_id owner) {
  for (size_t index = audio->loop_count; index-- > 0;) {
    const loop_audio *loop = &audio->loops[index];
    bool same_owner = loop->loop.owner == NULL ? owner == NULL : owner != NULL && strcmp(loop->loop.owner, owner) == 0;
    if (qa_same_actor(loop->loop.actor, actor) && same_owner
        && (audience.kind == QA_AUDIO_AUDIENCE_WORLD
            || (loop->loop.audience.kind == QA_AUDIO_AUDIENCE_SEAT && qa_same_seat(loop->loop.audience.seat, audience.seat))))
      remove_loop_at(audio, index);
  }
  qa_unified_audio_stop_loop(audio->engine, actor, audience, owner);
}
static void stop_owner_loops(qa_app_application_audio *audio, const qa_presentation_owner *owner) {
  size_t count = audio->loop_count;
  loop_audio *snapshot = xmalloc(count * sizeof *snapshot);
  if (count != 0) memcpy(snapshot, audio->loops, count * sizeof *snapshot);
  for (size_t i = 0; i < count; i++)
    if (snapshot[i].has_presentation_owner && qa_same_presentation_owner(owner, snapshot[i].presentation_owner))
      stop_loop(audio, snapshot[i].loop.actor, snapshot[i].loop.audience, snapshot[i].loop.owner);
  free(snapshot);
}

/* ---- events ---- */
static void play_character(qa_app_application_audio *audio, qa_content_id content, const qa_actor_id *actor,
    const char *path, double channel, qa_audio_audience audience) {
  play_sound(audio, content, QA_GAME_FAMILY_Q3, path, actor, NULL, channel, 1, 1, 0, audience, NULL);
}
static void character_sound(qa_app_application_audio *audio, qa_content_id content,
    const qa_app_sim_q3_character_presentation_event *event, qa_audio_audience audience) {
  qa_actor_id actor = event->actor;
  actor_audio *state = actor_state(audio, actor);
  int32_t kind = qa_js_to_int32(event->event) & ~0x300;
  switch (kind) {
    case QA_Q3_EV_PAIN: {
      if (qa_js_to_int32(event->time_milliseconds - state->pain_time) < 500) return;
      state->pain_time = event->time_milliseconds;
      double health = event->parameter;
      char path[32];
      snprintf(path, sizeof path, "*pain%d_1.wav", health < 25 ? 25 : health < 50 ? 50 : health < 75 ? 75 : 100);
      play_character(audio, content, &actor, path, 3, audience);
      break;
    }
    case QA_Q3_EV_DEATH1: case QA_Q3_EV_DEATH2: case QA_Q3_EV_DEATH3: {
      char path[32];
      snprintf(path, sizeof path, "*death%d.wav", kind - QA_Q3_EV_DEATH1 + 1);
      play_character(audio, content, &actor, path, 3, audience);
      break;
    }
    case QA_Q3_EV_JUMP_PAD: {
      const qa_vec3 *origin = NULL;
      for (size_t i = 0; audio->has_snapshot && i < audio->snapshot_body_count && origin == NULL; i++)
        if (qa_same_actor(audio->snapshot_bodies[i].actor, actor)) origin = &audio->snapshot_bodies[i].origin;
      if (origin != NULL) {
        qa_vec3 at = *origin;
        play_sound(audio, content, QA_GAME_FAMILY_Q3, "world/jumppad.wav", NULL, &at, 3, 1, 1, 0, audience, NULL);
      }
      play_character(audio, content, &actor, "*jump1.wav", 3, audience);
      break;
    }
    case QA_Q3_EV_JUMP: play_character(audio, content, &actor, "*jump1.wav", 3, audience); break;
    case QA_Q3_EV_TAUNT: play_character(audio, content, &actor, "*taunt.wav", 3, audience); break;
    case QA_Q3_EV_FALL_SHORT: play_character(audio, content, &actor, "player/land1.wav", 0, audience); break;
    case QA_Q3_EV_FALL_MEDIUM: play_character(audio, content, &actor, "*pain100_1.wav", 3, audience); break;
    case QA_Q3_EV_FALL_FAR: state->pain_time = event->time_milliseconds;
      play_character(audio, content, &actor, "*fall1.wav", 0, audience); break;
    case QA_Q3_EV_WATER_TOUCH: play_character(audio, content, &actor, "player/watr_in.wav", 0, audience); break;
    case QA_Q3_EV_WATER_LEAVE: play_character(audio, content, &actor, "player/watr_out.wav", 0, audience); break;
    case QA_Q3_EV_WATER_UNDER: play_character(audio, content, &actor, "player/watr_un.wav", 0, audience); break;
    case QA_Q3_EV_WATER_CLEAR: play_character(audio, content, &actor, "*gasp.wav", 0, audience); break;
    case QA_Q3_EV_FOOTSTEP: case QA_Q3_EV_FOOTSTEP_METAL:
    case QA_Q3_EV_FOOTSPLASH: case QA_Q3_EV_FOOTWADE: case QA_Q3_EV_SWIM: {
      const char *step = kind == QA_Q3_EV_FOOTSTEP ? qa_q3_player_footsteps_name(q3_footsteps(audio, content))
          : kind == QA_Q3_EV_FOOTSTEP_METAL ? "metal" : "splash";
      const char *name = is(step, "normal") ? "step" : is(step, "metal") ? "clank" : step;
      char path[64];
      snprintf(path, sizeof path, "player/footsteps/%s%d.wav", name, (qa_game_random_rand(&audio->random) & 3) + 1);
      play_character(audio, content, &actor, path, 5, audience);
      break;
    }
    case QA_Q3_EV_PLAYER_TELEPORT_IN: play_character(audio, content, &actor, "world/telein.wav", 0, audience); break;
    case QA_Q3_EV_PLAYER_TELEPORT_OUT: play_character(audio, content, &actor, "world/teleout.wav", 0, audience); break;
    case QA_Q3_EV_GIB_PLAYER: play_character(audio, content, &actor, "player/gibsplt1.wav", 5, audience); break;
    case QA_Q3_EV_ITEM_POP: case QA_Q3_EV_ITEM_RESPAWN: play_character(audio, content, &actor, "items/respawn1.wav", 0, audience); break;
    case QA_Q3_EV_CHANGE_WEAPON: play_character(audio, content, &actor, "weapons/change.wav", 0, audience); break;
    case QA_Q3_EV_STOPLOOPINGSOUND: stop_loop(audio, actor, audience, NULL); break;
    default: break;
  }
}
/* TS chat(content, family, target, audience = world). */
static void chat(qa_app_application_audio *audio, qa_content_id content, qa_game_family family,
    const qa_actor_id *target, qa_audio_audience audience) {
  size_t count = audio->listener_count;
  qa_audio_listener *listeners = xmalloc(count * sizeof *listeners);
  if (count != 0) memcpy(listeners, audio->listeners, count * sizeof *listeners);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, listeners);
  for (size_t i = 0; i < count; i++) {
    const qa_audio_listener *listener = &listeners[i];
    if (audience.kind == QA_AUDIO_AUDIENCE_SEAT && !qa_same_seat(listener->seat, audience.seat)) continue;
    if (target != NULL && !(listener->has_actor && qa_same_actor(listener->actor, *target))) continue;
    qa_audio_audience seat = {QA_AUDIO_AUDIENCE_SEAT, listener->seat};
    play_sound(audio, content, family, family == QA_GAME_FAMILY_Q3 ? "player/talk.wav" : "misc/talk.wav",
               NULL, NULL, 0, 1, 0, 0, seat, NULL);
  }
  qa_cleanup_pop(&cleanup);
  free(listeners);
}
static bool q1_channel(const qa_q1_event *event, double *out) {
  const char *channel = event->sound_channel;
  if (is(channel, "auto")) *out = 0;
  else if (is(channel, "weapon")) *out = 1;
  else if (is(channel, "voice")) *out = 2;
  else if (is(channel, "item")) *out = 3;
  else if (is(channel, "body")) *out = 4;
  else if (channel != NULL) *out = qa_js_string_to_number(channel); /* the numeric Q1SoundChannel members */
  else *out = event->channel;
  return true;
}
static bool snapshot_has_actor(const qa_app_application_audio *audio, qa_actor_id actor) {
  for (size_t i = 0; i < audio->snapshot_actor_count; i++)
    if (qa_same_actor(audio->snapshot_actors[i], actor)) return true;
  return false;
}
typedef struct event_scope {
  qa_app_application_audio *audio; const char *key; double revision; double sequence;
} event_scope;
static bool event_current(const event_scope *scope) {
  const qa_app_application_audio *audio = scope->audio;
  return !audio->closed && !owner_retired(audio, scope->key)
      && scope->sequence > revision_or(audio, scope->key, -1) && revision_or(audio, scope->key, 0) == scope->revision;
}
static bool is_rerelease(const qa_app_application_audio *audio, qa_content_id content) {
  return is(product_of(audio, content)->edition, "rerelease");
}
static void receive_owner_event(qa_app_application_audio *audio, const qa_app_sim_presentation_event *source, bool music) {
  qa_presentation_owner owner = source->presentation_owner.owner;
  char *key = owner_key(&owner);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, key);
  if (is(source->presentation_owner.kind, "retired")) map_insert(&audio->retired_owners, key);
  map_insert(&audio->owner_revisions, key)->number = source->sequence;
  if (is(source->presentation_owner.kind, "refreshed")) {
    if (qa_same_presentation_owner(audio->has_music_owner ? &audio->music_owner : NULL, owner)) {
      audio->music_request++; qa_app_application_music_invalidate_pending(audio->music);
    }
    qa_cleanup_pop(&cleanup); free(key);
    return;
  }
  for (size_t index = audio->static_count; index-- > 0;) {
    static_audio *sound = &audio->statics[index];
    if (!qa_same_presentation_owner(sound->has_owner ? &sound->owner : NULL, owner)) continue;
    for (size_t s = 0; s < sound->seat_count; s++) qa_unified_audio_remove_static_sound(audio->engine, sound->seats[s], sound->key);
    free(sound->seats);
    memmove(&audio->statics[index], &audio->statics[index + 1], (audio->static_count - index - 1) * sizeof *audio->statics);
    audio->static_count--;
  }
  stop_owner_loops(audio, &owner);
  if (qa_same_presentation_owner(audio->has_music_owner ? &audio->music_owner : NULL, owner)) {
    audio->music_request++; audio->has_music_owner = false;
    qa_app_application_music_stop_playback(audio->music, "source");
    if (music) qa_app_application_audio_start_world_music(audio);
  }
  if (qa_same_presentation_owner(audio->has_pause_owner ? &audio->pause_owner : NULL, owner)) {
    audio->has_pause_owner = false;
    if (music) {
      static const char *const resume[] = {"resume"};
      qa_app_application_music_cd_command(audio->music, resume, 1, NULL, NULL);
    }
  }
  qa_cleanup_pop(&cleanup); free(key);
}
static void play_q2_event_sound(qa_app_application_audio *audio, const qa_app_sim_presentation_event *source,
    const qa_actor_id *actor, const qa_app_q2_event_sound *sound, qa_audio_audience audience, const qa_presentation_owner *owner) {
  play_sound(audio, source->content, QA_GAME_FAMILY_Q2, sound->path, actor, NULL, sound->channel, sound->volume,
             sound->attenuation, sound->delay_seconds, audience, owner);
}
static void receive_one(qa_app_application_audio *audio, const qa_app_sim_presentation_event *source,
    qa_audio_audience audience, bool music) {
  if (is(source->kind, "presentation-owner")) { receive_owner_event(audio, source, music); return; }
  const qa_presentation_owner *owner = source->has_owner ? &source->owner : NULL;
  char *key_text = owner_key(owner);
  const char *key = intern(audio, key_text);
  free(key_text);
  event_scope scope = {audio, key, revision_or(audio, key, 0), source->sequence};
  if (!event_current(&scope)) return;
  if (is(source->kind, "music")) {
    if (!music) return;
    if (is(source->music.kind, "cd-track")) {
      char track[40];
      qa_net_js_number(source->music.track, track, sizeof track);
      qa_app_application_audio_play_music(audio, source->content, track, owner);
    } else {
      audio->has_pause_owner = owner != NULL;
      if (owner != NULL) audio->pause_owner = interned_owner(audio, *owner);
      const char *argument[] = {source->music.paused ? "pause" : "resume"};
      qa_app_application_music_cd_command(audio->music, argument, 1, NULL, NULL);
    }
  } else if (is(source->kind, "q1")) {
    const qa_q1_event *event = source->q1;
    if (is(event->kind, "sound")) {
      double channel = 0;
      q1_channel(event, &channel);
      play_sound(audio, source->content, QA_GAME_FAMILY_Q1, event->path, event->has_actor ? &event->actor : NULL,
                 event->has_origin ? &event->origin : NULL, channel, event->volume, event->attenuation, 0, audience, owner);
    } else if (is(event->kind, "stop-sound")) {
      qa_sound_channel_command command = qa_source_sound_channel(QA_SOUND_FAMILY_Q1, channel_number(event->channel));
      if (command.kind == QA_SOUND_CHANNEL_REPLACE_ACTOR) plain_error("NetQuake stop sound requires a nonnegative channel");
      qa_unified_audio_stop_sound(audio->engine, event->actor, command.kind == QA_SOUND_CHANNEL_AUTO ? NULL : command.channel, audience);
    } else if (is(event->kind, "ambient")) {
      const qa_sound_asset *sound = sound_for(audio, source->content, event->path, QA_GAME_FAMILY_Q1, NULL, NULL);
      if (event_current(&scope) && sound != NULL && sound->pcm.loop_start >= 0) {
        if (audio->static_count == audio->static_cap) {
          audio->static_cap = audio->static_cap ? audio->static_cap * 2 : 16;
          audio->statics = xrealloc(audio->statics, audio->static_cap * sizeof *audio->statics);
        }
        static_audio *added = &audio->statics[audio->static_count++];
        memset(added, 0, sizeof *added);
        added->key = audio->next_static_key++;
        added->has_owner = owner != NULL;
        if (owner != NULL) added->owner = interned_owner(audio, *owner);
        added->audience = audience; added->sound = sound; added->origin = event->origin;
        added->volume = trunc(event->volume * 255); added->attenuation = trunc(event->attenuation * 64);
      }
    }
  } else if (is(source->kind, "q1-level") && is(source->q1_level->kind, "finale")) {
    if (music) {
      char track[40];
      qa_net_js_number(source->q1_level->track, track, sizeof track);
      qa_app_application_audio_play_music(audio, source->content, track, owner);
    }
  } else if (is(source->kind, "q2")) {
    const qa_q2_presentation_event *event = source->q2;
    const qa_actor_id *actor = event->has_actor ? &event->actor : NULL;
    if (is(event->kind, "music")) {
      if (music) qa_app_application_audio_play_music(audio, source->content, event->track, owner);
    } else if (is(event->kind, "sound")) {
      qa_provider_id loop_owner = event->has_loop_owner ? event->loop_owner : NULL;
      if (is(event->loop, "stop") && actor != NULL) stop_loop(audio, *actor, audience, loop_owner);
      else if (is(event->loop, "start") && actor != NULL) {
        const qa_sound_asset *sound = sound_for(audio, source->content, event->path, QA_GAME_FAMILY_Q2, actor, NULL);
        if (!event_current(&scope)) return;
        stop_loop(audio, *actor, audience, loop_owner);
        qa_unified_audio_update_actor(audio->engine, *actor, event->origin);
        if (sound != NULL) {
          if (audio->loop_count == audio->loop_cap) {
            audio->loop_cap = audio->loop_cap ? audio->loop_cap * 2 : 16;
            audio->loops = xrealloc(audio->loops, audio->loop_cap * sizeof *audio->loops);
          }
          loop_audio *loop = &audio->loops[audio->loop_count++];
          memset(loop, 0, sizeof *loop);
          loop->has_presentation_owner = owner != NULL;
          if (owner != NULL) loop->presentation_owner = interned_owner(audio, *owner);
          loop->loop.sound = sound; loop->loop.family = QA_SOUND_FAMILY_Q2; loop->loop.actor = *actor;
          loop->loop.owner = intern(audio, loop_owner);
          loop->loop.origin.kind = QA_SOUND_ORIGIN_ACTOR; loop->loop.origin.actor = *actor;
          loop->loop.audience = audience; loop->loop.volume = event->volume; loop->loop.attenuation = event->attenuation;
          loop->loop.frame_number = 0; loop->loop.lifetime = QA_LOOP_LIFETIME_FRAME;
        }
      } else {
        bool live = actor != NULL && audio->has_snapshot && snapshot_has_actor(audio, *actor);
        qa_vec3 origin = event->origin;
        play_sound(audio, source->content, QA_GAME_FAMILY_Q2, event->path, actor, live ? NULL : &origin,
                   event->channel, event->volume, event->attenuation, 0, audience, owner);
      }
    } else if (is(event->kind, "monster-muzzleflash")) {
      qa_app_q2_event_sound sound;
      int count = qa_app_q2_monster_muzzle_sounds(event->flash, q2_random, audio, is_rerelease(audio, source->content), &sound);
      if (count < 0) {
        char *text = xformat("Unresolved Quake II monster muzzle sound %s\n", QA_JS_NUMBER(event->flash));
        print_text(audio, text); free(text);
      } else if (count == 1) play_q2_event_sound(audio, source, actor, &sound, audience, owner);
    } else if (is(event->kind, "entity-event")) {
      qa_app_q2_event_sound sound;
      if (qa_app_q2_entity_sound(event->event, q2_random, audio, &sound))
        play_q2_event_sound(audio, source, actor, &sound, audience, owner);
    } else if (is(event->kind, "print") && is(event->level, "chat")) {
      chat(audio, source->content, QA_GAME_FAMILY_Q2, actor, audience);
    }
  } else if (is(source->kind, "q2-rerelease")) {
    const qa_q2_rerelease_event *event = source->q2_rerelease;
    if (is(event->kind, "mission-objective") && event->talk_sound)
      chat(audio, source->content, QA_GAME_FAMILY_Q2, event->has_actor ? &event->actor : NULL, audience);
  } else if (is(source->kind, "q2-player")) {
    const qa_q2_player_event *event = source->q2_player;
    if (is(event->kind, "userinfo")) {
      actor_audio *state = actor_state(audio, event->actor);
      const char *skin = event->skin != NULL ? event->skin : "";
      const char *slash = strchr(skin, '/');
      size_t length = slash == NULL ? strlen(skin) : (size_t)(slash - skin);
      char *model = xmalloc(length + 1);
      memcpy(model, skin, length); model[length] = '\0';
      free(state->model);
      if (length == 0) { free(model); model = xstrdup("male"); }
      state->model = model;
      if (event->name != NULL && event->name[0] == '\0') stop_loop(audio, event->actor, audience, NULL);
    } else if (is(event->kind, "view")) actor_state(audio, event->actor)->underwater = event->view.underwater ? 1 : 0;
    else if (is(event->kind, "chase")) {
      actor_audio *state = actor_state(audio, event->actor);
      state->has_chase = event->has_chase_target;
      if (event->has_chase_target) state->chase = event->chase_target;
    } else if (is(event->kind, "print") && is(event->level, "chat"))
      chat(audio, source->content, QA_GAME_FAMILY_Q2, event->has_target ? &event->target : NULL, audience);
  } else if (is(source->kind, "q2-weapon")) {
    const qa_q2w_event *event = source->q2_weapon;
    if (event->kind != QA_Q2W_EVENT_MUZZLEFLASH) return;
    qa_app_q2_event_sound sounds[3];
    size_t count = qa_app_q2_muzzle_sounds(event->flash, event->silenced, q2_random, audio,
                                           is_rerelease(audio, source->content), sounds);
    for (size_t i = 0; i < count; i++) play_q2_event_sound(audio, source, event->actor, &sounds[i], audience, owner);
  } else if (is(source->kind, "q3-character")) {
    character_sound(audio, source->content, &source->q3_character, audience);
  }
}
void qa_app_application_audio_receive(qa_app_application_audio *audio,
    const qa_app_sim_presentation_event *events, size_t count, qa_audio_audience audience, bool music) {
  for (size_t i = 0; i < count; i++) receive_one(audio, &events[i], audience, music);
}

/* ---- command ---- */
typedef struct listed_sound { const qa_sound_asset *sound; } listed_sound;
static int compare_listed(const void *a, const void *b, void *ctx) {
  (void)ctx;
  return qa_js_locale_compare(((const listed_sound *)a)->sound->name, ((const listed_sound *)b)->sound->name);
}
static void command_print(const qa_app_audio_command_request *request, qa_app_application_audio *audio, const char *text) {
  if (request->print != NULL) request->print(request->print_ctx, text);
  else print_text(audio, text);
}
static void add_listed(listed_sound **list, size_t *count, size_t *cap, const qa_sound_asset *sound) {
  for (size_t i = 0; i < *count; i++) if ((*list)[i].sound == sound) return;
  if (*count == *cap) { *cap = *cap ? *cap * 2 : 16; *list = xrealloc(*list, *cap * sizeof **list); }
  (*list)[(*count)++].sound = sound;
}
static bool lower_in(const char *text, const char *const *values, size_t count) {
  if (text == NULL) text = "";
  char *lower = qa_js_to_lower_case(text);
  if (lower == NULL) qa_range_error("Out of memory");
  bool found = false;
  for (size_t i = 0; i < count && !found; i++) found = strcmp(lower, values[i]) == 0;
  free(lower);
  return found;
}
bool qa_app_application_audio_command(qa_app_application_audio *audio, const qa_app_audio_command_request *request) {
  bool known = false;
  for (size_t i = 0; i < qa_app_application_audio_command_count && !known; i++)
    known = is(qa_app_application_audio_commands[i], request->name);
  if (!known) return false;
  if (audio->closed) plain_error("Sound system is closed");
  qa_app_audio_print_fn print = request->print != NULL ? request->print : audio->print;
  void *print_ctx = request->print != NULL ? request->print_ctx : audio->print_ctx;
  const char *name = request->name;
  if (is(name, "snd_restart")) { qa_app_application_audio_restart_output(audio); return true; }
  if (is(name, "music")) {
    audio->has_music_owner = false; audio->music_request++;
    qa_app_application_music_invalidate_pending(audio->music);
    qa_app_application_music_music_command(audio->music, request->args, request->arg_count, print, print_ctx);
    return true;
  }
  if (is(name, "cd")) {
    static const char *const replacing[] = {"play", "loop", "stop", "off", "reset"};
    static const char *const pausing[] = {"pause", "resume"};
    const char *first = request->arg_count > 0 ? request->args[0] : NULL;
    if (lower_in(first, replacing, 5)) {
      audio->has_music_owner = false; audio->music_request++;
      qa_app_application_music_invalidate_pending(audio->music);
    }
    if (lower_in(first, pausing, 2)) audio->has_pause_owner = false;
    qa_app_application_music_cd_command(audio->music, request->args, request->arg_count, print, print_ctx);
    return true;
  }
  if (is(name, "soundinfo") || is(name, "s_info")) {
    qa_unified_audio_output_configuration output;
    bool has_output = qa_unified_audio_output_configuration_get(audio->engine, &output);
    char *text = xformat("Sound output: %s\n", qa_unified_audio_output_state_name(qa_unified_audio_output_state_of(audio->engine)));
    command_print(request, audio, text); free(text);
    if (has_output) {
      text = xformat("SDL device: %s\n", output.device_name != NULL ? output.device_name : "system default");
      command_print(request, audio, text); free(text);
      text = xformat("%lld Hz, %lld %s, %lld-bit PCM; buffer %lld frames\n", (long long)output.sample_rate,
                     (long long)output.channels, output.channels == 1 ? "channel" : "channels",
                     (long long)output.sample_bits, (long long)output.buffer_frames);
      command_print(request, audio, text); free(text);
      long long queued = (long long)qa_unified_audio_queued_frames(audio->engine);
      text = xformat("%lld queued frames, maximum %lld; mixed clock %lld\n", queued,
                     (long long)output.maximum_queued_frames, (long long)qa_unified_audio_sample_clock(audio->engine));
      command_print(request, audio, text); free(text);
    }
    char effects[128], music[128];
    qa_js_to_precision(qa_app_application_audio_effects_volume(audio), 6, effects, sizeof effects);
    qa_js_to_precision(qa_app_application_audio_music_volume(audio), 6, music, sizeof music);
    char effects_number[40], music_number[40];
    qa_net_js_number(qa_js_string_to_number(effects), effects_number, sizeof effects_number);
    qa_net_js_number(qa_js_string_to_number(music), music_number, sizeof music_number);
    text = xformat("Effects volume %s; music volume %s; listeners %zu\n", effects_number, music_number, audio->listener_count);
    command_print(request, audio, text); free(text);
    return true;
  }
  if (is(name, "soundlist") || is(name, "s_list")) {
    listed_sound *list = NULL; size_t count = 0, cap = 0;
    for (size_t i = 0; i < audio->sounds.count; i++) {
      const map_entry *entry = map_find(&audio->sounds, audio->sounds.order[i]);
      if (entry->value != NULL) add_listed(&list, &count, &cap, entry->value);
      else {
        char *text = xformat("unavailable: %s\n", entry->key);
        command_print(request, audio, text); free(text);
      }
    }
    for (size_t i = 0; i < request->registration_count; i++) {
      const qa_q3_pres_sound_registration *entry = &request->registrations[i];
      if (entry->sound != NULL) add_listed(&list, &count, &cap, entry->sound);
      else {
        char *text = xformat("unavailable: %s\n", entry->path);
        command_print(request, audio, text); free(text);
      }
    }
    qa_js_stable_sort(list, count, sizeof *list, compare_listed, NULL);
    double bytes = 0;
    for (size_t i = 0; i < count; i++) {
      const qa_sound_asset *sound = list[i].sound;
      double byte_length = (double)sound->pcm.sample_count * 2;
      bytes += byte_length;
      char *text = xformat("%s %lld Hz %dch 16-bit %lld frames %s bytes : %s [%s]\n", sound->pcm.loop_start < 0 ? " " : "L",
                           (long long)sound->pcm.sample_rate, sound->pcm.channels, (long long)sound->pcm.frame_count,
                           QA_JS_NUMBER(byte_length), sound->name, sound->resource);
      command_print(request, audio, text); free(text);
    }
    char *text = xformat("Decoded PCM storage: %s bytes in %zu registered resources\n", QA_JS_NUMBER(bytes), count);
    command_print(request, audio, text); free(text);
    free(list);
    return true;
  }
  if (is(name, "stopsound") || is(name, "s_stop")) {
    qa_app_application_music_stop_playback(audio->music, "manual");
    qa_unified_audio_stop_all(audio->engine);
    clear_statics(audio); audio->loop_count = 0;
    qa_event_ring_reset(&audio->ui_sounds); qa_event_ring_reset(&audio->effect_sounds); reset_cgame_frames(audio);
    return true;
  }
  if (request->arg_count == 0) plain_error("Usage: play <sound> [sound ...]");
  const qa_audio_listener *listener = NULL;
  for (size_t i = 0; i < audio->listener_count && listener == NULL; i++)
    if (!request->has_seat || qa_same_seat(audio->listeners[i].seat, request->seat)) listener = &audio->listeners[i];
  if (listener == NULL) plain_error("Sound playback requires a local listener");
  qa_audio_listener chosen = *listener;
  qa_content_id content = recipe_of(audio)->presentation.audio.content;
  qa_game_family family = product_of(audio, content)->family;
  for (size_t i = 0; i < request->arg_count; i++) {
    const char *argument = request->args[i];
    char *path = strchr(argument, '.') != NULL ? xstrdup(argument) : xformat("%s.wav", argument);
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, path);
    const qa_sound_asset *sound = sound_for(audio, content, path, family, chosen.has_actor ? &chosen.actor : NULL, NULL);
    if (audio->closed) plain_error("Sound system closed during sound loading");
    if (sound == NULL) plain_error("Sound unavailable: %s/%s", content, path);
    qa_cleanup_pop(&cleanup);
    free(path);
    qa_play_sound play;
    memset(&play, 0, sizeof play);
    play.sound = sound; play.family = sound_family(family);
    play.origin.kind = QA_SOUND_ORIGIN_LOCAL;
    play.audience.kind = QA_AUDIO_AUDIENCE_SEAT; play.audience.seat = chosen.seat;
    play.channel = family == QA_GAME_FAMILY_Q3 ? 6 : 0; play.volume = 1; play.attenuation = 0;
    qa_unified_audio_play(audio->engine, &play);
  }
  return true;
}

/* ---- frame ---- */
typedef struct body_seen { qa_actor_id id; } body_seen;
static bool seen_before(const qa_actor_id *seen, size_t count, qa_actor_id id) {
  /* TS uniqueByActor: buckets by slot:generation, then sameActor. */
  for (size_t i = 0; i < count; i++)
    if (seen[i].slot == id.slot && seen[i].generation == id.generation && qa_same_actor(seen[i], id)) return true;
  return false;
}
static void capture_snapshot(qa_app_application_audio *audio, const qa_world_snapshot *snapshot,
    const qa_app_audio_seat_events *seat_events, size_t seat_event_count) {
  const qa_world_snapshot **snapshots = xmalloc((seat_event_count + 1) * sizeof *snapshots);
  size_t snapshot_count = 0;
  snapshots[snapshot_count++] = snapshot;
  for (size_t i = 0; i < seat_event_count; i++) {
    bool seen = false;
    for (size_t j = 0; j < snapshot_count && !seen; j++) seen = snapshots[j] == seat_events[i].snapshot;
    if (!seen) snapshots[snapshot_count++] = seat_events[i].snapshot;
  }
  size_t actor_total = 0, body_total = 0;
  for (size_t i = 0; i < snapshot_count; i++) { actor_total += snapshots[i]->actor_count; body_total += snapshots[i]->body_count; }
  free(audio->snapshot_actors); free(audio->snapshot_bodies);
  audio->snapshot_actors = xmalloc(actor_total * sizeof *audio->snapshot_actors);
  audio->snapshot_bodies = xmalloc(body_total * sizeof *audio->snapshot_bodies);
  audio->snapshot_actor_count = 0; audio->snapshot_body_count = 0;
  qa_actor_id *seen_bodies = xmalloc(body_total * sizeof *seen_bodies);
  for (size_t i = 0; i < snapshot_count; i++) {
    for (size_t a = 0; a < snapshots[i]->actor_count; a++) {
      qa_actor_id id = snapshots[i]->actors[a].id;
      if (snapshot_count > 1 && seen_before(audio->snapshot_actors, audio->snapshot_actor_count, id)) continue;
      audio->snapshot_actors[audio->snapshot_actor_count++] = id;
    }
    for (size_t b = 0; b < snapshots[i]->body_count; b++) {
      const qa_body_snapshot *body = &snapshots[i]->bodies[b];
      if (snapshot_count > 1 && seen_before(seen_bodies, audio->snapshot_body_count, body->actor)) continue;
      seen_bodies[audio->snapshot_body_count] = body->actor;
      audio->snapshot_bodies[audio->snapshot_body_count].actor = body->actor;
      audio->snapshot_bodies[audio->snapshot_body_count].origin = body->body.origin;
      audio->snapshot_body_count++;
    }
  }
  free(seen_bodies); free(snapshots);
  audio->has_snapshot = true;
}
static bool listener_for_seat(const qa_audio_listener *listeners, size_t count, qa_seat_id seat) {
  for (size_t i = 0; i < count; i++) if (qa_same_seat(listeners[i].seat, seat)) return true;
  return false;
}
static void frame_effect(qa_app_application_audio *audio, const qa_app_application_effect_sound *sound,
    qa_audio_audience audience, double frame_number) {
  qa_game_family family = product_of(audio, sound->content)->family;
  if (is(sound->playback_kind, "once")) {
    qa_vec3 origin = sound->origin;
    play_sound(audio, sound->content, family, sound->path, NULL, &origin, sound->channel, sound->volume, 1, 0, audience, NULL);
  } else if (is(sound->playback_kind, "actor")) {
    play_sound(audio, sound->content, family, sound->path, &sound->actor, NULL, sound->channel, sound->volume, 1, 0, audience, NULL);
  } else {
    const qa_sound_asset *asset = sound_for(audio, sound->content, sound->path, family, &sound->actor, NULL);
    if (asset == NULL) return;
    qa_loop_sound loop;
    memset(&loop, 0, sizeof loop);
    loop.sound = asset; loop.family = sound_family(family); loop.actor = sound->actor;
    loop.origin.kind = QA_SOUND_ORIGIN_FIXED; loop.origin.position = sound->origin;
    loop.audience = audience; loop.volume = sound->volume; loop.attenuation = 1;
    loop.velocity = sound->velocity; loop.frame_number = frame_number; loop.lifetime = QA_LOOP_LIFETIME_FRAME;
    qa_unified_audio_loop(audio->engine, &loop);
  }
}
typedef struct frame_scratch {
  ui_sound_record *ui; qa_app_application_effect_sound *effects; cgame_record *frames; size_t frame_count;
} frame_scratch;
static void free_frame_scratch(void *ctx) {
  frame_scratch *scratch = ctx;
  free(scratch->ui); scratch->ui = NULL;
  free(scratch->effects); scratch->effects = NULL;
  for (size_t i = 0; i < scratch->frame_count; i++) free_cgame_record(&scratch->frames[i]);
  free(scratch->frames); scratch->frames = NULL; scratch->frame_count = 0;
}
void qa_app_application_audio_frame(qa_app_application_audio *audio, const qa_world_snapshot *snapshot,
    const qa_audio_listener *listeners, size_t listener_count,
    const qa_app_sim_presentation_event *events, size_t event_count, double frame_started_at,
    const qa_app_audio_seat_events *seat_events, size_t seat_event_count) {
  size_t scene_count = 0;
  for (size_t i = 0; i < seat_event_count; i++) if (seat_events[i].scene != NULL) scene_count++;
  free(audio->seat_scenes);
  audio->seat_scenes = xmalloc(scene_count * sizeof *audio->seat_scenes);
  audio->seat_scene_count = 0;
  for (size_t i = 0; i < seat_event_count; i++)
    if (seat_events[i].scene != NULL)
      audio->seat_scenes[audio->seat_scene_count++] = (seat_scene){seat_events[i].seat, *seat_events[i].scene};
  sync_geometry(audio);
  qa_unified_audio_set_effects_volume(audio->engine, qa_app_application_audio_effects_volume(audio));
  qa_app_application_music_set_volume(audio->music, qa_app_application_audio_music_volume(audio));
  capture_snapshot(audio, snapshot, seat_events, seat_event_count);
  free(audio->listeners);
  audio->listeners = xmalloc(listener_count * sizeof *audio->listeners);
  if (listener_count != 0) memcpy(audio->listeners, listeners, listener_count * sizeof *audio->listeners);
  audio->listener_count = listener_count;
  for (size_t i = 0; i < audio->snapshot_body_count; i++)
    qa_unified_audio_update_actor(audio->engine, audio->snapshot_bodies[i].actor, audio->snapshot_bodies[i].origin);
  {
    qa_actor_id *live = xmalloc(audio->snapshot_body_count * sizeof *live);
    for (size_t i = 0; i < audio->snapshot_body_count; i++) live[i] = audio->snapshot_bodies[i].actor;
    qa_unified_audio_retire_absent_actors(audio->engine, live, audio->snapshot_body_count);
    free(live);
  }
  qa_unified_audio_set_listeners(audio->engine, listeners, listener_count);
  for (size_t index = audio->environment_seat_count; index-- > 0;) {
    if (listener_for_seat(listeners, listener_count, audio->environment_seats[index])) continue;
    memmove(&audio->environment_seats[index], &audio->environment_seats[index + 1],
            (audio->environment_seat_count - index - 1) * sizeof *audio->environment_seats);
    audio->environment_seat_count--;
  }
  if (audio->has_environment) for (size_t i = 0; i < listener_count; i++) {
    bool configured = false;
    for (size_t s = 0; s < audio->environment_seat_count && !configured; s++)
      configured = qa_same_seat(audio->environment_seats[s], listeners[i].seat);
    if (configured) continue;
    environment_seat *context = xmalloc(sizeof *context);
    context->audio = audio; context->seat = listeners[i].seat;
    audio->environment_contexts = xrealloc(audio->environment_contexts,
        (audio->environment_context_count + 1) * sizeof *audio->environment_contexts);
    audio->environment_contexts[audio->environment_context_count++] = context;
    qa_unified_audio_set_environment(audio->engine, listeners[i].seat, audio->environment_definitions,
                                     audio->environment_definition_count, environment_trace, context);
    if (audio->environment_seat_count == audio->environment_seat_cap) {
      audio->environment_seat_cap = audio->environment_seat_cap ? audio->environment_seat_cap * 2 : 4;
      audio->environment_seats = xrealloc(audio->environment_seats, audio->environment_seat_cap * sizeof *audio->environment_seats);
    }
    audio->environment_seats[audio->environment_seat_count++] = listeners[i].seat;
  }
  qa_unified_audio_begin_loop_frame(audio->engine);
  frame_scratch scratch = {NULL, NULL, NULL, 0};
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free_frame_scratch, &scratch);
  size_t ui_count = 0;
  scratch.ui = qa_event_ring_take(&audio->ui_sounds, &audio->ui_sounds_cursor, &ui_count);
  qa_event_ring_retire(&audio->ui_sounds);
  for (size_t i = 0; i < ui_count; i++) {
    qa_content_id content = recipe_of(audio)->presentation.audio.content;
    qa_game_family family = product_of(audio, content)->family;
    const qa_sound_asset *sound = sound_for(audio, content, qa_app_menu_sound_path(family, scratch.ui[i].sound), family, NULL, NULL);
    if (sound == NULL) continue;
    qa_play_sound play;
    memset(&play, 0, sizeof play);
    play.sound = sound; play.family = sound_family(family); play.origin.kind = QA_SOUND_ORIGIN_LOCAL;
    play.audience.kind = QA_AUDIO_AUDIENCE_SEAT; play.audience.seat = scratch.ui[i].seat;
    play.channel = 0; play.volume = 1; play.attenuation = 0;
    qa_unified_audio_play(audio->engine, &play);
  }
  qa_app_application_audio_receive(audio, events, event_count, world_audience(), true);
  for (size_t i = 0; i < seat_event_count; i++) {
    qa_audio_audience seat = {QA_AUDIO_AUDIENCE_SEAT, seat_events[i].seat};
    qa_app_application_audio_receive(audio, seat_events[i].events, seat_events[i].event_count, seat, seat_events[i].music);
  }
  size_t effect_count = 0;
  scratch.effects = qa_event_ring_take(&audio->effect_sounds, &audio->effect_sounds_cursor, &effect_count);
  qa_event_ring_retire(&audio->effect_sounds);
  for (size_t i = 0; i < effect_count; i++) frame_effect(audio, &scratch.effects[i], world_audience(), snapshot->frame.frame);
  for (size_t b = 0; b < seat_event_count; b++) {
    qa_audio_audience seat = {QA_AUDIO_AUDIENCE_SEAT, seat_events[b].seat};
    for (size_t i = 0; i < seat_events[b].effect_sound_count; i++)
      frame_effect(audio, &seat_events[b].effect_sounds[i], seat, snapshot->frame.frame);
  }
  if (audio->closed) { qa_cleanup_pop(&cleanup); free_frame_scratch(&scratch); return; }
  {
    qa_audio_listener *mapped = xmalloc(listener_count * sizeof *mapped);
    for (size_t i = 0; i < listener_count; i++) {
      mapped[i] = listeners[i];
      if (!listeners[i].has_actor) continue;
      actor_audio *state = actor_state(audio, listeners[i].actor);
      actor_audio *followed = state->has_chase ? actor_state(audio, state->chase) : state;
      if (followed->underwater >= 0) mapped[i].underwater = followed->underwater == 1;
    }
    qa_unified_audio_set_listeners(audio->engine, mapped, listener_count);
    free(mapped);
  }
  {
    size_t count = audio->loop_count;
    loop_audio *loops = xmalloc(count * sizeof *loops);
    if (count != 0) memcpy(loops, audio->loops, count * sizeof *loops);
    for (size_t i = 0; i < count; i++) {
      if (!snapshot_has_actor(audio, loops[i].loop.actor)) {
        stop_loop(audio, loops[i].loop.actor, loops[i].loop.audience, loops[i].loop.owner);
        continue;
      }
      qa_loop_sound loop = loops[i].loop;
      loop.frame_number = snapshot->frame.frame;
      qa_unified_audio_loop(audio->engine, &loop);
    }
    free(loops);
  }
  for (size_t s = 0; s < audio->static_count; s++) {
    static_audio *sound = &audio->statics[s];
    for (size_t index = sound->seat_count; index-- > 0;) {
      if (listener_for_seat(listeners, listener_count, sound->seats[index])) continue;
      memmove(&sound->seats[index], &sound->seats[index + 1], (sound->seat_count - index - 1) * sizeof *sound->seats);
      sound->seat_count--;
    }
    for (size_t i = 0; i < listener_count; i++) {
      if (!(sound->audience.kind == QA_AUDIO_AUDIENCE_WORLD || qa_same_seat(sound->audience.seat, listeners[i].seat))) continue;
      bool added = false;
      for (size_t k = 0; k < sound->seat_count && !added; k++) added = qa_same_seat(sound->seats[k], listeners[i].seat);
      if (added) continue;
      qa_unified_audio_add_static_sound(audio->engine, listeners[i].seat, sound->sound, sound->origin, sound->volume,
                                        sound->attenuation, sound->key);
      if (sound->seat_count == sound->seat_cap) {
        sound->seat_cap = sound->seat_cap ? sound->seat_cap * 2 : 4;
        sound->seats = xrealloc(sound->seats, sound->seat_cap * sizeof *sound->seats);
      }
      sound->seats[sound->seat_count++] = listeners[i].seat;
    }
  }
  qa_app_application_world world = qa_app_loaded_content_world(audio->content);
  if (is(world.kind, "q1-bsp")) {
    qa_content_id content = recipe_of(audio)->map.entities.content;
    const qa_sound_asset *water = sound_for(audio, content, "ambience/water1.wav", QA_GAME_FAMILY_Q1, NULL, NULL);
    const qa_sound_asset *wind = sound_for(audio, content, "ambience/wind2.wav", QA_GAME_FAMILY_Q1, NULL, NULL);
    if (water != NULL && wind != NULL) {
      const qa_q1_world_geometry *geometry = &((const qa_q1_map *)world.value)->world;
      const qa_sound_asset *const ambient[2] = {water, wind};
      static const double silent[2] = {0, 0};
      for (size_t i = 0; i < listener_count; i++) {
        double leaf_index = qa_q1_find_leaf(geometry, listeners[i].origin, 0);
        const qa_q1_leaf *leaf = leaf_index >= 0 && leaf_index < (double)geometry->leaf_count
            ? &geometry->leaves[(size_t)leaf_index] : NULL;
        qa_source_time elapsed = snapshot->frame.elapsed;
        qa_unified_audio_update_ambient(audio->engine, listeners[i].seat, ambient, 2,
            leaf != NULL ? leaf->ambient_sound : silent, leaf != NULL ? 4 : 2,
            elapsed.value / (elapsed.kind == QA_SOURCE_TIME_MILLISECONDS ? 1000 : 1), 0.3, 100);
      }
    }
  }
  scratch.frames = qa_event_ring_take(&audio->cgame_frames, &audio->cgame_frames_cursor, &scratch.frame_count);
  qa_event_ring_retire(&audio->cgame_frames);
  for (size_t f = 0; f < scratch.frame_count; f++) {
    const cgame_record *frame = &scratch.frames[f];
    if (!listener_for_seat(listeners, listener_count, frame->seat)) continue;
    for (size_t o = 0; o < frame->operation_count; o++) {
      const qa_app_q3_seat_audio_operation *operation = &frame->operations[o];
      switch (operation->kind) {
        case QA_APP_Q3_SEAT_AUDIO_PLAY:
          play_haptics(audio, frame->content, operation->play.sound, operation->play.has_actor ? &operation->play.actor : NULL,
                       operation->play.audience);
          qa_unified_audio_play(audio->engine, &operation->play); break;
        case QA_APP_Q3_SEAT_AUDIO_LOOP: qa_unified_audio_loop(audio->engine, &operation->loop); break;
        case QA_APP_Q3_SEAT_AUDIO_POSITION:
          qa_unified_audio_update_q3_seat_actor(audio->engine, frame->seat, operation->actor, operation->origin, frame->owner); break;
        case QA_APP_Q3_SEAT_AUDIO_CLEAR_LOOPS:
          qa_unified_audio_clear_q3_seat_loops(audio->engine, frame->seat, operation->kill_all, frame->owner); break;
        case QA_APP_Q3_SEAT_AUDIO_STOP_LOOP:
          qa_unified_audio_stop_q3_seat_loop(audio->engine, frame->seat, operation->actor, frame->owner); break;
        case QA_APP_Q3_SEAT_AUDIO_RELEASE_OWNER:
          if (!frame->has_owner) plain_error("Component audio retirement requires its source owner");
          qa_unified_audio_release_q3_seat_owner(audio->engine, frame->seat, frame->owner); break;
      }
    }
  }
  qa_cleanup_pop(&cleanup);
  free_frame_scratch(&scratch);
  qa_unified_audio_end_loop_frame(audio->engine);
  qa_app_application_music_update_automatic(audio->music, music_shuffle(audio));
  qa_unified_audio_update_music(audio->engine);
  qa_unified_audio_pump(audio->engine, false, 0, qa_app_audio_now_milliseconds() - frame_started_at);
}

void qa_app_application_audio_reset_round(qa_app_application_audio *audio) {
  if (audio->closed) plain_error("Application audio closed");
  qa_app_application_music_stop_playback(audio->music, "source");
  qa_unified_audio_reset_round(audio->engine);
  map_clear(&audio->actor_audio, free_actor_audio);
  audio->loop_count = 0; clear_statics(audio);
  qa_event_ring_reset(&audio->ui_sounds); qa_event_ring_reset(&audio->effect_sounds); reset_cgame_frames(audio);
  audio->listener_count = 0; audio->seat_scene_count = 0;
  audio->has_snapshot = false; audio->snapshot_actor_count = 0; audio->snapshot_body_count = 0;
  audio->environment_seat_count = 0;
}

void qa_app_application_audio_close(qa_app_application_audio *audio) {
  if (audio == NULL || audio->closed) return;
  audio->closed = true;
  audio->has_geometry = false; audio->geometry_enabled = false;
  qa_app_application_music_stop(audio->music);
  qa_unified_audio_close(audio->engine);
  map_clear(&audio->banks, free_bank); map_clear(&audio->sounds, NULL); map_clear(&audio->footsteps, NULL);
  map_clear(&audio->actor_audio, free_actor_audio);
  audio->loop_count = 0; clear_statics(audio);
  qa_event_ring_reset(&audio->ui_sounds); qa_event_ring_reset(&audio->effect_sounds); reset_cgame_frames(audio);
  audio->listener_count = 0; audio->seat_scene_count = 0;
  audio->has_snapshot = false; audio->snapshot_actor_count = 0; audio->snapshot_body_count = 0;
}

void qa_app_application_audio_destroy(qa_app_application_audio *audio) {
  if (audio == NULL || audio->destroyed) return;
  audio->destroyed = true;
  if (audio->engine != NULL) qa_app_application_audio_close(audio);
  qa_app_application_music_destroy(audio->music);
  qa_unified_audio_destroy(audio->engine);
  map_clear(&audio->playlists, free_track_list);
  map_clear(&audio->retired_owners, NULL); map_clear(&audio->owner_revisions, NULL); map_clear(&audio->warned, NULL);
  free(audio->statics); free(audio->loops);
  qa_event_ring_close_cursor(&audio->ui_sounds, &audio->ui_sounds_cursor);
  qa_event_ring_close_cursor(&audio->effect_sounds, &audio->effect_sounds_cursor);
  qa_event_ring_close_cursor(&audio->cgame_frames, &audio->cgame_frames_cursor);
  qa_event_ring_destroy(&audio->ui_sounds); qa_event_ring_destroy(&audio->effect_sounds); qa_event_ring_destroy(&audio->cgame_frames);
  free(audio->listeners); free(audio->seat_scenes); free(audio->snapshot_actors); free(audio->snapshot_bodies);
  if (audio->has_environment) qa_reverb_environments_free(audio->environment_definitions, audio->environment_definition_count);
  for (size_t i = 0; i < audio->retired_environment_count; i++)
    qa_reverb_environments_free(audio->retired_environments[i].definitions, audio->retired_environments[i].count);
  free(audio->retired_environments);
  for (size_t i = 0; i < audio->environment_context_count; i++) free(audio->environment_contexts[i]);
  free(audio->environment_contexts); free(audio->environment_seats);
  free(audio->character_model);
  map_clear(&audio->strings, NULL);
  free(audio);
}
