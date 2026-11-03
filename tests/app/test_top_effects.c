/* C checks for src/app/bootstrap/effects.ts and effects/q3.ts over retail
 * content in ~/qfiles: tests/audio/q2-explosion-effects.test.ts,
 * tests/first-playable/effects-round-reset.test.ts, the flashlight case of
 * tests/first-playable/q2-shadow-light.test.ts, Q1 effect joins whose
 * random-stream results come from the TS module under bun
 * (QTS_TEST_CORPUS=~/qfiles), and Q2 shadow-light fade from the TS formula.
 * Every callback checks its sentinel ctx. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/effects.h"
#include "qa/app/bootstrap/effects/q3.h"
#include "qa/app/bootstrap/options.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/composition/q2/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/content/q3/presentation/local_entities.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/render/scene/view.h"
#include "qa/world/actors/registry.h"
#include "qa/world/collision/index.h"

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static bool near(double a, double b) { return fabs(a - b) < 1e-9; }

/* ---- retail fixture ---- */
typedef struct profile_state { int sentinel; } profile_state;
static profile_state profile_holder = {0x51};
static qa_finish_shader_profile assets_profile(void *ctx) {
  CHECK(ctx == &profile_holder && ((profile_state *)ctx)->sentinel == 0x51);
  return qa_default_shader_profile;
}
static double clock_ms;
static double assets_clock(void *ctx) { CHECK(ctx == &clock_ms); return clock_ms; }
typedef struct fixture {
  qa_app_application_command command;
  qa_app_loaded_content *content;
  qa_identity_owner ids;
  int owner_symbol;
  qa_app_application_assets *assets;
  qa_shared_scene_queries *queries;
  qa_session_actor_registry *actors;
} fixture;
/* As the TS hermetic preload: launches resolve their default content root
 * against QTS_TEST_CORPUS (~/qfiles). */
static const char *corpus_root(void) {
  const char *root = getenv("QTS_TEST_CORPUS");
  return root != NULL && root[0] != '\0' ? root : NULL;
}
static bool installed(const char *directory) {
  struct stat status;
  const char *corpus = corpus_root();
  if (corpus == NULL) { fprintf(stderr, "skip %s: QTS_TEST_CORPUS unset\n", directory); return false; }
  char root[512];
  snprintf(root, sizeof root, "%s/%s", corpus, directory);
  if (stat(root, &status) != 0) { fprintf(stderr, "skip: no installed %s content\n", directory); return false; }
  return true;
}
static bool open_fixture(fixture *f, const char *game, const char *map) {
  memset(f, 0, sizeof *f);
  const char *argv[] = {"--game", game, "--map", map, "--dedicated"};
  qa_content_root_search search = {corpus_root(), NULL};
  f->command = qa_app_parse_application_command(argv, 5, search, NULL);
  if (strcmp(f->command.kind, "run") != 0) return false;
  qa_app_content_host host; memset(&host, 0, sizeof host);
  /* The loaded content takes over the options (destroyed with it). */
  f->content = qa_app_load_application_content_native(host, f->command.options, NULL, false, NULL, NULL, NULL);
  memset(&f->command.options, 0, sizeof f->command.options);
  CHECK(f->content != NULL);
  if (f->content == NULL) return false;
  CHECK(qa_identity_owner_create(&f->ids, "effects"));
  qa_renderer_resource_owner owner = {&f->owner_symbol, f->ids.session, 0};
  qa_media_clock clock = {&clock_ms, assets_clock};
  qa_app_application_assets_options options; memset(&options, 0, sizeof options);
  options.shader_profile = assets_profile; options.shader_profile_ctx = &profile_holder;
  f->assets = qa_app_application_assets_create(f->content, owner, &clock, &options);
  CHECK(f->assets != NULL);
  qa_world_scene *world = qa_app_application_assets_load_world(f->assets);
  f->queries = qa_create_scene_queries(world->map);
  f->actors = qa_session_actor_registry_create(&f->ids, 64);
  return true;
}
static void close_fixture(fixture *f) {
  qa_session_actor_registry_destroy(f->actors);
  qa_shared_scene_queries_destroy(f->queries);
  qa_app_application_assets_destroy(f->assets);
  qa_identity_owner_destroy(&f->ids);
  qa_app_loaded_content_close(f->content);
  qa_app_loaded_content_destroy(f->content);
  qa_app_application_command_destroy(&f->command);
}
static qa_content_id map_content(fixture *f) { return qa_app_loaded_content_recipe(f->content)->map.entities.content; }
static qa_world_snapshot snapshot_at(fixture *f, double seconds) {
  qa_world_snapshot s; memset(&s, 0, sizeof s);
  s.session = f->ids.session;
  s.frame.frame = trunc(seconds * 10);
  s.frame.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, seconds};
  s.frame.elapsed = (qa_source_time){QA_SOURCE_TIME_SECONDS, 0.1};
  s.actors = qa_session_actor_registry_observations(f->actors, &s.actor_count);
  s.scene.session = f->ids.session;
  s.scene.time = s.frame.time;
  return s;
}
static qa_scene_camera camera_at(qa_vec3 origin) {
  qa_scene_camera camera; memset(&camera, 0, sizeof camera);
  camera.origin = origin; camera.axis = qa_angles_to_axis((qa_vec3){0, 0, 0});
  camera.projection = qa_scene_perspective_projection(90, 90, 4096, 4);
  camera.viewport = (qa_rect){0, 0, 64, 64};
  return camera;
}
static qa_app_sim_presentation_event event_of(const char *kind, qa_content_id content, double sequence, double seconds, const void *payload) {
  qa_app_sim_presentation_event event; memset(&event, 0, sizeof event);
  event.kind = kind; event.content = content; event.sequence = sequence; event.seconds = seconds;
  if (strcmp(kind, "q1") == 0) event.q1 = payload;
  else if (strcmp(kind, "q2") == 0) event.q2 = payload;
  else if (strcmp(kind, "q2-rerelease") == 0) event.q2_rerelease = payload;
  else if (strcmp(kind, "q3-source") == 0) event.q3_source = payload;
  else if (strcmp(kind, "q3-character") == 0) event.q3_character = *(const qa_app_sim_q3_character_presentation_event *)payload;
  return event;
}
typedef struct player_state { int sentinel; int calls; } player_state;
static player_state player_holder = {0x31, 0};
static bool is_player(void *ctx, qa_actor_id actor) {
  (void)actor;
  CHECK(ctx == &player_holder && ((player_state *)ctx)->sentinel == 0x31);
  ((player_state *)ctx)->calls++;
  return false;
}
typedef struct hardware_state { int sentinel; int reads; } hardware_state;
static hardware_state hardware_holder = {0x41, 0};
static qa_q3_hardware read_hardware(void *ctx) {
  CHECK(ctx == &hardware_holder && ((hardware_state *)ctx)->sentinel == 0x41);
  ((hardware_state *)ctx)->reads++;
  return "generic";
}
typedef struct style_state { int sentinel; int calls; double value; } style_state;
static style_state style_holder = {0x21, 0, 0.5};
static double light_style(void *ctx, double index) {
  CHECK(ctx == &style_holder && ((style_state *)ctx)->sentinel == 0x21);
  CHECK(index == 3);
  ((style_state *)ctx)->calls++;
  return ((style_state *)ctx)->value;
}
static size_t drain_sounds(qa_app_application_effects *effects, qa_app_source_effect_sound **out) {
  size_t count = 0;
  *out = qa_app_application_effects_drain_sounds(effects, &count);
  return count;
}
static size_t drain_unhandled(qa_app_application_effects *effects) {
  size_t count = 0;
  qa_app_unhandled_effect *rows = qa_app_application_effects_drain_unhandled(effects, &count);
  for (size_t i = 0; i < count; i++) fprintf(stderr, "  unhandled %s: %s\n", rows[i].source.kind, rows[i].reason);
  qa_app_unhandled_effects_free(rows, count);
  return count;
}

