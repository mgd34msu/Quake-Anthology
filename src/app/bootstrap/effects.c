/* Port of src/app/bootstrap/effects.ts. Application joins for Quake
 * cl_tent/r_part and Quake II cl_tent/cl_fx.
 * Copyright (C) 1996-2005 Id Software, Inc. GPL-2.0-or-later. */
#include "qa/app/bootstrap/effects.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/effects/particles.h"
#include "qa/app/bootstrap/effects/q2_muzzle.h"
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/content/catalog/index.h"
#include "qa/content/composition/q2/types.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/q2/base/player/view.h"
#include "qa/content/q2/foundation/effect_resources.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/shadow_lights.h"
#include "qa/content/q2/foundation/weapons/types.h"
#include "qa/content/q2/missionpacks/entities/types.h"
#include "qa/content/q2/missionpacks/types.h"
#include "qa/content/q2/multiplayer/ctf/types.h"
#include "qa/content/q2/multiplayer/lmctf/types.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/contracts/presentation.h"
#include "qa/core/math.h"
#include "qa/foundation/bounded_queue.h"
#include "qa/foundation/event_ring.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/support.h"
#include "qa/render/scene/models/renderer.h"
#include "qa/render/scene/particles/legacy.h"
#include "qa/render/scene/view.h"

enum { MAX_BEAM_SEGMENTS = 512 };
static const qa_vec3 zero = {0, 0, 0};
static const qa_vec3 white = {1, 1, 1};
static const qa_vec3 orange = {1, 0.5, 0.5};

static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_range_error("Out of memory");
  return next;
}
#define GROW(array, count, capacity) do { \
    if ((count) == (capacity)) { \
      (capacity) = (capacity) ? (capacity) * 2 : 8; \
      (array) = xrealloc((array), (capacity) * sizeof *(array)); \
    } \
  } while (0)
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static noreturn void fail(const char *format, const char *detail) { qa_raise("Error", 0, 0, format, detail); }

/* ---- interned text (the GC lifetime of retained event strings) ---- */
typedef struct strings { char **values; size_t count, capacity; } strings;
static const char *intern(strings *pool, const char *text) {
  if (text == NULL) return NULL;
  for (size_t i = 0; i < pool->count; i++) if (strcmp(pool->values[i], text) == 0) return pool->values[i];
  GROW(pool->values, pool->count, pool->capacity);
  size_t length = strlen(text) + 1;
  char *copy = xcalloc(length, 1);
  memcpy(copy, text, length);
  pool->values[pool->count++] = copy;
  return copy;
}
static void strings_destroy(strings *pool) {
  for (size_t i = 0; i < pool->count; i++) free(pool->values[i]);
  free(pool->values);
}

/* ---- retained events ---- */
/* TS keeps the received event objects alive until prepare; C holds an owned
 * deep copy, with content interned for the values that outlive it. */
typedef struct retained {
  qa_app_sim_presentation_event event;
  qa_content_id content;        /* interned event content */
  qa_presentation_owner owner;  /* event owner with its provider interned */
} retained;
static retained *retain_event(strings *pool, const qa_app_sim_presentation_event *event) {
  retained *r = xcalloc(1, sizeof *r);
  if (!qa_app_sim_presentation_event_copy(&r->event, event)) { free(r); qa_range_error("Out of memory"); }
  r->content = intern(pool, event->content);
  r->owner = event->owner;
  r->owner.provider = intern(pool, event->owner.provider);
  return r;
}
static retained *retained_clone(const retained *source) {
  retained *r = xcalloc(1, sizeof *r);
  if (!qa_app_sim_presentation_event_copy(&r->event, &source->event)) { free(r); qa_range_error("Out of memory"); }
  r->content = source->content;
  r->owner = source->owner;
  return r;
}
static void retained_free(retained *r) {
  if (r == NULL) return;
  qa_app_sim_presentation_event_release(&r->event);
  free(r);
}
typedef struct unhandled_record { retained *source; char *reason; } unhandled_record;

/* ---- state ---- */
typedef struct pose { qa_actor_id actor; qa_vec3 origin, angles; } pose;
typedef struct static_model { bool has_owner; qa_presentation_owner owner; qa_scene_entity entity; } static_model;
typedef struct beam_models {
  qa_actor_id actor;
  qa_scene_entity *remote; size_t remote_count;
  bool has_local; qa_scene_entity *local; size_t local_count;
} beam_models;
typedef struct group {
  qa_content_id content;
  qa_app_provider_scene_assets provider;
  qa_app_source_particles *particles;
  qa_scene_model_renderer *renderer;
  qa_scene_entity *models; size_t model_count, model_capacity;
  static_model *statics; size_t static_count, static_capacity;
  beam_models *beams; size_t beam_count, beam_capacity;
  qa_scene_particle *sampled; size_t sampled_count;
} group;
typedef struct prepared_renderer { qa_content_id content; qa_scene_model_renderer *renderer; } prepared_renderer;
typedef struct timed_light {
  qa_vec3 origin; double radius, minimum; qa_vec3 color;
  double born, die, decay; bool has_actor; qa_actor_id actor;
} timed_light;
typedef enum beam_family { BEAM_Q1, BEAM_Q2 } beam_family;
typedef struct beam {
  bool has_owner; qa_presentation_owner owner;
  qa_content_id content; qa_vec3 start, end; double die, width, color;
  const char *model; /* NULL is TS null */
  beam_family family; bool has_actor; qa_actor_id actor;
} beam;
typedef enum explosion_kind { EXPLOSION_POLY, EXPLOSION_MISC, EXPLOSION_FLASH } explosion_kind;
typedef struct explosion {
  qa_content_id content; qa_vec3 origin, angles; double start, frames, base_frame;
  const char *path; explosion_kind kind; double flags, skin;
  bool has_light; double light_radius; qa_vec3 light_color;
} explosion;
typedef struct steam { qa_content_id content; qa_q2mp_entity_event event; double end, next; } steam;
typedef struct static_brush {
  bool has_owner; qa_presentation_owner owner;
  qa_world_scene *scene; double model; qa_scene_model_transform transform; double frame;
} static_brush;
typedef struct flashlight { qa_actor_id actor; const char *hand; bool has_owner; qa_presentation_owner owner; } flashlight;
typedef struct shadow_light { qa_q2_shadow_light_state state; bool has_owner; qa_presentation_owner owner; } shadow_light;
typedef struct source_light { qa_actor_id actor; qa_surface_dynamic_light light; bool has_owner; qa_presentation_owner owner; } source_light;
typedef struct actor_time { qa_actor_id actor; double until; } actor_time;
typedef struct tracker_pain { qa_actor_id actor; qa_content_id content; double until; } tracker_pain;
typedef struct entity_trail { qa_actor_id actor; qa_content_id content; qa_vec3 origin; double count; } entity_trail;
typedef struct q1_trail { qa_actor_id actor; qa_content_id content; const char *path; qa_vec3 origin; } q1_trail;
typedef struct recipient { qa_actor_id actor; qa_app_application_effects *effects; } recipient;
typedef struct q3_owner { qa_content_id content; qa_app_q3_application_effects *effects; } q3_owner;
typedef struct family_image { qa_game_family family; qa_renderer_image image; qa_legacy_particle_image *content; } family_image;
typedef struct owner_revision { char *key; double revision; } owner_revision;

struct qa_app_application_effects {
  qa_app_application_assets *assets;
  qa_scene_queries queries;
  qa_app_effects_is_player_fn is_player; void *is_player_ctx;
  double seed;
  strings *strings; bool owns_strings;
  recipient *recipients; size_t recipient_count, recipient_capacity;
  bool events_only;
  qa_app_sim_source_random *random;
  group **groups; size_t group_count, group_capacity;
  prepared_renderer *prepared; size_t prepared_count, prepared_capacity;
  family_image *images; size_t image_count, image_capacity;
  q3_owner *q3; size_t q3_count, q3_capacity;
  entity_trail *entity_trails; size_t entity_trail_count;
  q1_trail *q1_trails; size_t q1_trail_count;
  qa_event_ring pending; qa_event_cursor pending_cursor;
  char **retired_owners; size_t retired_count, retired_capacity;
  owner_revision *revisions; size_t revision_count, revision_capacity;
  qa_event_ring unhandled; qa_event_cursor unhandled_cursor;
  beam *beams; size_t beam_count, beam_capacity;
  explosion *explosions; size_t explosion_count, explosion_capacity;
  static_brush *static_brushes; size_t static_brush_count, static_brush_capacity;
  qa_scene_light_style *styles; size_t style_count;
  qa_bounded_queue lights;
  qa_surface_dynamic_light *sampled_lights; size_t sampled_light_count, sampled_light_capacity;
  flashlight *flashlights; size_t flashlight_count, flashlight_capacity;
  shadow_light *shadow_lights; size_t shadow_light_count, shadow_light_capacity;
  source_light *source_lights; size_t source_light_count, source_light_capacity;
  qa_app_q2_effect_views *player_views;
  actor_time *bonus_flashes; size_t bonus_count, bonus_capacity;
  tracker_pain *tracker_pain; size_t tracker_count, tracker_capacity;
  steam *steam; size_t steam_count, steam_capacity;
  qa_event_ring sounds; qa_event_cursor sounds_cursor;
  pose *poses; size_t pose_count;
  bool has_time; double time;
  double sequence;
  bool closed;
  qa_q3_hardware_read_fn hardware_read; void *hardware_ctx;
};
typedef qa_app_application_effects effects_t;

static void ensure_open(const effects_t *e) { if (e->closed) qa_raise("Error", 0, 0, "Effect world is closed"); }
static qa_q3_hardware read_hardware(void *ctx) {
  effects_t *e = ctx;
  return e->hardware_read == NULL ? "generic" : e->hardware_read(e->hardware_ctx);
}
static const qa_content_catalog *catalog_of(effects_t *e) {
  return qa_app_loaded_content_catalog(qa_app_application_assets_content(e->assets));
}
static const qa_catalog_product_expectation *expectation(effects_t *e, qa_content_id content) {
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog_of(e), content);
  if (product == NULL) qa_raise("RangeError", 0, 0, "Unknown requested content or mod: %s", content);
  return &product->expectation;
}

/* ---- actor-keyed lists (TS Map insertion order) ---- */
#define FIND_ACTOR(array, count, key, out) do { \
    (out) = -1; \
    for (size_t i_ = 0; i_ < (count); i_++) if (qa_same_actor((array)[i_].actor, (key))) { (out) = (ptrdiff_t)i_; break; } \
  } while (0)
#define REMOVE_AT(array, count, index) do { \
    memmove(&(array)[(index)], &(array)[(index) + 1], ((count) - (size_t)(index) - 1) * sizeof *(array)); \
    (count)--; \
  } while (0)
static bool live_actor(const qa_world_snapshot *snapshot, qa_actor_id actor) {
  for (size_t i = 0; i < snapshot->actor_count; i++) if (qa_same_actor(snapshot->actors[i].id, actor)) return true;
  return false;
}
static const pose *pose_of(const effects_t *e, qa_actor_id actor) {
  for (size_t i = 0; i < e->pose_count; i++) if (qa_same_actor(e->poses[i].actor, actor)) return &e->poses[i];
  return NULL;
}
static bool pose_origin(void *ctx, qa_actor_id actor, qa_vec3 *origin_out) {
  const pose *p = pose_of(ctx, actor);
  if (p == NULL) return false;
  *origin_out = p->origin;
  return true;
}

/* ---- owner keys ---- */
static char *owner_key(const qa_app_sim_presentation_event *event, bool use_owner) {
  char *key = qa_presentation_owner_key(use_owner && event->has_owner ? &event->owner : NULL);
  if (key == NULL) qa_range_error("Out of memory");
  return key;
}
static bool retired(const effects_t *e, const char *key) {
  for (size_t i = 0; i < e->retired_count; i++) if (strcmp(e->retired_owners[i], key) == 0) return true;
  return false;
}
static const owner_revision *revision_of(const effects_t *e, const char *key) {
  for (size_t i = 0; i < e->revision_count; i++) if (strcmp(e->revisions[i].key, key) == 0) return &e->revisions[i];
  return NULL;
}
static void set_revision(effects_t *e, const char *key, double value) {
  for (size_t i = 0; i < e->revision_count; i++)
    if (strcmp(e->revisions[i].key, key) == 0) { e->revisions[i].revision = value; return; }
  GROW(e->revisions, e->revision_count, e->revision_capacity);
  size_t length = strlen(key) + 1;
  char *copy = xcalloc(length, 1);
  memcpy(copy, key, length);
  e->revisions[e->revision_count++] = (owner_revision){copy, value};
}
static bool same_owner(bool has_left, const qa_presentation_owner *left, qa_presentation_owner right) {
  return qa_same_presentation_owner(has_left ? left : NULL, right);
}

/* ---- event rings of retained pointers ---- */
static void free_unhandled_record(unhandled_record *record) {
  if (record == NULL) return;
  retained_free(record->source);
  free(record->reason);
  free(record);
}
typedef struct owner_filter { qa_presentation_owner owner; } owner_filter;
static bool pending_keep(const void *record, void *ctx) {
  const retained *r = *(retained *const *)record;
  const owner_filter *filter = ctx;
  return !same_owner(r->event.has_owner, &r->event.owner, filter->owner);
}
/* filter_live calls this once per live record: a dropped record is freed
 * after its owner is read (freeing first and filtering after read freed
 * records). */
static bool pending_keep_or_free(const void *record, void *ctx) {
  bool keep = pending_keep(record, ctx);
  if (!keep) retained_free(*(retained *const *)record);
  return keep;
}
static void free_pending_live(const void *record, int64_t id, void *ctx) {
  (void)id; (void)ctx;
  retained_free(*(retained *const *)record);
}
static void free_unhandled_live(const void *record, int64_t id, void *ctx) {
  (void)id; (void)ctx;
  free_unhandled_record(*(unhandled_record *const *)record);
}
static void reset_pending(effects_t *e) {
  qa_event_ring_for_each_live(&e->pending, free_pending_live, NULL);
  qa_event_ring_reset(&e->pending);
}
static void reset_unhandled(effects_t *e) {
  qa_event_ring_for_each_live(&e->unhandled, free_unhandled_live, NULL);
  qa_event_ring_reset(&e->unhandled);
}

