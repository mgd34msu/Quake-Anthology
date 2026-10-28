/* Port of key cases from tests/audio/startup.test.ts,
 * tests/app/q3-equipment-presentation.test.ts and the pure
 * helpers of the w7-client slice (audio/, effects/, q3-client/,
 * rerelease-presentation/fog). GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "qa/app/bootstrap/audio/commands.h"
#include "qa/app/bootstrap/audio/menu.h"
#include "qa/app/bootstrap/audio/music.h"
#include "qa/app/bootstrap/audio/output_settings.h"
#include "qa/app/bootstrap/audio/playlist.h"
#include "qa/app/bootstrap/audio/playlist_settings.h"
#include "qa/app/bootstrap/audio/q2_events.h"
#include "qa/app/bootstrap/effects/particles.h"
#include "qa/app/bootstrap/effects/q2_muzzle.h"
#include "qa/app/bootstrap/effects/q2_view.h"
#include "qa/app/bootstrap/effects/q3.h"
#include "qa/app/bootstrap/q3_client/cvars.h"
#include "qa/app/bootstrap/q3_client/equipment.h"
#include "qa/app/bootstrap/q3_client/status.h"
#include "qa/app/bootstrap/q3_client/userinfo.h"
#include "qa/app/bootstrap/q3_client/view.h"
#include "qa/app/bootstrap/rerelease_presentation/fog.h"
#include "qa/foundation/support.h"
static void set_ignored(qa_cvar_registry *cvars, const char *name, const char *value, bool force) {
  qa_cvar_snapshot ignored;
  if (qa_cvar_registry_set(cvars, name, value, force, &ignored)) qa_cvar_read_free(&ignored);
}
static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)
#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)
static bool text_eq(const char *a, const char *b) {
  return (a == NULL && b == NULL) || (a != NULL && b != NULL && strcmp(a, b) == 0);
}
static void print_sink(void *ctx, const char *text) { (void)ctx; (void)text; }
static qa_cvar_registry *make_cvars(qa_command_dialect dialect) {
  qa_cvar_registry_options opts;
  memset(&opts, 0, sizeof opts);
  opts.dialect = dialect;
  opts.print = print_sink;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&opts);
  if (!cvars) { fprintf(stderr, "no cvars\n"); exit(1); }
  return cvars;
}
static int64_t fixed_random(void *ctx) { return *(int64_t *)ctx; }
static void test_audio_commands_menu(void) {
  CHECK(qa_app_application_audio_command_count == 10);
  CHECK(text_eq(qa_app_application_audio_commands[0], "snd_restart"));
  CHECK(text_eq(qa_app_application_audio_commands[9], "s_stop"));
  CHECK(text_eq(qa_app_cd_command_documentation.allowed_values[0], "play"));
  /* tests/audio/startup.test.ts menu sounds */
  CHECK(text_eq(qa_app_menu_sound_path(QA_GAME_FAMILY_Q1, "open"), "misc/menu2.wav"));
  CHECK(text_eq(qa_app_menu_sound_path(QA_GAME_FAMILY_Q1, "move"), "misc/menu1.wav"));
  CHECK(text_eq(qa_app_menu_sound_path(QA_GAME_FAMILY_Q1, "change"), "misc/menu3.wav"));
  CHECK(text_eq(qa_app_menu_sound_path(QA_GAME_FAMILY_Q3, "reject"), "misc/menu4.wav"));
  CHECK(text_eq(qa_app_menu_sound_path(QA_GAME_FAMILY_Q2, "reject"), "misc/menu2.wav"));
  CHECK(text_eq(qa_app_menu_sound_path(QA_GAME_FAMILY_Q2, "close"), "misc/menu3.wav"));
}
static void test_world_music_track(void) {
  const qa_app_world_key_value world[] = {{"music", "music/custom.ogg"}, {"sounds", "6"}};
  CHECK(text_eq(qa_app_world_music_track(world, 2, true, QA_GAME_FAMILY_Q2, "rerelease"), "music/custom.ogg"));
  CHECK(text_eq(qa_app_world_music_track(world, 2, true, QA_GAME_FAMILY_Q2, "classic"), "6"));
  CHECK(text_eq(qa_app_world_music_track(world, 2, true, QA_GAME_FAMILY_Q1, "rerelease"), "6"));
  CHECK(text_eq(qa_app_world_music_track(world, 2, true, QA_GAME_FAMILY_Q3, "classic"), "music/custom.ogg"));
  const qa_app_world_key_value sounds_only[] = {{"sounds", "6"}};
  CHECK(text_eq(qa_app_world_music_track(sounds_only, 1, true, QA_GAME_FAMILY_Q3, "classic"), ""));
  const qa_app_world_key_value zero[] = {{"music", "0"}, {"sounds", "6"}};
  CHECK(text_eq(qa_app_world_music_track(zero, 2, true, QA_GAME_FAMILY_Q2, "rerelease"), "0"));
  CHECK(text_eq(qa_app_world_music_track(NULL, 0, false, QA_GAME_FAMILY_Q2, "rerelease"), ""));
}
static double half_random(void *ctx) { (void)ctx; return 0; }
static void test_playlist_helpers(void) {
  CHECK(qa_app_valid_menu_track("auto"));
  CHECK(qa_app_valid_menu_track("0"));
  CHECK(qa_app_valid_menu_track("255"));
  CHECK(!qa_app_valid_menu_track("256"));
  CHECK(!qa_app_valid_menu_track("00"));
  CHECK(qa_app_valid_menu_track("music/win.ogg"));
  CHECK(qa_app_valid_menu_track("music/win"));
  CHECK(!qa_app_valid_menu_track("music/win.mp3"));
  CHECK(!qa_app_valid_menu_track("../win.ogg"));
  CHECK(!qa_app_valid_menu_track("music//win.ogg"));
  CHECK(!qa_app_valid_menu_track(" music/win.ogg"));
  CHECK(!qa_app_valid_menu_track("music\\win.ogg"));
  char *cue = qa_app_music_file_cue("a b");
  CHECK(text_eq(cue, "\"a b\""));
  free(cue);
  cue = qa_app_music_file_cue("ab");
  CHECK(text_eq(cue, "ab"));
  free(cue);
  /* Fisher-Yates with random()=0 always swaps with slot 0:
   * [a,b,c] -> i=2 swap(0) [c,b,a] -> i=1 swap(0) [b,c,a]. */
  const char *const tracks[] = {"a", "b", "c", "b"};
  size_t count = 0;
  char **bag = qa_app_shuffled_tracks(tracks, 4, "previous", half_random, NULL, &count);
  CHECK(count == 3);
  CHECK(text_eq(bag[0], "b") && text_eq(bag[1], "c") && text_eq(bag[2], "a"));
  for (size_t i = 0; i < count; i++) free(bag[i]);
  free(bag);
  /* First entry equal to previous rotates to the end. */
  bag = qa_app_shuffled_tracks(tracks, 3, "b", half_random, NULL, &count);
  CHECK(count == 3 && text_eq(bag[0], "c") && text_eq(bag[2], "b"));
  for (size_t i = 0; i < count; i++) free(bag[i]);
  free(bag);
}
static void test_output_and_music_settings(void) {
  qa_cvar_registry *cvars = make_cvars(QA_COMMAND_DIALECT_Q3);
  qa_app_register_audio_output_cvars(cvars, NULL);
  qa_audio_output_format format = qa_app_read_audio_output_cvars(cvars);
  CHECK(format.sample_rate == 44100 && format.channels == 2 && format.sample_bits == 16);
  qa_app_write_audio_output_cvars(cvars, (qa_audio_output_format){22050, 1, 8});
  format = qa_app_read_audio_output_cvars(cvars);
  CHECK(format.sample_rate == 22050 && format.channels == 1 && format.sample_bits == 8);
  char *khz = qa_cvar_registry_variable_string(cvars, "s_khz");
  CHECK(text_eq(khz, "22"));
  free(khz);
  qa_app_register_music_settings(cvars);
  qa_app_audio_music_preferences prefs = qa_app_read_music_settings(cvars);
  CHECK(!prefs.music_shuffle && text_eq(prefs.menu_track, "auto"));
  free(prefs.menu_track);
  set_ignored(cvars, "music_shuffle", "1", false);
  set_ignored(cvars, "ogg_menu_track", "77", false);
  prefs = qa_app_read_music_settings(cvars);
  CHECK(prefs.music_shuffle && text_eq(prefs.menu_track, "77"));
  free(prefs.menu_track);
  prefs = qa_app_read_music_settings(NULL);
  CHECK(!prefs.music_shuffle && text_eq(prefs.menu_track, "auto"));
  free(prefs.menu_track);
  qa_cvar_registry_destroy(cvars);
}
static void test_q2_event_sounds(void) {
  qa_app_q2_event_sound sound;
  int64_t three = 3;
  CHECK(qa_app_q2_entity_sound(2, fixed_random, &three, &sound));
  CHECK(text_eq(sound.path, "player/step4.wav") && sound.channel == 4);
  CHECK(qa_app_q2_entity_sound(1, fixed_random, &three, &sound));
  CHECK(text_eq(sound.path, "items/respawn1.wav") && sound.attenuation == 2);
  CHECK(!qa_app_q2_entity_sound(99, fixed_random, &three, &sound));
  qa_app_q2_event_sound sounds[3];
  int64_t four = 4;
  size_t count = qa_app_q2_muzzle_sounds(128 + 2, true, fixed_random, &four, false, sounds);
  CHECK(count == 2);
  CHECK(text_eq(sounds[0].path, "weapons/shotgf1b.wav") && sounds[0].volume == 0.2);
  CHECK(sounds[1].channel == 0 && sounds[1].delay_seconds == 0.1);
  count = qa_app_q2_muzzle_sounds(6, false, fixed_random, &four, true, sounds);
  CHECK(count == 2 && text_eq(sounds[1].path, "weapons/railgr1b.wav") && sounds[1].channel == 7);
  CHECK(qa_app_q2_monster_muzzle_sounds(60, fixed_random, &four, false, &sound) == 1);
  CHECK(text_eq(sound.path, "medic/medatck1.wav"));
  CHECK(qa_app_q2_monster_muzzle_sounds(61, fixed_random, &four, false, &sound) == 0);
  CHECK(qa_app_q2_monster_muzzle_sounds(999, fixed_random, &four, false, &sound) == -1);
  CHECK(qa_app_q2_monster_muzzle_sounds(263, fixed_random, &four, true, &sound) == 1);
  CHECK(text_eq(sound.path, "hover/hovatck1.wav"));
  qa_app_monster_muzzle muzzle;
  CHECK(qa_app_q2_monster_muzzle(26, false, &muzzle));
  CHECK(muzzle.particles && muzzle.smoke && muzzle.radius == 200 && muzzle.mask == 31);
  CHECK(qa_app_q2_monster_muzzle(232, true, &muzzle) && muzzle.particles);
  CHECK(!qa_app_q2_monster_muzzle(232, false, &muzzle));
  CHECK(qa_app_q2_monster_muzzle(148, false, &muzzle) && muzzle.color.x == -1);
}
static void test_music_volume_and_cd(void) {
  static char captured[512];
  captured[0] = '\0';
  struct capture { int dummy; };
  qa_app_application_music *music = qa_app_application_music_create(NULL, print_sink, NULL,
      QA_MUSIC_VOLUME_SOURCE, NULL, NULL, NULL);
  CHECK(qa_app_application_music_volume(music) == 0.25);
  qa_app_application_music_set_volume(music, 0.5);
  CHECK(qa_app_application_music_volume(music) == 0.5);
  CHECK_ABORTS(qa_app_application_music_set_volume(music, -1));
  CHECK_ABORTS(qa_app_application_music_set_volume(music, NAN));
  qa_app_application_music_destroy(music);
  (void)captured;
}
static void test_fog(void) {
  qa_q2_fog_state source = qa_q2_create_fog();
  source.fog.density = 0.5;
  source.fog.color = (qa_vec3){1, 0.5, 0};
  source.fog.sky_factor = 1;
  source.height_fog.start_distance = 64.7;
  qa_q2_fog_state converted = qa_app_q2_fog_from_source(source);
  CHECK(converted.fog.color.x == 1);
  CHECK(converted.fog.color.y == (double)(float)(127.0 / 255.0));
  CHECK(converted.height_fog.start_distance == 64);
  qa_q2_svc_fog_data wire;
  qa_q2_svc_fog_data_init(&wire);
  wire.bits = QA_Q2_SVC_FOG_BIT_DENSITY | QA_Q2_SVC_FOG_BIT_R;
  wire.density = 0.25;
  wire.skyfactor = 255;
  wire.red = 128;
  qa_q2_fog_state from_wire = qa_app_q2_fog_from_wire(converted, wire);
  CHECK(from_wire.fog.density == 0.25 && from_wire.fog.sky_factor == 1);
  CHECK(from_wire.fog.color.x == 128.0 / 255.0);
  CHECK(from_wire.fog.color.y == converted.fog.color.y);
  qa_app_rerelease_fog fog;
  qa_app_rerelease_fog_init(&fog);
  qa_app_rerelease_fog_receive(&fog, source, 0, 10);
  qa_scene_fog current = qa_app_rerelease_fog_current(&fog, 10);
  CHECK(text_eq(current.kind, "q2") && current.density == 0.5);
  qa_q2_fog_state second = source;
  second.fog.density = 1;
  qa_app_rerelease_fog_receive(&fog, second, 1000, 10);
  current = qa_app_rerelease_fog_current(&fog, 10.5);
  CHECK(fabs(current.density - 0.75) < 1e-9);
}
static void test_equipment_and_userinfo(void) {
  qa_q3_wire_user_command command = {50, {10, 20, 30}, 100, -25, 127, 9, 7};
  qa_q3_wire_user_command same = qa_app_q3_equipment_command(command, NULL);
  CHECK(same.buttons == 9 && same.weapon == 7);
  qa_app_q3_equipment_presentation equipment = {2, "none"};
  qa_q3_wire_user_command changed = qa_app_q3_equipment_command(command, &equipment);
  CHECK(changed.weapon == 2 && changed.buttons == 8);
  CHECK(command.buttons == 9 && command.weapon == 7);
  CHECK(text_eq(qa_app_q3_client_model("q3", "visor"), "visor"));
  CHECK(text_eq(qa_app_q3_client_model("q1", "visor"), "sarge"));
  qa_cvar_registry *cvars = make_cvars(QA_COMMAND_DIALECT_Q3);
  qa_app_initialize_q3_client_cvars(cvars, (qa_app_q3_client_identity){"Player", "visor"});
  char *model = qa_cvar_registry_variable_string(cvars, "model");
  CHECK(text_eq(model, "visor/default"));
  free(model);
  CHECK(qa_cvar_registry_variable_value(cvars, "rate") == 25000);
  qa_app_apply_q3_proxy_model(cvars, "q1");
  model = qa_cvar_registry_variable_string(cvars, "model");
  CHECK(text_eq(model, "sarge/default"));
  free(model);
  qa_cvar_registry_destroy(cvars);
}
static qa_cvar_registry *cvars_owner_registry;
static qa_cvar_registry *cvars_owner(void *ctx, const char *name) { (void)ctx; (void)name; return cvars_owner_registry; }
static size_t cvars_visible(void *ctx, qa_cvar_registry ***out) {
  (void)ctx;
  static qa_cvar_registry *list[1];
  list[0] = cvars_owner_registry;
  *out = list;
  return 1;
}
static qa_cvar_registry *cvars_current(void *ctx, qa_cvar_registry *owner) { (void)ctx; return owner; }
static void test_client_cvars_and_status(void) {
  cvars_owner_registry = make_cvars(QA_COMMAND_DIALECT_Q3);
  qa_app_q3_client_cvars *client = qa_app_q3_client_cvars_create(
      (qa_app_q3_client_cvars_options){NULL, cvars_owner, cvars_visible, cvars_current, print_sink});
  double handle = qa_app_q3_client_cvars_bind_vm(client, "cg_drawstatus", "1", 0);
  CHECK(handle == 0);
  CHECK(qa_app_q3_client_cvars_bind_vm(client, "cg_drawstatus", "1", 0) == 0);
  qa_cvar_snapshot snapshot;
  CHECK(qa_app_q3_client_cvars_read_vm(client, handle, &snapshot));
  CHECK(snapshot.modification_count == 1);
  /* effectiveStatusCvar hides the value when not visible */
  qa_cvar_snapshot masked;
  qa_app_effective_status_cvar(&snapshot, false, &masked);
  CHECK(text_eq(masked.value, "0") && masked.numeric_value == 0);
  qa_cvar_read_free(&masked);
  qa_app_effective_status_cvar(&snapshot, true, &masked);
  CHECK(text_eq(masked.value, "1"));
  qa_cvar_read_free(&masked);
  qa_cvar_read_free(&snapshot);
  CHECK_ABORTS(qa_cvar_snapshot s; qa_app_q3_client_cvars_read_vm(client, 99, &s));
  char *info = qa_app_q3_client_cvars_info_string(client, QA_CVAR_FLAG_USER_INFO, -1);
  CHECK(info != NULL);
  free(info);
  qa_app_q3_client_cvars_destroy(client);
  qa_cvar_registry_destroy(cvars_owner_registry);
}
static void test_view(void) {
  qa_scene_camera camera;
  memset(&camera, 0, sizeof camera);
  camera.viewport = (qa_rect){0, 0, 1920, 540};
  camera.projection.values[0] = 2;
  camera.projection.values[5] = 3;
  qa_scene_camera wide = qa_app_q3_weapon_camera(camera, true);
  double aspect = 1920.0 / 540.0;
  double scale = (4.0 / 3.0) / aspect;
  CHECK(wide.projection.values[0] == (double)(float)(2 * scale));
  CHECK(wide.projection.values[5] == (double)(float)(3 * scale));
  qa_scene_camera narrow = camera;
  narrow.viewport = (qa_rect){0, 0, 640, 480};
  CHECK(qa_app_q3_weapon_camera(narrow, true).projection.values[0] == 2);
  qa_scene_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.transform.origin = (qa_vec3){1, 2, 3};
  entity.previous_origin = (qa_vec3){4, 5, 6};
  entity.lighting_origin = (qa_vec3){7, 8, 9};
  entity.shadow_plane = 10;
  qa_vec3 delta = {10, 20, 30};
  qa_scene_entity *moved = qa_app_offset_q3_view_entity(&entity, &delta);
  CHECK(moved->transform.origin.x == 11 && moved->previous_origin.z == 36);
  CHECK(moved->shadow_plane == 40);
  qa_app_offset_q3_view_entity_free(moved);
}
static void test_particles_and_effects(void) {
  qa_app_sim_source_random *random = qa_app_sim_source_random_create(1, "classic");
  qa_app_source_particles *particles = qa_app_source_particles_create(random, -1);
  qa_app_source_particles_q1_impact(particles, (qa_vec3){0, 0, 0}, (qa_vec3){1, 0, 0}, 208, 4, 1);
  qa_scene_particle *q1 = NULL, *q2 = NULL;
  size_t q1_count = 0, q2_count = 0;
  qa_app_source_particles_sample(particles, 1, 0, &q1, &q1_count, &q2, &q2_count);
  CHECK(q1_count == 4);
  CHECK(q2_count == 0);
  free(q1);
  free(q2);
  CHECK_ABORTS(qa_app_source_particles_q1_color_explosion(particles, (qa_vec3){0, 0, 0}, 1, 0, 0));
  qa_app_source_particles_destroy(particles);
  qa_app_sim_source_random_destroy(random);
}
typedef struct effects_probe { int spawns, puffs, gibs; } effects_probe;
static void fx_spawn(void *ctx, qa_vec3 origin) { (void)origin; ((effects_probe *)ctx)->spawns++; }
static void fx_puff(void *ctx, qa_vec3 o, qa_vec3 v, double r, qa_vec4 c, double d, double s, double f, double fl, const char *sh) {
  (void)o; (void)v; (void)r; (void)c; (void)d; (void)s; (void)f; (void)fl; (void)sh;
  ((effects_probe *)ctx)->puffs++;
}
static void fx_gib(void *ctx, qa_vec3 origin) { (void)origin; ((effects_probe *)ctx)->gibs++; }
static void fx_noop_prepare(void *ctx, double a, double b) { (void)ctx; (void)a; (void)b; }
static void fx_noop_frame(void *ctx, qa_scene_camera cam, const void *src, bool hv, qa_actor_id v,
                          bool hf, qa_scene_fog fog, const void **adm, const void **ops, size_t *oc,
                          const void **lights, size_t *lc) {
  (void)ctx; (void)cam; (void)src; (void)hv; (void)v; (void)hf; (void)fog;
  *adm = NULL; *ops = NULL; *oc = 0; *lights = NULL; *lc = 0;
}
static void fx_void(void *ctx) { (void)ctx; }
static void test_effects_routing(void) {
  effects_probe probe = {0, 0, 0};
  qa_app_q3_effects_host host = {&probe, fx_spawn, fx_puff, fx_gib, fx_noop_prepare, fx_noop_frame, fx_void, fx_void, fx_void};
  qa_app_q3_application_effects *effects = qa_app_q3_application_effects_create(host, "q3:base", "baseq3");
  /* EV_PLAYER_TELEPORT_IN = 42, EV_JUMP_PAD = 13, EV_GIB_PLAYER = 64,
   * EV_FOOTSTEP = 1 (movement/q3/constants). */
  CHECK(qa_app_q3_application_effects_event(effects, (qa_app_q3_character_presentation_event){42, 100}, (qa_vec3){0, 0, 0}));
  CHECK(probe.spawns == 1 && qa_app_q3_application_effects_time(effects) == 100);
  CHECK(qa_app_q3_application_effects_event(effects, (qa_app_q3_character_presentation_event){13 + 0x100, 200}, (qa_vec3){0, 0, 0}));
  CHECK(probe.puffs == 1);
  CHECK(qa_app_q3_application_effects_event(effects, (qa_app_q3_character_presentation_event){64, 300}, (qa_vec3){0, 0, 0}));
  CHECK(probe.gibs == 1);
  CHECK(qa_app_q3_application_effects_event(effects, (qa_app_q3_character_presentation_event){1, 400}, (qa_vec3){0, 0, 0}));
  CHECK(!qa_app_q3_application_effects_event(effects, (qa_app_q3_character_presentation_event){999, 500}, (qa_vec3){0, 0, 0}));
  qa_app_q3_application_effects_destroy(effects);
}
static void test_q2_effect_views(void) {
  qa_app_q2_effect_views *views = qa_app_q2_effect_views_create();
  qa_actor_id actor;
  memset(&actor, 0, sizeof actor);
  actor.slot = 7;
  qa_q2mp_player_effect event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2MP_EFFECT_IR;
  event.actor = actor;
  event.until = 10;
  qa_app_q2_effect_views_receive(views, event);
  qa_scene_camera camera;
  memset(&camera, 0, sizeof camera);
  qa_app_q2_effect_player_view view = qa_app_q2_effect_views_frame(views, actor, camera, 5, NULL, NULL);
  CHECK(view.infrared);
  CHECK(view.has_blend && fabs(view.blend.w - 0.2) < 1e-12);
  qa_app_q2_effect_views_destroy(views);
}
int main(void) {
  test_audio_commands_menu();
  test_world_music_track();
  test_playlist_helpers();
  test_output_and_music_settings();
  test_q2_event_sounds();
  test_music_volume_and_cd();
  test_fog();
  test_equipment_and_userinfo();
  test_client_cvars_and_status();
  test_view();
  test_particles_and_effects();
  test_effects_routing();
  test_q2_effect_views();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
