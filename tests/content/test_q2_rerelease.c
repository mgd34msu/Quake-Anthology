/*
 * Port of key cases from tests/gameplay/q2-rerelease/rerelease.test.ts
 * (world-text adapter), tests/settings/q2-source-cvars.test.ts and
 * tests/first-playable/rerelease-presentation.test.ts (fog/options),
 * plus source-derived cases for color, obituary, campaign report,
 * debug lifetime, connect, and client animation. The full-game TS
 * harness (Q2Foundation + base Q2Players) is unported, so cases that
 * need it are covered through the mirrored base ops surface.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/q2/rerelease.h"

static int failures = 0;
static int checks = 0;

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

static bool near(double a, double b) { return fabs(a - b) < 1e-9; }

static void test_world_text(void) {
  /* TS: "\u0141" + "b"*140, size 0.2, fixed angles {0,90,0}. */
  uint16_t units[141];
  units[0] = 0x0141;
  for (int i = 1; i < 141; i++) units[i] = 'b';
  char out[160];
  qa_vec3 angles = {0, 90, 0};
  qa_world_text t = qa_q2_world_text_utf16((qa_vec3){1, 2, 3}, &angles, units, 141,
                                           (qa_vec4){1, 1, 1, 1}, 0.2, false, out, sizeof out);
  char expected[128];
  expected[0] = 'A'; /* 0x141 & 255 = 0x41 */
  memset(expected + 1, 'b', 126); expected[127] = '\0';
  CHECK(strcmp(t.text, expected) == 0);
  CHECK(t.origin.x == 1 && t.origin.y == 2 && t.origin.z == 3);
  CHECK(t.orientation_fixed && t.angles.y == 90);
  CHECK(!t.depth_test);
  CHECK(near(t.cell_size, 1.6));
  CHECK(t.has_distance_cull_factor && near(t.distance_cull_factor, 0.004));
  CHECK(!t.font_selected);
  qa_world_text billboard = qa_q2_world_text((qa_vec3){0, 0, 0}, NULL, "Door\nAbove",
                                             (qa_vec4){1, 0, 0, 1}, 0.2, true, out, sizeof out);
  CHECK(strcmp(billboard.text, "Door\nAbove") == 0);
  CHECK(!billboard.orientation_fixed && billboard.depth_test);
  CHECK(near(billboard.cell_size, 1.6));
}

static void test_types_and_fog(void) {
  qa_q2_fog_state fog = qa_q2_create_fog();
  CHECK(fog.fog.density == 0 && fog.fog.sky_factor == 0);
  CHECK(fog.height_fog.density == 0 && fog.height_fog.falloff == 0);
  CHECK(qa_q2_equal_fog(fog, fog));
  qa_q2_fog_state on = fog;
  on.fog.density = 0.6; on.fog.color = (qa_vec3){0.2, 0.3, 0.4};
  CHECK(!qa_q2_equal_fog(fog, on));
  qa_q2_fog_state mid = qa_q2_interpolate_fog(fog, on, 0.5);
  CHECK(near(mid.fog.density, 0.3) && near(mid.fog.color.x, 0.1) && near(mid.fog.color.z, 0.2));

  qa_q2_rerelease_options o = qa_q2_create_rerelease_options(NULL);
  CHECK(o.coop_squad_respawn && o.coop_instanced_items && !o.coop_lives && o.coop_num_lives == 2);
  CHECK(!o.deathmatch_force_respawn && o.deathmatch_spawn_farthest && o.auto_save_minimum_time == 60);
  CHECK(qa_q2_uses_instanced_items(o));
  qa_q2_rerelease_options_changes ch = {0};
  ch.has_coop_instanced_items = true; ch.v.coop_instanced_items = false;
  ch.has_coop_squad_respawn = true; ch.v.coop_squad_respawn = false;
  CHECK(!qa_q2_uses_instanced_items(qa_q2_create_rerelease_options(&ch)));

  qa_q2_game_options go = {0}; go.map_name = "q64/base1";
  CHECK(qa_q2_is_n64(&go));
  go.map_name = "base1";
  CHECK(!qa_q2_is_n64(&go));

  /* spawn-field fog parsing (trigger_fog case from the TS test). */
  qa_q2_spawn_fields fields = {0};
  CHECK(qa_q2_spawn_set(&fields, "classname", "trigger_fog"));
  CHECK(qa_q2_spawn_set(&fields, "fog_density", "0.6"));
  CHECK(qa_q2_spawn_set(&fields, "fog_color", "0.2 0.3 0.4"));
  qa_q2_fog_state parsed = qa_q2_rerelease_fog_fields(&fields, false);
  CHECK(near(parsed.fog.density, 0.6));
  CHECK(near(parsed.fog.color.x, 0.2) && near(parsed.fog.color.y, 0.3) && near(parsed.fog.color.z, 0.4));
  qa_q2_fog_state off = qa_q2_rerelease_fog_fields(&fields, true);
  CHECK(off.fog.density == 0);
  qa_q2_spawn_fields_free(&fields);

  qa_q2rr_player_extra_state extra;
  qa_q2rr_player_extra_state_init(&extra, 3, "social");
  CHECK(extra.seat == 3 && strcmp(extra.social_id, "social") == 0);
  CHECK(extra.auto_shield == -1 && strcmp(extra.help_image, "friend") == 0);
  CHECK(extra.coop_respawn_state == QA_Q2_COOP_NONE);
  CHECK(strcmp(qa_q2_coop_respawn_state_name(QA_Q2_COOP_IN_COMBAT), "in-combat") == 0);
  qa_q2rr_player_extra_state_destroy(&extra);
}