/* ---- groups ---- */
static void entities_free(qa_scene_entity *entities) { free(entities); }
static void beams_clear(group *g) {
  for (size_t i = 0; i < g->beam_count; i++) { entities_free(g->beams[i].remote); entities_free(g->beams[i].local); }
  g->beam_count = 0;
}
static void group_destroy(group *g) {
  qa_app_source_particles_destroy(g->particles);
  if (g->renderer != NULL) { qa_scene_model_renderer_destroy(g->renderer); free(g->renderer); }
  free(g->models);
  free(g->statics);
  beams_clear(g);
  free(g->beams);
  free(g->sampled);
  free(g);
}
static group *group_find(const effects_t *e, qa_content_id content) {
  for (size_t i = 0; i < e->group_count; i++) if (strcmp(e->groups[i]->content, content) == 0) return e->groups[i];
  return NULL;
}
/* TS new SceneModelRenderer(provider, this.assets.world). */
static qa_scene_model_renderer *new_renderer(effects_t *e, qa_model_render_provider provider) {
  qa_world_scene *world = qa_app_application_assets_world(e->assets);
  qa_scene_model_renderer *renderer = xcalloc(1, sizeof *renderer);
  if (!qa_scene_model_renderer_init(renderer, provider, world)) qa_range_error("Out of memory");
  return renderer;
}
static ptrdiff_t prepared_find(const effects_t *e, qa_content_id content) {
  for (size_t i = 0; i < e->prepared_count; i++) if (strcmp(e->prepared[i].content, content) == 0) return (ptrdiff_t)i;
  return -1;
}
static void prepared_delete(effects_t *e, qa_content_id content, bool destroy) {
  ptrdiff_t index = prepared_find(e, content);
  if (index < 0) return;
  if (destroy) { qa_scene_model_renderer_destroy(e->prepared[index].renderer); free(e->prepared[index].renderer); }
  REMOVE_AT(e->prepared, e->prepared_count, index);
}
static const char *family_name(qa_game_family family) {
  return family == QA_GAME_FAMILY_Q1 ? "q1" : family == QA_GAME_FAMILY_Q2 ? "q2" : "q3";
}
static group *group_of(effects_t *e, qa_content_id content) {
  group *prior = group_find(e, content);
  if (prior != NULL) return prior;
  qa_app_provider_scene_assets provider = qa_app_application_assets_provider(e->assets, content);
  ptrdiff_t prepared = prepared_find(e, content);
  qa_scene_model_renderer *renderer = prepared >= 0 ? e->prepared[prepared].renderer : new_renderer(e, qa_app_application_assets_render_provider(e->assets, content));
  prepared_delete(e, content, false);
  group *g = xcalloc(1, sizeof *g);
  g->content = intern(e->strings, content);
  g->provider = provider;
  g->particles = qa_app_source_particles_create(e->random, -1);
  g->renderer = renderer;
  GROW(e->groups, e->group_count, e->group_capacity);
  e->groups[e->group_count++] = g;
  bool known = false;
  for (size_t i = 0; i < e->image_count; i++) if (e->images[i].family == provider.family) known = true;
  if (provider.family != QA_GAME_FAMILY_Q3 && !known) {
    family_image image; memset(&image, 0, sizeof image);
    image.family = provider.family;
    image.content = xcalloc(1, sizeof *image.content);
    const char *family = family_name(provider.family);
    qa_legacy_particle_image_make(image.content, family);
    char name[64];
    snprintf(name, sizeof name, "*%s-source-particles", family);
    qa_texture_sampling sampling = {"clamp", "linear"};
    image.image = qa_scene_image_registry_register_generated(qa_app_application_assets_images(e->assets), name,
        image.content->image, sampling)->image;
    GROW(e->images, e->image_count, e->image_capacity);
    e->images[e->image_count++] = image;
  }
  return g;
}

/* ---- lights, beams ---- */
typedef struct actor_match { qa_actor_id actor; } actor_match;
static bool light_has_actor(const void *value, void *ctx) {
  const timed_light *light = value;
  return light->has_actor && qa_same_actor(light->actor, ((actor_match *)ctx)->actor);
}
static void light(effects_t *e, qa_vec3 origin, double time, double radius, double duration, qa_vec3 color,
                  double decay, double minimum, const qa_actor_id *actor) {
  if (actor != NULL) {
    actor_match match = {*actor};
    qa_bounded_queue_remove_first(&e->lights, light_has_actor, &match);
  }
  timed_light value; memset(&value, 0, sizeof value);
  value.origin = origin; value.born = time; value.die = time + duration; value.radius = radius;
  value.color = color; value.decay = decay; value.minimum = minimum;
  value.has_actor = actor != NULL; if (actor != NULL) value.actor = *actor;
  qa_bounded_queue_push(&e->lights, &value);
}
static void add_beam(effects_t *e, beam value) {
  ptrdiff_t old = -1;
  if (value.has_actor)
    for (size_t i = 0; i < e->beam_count; i++) {
      const beam *b = &e->beams[i];
      if (b->has_actor && qa_same_actor(b->actor, value.actor) && b->family == value.family
          && (b->model == value.model || (b->model != NULL && value.model != NULL && strcmp(b->model, value.model) == 0))) {
        old = (ptrdiff_t)i; break;
      }
    }
  if (old >= 0) REMOVE_AT(e->beams, e->beam_count, old);
  GROW(e->beams, e->beam_count, e->beam_capacity);
  e->beams[e->beam_count++] = value;
}
static void push_beam(effects_t *e, beam value) {
  GROW(e->beams, e->beam_count, e->beam_capacity);
  e->beams[e->beam_count++] = value;
}
static void push_explosion(effects_t *e, explosion value) {
  GROW(e->explosions, e->explosion_count, e->explosion_capacity);
  e->explosions[e->explosion_count++] = value;
}
static void push_sampled(effects_t *e, qa_surface_dynamic_light value) {
  GROW(e->sampled_lights, e->sampled_light_count, e->sampled_light_capacity);
  e->sampled_lights[e->sampled_light_count++] = value;
}
static void admit_sound(effects_t *e, qa_content_id content, const char *path, qa_vec3 origin, double seconds) {
  qa_app_source_effect_sound sound; memset(&sound, 0, sizeof sound);
  sound.content = content; sound.path = intern(e->strings, path); sound.origin = origin;
  sound.channel = 0; sound.volume = 1; sound.seconds = seconds; sound.playback_kind = "once";
  qa_event_ring_admit(&e->sounds, &sound);
}
static void reject(effects_t *e, const retained *source, const char *reason) {
  unhandled_record *record = xcalloc(1, sizeof *record);
  record->source = retained_clone(source);
  size_t length = strlen(reason) + 1;
  record->reason = xcalloc(length, 1);
  memcpy(record->reason, reason, length);
  qa_event_ring_admit(&e->unhandled, &record);
}

/* ---- lifecycle ---- */
static effects_t *create(qa_app_application_assets *assets, qa_scene_queries queries, qa_app_effects_is_player_fn is_player,
                         void *is_player_ctx, double seed, strings *shared) {
  effects_t *e = xcalloc(1, sizeof *e);
  e->assets = assets; e->queries = queries; e->is_player = is_player; e->is_player_ctx = is_player_ctx; e->seed = seed;
  if (shared != NULL) e->strings = shared;
  else { e->strings = xcalloc(1, sizeof *e->strings); e->owns_strings = true; }
  e->random = qa_app_sim_source_random_create(seed, "classic");
  if (!qa_event_ring_init(&e->pending, 32768, sizeof(retained *))
      || !qa_event_ring_init(&e->unhandled, 4096, sizeof(unhandled_record *))
      || !qa_event_ring_init(&e->sounds, 8192, sizeof(qa_app_source_effect_sound))
      || !qa_bounded_queue_init(&e->lights, 32, sizeof(timed_light)))
    qa_range_error("Out of memory");
  qa_event_ring_open_cursor(&e->pending, &e->pending_cursor, false);
  qa_event_ring_open_cursor(&e->unhandled, &e->unhandled_cursor, false);
  qa_event_ring_open_cursor(&e->sounds, &e->sounds_cursor, false);
  e->player_views = qa_app_q2_effect_views_create();
  e->sequence = -1;
  return e;
}
qa_app_application_effects *qa_app_application_effects_create(qa_app_application_assets *assets,
    qa_scene_queries queries, qa_app_effects_is_player_fn is_player, void *is_player_ctx, double seed) {
  return create(assets, queries, is_player, is_player_ctx, seed, NULL);
}
qa_app_application_assets *qa_app_application_effects_assets(const qa_app_application_effects *e) { return e->assets; }
void qa_app_application_effects_bind_renderer_hardware(qa_app_application_effects *e, qa_q3_hardware_read_fn read, void *ctx) {
  e->hardware_read = read; e->hardware_ctx = ctx;
}
qa_app_q3_application_effects *qa_app_application_effects_q3(qa_app_application_effects *e, qa_content_id content) {
  for (size_t i = 0; i < e->q3_count; i++) if (strcmp(e->q3[i].content, content) == 0) return e->q3[i].effects;
  return NULL;
}

/* ---- receive ---- */
static void receive_one(effects_t *e, const qa_app_sim_presentation_event *event);
static void owner_event(effects_t *e, const qa_app_sim_presentation_event *event) {
  qa_presentation_owner owner = event->presentation_owner.owner;
  char *key = qa_presentation_owner_key(&owner);
  if (key == NULL) qa_range_error("Out of memory");
  if (is(event->presentation_owner.kind, "retired") && !retired(e, key)) {
    GROW(e->retired_owners, e->retired_count, e->retired_capacity);
    size_t length = strlen(key) + 1;
    char *copy = xcalloc(length, 1);
    memcpy(copy, key, length);
    e->retired_owners[e->retired_count++] = copy;
  }
  set_revision(e, key, event->sequence);
  free(key);
  owner_filter filter = {owner};
  qa_event_ring_filter_live(&e->pending, pending_keep_or_free, &filter);
  for (size_t i = 0; i < e->recipient_count; i++) receive_one(e->recipients[i].effects, event);
  if (is(event->presentation_owner.kind, "refreshed")) return;
  size_t kept = 0;
  for (size_t i = 0; i < e->beam_count; i++)
    if (!same_owner(e->beams[i].has_owner, &e->beams[i].owner, owner)) e->beams[kept++] = e->beams[i];
  e->beam_count = kept;
  for (size_t i = e->flashlight_count; i-- > 0;)
    if (same_owner(e->flashlights[i].has_owner, &e->flashlights[i].owner, owner)) REMOVE_AT(e->flashlights, e->flashlight_count, i);
  for (size_t i = e->shadow_light_count; i-- > 0;)
    if (same_owner(e->shadow_lights[i].has_owner, &e->shadow_lights[i].owner, owner)) REMOVE_AT(e->shadow_lights, e->shadow_light_count, i);
  for (size_t i = e->source_light_count; i-- > 0;)
    if (same_owner(e->source_lights[i].has_owner, &e->source_lights[i].owner, owner)) REMOVE_AT(e->source_lights, e->source_light_count, i);
  for (size_t i = e->static_brush_count; i-- > 0;)
    if (same_owner(e->static_brushes[i].has_owner, &e->static_brushes[i].owner, owner)) REMOVE_AT(e->static_brushes, e->static_brush_count, i);
  for (size_t g = 0; g < e->group_count; g++) {
    group *grp = e->groups[g];
    for (size_t i = grp->static_count; i-- > 0;)
      if (same_owner(grp->statics[i].has_owner, &grp->statics[i].owner, owner)) REMOVE_AT(grp->statics, grp->static_count, i);
  }
}
static void receive_one(effects_t *e, const qa_app_sim_presentation_event *event) {
  if (is(event->kind, "presentation-owner")) { owner_event(e, event); return; }
  if (event->has_owner) {
    char *key = owner_key(event, true);
    bool gone = retired(e, key);
    free(key);
    if (gone) return;
  }
  if (event->has_recipient) {
    qa_app_sim_presentation_event shared = *event;
    shared.has_recipient = false;
    memset(&shared.recipient, 0, sizeof shared.recipient);
    ptrdiff_t index;
    FIND_ACTOR(e->recipients, e->recipient_count, event->recipient, index);
    effects_t *target;
    if (index < 0) {
      target = create(e->assets, e->queries, e->is_player, e->is_player_ctx, e->seed, e->strings);
      target->events_only = true;
      qa_app_application_effects_bind_renderer_hardware(target, read_hardware, e);
      GROW(e->recipients, e->recipient_count, e->recipient_capacity);
      e->recipients[e->recipient_count++] = (recipient){event->recipient, target};
    } else target = e->recipients[index].effects;
    receive_one(target, &shared);
    return;
  }
  if (is(event->kind, "q1-fog")) return;
  if (event->sequence <= e->sequence) return;
  e->sequence = event->sequence;
  retained *r = retain_event(e->strings, event);
  qa_event_ring_admit(&e->pending, &r);
}
void qa_app_application_effects_receive(qa_app_application_effects *e, const qa_app_sim_presentation_event *events, size_t count) {
  ensure_open(e);
  for (size_t i = 0; i < count; i++) receive_one(e, &events[i]);
}

/* ---- drains ---- */
qa_app_unhandled_effect *qa_app_application_effects_drain_unhandled(qa_app_application_effects *e, size_t *out_count) {
  size_t count = 0;
  unhandled_record **taken = qa_event_ring_take(&e->unhandled, &e->unhandled_cursor, &count);
  qa_event_ring_retire(&e->unhandled);
  qa_app_unhandled_effect *result = NULL; size_t result_count = 0, capacity = 0;
  for (size_t i = 0; i < count; i++) {
    GROW(result, result_count, capacity);
    qa_app_unhandled_effect row; memset(&row, 0, sizeof row);
    retained *source = taken[i]->source;
    row.source = source->event; row.reason = taken[i]->reason; /* the owned copy moves into the row */
    result[result_count++] = row;
    free(source);
    free(taken[i]);
  }
  free(taken);
  for (size_t r = 0; r < e->recipient_count; r++) {
    size_t entries = 0;
    qa_app_unhandled_effect *rows = qa_app_application_effects_drain_unhandled(e->recipients[r].effects, &entries);
    for (size_t i = 0; i < entries; i++) {
      GROW(result, result_count, capacity);
      qa_app_unhandled_effect row = rows[i];
      row.source.has_recipient = true; row.source.recipient = e->recipients[r].actor;
      result[result_count++] = row;
    }
    free(rows);
  }
  *out_count = result_count;
  return result;
}
void qa_app_unhandled_effects_free(qa_app_unhandled_effect *rows, size_t count) {
  for (size_t i = 0; i < count; i++) { free(rows[i].reason); qa_app_sim_presentation_event_release(&rows[i].source); }
  free(rows);
}
qa_app_source_effect_sound *qa_app_application_effects_drain_sounds(qa_app_application_effects *e, size_t *out_count) {
  size_t count = 0;
  qa_app_source_effect_sound *taken = qa_event_ring_take(&e->sounds, &e->sounds_cursor, &count);
  qa_event_ring_retire(&e->sounds);
  qa_app_source_effect_sound *result = xcalloc(count, sizeof *result);
  size_t capacity = count == 0 ? 1 : count;
  if (count > 0) memcpy(result, taken, count * sizeof *result);
  free(taken);
  for (size_t i = 0; i < e->q3_count; i++) {
    size_t drained = 0;
    qa_app_source_effect_sound *sounds = qa_app_q3_application_effects_drain_sounds(e->q3[i].effects, &drained);
    for (size_t s = 0; s < drained; s++) {
      GROW(result, count, capacity);
      result[count++] = sounds[s];
    }
    free(sounds);
  }
  *out_count = count;
  return result;
}
qa_app_recipient_effect_sounds *qa_app_application_effects_drain_recipient_sounds(qa_app_application_effects *e, size_t *out_count) {
  qa_app_recipient_effect_sounds *result = xcalloc(e->recipient_count, sizeof *result);
  size_t count = 0;
  for (size_t i = 0; i < e->recipient_count; i++) {
    size_t drained = 0;
    qa_app_source_effect_sound *sounds = qa_app_application_effects_drain_sounds(e->recipients[i].effects, &drained);
    if (drained > 0) result[count++] = (qa_app_recipient_effect_sounds){e->recipients[i].actor, sounds, drained};
    else free(sounds);
  }
  *out_count = count;
  return result;
}

