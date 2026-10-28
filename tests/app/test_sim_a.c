/* Port of key cases from TS tests/app + tests/gameplay for the
 * simulation/{q3,arsenal,prediction} slice: ammo labels, weapon
 * status/warning staging, presentation source command conversion,
 * command policy rewrite, prediction snapshot copy/source hit
 * mapping, qw profile/snapshot guards, guest world portal refs and
 * server-state configstrings. GPL-2.0-or-later. */
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/app/bootstrap/simulation/arsenal/inventory_labels.h"
#include "qa/app/bootstrap/simulation/arsenal/weapon_status.h"
#include "qa/app/bootstrap/simulation/prediction/presentation.h"
#include "qa/app/bootstrap/simulation/prediction/qw_source_state.h"
#include "qa/app/bootstrap/simulation/prediction/source_state.h"
#include "qa/app/bootstrap/simulation/prediction/step.h"
#include "qa/app/bootstrap/simulation/q3/command_policy.h"
#include "qa/app/bootstrap/simulation/q3/guest_world.h"
#include "qa/app/bootstrap/simulation/q3/server_state.h"

static int checks, failures;
#define CHECK(...) do { checks++; if (!(__VA_ARGS__)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); } } while (0)
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

static void test_labels(void) {
  CHECK(strcmp(qa_app_selected_ammo_label("q1", "q1:ammo/shells"), "Shells") == 0);
  CHECK(strcmp(qa_app_selected_ammo_label("q1", "rogue:ammo/plasma"), "Plasma") == 0);
  CHECK_ABORTS((void)qa_app_selected_ammo_label("q1", "q1:ammo/unknown"));
  char label[64];
  qa_app_q1_weapon_display_name("supershotgun", label, sizeof label);
  CHECK(strcmp(label, "Double-barrelled Shotgun") == 0);
  qa_app_q1_weapon_display_name("hipnotic:mjolnir", label, sizeof label);
  CHECK(strcmp(label, "Mjolnir") == 0);
  qa_app_q1_weapon_display_name("custom:rail_gun", label, sizeof label);
  CHECK(strcmp(label, "Rail Gun") == 0);
}

static double count_none(void *ctx, qa_item_id item) { (void)ctx; (void)item; return 0; }
static double count_owned(void *ctx, qa_item_id item) {
  (void)ctx;
  return strcmp(item, "q3:weapon/rocketlauncher") == 0 ? 1 : strcmp(item, "q3:ammo/rocketlauncher") == 0 ? 3 : 0;
}
static void test_warning(void) {
  CHECK(strcmp(qa_app_q3_arsenal_warning("baseq3", count_none, NULL), "empty") == 0);
  /* rocket launcher is slow: 3 rockets * 1000 = 3000 < 5000 -> low */
  CHECK(strcmp(qa_app_q3_arsenal_warning("baseq3", count_owned, NULL), "low") == 0);
  char label[64];
  qa_weapon_hud_status status;
  qa_provider_reference source = {"content:x", "q3:base"};
  CHECK(qa_app_q3_weapon_status("q3:weapon/rocketlauncher", "baseq3", count_owned, NULL, source, label, sizeof label, &status));
  CHECK(status.ammo_is_finite && status.ammo_count == 3 && status.has_ammo_to_start);
  CHECK(!qa_app_q3_weapon_status("q3:weapon/nope", "baseq3", count_owned, NULL, source, label, sizeof label, &status));
}

static void test_presentation_command(void) {
  qa_actor_command input; memset(&input, 0, sizeof input);
  input.command.kind = QA_USER_COMMAND_Q1;
  input.command.as.q1.view_angles = (qa_vec3){90, 180, 0};
  input.command.as.q1.forward_move = 320; input.command.as.q1.side_move = -320;
  input.command.as.q1.buttons = 3;
  qa_q3_base_user_command out = qa_app_presentation_source_command(input, 1234.5, 5);
  CHECK(out.server_time == 1234);
  CHECK(out.angles.x == 16384 && out.angles.y == 32768);
  CHECK(out.forwardmove == 127 && out.rightmove == -127);
  CHECK(out.buttons == 1 && out.weapon == 5);
  CHECK(out.upmove == 127); /* Q1 jump bit */
}

static void test_command_policy(void) {
  qa_actor_command pending; memset(&pending, 0, sizeof pending);
  pending.command.kind = QA_USER_COMMAND_Q1;
  pending.command.as.q1.forward_move = 100; pending.command.as.q1.buttons = 3; pending.command.as.q1.impulse = 7;
  qa_q3_base_user_command before; memset(&before, 0, sizeof before);
  qa_q3_base_user_command after = before; after.forwardmove = 50;
  qa_user_command converted = pending.command; converted.as.q1.forward_move = 42;
  qa_arsenal_intent storage;
  qa_actor_command frozen = qa_app_apply_q3_command_policy(pending, before, after, converted, true, &storage);
  CHECK(frozen.command.as.q1.forward_move == 0 && frozen.command.as.q1.buttons == 0 && frozen.command.as.q1.impulse == 0);
  qa_actor_command rewritten = qa_app_apply_q3_command_policy(pending, before, after, converted, false, &storage);
  CHECK(rewritten.command.as.q1.forward_move == 42);
  CHECK(rewritten.command.as.q1.buttons == 3); /* upmove unchanged: only attack bit sourced from converted */
}

