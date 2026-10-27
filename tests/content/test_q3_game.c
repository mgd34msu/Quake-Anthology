/*
 * Port of key TS cases for src/content/q3/base/game: grapple velocity,
 * missile parameters, gameFormat, checkArmor, weapon pickup quantity,
 * rail statistics, memory, spawn variables, rankings warmup gating.
 * TypeScript toThrow cases run in a child process and must abort.
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

#include "qa/content/q3/base/game/ballistics_math.h"
#include "qa/content/q3/base/game/combat.h"
#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/grapple.h"
#include "qa/content/q3/base/game/hitscan.h"
#include "qa/content/q3/base/game/item_pickup.h"
#include "qa/content/q3/base/game/memory.h"
#include "qa/content/q3/base/game/rankings.h"
#include "qa/content/q3/base/game/save_callbacks.h"
#include "qa/content/q3/base/game/spawn.h"
#include "qa/content/q3/base/game/state.h"

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

static qa_vec3 v3(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }

static void test_grapple(void) {
  /* Near target: speed = 10*distance; far: 800. Point straight ahead. */
  qa_vec3 vel = qa_q3_grapple_velocity(v3(0, 0, 0), v3(26, 0, 0), v3(1, 0, 0));
  CHECK(vel.x > 0 && vel.y == 0 && vel.z == 0);
  double near_len = sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
  CHECK(fabs(near_len - 100) < 1); /* pull length 10 -> 10*10 */
  vel = qa_q3_grapple_velocity(v3(0, 0, 0), v3(1016, 0, 0), v3(1, 0, 0));
  double far_len = sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
  CHECK(fabs(far_len - 800) < 1);
  qa_bounds b = {v3(-10, -10, 0), v3(10, 10, 20)};
  qa_vec3 t = qa_q3_grapple_target(v3(100, 0, 0), b);
  CHECK(t.x == 100 && t.y == 0 && t.z == 10);
  qa_q3_grapple_cable cable;
  CHECK(!qa_q3_get_grapple_cable(v3(0, 0, 0), v3(0, 0, 1), v3(0, 0, 10), 26, &cable));
  CHECK(qa_q3_get_grapple_cable(v3(0, 0, 0), v3(0, 0, 1), v3(0, 0, 500), 26, &cable));
}

static void test_ballistics(void) {
  qa_q3_missile_params p = qa_q3_get_missile_parameters(5);
  CHECK(p.speed == 900 && p.duration == 15000 && !p.gravity && p.radius == 120);
  p = qa_q3_get_missile_parameters(4);
  CHECK(p.gravity && p.speed == 700);
  CHECK_ABORTS((void)qa_q3_get_missile_parameters(1));
  qa_vec3 bounced = qa_q3_bounce_velocity(v3(10, 0, -10), v3(0, 0, 1), false);
  CHECK(bounced.z == 10);
  qa_vec3 half = qa_q3_bounce_velocity(v3(10, 0, -10), v3(0, 0, 1), true);
  CHECK(fabs(half.z - (float)10 * (float)0.65) < 0.01);
  CHECK(qa_q3_missile_hit_time(0, 100, 0.5) == 50);
}

static void test_format(void) {
  qa_q3_game_format_arg a1 = qa_q3_game_format_number(42);
  char *s = qa_q3_game_format("%4i: ok\n", &a1, 1, 32000);
  CHECK(s && strcmp(s, "  42: ok\n") == 0); free(s);
  qa_q3_game_format_arg a2[2] = {qa_q3_game_format_string("x"), qa_q3_game_format_number(1.5)};
  s = qa_q3_game_format("%s=%5.2f@", a2, 2, 32000);
  CHECK(s && strcmp(s, "x=    1.50@") == 0); free(s);
  CHECK_ABORTS(free(qa_q3_game_format("%i", NULL, 0, 32000)));
}

static double dbg_zero(void *ctx) { (void)ctx; return 0; }
static void print_noop(void *ctx, const char *text) { (void)ctx; (void)text; }

static void test_memory(void) {
  static qa_q3_game_memory mem;
  qa_q3_game_memory_init(&mem, NULL, dbg_zero, print_noop);
  qa_q3_game_memory_allocation a = qa_q3_game_memory_allocate(&mem, 10);
  CHECK(a.length == 10);
  CHECK(qa_q3_game_memory_allocated_bytes(&mem) == 32);
  qa_q3_game_memory_allocation_write_string(a, "hi");
  char *s = qa_q3_game_memory_allocation_read_string(a);
  CHECK(s && strcmp(s, "hi") == 0); free(s);
  CHECK_ABORTS((void)qa_q3_game_memory_allocate(&mem, -1));
}