/* ---- player view ---- */
static qa_app_q2_effect_player_view shared_player_view(effects_t *e, qa_actor_id actor, qa_scene_camera camera) {
  double seconds = e->has_time ? e->time : 0;
  qa_app_q2_effect_player_view view = qa_app_q2_effect_views_frame(e->player_views, actor, camera, seconds, pose_origin, e);
  ptrdiff_t index;
  FIND_ACTOR(e->bonus_flashes, e->bonus_count, actor, index);
  if (index < 0) return view;
  /* WinQuake view.c V_BonusFlash_f / V_CalcBlend: 50 percent, decaying at 100 per second. */
  double until = e->bonus_flashes[index].until;
  double percent = qa_js_max(0, qa_js_min(50, (until - seconds) * 100));
  if (percent == 0) return view;
  qa_vec4 base = view.has_blend ? view.blend : (qa_vec4){0, 0, 0, 0};
  view.blend = qa_q2_add_blend(base, (qa_vec3){215.0 / 255, 186.0 / 255, 69.0 / 255}, percent / 255);
  view.has_blend = true;
  return view;
}
qa_app_q2_effect_player_view qa_app_application_effects_player_view(qa_app_application_effects *e, qa_actor_id actor,
    qa_scene_camera camera) {
  qa_app_q2_effect_player_view shared = shared_player_view(e, actor, camera);
  ptrdiff_t index;
  FIND_ACTOR(e->recipients, e->recipient_count, actor, index);
  if (index < 0) return shared;
  qa_app_q2_effect_player_view local = qa_app_application_effects_player_view(e->recipients[index].effects, actor, shared.camera);
  qa_app_q2_effect_player_view result;
  result.camera = local.camera;
  result.infrared = shared.infrared || local.infrared;
  if (!local.has_blend) { result.has_blend = shared.has_blend; result.blend = shared.blend; }
  else {
    qa_vec4 base = shared.has_blend ? shared.blend : (qa_vec4){0, 0, 0, 0};
    result.has_blend = true;
    result.blend = qa_q2_add_blend(base, (qa_vec3){local.blend.x, local.blend.y, local.blend.z}, local.blend.w);
  }
  return result;
}

/* ---- preload ---- */
typedef struct q3_create_call {
  effects_t *effects; qa_content_id content; bool preload_character;
  qa_app_q3_application_effects *result;
} q3_create_call;
static void q3_create_run(void *ctx) {
  q3_create_call *call = ctx;
  call->result = qa_app_q3_application_effects_create(call->effects->assets, call->effects->queries, call->content,
      call->preload_character, read_hardware, call->effects);
}
static void q3_set(effects_t *e, qa_content_id content, qa_app_q3_application_effects *effects) {
  GROW(e->q3, e->q3_count, e->q3_capacity);
  e->q3[e->q3_count++] = (q3_owner){intern(e->strings, content), effects};
}
typedef struct transient_call { effects_t *effects; qa_content_id content; const char *path; } transient_call;
static void transient_run(void *ctx) {
  transient_call *call = ctx;
  effects_t *e = call->effects;
  qa_app_provider_scene_assets provider = qa_app_application_assets_provider(e->assets, call->content);
  const qa_resolved_resource_reference *resolved = NULL;
  if (!qa_content_mounted_resolve(provider.mounts, call->path, &resolved) || resolved == NULL) return;
  qa_app_model_asset asset = qa_app_application_assets_model(e->assets, call->content, call->path);
  ptrdiff_t index = prepared_find(e, call->content);
  qa_scene_model_renderer *renderer;
  if (index < 0) {
    renderer = new_renderer(e, qa_app_application_assets_render_provider(e->assets, call->content));
    GROW(e->prepared, e->prepared_count, e->prepared_capacity);
    e->prepared[e->prepared_count++] = (prepared_renderer){intern(e->strings, call->content), renderer};
  } else renderer = e->prepared[index].renderer;
  qa_model_source_options options; memset(&options, 0, sizeof options);
  qa_scene_model_renderer_preload_model(renderer, &asset.resource, &asset.model, options, true);
}
static void push_failure(qa_app_effect_preload_failure **rows, size_t *count, size_t *capacity, qa_content_id content,
                         const char *path, const char *error) {
  GROW(*rows, *count, *capacity);
  qa_app_effect_preload_failure row;
  row.content = content;
  size_t p = strlen(path) + 1, m = strlen(error) + 1;
  row.path = xcalloc(p, 1); memcpy(row.path, path, p);
  row.error = xcalloc(m, 1); memcpy(row.error, error, m);
  (*rows)[(*count)++] = row;
}
static void add_content(qa_content_id **set, size_t *count, size_t *capacity, qa_content_id content) {
  for (size_t i = 0; i < *count; i++) if (strcmp((*set)[i], content) == 0) return;
  GROW(*set, *count, *capacity);
  (*set)[(*count)++] = content;
}
qa_app_effect_preload_failure *qa_app_application_effects_preload_transient_resources(qa_app_application_effects *e, size_t *out_count) {
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(qa_app_application_assets_content(e->assets));
  qa_content_id character = recipe->character.definition.content, map = recipe->map.entities.content;
  qa_content_id *contents = NULL; size_t content_count = 0, content_capacity = 0;
  add_content(&contents, &content_count, &content_capacity, map);
  add_content(&contents, &content_count, &content_capacity, character);
  for (size_t i = 0; i < recipe->weapons.count; i++) add_content(&contents, &content_count, &content_capacity, recipe->weapons.values[i].content);
  if (recipe->equipment.grapple.is_enabled) add_content(&contents, &content_count, &content_capacity, recipe->equipment.grapple.source.content);
  if (recipe->equipment.hand_grenades.is_enabled)
    add_content(&contents, &content_count, &content_capacity, recipe->equipment.hand_grenades.source.content);
  if (recipe->enemies.kind == QA_ENEMY_SELECTION_REPLACE) {
    if (!recipe->enemies.default_target.is_map_defined)
      add_content(&contents, &content_count, &content_capacity, recipe->enemies.default_target.reference.source.content);
    for (size_t i = 0; i < recipe->enemies.by_classname_count; i++)
      if (!recipe->enemies.by_classname[i].target.is_map_defined)
        add_content(&contents, &content_count, &content_capacity, recipe->enemies.by_classname[i].target.reference.source.content);
  }
  if (recipe->enemies.kind == QA_ENEMY_SELECTION_PLAYERS) add_content(&contents, &content_count, &content_capacity, recipe->enemies.character.content);
  qa_app_effect_preload_failure *failures = NULL; size_t failure_count = 0, failure_capacity = 0;
  if (strcmp(character, map) != 0 && expectation(e, character)->family == QA_GAME_FAMILY_Q3
      && qa_app_application_effects_q3(e, character) == NULL) {
    q3_create_call call = {e, character, true, NULL};
    qa_caught_error caught;
    if (qa_try(q3_create_run, &call, &caught)) q3_set(e, character, call.result);
    else push_failure(&failures, &failure_count, &failure_capacity, character, "Q3 character effect media", caught.message);
  }
  const qa_q2_transient_models *models = &QA_Q2_TRANSIENT_MODELS;
  const char *const paths[] = {models->cable, models->parasite, models->explosion, models->flash,
                               models->rocket_explosion, models->smoke, models->lightning, models->bfg_explosion};
  for (size_t c = 0; c < content_count; c++) {
    if (expectation(e, contents[c])->family != QA_GAME_FAMILY_Q2) continue;
    for (size_t p = 0; p < sizeof paths / sizeof paths[0]; p++) {
      transient_call call = {e, contents[c], paths[p]};
      qa_caught_error caught;
      if (!qa_try(transient_run, &call, &caught)) {
        prepared_delete(e, contents[c], true);
        push_failure(&failures, &failure_count, &failure_capacity, contents[c], paths[p], caught.message);
      }
    }
  }
  free(contents);
  *out_count = failure_count;
  return failures;
}
void qa_app_effect_preload_failures_free(qa_app_effect_preload_failure *rows, size_t count) {
  for (size_t i = 0; i < count; i++) { free(rows[i].path); free(rows[i].error); }
  free(rows);
}
void qa_app_application_effects_preload_model(qa_app_application_effects *e, qa_content_id content, const char *path) {
  qa_model_render_provider provider = qa_app_application_assets_render_provider(e->assets, content);
  qa_app_model_asset asset = qa_app_application_assets_model(e->assets, content, path);
  group *g = group_find(e, content);
  qa_scene_model_renderer *renderer = g != NULL ? g->renderer : NULL;
  if (renderer == NULL) {
    ptrdiff_t index = prepared_find(e, content);
    if (index >= 0) renderer = e->prepared[index].renderer;
  }
  if (renderer == NULL) {
    renderer = new_renderer(e, provider);
    GROW(e->prepared, e->prepared_count, e->prepared_capacity);
    e->prepared[e->prepared_count++] = (prepared_renderer){intern(e->strings, content), renderer};
  }
  qa_model_source_options options; memset(&options, 0, sizeof options);
  qa_scene_model_renderer_preload_model(renderer, &asset.resource, &asset.model, options, false);
}

/* ---- scene entities ---- */
static qa_scene_entity frame_entity(const qa_app_model_asset *asset, qa_vec3 origin, qa_vec3 angles, double frame,
                                    double previous_frame, double back_lerp, double skin, double alpha,
                                    const char *flags_kind, double flags_bits) {
  qa_scene_entity entity; memset(&entity, 0, sizeof entity);
  entity.actor = NULL;
  entity.resource = asset->resource;
  entity.model = asset->model;
  entity.transform.origin = origin; entity.transform.axis = qa_angles_to_axis(angles); entity.transform.scale = white;
  entity.previous_origin = origin;
  entity.pose.is_frame = true; entity.pose.frame = frame; entity.pose.previous_frame = previous_frame; entity.pose.back_lerp = back_lerp;
  entity.skin = skin;
  entity.color = (qa_vec4){1, 1, 1, alpha};
  entity.shader_time.kind = QA_SOURCE_TIME_SECONDS; entity.shader_time.value = 0;
  entity.flags_kind = flags_kind; entity.flags_bits = flags_bits;
  entity.lighting_origin = origin;
  entity.shadow_plane = 0;
  return entity;
}

