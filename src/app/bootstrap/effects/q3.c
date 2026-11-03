/* Port of src/app/bootstrap/effects/q3.ts: source cgame effect producers
 * joined to shared assets, collision and drawing. GPL-2.0-or-later. */
#include "qa/app/bootstrap/effects/q3.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/content.h"
#include "qa/audio/bank.h"
#include "qa/content/catalog/index.h"
#include "qa/content/q3/presentation/effects.h"
#include "qa/content/q3/presentation/local_entities.h"
#include "qa/content/q3/presentation/marks.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/content/q3/presentation/world_mark_projector.h"
#include "qa/core/game_numeric.h"
#include "qa/core/math.h"
#include "qa/foundation/event_ring.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/materials/evaluate.h"
#include "qa/movement/q3/constants.h"
#include "qa/render/scene/models/renderer.h"
#include "qa/render/scene/particles/primitives.h"
#include "qa/render/scene/view.h"

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
static char *xdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = xcalloc(length, 1);
  memcpy(copy, text, length);
  return copy;
}
#define GROW(array, count, capacity) do { \
    if ((count) == (capacity)) { \
      (capacity) = (capacity) ? (capacity) * 2 : 8; \
      (array) = xrealloc((array), (capacity) * sizeof *(array)); \
    } \
  } while (0)

/* ---- shared frame storage ---- */
void qa_app_effect_storage_add(qa_app_effect_storage *storage, void (*release)(void *value), void *value) {
  GROW(storage->entries, storage->count, storage->capacity);
  storage->entries[storage->count++] = (qa_app_effect_storage_entry){release, value};
}
void qa_app_effect_storage_release(qa_app_effect_storage *storage) {
  for (size_t i = storage->count; i-- > 0;) storage->entries[i].release(storage->entries[i].value);
  free(storage->entries);
  memset(storage, 0, sizeof *storage);
}
void qa_app_effect_operations_push(qa_app_effect_operations *list, qa_app_effect_operation operation) {
  GROW(list->items, list->count, list->capacity);
  list->items[list->count++] = operation;
}
typedef struct model_groups { qa_scene_model_group *groups; size_t count; } model_groups;
static void release_model_groups(void *value) {
  model_groups *groups = value;
  qa_scene_model_groups_destroy(groups->groups, groups->count);
  free(groups);
}
void qa_app_effect_operations_push_model_groups(qa_app_effect_operations *list, qa_app_effect_storage *storage,
    qa_scene_model_group *groups, size_t count) {
  model_groups *owned = xcalloc(1, sizeof *owned);
  owned->groups = groups; owned->count = count;
  qa_app_effect_storage_add(storage, release_model_groups, owned);
  for (size_t i = 0; i < count; i++) {
    qa_app_effect_operation operation; memset(&operation, 0, sizeof operation);
    operation.is_model_group = true; operation.model_group = &groups[i];
    qa_app_effect_operations_push(list, operation);
  }
}
void qa_app_q3_effect_frame_destroy(qa_app_q3_effect_frame *frame) {
  if (frame == NULL) return;
  qa_app_effect_storage_release(&frame->storage);
  free(frame->operations.items);
  memset(frame, 0, sizeof *frame);
}

/* ---- the effect owner ---- */
typedef struct sound_name { const qa_pcm_sound *pcm; char *path; } sound_name;
typedef struct shader_entry { char *name; qa_registered_scene_material material; } shader_entry;
typedef struct captured_ref { double entity_index; qa_q3_pres_ref_entity ref; double cull_radius; } captured_ref;
typedef struct model_entry { double entity_index; qa_scene_entity entity; qa_model_source_options options; } model_entry;
typedef struct owner_entry { const qa_q3_pres_ref_entity *ref; double radius; } owner_entry;

struct qa_app_q3_application_effects {
  qa_app_application_assets *assets;
  char *content;
  qa_scene_queries queries;
  qa_q3_effect_hardware hardware;
  qa_app_provider_scene_assets provider;
  qa_q3_pres_client_game_state state;
  qa_scene_model_renderer renderer;
  qa_sound_bank *bank;
  qa_event_ring sounds; bool has_cursor; qa_event_cursor cursor;
  sound_name *names; size_t name_count, name_capacity;
  shader_entry *shaders; size_t shader_count, shader_capacity;
  qa_q3_pres_scene_shader **scene_shaders; size_t scene_shader_count, scene_shader_capacity;
  char **model_paths; size_t model_path_count, model_path_capacity;
  bool preloading;
  qa_q3_pres_local_entity_pool *pool;
  qa_game_random random;
  qa_q3_pres_client_effects effects;
  qa_q3_pres_world_mark_projector projector;
  qa_q3_pres_impact_mark_system *marks;
  qa_q3_pres_local_entity_media local_media;
  qa_q3_pres_mission_local_entity_media mission_media;
  qa_q3_pres_local_entity_system system;
  qa_q3_pres_scene_shader smoke_puff;
  qa_q3_pres_fog_selection *fogs; size_t fog_count;
  captured_ref *refs; size_t ref_count, ref_capacity;
  qa_q3_pres_scene_admission admission;
  qa_q3_dynamic_light *lights; size_t light_count, light_capacity;
  model_entry *models; size_t model_count, model_capacity;
  owner_entry *owners; size_t owners_size; double owners_count;
};
typedef qa_app_q3_application_effects q3_t;

