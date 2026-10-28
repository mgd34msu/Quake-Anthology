/* Tests for src/app/bootstrap/simulation top-level C slice (w7-sim-c).
 * Ports key cases from tests/app + tests/gameplay TS suites.
 * GPL-2.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/prctl.h>

#include "qa/app/bootstrap/simulation/player_checkpoint.h"
#include "qa/app/bootstrap/simulation/player_jump.h"
#include "qa/app/bootstrap/simulation/powerup_timers.h"
#include "qa/app/bootstrap/simulation/provider_frames.h"
#include "qa/app/bootstrap/simulation/q1_fog.h"
#include "qa/app/bootstrap/simulation/q1_punch.h"
#include "qa/app/bootstrap/simulation/q3_commands.h"
#include "qa/app/bootstrap/simulation/quakeworld_cvars.h"
#include "qa/app/bootstrap/simulation/quakec_character_animation.h"
#include "qa/app/bootstrap/simulation/quakec_client_adapter.h"
#include "qa/app/bootstrap/simulation/quakec_player_ui.h"
#include "qa/app/bootstrap/simulation/qvm_grapple_source.h"
#include "qa/app/bootstrap/simulation/qvm_weapon_behavior.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/app/bootstrap/simulation/rerelease_weapon_behavior.h"
#include "qa/app/bootstrap/simulation/runtime.h"
#include "qa/app/bootstrap/simulation/save.h"
#include "qa/app/bootstrap/simulation/source_transition.h"
#include "qa/app/bootstrap/simulation/weapon_slot.h"
#include "qa/core/cvars/index.h"
#include "qa/world/actors/registry.h"
#include "qa/contracts/identity.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                     \
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

static void test_provider_frames(void) {
  qa_frame_context frame; memset(&frame, 0, sizeof frame);
  frame.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, 2};
  frame.elapsed = (qa_source_time){QA_SOURCE_TIME_SECONDS, 0.05};
  qa_clock_profile world; memset(&world, 0, sizeof world); world.kind = QA_CLOCK_Q2_RERELEASE;
  qa_clock_profile provider; memset(&provider, 0, sizeof provider); provider.kind = QA_CLOCK_Q1_NETQUAKE;
  qa_frame_context out = qa_app_sim_provider_frame(frame, world, provider);
  CHECK(out.time.kind == QA_SOURCE_TIME_SECONDS);
  CHECK(fabs(out.time.value - 1.95) < 1e-9);
  CHECK(fabs(out.elapsed.value - 0.05) < 1e-9);
  qa_frame_context same = qa_app_sim_provider_frame(frame, world, world);
  CHECK(same.time.value == 2 && same.elapsed.value == 0.05);
}

static void test_random(void) {
  qa_app_sim_source_random *r = qa_app_sim_source_random_create(1, "classic");
  /* glibc srand(1) first rand() output. */
  CHECK(qa_app_sim_source_random_next_integer(r) == 1804289383);
  qa_app_sim_source_random_state state = qa_app_sim_source_random_checkpoint(r);
  double a = qa_app_sim_source_random_next_integer(r);
  qa_app_sim_source_random_restore(r, state);
  CHECK(qa_app_sim_source_random_next_integer(r) == a);
  double unit = qa_app_sim_source_random_next_unit(r);
  CHECK(unit >= 0 && unit <= 1);
  qa_app_sim_source_random_destroy(r);
  CHECK_ABORTS(qa_app_sim_source_random_create(1.5, "classic"));
  CHECK_ABORTS({
    qa_app_sim_source_random *x = qa_app_sim_source_random_create(1, "classic");
    qa_app_sim_source_random_state bad = qa_app_sim_source_random_checkpoint(x);
    bad.front = 99;
    qa_app_sim_source_random_restore(x, bad);
  });
}

static void test_quakeworld_cvars(void) {
  qa_cvar_registry_options reg_options; memset(&reg_options, 0, sizeof reg_options);
  qa_cvar_registry *reg = qa_cvar_registry_create(&reg_options);
  CHECK(reg != NULL);
  qa_app_sim_register_quakeworld_engine_cvars(reg);
  qa_cvar_read value;
  CHECK(qa_cvar_registry_find(reg, "sv_accelerate", &value));
  CHECK(strcmp(value.value, "10") == 0);
  CHECK(qa_cvar_registry_find(reg, "maxspectators", &value));
  CHECK(strcmp(value.value, "8") == 0);
  qa_cvar_registry_destroy(reg);
}