/* ---- per-event joins ---- */
typedef struct event_scope { effects_t *effects; char *key; double revision; const retained *source; } event_scope;
static bool current(const event_scope *s) {
  const effects_t *e = s->effects;
  const owner_revision *now = revision_of(e, s->key);
  return !e->closed && !retired(e, s->key) && s->source->event.sequence > (now == NULL ? -1 : now->revision)
      && (now == NULL ? 0 : now->revision) == s->revision;
}
static beam beam_base(const retained *source) {
  beam value; memset(&value, 0, sizeof value);
  value.has_owner = source->event.has_owner; value.owner = source->owner;
  value.content = source->content;
  return value;
}
static bool muzzle(effects_t *e, qa_vec3 origin, double seconds, double flash, bool silenced, qa_actor_id actor) {
  if (!((flash >= 0 && flash <= 20 && flash != 15) || (flash >= 30 && flash <= 39))) return false;
  qa_vec3 color = flash == 6 ? (qa_vec3){0.5, 0.5, 1} : flash == 7 ? (qa_vec3){1, 0.5, 0.2}
      : flash == 8 || flash == 4 || flash == 31 ? (qa_vec3){1, 0.5, 0} : flash == 3 ? (qa_vec3){1, 0.25, 0}
      : flash == 12 || flash == 19 || flash == 34 || flash == 9 ? (qa_vec3){0, 1, 0}
      : flash == 10 || flash == 36 ? (qa_vec3){1, 0, 0} : flash == 35 ? (qa_vec3){-1, -1, -1}
      : flash == 17 || flash == 38 ? (qa_vec3){0, 0, 1} : flash == 39 ? (qa_vec3){0, 1, 1}
      : flash == 16 || flash == 18 || flash == 20 ? (qa_vec3){1, 0.5, 0.5} : flash == 30 ? (qa_vec3){0.9, 0.7, 0} : (qa_vec3){1, 1, 0};
  double radius = flash == 4 ? 225 : flash == 5 ? 250 : flash == 3 ? 200 : silenced ? 100 : 200;
  double duration = flash == 33 || (flash >= 36 && flash <= 39) ? 0.1 : flash >= 9 && flash <= 11 ? 0.001 : flash == 4 || flash == 5 ? 0.0001 : 0;
  double jitter = qa_js_bit_and(qa_app_sim_source_random_next_integer(e->random), 31);
  light(e, origin, seconds, radius + jitter, duration, color, 0, 32, &actor);
  return true;
}
static void monster_muzzle(effects_t *e, const event_scope *scope, qa_vec3 origin, double flash) {
  const retained *source = scope->source;
  qa_app_monster_muzzle profile;
  bool rerelease = is(expectation(e, source->content)->edition, "rerelease");
  if (!qa_app_q2_monster_muzzle(flash, rerelease, &profile)) {
    char reason[160];
    snprintf(reason, sizeof reason, "Quake II monster muzzle flash %s has no source definition", QA_JS_NUMBER(flash));
    reject(e, source, reason);
    return;
  }
  group *g = group_of(e, source->content);
  if (!current(scope)) return;
  double jitter = qa_js_bit_and(qa_app_sim_source_random_next_integer(e->random), profile.mask);
  const qa_q2_presentation_event *q2 = is(source->event.kind, "q2") ? source->event.q2 : NULL;
  bool has_actor = q2 != NULL && is(q2->kind, "monster-muzzleflash") && q2->has_actor;
  light(e, origin, source->event.seconds, profile.radius + jitter, profile.radius == 300 ? 0.2 : 0, profile.color, 0, 32,
        has_actor ? &q2->actor : NULL);
  if (profile.particles) qa_app_source_particles_q2_impact(g->particles, origin, zero, 0, 40, source->event.seconds, "normal");
  if (profile.smoke) {
    explosion smoke; memset(&smoke, 0, sizeof smoke);
    smoke.content = source->content; smoke.origin = origin; smoke.angles = zero; smoke.start = source->event.seconds - 0.1;
    smoke.frames = 4; smoke.base_frame = 0; smoke.path = QA_Q2_TRANSIENT_MODELS.smoke; smoke.kind = EXPLOSION_MISC; smoke.flags = 32;
    push_explosion(e, smoke);
    explosion flash_model = smoke;
    flash_model.frames = 2; flash_model.path = QA_Q2_TRANSIENT_MODELS.flash; flash_model.kind = EXPLOSION_FLASH; flash_model.flags = 8;
    push_explosion(e, flash_model);
  }
}
static double random_unit(effects_t *e) { return qa_app_sim_source_random_next_unit(e->random); }
static double random_integer(effects_t *e) { return qa_app_sim_source_random_next_integer(e->random); }
static void q2_effect(effects_t *e, const event_scope *scope, const qa_q2_presentation_event *event) {
  const retained *source = scope->source;
  const char *original = event->effect == NULL ? "" : event->effect;
  char name[256];
  const char *stripped = strncmp(original, "q2:", 3) == 0 ? original + 3 : original;
  size_t length = 0;
  for (; stripped[length] != '\0' && length + 1 < sizeof name; length++) name[length] = stripped[length] == '_' ? '-' : stripped[length];
  name[length] = '\0';
  group *g = group_of(e, source->content);
  qa_app_source_particles *p = g->particles;
  double time = source->event.seconds;
  qa_content_id content = source->content;
  if (!current(scope)) return;
  if (is(name, "heatbeam-sparks") || is(name, "heatbeam-steam")) {
    bool sparks = is(name, "heatbeam-sparks");
    qa_app_source_particles_q2_steam(p, event->origin, event->direction, sparks ? 8 : 0xe0, sparks ? 50 : 20, 60, time, false);
    admit_sound(e, content, "weapons/lashit.wav", event->origin, time);
  } else if (is(name, "chainfist-smoke")) qa_app_source_particles_q2_steam(p, event->origin, (qa_vec3){0, 0, 1}, 0, 20, 20, time, true);
  else if (is(name, "tracker-explosion")) {
    qa_app_source_particles_q2_color_explosion(p, event->origin, time, 0, 1);
    light(e, event->origin, time, 150, 0.1, (qa_vec3){-1, -1, -1}, 0, 250, NULL);
    admit_sound(e, content, "weapons/disrupthit.wav", event->origin, time);
  } else if (is(name, "blood")) qa_app_source_particles_q2_impact(p, event->origin, event->direction, 0xe8, 60, time, "normal");
  else if (is(name, "moreblood")) qa_app_source_particles_q2_impact(p, event->origin, event->direction, 0xe8, 250, time, "normal");
  else if (is(name, "gunshot") || is(name, "shotgun"))
    qa_app_source_particles_q2_impact(p, event->origin, event->direction, 0, is(name, "gunshot") ? 40 : 20, time, "normal");
  else if (is(name, "sparks") || is(name, "bullet-sparks")) qa_app_source_particles_q2_impact(p, event->origin, event->direction, 0xe0, 6, time, "normal");
  else if (is(name, "screen-sparks") || is(name, "shield-sparks"))
    qa_app_source_particles_q2_impact(p, event->origin, event->direction, is(name, "screen-sparks") ? 0xd0 : 0xb0, 40, time, "normal");
  else if (is(name, "laser-sparks")) qa_app_source_particles_q2_impact(p, event->origin, event->direction, event->color, event->count, time, "fixed");
  else if (is(name, "tunnel-sparks")) qa_app_source_particles_q2_impact(p, event->origin, event->direction, event->color, event->count, time, "up");
  else if (is(name, "splash")) {
    static const double colors[] = {0, 0xe0, 0xb0, 0x50, 0xd0, 0xe0, 0xe8};
    double color = event->color >= 0 && event->color < 7 && event->color == floor(event->color) ? colors[(size_t)event->color] : 0;
    qa_app_source_particles_q2_impact(p, event->origin, event->direction, color, event->count, time, "normal");
  } else if (is(name, "bluehyperblaster")) qa_app_source_particles_q2_impact(p, event->origin, event->direction, 0xe0, 40, time, "blaster");
  else if (is(name, "blaster") || is(name, "blaster2") || is(name, "flechette")) {
    bool blaster = is(name, "blaster"), blaster2 = is(name, "blaster2");
    qa_app_source_particles_q2_impact(p, event->origin, event->direction, blaster ? 0xe0 : blaster2 ? 0xd0 : 0x6f, 40, time, "blaster");
    qa_vec3 direction = event->direction;
    double yaw = direction.x != 0 ? atan2(direction.y, direction.x) * 180 / M_PI : direction.y > 0 ? 90 : direction.y < 0 ? 270 : 0;
    explosion value; memset(&value, 0, sizeof value);
    value.content = content; value.origin = event->origin;
    value.angles = (qa_vec3){acos(direction.z) * 180 / M_PI, yaw, 0};
    value.start = time - 0.1; value.frames = 4; value.base_frame = 0; value.path = QA_Q2_TRANSIENT_MODELS.explosion;
    value.kind = EXPLOSION_MISC; value.flags = 8 | 32; value.skin = blaster ? 0 : blaster2 ? 1 : 2;
    value.has_light = true; value.light_radius = 150;
    value.light_color = blaster ? (qa_vec3){1, 1, 0} : blaster2 ? (qa_vec3){0, 1, 0} : (qa_vec3){0.19, 0.41, 0.75};
    push_explosion(e, value);
  } else if (is(name, "greenblood")) qa_app_source_particles_q2_impact(p, event->origin, event->direction, 0xdf, 30, time, "fixed");
  else if (is(name, "electric-sparks")) qa_app_source_particles_q2_impact(p, event->origin, event->direction, 0x75, 40, time, "normal");
  else if (is(name, "player-teleport") || is(name, "other-teleport")) qa_app_source_particles_q2_teleport(p, event->origin, time);
  else if (is(name, "boss-teleport")) qa_app_source_particles_q2_big_teleport(p, event->origin, time);
  else if (is(name, "item-respawn")) qa_app_source_particles_q2_respawn(p, event->origin, time, "item");
  else if (is(name, "logout")) qa_app_source_particles_q2_respawn(p, event->origin, time, "logout");
  else if (is(name, "bfg-bigexplosion")) qa_app_source_particles_q2_explosion(p, event->origin, time, true);
  else if (is(name, "berserk-slam")) qa_app_source_particles_q2_berserk_slam(p, event->origin, event->direction, time);
  else if (is(name, "plain-explosion")) {
    explosion value; memset(&value, 0, sizeof value);
    value.content = content; value.origin = event->origin;
    value.angles = (qa_vec3){0, fmod(random_integer(e), 360), 0};
    value.start = time - 0.1; value.frames = 15;
    value.base_frame = random_unit(e) < 0.5 ? 15 : 0;
    value.path = QA_Q2_TRANSIENT_MODELS.rocket_explosion; value.kind = EXPLOSION_POLY; value.flags = 8; value.skin = 0;
    value.has_light = true; value.light_radius = 350; value.light_color = orange;
    push_explosion(e, value);
    admit_sound(e, content, "weapons/rocklx1a.wav", event->origin, time);
  } else if (is(name, "explosion1") || is(name, "explosion2") || is(name, "rocket-explosion") || is(name, "rocket-explosion-water")
             || is(name, "grenade-explosion") || is(name, "grenade-explosion-water") || is(name, "bfg-explosion")) {
    bool grenade = is(name, "explosion2") || strncmp(name, "grenade", 7) == 0, bfg = is(name, "bfg-explosion");
    if (!bfg) qa_app_source_particles_q2_explosion(p, event->origin, time, false);
    double start = time - 0.1, frames = bfg ? 4 : grenade ? 19 : 15;
    explosion value; memset(&value, 0, sizeof value);
    value.content = content; value.origin = event->origin;
    value.angles = (qa_vec3){0, fmod(random_integer(e), 360), 0};
    value.start = start; value.frames = frames;
    value.base_frame = bfg ? 0 : grenade ? 30 : random_unit(e) < 0.5 ? 15 : 0;
    value.path = bfg ? QA_Q2_TRANSIENT_MODELS.bfg_explosion : QA_Q2_TRANSIENT_MODELS.rocket_explosion;
    value.kind = EXPLOSION_POLY; value.flags = bfg ? 8 | 32 : 8; value.skin = 0;
    value.has_light = true; value.light_radius = 350; value.light_color = bfg ? (qa_vec3){0, 1, 0} : orange;
    push_explosion(e, value);
    size_t name_length = strlen(name);
    bool water = name_length >= 6 && strcmp(name + name_length - 6, "-water") == 0;
    if (!bfg)
      admit_sound(e, content, water ? "weapons/xpld_wat.wav" : grenade ? "weapons/grenlx1a.wav" : "weapons/rocklx1a.wav", event->origin, time);
  } else if (is(name, "footstep") || is(name, "monster-footstep") || is(name, "fall") || is(name, "fall-short") || is(name, "fall-far")) {
  } else {
    char reason[320];
    snprintf(reason, sizeof reason, "Unresolved Quake II source effect %s", original);
    reject(e, source, reason);
  }
}
static void q1_event(effects_t *e, const event_scope *scope, const qa_q1_event *event) {
  const retained *source = scope->source;
  if (is(event->kind, "effect") && is(event->effect, "pickup")) {
    if (event->has_actor) {
      ptrdiff_t index;
      FIND_ACTOR(e->bonus_flashes, e->bonus_count, event->actor, index);
      if (index >= 0) e->bonus_flashes[index].until = source->event.seconds + 0.5;
      else {
        GROW(e->bonus_flashes, e->bonus_count, e->bonus_capacity);
        e->bonus_flashes[e->bonus_count++] = (actor_time){event->actor, source->event.seconds + 0.5};
      }
    }
    return;
  }
  if (is(event->kind, "static-model")) {
    if (event->path == NULL || event->path[0] == '\0') return;
    qa_app_model_asset asset = qa_app_application_assets_model(e->assets, source->content, event->path);
    if (!current(scope)) return;
    const qa_decoded_model *model = &asset.model;
    if (model->kind == QA_DECODED_MODEL_BRUSH_MODEL) {
      qa_world_scene *scene = asset.brush_scene;
      if (scene == NULL) fail("Static brush %s has no prepared world scene", event->path);
      static_brush brush; memset(&brush, 0, sizeof brush);
      brush.has_owner = source->event.has_owner; brush.owner = source->owner;
      brush.scene = scene; brush.model = model->brush_model_index;
      brush.transform.origin = event->origin; brush.transform.axis = qa_angles_to_axis(event->angles); brush.transform.has_scale = false;
      brush.frame = event->frame;
      GROW(e->static_brushes, e->static_brush_count, e->static_brush_capacity);
      e->static_brushes[e->static_brush_count++] = brush;
      return;
    }
    group *g = group_of(e, source->content);
    if (!current(scope)) return;
    static_model value; memset(&value, 0, sizeof value);
    value.has_owner = source->event.has_owner; value.owner = source->owner;
    value.entity = frame_entity(&asset, event->origin, event->angles, event->frame, event->frame, 0, event->skin, 1, "q1", 0);
    GROW(g->statics, g->static_count, g->static_capacity);
    g->statics[g->static_count++] = value;
    return;
  }
  if (!is(event->kind, "effect") && !is(event->kind, "beam") && !is(event->kind, "colored-explosion")) return;
  group *g = group_of(e, source->content);
  qa_app_source_particles *particles = g->particles;
  double seconds = source->event.seconds;
  qa_content_id content = source->content;
  if (!current(scope)) return;
  if (is(event->kind, "beam")) {
    static const struct { const char *style, *model; } models[] = {
      {"lightning1", "progs/bolt.mdl"}, {"lightning2", "progs/bolt2.mdl"}, {"lightning3", "progs/bolt3.mdl"}, {"grapple", "progs/beam.mdl"}};
    const char *model = NULL;
    for (size_t i = 0; i < sizeof models / sizeof models[0]; i++) if (is(event->beam_style, models[i].style)) model = models[i].model;
    beam value = beam_base(source);
    value.has_actor = true; value.actor = event->actor; value.start = event->start; value.end = event->end;
    value.die = seconds + 0.2; value.width = 0; value.color = 0; value.model = model; value.family = BEAM_Q1;
    add_beam(e, value);
    return;
  }
  if (is(event->kind, "colored-explosion")) {
    qa_app_source_particles_q1_color_explosion(particles, event->origin, seconds, event->color_start, event->color_length);
    light(e, event->origin, seconds, 350, 0.5, white, 300, 0, NULL);
    admit_sound(e, content, "weapons/r_exp3.wav", event->origin, seconds);
    return;
  }
  const char *effect = event->effect;
  if (is(effect, "blood") || is(effect, "meat-spray")) qa_app_source_particles_q1_impact(particles, event->origin, zero, 73, event->amount, seconds);
  else if (is(effect, "gunshot")) qa_app_source_particles_q1_impact(particles, event->origin, zero, 0, 20, seconds);
  else if (is(effect, "spike") || is(effect, "superspike")) {
    qa_app_source_particles_q1_impact(particles, event->origin, zero, 0, is(effect, "spike") ? 10 : 20, seconds);
    if (fmod(random_integer(e), 5) != 0) admit_sound(e, content, "weapons/tink1.wav", event->origin, seconds);
    else {
      double ricochet = qa_js_bit_and(random_integer(e), 3);
      admit_sound(e, content, ricochet == 1 ? "weapons/ric1.wav" : ricochet == 2 ? "weapons/ric2.wav" : "weapons/ric3.wav", event->origin, seconds);
    }
  } else if (is(effect, "wizard-spike") || is(effect, "knight-spike")) {
    bool wizard = is(effect, "wizard-spike");
    qa_app_source_particles_q1_impact(particles, event->origin, zero, wizard ? 20 : 226, wizard ? 30 : 20, seconds);
    admit_sound(e, content, wizard ? "wizard/hit.wav" : "hknight/hit.wav", event->origin, seconds);
  } else if (is(effect, "explosion")) {
    qa_app_source_particles_q1_explosion(particles, event->origin, seconds, false);
    light(e, event->origin, seconds, 350, 0.5, white, 300, 0, NULL);
    admit_sound(e, content, "weapons/r_exp3.wav", event->origin, seconds);
  } else if (is(effect, "tar-explosion")) {
    qa_app_source_particles_q1_explosion(particles, event->origin, seconds, true);
    admit_sound(e, content, "weapons/r_exp3.wav", event->origin, seconds);
  } else if (is(effect, "lava-splash") || is(effect, "teleport")) {
    qa_app_source_particles_q1_splash(particles, event->origin, seconds, is(effect, "lava-splash"));
  } else if (is(effect, "muzzleflash")) {
    const pose *p = event->has_actor ? pose_of(e, event->actor) : NULL;
    qa_vec3 direction = p == NULL ? zero : qa_angles_to_axis(p->angles).vectors[0];
    qa_vec3 origin = !event->has_muzzle ? qa_add3(qa_add3(event->origin, (qa_vec3){0, 0, 16}), qa_scale3(direction, 18))
        : qa_add3(event->muzzle_origin, qa_scale3(qa_angles_to_axis(event->muzzle_angles).vectors[0], 18));
    double jitter = qa_js_bit_and(random_integer(e), 31);
    light(e, origin, seconds, 200 + jitter, 0.1, white, 0, 32, event->has_actor ? &event->actor : NULL);
  }
}
static void q2_weapon_event(effects_t *e, const event_scope *scope, const qa_q2w_event *event) {
  const retained *source = scope->source;
  if (event->kind != QA_Q2W_EVENT_BEAM && event->kind != QA_Q2W_EVENT_MUZZLEFLASH) return;
  group *g = group_of(e, source->content);
  if (!current(scope)) return;
  if (event->kind == QA_Q2W_EVENT_MUZZLEFLASH) {
    const pose *p = pose_of(e, *event->actor);
    if (p == NULL) { reject(e, source, "Q2 muzzle flash has no captured actor pose"); return; }
    qa_axis axis = qa_angles_to_axis(p->angles);
    qa_vec3 origin = qa_add3(qa_add3(p->origin, qa_scale3(axis.vectors[0], 18)), qa_scale3(axis.vectors[1], -16));
    if (!muzzle(e, origin, source->event.seconds, event->flash, event->silenced, *event->actor)) {
      char reason[160];
      snprintf(reason, sizeof reason, "Quake II player muzzle flash %s has no source definition", QA_JS_NUMBER(event->flash));
      reject(e, source, reason);
    }
    if (event->flash == 9 || event->flash == 10 || event->flash == 11)
      qa_app_source_particles_q2_respawn(g->particles, p->origin, source->event.seconds,
          event->flash == 9 ? "login" : event->flash == 10 ? "logout" : "respawn");
    return;
  }
  const char *effect = event->effect;
  if (is(effect, "rail") || is(effect, "rail-water")) qa_app_source_particles_q2_rail(g->particles, event->start, event->end, source->event.seconds);
  else if (is(effect, "bubble-trail")) qa_app_source_particles_q2_bubbles(g->particles, event->start, event->end, source->event.seconds);
  else if (is(effect, "bfg-laser") || is(effect, "bfg-zap") || is(effect, "bfg-lightning")) {
    beam value = beam_base(source);
    value.has_actor = event->actor != NULL; if (event->actor != NULL) value.actor = *event->actor;
    value.start = event->start; value.end = event->end;
    value.die = source->event.seconds + (event->duration > 0 ? event->duration : 0.1);
    value.width = 4;
    value.color = 0xd0 + qa_js_bit_and(random_integer(e), 3);
    value.model = is(effect, "bfg-lightning") ? QA_Q2_TRANSIENT_MODELS.lightning : NULL;
    value.family = BEAM_Q2;
    push_beam(e, value);
  } else reject(e, source, "Q2 heatbeam requires the source player-beam view and offset context");
}
static void set_tracker_pain(effects_t *e, qa_actor_id actor, qa_content_id content, double until) {
  ptrdiff_t index;
  FIND_ACTOR(e->tracker_pain, e->tracker_count, actor, index);
  if (index >= 0) { e->tracker_pain[index].content = content; e->tracker_pain[index].until = until; return; }
  GROW(e->tracker_pain, e->tracker_count, e->tracker_capacity);
  e->tracker_pain[e->tracker_count++] = (tracker_pain){actor, content, until};
}
static void q2_composition_event(effects_t *e, const event_scope *scope, const qa_q2_composition_event *event) {
  const retained *source = scope->source;
  switch (event->kind) {
    case QA_Q2_COMPOSITION_EVENT_MISSIONPACK_PLAYER: {
      const qa_q2mp_player_effect *effect = event->player_effect;
      if (effect->kind == QA_Q2MP_EFFECT_TRACKER_PAIN) set_tracker_pain(e, effect->actor, source->content, effect->until);
      else qa_app_q2_effect_views_receive(e->player_views, *effect);
      return;
    }
    case QA_Q2_COMPOSITION_EVENT_MISSIONPACK_ENTITY: {
      group *g = group_of(e, source->content);
      const qa_q2mp_entity_event *effect = event->entity_event;
      if (!current(scope)) return;
      if (effect->kind == QA_Q2MP_ENTITY_EVENT_FORCE_WALL)
        qa_app_source_particles_q2_force_wall(g->particles, effect->start, effect->end, effect->color, source->event.seconds);
      else if (effect->id == -1)
        qa_app_source_particles_q2_steam(g->particles, effect->origin, effect->direction, effect->color, effect->count, effect->speed,
                                         source->event.seconds, false);
      else if (e->steam_count < 32) {
        GROW(e->steam, e->steam_count, e->steam_capacity);
        e->steam[e->steam_count++] = (steam){source->content, *effect, source->event.seconds + effect->milliseconds / 1000,
                                             source->event.seconds};
      }
      return;
    }
    case QA_Q2_COMPOSITION_EVENT_CTF: case QA_Q2_COMPOSITION_EVENT_LMCTF: {
      const char *kind = event->kind == QA_Q2_COMPOSITION_EVENT_CTF ? event->ctf->kind : event->lmctf->kind;
      if (!is(kind, "grapple-cable")) return;
      qa_actor_id actor = event->kind == QA_Q2_COMPOSITION_EVENT_CTF ? event->ctf->actor : event->lmctf->actor;
      qa_vec3 start = event->kind == QA_Q2_COMPOSITION_EVENT_CTF ? event->ctf->start : event->lmctf->start;
      qa_vec3 offset = event->kind == QA_Q2_COMPOSITION_EVENT_CTF ? event->ctf->offset : event->lmctf->offset;
      qa_vec3 end = event->kind == QA_Q2_COMPOSITION_EVENT_CTF ? event->ctf->end : event->lmctf->end;
      group_of(e, source->content);
      if (!current(scope)) return;
      beam value = beam_base(source);
      value.has_actor = true; value.actor = actor; value.start = qa_add3(start, offset); value.end = end;
      value.die = source->event.seconds + 0.2; value.width = 0; value.color = 0; value.model = QA_Q2_TRANSIENT_MODELS.cable;
      value.family = BEAM_Q2;
      add_beam(e, value);
      return;
    }
    case QA_Q2_COMPOSITION_EVENT_KICK: case QA_Q2_COMPOSITION_EVENT_GRAPPLE_PREDICTION:
      reject(e, source, "Q2 session action reached the presentation owner");
      return;
  }
  qa_raise("Error", 0, 0, "Unknown Q2 composition event: %d", (int)event->kind);
}
static void q2_rerelease_event(effects_t *e, const retained *source, const qa_q2_rerelease_event *event) {
  if (is(event->kind, "flashlight")) {
    ptrdiff_t index;
    FIND_ACTOR(e->flashlights, e->flashlight_count, event->actor, index);
    if (event->enabled) {
      flashlight value = {event->actor, intern(e->strings, event->hand), source->event.has_owner, source->owner};
      if (index >= 0) e->flashlights[index] = value;
      else { GROW(e->flashlights, e->flashlight_count, e->flashlight_capacity); e->flashlights[e->flashlight_count++] = value; }
    } else if (index >= 0) REMOVE_AT(e->flashlights, e->flashlight_count, index);
  } else if (is(event->kind, "dynamic-light")) {
    ptrdiff_t index;
    FIND_ACTOR(e->source_lights, e->source_light_count, event->actor, index);
    if (event->visible) {
      source_light value; memset(&value, 0, sizeof value);
      value.actor = event->actor; value.has_owner = source->event.has_owner; value.owner = source->owner;
      value.light.origin = event->origin; value.light.radius = event->radius; value.light.color = event->light_color; value.light.minimum = 0;
      if (index >= 0) e->source_lights[index] = value;
      else { GROW(e->source_lights, e->source_light_count, e->source_light_capacity); e->source_lights[e->source_light_count++] = value; }
    } else if (index >= 0) REMOVE_AT(e->source_lights, e->source_light_count, index);
  }
}
static void q2_event(effects_t *e, const event_scope *scope, const qa_q2_presentation_event *event) {
  const retained *source = scope->source;
  if (is(event->kind, "effect")) { q2_effect(e, scope, event); return; }
  if (is(event->kind, "beam") || is(event->kind, "monster-beam")) {
    group_of(e, source->content);
    if (!current(scope)) return;
    bool plain = is(event->kind, "beam");
    if (plain && !event->visible) {
      size_t kept = 0;
      for (size_t i = 0; i < e->beam_count; i++)
        if (!(e->beams[i].has_actor && event->has_actor && qa_same_actor(e->beams[i].actor, event->actor))) e->beams[kept++] = e->beams[i];
      e->beam_count = kept;
      return;
    }
    beam value = beam_base(source);
    value.has_actor = event->has_actor; value.actor = event->actor; value.start = event->start; value.end = event->end;
    value.die = plain ? INFINITY : source->event.seconds + 0.2;
    value.width = plain ? event->width : 0;
    value.color = plain ? qa_js_bit_and(event->color, 255) : 0;
    value.model = plain ? NULL : QA_Q2_TRANSIENT_MODELS.parasite;
    value.family = BEAM_Q2;
    add_beam(e, value);
  } else if (is(event->kind, "monster-muzzleflash")) monster_muzzle(e, scope, event->origin, event->flash);
  else if (is(event->kind, "entity-event")) {
    if (event->event == 1 || event->event == 6 || event->event == 7) {
      const pose *p = pose_of(e, event->actor);
      if (p == NULL) { reject(e, source, "Q2 entity event has no captured actor pose"); return; }
      qa_vec3 origin = p->origin;
      group *g = group_of(e, source->content);
      if (!current(scope)) return;
      if (event->event == 1) qa_app_source_particles_q2_respawn(g->particles, origin, source->event.seconds, "item");
      else qa_app_source_particles_q2_teleport(g->particles, origin, source->event.seconds);
    }
  } else if (is(event->kind, "dynamic-light")) {
    shadow_light value; memset(&value, 0, sizeof value);
    value.state.actor = event->actor; value.state.origin = event->origin; value.state.color = event->light_color;
    value.state.visible = event->visible; value.state.radius = event->radius; value.state.intensity = event->intensity;
    value.state.resolution = event->resolution; value.state.fade_start = event->fade_start; value.state.fade_end = event->fade_end;
    value.state.lightstyle = event->lightstyle; value.state.has_cone = event->has_cone;
    value.state.cone_direction = event->cone_direction; value.state.cone_cos_half_angle = event->cone_cos_half_angle;
    value.has_owner = source->event.has_owner; value.owner = source->owner;
    ptrdiff_t index = -1;
    for (size_t i = 0; i < e->shadow_light_count; i++)
      if (qa_same_actor(e->shadow_lights[i].state.actor, event->actor)) { index = (ptrdiff_t)i; break; }
    if (index >= 0) e->shadow_lights[index] = value;
    else { GROW(e->shadow_lights, e->shadow_light_count, e->shadow_light_capacity); e->shadow_lights[e->shadow_light_count++] = value; }
  }
}
static void q3_character_event(effects_t *e, const event_scope *scope, const qa_app_sim_q3_character_presentation_event *event) {
  const retained *source = scope->source;
  const pose *p = pose_of(e, event->actor);
  if (p == NULL) { reject(e, source, "Q3 character event has no captured actor pose"); return; }
  qa_vec3 origin = p->origin;
  qa_app_q3_application_effects *effects = qa_app_application_effects_q3(e, source->content);
  if (effects == NULL) {
    effects = qa_app_q3_application_effects_create(e->assets, e->queries, source->content, false, read_hardware, e);
    if (!current(scope)) { qa_app_q3_application_effects_close(effects); qa_app_q3_application_effects_destroy(effects); return; }
    q3_set(e, source->content, effects);
  }
  if (!qa_app_q3_application_effects_event(effects, event, origin)) reject(e, source, "Q3 event requires the full cgame snapshot payload");
}
static void source_event(effects_t *e, const retained *source) {
  event_scope scope;
  scope.effects = e; scope.source = source;
  scope.key = owner_key(&source->event, true);
  const owner_revision *revision = revision_of(e, scope.key);
  scope.revision = revision == NULL ? 0 : revision->revision;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, scope.key);
  const char *kind = source->event.kind;
  if (!current(&scope)) {
  } else if (is(kind, "view-reset") || is(kind, "q2-player") || is(kind, "q1-level") || is(kind, "debug-graph")) {
  } else if (is(kind, "q3-source")) {
    const qa_app_q3_source_event *event = source->event.q3_source;
    if (event->kind == QA_APP_Q3_EVENT_SOUND) {
      qa_app_source_effect_sound sound; memset(&sound, 0, sizeof sound);
      sound.content = source->content; sound.path = intern(e->strings, event->path); sound.origin = event->origin; sound.channel = event->channel;
      sound.volume = event->volume; sound.seconds = source->event.seconds;
      sound.playback_kind = event->loop ? "loop" : "actor"; sound.actor = event->actor;
      if (event->loop) sound.velocity = event->velocity;
      qa_event_ring_admit(&e->sounds, &sound);
    }
    if (event->kind == QA_APP_Q3_EVENT_ENTITY)
      reject(e, source, "Native Q3 entity event requires its per-seat cgame snapshot and weapon presentation context");
  } else if (is(kind, "q2-composition")) q2_composition_event(e, &scope, source->event.q2_composition);
  else if (is(kind, "q2-rerelease")) q2_rerelease_event(e, source, source->event.q2_rerelease);
  else if (is(kind, "q3-character")) q3_character_event(e, &scope, &source->event.q3_character);
  else if (is(kind, "q1")) q1_event(e, &scope, source->event.q1);
  else if (is(kind, "q2-weapon")) q2_weapon_event(e, &scope, source->event.q2_weapon);
  else if (is(kind, "q2")) q2_event(e, &scope, source->event.q2);
  qa_cleanup_pop(&cleanup);
  free(scope.key);
}