static const qa_render_state primitive_state = {{"one", "zero"}, "less-equal", true, "none", "back", {0, 1}, NULL};
static qa_numeric_profile effect_numeric(void) {
  qa_numeric_profile p = {"q3:effect", {QA_ARITHMETIC_BINARY32, NULL, 0, QA_FLOATING_ROUNDING_NEAREST_EVEN, false, false},
                          QA_FLOAT_TO_INT_QVM_INDEFINITE};
  return p;
}
/* ClientEffects keeps the hardware value; TS reads the getter each time. */
static void refresh_hardware(q3_t *e) {
  const char *hardware = qa_q3_effect_hardware_hardware(&e->hardware);
  e->effects.options.hardware = hardware != NULL && strcmp(hardware, "ragepro") == 0 ? QA_Q3_PRES_EFFECT_RAGEPRO : QA_Q3_PRES_EFFECT_GENERIC;
}
static qa_q3_hardware generic_hardware(void *ctx) { (void)ctx; return "generic"; }

/* create() closures */
static qa_pcm_sound *sound(q3_t *e, const char *path) {
  const qa_sound_asset *loaded = NULL;
  if (!qa_sound_bank_register(e->bank, path, QA_SOUND_FAMILY_Q3, &loaded)) qa_range_error("Out of memory");
  if (loaded == NULL) return NULL;
  for (size_t i = 0; i < e->name_count; i++)
    if (e->names[i].pcm == &loaded->pcm) { free(e->names[i].path); e->names[i].path = xdup(path); return (qa_pcm_sound *)&loaded->pcm; }
  GROW(e->names, e->name_count, e->name_capacity);
  e->names[e->name_count++] = (sound_name){&loaded->pcm, xdup(path)};
  return (qa_pcm_sound *)&loaded->pcm;
}
static const qa_q3_pres_scene_shader *shader(q3_t *e, const char *name) {
  if (e->preloading && qa_scene_shader_registry_has_cinematic(e->provider.shaders, name))
    qa_raise("Error", 0, 0, "Effect cinematic deferred until use: %s", name);
  qa_registered_scene_material material = qa_scene_shader_registry_register(e->provider.shaders, name, NULL);
  bool found = false;
  for (size_t i = 0; i < e->shader_count; i++)
    if (strcmp(e->shaders[i].name, name) == 0) { e->shaders[i].material = material; found = true; break; }
  if (!found) {
    GROW(e->shaders, e->shader_count, e->shader_capacity);
    e->shaders[e->shader_count++] = (shader_entry){xdup(name), material};
  }
  qa_q3_pres_scene_shader *value = xcalloc(1, sizeof *value);
  value->name = xdup(name);
  GROW(e->scene_shaders, e->scene_shader_count, e->scene_shader_capacity);
  e->scene_shaders[e->scene_shader_count++] = value;
  return value;
}
static qa_q3_pres_scene_model model(q3_t *e, const char *path) {
  qa_app_model_asset loaded = qa_app_application_assets_model(e->assets, e->content, path);
  if (e->preloading) {
    qa_model_source_options options; memset(&options, 0, sizeof options);
    qa_scene_model_renderer_preload_model(&e->renderer, &loaded.resource, &loaded.model, options, false);
  }
  GROW(e->model_paths, e->model_path_count, e->model_path_capacity);
  char *owned = xdup(path);
  e->model_paths[e->model_path_count++] = owned;
  qa_q3_pres_scene_model value; memset(&value, 0, sizeof value);
  value.kind = QA_Q3_PRES_SCENE_MODEL_LOADED; value.path = owned; value.model = loaded.model; value.resource = loaded.resource;
  return value;
}
static void source_sound(q3_t *e, qa_pcm_sound *pcm, qa_vec3 origin, double channel, double volume, const qa_actor_id *actor) {
  if (pcm == NULL) return;
  const char *path = NULL;
  for (size_t i = 0; i < e->name_count; i++) if (e->names[i].pcm == pcm) { path = e->names[i].path; break; }
  if (path == NULL) qa_raise("Error", 0, 0, "Q3 local sound lacks source registration");
  qa_app_source_effect_sound value; memset(&value, 0, sizeof value);
  value.content = e->content; value.path = path; value.origin = origin; value.channel = channel; value.volume = volume;
  value.seconds = e->state.time / 1000;
  value.playback_kind = actor == NULL ? "once" : "actor";
  if (actor != NULL) value.actor = *actor;
  qa_event_ring_admit(&e->sounds, &value);
}
static double random_integer(void *ctx) { return qa_game_random_rand(&((q3_t *)ctx)->random); }
static void effects_start_sound(void *ctx, qa_vec3 origin, double entity, double channel, qa_pcm_sound *pcm) {
  (void)entity;
  source_sound(ctx, pcm, origin, channel, 1, NULL);
}
static double mark_clock(void *ctx) { return ((q3_t *)ctx)->state.time; }
static bool mark_enabled(void *ctx) { (void)ctx; return true; }
static const qa_q3_pres_scene_shader *mark_energy_shader(void *ctx) { (void)ctx; return NULL; }
static qa_q3_pres_movement_trace prediction_trace(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds, double skip, double mask) {
  (void)skip;
  q3_t *e = ctx;
  qa_trace_query query; memset(&query, 0, sizeof query);
  query.start = start; query.end = end;
  query.shape.kind = "box"; query.shape.bounds = bounds;
  query.target.is_model = false;
  query.policy = (qa_trace_policy){"q3", NULL, false, 0, mask, NULL, true, true, false};
  query.numeric = effect_numeric();
  query.pass_actor = NULL;
  qa_trace_result trace = e->queries.trace(e->queries.ctx, query);
  if (trace.kind == NULL || strcmp(trace.kind, "q3") != 0) qa_raise("Error", 0, 0, "Selected map did not apply Q3 trace policy");
  qa_q3_pres_movement_trace out; memset(&out, 0, sizeof out);
  out.result.fraction = trace.fraction; out.result.end = trace.end;
  out.result.solidity = trace.all_solid ? "all-solid" : trace.start_solid ? "start-solid" : "clear";
  out.result.has_plane = trace.contact.has_plane; out.result.plane = trace.contact.plane;
  out.result.contents = trace.contents; out.result.surface_flags = trace.surface_flags;
  out.entity_num = trace.fraction < 1 ? 1022 : 1023;
  return out;
}
static double point_contents(void *ctx, qa_vec3 point, double model_index) {
  (void)model_index;
  q3_t *e = ctx;
  qa_point_contents_query query; memset(&query, 0, sizeof query);
  query.point = point; query.target.is_model = false;
  query.policy = (qa_trace_policy){"q3", NULL, false, 0, -1, NULL, true, true, false};
  query.numeric = effect_numeric(); query.pass_actor = NULL;
  qa_point_contents_result contents = e->queries.point_contents(e->queries.ctx, query);
  if (contents.kind == NULL || strcmp(contents.kind, "q3") != 0) qa_raise("Error", 0, 0, "Selected map did not apply Q3 content policy");
  return contents.contents;
}
static void audio_start_sound(void *ctx, qa_pcm_sound *pcm, qa_start_sound_options options) {
  if (options.origin.kind != QA_VOICE_ORIGIN_FIXED) qa_raise("Error", 0, 0, "World effect sound requires a fixed origin");
  source_sound(ctx, pcm, options.origin.position, options.channel, options.volume / 127, NULL);
}
static bool marks_impact(void *ctx, qa_q3_pres_impact_mark_request request, qa_q3_pres_ref_poly **out_polys, size_t *out_count) {
  return qa_q3_pres_impact_mark_system_impact_mark(((q3_t *)ctx)->marks, request, out_polys, out_count);
}