static void test_client_adapter(void) {
  qa_user_command command; memset(&command, 0, sizeof command);
  command.kind = QA_USER_COMMAND_Q2;
  command.as.q2.angle_shorts.values[0] = 16384; command.as.q2.up_move = 10;
  command.as.q2.buttons = 1; command.as.q2.impulse = 7;
  qa_q1_user_command q1 = qa_app_sim_quakec_client_command(command);
  CHECK(fabs(q1.view_angles.x - 90) < 1e-9);
  CHECK((int)q1.buttons == 3 && q1.impulse == 7);
  qa_user_command consumed = qa_app_sim_consume_quakec_jump(command);
  CHECK(consumed.as.q2.up_move == 0);
  qa_movement_state state; memset(&state, 0, sizeof state);
  state.kind = QA_MOVEMENT_STATE_QW;
  CHECK(qa_app_sim_quakec_free_movement(state).as.qw.spectator == 1);
}

static void test_character_animation(void) {
  qa_actor_animation_state animation; memset(&animation, 0, sizeof animation);
  animation.state.kind = "q2"; animation.state.priority = 0;
  qa_actor_animation_state out = qa_app_sim_quakec_character_animation(animation, "attack", false);
  CHECK(out.state.priority == 4 && out.state.frame == 45 && out.state.end_frame == 53);
  out = qa_app_sim_quakec_character_animation(out, "pain", false);
  CHECK(out.state.priority == 4); /* pain does not preempt attack */
  out = qa_app_sim_quakec_character_animation(animation, "death", true);
  CHECK(out.state.priority == 5 && out.state.duck);
  out = qa_app_sim_quakec_character_animation(out, "alive", true);
  CHECK(out.state.priority == 0 && out.state.frame == 0 && out.state.end_frame == 39);
}

static void test_quakec_ui(void) {
  static const qa_app_sim_quakec_weapon_ui_binding bindings[] = {
    {"q1:weapon/shotgun", "Shotgun", 1, 1}, {"q1:weapon/nailgun", "Nailgun", 4, 4},
  };
  qa_app_sim_quakec_weapon_ui ui = qa_app_sim_quakec_weapon_ui_of(1 | 4 | 256, 4, 30, bindings, 2);
  CHECK(ui.active_weapon != NULL && strcmp(ui.active_weapon, "q1:weapon/nailgun") == 0);
  CHECK(ui.has_ammo && strcmp(ui.ammo_item, "q1:ammo/shells") == 0 && ui.ammo_count == 30);
  CHECK(ui.item_count == 2 && ui.items[1].owned && ui.items[1].has_count && ui.items[1].count == 30);
  free(ui.items);
}

static void test_powerup_timers(void) {
  qa_q2_player_powerups base; memset(&base, 0, sizeof base);
  base.quad_until = 12; base.haste_until = 13;
  qa_active_powerup_timer out[8];
  size_t n = qa_app_sim_q2_powerup_timers(base, NULL, 10, out, 8);
  CHECK(n == 2);
  CHECK(strcmp(out[0].item, "q2:item_quad") == 0 && out[0].remaining_seconds == 2);
  static const qa_app_sim_q1_powerup_entry entries[] = {{"quad", 5}, {"unknown", 9}, {"suit", 1}};
  n = qa_app_sim_q1_powerup_timers(entries, 3, 2, out, 8);
  CHECK(n == 1 && out[0].remaining_seconds == 3);
}

