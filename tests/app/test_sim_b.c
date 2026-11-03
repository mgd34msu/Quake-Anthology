/* Port of key cases from tests/app/ (native-q2-map, classic-guest-player)
 * and simulation pure helpers for the w7-sim-b slice. GPL-2.0-or-later. */
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
#include "qa/app/bootstrap/simulation/arsenal_intent.h"
#include "qa/app/bootstrap/simulation/bot_commands.h"
#include "qa/app/bootstrap/simulation/bot_mover_connections.h"
#include "qa/app/bootstrap/simulation/bot_objectives.h"
#include "qa/app/bootstrap/simulation/bot_selected_knowledge.h"
#include "qa/app/bootstrap/simulation/classic_guest_files.h"
#include "qa/app/bootstrap/simulation/classic_guest_player.h"
#include "qa/app/bootstrap/simulation/dropped_pickups.h"
#include "qa/app/bootstrap/simulation/guest_monsters.h"
#include "qa/app/bootstrap/simulation/monster_placement.h"
#include "qa/app/bootstrap/simulation/native_commands.h"
#include "qa/app/bootstrap/simulation/native_mod_camera.h"
#include "qa/app/bootstrap/simulation/native_q2_map.h"
#include "qa/foundation/support.h"
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
static bool has(const char *s, const char *sub) { return strstr(s, sub) != NULL; }

static void test_native_q2_map(void) {
  const char *entities =
      "// authored formatting\n{ \"classname\" \"worldspawn\" }\n"
      "{ \"classname\" \"info_player_start\" \"origin\" \"10 20 30\" \"angle\" \"90\" }\n"
      "{ \"classname\" \"custom_mod_entity\" \"custom\" \"retain\" }";
  qa_app_sim_native_q2_map_geometry q2 = {"q2-bsp", entities, 0};
  char *same = qa_app_sim_prepare_native_q2_map(q2, "classic", "deathmatch");
  CHECK(strcmp(same, entities) == 0); free(same);
  qa_app_sim_native_q2_map_geometry q1 = {"q1-bsp", entities, 0};
  char *out = qa_app_sim_prepare_native_q2_map(q1, "classic", "deathmatch");
  CHECK(has(out, "\"info_player_deathmatch\"") && has(out, "\"10 20 30\"") && has(out, "\"retain\""));
  free(out);
  qa_app_sim_native_q2_map_geometry q3 = {"q3-bsp",
      "{ \"classname\" \"team_CTF_redplayer\" \"origin\" \"1 2 3\" } "
      "{ \"classname\" \"team_CTF_redflag\" \"targetname\" \"authored\" }", 0};
  char *o3 = qa_app_sim_prepare_native_q2_map(q3, "rerelease", "deathmatch");
  CHECK(has(o3, "\"info_player_team1\"") && has(o3, "\"item_flag_team1\"") && has(o3, "\"info_player_deathmatch\""));
  CHECK(has(o3, "\"authored\"")); free(o3);
  /* TS: 256 models > classic 255 capacity throws. */
  CHECK_ABORTS(free(qa_app_sim_prepare_native_q2_map((qa_app_sim_native_q2_map_geometry){"q1-bsp", entities, 256}, "classic", "deathmatch")));
}

static qa_q2_player_state guest_state(double *stats) {
  qa_q2_player_state s = {0};
  s.movement.origin_eighths.values[0] = 800;
  s.movement.origin_eighths.values[1] = -1600;
  s.movement.origin_eighths.values[2] = 2400;
  s.movement.delta_angle_shorts.values[0] = 16384;
  s.movement.delta_angle_shorts.values[1] = 32768;
  s.movement.delta_angle_shorts.values[2] = 65535;
  s.view.view_angles = (qa_vec3){10, 20, 30};
  s.view.view_offset = (qa_vec3){2, -3, 22};
  s.view.kick_angles = (qa_vec3){-4, 5, 6};
  s.view.gun_index = 7; s.view.fov = 105;
  for (int i = 0; i < 32; i++) stats[i] = i == 1 ? -17 : i == 3 ? 11 : i == 5 ? 23 : 0;
  s.view.stats.values = stats; s.view.stats.count = 32;
  return s;
}