static void destroy_parts(q3_t *e) {
  if (e->marks != NULL) qa_q3_pres_impact_mark_system_destroy(e->marks);
  qa_q3_pres_world_mark_projector_destroy(&e->projector);
  if (e->pool != NULL) qa_q3_pres_local_entity_pool_destroy(e->pool);
  qa_q3_pres_client_game_state_destroy(&e->state);
  qa_scene_model_renderer_destroy(&e->renderer);
  if (e->bank != NULL) qa_sound_bank_destroy(e->bank);
  qa_event_ring_destroy(&e->sounds);
  for (size_t i = 0; i < e->name_count; i++) free(e->names[i].path);
  free(e->names);
  for (size_t i = 0; i < e->shader_count; i++) free(e->shaders[i].name);
  free(e->shaders);
  for (size_t i = 0; i < e->scene_shader_count; i++) { free((char *)e->scene_shaders[i]->name); free(e->scene_shaders[i]); }
  free(e->scene_shaders);
  for (size_t i = 0; i < e->model_path_count; i++) free(e->model_paths[i]);
  free(e->model_paths);
  free(e->fogs);
  free(e->refs);
  qa_q3_pres_scene_admission_destroy(&e->admission);
  free(e->lights);
  free(e->models);
  free(e->owners);
  free(e->content);
}
static void destroy_partial(void *value) { destroy_parts(value); free(value); }