/* ---- per-frame entity effects ---- */
static void q1_entities(effects_t *e, const qa_app_simulation_presentation *presentations, size_t count, double seconds, bool advance) {
  if (!advance) return;
  q1_trail *trails = NULL; size_t trail_count = 0, trail_capacity = 0;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, NULL);
  for (size_t i = 0; i < count; i++) {
    const qa_app_simulation_presentation *entity = &presentations[i];
    if (entity->family != QA_GAME_FAMILY_Q1 || entity->view_weapon || entity->path == NULL || entity->path[0] == '\0') continue;
    group *g = group_of(e, entity->content);
    qa_app_model_asset asset = qa_app_application_assets_model(e->assets, entity->content, entity->path);
    const qa_decoded_model *model = &asset.model;
    double flags = model->kind == QA_DECODED_MODEL_Q1_ALIAS ? model->as.q1_alias->flags
        : model->kind == QA_DECODED_MODEL_MD5 && is(model->as.md5->skin_selection.kind, "q1-mdl-replacement") ? model->as.md5->skin_selection.flags : 0;
    ptrdiff_t prior_index;
    FIND_ACTOR(e->q1_trails, e->q1_trail_count, entity->actor, prior_index);
    const q1_trail *prior = prior_index < 0 ? NULL : &e->q1_trails[prior_index];
    qa_vec3 delta = prior == NULL ? zero : qa_sub3(entity->origin, prior->origin);
    bool reset = prior == NULL || strcmp(prior->content, entity->content) != 0 || strcmp(prior->path, entity->path) != 0
        || fabs(delta.x) > 100 || fabs(delta.y) > 100 || fabs(delta.z) > 100;
    qa_vec3 start = reset || prior == NULL ? entity->origin : prior->origin;
    ptrdiff_t existing;
    FIND_ACTOR(trails, trail_count, entity->actor, existing);
    qa_content_id trail_content = intern(e->strings, entity->content);
    const char *trail_path = intern(e->strings, entity->path);
    q1_trail next = {entity->actor, trail_content, trail_path, entity->origin};
    if (existing >= 0) trails[existing] = next;
    else { GROW(trails, trail_count, trail_capacity); cleanup.arg = trails; trails[trail_count++] = next; }
    const qa_actor_id *actor = &entity->actor;
    double effects = entity->effects;
    if (advance && qa_js_bit_and(effects, 1) != 0) qa_app_source_particles_q1_entity(g->particles, entity->origin, seconds);
    if (qa_js_bit_and(effects, 2) != 0) {
      qa_vec3 forward = qa_angles_to_axis(entity->angles).vectors[0];
      qa_vec3 origin = qa_add3(qa_add3(entity->origin, (qa_vec3){0, 0, 16}), qa_scale3(forward, 18));
      double radius = 200 + qa_js_bit_and(random_integer(e), 31);
      light(e, origin, seconds, radius, 0.1, white, 0, 32, actor);
    }
    if (qa_js_bit_and(effects, 4) != 0) {
      qa_vec3 origin = qa_add3(entity->origin, (qa_vec3){0, 0, 16});
      light(e, origin, seconds, 400 + qa_js_bit_and(random_integer(e), 31), 0.001, white, 0, 0, actor);
    }
    if (qa_js_bit_and(effects, 8) != 0) light(e, entity->origin, seconds, 200 + qa_js_bit_and(random_integer(e), 31), 0.001, white, 0, 0, actor);
    if (is(expectation(e, entity->content)->edition, "rerelease")) {
      if (qa_js_bit_and(effects, 16) != 0)
        light(e, entity->origin, seconds, 200 + qa_js_bit_and(random_integer(e), 31), 0.001, (qa_vec3){0.25, 0.25, 1}, 0, 0, actor);
      if (qa_js_bit_and(effects, 32) != 0)
        light(e, entity->origin, seconds, 200 + qa_js_bit_and(random_integer(e), 31), 0.001, (qa_vec3){1, 0.25, 0.25}, 0, 0, actor);
      if (qa_js_bit_and(effects, 64) != 0) {
        double radius = 64 + qa_js_bit_and(random_integer(e), 31);
        light(e, entity->origin, seconds, radius, (double)(float)(seconds + 0.001) - seconds, (qa_vec3){1, 192.0 / 255, 120.0 / 255}, 0, 0, actor);
      }
    }
    double f = flags;
    double trail = qa_js_bit_and(f, 4) != 0 ? 2 : qa_js_bit_and(f, 32) != 0 ? 4 : qa_js_bit_and(f, 16) != 0 ? 3 : qa_js_bit_and(f, 64) != 0 ? 5
        : qa_js_bit_and(f, 1) != 0 ? 0 : qa_js_bit_and(f, 2) != 0 ? 1 : qa_js_bit_and(f, 128) != 0 ? 6 : -1;
    if (trail >= 0 && advance) qa_app_source_particles_q1_trail(g->particles, start, entity->origin, trail, seconds);
    if (trail == 0) light(e, entity->origin, seconds, 200, 0.01, white, 0, 0, actor);
  }
  qa_cleanup_pop(&cleanup);
  free(e->q1_trails);
  e->q1_trails = trails; e->q1_trail_count = trail_count;
}
static double tracker_until(const effects_t *e, qa_actor_id actor) {
  ptrdiff_t index;
  FIND_ACTOR(e->tracker_pain, e->tracker_count, actor, index);
  return index < 0 ? 0 : e->tracker_pain[index].until;
}
static void q2_entities(effects_t *e, const qa_app_simulation_presentation *presentations, size_t count, double seconds, bool advance) {
  entity_trail *trails = NULL; size_t trail_count = 0, trail_capacity = 0;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, NULL);
  for (size_t i = 0; i < count; i++) {
    const qa_app_simulation_presentation *entity = &presentations[i];
    if (entity->family != QA_GAME_FAMILY_Q2 || !entity->visible || entity->view_weapon) continue;
    ptrdiff_t prior_index;
    FIND_ACTOR(e->entity_trails, e->entity_trail_count, entity->actor, prior_index);
    const entity_trail *prior = prior_index < 0 ? NULL : &e->entity_trails[prior_index];
    qa_vec3 delta = prior == NULL ? zero : qa_sub3(prior->origin, entity->origin);
    bool reset = prior == NULL || strcmp(prior->content, entity->content) != 0
        || fabs(delta.x) > 512 || fabs(delta.y) > 512 || fabs(delta.z) > 512;
    qa_vec3 start = reset || prior == NULL ? entity->origin : prior->origin;
    double trail_count_value = reset || prior == NULL ? 1024 : prior->count;
    if (entity->effects != 0 || tracker_until(e, entity->actor) > seconds) {
      group *g = group_of(e, entity->content);
      qa_app_source_particles *p = g->particles;
      double flags = qa_js_bit_or(entity->effects, tracker_until(e, entity->actor) > seconds ? (double)0x80000000u : 0);
      qa_vec3 o = entity->origin;
#define LIGHT(radius_value, ...) push_sampled(e, (qa_surface_dynamic_light){o, (radius_value), 0, __VA_ARGS__})
      if (advance && qa_js_bit_and(flags, 0x00020000) != 0) qa_app_source_particles_q2_teleporter(p, o, seconds);
      /* CL_AddPacketEntities preserves this order; tracker overloads blaster/hyperblaster. */
      bool tracker = qa_js_bit_and(flags, 0x04000000) != 0;
      if (qa_js_bit_and(flags, 0x10) != 0) {
        if (advance) trail_count_value = qa_app_source_particles_q2_diminishing_trail(p, start, o, seconds, trail_count_value, "rocket");
        LIGHT(200, (qa_vec3){1, 1, 0});
      } else if (qa_js_bit_and(flags, 0x08) != 0) {
        if (advance) qa_app_source_particles_q2_blaster_trail(p, start, o, seconds, tracker);
        LIGHT(200, (qa_vec3){tracker ? 0 : 1, 1, 0});
      } else if (qa_js_bit_and(flags, 0x40) != 0) LIGHT(200, (qa_vec3){tracker ? 0 : 1, 1, 0});
      else if (qa_js_bit_and(flags, 0x02) != 0) {
        if (advance) trail_count_value = qa_app_source_particles_q2_diminishing_trail(p, start, o, seconds, trail_count_value, "blood");
      } else if (qa_js_bit_and(flags, 0x20) != 0) {
        if (advance) trail_count_value = qa_app_source_particles_q2_diminishing_trail(p, start, o, seconds, trail_count_value, "smoke");
      } else if (qa_js_bit_and(flags, 0x80) != 0) {
        static const double radii[] = {300, 400, 600, 300, 150, 75};
        double frame = entity->frame;
        double radius = qa_js_bit_and(flags, 0x2000) != 0 ? 200
            : frame >= 0 && frame < 6 && frame == floor(frame) ? radii[(size_t)frame] : 0;
        LIGHT(radius, (qa_vec3){0, 1, 0});
      } else if (qa_js_bit_and(flags, (double)0x80000000u) != 0) {
        if (tracker) LIGHT(50 + 500 * (sin(seconds * 2) + 1), (qa_vec3){-1, -1, -1});
        else { if (advance) qa_app_source_particles_q2_tracker_shell(p, start, seconds); LIGHT(155, (qa_vec3){-1, -1, -1}); }
      } else if (tracker) {
        if (advance) qa_app_source_particles_q2_tracker_trail(p, start, o, seconds);
        LIGHT(200, (qa_vec3){-1, -1, -1});
      } else if (qa_js_bit_and(flags, 0x00200000) != 0) {
        if (advance) trail_count_value = qa_app_source_particles_q2_diminishing_trail(p, start, o, seconds, trail_count_value, "green-blood");
      } else if (qa_js_bit_and(flags, 0x00400000) != 0) LIGHT(200, (qa_vec3){0, 0, 1});
      else if (qa_js_bit_and(flags, 0x01000000) != 0) {
        if (advance && qa_js_bit_and(flags, 0x2000) != 0) qa_app_source_particles_q2_blaster_trail(p, start, o, seconds, false);
        LIGHT(130, orange);
      }
#undef LIGHT
    }
    ptrdiff_t existing;
    FIND_ACTOR(trails, trail_count, entity->actor, existing);
    entity_trail next = {entity->actor, intern(e->strings, entity->content), entity->origin, trail_count_value};
    if (existing >= 0) trails[existing] = next;
    else { GROW(trails, trail_count, trail_capacity); cleanup.arg = trails; trails[trail_count++] = next; }
  }
  qa_cleanup_pop(&cleanup);
  free(e->entity_trails);
  e->entity_trails = trails; e->entity_trail_count = trail_count;
}
static void push_entities(qa_scene_entity **array, size_t *count, size_t *capacity, const qa_scene_entity *values, size_t value_count) {
  for (size_t i = 0; i < value_count; i++) { GROW(*array, *count, *capacity); (*array)[(*count)++] = values[i]; }
}
static qa_scene_entity *build_beam(effects_t *e, const beam *b, const qa_app_model_asset *asset, qa_vec3 start, double **rolls,
                                   size_t *roll_count, size_t *out_count) {
  qa_scene_entity *models = NULL; size_t count = 0, capacity = 0;
  qa_vec3 delta = qa_sub3(b->end, start);
  double length = qa_length3(delta);
  qa_vec3 direction = qa_normalize3_or_zero(delta);
  double horizontal = qa_js_hypot2(delta.x, delta.y);
  double yaw_angle = horizontal == 0 ? 0 : atan2(delta.y, delta.x) * 180 / M_PI;
  double pitch_angle = b->family == BEAM_Q1
      ? horizontal == 0 ? delta.z > 0 ? -90 : 90 : -atan2(delta.z, horizontal) * 180 / M_PI
      : horizontal == 0 ? delta.z > 0 ? 90 : 270 : -atan2(delta.z, horizontal) * 180 / M_PI;
  double yaw = b->family == BEAM_Q1 ? trunc(yaw_angle) : yaw_angle < 0 ? yaw_angle + 360 : yaw_angle;
  double pitch = b->family == BEAM_Q1 ? trunc(pitch_angle) : pitch_angle < 0 ? pitch_angle + 360 : pitch_angle;
  bool lightning = is(b->model, QA_Q2_TRANSIENT_MODELS.lightning);
  double model_length = lightning ? 35 : 30;
  double beam_length = lightning ? length - 20 : length;
  bool short_lightning = lightning && beam_length <= model_length;
  *out_count = 0;
  if (!isfinite(beam_length)) return NULL;
  double steps = short_lightning ? 1 : qa_js_min(MAX_BEAM_SEGMENTS, ceil(beam_length / model_length));
  double spacing = b->family == BEAM_Q1 ? 30 : steps > 1 ? (beam_length - model_length) / (steps - 1) : 0;
  for (double segment = 0; segment < steps; segment++) {
    qa_vec3 origin = short_lightning ? b->end : qa_add3(start, qa_scale3(direction, segment * spacing));
    size_t index = (size_t)segment;
    double roll;
    if (index < *roll_count) roll = (*rolls)[index];
    else {
      roll = fmod(random_integer(e), 360);
      *rolls = xrealloc(*rolls, (index + 1) * sizeof **rolls);
      (*rolls)[index] = roll; *roll_count = index + 1;
    }
    qa_vec3 angles = {lightning && !short_lightning ? -pitch : pitch, lightning && !short_lightning ? yaw + 180 : yaw, roll};
    qa_scene_entity entity = frame_entity(asset, origin, angles, 0, 0, 0, 0, 1, b->family == BEAM_Q1 ? "q1" : "q2", lightning ? 8 : 0);
    GROW(models, count, capacity);
    models[count++] = entity;
  }
  *out_count = count;
  return models;
}
static void beam_models_of(effects_t *e, const beam *b) {
  if (b->model == NULL) return;
  qa_app_model_asset asset = qa_app_application_assets_model(e->assets, b->content, b->model);
  group *g = group_of(e, b->content);
  double *rolls = NULL; size_t roll_count = 0;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, NULL);
  size_t remote_count = 0;
  qa_scene_entity *remote = build_beam(e, b, &asset, b->start, &rolls, &roll_count, &remote_count);
  cleanup.arg = rolls;
  const pose *p = b->family == BEAM_Q1 && b->has_actor ? pose_of(e, b->actor) : NULL;
  if (b->family == BEAM_Q2) {
    push_entities(&g->models, &g->model_count, &g->model_capacity, remote, remote_count);
    free(remote);
  } else {
    beam_models value; memset(&value, 0, sizeof value);
    value.actor = b->actor; value.remote = remote; value.remote_count = remote_count;
    if (p != NULL) { value.has_local = true; value.local = build_beam(e, b, &asset, p->origin, &rolls, &roll_count, &value.local_count); }
    GROW(g->beams, g->beam_count, g->beam_capacity);
    g->beams[g->beam_count++] = value;
  }
  qa_cleanup_pop(&cleanup);
  free(rolls);
}
static double explosion_age(double now, double start) { return (qa_js_round(now * 1000) - qa_js_round(start * 1000)) / 100; }
static void explosion_model(effects_t *e, const explosion *x, double now) {
  qa_app_model_asset asset = qa_app_application_assets_model(e->assets, x->content, x->path);
  group *g = group_of(e, x->content);
  double fraction = explosion_age(now, x->start), frame = qa_js_max(0, floor(fraction));
  double alpha = x->kind == EXPLOSION_FLASH ? 1 : x->kind == EXPLOSION_MISC ? 1 - fraction / (x->frames - 1) : (16 - frame) / 16;
  double skin = x->kind == EXPLOSION_POLY ? frame < 10 ? qa_js_shr(frame, 1) : frame < 13 ? 5 : 6 : x->skin;
  if (x->has_light) push_sampled(e, (qa_surface_dynamic_light){x->origin, x->light_radius * alpha, 0, x->light_color});
  double bits = qa_js_bit_or(x->flags, x->kind == EXPLOSION_POLY && frame >= 10 ? 32 : 0);
  qa_scene_entity entity = frame_entity(&asset, x->origin, x->angles, x->base_frame + frame + 1, x->base_frame + frame,
                                        1 - (fraction - frame), skin, alpha, "q2", bits);
  GROW(g->models, g->model_count, g->model_capacity);
  g->models[g->model_count++] = entity;
}