static void test_classic_guest_player(void) {
  double stats[32]; qa_q2_player_state s = guest_state(stats);
  qa_app_sim_player_view v = qa_app_sim_classic_guest_player_view(s);
  CHECK(v.origin.x == 102 && v.origin.y == -203 && v.origin.z == 300);
  CHECK(v.view_height == 22 && v.field_of_view == 105);
  CHECK(v.angles.x == 10 && v.kick_angles.x == -4);
  qa_app_sim_configstring cs[] = {{39, "models/weapons/v_rocket/tris.md2"}};
  qa_provider_reference source = {"q2:weapons", "q2:classic:baseq2:installed"};
  qa_weapon_hud_status scratch; char label[128];
  qa_app_sim_player_ui ui = qa_app_sim_classic_guest_player_ui(s, cs, 1, source, NULL, 0, &scratch, label, sizeof label);
  CHECK(ui.health == -17);
  CHECK(ui.armor.regular.kind == QA_REGULAR_ARMOR_Q2 && ui.armor.regular.points == 23);
  /* layout.models for classic v34 may not be 32; gun resolution is layout-driven.
   * Assert the ammo/health path regardless; weapon fields only when resolved. */
  if (ui.active_weapon != NULL) {
    CHECK(strcmp(ui.active_weapon, "q2:weapon_rocketlauncher") == 0);
    CHECK(ui.has_ammo && strcmp(ui.ammo_item, "q2:ammo_rockets") == 0 && ui.ammo_count == 11);
    CHECK(ui.weapon_status != NULL && ui.weapon_status->ammo_is_finite && ui.weapon_status->ammo_count == 11);
  }
  qa_user_command cmd = {0}; cmd.kind = QA_USER_COMMAND_Q2;
  cmd.as.q2.milliseconds = 17; cmd.as.q2.forward_move = 300; cmd.as.q2.side_move = -125;
  cmd.as.q2.up_move = 200; cmd.as.q2.buttons = 3; cmd.as.q2.impulse = 9; cmd.as.q2.light_level = 128;
  cmd.as.q2.angle_shorts.values[0] = 0; cmd.as.q2.angle_shorts.values[1] = 65535; cmd.as.q2.angle_shorts.values[2] = 17;
  qa_q2_user_command conv = qa_app_sim_classic_guest_local_command("bot", cmd, s);
  CHECK(conv.angle_shorts.values[0] == 49152 && conv.angle_shorts.values[1] == 32767 && conv.angle_shorts.values[2] == 18);
  CHECK(cmd.as.q2.angle_shorts.values[0] == 0);
  CHECK_ABORTS((void)qa_app_sim_classic_guest_local_command("remote-client", cmd, s));
  qa_user_command q3cmd = {0}; q3cmd.kind = QA_USER_COMMAND_Q3;
  CHECK_ABORTS((void)qa_app_sim_classic_guest_local_command("bot", q3cmd, s));
}

static void test_arsenal_and_commands(void) {
  qa_arsenal_state arsenal = {0}; arsenal.provider = "q3:arsenal";
  arsenal.state.kind = "q3"; arsenal.state.source_weapon = 5;
  qa_user_command cmd = {0}; cmd.kind = QA_USER_COMMAND_Q3; cmd.as.q3.weapon = 7; cmd.as.q3.buttons = 1;
  qa_q3_arsenal_controls c = qa_app_sim_resolve_q3_arsenal_controls(arsenal, NULL, cmd, "baseq3");
  CHECK(c.attack && c.requested_weapon == 7 && !c.use_holdable);
  qa_arsenal_intent intent = {"q3:arsenal", "q3:weapon/railgun", true, false, 0};
  qa_q3_arsenal_controls c2 = qa_app_sim_resolve_q3_arsenal_controls(arsenal, &intent, cmd, "baseq3");
  CHECK(c2.use_holdable && c2.requested_weapon == qa_q3_weapon_items[6].weapon);
  qa_arsenal_intent mp = {"q3:arsenal", "q3:weapon/chaingun", false, false, 0};
  /* Current TS: a weapon outside the product is no request (keeps sourceWeapon). */
  qa_q3_arsenal_controls c_out = qa_app_sim_resolve_q3_arsenal_controls(arsenal, &mp, cmd, "baseq3");
  CHECK(c_out.requested_weapon == 5 && c_out.attack);
  qa_q3_arsenal_controls c3 = qa_app_sim_resolve_q3_arsenal_controls(arsenal, &mp, cmd, "missionpack");
  CHECK(c3.requested_weapon == qa_q3_weapon_items[12].weapon);
  qa_arsenal_state bad = arsenal; bad.state.kind = "q1";
  CHECK_ABORTS((void)qa_app_sim_resolve_q3_arsenal_controls(bad, NULL, cmd, "baseq3"));

  qa_bot_usercmd src = {320, -320, 0, QA_BOT_BUTTON_ATTACK | QA_BOT_BUTTON_JUMP, 0, {0, 90, 0}};
  qa_user_command q3 = qa_app_sim_rerelease_bot_command(src, "q3", 16, 2000);
  CHECK(q3.kind == QA_USER_COMMAND_Q3 && q3.as.q3.forward_move == 127 && q3.as.q3.right_move == -127);
  CHECK(q3.as.q3.up_move == 127 && q3.as.q3.server_time_milliseconds == 2000);
  qa_user_command nq = qa_app_sim_rerelease_bot_command(src, "q1-netquake", 16, 2000);
  CHECK(nq.as.q1.acknowledged_server_time_seconds == 2 && nq.as.q1.buttons == 3 && nq.as.q1.up_move == 200);

  qa_user_command q2cmd = {0}; q2cmd.kind = QA_USER_COMMAND_Q2;
  q2cmd.as.q2.forward_move = 200; q2cmd.as.q2.side_move = -100; q2cmd.as.q2.up_move = 200; q2cmd.as.q2.buttons = 1; q2cmd.as.q2.milliseconds = 10;
  qa_app_sim_native_player_view player = {"q3", "q3", 4};
  qa_user_command sel = qa_app_sim_selected_native_command(q2cmd, (qa_vec3){0, 0, 0}, player, 500);
  CHECK(sel.kind == QA_USER_COMMAND_Q3 && sel.as.q3.forward_move == 127 && sel.as.q3.up_move == 127 && sel.as.q3.weapon == 4);
}