qa_app_q3_application_effects *qa_app_q3_application_effects_create(qa_app_application_assets *assets,
    qa_scene_queries queries, qa_content_id content, bool preload_character,
    qa_q3_hardware_read_fn read_hardware, void *read_hardware_ctx) {
  q3_t *e = xcalloc(1, sizeof *e);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, destroy_partial, e);
  e->assets = assets; e->content = xdup(content); e->queries = queries;
  e->hardware = read_hardware == NULL ? qa_q3_effect_hardware_make(generic_hardware, NULL)
      : qa_q3_effect_hardware_make(read_hardware, read_hardware_ctx);
  e->provider = qa_app_application_assets_provider(assets, content);
  const qa_catalog_discovered_product *product_row = qa_content_catalog_product(
      qa_app_loaded_content_catalog(qa_app_application_assets_content(assets)), content);
  if (product_row == NULL) qa_raise("RangeError", 0, 0, "Unknown requested content or mod: %s", content);
  qa_q3_base_product product = product_row->expectation.campaign != NULL && strcmp(product_row->expectation.campaign, "missionpack") == 0
      ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
  qa_world_scene *world = qa_app_application_assets_world(assets);
  /* TS new SceneModelRenderer(provider, assets.world). */
  if (!qa_scene_model_renderer_init(&e->renderer, qa_app_application_assets_render_provider(assets, content), world))
    qa_range_error("Out of memory");
  e->bank = qa_sound_bank_create(e->provider.mounts);
  if (e->bank == NULL || !qa_event_ring_init(&e->sounds, 8192, sizeof(qa_app_source_effect_sound))) qa_range_error("Out of memory");
  e->preloading = preload_character;
  /* TS state = { time: 0, product, snap: null, predictedPlayerState: PlayerStateRecord(product, 0, 0, 0) }. */
  if (!qa_q3_pres_client_game_state_init(&e->state, product, 0, 0)) qa_range_error("Out of memory");
  e->state.time = 0; e->state.snap = NULL;
  qa_q3_pres_effect_media media; memset(&media, 0, sizeof media);
  media.water_bubble_shader = shader(e, "waterBubble");
  media.smoke_puff_rage_pro_shader = shader(e, "smokePuffRagePro");
  media.blood_explosion_shader = shader(e, "bloodExplosion");
  media.teleport_effect_model = model(e, product == QA_Q3_BASE_PRODUCT_BASEQ3 ? "models/misc/telep.md3" : "models/powerups/pop.md3");
  media.gib_skull = model(e, "models/gibs/skull.md3"); media.gib_brain = model(e, "models/gibs/brain.md3");
  media.gib_abdomen = model(e, "models/gibs/abdomen.md3"); media.gib_arm = model(e, "models/gibs/arm.md3");
  media.gib_chest = model(e, "models/gibs/chest.md3"); media.gib_fist = model(e, "models/gibs/fist.md3");
  media.gib_foot = model(e, "models/gibs/foot.md3"); media.gib_forearm = model(e, "models/gibs/forearm.md3");
  media.gib_intestine = model(e, "models/gibs/intestine.md3"); media.gib_leg = model(e, "models/gibs/leg.md3");
  media.smoke2 = model(e, "models/weapons2/shells/s_shell.md3");
  media.product = product;
  if (product == QA_Q3_BASE_PRODUCT_BASEQ3) media.teleport_effect_shader = shader(e, "teleportEffect");
  else {
    qa_q3_pres_mission_effect_media *m = &media.mission;
    m->lightning_shader = shader(e, "lightningBolt");
    m->kamikaze_effect_model = model(e, "models/weaphits/kamboom2.md3");
    m->dish_flash_model = model(e, "models/weaphits/boom01.md3");
    m->rocket_explosion_shader = shader(e, "rocketExplosion");
    m->obelisk_hit_sounds[0] = sound(e, "sound/items/obelisk_hit_01.wav");
    m->obelisk_hit_sounds[1] = sound(e, "sound/items/obelisk_hit_02.wav");
    m->obelisk_hit_sounds[2] = sound(e, "sound/items/obelisk_hit_03.wav");
    m->invulnerability_impact_model = model(e, "models/powerups/shield/impact.md3");
    m->invulnerability_impact_sounds[0] = sound(e, "sound/items/invul_impact_01.wav");
    m->invulnerability_impact_sounds[1] = sound(e, "sound/items/invul_impact_02.wav");
    m->invulnerability_impact_sounds[2] = sound(e, "sound/items/invul_impact_03.wav");
    m->invulnerability_juiced_model = model(e, "models/powerups/shield/juicer.md3");
    m->invulnerability_juiced_sound = sound(e, "sound/items/invul_juiced.wav");
  }
  shader(e, "smokePuff");
  if (!qa_q3_pres_local_entity_pool_create(&e->pool, product)) qa_range_error("Out of memory");
  qa_game_random_init(&e->random, 0);
  qa_q3_pres_effect_options options = {false, true, true, true, QA_Q3_PRES_EFFECT_GENERIC};
  qa_q3_pres_effect_imports imports = {e, random_integer, effects_start_sound};
  qa_q3_pres_client_effects_init(&e->effects, &e->state, e->pool, product, media, options, imports);
  refresh_hardware(e);
  qa_q3_pres_world_mark_projector_init(&e->projector, world);
  qa_q3_pres_impact_mark_options mark_options = {e, mark_clock, mark_enabled, mark_energy_shader};
  e->marks = qa_q3_pres_impact_mark_system_create(&e->projector.projector, mark_options);
  if (e->marks == NULL) qa_range_error("Out of memory");
  qa_q3_pres_local_entity_media *local = &e->mission_media.base;
  local->blood_trail_shader = shader(e, "bloodTrail");
  local->blood_mark_shader = shader(e, "bloodMark");
  local->burn_mark_shader = shader(e, "burnMark");
  static const char *const numbers[11] = {"gfx/2d/numbers/zero_32b", "gfx/2d/numbers/one_32b", "gfx/2d/numbers/two_32b",
    "gfx/2d/numbers/three_32b", "gfx/2d/numbers/four_32b", "gfx/2d/numbers/five_32b", "gfx/2d/numbers/six_32b",
    "gfx/2d/numbers/seven_32b", "gfx/2d/numbers/eight_32b", "gfx/2d/numbers/nine_32b", "gfx/2d/numbers/minus_32b"};
  for (size_t i = 0; i < 11; i++) local->number_shaders[i] = shader(e, numbers[i]);
  local->gib_bounce_sounds[0] = sound(e, "sound/player/gibimp1.wav");
  local->gib_bounce_sounds[1] = sound(e, "sound/player/gibimp2.wav");
  local->gib_bounce_sounds[2] = sound(e, "sound/player/gibimp3.wav");
  qa_q3_pres_local_entity_host host; memset(&host, 0, sizeof host);
  host.product = product;
  host.prediction_ctx = e; host.prediction_trace = prediction_trace;
  host.collision.ctx = e; host.collision.point_contents = point_contents;
  host.audio_ctx = e; host.audio_start_sound = audio_start_sound;
  host.client_num = -1;
  host.random = &e->random;
  host.marks = (qa_q3_pres_impact_mark_system_view){e, marks_impact};
  if (product == QA_Q3_BASE_PRODUCT_BASEQ3) {
    e->local_media = *local;
    host.media = &e->local_media;
  } else {
    e->mission_media.kamikaze_shock_wave = model(e, "models/weaphits/kamwave.md3");
    e->mission_media.kamikaze_explode_sound = sound(e, "sound/items/kam_explode.wav");
    e->mission_media.kamikaze_implode_sound = sound(e, "sound/items/kam_implode.wav");
    host.media = &e->mission_media.base; host.mission_media = &e->mission_media;
  }
  qa_q3_pres_local_entity_system_init(&e->system, qa_q3_pres_client_effects_as_view(&e->effects), host);
  e->smoke_puff.name = "smokePuff";
  e->fog_count = world->fog_selection_count;
  e->fogs = xcalloc(e->fog_count, sizeof *e->fogs);
  for (size_t i = 0; i < e->fog_count; i++) {
    e->fogs[i].index = world->fog_selections[i].index;
    e->fogs[i].bounds = world->fog_selections[i].volume.bounds;
  }
  if (!qa_q3_pres_snapshot_scene_admission(QA_Q3_PRES_SCENE_ORIGIN_MIXED, NULL, 0, NULL, 0, &e->admission))
    qa_range_error("Out of memory");
  e->owners_count = -1;
  e->preloading = false;
  qa_cleanup_pop(&cleanup);
  return e;
}
void qa_app_q3_application_effects_destroy(qa_app_q3_application_effects *e) {
  if (e == NULL) return;
  destroy_parts(e);
  free(e);
}
qa_content_id qa_app_q3_application_effects_content(const qa_app_q3_application_effects *e) { return e->content; }
double qa_app_q3_application_effects_time(const qa_app_q3_application_effects *e) { return e->state.time; }
qa_q3_base_product qa_app_q3_application_effects_product(const qa_app_q3_application_effects *e) { return e->state.product; }
qa_scene_model_renderer *qa_app_q3_application_effects_renderer(qa_app_q3_application_effects *e) { return &e->renderer; }
qa_q3_pres_local_entity_pool *qa_app_q3_application_effects_pool(qa_app_q3_application_effects *e) { return e->pool; }
bool qa_app_q3_application_effects_shader(const qa_app_q3_application_effects *e, const char *name, qa_registered_scene_material *out) {
  for (size_t i = 0; i < e->shader_count; i++) if (strcmp(e->shaders[i].name, name) == 0) { *out = e->shaders[i].material; return true; }
  return false;
}
size_t qa_app_q3_application_effects_shader_count(const qa_app_q3_application_effects *e) { return e->shader_count; }