/* ---- prepare ---- */
typedef struct light_keep { double now; } light_keep;
static bool keep_light(const void *value, void *ctx) {
  const timed_light *l = value;
  double now = ((light_keep *)ctx)->now;
  return l->die >= now && l->radius - l->decay * (now - l->born) > 0;
}
typedef struct sample_lights { effects_t *effects; double now; } sample_lights;
static void sample_light(const void *value, size_t index, void *ctx) {
  (void)index;
  const timed_light *l = value;
  sample_lights *s = ctx;
  push_sampled(s->effects, (qa_surface_dynamic_light){l->origin, qa_js_max(0, l->radius - l->decay * (s->now - l->born)), l->minimum, l->color});
}
static void close_effects(effects_t *e);
static bool beam_actor_gone(const qa_world_snapshot *snapshot, const beam *b) {
  return b->has_actor && !live_actor(snapshot, b->actor);
}
static bool presented_q2(const qa_app_simulation_presentation *presentations, size_t count, qa_actor_id actor) {
  for (size_t i = 0; i < count; i++)
    if (presentations[i].family == QA_GAME_FAMILY_Q2 && qa_same_actor(presentations[i].actor, actor) && presentations[i].visible
        && !presentations[i].view_weapon) return true;
  return false;
}
static qa_model_source_options no_options(void *ctx, const qa_scene_entity *entity) {
  (void)ctx; (void)entity;
  qa_model_source_options options; memset(&options, 0, sizeof options);
  return options;
}
void qa_app_application_effects_prepare(qa_app_application_effects *e, const qa_world_snapshot *snapshot,
    const qa_app_simulation_presentation *presentations, size_t presentation_count,
    const qa_q3_character_view *characters, size_t character_count) {
  ensure_open(e);
  double now = snapshot->frame.time.kind == QA_SOURCE_TIME_SECONDS ? snapshot->frame.time.value : snapshot->frame.time.value / 1000;
  /* A server can send a lower time, as with a NetQuake svc_time. Effects hold still for that frame rather than ending the client. */
  double elapsed = !e->has_time || now < e->time ? 0 : now - e->time;
  bool advance = elapsed > 0 || !e->has_time;
  free(e->styles);
  e->style_count = snapshot->scene.light_style_count;
  e->styles = xcalloc(e->style_count, sizeof *e->styles);
  if (e->style_count > 0) memcpy(e->styles, snapshot->scene.light_styles, e->style_count * sizeof *e->styles);
  free(e->poses);
  e->poses = xcalloc(character_count + presentation_count + snapshot->body_count, sizeof *e->poses);
  e->pose_count = 0;
  for (size_t i = 0; i < character_count; i++) e->poses[e->pose_count++] = (pose){characters[i].actor, characters[i].origin, characters[i].angles};
  for (size_t i = 0; i < presentation_count; i++)
    if (!presentations[i].view_weapon) e->poses[e->pose_count++] = (pose){presentations[i].actor, presentations[i].origin, presentations[i].angles};
  for (size_t i = 0; i < snapshot->body_count; i++)
    e->poses[e->pose_count++] = (pose){snapshot->bodies[i].actor, snapshot->bodies[i].body.origin, snapshot->bodies[i].body.angles};
  for (size_t i = 0; i < e->group_count; i++) { e->groups[i]->model_count = 0; beams_clear(e->groups[i]); }
  size_t pending_count = 0;
  retained **pending = qa_event_ring_take(&e->pending, &e->pending_cursor, &pending_count);
  qa_event_ring_retire(&e->pending);
  for (size_t i = 0; i < pending_count; i++) {
    source_event(e, pending[i]);
    retained_free(pending[i]);
  }
  free(pending);
  for (size_t i = e->flashlight_count; i-- > 0;) if (!live_actor(snapshot, e->flashlights[i].actor)) REMOVE_AT(e->flashlights, e->flashlight_count, i);
  for (size_t i = e->shadow_light_count; i-- > 0;)
    if (!live_actor(snapshot, e->shadow_lights[i].state.actor)) REMOVE_AT(e->shadow_lights, e->shadow_light_count, i);
  for (size_t i = e->source_light_count; i-- > 0;) if (!live_actor(snapshot, e->source_lights[i].actor)) REMOVE_AT(e->source_lights, e->source_light_count, i);
  for (size_t i = e->tracker_count; i-- > 0;) if (!live_actor(snapshot, e->tracker_pain[i].actor)) REMOVE_AT(e->tracker_pain, e->tracker_count, i);
  {
    qa_actor_id *live = xcalloc(snapshot->actor_count, sizeof *live);
    for (size_t i = 0; i < snapshot->actor_count; i++) live[i] = snapshot->actors[i].id;
    qa_app_q2_effect_views_retain(e->player_views, live, snapshot->actor_count);
    free(live);
  }
  for (size_t i = e->bonus_count; i-- > 0;)
    if (!live_actor(snapshot, e->bonus_flashes[i].actor) || e->bonus_flashes[i].until <= now) REMOVE_AT(e->bonus_flashes, e->bonus_count, i);
  {
    size_t kept = 0;
    for (size_t i = 0; i < e->steam_count; i++) if (e->steam[i].end >= now) e->steam[kept++] = e->steam[i];
    e->steam_count = kept;
  }
  if (advance)
    for (size_t i = 0; i < e->steam_count; i++) {
      steam *s = &e->steam[i];
      if (s->next > now) continue;
      group *g = group_of(e, s->content);
      if (qa_app_source_particles_q2_steam(g->particles, s->event.origin, s->event.direction, s->event.color, s->event.count, s->event.speed, now, false))
        s->next += 0.1;
    }
  /* A Q2 laser beam has no end time: it goes away with its target entity. */
  {
    size_t kept = 0;
    for (size_t i = 0; i < e->beam_count; i++) {
      const beam *b = &e->beams[i];
      if (b->die >= now && (b->die != INFINITY || !beam_actor_gone(snapshot, b))) e->beams[kept++] = *b;
    }
    e->beam_count = kept;
    kept = 0;
    for (size_t i = 0; i < e->explosion_count; i++)
      if (floor(explosion_age(now, e->explosions[i].start)) < e->explosions[i].frames - 1) e->explosions[kept++] = e->explosions[i];
    e->explosion_count = kept;
  }
  light_keep keep = {now};
  qa_bounded_queue_retain(&e->lights, keep_light, &keep);
  if (!e->events_only) q1_entities(e, presentations, presentation_count, now, advance);
  e->sampled_light_count = 0;
  sample_lights sampler = {e, now};
  qa_bounded_queue_for_each(&e->lights, sample_light, &sampler);
  for (size_t i = 0; i < e->source_light_count; i++) push_sampled(e, e->source_lights[i].light);
  if (!e->events_only) q2_entities(e, presentations, presentation_count, now, advance);
  for (size_t i = 0; i < e->tracker_count;) {
    tracker_pain effect = e->tracker_pain[i];
    if (effect.until <= now) { REMOVE_AT(e->tracker_pain, e->tracker_count, i); continue; }
    i++;
    if (!e->events_only && presented_q2(presentations, presentation_count, effect.actor)) continue;
    const pose *p = pose_of(e, effect.actor);
    if (p != NULL) {
      qa_vec3 origin = p->origin;
      if (advance) qa_app_source_particles_q2_tracker_shell(group_of(e, effect.content)->particles, origin, now);
      push_sampled(e, (qa_surface_dynamic_light){origin, 155, 0, (qa_vec3){-1, -1, -1}});
    }
  }
  for (size_t i = 0; i < e->beam_count; i++) if (e->beams[i].model != NULL) beam_models_of(e, &e->beams[i]);
  for (size_t i = 0; i < e->explosion_count; i++) explosion_model(e, &e->explosions[i], now);
  for (size_t i = 0; i < e->group_count; i++) {
    group *g = e->groups[i];
    qa_scene_particle *q1 = NULL, *q2 = NULL; size_t q1_count = 0, q2_count = 0;
    qa_app_source_particles_sample(g->particles, now, elapsed, &q1, &q1_count, &q2, &q2_count);
    free(g->sampled);
    if (g->provider.family == QA_GAME_FAMILY_Q1) { g->sampled = q1; g->sampled_count = q1_count; free(q2); }
    else { g->sampled = q2; g->sampled_count = q2_count; free(q1); }
    qa_scene_entity *preload = NULL; size_t preload_count = 0, preload_capacity = 0;
    for (size_t s = 0; s < g->static_count; s++) push_entities(&preload, &preload_count, &preload_capacity, &g->statics[s].entity, 1);
    push_entities(&preload, &preload_count, &preload_capacity, g->models, g->model_count);
    for (size_t b = 0; b < g->beam_count; b++) {
      push_entities(&preload, &preload_count, &preload_capacity, g->beams[b].remote, g->beams[b].remote_count);
      if (g->beams[b].has_local) push_entities(&preload, &preload_count, &preload_capacity, g->beams[b].local, g->beams[b].local_count);
    }
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, preload);
    qa_scene_model_renderer_preload(g->renderer, preload, preload_count, no_options, NULL);
    qa_cleanup_pop(&cleanup);
    free(preload);
  }
  for (size_t i = 0; i < e->q3_count; i++) qa_app_q3_application_effects_prepare(e->q3[i].effects, trunc(now * 1000), trunc(elapsed * 1000));
  e->has_time = true; e->time = now;
  for (size_t i = 0; i < e->recipient_count;) {
    if (!live_actor(snapshot, e->recipients[i].actor)) {
      effects_t *gone = e->recipients[i].effects;
      REMOVE_AT(e->recipients, e->recipient_count, i);
      close_effects(gone);
      qa_app_application_effects_destroy(gone);
      continue;
    }
    qa_app_application_effects_prepare(e->recipients[i].effects, snapshot, presentations, presentation_count, characters, character_count);
    i++;
  }
}

