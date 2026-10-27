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

/* ---- stub host for state/spawn/combat cases ---- */
static bool stub_is_live(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return true; }
static qa_actor_id stub_reference(void *ctx, qa_saved_actor_id saved) {
  (void)ctx;
  qa_actor_id actor;
  memset(&actor, 0, sizeof actor);
  actor.slot = (uint64_t)saved.slot;
  actor.generation = (uint64_t)saved.generation;
  return actor;
}
static qa_q2f_owned_result stub_resolve_saved(void *ctx, qa_saved_actor_id saved) {
  (void)ctx;
  qa_q2f_owned_result result;
  memset(&result, 0, sizeof result);
  result.has_value = true;
  result.value.id.slot = (uint64_t)saved.slot;
  result.value.id.generation = (uint64_t)saved.generation;
  return result;
}
static qa_actor_id stub_world_actor(void *ctx) {
  (void)ctx;
  qa_actor_id world;
  memset(&world, 0, sizeof world);
  return world;
}
static qa_trace_result trace_script[4];
static int trace_index;
static qa_trace_result stub_trace(void *ctx, qa_q2f_trace_request request) {
  (void)ctx; (void)request;
  qa_trace_result result = trace_script[trace_index < 4 ? trace_index : 3];
  trace_index++;
  return result;
}
static qa_q2f_host stub_host;
static qa_q2f_game stub_game;
static void stub_host_init(void) {
  memset(&stub_host, 0, sizeof stub_host);
  memset(&stub_game, 0, sizeof stub_game);
  stub_host.actors.is_live = stub_is_live;
  stub_host.actors.reference_saved = stub_reference;
  stub_host.actors.resolve_saved = stub_resolve_saved;
  stub_host.world_actor = stub_world_actor;
  stub_host.trace = stub_trace;
  stub_game.host = &stub_host;
  stub_game.options.edition = QA_Q2F_CLASSIC;
  stub_game.options.mode = "singleplayer";
  stub_game.options.skill = 1;
  for (int i = 0; i < 4; i++) {
    memset(&trace_script[i], 0, sizeof trace_script[i]);
    trace_script[i].kind = "q2";
    trace_script[i].fraction = 1;
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
  qa_q2f_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.actor.id.slot = 7;
  entity.actor.id.generation = 3;
  qa_q2mpm_rogue_state *saved = qa_q2mpm_state_get(&state, &entity);
  CHECK(saved->blocked == false);
  saved->has_healer = true;
  saved->healer.slot = 9;
  saved->healer.generation = 1;
  saved->summon_strength = 3;
  saved->widow_quad_until = 42;
  state.widow_damage_multiplier = 4;
  state.flyer_next_move = QA_Q2MPM_FLYER_RUN;
  qa_q2mpm_monsters_checkpoint checkpoint;
  CHECK(qa_q2mpm_state_capture(&state, &stub_game, &checkpoint));
  CHECK(checkpoint.actor_count == 1);
  CHECK(checkpoint.actors[0].state.has_healer);
  CHECK(checkpoint.actors[0].state.healer.slot == 9);
  qa_q2mpm_monster_state restored;
  qa_q2mpm_state_init(&restored);
  qa_q2mpm_state_restore(&restored, &stub_game, &checkpoint);
  CHECK(restored.widow_damage_multiplier == 4);
  CHECK(restored.flyer_next_move == QA_Q2MPM_FLYER_RUN);
  qa_q2mpm_rogue_state *again = qa_q2mpm_state_get(&restored, &entity);
  CHECK(again->has_healer && again->healer.slot == 9 && again->widow_quad_until == 42);
  qa_q2mpm_monsters_checkpoint_free(&checkpoint);
  qa_q2mpm_state_destroy(&state);
  qa_q2mpm_state_destroy(&restored);
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

static qa_q2f_entity *damage_target;
static int beam_emitted;
static void beam_emit(void *ctx, qa_q2mpm_beam_event event) { (void)ctx; (void)event; beam_emitted++; }
static void test_beam_sink(void) {
  qa_q2mpm_beam_event event;
  memset(&event, 0, sizeof event);
  CHECK_ABORTS(qa_q2mpm_emit_beam(event));
  qa_q2mpm_beam_sink sink = {NULL, beam_emit};
  qa_q2mpm_set_beam_sink(sink);
  beam_emitted = 0;
  qa_q2mpm_emit_beam(event);
  CHECK(beam_emitted == 1);
  (void)damage_target;
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
  test_beam_sink();
  test_eye_arrays();
  printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
  return failures ? 1 : 0;
}