bool qa_app_q3_application_effects_event(qa_app_q3_application_effects *e,
    const qa_app_sim_q3_character_presentation_event *event, qa_vec3 origin) {
  e->state.time = event->time_milliseconds;
  refresh_hardware(e);
  switch (qa_js_to_int32(event->event) & ~0x300) {
    case QA_Q3_EV_PLAYER_TELEPORT_IN: case QA_Q3_EV_PLAYER_TELEPORT_OUT:
      qa_q3_pres_client_effects_spawn_effect(&e->effects, origin);
      return true;
    case QA_Q3_EV_JUMP_PAD: {
      qa_q3_pres_smoke_puff_options puff; memset(&puff, 0, sizeof puff);
      puff.origin = origin; puff.velocity = (qa_vec3){0, 0, 1}; puff.radius = 32; puff.color = (qa_vec4){1, 1, 1, 0.33};
      puff.duration = 1000; puff.start_time = event->time_milliseconds; puff.fade_in_time = 0; puff.flags = 1;
      puff.shader = &e->smoke_puff;
      qa_q3_pres_client_effects_smoke_puff(&e->effects, puff);
      return true;
    }
    case QA_Q3_EV_GIB_PLAYER:
      qa_q3_pres_client_effects_gib_player(&e->effects, origin);
      return true;
    case QA_Q3_EV_NONE: case QA_Q3_EV_FOOTSTEP: case QA_Q3_EV_FOOTSTEP_METAL: case QA_Q3_EV_FOOTSPLASH:
    case QA_Q3_EV_FOOTWADE: case QA_Q3_EV_SWIM: case QA_Q3_EV_STEP_4: case QA_Q3_EV_STEP_8: case QA_Q3_EV_STEP_12:
    case QA_Q3_EV_STEP_16: case QA_Q3_EV_FALL_SHORT: case QA_Q3_EV_FALL_MEDIUM: case QA_Q3_EV_FALL_FAR:
    case QA_Q3_EV_JUMP: case QA_Q3_EV_WATER_TOUCH: case QA_Q3_EV_WATER_LEAVE: case QA_Q3_EV_WATER_UNDER:
    case QA_Q3_EV_WATER_CLEAR: case QA_Q3_EV_NOAMMO: case QA_Q3_EV_CHANGE_WEAPON: case QA_Q3_EV_PAIN:
    case QA_Q3_EV_DEATH1: case QA_Q3_EV_DEATH2: case QA_Q3_EV_DEATH3: case QA_Q3_EV_OBITUARY:
    case QA_Q3_EV_STOPLOOPINGSOUND: case QA_Q3_EV_TAUNT:
      return true;
    default:
      return false;
  }
}