/* ---- frame ---- */
static qa_vec3 palette_color(const group *g, double index) {
  const qa_palette *palette = g->provider.palette;
  if (palette == NULL) qa_raise("Error", 0, 0, "Indexed effects require their source palette");
  size_t offset = (size_t)(qa_js_to_int32(index) & 255) * 3;
  if (offset + 2 >= palette->colors.length) qa_raise("Error", 0, 0, "Incomplete effect palette");
  return (qa_vec3){palette->colors.bytes[offset], palette->colors.bytes[offset + 1], palette->colors.bytes[offset + 2]};
}
static qa_vec3 group_palette_color(void *ctx, double index) { return palette_color(ctx, index); }
static qa_vec4 project_point(void *ctx, qa_vec3 point) { return qa_scene_projector_project(*(const qa_scene_view_projector *)ctx, point); }
typedef struct sequence_storage { qa_legacy_particle_batch batch; qa_scene_group group; } sequence_storage;
static void release_sequence(void *value) {
  sequence_storage *s = value;
  qa_scene_group_destroy(&s->group);
  qa_legacy_particle_batch_destroy(&s->batch);
  free(s);
}
static void push_sequence(qa_app_effect_operations *list, qa_app_effect_storage *storage, qa_legacy_particle_batch batch) {
  sequence_storage *s = xcalloc(1, sizeof *s);
  s->batch = batch;
  s->group = qa_sequence_draw_group(QA_SEQUENCE_TRANSLUCENT, &s->batch.batch, 1);
  qa_app_effect_storage_add(storage, release_sequence, s);
  qa_app_effect_operation operation; memset(&operation, 0, sizeof operation);
  operation.scene.is_group = true; operation.scene.group = s->group;
  qa_app_effect_operations_push(list, operation);
}
static void release_world_operations(void *value) { qa_world_operations_destroy(value); free(value); }
static bool polygon(const qa_app_effect_operation *operation) {
  return !operation->is_model_group && operation->scene.is_group && operation->scene.group.order.kind == QA_GROUP_SOURCE
      && !operation->scene.group.order.source.entity.is_refentity;
}
void qa_app_application_effects_frame(qa_app_application_effects *e, qa_scene_camera camera, qa_source_scene_order *source,
    const qa_actor_id *viewer, const qa_scene_fog *q1_fog, qa_app_application_effect_frame *out) {
  ensure_open(e);
  memset(out, 0, sizeof *out);
  qa_source_time time = {QA_SOURCE_TIME_SECONDS, e->has_time ? e->time : 0};
  qa_scene_view_projector projector = qa_scene_create_view_projector(camera);
  qa_app_effect_operations prepared; memset(&prepared, 0, sizeof prepared);
  qa_q3_dynamic_light *q3_lights = NULL; size_t q3_count = 0, q3_capacity = 0;
  for (size_t i = 0; i < e->group_count; i++) {
    group *g = e->groups[i];
    qa_game_family family = g->provider.family;
    if (family != QA_GAME_FAMILY_Q3 && g->sampled_count > 0) {
      const family_image *image = NULL;
      for (size_t m = 0; m < e->image_count; m++) if (e->images[m].family == family) image = &e->images[m];
      if (image == NULL) qa_raise("Error", 0, 0, "Particles have no source texture");
      qa_particle_preparation_context context = {camera, family_name(family), g, group_palette_color};
      push_sequence(&prepared, &out->storage,
          qa_prepare_particle_batch(g->sampled, g->sampled_count, &context, image->image, project_point, &projector));
    }
    qa_scene_entity *models = NULL; size_t model_count = 0, model_capacity = 0;
    for (size_t s = 0; s < g->static_count; s++) push_entities(&models, &model_count, &model_capacity, &g->statics[s].entity, 1);
    push_entities(&models, &model_count, &model_capacity, g->models, g->model_count);
    for (size_t b = 0; b < g->beam_count; b++) {
      const beam_models *bm = &g->beams[b];
      bool local = viewer != NULL && qa_same_actor(*viewer, bm->actor) && bm->has_local;
      if (local) push_entities(&models, &model_count, &model_capacity, bm->local, bm->local_count);
      else push_entities(&models, &model_count, &model_capacity, bm->remote, bm->remote_count);
    }
    qa_app_effect_storage_add(&out->storage, free, models);
    qa_world_view_input input; memset(&input, 0, sizeof input);
    input.camera = camera; input.time = time; input.target_is_seat = false; input.target_preview_id = "effects";
    input.lights = e->sampled_lights; input.light_count = e->sampled_light_count; input.has_lights = true;
    size_t group_count = 0;
    qa_scene_model_group *groups = qa_scene_model_renderer_prepare(g->renderer, models, model_count, &input, NULL, NULL, NULL, &group_count);
    qa_app_effect_operations_push_model_groups(&prepared, &out->storage, groups, group_count);
  }
  for (size_t i = 0; i < e->beam_count; i++) {
    const beam *b = &e->beams[i];
    if (b->model != NULL) continue;
    group *g = group_find(e, b->content);
    if (g == NULL) qa_raise("Error", 0, 0, "Beam has no source palette");
    qa_vec3 color = palette_color(g, b->color);
    static const qa_render_state state = {{"src-alpha", "one-minus-src-alpha"}, "less-equal", false, "none", "none", {0, 1}, NULL};
    qa_legacy_particle_batch beam_batch = qa_q2_beam_batch(b->start, b->end, b->width, (qa_vec4){color.x, color.y, color.z, 76.5},
        project_point, &projector, g->provider.textures->white->image, state);
    push_sequence(&prepared, &out->storage, beam_batch);
  }
  /* The table only feeds static brush models. */
  double q1_styles[256];
  size_t q1_style_count = e->static_brush_count == 0 ? 0 : 256;
  for (size_t index = 0; index < q1_style_count; index++) {
    double value = 256;
    for (size_t s = 0; s < e->style_count; s++)
      if (!e->styles[s].is_q2 && e->styles[s].style == (double)index) { value = e->styles[s].value; break; }
    q1_styles[index] = value;
  }
  qa_app_effect_operations operations; memset(&operations, 0, sizeof operations);
  for (size_t i = 0; i < e->static_brush_count; i++) {
    const static_brush *brush = &e->static_brushes[i];
    qa_world_view_input input; memset(&input, 0, sizeof input);
    input.camera = camera; input.time = time; input.target_is_seat = false; input.target_preview_id = "effects";
    input.lights = e->sampled_lights; input.light_count = e->sampled_light_count;
    input.q1_styles = q1_styles; input.q1_style_count = q1_style_count;
    if (q1_fog != NULL) { input.has_q1_fog = true; input.q1_fog = *q1_fog; }
    input.has_animation_frame = true; input.animation_frame = brush->frame;
    qa_world_operations *result = xcalloc(1, sizeof *result);
    qa_world_scene_prepare_model(brush->scene, brush->model, brush->transform, &input, NULL, result);
    qa_app_effect_storage_add(&out->storage, release_world_operations, result);
    for (size_t o = 0; o < result->count; o++) {
      qa_app_effect_operation operation; memset(&operation, 0, sizeof operation);
      operation.scene = result->operations[o];
      qa_app_effect_operations_push(&operations, operation);
    }
  }
  for (size_t i = 0; i < prepared.count; i++) qa_app_effect_operations_push(&operations, prepared.items[i]);
  free(prepared.items);
  qa_surface_dynamic_light *source_lights = NULL; size_t source_count = 0, source_capacity = 0;
  const qa_q3_pres_scene_admission **admissions = NULL; size_t admission_count = 0, admission_capacity = 0;
  for (size_t i = 0; i < e->q3_count; i++) {
    qa_app_q3_effect_frame frame;
    qa_app_q3_application_effects_frame(e->q3[i].effects, camera, source, viewer, q1_fog, &frame);
    GROW(admissions, admission_count, admission_capacity);
    admissions[admission_count++] = frame.admission;
    for (size_t o = 0; o < frame.operations.count; o++) qa_app_effect_operations_push(&operations, frame.operations.items[o]);
    for (size_t l = 0; l < frame.q3_light_count; l++) { GROW(q3_lights, q3_count, q3_capacity); q3_lights[q3_count++] = frame.q3_lights[l]; }
    for (size_t l = 0; l < frame.q3_light_count; l++) {
      GROW(source_lights, source_count, source_capacity);
      source_lights[source_count++] = (qa_surface_dynamic_light){frame.q3_lights[l].origin, frame.q3_lights[l].radius, 0, frame.q3_lights[l].color};
    }
    /* The operations array moves into this frame's storage with its owned values. */
    for (size_t s = 0; s < frame.storage.count; s++)
      qa_app_effect_storage_add(&out->storage, frame.storage.entries[s].release, frame.storage.entries[s].value);
    free(frame.storage.entries);
    free(frame.operations.items);
  }
  for (size_t i = 0; i < e->sampled_light_count; i++) {
    GROW(q3_lights, q3_count, q3_capacity);
    q3_lights[q3_count++] = (qa_q3_dynamic_light){e->sampled_lights[i].origin, e->sampled_lights[i].radius, e->sampled_lights[i].color, false};
  }
  if (viewer != NULL) {
    ptrdiff_t index;
    FIND_ACTOR(e->recipients, e->recipient_count, *viewer, index);
    if (index >= 0) {
      qa_app_application_effect_frame local;
      qa_app_application_effects_frame(e->recipients[index].effects, camera, source, viewer, q1_fog, &local);
      for (size_t i = 0; i < local.q3_admission_count; i++) { GROW(admissions, admission_count, admission_capacity); admissions[admission_count++] = local.q3_admissions[i]; }
      for (size_t i = 0; i < local.operations.count; i++) qa_app_effect_operations_push(&operations, local.operations.items[i]);
      for (size_t i = 0; i < local.light_count; i++) { GROW(source_lights, source_count, source_capacity); source_lights[source_count++] = local.lights[i]; }
      for (size_t i = 0; i < local.q3_light_count; i++) { GROW(q3_lights, q3_count, q3_capacity); q3_lights[q3_count++] = local.q3_lights[i]; }
      for (size_t s = 0; s < local.storage.count; s++)
        qa_app_effect_storage_add(&out->storage, local.storage.entries[s].release, local.storage.entries[s].value);
      free(local.storage.entries);
      free(local.operations.items);
      free(local.q3_admissions);
      free(local.lights);
      free(local.q3_lights);
    }
  }
  qa_app_effect_operation *ordered = xcalloc(operations.count, sizeof *ordered);
  size_t ordered_count = 0;
  for (size_t i = 0; i < operations.count; i++) if (polygon(&operations.items[i])) ordered[ordered_count++] = operations.items[i];
  for (size_t i = 0; i < operations.count; i++) if (!polygon(&operations.items[i])) ordered[ordered_count++] = operations.items[i];
  free(operations.items);
  out->operations.items = ordered; out->operations.count = ordered_count; out->operations.capacity = ordered_count;
  out->q3_admissions = admissions; out->q3_admission_count = admission_count;
  out->light_count = e->sampled_light_count + source_count;
  out->lights = xcalloc(out->light_count, sizeof *out->lights);
  if (e->sampled_light_count > 0) memcpy(out->lights, e->sampled_lights, e->sampled_light_count * sizeof *out->lights);
  if (source_count > 0) memcpy(out->lights + e->sampled_light_count, source_lights, source_count * sizeof *out->lights);
  free(source_lights);
  out->q3_light_count = q3_count < 32 ? q3_count : 32;
  out->q3_lights = q3_lights;
}
void qa_app_application_effect_frame_destroy(qa_app_application_effect_frame *frame) {
  if (frame == NULL) return;
  qa_app_effect_storage_release(&frame->storage);
  free(frame->q3_admissions);
  free(frame->operations.items);
  free(frame->lights);
  free(frame->q3_lights);
  memset(frame, 0, sizeof *frame);
}