static void test_debug_lifetime_and_color(void) {
  CHECK(qa_q2_rerelease_debug_lifetime(0.025) == 25);
  CHECK(qa_q2_rerelease_debug_lifetime(1.5) == 1500);
  CHECK(qa_q2_rerelease_debug_lifetime(-0.001) == 4294967295.0); /* >>>0 wrap of -1 */
  CHECK_ABORTS((void)qa_q2_rerelease_debug_lifetime(INFINITY));
  CHECK_ABORTS((void)qa_q2_rerelease_debug_lifetime(NAN));

  /* ED_LoadColor: packed int passthrough, 0..1 floats scale by 255. */
  CHECK(qa_q2_rerelease_color("12345") == 12345);
  uint32_t red = (uint32_t)(int32_t)qa_q2_rerelease_color("1 0 0");
  CHECK(red == 0xff0000ffu);
  uint32_t blue = (uint32_t)(int32_t)qa_q2_rerelease_color("0 0 1");
  CHECK(blue == 0x0000ffffu);
  uint32_t bytes = (uint32_t)(int32_t)qa_q2_rerelease_color("255 128 0 255");
  CHECK(bytes == 0xff8000ffu);
}

static void test_obituary(void) {
  qa_q2_base_player_state victim, attacker;
  qa_q2_base_player_state_init(&victim, 0, 0);
  qa_q2_base_player_state_init(&attacker, 1, 0);
  snprintf(victim.name, sizeof victim.name, "Vic");
  snprintf(attacker.name, sizeof attacker.name, "Atk");
  /* generic means 22 = falling, deathmatch loses a point */
  qa_q2rr_obituary ob = qa_q2_rerelease_obituary(&victim, NULL, 22, QA_Q2_MODE_DEATHMATCH, false, NULL, NULL);
  CHECK(strcmp(ob.text, "$g_mod_generic_falling") == 0);
  CHECK(ob.arg_count == 1 && victim.score == -1);
  /* kill means 11 = railgun */
  ob = qa_q2_rerelease_obituary(&victim, &attacker, 11, QA_Q2_MODE_DEATHMATCH, false, NULL, NULL);
  CHECK(strcmp(ob.text, "$g_mod_kill_railgun") == 0);
  CHECK(ob.arg_count == 2 && attacker.score == 1);
  /* self means 9 = rocket_splash */
  ob = qa_q2_rerelease_obituary(&victim, &victim, 9, QA_Q2_MODE_SINGLEPLAYER, false, NULL, NULL);
  CHECK(strcmp(ob.text, "$g_mod_self_rocket_splash") == 0);
  /* friendly-fire bit 0x8000000 + means 8 = rocket: attacker loses point */
  ob = qa_q2_rerelease_obituary(&victim, &attacker, 8 + 0x8000000, QA_Q2_MODE_DEATHMATCH, false, NULL, NULL);
  CHECK(strcmp(ob.text, "$g_mod_kill_rocket") == 0 && attacker.score == 0);
  qa_q2_base_player_state_destroy(&victim);
  qa_q2_base_player_state_destroy(&attacker);
}