/* ---- prepare ---- */
static const owner_entry *owner_of(q3_t *e, const qa_q3_pres_ref_entity *ref) {
  for (size_t i = 0; i < e->owners_size; i++) if (e->owners[i].ref == ref) return &e->owners[i];
  if (e->owners_count == qa_q3_pres_local_entity_pool_active_count(e->pool)) return NULL;
  qa_q3_pres_local_entity **active = NULL; size_t active_count = 0;
  if (!qa_q3_pres_local_entity_pool_active_entities(e->pool, &active, &active_count)) qa_range_error("Out of memory");
  free(e->owners);
  e->owners = xcalloc(active_count, sizeof *e->owners);
  e->owners_size = active_count;
  for (size_t i = 0; i < active_count; i++) e->owners[i] = (owner_entry){&active[i]->ref_entity, active[i]->radius};
  free(active);
  e->owners_count = qa_q3_pres_local_entity_pool_active_count(e->pool);
  for (size_t i = 0; i < e->owners_size; i++) if (e->owners[i].ref == ref) return &e->owners[i];
  return NULL;
}
static void add_ref_entity(void *ctx, const qa_q3_pres_ref_entity *ref) {
  q3_t *e = ctx;
  const owner_entry *owner = owner_of(e, ref);
  GROW(e->refs, e->ref_count, e->ref_capacity);
  e->refs[e->ref_count] = (captured_ref){(double)e->ref_count, qa_q3_pres_copy_ref_entity(ref), owner == NULL ? 0 : owner->radius};
  e->ref_count++;
}
static void add_light(void *ctx, qa_q3_pres_dynamic_light light) {
  q3_t *e = ctx;
  GROW(e->lights, e->light_count, e->light_capacity);
  e->lights[e->light_count++] = light;
}
typedef struct preload_options { const q3_t *effects; const qa_scene_entity *base; } preload_options;
/* TS entity => this.options.get(entity) ?? {}. */
static qa_model_source_options model_options(void *ctx, const qa_scene_entity *entity) {
  const preload_options *p = ctx;
  size_t index = (size_t)(entity - p->base);
  if (entity >= p->base && index < p->effects->model_count) return p->effects->models[index].options;
  qa_model_source_options none; memset(&none, 0, sizeof none);
  return none;
}
void qa_app_q3_application_effects_prepare(qa_app_q3_application_effects *e, double time_milliseconds, double elapsed_milliseconds) {
  e->state.time = time_milliseconds; e->ref_count = 0; e->light_count = 0; e->model_count = 0;
  refresh_hardware(e);
  /* Camera-near puff removal belongs to each seat; it must not retire another seat's effect. */
  qa_vec3 far = qa_app_application_assets_world(e->assets)->bounds.max;
  /* The pool list is walked once per frame, and again only when entities were allocated or freed while adding. */
  free(e->owners); e->owners = NULL; e->owners_size = 0; e->owners_count = -1;
  qa_q3_pres_local_entity_frame frame = {e->state.time, elapsed_milliseconds, {far.x + 65536, far.y + 65536, far.z + 65536}};
  qa_q3_pres_local_entity_scene_sink sink = {e, add_ref_entity, add_light};
  qa_q3_pres_local_entity_system_add_entities(&e->system, frame, sink);
  qa_q3_pres_admitted_ref_entity *entities = xcalloc(e->ref_count, sizeof *entities);
  for (size_t i = 0; i < e->ref_count; i++) { entities[i].is_poly_record = false; entities[i].entity = e->refs[i].ref; }
  qa_q3_pres_ref_poly *polys = NULL; size_t poly_count = 0;
  if (!qa_q3_pres_impact_mark_system_add_marks(e->marks, &polys, &poly_count)) qa_range_error("Out of memory");
  qa_q3_pres_admitted_poly *admitted = xcalloc(poly_count, sizeof *admitted);
  for (size_t i = 0; i < poly_count; i++)
    if (!qa_q3_pres_admit_poly(&polys[i], e->fogs, e->fog_count, &admitted[i])) qa_range_error("Out of memory");
  qa_q3_pres_scene_admission_destroy(&e->admission);
  if (!qa_q3_pres_snapshot_scene_admission(QA_Q3_PRES_SCENE_ORIGIN_MIXED, entities, e->ref_count, admitted, poly_count, &e->admission))
    qa_range_error("Out of memory");
  for (size_t i = 0; i < poly_count; i++) { qa_q3_pres_admitted_poly_destroy(&admitted[i]); qa_q3_pres_ref_poly_destroy(&polys[i]); }
  free(admitted); free(polys); free(entities);
  for (size_t i = 0; i < e->ref_count; i++) {
    const qa_q3_pres_ref_entity *ref = &e->refs[i].ref;
    if (ref->kind != QA_Q3_PRES_REF_MODEL || ref->as.model.model.kind != QA_Q3_PRES_SCENE_MODEL_LOADED) continue;
    const qa_q3_pres_ref_model_entity *m = &ref->as.model;
    model_entry entry; memset(&entry, 0, sizeof entry);
    entry.entity_index = e->refs[i].entity_index;
    entry.entity.actor = NULL; entry.entity.resource = m->model.resource; entry.entity.model = m->model.model;
    entry.entity.transform.origin = m->origin; entry.entity.transform.axis = m->axis; entry.entity.transform.scale = (qa_vec3){1, 1, 1};
    entry.entity.previous_origin = m->old_origin;
    entry.entity.pose.is_frame = true; entry.entity.pose.frame = m->frame; entry.entity.pose.previous_frame = m->old_frame;
    entry.entity.pose.back_lerp = m->back_lerp;
    entry.entity.skin = m->skin_num;
    qa_vec4 rgba = m->shading.shader_rgba;
    entry.entity.color = (qa_vec4){rgba.x / 255, rgba.y / 255, rgba.z / 255, rgba.w / 255};
    entry.entity.shader_time.kind = QA_SOURCE_TIME_SECONDS; entry.entity.shader_time.value = m->shading.shader_time;
    entry.entity.flags_kind = "q3"; entry.entity.flags_bits = m->shading.render_flags;
    entry.entity.lighting_origin = m->lighting_origin; entry.entity.shadow_plane = m->shadow_plane;
    entry.options.custom_shader = m->shading.custom_shader == NULL ? NULL : m->shading.custom_shader->name;
    GROW(e->models, e->model_count, e->model_capacity);
    e->models[e->model_count++] = entry;
  }
  qa_scene_entity *preload = xcalloc(e->model_count, sizeof *preload);
  for (size_t i = 0; i < e->model_count; i++) preload[i] = e->models[i].entity;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, preload);
  preload_options options = {e, preload};
  qa_scene_model_renderer_preload(&e->renderer, preload, e->model_count, model_options, &options);
  qa_cleanup_pop(&cleanup);
  free(preload);
}

/* ---- frame ---- */
static qa_vec4 project_point(void *ctx, qa_vec3 point) { return qa_scene_projector_project(*(const qa_scene_view_projector *)ctx, point); }
static const qa_fog_volume *world_fog(const qa_world_scene *world, const qa_q3_pres_fog_selection *fog) {
  if (fog == NULL) return NULL;
  for (size_t i = 0; i < world->fog_selection_count; i++) if (world->fog_selections[i].index == fog->index) return &world->fog_selections[i].volume;
  return NULL;
}
typedef struct source_group { qa_scene_group group; qa_draw_batch *batches; size_t batch_count; qa_world_material_context *context;
                              qa_entity_geometry geometry; qa_particle_draw_batch particle; bool has_particle; } source_group;