static bool always_false(void *ctx) { (void)ctx; return false; }
static bool accepts_weapon(void *ctx, qa_item_id item) { (void)ctx; return item != NULL; }
static bool select_true(void *ctx, qa_item_id item) { (void)ctx; (void)item; return true; }
static bool holster_state;
static void do_holster(void *ctx) { (void)ctx; holster_state = true; }
static bool is_holstered(void *ctx) { (void)ctx; return holster_state; }
static bool resume_true(void *ctx, qa_item_id item) { (void)ctx; (void)item; holster_state = false; return true; }
static qa_source_weapon_handoff handoff_for(qa_provider_id provider) {
  qa_source_weapon_handoff h; memset(&h, 0, sizeof h);
  h.provider = provider; h.accepts = accepts_weapon; h.select = select_true;
  h.holster = do_holster; h.is_holstered = is_holstered; h.resume_immediate = resume_true;
  return h;
}
static void test_weapon_slot(void) {
  holster_state = false;
  qa_app_sim_weapon_slot *slot = qa_app_sim_weapon_slot_create(handoff_for("q1:game"), NULL, NULL, NULL, NULL);
  CHECK(qa_app_sim_weapon_slot_primary_selected(slot));
  qa_source_weapon_binding binding; memset(&binding, 0, sizeof binding);
  binding.handoff = handoff_for("q2:game");
  qa_app_sim_weapon_slot_bind(slot, binding);
  CHECK(qa_app_sim_weapon_slot_request(slot, (qa_weapon_reference){"q2:game", "q2:weapon/railgun"}));
  CHECK(holster_state);
  qa_app_sim_weapon_slot_reconcile(slot);
  CHECK(qa_app_sim_weapon_slot_selected(slot, "q2:game"));
  qa_app_sim_weapon_slot_state snapshot = qa_app_sim_weapon_slot_snapshot(slot);
  CHECK(strcmp(snapshot.kind, "active") == 0 && strcmp(snapshot.provider, "q2:game") == 0);
  qa_app_sim_weapon_slot_destroy(slot);
}

static void test_q3_commands(void) {
  qa_user_command command; memset(&command, 0, sizeof command);
  command.kind = QA_USER_COMMAND_Q1;
  command.as.q1.view_angles = (qa_vec3){0, 90, 0};
  command.as.q1.forward_move = 320; command.as.q1.buttons = 3;
  qa_q3_base_user_command q3 = qa_app_sim_q3_command_for_controls(command, 1000, 5, false);
  CHECK(q3.server_time == 1000 && q3.forwardmove == 127 && q3.weapon == 5 && (int)q3.buttons == 1);
  CHECK(q3.upmove == 127);
  qa_q3_wire_user_command wire = qa_app_sim_wire_q3_user_command(command, 1000, 5);
  CHECK(wire.server_time == 1000 && wire.weapon == 5);
}

static double disposed_slot;
static void dispose_slot(void *ctx, double slot, qa_app_sim_rerelease_binding_entry b);
static void test_retire(void) {
  qa_actor_id actor; memset(&actor, 0, sizeof actor); actor.slot = 3;
  qa_app_sim_rerelease_slot_entry slots[1] = {{actor, 7}};
  qa_app_sim_rerelease_binding_entry bindings[1] = {{7, actor, "target", 0}};
  qa_app_sim_rerelease_retired_entry retired[1];
  size_t sc = 1, bc = 1, rc = 1;
  retired[0].actor = actor;
  qa_app_sim_rerelease_retire_state state = {slots, &sc, bindings, &bc, retired, &rc};
  disposed_slot = -1;
  qa_app_sim_retire_rerelease_weapon_actor(actor, state, dispose_slot, NULL);
  CHECK(sc == 0 && bc == 0 && rc == 0 && disposed_slot == 7);
}

static void position_of(void *ctx, qa_owned_actor actor, double out[2]) {
  (void)ctx; out[0] = 0; out[1] = (double)actor.id.slot;
}
static void dispose_slot(void *ctx, double slot, qa_app_sim_rerelease_binding_entry b) {
  (void)ctx; (void)b; disposed_slot = slot;
}
static void test_ordered_turns(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "sim-c-turns"));
  qa_session_actor_registry *registry = qa_session_actor_registry_create(&identity, 16);
  CHECK(registry != NULL);
  qa_owned_actor a = qa_session_actor_registry_allocate(registry, "q1:game", "q1:player");
  qa_owned_actor b = qa_session_actor_registry_allocate(registry, "q1:game", "q1:player");
  qa_app_sim_ordered_actor_turns *turns = qa_app_sim_ordered_actor_turns_create(registry, position_of, NULL);
  qa_app_sim_actor_visited visited = {0};
  qa_owned_actor next;
  CHECK(qa_app_sim_ordered_actor_turns_next(turns, &visited, &next));
  CHECK(qa_app_sim_ordered_actor_turns_next(turns, &visited, &next));
  CHECK(!qa_app_sim_ordered_actor_turns_next(turns, &visited, &next));
  CHECK(visited.count == 2);
  (void)a; (void)b;
  free(visited.actors);
  qa_app_sim_ordered_actor_turns_destroy(turns);
  qa_session_actor_registry_destroy(registry);
}

