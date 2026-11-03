/* Port of src/content/q3/presentation/audio.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/audio.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct entry {
  char *path; bool compressed; const qa_sound_asset *sound;
} entry;
struct qa_q3_pres_presentation_sound_bank {
  qa_sound_bank *bank;
  const qa_sound_asset *zero_sound;
  const qa_sound_asset *(*load_sync)(void *ctx, const char *path, bool compressed);
  void *load_ctx;
  entry *requests; size_t request_count, request_cap;
  const qa_sound_asset **registered; size_t registered_count, registered_cap;
  double operations;
  double failed_sounds;
};
static const qa_pcm_sound *pcm_of(const qa_sound_asset *asset) { return asset ? &asset->pcm : NULL; }
static bool is_zero(qa_q3_pres_presentation_sound_bank *b, const qa_sound_asset *asset) {
  return asset && b->zero_sound && pcm_of(asset) == pcm_of(b->zero_sound);
}
static entry *find_request(qa_q3_pres_presentation_sound_bank *b, const char *path) {
  for (size_t i = 0; i < b->request_count; i++)
    if (strcmp(b->requests[i].path, path) == 0) return &b->requests[i];
  return NULL;
}
static bool note_registered(qa_q3_pres_presentation_sound_bank *b, const qa_sound_asset *asset) {
  if (!asset || is_zero(b, asset)) return true;
  for (size_t i = 0; i < b->registered_count; i++)
    if (b->registered[i] == asset) return true;
  if (b->registered_count == b->registered_cap) {
    size_t nc = b->registered_cap ? b->registered_cap * 2 : 8;
    const qa_sound_asset **np = realloc(b->registered, nc * sizeof *np);
    if (!np) return false;
    b->registered = np;
    b->registered_cap = nc;
  }
  b->registered[b->registered_count++] = asset;
  return true;
}
static bool add_request(qa_q3_pres_presentation_sound_bank *b, const char *path, bool compressed,
                        const qa_sound_asset *sound, entry **out) {
  if (b->request_count == b->request_cap) {
    size_t nc = b->request_cap ? b->request_cap * 2 : 8;
    entry *np = realloc(b->requests, nc * sizeof *np);
    if (!np) return false;
    b->requests = np;
    b->request_cap = nc;
  }
  char *copy = malloc(strlen(path) + 1);
  if (!copy) return false;
  strcpy(copy, path);
  b->requests[b->request_count] = (entry){copy, compressed, sound};
  *out = &b->requests[b->request_count];
  b->request_count++;
  return true;
}
static qa_pcm_sound *result_of(qa_q3_pres_presentation_sound_bank *b, const qa_sound_asset *asset) {
  if (!asset || is_zero(b, asset)) return NULL;
  return (qa_pcm_sound *)&asset->pcm;
}
qa_q3_pres_presentation_sound_bank *qa_q3_pres_presentation_sound_bank_create(
    qa_sound_bank *bank, const qa_sound_asset *zero_sound,
    const qa_sound_asset *(*load_sync)(void *ctx, const char *path, bool compressed), void *load_ctx) {
  qa_q3_pres_presentation_sound_bank *b = calloc(1, sizeof *b);
  if (!b) return NULL;
  b->bank = bank;
  b->zero_sound = zero_sound;
  b->load_sync = load_sync;
  b->load_ctx = load_ctx;
  return b;
}
void qa_q3_pres_presentation_sound_bank_destroy(qa_q3_pres_presentation_sound_bank *b) {
  if (!b) return;
  for (size_t i = 0; i < b->request_count; i++) free(b->requests[i].path);
  free(b->requests);
  free(b->registered);
  free(b);
}
qa_sound_bank *qa_q3_pres_presentation_sound_bank_bank(qa_q3_pres_presentation_sound_bank *b) { return b->bank; }
const qa_sound_asset *qa_q3_pres_presentation_sound_bank_zero_sound(qa_q3_pres_presentation_sound_bank *b) {
  return b->zero_sound;
}
bool qa_q3_pres_presentation_sound_bank_register_sound(qa_q3_pres_presentation_sound_bank *b,
    const char *path, bool compressed, qa_pcm_sound **out) {
  *out = NULL;
  if (path == NULL) qa_range_error("S_RegisterSound dereferences NULL name at strlen");
  if (path[0] == '\0' || path[0] == '*') return true;
  entry *prior = find_request(b, path);
  if (prior) {
    *out = result_of(b, prior->sound);
    return true;
  }
  b->operations++;
  const qa_sound_asset *sound = NULL;
  bool ok = qa_sound_bank_register(b->bank, path, QA_SOUND_FAMILY_Q3, &sound);
  b->operations--;
  if (!ok) return false;
  entry *created = NULL;
  bool missing = sound == NULL || is_zero(b, sound);
  if (!missing || b->failed_sounds++ < 256)
    if (!add_request(b, path, compressed, sound, &created)) return false;
  if (!note_registered(b, sound)) return false;
  *out = result_of(b, sound);
  return true;
}
bool qa_q3_pres_presentation_sound_bank_sound(qa_q3_pres_presentation_sound_bank *b,
    const char *path, bool compressed, qa_pcm_sound **out) {
  *out = NULL;
  if (path == NULL) return true;
  entry *existing = find_request(b, path);
  if (!existing) {
    const qa_sound_asset *sound = b->load_sync(b->load_ctx, path, compressed);
    if (!add_request(b, path, compressed, sound, &existing)) return false;
    if (!note_registered(b, sound)) return false;
  }
  *out = result_of(b, existing->sound);
  return true;
}
double qa_q3_pres_presentation_sound_bank_index_for_sound(qa_q3_pres_presentation_sound_bank *b, qa_pcm_sound *sound) {
  if (!sound || (b->zero_sound && sound == (qa_pcm_sound *)&b->zero_sound->pcm)) return 0;
  for (size_t i = 0; i < b->registered_count; i++)
    if (pcm_of(b->registered[i]) == sound) return (double)i + 1;
  qa_range_error("PCM does not belong to this cgame sound bank");
}
const qa_sound_asset *qa_q3_pres_presentation_sound_bank_asset(qa_q3_pres_presentation_sound_bank *b, qa_pcm_sound *sound) {
  double index = qa_q3_pres_presentation_sound_bank_index_for_sound(b, sound);
  if (index == 0) return b->zero_sound;
  const qa_sound_asset *asset = b->registered[(size_t)index - 1];
  return asset ? asset : b->zero_sound;
}
static void check_integer_handle(double index) {
  if (index != trunc(index)) qa_range_error("Sound handle must be an integer");
}
qa_pcm_sound *qa_q3_pres_presentation_sound_bank_sound_at_index(qa_q3_pres_presentation_sound_bank *b, double index) {
  if (index == 0) return NULL;
  const qa_sound_asset *sound = NULL;
  if (index == trunc(index) && index >= 1 && index <= (double)b->registered_count)
    sound = b->registered[(size_t)index - 1];
  if (!sound) {
    if (index == trunc(index)) qa_range_error("Q3 sound handle %lld is not registered", (long long)index);
    qa_range_error("Q3 sound handle %s is not registered", QA_JS_NUMBER(index));
  }
  return (qa_pcm_sound *)&sound->pcm;
}
void qa_q3_pres_presentation_sound_bank_sound_for_index(qa_q3_pres_presentation_sound_bank *b, double index,
                                                        qa_pcm_sound **out, bool *has) {
  check_integer_handle(index);
  *out = NULL;
  *has = true;
  if (index == 0) return;
  if (index >= 1 && index <= (double)b->registered_count)
    *out = (qa_pcm_sound *)&b->registered[(size_t)index - 1]->pcm;
  else *has = false;
}
const qa_q3_pres_sound_registration *qa_q3_pres_presentation_sound_bank_registrations(
    qa_q3_pres_presentation_sound_bank *b, size_t *out_count) {
  *out_count = b->request_count;
  return (const qa_q3_pres_sound_registration *)b->requests;
}
qa_save_value *qa_q3_pres_presentation_sound_bank_capture_checkpoint(qa_q3_pres_presentation_sound_bank *b) {
  if (b->operations != 0) qa_range_error("Cannot checkpoint pending sound registration");
  qa_save_value *list = qa_save_value_array();
  if (!list) return NULL;
  for (size_t i = 0; i < b->request_count; i++) {
    entry *row = &b->requests[i];
    qa_save_value *object = qa_save_value_object();
    qa_save_value *path = object ? qa_save_value_string(row->path) : NULL;
    qa_save_value *compressed = path ? qa_save_value_boolean(row->compressed) : NULL;
    qa_save_value *handle = compressed ? qa_save_value_number(
        qa_q3_pres_presentation_sound_bank_index_for_sound(b, (qa_pcm_sound *)pcm_of(row->sound))) : NULL;
    qa_save_value *resource = handle ? (row->sound ? qa_save_value_string(row->sound->resource)
                                                   : qa_save_value_null()) : NULL;
    bool ok = resource && qa_save_value_set(object, "path", path);
    if (ok) ok = qa_save_value_set(object, "compressed", compressed);
    if (ok) ok = qa_save_value_set(object, "handle", handle);
    if (ok) ok = qa_save_value_set(object, "resource", resource);
    if (ok) ok = qa_save_value_push(list, object);
    if (!ok) {
      if (object) qa_save_value_free(object);
      qa_save_value_free(list);
      return NULL;
    }
  }
  return list;
}
bool qa_q3_pres_presentation_sound_bank_restore_checkpoint(qa_q3_pres_presentation_sound_bank *b,
                                                           const qa_save_value *value) {
  if (b->operations != 0 || b->request_count != 0) qa_range_error("Sound restore requires an empty owner");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "client-sounds")) return false;
  size_t count = qa_save_reader_list_count(&reader);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item, field;
    if (!qa_save_reader_item(&reader, i, &item)) { qa_save_reader_destroy(&reader); return false; }
    if (!qa_save_reader_field(&item, "path", &field)) { qa_save_reader_destroy(&reader); return false; }
    const char *path = qa_save_reader_string(&field);
    if (!qa_save_reader_field(&item, "compressed", &field)) { qa_save_reader_destroy(&reader); return false; }
    bool compressed = qa_save_reader_boolean(&field);
    if (!qa_save_reader_field(&item, "handle", &field)) { qa_save_reader_destroy(&reader); return false; }
    double handle = qa_save_reader_integer_min(&field, 0);
    if (!qa_save_reader_field(&item, "resource", &field)) { qa_save_reader_destroy(&reader); return false; }
    const char *resource = qa_save_reader_is_null(&field) ? NULL : qa_save_reader_string(&field);
    qa_pcm_sound *sound = NULL;
    if (!qa_q3_pres_presentation_sound_bank_register_sound(b, path, compressed, &sound)) {
      qa_save_reader_destroy(&reader);
      return false;
    }
    entry *created = find_request(b, path);
    const char *actual = created && created->sound ? created->sound->resource : NULL;
    bool same = qa_q3_pres_presentation_sound_bank_index_for_sound(b, sound) == handle &&
                ((actual == NULL && resource == NULL) || (actual && resource && strcmp(actual, resource) == 0));
    if (!same) qa_save_reader_fail(&reader, "sound resource binding changed");
  }
  qa_save_reader_destroy(&reader);
  return true;
}
void qa_q3_pres_presentation_audio_init(qa_q3_pres_presentation_audio *audio, qa_q3_pres_audio_target target) {
  audio->target = target;
}
static qa_audio_audience seat_audience(qa_q3_pres_presentation_audio *audio) {
  qa_audio_audience audience = {QA_AUDIO_AUDIENCE_SEAT, audio->target.seat};
  return audience;
}
void qa_q3_pres_presentation_audio_start_source_sound(qa_q3_pres_presentation_audio *audio,
    qa_pcm_sound *sound, qa_start_sound_options options) {
  const qa_sound_asset *asset = qa_q3_pres_presentation_sound_bank_asset(audio->target.sounds, sound);
  if (!asset) return;
  qa_actor_id actor_value;
  bool has_actor = options.entity >= 0 && audio->target.actor(audio->target.ctx, options.entity, &actor_value);
  qa_sound_origin origin;
  memset(&origin, 0, sizeof origin);
  if (options.origin.kind == QA_VOICE_ORIGIN_ENTITY) {
    qa_actor_id origin_actor;
    if (!audio->target.actor(audio->target.ctx, options.origin.entity, &origin_actor)) return;
    origin.kind = QA_SOUND_ORIGIN_ACTOR;
    origin.actor = origin_actor;
  } else if (options.origin.kind == QA_VOICE_ORIGIN_FIXED) {
    origin.kind = QA_SOUND_ORIGIN_FIXED;
    origin.position = options.origin.position;
  } else origin.kind = QA_SOUND_ORIGIN_LOCAL;
  qa_play_sound play;
  memset(&play, 0, sizeof play);
  play.sound = asset;
  play.family = QA_SOUND_FAMILY_Q3;
  play.has_actor = has_actor;
  if (has_actor) play.actor = actor_value;
  play.origin = origin;
  play.audience = seat_audience(audio);
  play.channel = (int64_t)options.channel;
  play.volume = options.volume / 127;
  play.attenuation = origin.kind == QA_SOUND_ORIGIN_LOCAL ? 0 : 1;
  audio->target.play(audio->target.ctx, play);
}
void qa_q3_pres_presentation_audio_start_sound(qa_q3_pres_presentation_audio *audio, const qa_vec3 *origin,
    double entity, double channel, qa_pcm_sound *sound) {
  const qa_sound_asset *asset = qa_q3_pres_presentation_sound_bank_asset(audio->target.sounds, sound);
  if (!asset) return;
  qa_actor_id actor_value;
  bool has_actor = entity >= 0 && audio->target.actor(audio->target.ctx, entity, &actor_value);
  qa_sound_origin source;
  memset(&source, 0, sizeof source);
  if (origin) {
    source.kind = QA_SOUND_ORIGIN_FIXED;
    source.position = *origin;
  } else {
    if (!has_actor) return;
    source.kind = QA_SOUND_ORIGIN_ACTOR;
    source.actor = actor_value;
  }
  qa_play_sound play;
  memset(&play, 0, sizeof play);
  play.sound = asset;
  play.family = QA_SOUND_FAMILY_Q3;
  play.has_actor = has_actor;
  if (has_actor) play.actor = actor_value;
  play.origin = source;
  play.audience = seat_audience(audio);
  play.channel = (int64_t)channel;
  play.volume = 1;
  play.attenuation = 1;
  audio->target.play(audio->target.ctx, play);
}
void qa_q3_pres_presentation_audio_start_local_sound(qa_q3_pres_presentation_audio *audio,
    qa_pcm_sound *sound, double channel) {
  const qa_sound_asset *asset = qa_q3_pres_presentation_sound_bank_asset(audio->target.sounds, sound);
  if (!asset) return;
  qa_play_sound play;
  memset(&play, 0, sizeof play);
  play.sound = asset;
  play.family = QA_SOUND_FAMILY_Q3;
  play.origin.kind = QA_SOUND_ORIGIN_LOCAL;
  play.audience = seat_audience(audio);
  play.channel = (int64_t)channel;
  play.volume = 1;
  play.attenuation = 0;
  audio->target.play(audio->target.ctx, play);
}
void qa_q3_pres_presentation_audio_add_loop_sound(qa_q3_pres_presentation_audio *audio, double entity,
    qa_vec3 origin, qa_vec3 velocity, qa_pcm_sound *sound, bool real_loop) {
  const qa_sound_asset *asset = qa_q3_pres_presentation_sound_bank_asset(audio->target.sounds, sound);
  if (!asset) return;
  qa_actor_id actor_value;
  if (!audio->target.actor(audio->target.ctx, entity, &actor_value)) return;
  qa_loop_sound loop;
  memset(&loop, 0, sizeof loop);
  loop.sound = asset;
  loop.family = QA_SOUND_FAMILY_Q3;
  loop.actor = actor_value;
  loop.origin.kind = QA_SOUND_ORIGIN_FIXED;
  loop.origin.position = origin;
  loop.audience = seat_audience(audio);
  loop.velocity = velocity;
  loop.volume = 1;
  loop.attenuation = 1;
  loop.frame_number = audio->target.frame_number(audio->target.ctx);
  loop.lifetime = real_loop ? QA_LOOP_LIFETIME_PERSISTENT : QA_LOOP_LIFETIME_FRAME;
  audio->target.loop(audio->target.ctx, loop);
}
void qa_q3_pres_presentation_audio_update_sound_position(qa_q3_pres_presentation_audio *audio,
    double entity, qa_vec3 origin) {
  qa_actor_id actor_value;
  if (audio->target.actor(audio->target.ctx, entity, &actor_value))
    audio->target.update_actor(audio->target.ctx, actor_value, origin);
}
void qa_q3_pres_presentation_audio_stop_looping_sound(qa_q3_pres_presentation_audio *audio, double entity) {
  qa_actor_id actor_value;
  if (audio->target.actor(audio->target.ctx, entity, &actor_value))
    audio->target.stop_loop(audio->target.ctx, audio->target.seat, actor_value);
}