/* ---- Q1 joins: values from the TS module under bun ---- */
static void test_q1_effects(void) {
  if (!installed("Quake")) return;
  fixture f;
  if (!open_fixture(&f, "q1-classic-id1", "start")) return;
  qa_owned_actor actor = qa_session_actor_registry_allocate(f.actors, "q1:player", "q1:player/player");
  qa_app_application_effects *effects = qa_app_application_effects_create(f.assets, qa_shared_as_scene_queries(f.queries), is_player, &player_holder, 1);
  qa_content_id content = map_content(&f);
  qa_q1_event payloads[12]; memset(payloads, 0, sizeof payloads);
  qa_app_sim_presentation_event events[12];
  for (int i = 0; i < 8; i++) {
    payloads[i].kind = "effect"; payloads[i].effect = i % 2 ? "spike" : "superspike";
    payloads[i].origin = (qa_vec3){100 + i, 0, 0};
  }
  payloads[8].kind = "effect"; payloads[8].effect = "muzzleflash"; payloads[8].origin = (qa_vec3){10, 20, 30};
  payloads[8].has_actor = true; payloads[8].actor = actor.id;
  payloads[9].kind = "colored-explosion"; payloads[9].origin = (qa_vec3){1, 2, 3}; payloads[9].color_start = 32; payloads[9].color_length = 8;
  payloads[10].kind = "effect"; payloads[10].effect = "pickup"; payloads[10].has_actor = true; payloads[10].actor = actor.id;
  payloads[11].kind = "effect"; payloads[11].effect = "gunshot"; payloads[11].origin = (qa_vec3){5, 5, 5};
  for (int i = 0; i < 12; i++) events[i] = event_of("q1", content, i, 1, &payloads[i]);
  qa_app_application_effects_receive(effects, events, 12);
  /* The payload storage is the caller's; the effects retain their own copy until prepare. */
  memset(payloads, 0, sizeof payloads);
  qa_q3_character_view pose; memset(&pose, 0, sizeof pose);
  pose.actor = actor.id; pose.origin = (qa_vec3){10, 20, 30}; pose.angles = (qa_vec3){0, 90, 0};
  qa_world_snapshot snapshot = snapshot_at(&f, 1);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, &pose, 1);
  qa_app_source_effect_sound *sounds = NULL;
  size_t sound_count = drain_sounds(effects, &sounds);
  static const char *const expected[] = {"weapons/ric2.wav", "weapons/tink1.wav", "weapons/ric2.wav", "weapons/tink1.wav",
    "weapons/tink1.wav", "weapons/tink1.wav", "weapons/tink1.wav", "weapons/tink1.wav", "weapons/r_exp3.wav"};
  static const double expected_x[] = {100, 101, 102, 103, 104, 105, 106, 107, 1};
  CHECK(sound_count == 9);
  for (size_t i = 0; i < sound_count && i < 9; i++) {
    CHECK(strcmp(sounds[i].path, expected[i]) == 0 && sounds[i].origin.x == expected_x[i]);
    CHECK(strcmp(sounds[i].playback_kind, "once") == 0 && sounds[i].channel == 0 && sounds[i].volume == 1 && sounds[i].seconds == 1);
  }
  free(sounds);
  CHECK(drain_unhandled(effects) == 0);
  qa_source_scene_order order;
  qa_source_scene_order_init(&order, qa_app_application_assets_material_registrations(f.assets));
  qa_scene_camera camera = camera_at((qa_vec3){0, 0, 0});
  qa_app_application_effect_frame frame;
  qa_app_application_effects_frame(effects, camera, &order, NULL, NULL, &frame);
  CHECK(frame.light_count == 2);
  if (frame.light_count == 2) {
    CHECK(frame.lights[0].origin.x == 9.999999046325684 && frame.lights[0].origin.y == 38 && frame.lights[0].origin.z == 46);
    CHECK(frame.lights[0].radius == 215 && frame.lights[0].minimum == 32);
    CHECK(frame.lights[1].radius == 350 && frame.lights[1].origin.z == 3);
  }
  CHECK(frame.q3_light_count == 2 && frame.operations.count == 1);
  qa_app_application_effect_frame_destroy(&frame);
  qa_app_q2_effect_player_view view = qa_app_application_effects_player_view(effects, actor.id, camera);
  CHECK(view.has_blend && near(view.blend.x, 215.0 / 255) && near(view.blend.y, 186.0 / 255) && near(view.blend.z, 69.0 / 255));
  CHECK(near(view.blend.w, 50.0 / 255));
  snapshot = snapshot_at(&f, 1.25);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, NULL, 0);
  view = qa_app_application_effects_player_view(effects, actor.id, camera);
  CHECK(view.has_blend && near(view.blend.w, 25.0 / 255));
  qa_app_application_effects_frame(effects, camera, &order, NULL, NULL, &frame);
  CHECK(frame.light_count == 1 && frame.lights[0].radius == 275);
  qa_app_application_effect_frame_destroy(&frame);
  /* A retired owner drops its pending and retained presentation. */
  qa_presentation_owner owner = {"q1:official", 2};
  qa_q1_event explosion; memset(&explosion, 0, sizeof explosion);
  explosion.kind = "colored-explosion"; explosion.origin = (qa_vec3){4, 4, 4}; explosion.color_start = 32; explosion.color_length = 8;
  qa_app_sim_presentation_event owned = event_of("q1", content, 20, 1.5, &explosion);
  owned.has_owner = true; owned.owner = owner;
  qa_app_application_effects_receive(effects, &owned, 1);
  qa_app_sim_presentation_event retire = event_of("presentation-owner", content, 21, 1.5, NULL);
  retire.presentation_owner.kind = "retired"; retire.presentation_owner.owner = owner;
  qa_app_application_effects_receive(effects, &retire, 1);
  owned.sequence = 22;
  qa_app_application_effects_receive(effects, &owned, 1);
  snapshot = snapshot_at(&f, 1.5);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, NULL, 0);
  sound_count = drain_sounds(effects, &sounds);
  CHECK(sound_count == 0);
  free(sounds);
  CHECK(player_holder.calls == 0); /* TS isPlayer is retained, never read by the effects. */
  qa_app_application_effects_close(effects);
  qa_app_application_effects_destroy(effects);
  qa_session_actor_registry_release(f.actors, actor);
  close_fixture(&f);
}

