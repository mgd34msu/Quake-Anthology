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
#include "qa/content/q3/base/game/death.h"
#include "qa/content/q3/base/game/misc_spawn.h"
#include "qa/content/q3/base/game/mover_spawn.h"
#include "qa/content/q3/base/game/personal_portal.h"
#include "qa/content/q3/base/game/targets.h"
#include "qa/content/q3/base/game/triggers.h"
#include "qa/content/q3/base/game/weapon.h"
#include "qa/world/gameplay/policies.h"
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

/* Fake pool records for the completed spawn/death handler modules. */
static qa_q3_game_entity fake_entities[160];
static qa_q3_game_client fake_clients[64];
static bool fake_active[160];
static int fake_released, fake_registered, fake_init_binary_count, fake_links, fake_returned, fake_ranks;
static bool fake_is_active(void *ctx) { return *(bool *)ctx; }
static qa_owned_actor fake_actor(void *ctx) { (void)ctx; qa_owned_actor a; memset(&a, 0, sizeof a); return a; }
static qa_q3_game_entity *fake_get(void *ctx, double slot) { (void)ctx; return slot >= 0 && slot < 160 ? &fake_entities[(int)slot] : NULL; }
static qa_q3_game_entity *fake_activate(void *ctx, double slot) {
  (void)ctx; int i = (int)slot; qa_q3_game_entity_binding b; memset(&b, 0, sizeof b);
  b.ctx = &fake_active[i]; b.active = fake_is_active; b.actor = fake_actor;
  qa_q3_game_entity_init(&fake_entities[i], slot, b); fake_active[i] = true; return &fake_entities[i];
}
static qa_q3_game_client *fake_client(void *ctx, double slot) { (void)ctx; return &fake_clients[(int)slot]; }
static void fake_release(void *ctx, qa_q3_game_entity *e) { (void)ctx; fake_active[(int)e->slot] = false; fake_released++; }
static void fake_noop_client(void *ctx, double slot) { (void)ctx; (void)slot; }
static double fake_time1000(void *ctx) { (void)ctx; return 1000; }
static void fake_link(void *ctx, qa_q3_game_entity *e) { (void)ctx; (void)e; fake_links++; }
static void fake_unlink(void *ctx, qa_q3_game_entity *e) { (void)ctx; (void)e; }
static void fake_unlink_num(void *ctx, double n) { (void)ctx; (void)n; }
static double fake_zero_d(void *ctx) { (void)ctx; return 0; }
static double fake_sound(void *ctx, const char *path) { (void)ctx; (void)path; return 0; }
static void fake_warn(void *ctx, const char *m) { (void)ctx; (void)m; }
static qa_q3_entity_pool fake_pool;
static void fake_pool_setup(void) {
  static bool done; if (done) return; done = true;
  for (int i = 0; i < 64; i++) qa_q3_game_client_init(&fake_clients[i], QA_Q3_PRODUCT_BASEQ3);
  qa_q3_entity_pool_options o; memset(&o, 0, sizeof o);
  o.records.get = fake_get; o.records.activate = fake_activate; o.records.client = fake_client;
  o.records.release = fake_release; o.records.deactivate_client = fake_noop_client;
  o.product = QA_Q3_PRODUCT_BASEQ3; o.max_clients = 8; o.time = fake_time1000; o.print = print_noop;
  o.link = fake_link; o.unlink = fake_unlink;
  qa_q3_entity_pool_init(&fake_pool, o);
}
static void fake_fire(void *ctx, qa_q3_game_entity *e, qa_vec3 o, qa_q3_missile_direction d) { (void)ctx; (void)e; (void)o; (void)d; }
static void fake_register(void *ctx, const qa_q3_game_item_definition_mirror *i) { (void)ctx; (void)i; fake_registered++; }
static const qa_q3_game_item_definition_mirror *fake_find_weapon(void *ctx, double w) { (void)ctx; (void)w; return NULL; }
static void fake_brush(void *ctx, qa_q3_game_entity *e, const char *n) { (void)ctx; (void)n; e->r.mins = v3(-8, -8, -8); e->r.maxs = v3(8, 8, 8); }
static void fake_init_binary(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)ctx; (void)e; (void)v; fake_init_binary_count++; }
static void test_completed_spawns(void) {
  fake_pool_setup();
  qa_q3_spawn_variables empty; qa_q3_spawn_variables_init(&empty, NULL, 0);
  /* misc-spawn: 11 handlers, info_null frees, shooter registers + aims. */
  qa_q3_misc_spawn_host mh; memset(&mh, 0, sizeof mh);
  mh.pool = &fake_pool; mh.time = fake_time1000; mh.rand_value = fake_zero_d; mh.crandom = fake_zero_d;
  mh.warn = fake_warn; mh.fire_grenade = mh.fire_rocket = mh.fire_plasma = fake_fire;
  mh.register_item = fake_register; mh.find_item_for_weapon = fake_find_weapon;
  mh.world.link = fake_link; mh.world.unlink_number = fake_unlink_num;
  qa_q3_spawn_handler_entry entries[16];
  size_t n = qa_q3_misc_spawn_handlers(mh, entries, 16);
  CHECK(n == 11);
  qa_q3_game_entity *e = qa_q3_entity_pool_spawn(&fake_pool);
  fake_released = 0;
  entries[1].fn(entries[1].ctx, e, &empty); /* info_null */
  CHECK(fake_released == 1 && strcmp(qa_q3_game_entity_classname(e), "freed") == 0);
  e = qa_q3_entity_pool_spawn(&fake_pool); fake_registered = 0;
  entries[8].fn(entries[8].ctx, e, &empty); /* shooter_rocket */
  CHECK(e->s.weapon == 5 && fake_registered == 1 && fabs(e->random - sin(3.141592653589793 / 180)) < 1e-6);
  /* mover-spawn: 9 handlers; rotating picks yaw axis and default damage. */
  qa_q3_mover_spawn_runtime mr; qa_q3_mover_spawn_host vh; memset(&vh, 0, sizeof vh);
  vh.pool = &fake_pool; vh.time = fake_time1000; vh.gravity = fake_zero_d; vh.sound_index = fake_sound;
  vh.set_brush_model = fake_brush; vh.initialize_binary = fake_init_binary; vh.warn = fake_warn;
  vh.world.link = fake_link; vh.world.unlink_number = fake_unlink_num;
  qa_q3_mover_spawn_runtime_init(&mr, vh);
  n = qa_q3_mover_spawn_runtime_handlers(&mr, entries, 16);
  CHECK(n == 9);
  e = qa_q3_entity_pool_spawn(&fake_pool); fake_init_binary_count = 0;
  entries[6].fn(entries[6].ctx, e, &empty); /* func_rotating */
  CHECK(fake_init_binary_count == 1 && e->damage == 2 && e->s.apos.delta.y == 100 && e->s.apos.type == 2);
  /* triggers: 6 handlers; hurt defaults damage to 5. */
  qa_q3_trigger_runtime tr; qa_q3_trigger_host th; memset(&th, 0, sizeof th);
  th.entities = &fake_pool; th.gravity = fake_zero_d; th.rand_value = fake_zero_d; th.crandom = fake_zero_d;
  th.set_brush_model = fake_brush; th.sound_index = fake_sound; th.warn = fake_warn;
  qa_q3_trigger_runtime_init(&tr, th);
  n = qa_q3_trigger_spawn_handlers(th, entries, 16);
  CHECK(n == 6);
  e = qa_q3_entity_pool_spawn(&fake_pool);
  entries[4].fn(entries[4].ctx, e, &empty); /* trigger_hurt */
  CHECK(e->damage == 5);
  /* targets: 13 handlers; delay defaults wait to 1. */
  qa_q3_target_runtime tg; qa_q3_target_runtime_init(&tg, NULL, &fake_pool);
  tg.item_lifecycle_matches = true; tg.warn = fake_warn; tg.rand_value = fake_zero_d; tg.crandom = fake_zero_d;
  n = qa_q3_target_spawn_handlers(&tg, entries, 16);
  CHECK(n == 13);
  e = qa_q3_entity_pool_spawn(&fake_pool);
  entries[2].fn(entries[2].ctx, e, &empty); /* target_delay */
  CHECK(e->wait == 1);
  qa_q3_spawn_variables_destroy(&empty);
}
static void fake_return_item(void *ctx, qa_q3_game_entity *p) { (void)ctx; (void)p; fake_returned++; }
static qa_q3_death_frame fake_frame(void *ctx) { (void)ctx; qa_q3_death_frame f = {1000, 0, 0, 0, true}; return f; }
static void fake_calc_ranks(void *ctx) { (void)ctx; fake_ranks++; }
static void test_completed_runtime(void) {
  fake_pool_setup();
  /* Weapon helpers. */
  qa_q3_game_client wc; qa_q3_game_client_init(&wc, QA_Q3_PRODUCT_BASEQ3);
  CHECK(qa_q3_weapon_damage_factor(&wc, 3, QA_Q3_PRODUCT_BASEQ3) == 1);
  qa_q3_game_slots_set_mirror(&wc.ps.powerups, 1, 5000);
  CHECK(qa_q3_weapon_damage_factor(&wc, 3, QA_Q3_PRODUCT_BASEQ3) == 3);
  qa_q3_missile_direction dir = {2, 0, 0};
  qa_q3_sphere_intersections hits = qa_q3_ray_sphere_intersections(v3(0, 0, 0), 10, v3(0, 0, 0), &dir);
  CHECK(hits.count == 2 && dir.x == 1);
  qa_q3_game_client_destroy(&wc);
  /* Death: persistent powerup toss + addScore. */
  qa_q3_game_entity victim; qa_q3_game_entity_binding vb; memset(&vb, 0, sizeof vb);
  qa_q3_game_entity_init(&victim, 3, vb);
  qa_q3_game_client vc; qa_q3_game_client_init(&vc, QA_Q3_PRODUCT_MISSIONPACK);
  qa_q3_game_entity power; qa_q3_game_entity_init(&power, 4, vb);
  vc.persistant_powerup = &power; victim.client = &vc;
  qa_q3_game_slots_set_mirror(&vc.ps.stats, 2, 7);
  fake_returned = 0;
  qa_q3_toss_client_persistent_powerup(&victim, fake_return_item, NULL);
  CHECK(fake_returned == 1 && vc.persistant_powerup == NULL && qa_q3_game_slots_get_mirror(&vc.ps.stats, 2) == 0);
  static qa_q3_game_player_slots_mirror team_scores; team_scores.length = 4;
  static qa_q3_death_animation_sequence seq; qa_q3_death_animation_sequence_init(&seq);
  qa_q3_death_runtime dr; qa_q3_death_host dh; memset(&dh, 0, sizeof dh);
  dh.product = QA_Q3_PRODUCT_BASEQ3; dh.pool = &fake_pool; dh.team_scores = &team_scores;
  dh.death_animations = &seq; dh.frame = fake_frame; dh.calculate_ranks = fake_calc_ranks;
  qa_q3_death_runtime_init(&dr, dh);
  qa_q3_game_entity *scorer = qa_q3_entity_pool_at(&fake_pool, 0);
  fake_ranks = 0;
  qa_q3_death_add_score(&dr, scorer, v3(0, 0, 0), 5);
  CHECK(fake_ranks == 1 && qa_q3_game_slots_get_mirror(&scorer->client->ps.persistant, 0) == 5);
  /* Personal portal: sequence save/restore and product gate. */
  qa_q3_personal_portal_runtime pr; memset(&pr, 0, sizeof pr);
  qa_q3_personal_portal_restore_sequence(&pr, 42);
  CHECK(qa_q3_personal_portal_capture_sequence(&pr) == 42);
  CHECK_ABORTS(qa_q3_personal_portal_restore_sequence(&pr, 3e9));
  /* State: new shared fields, event ring, classname capture. */
  qa_q3_game_client sc; qa_q3_game_client_init(&sc, QA_Q3_PRODUCT_BASEQ3);
  CHECK(sc.ps.ping == 0 && sc.ps.generic1 == 0 && sc.ps.weapon == 0);
  qa_q3_game_player_state_add_event_mirror(&sc.ps, 9, 4);
  CHECK(sc.ps.event_sequence == 1 && qa_q3_game_slots_get_mirror(&sc.ps.events, 0) == 9);
  qa_q3_game_entity se; qa_q3_game_entity_init(&se, 5, vb);
  qa_q3_game_entity_bind_client_name(&se, &sc);
  CHECK(qa_q3_game_entity_capture_classname(&se).kind == 1);
  qa_q3_game_entity_destroy(&se); qa_q3_game_client_destroy(&sc);
  qa_q3_game_client_destroy(&vc); qa_q3_game_entity_destroy(&victim); qa_q3_game_entity_destroy(&power);
  /* Collision fix: both context types coexist with distinct shapes. */
  qa_q3_game_combat_context game_ctx; memset(&game_ctx, 0, sizeof game_ctx); game_ctx.entities = &fake_pool;
  qa_q3_combat_context policy_ctx; memset(&policy_ctx, 0, sizeof policy_ctx); policy_ctx.player = true;
  CHECK(game_ctx.entities == &fake_pool && policy_ctx.player);
}
int main(void) {
  test_grapple(); test_ballistics(); test_format(); test_memory();
  test_check_armor(); test_pickup_quantity(); test_rankings(); test_callbacks(); test_spawn();
  test_completed_spawns(); test_completed_runtime();
  if (failures) { fprintf(stderr, "%d/%d checks failed\n", failures, checks); return 1; }
  printf("test_q3_game: %d checks passed\n", checks);
  return 0;
}