/* ---- shadow lights ---- */
qa_scene_light *qa_app_application_effects_shadow_scene_lights(qa_app_application_effects *e, qa_scene_camera camera,
    double (*style)(void *ctx, double index), void *style_ctx, const qa_actor_id *viewer, size_t *out_count) {
  qa_scene_light *flashlights = NULL; size_t flash_count = 0, flash_capacity = 0;
  for (size_t i = 0; i < e->flashlight_count; i++) {
    const flashlight *l = &e->flashlights[i];
    const pose *p = pose_of(e, l->actor);
    if (p == NULL) continue;
    bool local = viewer != NULL && qa_same_actor(*viewer, l->actor);
    qa_axis axis = local ? camera.axis : qa_angles_to_axis(p->angles);
    /* q2repro CL_AddPacketEntities per-pixel flashlight; scene axis[1] is left. */
    qa_vec3 origin = local ? qa_add3(camera.origin, qa_scale3(axis.vectors[1], is(l->hand, "center") ? 0 : is(l->hand, "left") ? 7 : -7)) : p->origin;
    qa_scene_light light_value; memset(&light_value, 0, sizeof light_value);
    light_value.origin = origin; light_value.color = white; light_value.radius = 512; light_value.additive = true;
    light_value.profile.kind = "q2"; light_value.profile.scale = 2; light_value.profile.has_cone = true;
    light_value.profile.cone_direction = axis.vectors[0]; light_value.profile.cone_cos_half_angle = cos(22 * M_PI / 180);
    light_value.profile.shadow_cast = true; light_value.profile.shadow_resolution = 512;
    GROW(flashlights, flash_count, flash_capacity);
    flashlights[flash_count++] = light_value;
  }
  qa_scene_light *result = NULL; size_t count = 0, capacity = 0;
  if (viewer != NULL) {
    ptrdiff_t index;
    FIND_ACTOR(e->recipients, e->recipient_count, *viewer, index);
    if (index >= 0) {
      size_t local_count = 0;
      qa_scene_light *local = qa_app_application_effects_shadow_scene_lights(e->recipients[index].effects, camera, style, style_ctx, viewer, &local_count);
      for (size_t i = 0; i < local_count; i++) { GROW(result, count, capacity); result[count++] = local[i]; }
      free(local);
    }
  }
  for (size_t i = 0; i < flash_count; i++) { GROW(result, count, capacity); result[count++] = flashlights[i]; }
  free(flashlights);
  for (size_t i = 0; i < e->shadow_light_count; i++) {
    const qa_q2_shadow_light_state *l = &e->shadow_lights[i].state;
    if (!l->visible || l->radius <= 0) continue;
    double fade = 1;
    if (!(l->fade_start <= 1 && l->fade_end <= 1) && l->fade_start <= l->fade_end) {
      double fraction = qa_js_min(1, qa_js_max(0, qa_length3(qa_sub3(l->origin, camera.origin)) / l->fade_end)), start = l->fade_start / l->fade_end;
      if (start <= 0) fade = fraction;
      else if (start < 1) {
        double value = qa_js_min(1, qa_js_max(0, (fraction - start) / (1 - start)));
        fade = 1 - value * value * (3 - 2 * value);
      } else fade = fraction < 1 ? 1 : 0;
    }
    if (fade <= 0) continue;
    qa_scene_light light_value; memset(&light_value, 0, sizeof light_value);
    light_value.origin = l->origin; light_value.color = l->color; light_value.radius = l->radius; light_value.additive = true;
    light_value.profile.kind = "q2";
    double scale = l->intensity * fade;
    light_value.profile.scale = scale * (l->lightstyle == -1 ? 1 : style(style_ctx, l->lightstyle));
    light_value.profile.has_cone = l->has_cone; light_value.profile.cone_direction = l->cone_direction;
    light_value.profile.cone_cos_half_angle = l->cone_cos_half_angle;
    light_value.profile.shadow_cast = true; light_value.profile.shadow_resolution = l->resolution;
    GROW(result, count, capacity);
    result[count++] = light_value;
  }
  *out_count = count;
  return result;
}

/* ---- reset / close ---- */
void qa_app_application_effects_reset_round(qa_app_application_effects *e) {
  ensure_open(e);
  for (size_t i = 0; i < e->recipient_count; i++) qa_app_application_effects_reset_round(e->recipients[i].effects);
  reset_pending(e); reset_unhandled(e); e->beam_count = 0; e->explosion_count = 0;
  e->static_brush_count = 0; free(e->styles); e->styles = NULL; e->style_count = 0;
  qa_bounded_queue_clear(&e->lights); e->sampled_light_count = 0;
  e->entity_trail_count = 0; e->q1_trail_count = 0; e->shadow_light_count = 0; e->source_light_count = 0; e->flashlight_count = 0;
  qa_app_q2_effect_views_clear(e->player_views); e->bonus_count = 0; e->tracker_count = 0; e->steam_count = 0;
  qa_event_ring_reset(&e->sounds);
  free(e->poses); e->poses = NULL; e->pose_count = 0; e->has_time = false; e->time = 0;
  for (size_t i = 0; i < e->group_count; i++) {
    group *g = e->groups[i];
    qa_app_source_particles_clear(g->particles); g->model_count = 0; g->static_count = 0; beams_clear(g);
    free(g->sampled); g->sampled = NULL; g->sampled_count = 0;
  }
  for (size_t i = 0; i < e->q3_count; i++) qa_app_q3_application_effects_reset_round(e->q3[i].effects);
}
static void close_effects(effects_t *e) {
  if (e->closed) return;
  for (size_t i = 0; i < e->recipient_count; i++) {
    close_effects(e->recipients[i].effects);
    qa_app_application_effects_destroy(e->recipients[i].effects);
  }
  e->recipient_count = 0;
  e->closed = true; reset_pending(e); reset_unhandled(e); e->beam_count = 0; e->explosion_count = 0;
  qa_bounded_queue_clear(&e->lights); e->sampled_light_count = 0;
  e->static_brush_count = 0; free(e->styles); e->styles = NULL; e->style_count = 0;
  for (size_t i = 0; i < e->q3_count; i++) {
    qa_app_q3_application_effects_close(e->q3[i].effects);
    qa_app_q3_application_effects_destroy(e->q3[i].effects);
  }
  for (size_t i = 0; i < e->image_count; i++) {
    qa_scene_image_registry_release(qa_app_application_assets_images(e->assets), e->images[i].image);
    qa_legacy_particle_image_destroy(e->images[i].content);
    free(e->images[i].content);
  }
  e->image_count = 0;
  for (size_t i = 0; i < e->group_count; i++) group_destroy(e->groups[i]);
  e->group_count = 0;
  for (size_t i = 0; i < e->prepared_count; i++) { qa_scene_model_renderer_destroy(e->prepared[i].renderer); free(e->prepared[i].renderer); }
  e->prepared_count = 0;
  e->q3_count = 0; e->entity_trail_count = 0; e->q1_trail_count = 0;
  e->shadow_light_count = 0; e->source_light_count = 0; e->flashlight_count = 0;
  qa_app_q2_effect_views_clear(e->player_views); e->bonus_count = 0; e->tracker_count = 0; e->steam_count = 0;
  qa_event_ring_reset(&e->sounds);
}
void qa_app_application_effects_close(qa_app_application_effects *e) { close_effects(e); }
void qa_app_application_effects_destroy(qa_app_application_effects *e) {
  if (e == NULL) return;
  close_effects(e);
  free(e->recipients);
  qa_app_sim_source_random_destroy(e->random);
  free(e->groups); free(e->prepared); free(e->images); free(e->q3);
  free(e->entity_trails); free(e->q1_trails);
  qa_event_ring_destroy(&e->pending); qa_event_ring_destroy(&e->unhandled); qa_event_ring_destroy(&e->sounds);
  for (size_t i = 0; i < e->retired_count; i++) free(e->retired_owners[i]);
  free(e->retired_owners);
  for (size_t i = 0; i < e->revision_count; i++) free(e->revisions[i].key);
  free(e->revisions);
  free(e->beams); free(e->explosions); free(e->static_brushes); free(e->styles);
  qa_bounded_queue_destroy(&e->lights);
  free(e->sampled_lights); free(e->flashlights); free(e->shadow_lights); free(e->source_lights);
  qa_app_q2_effect_views_destroy(e->player_views);
  free(e->bonus_flashes); free(e->tracker_pain); free(e->steam); free(e->poses);
  if (e->owns_strings) { strings_destroy(e->strings); free(e->strings); }
  free(e);
}

qa_scene_queries qa_app_application_effects_queries(const qa_app_application_effects *effects) { return effects->queries; }