/* ---- flashlight (tests/first-playable/q2-shadow-light.test.ts) and fade ---- */
static void reset_raises(void *ctx) { qa_app_application_effects_reset_round(ctx); }
/* Joins that never reach ApplicationAssets (no group, model or Q3 owner):
 * identity and actors only. */
static void open_light_fixture(fixture *f) {
  memset(f, 0, sizeof *f);
  CHECK(qa_identity_owner_create(&f->ids, "effects-lights"));
  f->actors = qa_session_actor_registry_create(&f->ids, 64);
}
static void close_light_fixture(fixture *f) {
  qa_session_actor_registry_destroy(f->actors);
  qa_identity_owner_destroy(&f->ids);
}
static qa_scene_queries no_queries(void) { qa_scene_queries q; memset(&q, 0, sizeof q); return q; }
static void test_flashlight_and_shadow_lights(void) {
  fixture f;
  open_light_fixture(&f);
  qa_owned_actor actor = qa_session_actor_registry_allocate(f.actors, "q3:character", "q3:character/sarge");
  qa_app_application_effects *effects = qa_app_application_effects_create(NULL, no_queries(), is_player, &player_holder, 1);
  qa_content_id content = "q1:classic:id1:installed";
  double sequence = 0;
  qa_scene_camera camera = camera_at((qa_vec3){40, 50, 60});
  qa_q3_character_view pose; memset(&pose, 0, sizeof pose);
  pose.actor = actor.id; pose.origin = (qa_vec3){10, 20, 30}; pose.angles = (qa_vec3){0, 90, 0};
  qa_q2_rerelease_event flash; memset(&flash, 0, sizeof flash);
  flash.kind = "flashlight"; flash.has_actor = true; flash.actor = actor.id;
#define EMIT(enabled_value, hand_value) do { \
    sequence++; flash.enabled = (enabled_value); flash.hand = (hand_value); \
    qa_app_sim_presentation_event e_ = event_of("q2-rerelease", content, sequence, sequence, &flash); \
    qa_app_application_effects_receive(effects, &e_, 1); \
    qa_world_snapshot s_ = snapshot_at(&f, sequence); \
    qa_app_application_effects_prepare(effects, &s_, NULL, 0, &pose, 1); \
  } while (0)
  size_t count = 0;
  qa_scene_light *lights;
  EMIT(true, "right");
  lights = qa_app_application_effects_shadow_scene_lights(effects, camera, light_style, &style_holder, &actor.id, &count);
  CHECK(count == 1 && lights[0].origin.x == 40 && lights[0].origin.y == 43 && lights[0].origin.z == 60);
  CHECK(count == 1 && lights[0].radius == 512 && lights[0].color.x == 1 && lights[0].profile.scale == 2);
  CHECK(count == 1 && lights[0].profile.has_cone && lights[0].profile.cone_cos_half_angle == cos(22 * M_PI / 180));
  CHECK(count == 1 && lights[0].profile.shadow_cast && lights[0].profile.shadow_resolution == 512);
  free(lights);
  lights = qa_app_application_effects_shadow_scene_lights(effects, camera, light_style, &style_holder, NULL, &count);
  CHECK(count == 1 && lights[0].origin.x == 10 && lights[0].origin.y == 20 && lights[0].origin.z == 30);
  free(lights);
  pose.origin = (qa_vec3){12, 22, 32};
  qa_world_snapshot snapshot = snapshot_at(&f, sequence);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, &pose, 1);
  lights = qa_app_application_effects_shadow_scene_lights(effects, camera, light_style, &style_holder, NULL, &count);
  CHECK(count == 1 && lights[0].origin.x == 12 && lights[0].origin.z == 32);
  free(lights);
  EMIT(true, "left");
  lights = qa_app_application_effects_shadow_scene_lights(effects, camera, light_style, &style_holder, &actor.id, &count);
  CHECK(count == 1 && lights[0].origin.y == 57);
  free(lights);
  EMIT(true, "center");
  lights = qa_app_application_effects_shadow_scene_lights(effects, camera, light_style, &style_holder, &actor.id, &count);
  CHECK(count == 1 && lights[0].origin.x == 40 && lights[0].origin.y == 50 && lights[0].origin.z == 60);
  free(lights);
  EMIT(false, "right");
  lights = qa_app_application_effects_shadow_scene_lights(effects, camera, light_style, &style_holder, NULL, &count);
  CHECK(count == 0); free(lights);
  EMIT(true, "right");
  qa_app_application_effects_reset_round(effects);
  lights = qa_app_application_effects_shadow_scene_lights(effects, camera, light_style, &style_holder, NULL, &count);
  CHECK(count == 0); free(lights);
  EMIT(true, "right");
  qa_session_actor_registry_release(f.actors, actor);
  qa_owned_actor next = qa_session_actor_registry_allocate(f.actors, "q3:character", "q3:character/sarge");
  pose.actor = next.id;
  snapshot = snapshot_at(&f, sequence);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, &pose, 1);
  lights = qa_app_application_effects_shadow_scene_lights(effects, camera, light_style, &style_holder, NULL, &count);
  CHECK(count == 0); free(lights);
  qa_app_source_effect_sound *sounds = NULL;
  CHECK(drain_sounds(effects, &sounds) == 0); free(sounds);
  /* Source shadow light: cone, fade and lightstyle scaling (TS shadowSceneLights). */
  qa_q2_presentation_event dynamic; memset(&dynamic, 0, sizeof dynamic);
  dynamic.kind = "dynamic-light"; dynamic.has_actor = true; dynamic.actor = next.id; dynamic.visible = true;
  dynamic.origin = (qa_vec3){0, 0, 64}; dynamic.light_color = (qa_vec3){1, 127.0 / 255, 0}; dynamic.radius = 192;
  dynamic.intensity = 2; dynamic.resolution = 128; dynamic.fade_start = 100; dynamic.fade_end = 200; dynamic.lightstyle = -1;
  dynamic.has_cone = true; dynamic.cone_direction = (qa_vec3){0, 0, -1}; dynamic.cone_cos_half_angle = cos(45 * M_PI / 180);
  sequence++;
  qa_app_sim_presentation_event light_event = event_of("q2", content, sequence, sequence, &dynamic);
  qa_app_application_effects_receive(effects, &light_event, 1);
  snapshot = snapshot_at(&f, sequence);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, &pose, 1);
  qa_scene_camera at = camera_at((qa_vec3){0, 0, 64});
  lights = qa_app_application_effects_shadow_scene_lights(effects, at, light_style, &style_holder, NULL, &count);
  CHECK(count == 1 && lights[0].profile.scale == 2 && lights[0].profile.shadow_resolution == 128 && lights[0].profile.cone_direction.z == -1);
  CHECK(count == 1 && lights[0].color.y == 127.0 / 255);
  free(lights);
  at.origin.x = 150;
  lights = qa_app_application_effects_shadow_scene_lights(effects, at, light_style, &style_holder, NULL, &count);
  CHECK(count == 1 && lights[0].profile.scale == 1); free(lights);
  at.origin.x = 200;
  lights = qa_app_application_effects_shadow_scene_lights(effects, at, light_style, &style_holder, NULL, &count);
  CHECK(count == 0); free(lights);
  CHECK(style_holder.calls == 0);
  dynamic.lightstyle = 3; sequence++;
  light_event = event_of("q2", content, sequence, sequence, &dynamic);
  qa_app_application_effects_receive(effects, &light_event, 1);
  snapshot = snapshot_at(&f, sequence);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, &pose, 1);
  at.origin.x = 0;
  lights = qa_app_application_effects_shadow_scene_lights(effects, at, light_style, &style_holder, NULL, &count);
  CHECK(count == 1 && lights[0].profile.scale == 1 && style_holder.calls == 1); free(lights);
  qa_app_application_effects_close(effects);
  qa_caught_error caught;
  CHECK(!qa_try(reset_raises, effects, &caught) && strcmp(caught.message, "Effect world is closed") == 0);
  qa_app_application_effects_destroy(effects);
  qa_session_actor_registry_release(f.actors, next);
  close_light_fixture(&f);