static void release_source_group(void *value) {
  source_group *s = value;
  qa_scene_group_destroy(&s->group);
  if (s->batches != NULL) qa_mat_draw_batches_destroy(s->batches, s->batch_count);
  if (s->has_particle) qa_particle_draw_batch_destroy(&s->particle);
  qa_entity_geometry_destroy(&s->geometry);
  free(s->context);
  free(s);
}
static void push_group(qa_app_q3_effect_frame *out, source_group *s) {
  qa_app_effect_storage_add(&out->storage, release_source_group, s);
  qa_app_effect_operation operation; memset(&operation, 0, sizeof operation);
  operation.scene.is_group = true; operation.scene.group = s->group;
  qa_app_effect_operations_push(&out->operations, operation);
}
static qa_material_geometry geometry_of(const qa_entity_geometry *geometry) {
  qa_material_geometry out = {geometry->vertices, geometry->vertex_count, geometry->indices, geometry->index_count};
  return out;
}
static bool registered(const q3_t *e, const char *name, qa_registered_scene_material *out) {
  return qa_app_q3_application_effects_shader(e, name, out);
}
typedef struct frame_source { qa_model_source_options options; } frame_source;
static qa_model_source_options frame_options(void *ctx, const qa_scene_entity *entity) {
  (void)entity;
  return ((frame_source *)ctx)->options;
}
void qa_app_q3_application_effects_frame(qa_app_q3_application_effects *e, qa_scene_camera camera,
    qa_source_scene_order *source, const qa_actor_id *viewer, const qa_scene_fog *q1_fog, qa_app_q3_effect_frame *out) {
  (void)viewer;
  memset(out, 0, sizeof *out);
  qa_world_scene *world = qa_app_application_assets_world(e->assets);
  qa_world_view_input input; memset(&input, 0, sizeof input);
  if (q1_fog != NULL) { input.has_q1_fog = true; input.q1_fog = *q1_fog; }
  input.camera = camera; input.time.kind = QA_SOURCE_TIME_MILLISECONDS; input.time.value = e->state.time;
  input.target_is_seat = false; input.target_preview_id = "effects";
  const qa_q3_pres_scene_admission *admission = &e->admission;
  double first_entity = qa_reserve_source_entity_range(source, (double)admission->entity_count);
  for (size_t index = 0; index < admission->polygon_count; index++) {
    const qa_q3_pres_admitted_poly *poly = &admission->polygons[index];
    if (poly->poly.shader == NULL) continue;
    qa_registered_scene_material material;
    if (!registered(e, poly->poly.shader->name, &material)) qa_raise("Error", 0, 0, "Unregistered mark shader %s", poly->poly.shader->name);
    source_group *s = xcalloc(1, sizeof *s);
    qa_poly_input_vertex *vertices = xcalloc(poly->poly.vertex_count, sizeof *vertices);
    for (size_t v = 0; v < poly->poly.vertex_count; v++)
      vertices[v] = (qa_poly_input_vertex){poly->poly.vertices[v].position, poly->poly.vertices[v].tex_coord, poly->poly.vertices[v].color};
    s->geometry = qa_poly_geometry(vertices, poly->poly.vertex_count);
    free(vertices);
    s->context = xcalloc(1, sizeof *s->context);
    qa_world_scene_material_context(world, &input, NULL, world_fog(world, poly->fog), s->context);
    s->batches = qa_prepare_material_batches(qa_registered_scene_material_compiled(material), geometry_of(&s->geometry),
                                             &s->context->context, &s->batch_count);
    qa_source_surface_order order = {source, {false, 0}, (double)index, poly->fog == NULL ? 0 : poly->fog->index + 1, 0};
    s->group = qa_source_draw_group(material, order, s->batches, s->batch_count);
    push_group(out, s);
  }
  qa_scene_view_projector projector = qa_scene_create_view_projector(camera);
  qa_renderer_image white = qa_model_render_provider_textures(&e->renderer.provider)->white->image;
  qa_source_scene_materials defaults = qa_scene_shader_registry_source_materials(e->provider.shaders);
  for (size_t c = 0; c < e->ref_count; c++) {
    const captured_ref *captured = &e->refs[c];
    const qa_q3_pres_ref_entity *ref = &captured->ref;
    double render_flags = ref->kind == QA_Q3_PRES_REF_PORTAL ? ref->as.portal.render_flags
        : ref->kind == QA_Q3_PRES_REF_MODEL ? ref->as.model.shading.render_flags
        : ref->kind == QA_Q3_PRES_REF_SPRITE ? ref->as.sprite.shading.render_flags
        : ref->kind == QA_Q3_PRES_REF_BEAM ? ref->as.beam.shading.render_flags : ref->as.rail.shading.render_flags;
    int32_t flags = qa_js_to_int32(render_flags);
    if (camera.clip_is_portal && (flags & 4) != 0) continue;
    if (ref->kind == QA_Q3_PRES_REF_PORTAL) continue;
    qa_source_entity_order *entity = xcalloc(1, sizeof *entity);
    entity->is_refentity = true; entity->index = first_entity + captured->entity_index;
    qa_app_effect_storage_add(&out->storage, free, entity);
    if (ref->kind == QA_Q3_PRES_REF_MODEL) {
      const qa_q3_pres_ref_model_entity *m = &ref->as.model;
      if (m->model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
        if (!camera.clip_is_portal && (flags & 2) != 0) continue;
        source_group *s = xcalloc(1, sizeof *s);
        qa_model_transform transform = {m->origin, m->axis, {1, 1, 1}};
        s->particle = qa_default_model_batch(transform, project_point, &projector, primitive_state, white);
        s->has_particle = true;
        qa_source_surface_order order = {source, *entity, 0, 0, 0};
        s->group = qa_source_draw_group(defaults.default_material, order, &s->particle.batch, 1);
        push_group(out, s);
        continue;
      }
      const model_entry *model = NULL;
      for (size_t i = 0; i < e->model_count; i++) if (e->models[i].entity_index == captured->entity_index) { model = &e->models[i]; break; }
      if (model == NULL) qa_raise("Error", 0, 0, "Admitted Q3 effect model lost its prepared descriptor");
      frame_source options = {model->options};
      options.options.has_shader_tex_coord = true; options.options.shader_tex_coord = m->shading.shader_tex_coord;
      options.options.has_source = true; options.options.source_view = source; options.options.source_entity = *entity;
      size_t group_count = 0;
      qa_scene_model_group *groups = qa_scene_model_renderer_prepare(&e->renderer, &model->entity, 1, &input, frame_options,
                                                                     &options, NULL, &group_count);
      qa_app_effect_operations_push_model_groups(&out->operations, &out->storage, groups, group_count);
      continue;
    }
    if (!camera.clip_is_portal && (flags & 2) != 0) continue;
    const qa_q3_pres_shading *shading = ref->kind == QA_Q3_PRES_REF_SPRITE ? &ref->as.sprite.shading
        : ref->kind == QA_Q3_PRES_REF_BEAM ? &ref->as.beam.shading : &ref->as.rail.shading;
    qa_vec3 origin = ref->kind == QA_Q3_PRES_REF_SPRITE ? ref->as.sprite.origin : ref->kind == QA_Q3_PRES_REF_BEAM ? ref->as.beam.origin : ref->as.rail.origin;
    double radius = ref->kind == QA_Q3_PRES_REF_SPRITE ? ref->as.sprite.radius : ref->kind == QA_Q3_PRES_REF_BEAM ? ref->as.beam.radius : ref->as.rail.radius;
    if (qa_length3(qa_sub3(origin, camera.origin)) < captured->cull_radius) continue;
    qa_registered_scene_material material;
    if (shading->custom_shader == NULL) material = defaults.default_material;
    else if (!registered(e, shading->custom_shader->name, &material))
      qa_raise("Error", 0, 0, "Unregistered effect shader %s", shading->custom_shader->name);
    const qa_q3_pres_fog_selection *fog = qa_q3_pres_procedural_fog(origin, radius, e->fogs, e->fog_count);
    double fog_index = fog == NULL ? 0 : fog->index + 1;
    source_group *s = xcalloc(1, sizeof *s);
    qa_source_surface_order order = {source, *entity, 0, fog_index, 0};
    if (ref->kind == QA_Q3_PRES_REF_BEAM) {
      qa_beam_pose pose = {ref->as.beam.origin, ref->as.beam.old_origin};
      s->particle = qa_beam_batch(pose, project_point, &projector, primitive_state, white);
      s->has_particle = true;
      s->group = qa_source_draw_group(material, order, &s->particle.batch, 1);
      push_group(out, s);
      continue;
    }
    if (ref->kind == QA_Q3_PRES_REF_SPRITE) {
      qa_sprite_pose pose = {ref->as.sprite.origin, ref->as.sprite.radius, ref->as.sprite.rotation, shading->shader_rgba};
      s->geometry = qa_sprite_geometry(pose, camera.axis, camera.clip_is_portal && camera.clip_mirror);
    } else {
      const char *kind = ref->kind == QA_Q3_PRES_REF_RAIL_CORE ? "rail-core" : ref->kind == QA_Q3_PRES_REF_RAIL_RINGS ? "rail-rings" : "lightning";
      qa_rail_pose pose = {kind, ref->as.rail.origin, ref->as.rail.old_origin, shading->shader_rgba};
      s->geometry = qa_rail_geometry(pose, camera.origin, NULL);
    }
    s->context = xcalloc(1, sizeof *s->context);
    qa_world_scene_material_context(world, &input, NULL, world_fog(world, fog), s->context);
    s->context->context.entity_rgba = shading->shader_rgba;
    s->context->context.shader_tex_coord = shading->shader_tex_coord;
    s->context->context.time_offset = shading->shader_time;
    s->batches = qa_prepare_material_batches(qa_registered_scene_material_compiled(material), geometry_of(&s->geometry),
                                             &s->context->context, &s->batch_count);
    s->group = qa_source_draw_group(material, order, s->batches, s->batch_count);
    push_group(out, s);
  }
  out->admission = admission;
  out->q3_lights = e->lights; out->q3_light_count = e->light_count;
}
qa_app_source_effect_sound *qa_app_q3_application_effects_drain_sounds(qa_app_q3_application_effects *e, size_t *out_count) {
  if (!e->has_cursor) { qa_event_ring_open_cursor(&e->sounds, &e->cursor, false); e->has_cursor = true; }
  size_t count = 0;
  qa_app_source_effect_sound *taken = qa_event_ring_take(&e->sounds, &e->cursor, &count);
  qa_event_ring_retire(&e->sounds);
  if (taken == NULL) taken = xcalloc(1, sizeof *taken);
  *out_count = count;
  return taken;
}
void qa_app_q3_application_effects_reset_round(qa_app_q3_application_effects *e) {
  qa_q3_pres_local_entity_pool_initialize(e->pool); qa_q3_pres_impact_mark_system_reset(e->marks);
  e->ref_count = 0;
  qa_q3_pres_scene_admission_destroy(&e->admission);
  if (!qa_q3_pres_snapshot_scene_admission(QA_Q3_PRES_SCENE_ORIGIN_MIXED, NULL, 0, NULL, 0, &e->admission))
    qa_range_error("Out of memory");
  e->light_count = 0;
  e->model_count = 0;
  qa_event_ring_reset(&e->sounds);
}
void qa_app_q3_application_effects_close(qa_app_q3_application_effects *e) { qa_app_q3_application_effects_reset_round(e); }