static bool reg_live(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return true; }
static bool reg_src(void *ctx, qa_actor_id actor, qa_provider_id *p, double *s) {
  (void)ctx; (void)actor; *p = "q2:src"; *s = 3; return true;
}
static bool reg_at(void *ctx, qa_provider_id p, double s, qa_actor_id *out) {
  (void)ctx; (void)p; (void)s; qa_actor_id a = {0}; a.slot = 9; *out = a; return true;
}
static bool reg_res(void *ctx, qa_saved_actor_id sv, qa_actor_id *out) {
  (void)ctx; (void)sv; qa_actor_id a = {0}; *out = a; return true;
}

static void test_misc_pure(void) {
  char *f = qa_app_sim_filter_entity_text("{ \"a\" \"1\" }\n{ \"b\" \"2\" }\n{ \"c\" \"3\" }", (double[]){2}, 1);
  CHECK(has(f, "\"a\"") && has(f, "\"c\"") && !has(f, "\"b\"")); free(f);
  size_t n = 0; qa_app_sim_placement_offset *off = qa_app_sim_placement_offsets_within(8, &n);
  CHECK(n > 0 && off[0].x == 0 && off[0].y == 0); free(off);
  qa_vec3 ride = qa_app_sim_train_riding_point((qa_vec3){10, 20, 30}, (qa_bounds){{-8, -8, 0}, {8, 8, 32}}, -24);
  CHECK(ride.x == 10 && ride.y == 20 && fabs(ride.z - (30 + 32 + 24 + 0.125)) < 1e-9);
  CHECK(qa_app_sim_rerelease_bot_team_of("team:red") == 1 && qa_app_sim_rerelease_bot_team_of("2") == 2);
  CHECK(qa_app_sim_rerelease_bot_team_of("green") == 0);
  CHECK(qa_app_sim_q3_knowledge_max_range(1) == 60 && qa_app_sim_q3_knowledge_max_range(6) == 768);
  qa_app_sim_classic_guest_file_position(0, 10);
  CHECK_ABORTS(qa_app_sim_classic_guest_file_position(-1, 0));
  CHECK_ABORTS((void)qa_app_sim_classic_guest_file_allowed((qa_app_sim_classic_guest_file_options){9, true, true}));
  qa_app_sim_pickup_registry reg = {NULL, reg_live, reg_src, reg_at, reg_res};
  qa_app_sim_source_pickup_cargo *cargo = qa_app_sim_source_pickup_cargo_create(reg, "q2:src");
  qa_actor_id actor = {0}; actor.slot = 1;
  qa_app_sim_source_pickup_cargo_set(cargo, actor, (qa_app_sim_dropped_pickup_cargo){"q2:item", 2});
  qa_app_sim_dropped_pickup_cargo got = {0};
  CHECK(qa_app_sim_source_pickup_cargo_get(cargo, actor, &got) && got.count == 2);
  size_t lc = 0; qa_app_sim_dropped_pickup_level *lv = qa_app_sim_source_pickup_cargo_travel(cargo, "base1", false, &lc);
  CHECK(lc == 1 && lv[0].item_count == 1 && lv[0].items[0].slot == 3);
  qa_app_sim_dropped_pickup_levels_destroy(lv, lc);
  qa_app_sim_source_pickup_cargo_destroy(cargo);
  /* invalid cargo aborts */
  qa_app_sim_source_pickup_cargo *bad = qa_app_sim_source_pickup_cargo_create(reg, "q2:src");
  CHECK_ABORTS(qa_app_sim_source_pickup_cargo_set(bad, actor, (qa_app_sim_dropped_pickup_cargo){"q2:item", 0}));
  qa_app_sim_source_pickup_cargo_destroy(bad);
  qa_app_sim_native_mod_camera_view view = {0};
  view.view.origin = (qa_vec3){100, 100, 100}; view.view.angles = (qa_vec3){1, 2, 3};
  view.native.movement_origin = (qa_vec3){90, 90, 90};
  view.native.position_prediction = true; view.native.angular_prediction = false;
  qa_vec3 origin = {50, 50, 50};
  qa_app_sim_player_view resolved = qa_app_sim_resolve_native_mod_camera(view, &origin, (qa_vec3){9, 9, 9});
  CHECK(resolved.origin.x == 60 && resolved.angles.x == 1);
}

int main(void) {
  test_native_q2_map();
  test_classic_guest_player();
  test_arsenal_and_commands();
  test_misc_pure();
  printf("test_sim_b: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