#undef EMIT
}

/* ---- event routing without assets: q3-source sounds, rejected joins,
 * recipients and presentation-owner lifecycle (TS receive/event/prepare). ---- */
static void test_routing_without_assets(void) {
  fixture f;
  open_light_fixture(&f);
  qa_content_id content = "q3:baseq3";
  qa_owned_actor seat = qa_session_actor_registry_allocate(f.actors, "q3:character", "q3:character/sarge");
  qa_owned_actor other = qa_session_actor_registry_allocate(f.actors, "q3:character", "q3:character/sarge");
  qa_app_application_effects *effects = qa_app_application_effects_create(NULL, no_queries(), is_player, &player_holder, 1);
  char path[64]; snprintf(path, sizeof path, "sound/weapons/rocket/rocklf1a.wav");
  qa_app_q3_source_event sound; memset(&sound, 0, sizeof sound);
  sound.kind = QA_APP_Q3_EVENT_SOUND; sound.actor = seat.id; sound.origin = (qa_vec3){1, 2, 3}; sound.velocity = (qa_vec3){4, 5, 6};
  sound.path = path; sound.channel = 3; sound.volume = 0.5; sound.loop = true;
  qa_app_q3_source_event entity; memset(&entity, 0, sizeof entity);
  entity.kind = QA_APP_Q3_EVENT_ENTITY;
  qa_q2_composition_event kick; memset(&kick, 0, sizeof kick);
  kick.kind = QA_Q2_COMPOSITION_EVENT_KICK; kick.actor = seat.id;
  qa_app_sim_q3_character_presentation_event lost = {other.id, 1, 500, QA_Q3_EV_JUMP_PAD, 0};
  qa_q2_rerelease_event light; memset(&light, 0, sizeof light);
  light.kind = "dynamic-light"; light.has_actor = true; light.actor = other.id; light.visible = true;
  light.origin = (qa_vec3){7, 8, 9}; light.radius = 64; light.light_color = (qa_vec3){0, 1, 0};
  qa_app_sim_presentation_event events[7] = {
    event_of("q3-source", content, 1, 0.5, &sound), event_of("q3-source", content, 2, 0.5, &entity),
    event_of("q3-character", content, 4, 0.5, &lost), event_of("q2-rerelease", content, 5, 0.5, &light),
    event_of("q3-source", content, 3, 0.5, &sound), /* an older sequence is dropped */
    event_of("q1-fog", content, 6, 0.5, NULL), event_of("q2-rerelease", content, 7, 0.5, &light)};
  events[1].sequence = 2;
  qa_q2_composition_event *kick_payload = &kick;
  qa_app_sim_presentation_event kick_event = event_of("q2-composition", content, 8, 0.5, NULL);
  kick_event.q2_composition = kick_payload;
  events[6].has_recipient = true; events[6].recipient = seat.id; events[6].sequence = 1;
  qa_app_application_effects_receive(effects, events, 7);
  qa_app_application_effects_receive(effects, &kick_event, 1);
  memset(path, 'x', sizeof path - 1); /* receive retained its own text */
  qa_world_snapshot snapshot = snapshot_at(&f, 0.5);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, NULL, 0);
  qa_app_source_effect_sound *sounds = NULL;
  size_t count = drain_sounds(effects, &sounds);
  CHECK(count == 1 && strcmp(sounds[0].path, "sound/weapons/rocket/rocklf1a.wav") == 0 && strcmp(sounds[0].playback_kind, "loop") == 0);
  CHECK(count == 1 && sounds[0].channel == 3 && sounds[0].volume == 0.5 && sounds[0].seconds == 0.5 && sounds[0].velocity.y == 5);
  CHECK(count == 1 && qa_same_actor(sounds[0].actor, seat.id) && strcmp(sounds[0].content, content) == 0);
  free(sounds);
  size_t rows = 0;
  qa_app_unhandled_effect *unhandled = qa_app_application_effects_drain_unhandled(effects, &rows);
  CHECK(rows == 3);
  if (rows == 3) {
    CHECK(strcmp(unhandled[0].reason, "Native Q3 entity event requires its per-seat cgame snapshot and weapon presentation context") == 0);
    CHECK(strcmp(unhandled[1].reason, "Q3 character event has no captured actor pose") == 0 && unhandled[1].source.q3_character.time_milliseconds == 500);
    CHECK(strcmp(unhandled[2].reason, "Q2 session action reached the presentation owner") == 0 && unhandled[2].source.sequence == 8);
  }
  qa_app_unhandled_effects_free(unhandled, rows);
  /* The shared light belongs to every seat; the recipient light only to its seat. */
  qa_source_scene_order order; qa_source_scene_order_init(&order, NULL);
  qa_app_application_effect_frame frame;
  qa_app_application_effects_frame(effects, camera_at((qa_vec3){0, 0, 0}), &order, &seat.id, NULL, &frame);
  CHECK(frame.light_count == 2 && frame.lights[0].radius == 64 && frame.lights[1].radius == 64 && frame.q3_light_count == 2);
  CHECK(frame.operations.count == 0 && frame.q3_admission_count == 0);
  qa_app_application_effect_frame_destroy(&frame);
  qa_app_application_effects_frame(effects, camera_at((qa_vec3){0, 0, 0}), &order, &other.id, NULL, &frame);
  CHECK(frame.light_count == 1);
  qa_app_application_effect_frame_destroy(&frame);
  /* An owner refresh keeps presentation; retiring it drops lights and later events. */
  qa_presentation_owner owner = {"q2:rerelease", 3};
  qa_q2_rerelease_event owned_light = light; owned_light.radius = 99;
  qa_app_sim_presentation_event owned = event_of("q2-rerelease", content, 9, 0.6, &owned_light);
  owned.has_owner = true; owned.owner = owner;
  qa_app_application_effects_receive(effects, &owned, 1);
  snapshot = snapshot_at(&f, 0.6);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, NULL, 0);
  qa_app_application_effects_frame(effects, camera_at((qa_vec3){0, 0, 0}), &order, NULL, NULL, &frame);
  CHECK(frame.light_count == 1 && frame.lights[0].radius == 99);
  qa_app_application_effect_frame_destroy(&frame);
  qa_app_sim_presentation_event lifecycle = event_of("presentation-owner", content, 10, 0.6, NULL);
  lifecycle.presentation_owner.kind = "refreshed"; lifecycle.presentation_owner.owner = owner;
  qa_app_application_effects_receive(effects, &lifecycle, 1);
  qa_app_application_effects_frame(effects, camera_at((qa_vec3){0, 0, 0}), &order, NULL, NULL, &frame);
  CHECK(frame.light_count == 1);
  qa_app_application_effect_frame_destroy(&frame);
  lifecycle.presentation_owner.kind = "retired"; lifecycle.sequence = 11;
  qa_app_application_effects_receive(effects, &lifecycle, 1);
  /* TS frame() samples the lights prepare() gathered; the retirement shows at the next prepare. */
  qa_app_application_effects_frame(effects, camera_at((qa_vec3){0, 0, 0}), &order, NULL, NULL, &frame);
  CHECK(frame.light_count == 1);
  qa_app_application_effect_frame_destroy(&frame);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, NULL, 0);
  qa_app_application_effects_frame(effects, camera_at((qa_vec3){0, 0, 0}), &order, NULL, NULL, &frame);
  CHECK(frame.light_count == 0);
  qa_app_application_effect_frame_destroy(&frame);
  owned.sequence = 12;
  qa_app_application_effects_receive(effects, &owned, 1);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, NULL, 0);
  qa_app_application_effects_frame(effects, camera_at((qa_vec3){0, 0, 0}), &order, NULL, NULL, &frame);
  CHECK(frame.light_count == 0);
  qa_app_application_effect_frame_destroy(&frame);
  /* The recipient effects retire with their seat actor. */
  qa_session_actor_registry_release(f.actors, seat);
  snapshot = snapshot_at(&f, 0.7);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, NULL, 0);
  qa_app_application_effects_frame(effects, camera_at((qa_vec3){0, 0, 0}), &order, &seat.id, NULL, &frame);
  CHECK(frame.light_count == 0);
  qa_app_application_effect_frame_destroy(&frame);
  qa_app_recipient_effect_sounds *recipient_sounds = qa_app_application_effects_drain_recipient_sounds(effects, &rows);
  CHECK(rows == 0); free(recipient_sounds);
  qa_app_application_effects_close(effects);
  qa_app_application_effects_destroy(effects);
  qa_session_actor_registry_release(f.actors, other);
  close_light_fixture(&f);
}