static void test_check_armor(void) {
  qa_q3_game_entity e; qa_q3_game_entity_binding binding; memset(&binding, 0, sizeof binding);
  qa_q3_game_entity_init(&e, 0, binding);
  qa_q3_game_client client; qa_q3_game_client_init(&client, QA_Q3_PRODUCT_BASEQ3);
  e.client = &client;
  qa_q3_game_slots_set_mirror(&client.ps.stats, 3, 100);
  double save = qa_q3_check_armor(&e, 100, 0);
  CHECK(save == 66);
  CHECK(qa_q3_game_slots_get_mirror(&client.ps.stats, 3) == 34);
  CHECK(qa_q3_check_armor(&e, 0, 0) == 0);
  CHECK(qa_q3_check_armor(&e, 100, QA_Q3_DAMAGE_NO_ARMOR) == 0);
  qa_q3_game_client_destroy(&client); qa_q3_game_entity_destroy(&e);
}

static void test_pickup_quantity(void) {
  qa_q3_weapon_pickup_quantity_input in = {0, 10, false, 0, 3};
  CHECK(qa_q3_weapon_pickup_quantity(in) == 7);
  in.current_ammo = 20; CHECK(qa_q3_weapon_pickup_quantity(in) == 1);
  in.dropped = true; CHECK(qa_q3_weapon_pickup_quantity(in) == 10);
  in.count = -1; CHECK(qa_q3_weapon_pickup_quantity(in) == 0);
  qa_q3_rail_statistics st = {0, 0, 0, 0};
  qa_q3_rail_statistics_result r = qa_q3_update_rail_statistics(st, 2, 1000);
  CHECK(r.awarded && r.state.impressive_count == 1 && r.state.reward_until == 3000);
  r = qa_q3_update_rail_statistics(st, 0, 1000);
  CHECK(!r.awarded && r.state.streak == 0);
}

static int rank_count;
static void rank_sink(void *ctx, qa_q3_ranking_report report) { (void)ctx; (void)report; rank_count++; }
static bool warmup_false(void *ctx) { (void)ctx; return false; }
static bool warmup_true(void *ctx) { (void)ctx; return true; }

static void test_rankings(void) {
  qa_q3_ranking_reports r; qa_q3_ranking_reports_init(&r);
  rank_count = 0;
  qa_q3_ranking_reports_attach(&r, NULL, rank_sink, warmup_true);
  qa_q3_ranking_reports_fire_weapon(&r, 0, 2);
  CHECK(rank_count == 0); /* warmup gates reports */
  qa_q3_ranking_reports_detach(&r);
  qa_q3_ranking_reports_attach(&r, NULL, rank_sink, warmup_false);
  qa_q3_ranking_reports_fire_weapon(&r, 0, 2);
  CHECK(rank_count == 2); /* generic + machinegun */
  qa_q3_ranking_reports_fire_weapon(&r, 0, 1);
  CHECK(rank_count == 2); /* gauntlet excluded */
  qa_q3_ranking_reports_detach(&r);
}

static void test_callbacks(void) {
  qa_q3_callback_family f; qa_q3_callback_family_init(&f);
  static int cb_anchor; qa_q3_entity_callback cb = {(void *)&cb_anchor, NULL};
  CHECK(qa_q3_callback_family_register(&f, "a", cb).fn == cb.fn);
  CHECK(strcmp(qa_q3_callback_family_capture(&f, cb), "a") == 0);
  CHECK(qa_q3_callback_family_resolve(&f, "a").fn == cb.fn);
  CHECK(qa_q3_callback_family_capture(&f, (qa_q3_entity_callback){NULL, NULL}) == NULL);
  qa_q3_callback_family_destroy(&f);
}

static void test_spawn(void) {
  qa_q3_spawn_parser p; qa_q3_spawn_parser_init(&p, "{ \"classname\" \"worldspawn\" \"gravity\" \"900\" }", NULL);
  qa_q3_spawn_variables vars;
  CHECK(qa_q3_spawn_parser_next(&p, &vars));
  CHECK(strcmp(qa_q3_spawn_variables_string(&vars, "CLASSNAME", "").value, "worldspawn") == 0);
  CHECK(qa_q3_spawn_variables_string(&vars, "gravity", "800").present);
  CHECK(!qa_q3_spawn_variables_string(&vars, "missing", "800").present);
  qa_q3_spawn_variables_destroy(&vars);
  CHECK_ABORTS(do { qa_q3_game_entity e2; qa_q3_game_entity_binding b2; memset(&b2, 0, sizeof b2); qa_q3_game_entity_init(&e2, 5000, b2); } while (0));
}

int main(void) {
  test_grapple(); test_ballistics(); test_format(); test_memory();
  test_check_armor(); test_pickup_quantity(); test_rankings(); test_callbacks(); test_spawn();
  if (failures) { fprintf(stderr, "%d/%d checks failed\n", failures, checks); return 1; }
  printf("test_q3_game: %d checks passed\n", checks);
  return 0;
}