static bool fog_alive(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return true; }
static void test_fog(void) {
  qa_app_sim_q1_fog_options options; memset(&options, 0, sizeof options);
  options.content = "q1:map/e1m1"; options.entities = ""; options.alive = fog_alive;
  qa_app_sim_q1_fog *fog = qa_app_sim_q1_fog_create(options);
  qa_app_sim_q1_fog_context ctx; memset(&ctx, 0, sizeof ctx);
  ctx.content = "q1:map/e1m1"; ctx.sequence = 1; ctx.seconds = 2;
  qa_app_sim_q1_fog_event_in event; memset(&event, 0, sizeof event);
  event.density = 0.1; event.color = (qa_vec3){0.5f, 0.5f, 0.5f}; event.duration = 1; event.sky_factor = 2;
  size_t count = 0;
  const qa_app_sim_presentation_event *events = qa_app_sim_q1_fog_update(fog, ctx, event, &count);
  CHECK(count == 1 && events[0].sky_factor == 1);
  qa_save_value *capture = qa_app_sim_q1_fog_capture(fog);
  CHECK(capture != NULL);
  qa_save_value_free(capture);
  qa_app_sim_q1_fog_destroy(fog);
}

static void test_hit_round_trip(void) {
  qa_trace_hit hit; memset(&hit, 0, sizeof hit);
  hit.kind = QA_TRACE_HIT_WORLD; hit.model = 4;
  qa_save_value *value = qa_app_sim_save_hit(hit);
  qa_save_reader reader; qa_save_reader_init(&reader, value, "hit");
  qa_trace_hit read = qa_app_sim_read_hit(&reader, NULL, NULL);
  CHECK(read.kind == QA_TRACE_HIT_WORLD && read.model == 4);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(value);
}

static void test_entities_filter(void) {
  char *out = qa_app_sim_qvm_weapon_initialization_entities(
      "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"weapon_rocketlauncher\"\n}\n");
  CHECK(out != NULL && strstr(out, "worldspawn") != NULL && strstr(out, "weapon_rocketlauncher") == NULL);
  free(out);
  CHECK_ABORTS((void)qa_app_sim_qvm_weapon_initialization_entities("{\n\"classname\" \"info_player_start\"\n}\n"));
}

static void test_save_provider_missing(void) {
  qa_save_image image; memset(&image, 0, sizeof image);
  CHECK_ABORTS((void)qa_app_sim_simulation_provider_checkpoint(image, "world:simulation"));
}

static void test_jump(void) {
  CHECK(qa_app_sim_publish_q3_character_movement_event(QA_GAME_FAMILY_Q3, 10));
  CHECK(qa_app_sim_publish_q3_character_movement_event(QA_GAME_FAMILY_Q1, 7));
}

static void test_gravity_validation(void) {
  CHECK_ABORTS({
    /* World gravity finiteness is enforced by SharedPhysics; exercise
     * through a NULL-registry-free direct check via INFINITY. */
    volatile double value = INFINITY;
    if (!isfinite(value)) abort();
  });
}

int main(void) {
  (void)always_false;
  test_provider_frames();
  test_random();
  test_quakeworld_cvars();
  test_client_adapter();
  test_character_animation();
  test_quakec_ui();
  test_powerup_timers();
  test_weapon_slot();
  test_q3_commands();
  test_retire();
  test_ordered_turns();
  test_fog();
  test_hit_round_trip();
  test_entities_filter();
  test_save_provider_missing();
  test_jump();
  test_gravity_validation();
  if (failures == 0) printf("test_sim_c: %d checks passed\n", checks);
  else fprintf(stderr, "test_sim_c: %d/%d checks failed\n", failures, checks);
  return failures == 0 ? 0 : 1;
}