/* ---- tests/audio/q2-explosion-effects.test.ts ---- */
static void test_q2_explosions(void) {
  if (!installed("Quake 2")) return;
  fixture f;
  if (!open_fixture(&f, "q2-classic-baseq2", "base1")) return;
  qa_app_application_effects *effects = qa_app_application_effects_create(f.assets, qa_shared_as_scene_queries(f.queries), is_player, &player_holder, 1);
  size_t failure_count = 0;
  qa_app_effect_preload_failure *failed = qa_app_application_effects_preload_transient_resources(effects, &failure_count);
  for (size_t i = 0; i < failure_count; i++) fprintf(stderr, "  preload %s %s: %s\n", failed[i].content, failed[i].path, failed[i].error);
  CHECK(failure_count == 0);
  qa_app_effect_preload_failures_free(failed, failure_count);
  static const char *const names[][2] = {
    {"rocket-explosion", "rocklx1a.wav"}, {"rocket-explosion-water", "xpld_wat.wav"},
    {"grenade-explosion", "grenlx1a.wav"}, {"grenade-explosion-water", "xpld_wat.wav"},
    {"explosion1", "rocklx1a.wav"}, {"explosion2", "grenlx1a.wav"},
    {"plain-explosion", "rocklx1a.wav"}, {"bfg-explosion", NULL}};
  qa_content_id content = map_content(&f);
  for (size_t index = 0; index < 8; index++) {
    double seconds = (double)index + 1;
    char effect[64];
    snprintf(effect, sizeof effect, "q2:%s", names[index][0]);
    qa_q2_presentation_event payload; memset(&payload, 0, sizeof payload);
    payload.kind = "effect"; payload.effect = effect; payload.origin = (qa_vec3){8192 + (double)index, -4096, 384};
    payload.direction = (qa_vec3){0, 0, 1};
    qa_app_sim_presentation_event event = event_of("q2", content, (double)index, seconds, &payload);
    qa_world_snapshot snapshot = snapshot_at(&f, seconds);
    qa_app_application_effects_receive(effects, &event, 1);
    qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, NULL, 0);
    CHECK(drain_unhandled(effects) == 0);
    qa_app_source_effect_sound *sounds = NULL;
    size_t count = drain_sounds(effects, &sounds);
    if (names[index][1] == NULL) CHECK(count == 0);
    else {
      char path[64];
      snprintf(path, sizeof path, "weapons/%s", names[index][1]);
      CHECK(count == 1 && strcmp(sounds[0].path, path) == 0 && strcmp(sounds[0].content, content) == 0);
      CHECK(count == 1 && sounds[0].origin.x == 8192 + (double)index && sounds[0].origin.y == -4096 && sounds[0].origin.z == 384);
      CHECK(count == 1 && sounds[0].channel == 0 && sounds[0].volume == 1 && sounds[0].seconds == seconds && strcmp(sounds[0].playback_kind, "once") == 0);
    }
    free(sounds);
    qa_app_application_effects_receive(effects, &event, 1);
    qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, NULL, 0);
    count = drain_sounds(effects, &sounds);
    CHECK(count == 0);
    free(sounds);
  }
  /* Unresolved effects are reported with their source text. */
  qa_q2_presentation_event unknown; memset(&unknown, 0, sizeof unknown);
  unknown.kind = "effect"; unknown.effect = "q2:mystery_effect";
  qa_app_sim_presentation_event event = event_of("q2", content, 100, 10, &unknown);
  qa_app_application_effects_receive(effects, &event, 1);
  qa_world_snapshot snapshot = snapshot_at(&f, 10);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, NULL, 0);
  size_t count = 0;
  qa_app_unhandled_effect *rows = qa_app_application_effects_drain_unhandled(effects, &count);
  CHECK(count == 1 && strcmp(rows[0].reason, "Unresolved Quake II source effect q2:mystery_effect") == 0);
  CHECK(count == 1 && rows[0].source.sequence == 100 && strcmp(rows[0].source.q2->effect, "q2:mystery_effect") == 0);
  qa_app_unhandled_effects_free(rows, count);
  qa_app_application_effects_close(effects);
  qa_app_application_effects_destroy(effects);
  close_fixture(&f);
}