static void test_campaign_report(void) {
  qa_q2_rerelease_campaign_state camp;
  CHECK(qa_q2_create_rerelease_campaign_state(&camp));
  CHECK(camp.cross_unit_flags == 0 && camp.level_count == 0);
  camp.levels = calloc(3, sizeof *camp.levels); camp.level_cap = 3; camp.level_count = 3;
  camp.levels[0].map = qa_q2rr_dup("base2"); camp.levels[0].name = qa_q2rr_dup("");
  camp.levels[1].map = qa_q2rr_dup("base1"); camp.levels[1].name = qa_q2rr_dup("Outer Base");
  camp.levels[1].visit_order = 1;
  camp.levels[2].map = qa_q2rr_dup("base3"); camp.levels[2].name = qa_q2rr_dup("Named Unvisited");
  size_t n = 0;
  qa_q2_rerelease_level_entry *report = qa_q2_rerelease_unit_report(&camp, &n);
  CHECK(n == 3);
  /* visitOrder 1 first, then named-unvisited (9), then unnamed (10) */
  CHECK(strcmp(report[0].map, "base1") == 0);
  CHECK(strcmp(report[1].map, "base3") == 0);
  CHECK(strcmp(report[2].map, "base2") == 0);
  qa_q2_rerelease_unit_report_free(report, n);
  qa_q2_rerelease_campaign_state clone;
  CHECK(qa_q2_rerelease_campaign_clone(&clone, &camp));
  clone.mission.primary[0] = 'X';
  CHECK(camp.mission.primary[0] == '\0'); /* clone is deep (structuredClone) */
  qa_q2_rerelease_campaign_destroy(&clone);
  qa_q2_rerelease_campaign_destroy(&camp);
}

/* Minimal hooks/game for connect + animation (no base ops needed). */
static qa_q2_player_movement fake_movement(void *c, qa_actor_id a) {
  (void)c; (void)a;
  qa_q2_player_movement m = {0};
  m.standing_bounds = QA_Q2_RERELEASE_PLAYER_BOUNDS;
  m.animate_q2 = true; m.grounded = true;
  return m;
}
static void fake_emit(void *c, qa_q2_player_event e) { (void)c; (void)e; }
static void fake_set_movement(void *c, qa_actor_id a, qa_q2_player_movement_change ch) { (void)c; (void)a; (void)ch; }
static void fake_noise(void *c, qa_actor_id a, qa_vec3 o) { (void)c; (void)a; (void)o; }
static bool fake_banned(void *c, const char *a) { (void)c; (void)a; return false; }
static void rr_emit(void *c, qa_q2_rerelease_event e) { (void)c; (void)e; }
static const char *rr_style(void *c, double s) { (void)c; (void)s; return "az"; }
static qa_q2rr_player_identity rr_identity(void *c, qa_actor_id a) {
  (void)c; (void)a; qa_q2rr_player_identity id = {0, ""}; return id;
}
static bool rr_true1(void *c, qa_actor_id a) { (void)c; (void)a; return true; }
static qa_q2rr_navigation rr_nav(void *c, qa_vec3 s, qa_vec3 g, const qa_actor_id *a) {
  (void)c; (void)s; (void)g; (void)a;
  qa_q2rr_navigation n = {"no-navigation", 0, NULL, 0}; return n;
}
static bool rr_search(void *c, const qa_actor_id *a) { (void)c; (void)a; return false; }
static void rr_void_actor(void *c, qa_actor_id a, qa_vec3 v) { (void)c; (void)a; (void)v; }
static void rr_grav(void *c, qa_actor_id a, double g) { (void)c; (void)a; (void)g; }
static void rr_world_grav(void *c, double g) { (void)c; (void)g; }
static bool rr_clip(void *c, qa_q2_entity *t, qa_actor_id p, qa_q2_game_services *g) {
  (void)c; (void)t; (void)p; (void)g; return true;
}

static void make_players(qa_q2_rerelease_players *p) {
  qa_q2_player_hooks hooks = {0};
  hooks.movement = fake_movement; hooks.set_movement = fake_set_movement;
  hooks.emit = fake_emit; hooks.noise = fake_noise; hooks.banned = fake_banned;
  qa_q2_rerelease_hooks rr = {0};
  rr.emit = rr_emit; rr.light_style = rr_style; rr.player_identity = rr_identity;
  rr.clip_trigger = rr_clip; rr.navigation = rr_nav; rr.monsters_searching = rr_search;
  rr.grounded_on_world = rr_true1; rr.push_player = rr_void_actor;
  rr.set_actor_gravity = rr_grav; rr.set_world_gravity = rr_world_grav;
  CHECK(qa_q2_rerelease_players_init(p, NULL, NULL, hooks, rr, NULL, NULL, 0));
}

static void test_connect(void) {
  qa_q2_rerelease_players p;
  make_players(&p);
  qa_q2_game_services g = {0};
  g.options.mode = QA_Q2_MODE_DEATHMATCH;
  qa_q2_connection_result r = qa_q2_rerelease_players_connect(&p, &g, "\\name\\Player", false);
  CHECK(r.allowed);
  qa_q2_players_rules(p.base)->password = "secret";
  r = qa_q2_rerelease_players_connect(&p, &g, "\\name\\Player", false);
  CHECK(!r.allowed);
  CHECK(strcmp(r.reason, "Password required or incorrect.") == 0);
  CHECK(strstr(r.userinfo, "\\rejmsg\\Password required or incorrect.") != NULL);
  r = qa_q2_rerelease_players_connect(&p, &g, "\\name\\Player\\password\\secret", false);
  CHECK(r.allowed);
  /* bots bypass the player password in rerelease connect */
  r = qa_q2_rerelease_players_connect(&p, &g, "\\name\\Bot", true);
  CHECK(r.allowed);
  /* extra() aborts for unadmitted actors (TS Error) */
  qa_actor_id unknown = {0}; unknown.slot = 7; unknown.generation = 1;
  CHECK_ABORTS((void)qa_q2_rerelease_players_get_extra(&p, unknown));
  qa_q2_rerelease_players_destroy(&p);
}

