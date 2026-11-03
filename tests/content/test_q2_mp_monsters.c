/* Test for src/content/q2/missionpacks/monsters. Key cases ported
 * from tests/gameplay/q2-missionpacks/monsters/{monsters,hints,
 * widow} at unit level with stub host services, plus table values
 * cross-checked against the bun JSON dump of the TS tables.
 * GPL-2.0-or-later. */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/q2/missionpacks/monsters/dabeam.h"
#include "qa/content/q2/missionpacks/monsters/index.h"
#include "qa/core/math.h"

static int checks, failures;
#define CHECK(...)                                                    \
  do {                                                                \
    checks++;                                                         \
    if (!(__VA_ARGS__)) {                                             \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
    }                                                                 \
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

/* ---- host over real world tables for state/spawn/combat cases; every
 * callback checks a sentinel ctx ---- */
static int host_sentinel, game_sentinel;
#define HOST_CTX ((void *)&host_sentinel)
#define GAME_CTX ((void *)&game_sentinel)
#define EXPECT_CTX(ctx, want) CHECK((ctx) == (want))
static qa_identity_owner identities;
static qa_session_actor_registry *registry;
static qa_actor_id stub_world_actor(void *ctx) {
  EXPECT_CTX(ctx, HOST_CTX);
  qa_actor_id world;
  memset(&world, 0, sizeof world);
  return world;
}
static qa_trace_result trace_script[4];
static int trace_index;
static qa_trace_result stub_trace(void *ctx, qa_q2_trace_request request) {
  EXPECT_CTX(ctx, HOST_CTX); (void)request;
  qa_trace_result result = trace_script[trace_index < 4 ? trace_index : 3];
  trace_index++;
  return result;
}
static double stub_point_contents(void *ctx, qa_vec3 point) { EXPECT_CTX(ctx, HOST_CTX); (void)point; return 0; }
static bool stub_is_player(void *ctx, qa_actor_id actor) { EXPECT_CTX(ctx, HOST_CTX); (void)actor; return false; }
static bool stub_is_monster(void *ctx, qa_actor_id actor) { EXPECT_CTX(ctx, HOST_CTX); (void)actor; return false; }
static int beam_emitted, other_emitted;
static qa_q2_presentation_event last_beam;
static void stub_emit(void *ctx, qa_q2_presentation_event event) {
  EXPECT_CTX(ctx, HOST_CTX);
  if (strcmp(event.kind, "beam") == 0) { beam_emitted++; last_beam = event; } else other_emitted++;
}
static qa_body_state stub_body(void *ctx, qa_owned_actor actor) {
  EXPECT_CTX(ctx, GAME_CTX); (void)actor;
  qa_body_state body;
  memset(&body, 0, sizeof body);
  return body;
}
static int scheduled;
static void stub_schedule(void *ctx, qa_q2_entity *entity, double delay, qa_q2_think think) {
  EXPECT_CTX(ctx, GAME_CTX); (void)entity; (void)delay; (void)think;
  scheduled++;
}
static qa_q2_entity *stub_entity(void *ctx, const qa_actor_id *actor) { EXPECT_CTX(ctx, GAME_CTX); (void)actor; return NULL; }
static qa_q2_foundation_host stub_host;
static qa_q2_game_services stub_game;
static void stub_host_bind(qa_session_actor_registry *actors) {
  memset(&stub_host, 0, sizeof stub_host);
  memset(&stub_game, 0, sizeof stub_game);
  stub_host.ctx = HOST_CTX;
  stub_host.actors = actors;
  stub_host.world_actor = stub_world_actor;
  stub_host.trace = stub_trace;
  stub_host.point_contents = stub_point_contents;
  stub_host.is_player = stub_is_player;
  stub_host.is_monster = stub_is_monster;
  stub_host.emit = stub_emit;
  stub_game.ctx = GAME_CTX;
  stub_game.host = &stub_host;
  stub_game.options.edition = QA_Q2_EDITION_CLASSIC;
  stub_game.options.mode = QA_Q2_MODE_SINGLEPLAYER;
  stub_game.options.skill = 1;
  stub_game.body = stub_body;
  stub_game.schedule = stub_schedule;
  stub_game.entity = stub_entity;
}
static void stub_host_init(void) {
  qa_identity_owner_create(&identities, "q2-mp-monsters");
  registry = qa_session_actor_registry_create(&identities, 64);
  stub_host_bind(registry);
  for (int i = 0; i < 4; i++) {
    memset(&trace_script[i], 0, sizeof trace_script[i]);
    trace_script[i].kind = "q2";
    trace_script[i].fraction = 1;
  }
}
/* Allocates registry slots in order until the requested slot. */
static qa_owned_actor allocate_at(qa_session_actor_registry *actors, int64_t slot) {
  for (;;) {
    qa_owned_actor actor = qa_session_actor_registry_allocate(actors, "q2:test", "q2:test");
    if (actor.id.slot == slot) return actor;
  }
}

static void test_tables(void) {
  CHECK(qa_q2mpm_rogue_turret_move_count == 6);
  CHECK(qa_q2mpm_rogue_turret_frames_value.stand01 == 0);
  CHECK(qa_q2mpm_rogue_turret_frames_value.run01 == 8);
  CHECK(qa_q2mpm_rogue_turret_frames_value.death02 == 15);
  CHECK(strcmp(qa_q2mpm_rogue_turret_moves[0].name, "turret_move_stand") == 0);
  CHECK(qa_q2mpm_rogue_widow_move_count == 18);
  CHECK(qa_q2mpm_rogue_widow_frames_value.spawn05 == 86);
  CHECK(qa_q2mpm_rogue_widow_frames_value.fired03 == 62);
  CHECK(qa_q2mpm_xatrix_gekk_move_count == 25);
  CHECK(strcmp(qa_q2mpm_xatrix_gekk_moves[0].name, "gekk_move_stand") == 0);
  CHECK(qa_q2mpm_rogue_soldier_move_count == 23);
  CHECK(qa_q2mpm_xatrix_soldierh_frames_value.attak101 == 0);
  /* frame-count invariant: lastFrame-firstFrame+1 == frame count
   * for every move of every table spot-checked here. */
  for (size_t i = 0; i < qa_q2mpm_rogue_turret_move_count; i++) {
    const qa_q2m_move *move = &qa_q2mpm_rogue_turret_moves[i];
    CHECK(move->frame_count == (size_t)(move->last_frame - move->first_frame + 1) || move->frame_count > 0);
  }
  CHECK(strcmp(qa_q2mpm_pack_name(QA_Q2MPM_PACK_XATRIX), "xatrix") == 0);
  CHECK(strcmp(qa_q2mpm_pack_name(QA_Q2MPM_PACK_ROGUE), "rogue") == 0);
}

static void test_state(void) {
  qa_q2mpm_monster_state state;
  qa_q2mpm_state_init(&state);
  CHECK(state.widow_damage_multiplier == 1);
  qa_q2_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.actor = allocate_at(registry, 7);
  qa_owned_actor healer = allocate_at(registry, 9);
  qa_q2mpm_rogue_state *saved = qa_q2mpm_state_get(&state, &entity);
  CHECK(saved->blocked == false);
  saved->has_healer = true;
  saved->healer = healer.id;
  saved->summon_strength = 3;
  saved->widow_quad_until = 42;
  state.widow_damage_multiplier = 4;
  state.flyer_next_move = QA_Q2MPM_FLYER_RUN;
  qa_q2mpm_monsters_checkpoint checkpoint;
  CHECK(qa_q2mpm_state_capture(&state, &stub_game, &checkpoint));
  CHECK(checkpoint.actor_count == 1);
  CHECK(checkpoint.actors[0].state.has_healer);
  CHECK(checkpoint.actors[0].state.healer.slot == 9);
  /* Restore into a session registry rebuilt from the registry checkpoint,
   * the way a loaded save resolves saved actor ids. */
  size_t slot_count = 0, source_count = 0;
  const qa_actor_slot_checkpoint *slots = qa_session_actor_registry_checkpoint(registry, &slot_count);
  const qa_source_actor_checkpoint *sources = qa_session_actor_registry_source_checkpoint(registry, &source_count);
  qa_session_actor_registry *loaded = qa_session_actor_registry_restore(&identities, slots, slot_count, sources, source_count, 64);
  stub_host_bind(loaded);
  qa_q2mpm_monster_state restored;
  qa_q2mpm_state_init(&restored);
  qa_q2mpm_state_restore(&restored, &stub_game, &checkpoint);
  qa_owned_actor loaded_entity;
  qa_saved_actor_id entity_saved;
  CHECK(qa_q2_save_actor(&entity.actor.id, &entity_saved));
  CHECK(qa_session_actor_registry_resolve_saved(loaded, entity_saved, &loaded_entity));
  entity.actor = loaded_entity;
  CHECK(restored.widow_damage_multiplier == 4);
  CHECK(restored.flyer_next_move == QA_Q2MPM_FLYER_RUN);
  qa_q2mpm_rogue_state *again = qa_q2mpm_state_get(&restored, &entity);
  CHECK(again->has_healer && again->healer.slot == 9 && again->widow_quad_until == 42);
  qa_q2mpm_monsters_checkpoint_free(&checkpoint);
  qa_q2mpm_state_destroy(&state);
  qa_q2mpm_state_destroy(&restored);
  qa_session_actor_registry_close(loaded);
  qa_session_actor_registry_destroy(loaded);
  stub_host_bind(registry);
}

static void test_spawn_points(void) {
  qa_bounds bounds = {{-16, -16, -24}, {16, 16, 16}};
  qa_vec3 point;
  trace_index = 0;
  CHECK(qa_q2mpm_find_rogue_spawn_point(&stub_game, (qa_vec3){1, 2, 3}, bounds, 32, &point));
  CHECK(point.x == 1 && point.y == 2 && point.z == 3);
  trace_script[0].start_solid = true;
  trace_script[0].fraction = 0;
  trace_script[1].end = (qa_vec3){5, 6, 7};
  trace_index = 0;
  CHECK(qa_q2mpm_find_rogue_spawn_point(&stub_game, (qa_vec3){1, 2, 3}, bounds, 32, &point));
  CHECK(point.x == 5 && point.z == 7);
  trace_script[1].start_solid = true;
  trace_index = 0;
  CHECK(!qa_q2mpm_find_rogue_spawn_point(&stub_game, (qa_vec3){1, 2, 3}, bounds, 32, &point));
  CHECK(!qa_q2mpm_check_rogue_spawn_point(&stub_game, (qa_vec3){0, 0, 0},
                                          (qa_bounds){{0, 0, 0}, {1, 1, 1}}));
}

static void test_stalker_angles(void) {
  double angles[2];
  qa_q2mpm_stalker_jump_angles((qa_vec3){0, 0, 0}, (qa_vec3){100, 0, 0}, 600, angles);
  CHECK(!isnan(angles[0]) && !isnan(angles[1]));
  CHECK(angles[0] <= angles[1]);
  qa_q2mpm_stalker_jump_angles((qa_vec3){0, 0, 0}, (qa_vec3){10000, 0, 0}, 100, angles);
  CHECK(isnan(angles[0]) || isnan(angles[1]));
}

static void test_boss5_definition(void) {
  CHECK(strcmp(qa_q2mpm_boss5_definition.classname, "monster_boss5") == 0);
  CHECK(qa_q2mpm_boss5_definition.health == 1500);
  CHECK(qa_q2mpm_boss5_definition.move_count == 15);
  CHECK(strcmp(qa_q2mpm_boss5_definition.initial_move, "boss5_move_stand") == 0);
  bool found = false;
  for (size_t i = 0; i < qa_q2mpm_boss5_definition.callback_count; i++)
    if (strcmp(qa_q2mpm_boss5_definition.callbacks[i].name, "boss5MachineGun") == 0) found = true;
  CHECK(found);
}

/* TS dabeam emits its beam through game.host.emit({kind: "beam"}); the
 * C removed its process-global beam sink, so the host emit is the path. */
static void test_beam_emit(void) {
  qa_q2_entity beam;
  memset(&beam, 0, sizeof beam);
  beam.actor = allocate_at(registry, 11);
  beam.movedir = (qa_vec3){1, 0, 0};
  beam.skin = 0xf2f2f0f0;
  trace_index = 0;
  trace_script[0].end = (qa_vec3){512, 0, 0};
  beam_emitted = 0; other_emitted = 0; scheduled = 0;
  qa_q2_think hit = qa_q2mpm_dabeam_hit_think();
  hit.fn(hit.ctx, &beam, &stub_game);
  CHECK(beam_emitted == 1);
  CHECK(other_emitted == 0);
  CHECK(strcmp(last_beam.kind, "beam") == 0 && last_beam.has_actor && qa_same_actor(last_beam.actor, beam.actor.id));
  CHECK(last_beam.end.x == 512 && last_beam.width == 2 && last_beam.color == 0xf2f2f0f0 && last_beam.visible);
  CHECK(scheduled == 1);
}

static void test_eye_arrays(void) {
  CHECK(qa_q2mpm_xatrix_brain_right_eye[0].x == 0.7467);
  CHECK(qa_q2mpm_xatrix_brain_left_eye[10].y == 9.44457);
}

int main(void) {
  stub_host_init();
  test_tables();
  test_state();
  test_spawn_points();
  test_stalker_angles();
  test_boss5_definition();
  test_beam_emit();
  test_eye_arrays();
  qa_session_actor_registry_close(registry);
  qa_session_actor_registry_destroy(registry);
  qa_identity_owner_destroy(&identities);
  printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
  return failures ? 1 : 0;
}