/* ---- tests/first-playable/effects-round-reset.test.ts ---- */
static qa_q3_character_view character_view(qa_actor_id actor) {
  qa_q3_character_view view; memset(&view, 0, sizeof view);
  view.actor = actor; view.origin = (qa_vec3){80, 0, 0}; view.color = (qa_vec4){1, 1, 1, 1};
  return view;
}
static void test_q3_round_reset(void) {
  if (!installed("Quake 3 Arena")) return;
  fixture f;
  if (!open_fixture(&f, "q3-baseq3", "q3dm1")) return;
  qa_app_application_effects *effects = qa_app_application_effects_create(f.assets, qa_shared_as_scene_queries(f.queries), is_player, &player_holder, 1);
  qa_app_application_effects_bind_renderer_hardware(effects, read_hardware, &hardware_holder);
  qa_content_id source = map_content(&f);
  qa_owned_actor old = qa_session_actor_registry_allocate(f.actors, "q3:character", "q3:character/sarge");
  qa_app_sim_q3_character_presentation_event jump = {old.id, 1, 1000, QA_Q3_EV_JUMP_PAD, 0};
  qa_q2_rerelease_event dynamic; memset(&dynamic, 0, sizeof dynamic);
  dynamic.kind = "dynamic-light"; dynamic.has_actor = true; dynamic.actor = old.id; dynamic.origin = (qa_vec3){80, 0, 0};
  dynamic.radius = 200; dynamic.light_color = (qa_vec3){1, 0, 0}; dynamic.visible = true;
  qa_app_sim_presentation_event pair[2] = {event_of("q3-character", source, 1, 1, &jump), event_of("q2-rerelease", source, 2, 1, &dynamic)};
  qa_app_application_effects_receive(effects, pair, 2);
  qa_q3_character_view view = character_view(old.id);
  qa_world_snapshot snapshot = snapshot_at(&f, 1);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, &view, 1);
  qa_app_q3_application_effects *instance = qa_app_application_effects_q3(effects, source);
  CHECK(instance != NULL);
  if (instance == NULL) { qa_app_application_effects_destroy(effects); close_fixture(&f); return; }
  CHECK(hardware_holder.reads > 0);
  qa_scene_model_renderer *renderer = qa_app_q3_application_effects_renderer(instance);
  size_t shader_count = qa_app_q3_application_effects_shader_count(instance);
  qa_world_scene *world = qa_app_application_assets_world(f.assets);
  CHECK(qa_q3_pres_local_entity_pool_active_count(qa_app_q3_application_effects_pool(instance)) > 0);
  CHECK(drain_unhandled(effects) == 0);
  qa_scene_camera camera = camera_at((qa_vec3){0, 0, 0});
  camera.projection = qa_identity_mat4();
  qa_source_scene_order order;
  qa_source_scene_order_init(&order, qa_app_application_assets_material_registrations(f.assets));
  qa_app_application_effect_frame frame;
  qa_app_application_effects_frame(effects, camera, &order, NULL, NULL, &frame);
  CHECK(frame.light_count == 1);
  qa_app_application_effect_frame_destroy(&frame);
  jump.sequence = 3;
  qa_app_sim_presentation_event pending = event_of("q3-character", source, 3, 1, &jump);
  qa_app_application_effects_receive(effects, &pending, 1); /* Pending old-round effects must also disappear. */
  qa_app_application_effects_reset_round(effects);
  qa_session_actor_registry_release(f.actors, old);
  qa_owned_actor next = qa_session_actor_registry_allocate(f.actors, "q3:character", "q3:character/sarge");
  CHECK(!qa_same_actor(next.id, old.id));
  CHECK(qa_q3_pres_local_entity_pool_active_count(qa_app_q3_application_effects_pool(instance)) == 0);
  qa_source_scene_order_init(&order, qa_app_application_assets_material_registrations(f.assets));
  qa_app_application_effects_frame(effects, camera, &order, NULL, NULL, &frame);
  CHECK(frame.light_count == 0);
  qa_app_application_effect_frame_destroy(&frame);
  qa_app_source_effect_sound *sounds = NULL;
  CHECK(drain_sounds(effects, &sounds) == 0); free(sounds);
  CHECK(qa_app_q3_application_effects_renderer(instance) == renderer && qa_app_q3_application_effects_shader_count(instance) == shader_count);
  CHECK(qa_app_application_assets_world(f.assets) == world);
  qa_app_sim_presentation_event replay[3] = {event_of("q3-character", source, 1, 1, &jump), event_of("q2-rerelease", source, 2, 1, &dynamic),
                                             event_of("q3-character", source, 3, 1, &jump)};
  qa_app_application_effects_receive(effects, replay, 3);
  view = character_view(next.id);
  snapshot = snapshot_at(&f, 1.4);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, &view, 1);
  CHECK(qa_q3_pres_local_entity_pool_active_count(qa_app_q3_application_effects_pool(instance)) == 0);
  CHECK(drain_unhandled(effects) == 0);
  qa_app_sim_q3_character_presentation_event jump_next = {next.id, 1, 1400, QA_Q3_EV_JUMP_PAD, 0};
  qa_app_sim_presentation_event fresh = event_of("q3-character", source, 4, 1.4, &jump_next);
  qa_app_application_effects_receive(effects, &fresh, 1);
  qa_app_application_effects_prepare(effects, &snapshot, NULL, 0, &view, 1);
  CHECK(qa_q3_pres_local_entity_pool_active_count(qa_app_q3_application_effects_pool(instance)) > 0);
  CHECK(qa_app_application_effects_q3(effects, source) == instance);
  CHECK(qa_app_q3_application_effects_renderer(instance) == renderer);
  qa_app_application_effects_reset_round(effects); qa_app_application_effects_reset_round(effects);
  CHECK(qa_q3_pres_local_entity_pool_active_count(qa_app_q3_application_effects_pool(instance)) == 0);
  /* Per-recipient effects. */
  qa_owned_actor recipient = qa_session_actor_registry_allocate(f.actors, "q3:character", "q3:character/sarge");
  qa_owned_actor other = qa_session_actor_registry_allocate(f.actors, "q3:character", "q3:character/sarge");
  qa_app_application_effects *targeted = qa_app_application_effects_create(f.assets, qa_shared_as_scene_queries(f.queries), is_player, &player_holder, 1);
  qa_app_sim_q3_character_presentation_event jump_recipient = {recipient.id, 1, 2000, QA_Q3_EV_JUMP_PAD, 0};
  qa_q2_rerelease_event light_recipient = dynamic; light_recipient.actor = recipient.id;
  qa_q2_rerelease_event light_other = dynamic; light_other.actor = other.id; light_other.radius = 50;
  qa_app_q3_source_event sound; memset(&sound, 0, sizeof sound);
  sound.kind = QA_APP_Q3_EVENT_SOUND; sound.actor = recipient.id; sound.origin = (qa_vec3){80, 0, 0}; sound.velocity = sound.origin;
  sound.path = "sound/weapons/grenade/hgrenb1a.wav"; sound.channel = 0; sound.volume = 1; sound.loop = false;
  qa_app_sim_presentation_event mixed[4] = {event_of("q3-character", source, 20, 2, &jump_recipient), event_of("q2-rerelease", source, 21, 2, &light_recipient),
                                            event_of("q3-source", source, 22, 2, &sound), event_of("q2-rerelease", source, 10, 2, &light_other)};
  for (int i = 0; i < 3; i++) { mixed[i].has_recipient = true; mixed[i].recipient = recipient.id; }
  qa_app_application_effects_receive(targeted, mixed, 4);
  qa_q3_character_view views[2] = {character_view(recipient.id), character_view(other.id)};
  snapshot = snapshot_at(&f, 2);
  qa_app_application_effects_prepare(targeted, &snapshot, NULL, 0, views, 2);
  qa_source_scene_order_init(&order, qa_app_application_assets_material_registrations(f.assets));
  qa_app_application_effects_frame(targeted, camera, &order, &recipient.id, NULL, &frame);
  CHECK(frame.light_count == 2 && frame.lights[0].radius == 50 && frame.lights[1].radius == 200);
  qa_app_application_effect_frame_destroy(&frame);
  qa_app_application_effects_frame(targeted, camera, &order, &other.id, NULL, &frame);
  CHECK(frame.light_count == 1 && frame.lights[0].radius == 50);
  qa_app_application_effect_frame_destroy(&frame);
  qa_app_application_effects_frame(targeted, camera, &order, NULL, NULL, &frame);
  CHECK(frame.light_count == 1 && frame.lights[0].radius == 50);
  qa_app_application_effect_frame_destroy(&frame);
  CHECK(drain_sounds(targeted, &sounds) == 0); free(sounds);
  size_t rows = 0;
  qa_app_recipient_effect_sounds *recipient_sounds = qa_app_application_effects_drain_recipient_sounds(targeted, &rows);
  CHECK(rows == 1 && qa_same_actor(recipient_sounds[0].recipient, recipient.id) && recipient_sounds[0].sound_count > 0);
  for (size_t i = 0; i < rows; i++) free(recipient_sounds[i].sounds);
  free(recipient_sounds);
  recipient_sounds = qa_app_application_effects_drain_recipient_sounds(targeted, &rows);
  CHECK(rows == 0); free(recipient_sounds);
  qa_app_application_effects_reset_round(targeted);
  qa_app_sim_presentation_event again = event_of("q2-rerelease", source, 21, 1, &light_recipient);
  again.has_recipient = true; again.recipient = recipient.id;
  qa_app_application_effects_receive(targeted, &again, 1);
  views[0] = character_view(recipient.id);
  snapshot = snapshot_at(&f, 2.1);
  qa_app_application_effects_prepare(targeted, &snapshot, NULL, 0, views, 1);
  qa_app_application_effects_frame(targeted, camera, &order, &recipient.id, NULL, &frame);
  CHECK(frame.light_count == 0);
  qa_app_application_effect_frame_destroy(&frame);
  again.sequence = 23;
  qa_app_application_effects_receive(targeted, &again, 1);
  snapshot = snapshot_at(&f, 2.2);
  qa_app_application_effects_prepare(targeted, &snapshot, NULL, 0, views, 1);
  qa_app_application_effects_frame(targeted, camera, &order, &recipient.id, NULL, &frame);
  CHECK(frame.light_count == 1);
  qa_app_application_effect_frame_destroy(&frame);
  qa_session_actor_registry_release(f.actors, recipient);
  snapshot = snapshot_at(&f, 2.3);
  qa_app_application_effects_prepare(targeted, &snapshot, NULL, 0, NULL, 0);
  qa_app_application_effects_frame(targeted, camera, &order, &recipient.id, NULL, &frame);
  CHECK(frame.light_count == 0);
  qa_app_application_effect_frame_destroy(&frame);
  CHECK(drain_unhandled(targeted) == 0);
  qa_app_application_effects_close(targeted);
  qa_app_application_effects_destroy(targeted);
  qa_app_application_effects_close(effects);
  qa_caught_error caught;
  CHECK(!qa_try(reset_raises, effects, &caught) && strcmp(caught.message, "Effect world is closed") == 0);
  qa_app_application_effects_destroy(effects);
  qa_session_actor_registry_release(f.actors, next);
  qa_session_actor_registry_release(f.actors, other);
  close_fixture(&f);
}