static double fake_now_value = 0;
static double fake_now(void *c) { (void)c; return fake_now_value; }
static double fake_frame(void *c) { (void)c; return 0.1; }
static qa_body_state anim_body;
static qa_body_state fake_body(void *c, qa_owned_actor a) { (void)c; (void)a; return anim_body; }

static void test_client_animation(void) {
  qa_q2_foundation_host host = {0};
  host.now = fake_now; host.frame_seconds = fake_frame;
  qa_q2_game_services g = {0};
  g.host = &host; g.body = fake_body;
  qa_q2_spawn_fields fields = {0};
  qa_q2_entity e;
  qa_q2_entity_init(&e, (qa_owned_actor){0}, fields);
  qa_q2_base_player_state st;
  qa_q2_base_player_state_init(&st, 0, 0);
  qa_q2rr_player_extra_state extra;
  qa_q2rr_player_extra_state_init(&extra, 0, "");
  qa_q2_player_context ctx = {0};
  ctx.entity = &e; ctx.game = &g; ctx.state = &st;
  ctx.movement = fake_movement(NULL, e.actor.id);
  /* idle standing: frame 0, end 39 (TS q2RereleaseClientAnimation) */
  anim_body = (qa_body_state){0};
  fake_now_value = 1;
  e.frame = 99;
  qa_q2_rerelease_client_animation(&ctx, &extra);
  CHECK(e.frame == 0 && st.animation_end == 39);
  /* advance one frame after animationTime */
  fake_now_value = 1.2;
  qa_q2_rerelease_client_animation(&ctx, &extra);
  CHECK(e.frame == 1);
  /* running: frame 40..45 */
  anim_body.velocity = (qa_vec3){100, 0, 0};
  st.animation_priority = 0;
  qa_q2_rerelease_client_animation(&ctx, &extra);
  CHECK(e.frame == 40 && st.animation_end == 45);
  qa_q2_base_player_state_destroy(&st);
  qa_q2rr_player_extra_state_destroy(&extra);
  qa_q2_entity_destroy(&e);
}

static void test_goals_checkpoint(void) {
  qa_q2_rerelease_players p;
  make_players(&p);
  qa_q2_rerelease_campaign_state camp;
  CHECK(qa_q2_create_rerelease_campaign_state(&camp));
  qa_q2_rerelease_goals goals;
  qa_q2_rerelease_goals_init(&goals, &p, &camp, p.rerelease_hooks, (qa_q2_use){0});
  goals.goals = qa_q2rr_dup("Find the commander\tDestroy the gate");
  goals.goal_number = 1;
  qa_q2_rerelease_goals_checkpoint cp = qa_q2_rerelease_goals_capture(&goals);
  CHECK(cp.goal_number == 1 && strcmp(cp.goals, "Find the commander\tDestroy the gate") == 0);
  qa_q2_rerelease_goals restored;
  qa_q2_rerelease_goals_init(&restored, &p, &camp, p.rerelease_hooks, (qa_q2_use){0});
  qa_q2_rerelease_goals_restore(&restored, cp);
  CHECK(restored.goal_number == 1);
  CHECK(strcmp(restored.goals, cp.goals) == 0 && restored.goals != cp.goals);
  qa_q2_callback_definitions defs = qa_q2_rerelease_goals_callbacks(&restored);
  CHECK(defs.think_count == 1 && strcmp(defs.think[0].name, "rr.G_VerifyTargetted") == 0);
  CHECK(defs.use_count == 2 && strcmp(defs.use[0].name, "rr.Use_Target_Help") == 0);
  qa_q2_rerelease_goals_destroy(&goals);
  qa_q2_rerelease_goals_destroy(&restored);
  qa_q2_rerelease_campaign_destroy(&camp);
  qa_q2_rerelease_players_destroy(&p);
}

int main(void) {
  test_world_text();
  test_types_and_fog();
  test_debug_lifetime_and_color();
  test_obituary();
  test_campaign_report();
  test_connect();
  test_client_animation();
  test_goals_checkpoint();
  if (failures) {
    fprintf(stderr, "%d/%d checks failed\n", failures, checks);
    return 1;
  }
  printf("ok %d checks\n", checks);
  return 0;
}