static bool actor_at_none(void *ctx, double number, qa_actor_id *out) { (void)ctx; (void)number; (void)out; return false; }
static bool number_none(void *ctx, qa_actor_id actor, double *out) { (void)ctx; (void)actor; (void)out; return false; }
static void test_prediction(void) {
  qa_app_prediction_source_entities entities = {NULL, actor_at_none, number_none};
  CHECK(qa_app_prediction_source_hit(1022, entities).kind == QA_TRACE_HIT_WORLD);
  CHECK(qa_app_prediction_source_hit(1023, entities).kind == QA_TRACE_HIT_NONE);
  CHECK(qa_app_prediction_source_hit(5, entities).kind == QA_TRACE_HIT_NONE);
  qa_trace_hit world; memset(&world, 0, sizeof world); world.kind = QA_TRACE_HIT_WORLD;
  CHECK(qa_app_prediction_source_number(world, entities) == 1022);

  qa_app_movement_prediction_snapshot snapshot; memset(&snapshot, 0, sizeof snapshot);
  qa_inventory_entry ammo[1]; memset(ammo, 0, sizeof ammo); ammo[0].item = "q3:ammo/rocketlauncher"; ammo[0].count = 7; ammo[0].capacity = 200;
  snapshot.arsenal.ammo = ammo; snapshot.arsenal.ammo_count = 1;
  qa_app_movement_prediction_snapshot copy = qa_app_copy_prediction_snapshot(snapshot);
  CHECK(copy.arsenal.ammo != ammo && copy.arsenal.ammo[0].count == 7);
  ((qa_inventory_entry *)copy.arsenal.ammo)[0].count = 1;
  CHECK(ammo[0].count == 7);
  qa_app_movement_prediction_snapshot_destroy(&copy);

  qa_movement_profile base; memset(&base, 0, sizeof base);
  base.parameters.gravity = 999;
  qa_q1_movement_parameters variables; memset(&variables, 0, sizeof variables); variables.gravity = 800;
  qa_movement_profile profile = qa_app_qw_prediction_profile(base, variables);
  CHECK(strcmp(profile.kind, "q1-quakeworld") == 0 && profile.parameters.gravity == 800);
  CHECK(profile.clock.maximum_command_milliseconds == 50);

  qa_qw_player_state player; memset(&player, 0, sizeof player); player.flags = 1024;
  qa_app_qw_prediction_status spectator = {100, 1};
  qa_app_movement_prediction_snapshot qw_base; memset(&qw_base, 0, sizeof qw_base);
  qw_base.state.kind = QA_MOVEMENT_STATE_QW;
  CHECK_ABORTS((void)qa_app_qw_prediction_snapshot(qw_base, player, spectator, NULL, 1, 2));
  qa_app_qw_prediction_status alive = {50, 0};
  qa_app_movement_prediction_snapshot ducked = qa_app_qw_prediction_snapshot(qw_base, player, alive, NULL, 3, 40);
  CHECK(ducked.view_height == 8 && ducked.environment.health == 50 && ducked.sequence == 3);
  qa_app_movement_prediction_snapshot_destroy(&ducked);
}

static void print_sink(void *ctx, const char *text) { (void)ctx; (void)text; }
static void test_server_state(void) {
  qa_app_q3_server_state_options options; memset(&options, 0, sizeof options);
  options.session.name = "test"; options.session.token = "test";
  options.settings.game_type = 0; options.settings.max_clients = 8; options.settings.map_name = "q3dm17";
  options.ctx = NULL; options.print = print_sink;
  qa_app_q3_server_state *state = qa_app_q3_server_state_create(options);
  CHECK(state != NULL);
  qa_app_q3_server_state_configstring_set(state, 544, "hero");
  CHECK(strcmp(qa_app_q3_server_state_configstring_get(state, 544), "hero") == 0);
  CHECK_ABORTS(qa_app_q3_server_state_configstring_set(state, 1024, "x"));
  qa_app_q3_server_state_set_userinfo(state, 2, "\\name\\A");
  CHECK(strcmp(qa_app_q3_server_state_get_userinfo(state, 2), "\\name\\A") == 0);
  qa_q3_base_user_command command; memset(&command, 0, sizeof command); command.weapon = 3;
  qa_app_q3_server_state_set_user_command(state, 2, command);
  qa_q3_base_user_command read_back;
  CHECK(qa_app_q3_server_state_get_user_command(state, 2, &read_back) && read_back.weapon == 3);
  qa_app_q3_server_state_clear_client(state, 2);
  CHECK(qa_app_q3_server_state_get_userinfo(state, 2) == NULL);
  qa_save_value *saved = qa_app_q3_server_state_capture_save_state(state);
  CHECK(saved != NULL);
  qa_app_q3_server_state_configstring_set(state, 544, "changed");
  qa_app_q3_server_state_restore_save_state(state, saved);
  CHECK(strcmp(qa_app_q3_server_state_configstring_get(state, 544), "hero") == 0);
  qa_save_value_free(saved);
  qa_app_q3_server_state_destroy(state);
}

static void test_guest_world(void) {
  /* Zeroed scene: Q1-kind geometry makes portal adjust a no-op and
   * the native clip-model probe returns NULL. */
  qa_shared_scene_queries storage; memset(&storage, 0, sizeof storage);
  storage.geometry.kind = QA_DECODED_WORLD_Q1;
  qa_shared_scene_queries *scene = &storage;
  qa_app_q3_guest_world *guest = qa_app_q3_guest_world_create(scene);
  /* Q1 geometry: portal adjust is a no-op, close must not abort. */
  qa_app_q3_guest_world_adjust_area_portal_state(guest, 1, 2, true);
  qa_app_q3_guest_world_close(guest);
  qa_save_value *checkpoint = qa_app_q3_guest_world_capture_portal_checkpoint(guest);
  CHECK(checkpoint != NULL);
  qa_save_value_free(checkpoint);
  qa_app_q3_guest_world_destroy(guest);
}

int main(void) {
  test_labels();
  test_warning();
  test_presentation_command();
  test_command_policy();
  test_prediction();
  test_server_state();
  test_guest_world();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