/* ---- Q3ApplicationEffects.event routing (moved from tests/app/test_client.c) ---- */
static void test_q3_event_routing(void) {
  if (!installed("Quake 3 Arena")) return;
  fixture f;
  if (!open_fixture(&f, "q3-baseq3", "q3dm1")) return;
  int reads = hardware_holder.reads;
  qa_app_q3_application_effects *effects = qa_app_q3_application_effects_create(f.assets, qa_shared_as_scene_queries(f.queries),
      map_content(&f), false, read_hardware, &hardware_holder);
  CHECK(hardware_holder.reads > reads);
  CHECK(qa_app_q3_application_effects_product(effects) == QA_Q3_BASE_PRODUCT_BASEQ3);
  qa_registered_scene_material material;
  CHECK(qa_app_q3_application_effects_shader(effects, "smokePuff", &material) && qa_app_q3_application_effects_shader(effects, "bloodMark", &material));
  CHECK(!qa_app_q3_application_effects_shader(effects, "lightningBolt", &material));
  qa_q3_pres_local_entity_pool *pool = qa_app_q3_application_effects_pool(effects);
  qa_app_sim_q3_character_presentation_event event; memset(&event, 0, sizeof event);
  event.event = QA_Q3_EV_PLAYER_TELEPORT_IN; event.time_milliseconds = 100;
  CHECK(qa_app_q3_application_effects_event(effects, &event, (qa_vec3){0, 0, 0}));
  CHECK(qa_app_q3_application_effects_time(effects) == 100 && qa_q3_pres_local_entity_pool_active_count(pool) == 1);
  event.event = QA_Q3_EV_JUMP_PAD + 0x100; event.time_milliseconds = 200;
  CHECK(qa_app_q3_application_effects_event(effects, &event, (qa_vec3){0, 0, 0}));
  CHECK(qa_q3_pres_local_entity_pool_active_count(pool) == 2);
  event.event = QA_Q3_EV_GIB_PLAYER; event.time_milliseconds = 300;
  CHECK(qa_app_q3_application_effects_event(effects, &event, (qa_vec3){0, 0, 0}));
  CHECK(qa_q3_pres_local_entity_pool_active_count(pool) > 2);
  event.event = QA_Q3_EV_FOOTSTEP;
  CHECK(qa_app_q3_application_effects_event(effects, &event, (qa_vec3){0, 0, 0}));
  event.event = 999;
  CHECK(!qa_app_q3_application_effects_event(effects, &event, (qa_vec3){0, 0, 0}));
  qa_app_q3_application_effects_prepare(effects, 400, 100);
  size_t count = 0;
  qa_app_source_effect_sound *sounds = qa_app_q3_application_effects_drain_sounds(effects, &count);
  for (size_t i = 0; i < count; i++) CHECK(strcmp(sounds[i].playback_kind, "once") == 0 && sounds[i].seconds >= 0.3);
  free(sounds);
  qa_app_q3_application_effects_close(effects);
  CHECK(qa_q3_pres_local_entity_pool_active_count(pool) == 0);
  qa_app_q3_application_effects_destroy(effects);
  close_fixture(&f);
}

int main(void) {
  test_q1_effects();
  test_flashlight_and_shadow_lights();
  test_routing_without_assets();
  test_q2_explosions();
  test_q3_event_routing();
  test_q3_round_reset();
  printf("test_top_effects: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
