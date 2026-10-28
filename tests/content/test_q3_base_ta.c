/*
 * Tests for content/q3/base shared + settings ports. Expected values are
 * produced by the TypeScript implementation (bun dumps of
 * shared/trajectory.ts, direction-byte.ts, items.ts; see docs/PORTING.md).
 * TypeScript throw cases run in a child process and must abort.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/q3/base/settings.h"
#include "qa/content/q3/team_arena/foreign_objectives.h"
#include "qa/content/q3/team_arena/objective_placement.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/direction_byte.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/base/shared/jump_pad.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/base/shared/snapshot_state.h"
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/contracts/world.h"
#include "qa/core/math.h"

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

static bool vec_eq(qa_vec3 v, double x, double y, double z) {
  return v.x == x && v.y == y && v.z == z;
}

static void test_definitions(void) {
  const qa_q3_base_stat_schema *base = qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_BASEQ3);
  const qa_q3_base_stat_schema *mp = qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_MISSIONPACK);
  CHECK(base->health == 0 && base->weapons == 2 && base->armor == 3 && base->max_health == 6);
  CHECK(!base->has_persistent_powerup);
  CHECK(mp->weapons == 3 && mp->armor == 4 && mp->max_health == 7);
  CHECK(mp->has_persistent_powerup && mp->persistent_powerup == 2);
  CHECK(qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_BASEQ3) == base); /* frozen identity */
  CHECK(qa_q3_base_weapon_count(QA_Q3_BASE_PRODUCT_BASEQ3) == 11);
  CHECK(qa_q3_base_weapon_count(QA_Q3_BASE_PRODUCT_MISSIONPACK) == 14);
  CHECK(!qa_q3_base_weapon_available(QA_Q3_BASE_PRODUCT_MISSIONPACK, QA_Q3_WP_NONE));
  CHECK(qa_q3_base_weapon_available(QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_WP_GRAPPLING_HOOK));
  CHECK(!qa_q3_base_weapon_available(QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_WP_NAILGUN));
  CHECK(qa_q3_base_weapon_available(QA_Q3_BASE_PRODUCT_MISSIONPACK, QA_Q3_WP_CHAINGUN));
  CHECK(strcmp(qa_q3_base_product_name(QA_Q3_BASE_PRODUCT_BASEQ3), "baseq3") == 0);
}

static void test_direction_byte(void) {
  qa_vec3 x = qa_math_vec3(1, 0, 0), mz = qa_math_vec3(0, 0, -1), neg = qa_math_vec3(-1, -1, -1);
  CHECK(qa_q3_base_direction_to_byte(NULL) == 0);
  CHECK(qa_q3_base_direction_to_byte(&x) == 52);
  CHECK(qa_q3_base_direction_to_byte(&mz) == 84);
  CHECK(qa_q3_base_direction_to_byte(&neg) == 160);
  CHECK(vec_eq(qa_q3_base_byte_to_direction(52), 1, 0, 0));
  CHECK(vec_eq(qa_q3_base_byte_to_direction(200), 0, 0, 0));
  CHECK(vec_eq(qa_q3_base_byte_to_direction(-1), 0, 0, 0));
  CHECK(qa_q3_base_byte_to_direction(0).x == (double)(float)-0.525731);
  CHECK_ABORTS((void)qa_q3_base_byte_to_direction(1.5));
  CHECK_ABORTS((void)qa_q3_base_byte_to_direction(9007199254740992.0));
}

static qa_q3_base_trajectory traj(double type, double time, double dur, qa_vec3 b, qa_vec3 d) {
  qa_q3_base_trajectory t = {type, time, dur, b, d};
  return t;
}
static void test_trajectory(void) {
  qa_q3_base_trajectory lin = traj(QA_Q3_BASE_TR_LINEAR, 1000, 0, qa_math_vec3(1, 2, 3), qa_math_vec3(100, -50, 25));
  CHECK(vec_eq(qa_q3_base_evaluate_trajectory(lin, 1500), 51, -23, 15.5));
  CHECK(vec_eq(qa_q3_base_evaluate_trajectory_delta(lin, 1500), 100, -50, 25));
  qa_q3_base_trajectory grav = traj(QA_Q3_BASE_TR_GRAVITY, 0, 0, qa_math_vec3(0, 0, 1000), qa_math_vec3(10, 0, 300));
  CHECK(vec_eq(qa_q3_base_evaluate_trajectory(grav, 250), 2.5, 0, 1050));
  CHECK(vec_eq(qa_q3_base_evaluate_trajectory_delta(grav, 250), 10, 0, 100));
  qa_q3_base_trajectory sine = traj(QA_Q3_BASE_TR_SINE, 0, 1000, qa_math_vec3(0, 0, 0), qa_math_vec3(0, 0, 64));
  CHECK(vec_eq(qa_q3_base_evaluate_trajectory(sine, 250), 0, 0, 64));
  CHECK(qa_q3_base_evaluate_trajectory_delta(sine, 250).z == (double)(float)-0.0000013987644251756137 || fabs(qa_q3_base_evaluate_trajectory_delta(sine, 250).z) < 1e-5);
  qa_q3_base_trajectory stop = traj(QA_Q3_BASE_TR_LINEAR_STOP, 100, 200, qa_math_vec3(0, 0, 0), qa_math_vec3(1000, 0, 0));
  CHECK(vec_eq(qa_q3_base_evaluate_trajectory(stop, 1000), 200, 0, 0));
  CHECK(vec_eq(qa_q3_base_evaluate_trajectory(stop, 50), 0, 0, 0));
  CHECK(vec_eq(qa_q3_base_evaluate_trajectory_delta(stop, 1000), 0, 0, 0));
  qa_q3_base_trajectory bad = traj(99, 5, 0, qa_math_vec3(0, 0, 0), qa_math_vec3(0, 0, 0));
  CHECK_ABORTS((void)qa_q3_base_evaluate_trajectory(bad, 0));
  CHECK_ABORTS((void)qa_q3_base_evaluate_trajectory_delta(bad, 0));
}

static double inv_ammo(void *ctx, qa_q3_weapon weapon) { (void)weapon; return *(double *)ctx; }
static double inv_powerup(void *ctx, qa_q3_powerup powerup) { (void)powerup; (void)ctx; return 0; }
static void test_items(void) {
  size_t count = 0;
  const qa_q3_base_item_definition *list = qa_q3_base_item_list(QA_Q3_BASE_PRODUCT_BASEQ3, &count);
  CHECK(count == 36);
  (void)qa_q3_base_item_list(QA_Q3_BASE_PRODUCT_MISSIONPACK, &count);
  CHECK(count == 52);
  CHECK(list[0].class_name == NULL && list[0].type == QA_Q3_BASE_IT_BAD);
  CHECK(strcmp(qa_q3_base_item_at(QA_Q3_BASE_PRODUCT_BASEQ3, 1)->class_name, "item_armor_shard") == 0);
  CHECK(qa_q3_base_item_at(QA_Q3_BASE_PRODUCT_BASEQ3, 1)->quantity == 5);
  CHECK(strcmp(qa_q3_base_item_at(QA_Q3_BASE_PRODUCT_MISSIONPACK, 51)->class_name, "weapon_chaingun") == 0);
  CHECK(qa_q3_base_find_item(QA_Q3_BASE_PRODUCT_BASEQ3, "BFG10K") == qa_q3_base_find_item(QA_Q3_BASE_PRODUCT_BASEQ3, "bfg10k"));
  CHECK(qa_q3_base_find_item(QA_Q3_BASE_PRODUCT_BASEQ3, "No Such") == NULL);
  const qa_q3_base_item_definition *quad = qa_q3_base_find_item_for_powerup(QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_PW_QUAD);
  CHECK(quad && strcmp(quad->class_name, "item_quad") == 0);
  CHECK(qa_q3_base_find_item_for_powerup(QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_PW_SCOUT) == NULL);
  CHECK(strcmp(qa_q3_base_find_item_for_weapon(QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_WP_BFG)->class_name, "weapon_bfg") == 0);
  CHECK(strcmp(qa_q3_base_find_item_for_holdable(QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_HI_MEDKIT)->class_name, "holdable_medkit") == 0);
  CHECK_ABORTS((void)qa_q3_base_item_at(QA_Q3_BASE_PRODUCT_BASEQ3, 36));
  CHECK_ABORTS((void)qa_q3_base_item_at(QA_Q3_BASE_PRODUCT_BASEQ3, 1.5));
  CHECK_ABORTS((void)qa_q3_base_find_item_for_holdable(QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_HI_KAMIKAZE));
  CHECK_ABORTS((void)qa_q3_base_find_item_for_weapon(QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_WP_NAILGUN));
  double ammo = 199;
  qa_q3_base_player_inventory inv = {0};
  inv.ctx = &ammo; inv.product = QA_Q3_BASE_PRODUCT_BASEQ3;
  inv.health = 100; inv.armor = 0; inv.max_health = 100; inv.holdable_item = 0; inv.team = QA_Q3_BASE_TEAM_FREE;
  inv.ammo = inv_ammo; inv.powerup = inv_powerup;
  qa_q3_base_pickup_entity ent = {19, 0, 0}; /* ammo_shells */
  CHECK(qa_q3_base_can_item_be_grabbed(QA_Q3_BASE_GT_FFA, ent, &inv));
  ammo = 200;
  CHECK(!qa_q3_base_can_item_be_grabbed(QA_Q3_BASE_GT_FFA, ent, &inv));
  ent.model_index = 0;
  CHECK_ABORTS((void)qa_q3_base_can_item_be_grabbed(QA_Q3_BASE_GT_FFA, ent, &inv));
  ent.model_index = 6; /* item_health_large, quantity 50 */
  CHECK(!qa_q3_base_can_item_be_grabbed(QA_Q3_BASE_GT_FFA, ent, &inv));
  ent.model_index = 7; /* mega, quantity 100: 2x cap */
  CHECK(qa_q3_base_can_item_be_grabbed(QA_Q3_BASE_GT_FFA, ent, &inv));
  qa_q3_base_trajectory at = traj(QA_Q3_BASE_TR_STATIONARY, 0, 0, qa_math_vec3(44, 36, -36), qa_math_vec3(0, 0, 0));
  CHECK(qa_q3_base_player_touches_item(qa_math_vec3(0, 0, 0), at, 0)); /* diffs -44,-36,36 all inside */
  at.base = qa_math_vec3(-44, 0, 0);
  CHECK(qa_q3_base_player_touches_item(qa_math_vec3(0, 0, 0), at, 0));
  at.base = qa_math_vec3(-45, 0, 0); /* player-origin x = 45 > 44 */
  CHECK(!qa_q3_base_player_touches_item(qa_math_vec3(0, 0, 0), at, 0));
}

static void test_player_state(void) {
  qa_q3_base_player_state ps;
  CHECK(qa_q3_base_create_player_state(&ps, QA_Q3_BASE_PRODUCT_BASEQ3, NULL));
  CHECK(ps.pm_type == QA_Q3_PM_NORMAL && ps.weapon == QA_Q3_WP_NONE && ps.weapon_state == QA_Q3_WEAPON_READY);
  qa_q3_base_player_state_record_set_health(&ps, 80);
  CHECK(qa_q3_base_player_state_record_health(&ps) == 80);
  qa_q3_base_player_state_slots_set(&ps.stats, 3, 50.9);
  CHECK(qa_q3_base_player_state_slots_get(&ps.stats, 3) == 50); /* Int32Array store */
  qa_q3_base_predictable_event ev = qa_q3_base_player_state_record_add_event(&ps, QA_Q3_EV_JUMP, 7);
  CHECK(ev.sequence == 0 && ev.event == QA_Q3_EV_JUMP && ev.parameter == 7);
  CHECK(ps.event_sequence == 1);
  CHECK(qa_q3_base_player_state_slots_get(&ps.events, 0) == QA_Q3_EV_JUMP);
  ev = qa_q3_base_player_state_record_add_event(&ps, QA_Q3_EV_PAIN, 0);
  CHECK(ev.sequence == 1 && qa_q3_base_player_state_slots_get(&ps.events, 1) == QA_Q3_EV_PAIN);
  qa_q3_base_player_state copy;
  CHECK(qa_q3_base_player_state_copy(&copy, &ps));
  CHECK(qa_q3_base_player_state_record_health(&copy) == 80 && copy.event_sequence == 2);
  qa_q3_base_player_state_record_destroy(&copy);
  CHECK_ABORTS(qa_q3_base_player_state_slots_set(&ps.stats, 16, 1));
  CHECK_ABORTS((void)qa_q3_base_player_state_slots_get(&ps.stats, -1));
  CHECK_ABORTS((void)qa_q3_base_player_state_slots_get(&ps.stats, 0.5));
  qa_q3_base_player_state_record_destroy(&ps);
  int32_t source[2] = {1, 2};
  qa_q3_base_player_state_slots borrowed;
  CHECK(qa_q3_base_player_state_slots_init(&borrowed, 2, source, 2, true, NULL, NULL, NULL));
  qa_q3_base_player_state_slots_set(&borrowed, 1, 9);
  CHECK(source[1] == 9);
  qa_q3_base_player_state_slots_destroy(&borrowed);
  CHECK_ABORTS({ qa_q3_base_player_state_slots bad; (void)qa_q3_base_player_state_slots_init(&bad, 3, source, 2, true, NULL, NULL, NULL); });
}

typedef struct fake_body { qa_body_state state; bool linked; qa_linked_body link; } fake_body;
static qa_body_state fake_read(void *ctx) { return ((fake_body *)ctx)->state; }
static void fake_write(void *ctx, qa_body_state value) { ((fake_body *)ctx)->state = value; }
static bool fake_linked(void *ctx, qa_linked_body *out) {
  fake_body *b = ctx;
  if (!b->linked) return false;
  *out = b->link;
  return true;
}
static qa_q3_base_entity_shared *view_target;
static bool view_seen;
static void view_call(void *ctx) {
  (void)ctx;
  view_seen = vec_eq(qa_q3_base_entity_shared_current_origin(view_target), 9, 9, 9);
  qa_q3_base_entity_shared_set_current_origin(view_target, (qa_vec3){5, 5, 5});
}
static void test_entity_shared_snapshot_jumppad(void) {
  fake_body body;
  memset(&body, 0, sizeof body);
  body.state.origin = (qa_vec3){1, 2, 3};
  body.link.absolute_bounds.min = (qa_vec3){-10, -10, -10};
  body.link.absolute_bounds.max = (qa_vec3){10, 10, 10};
  body.link.link_count = 4;
  qa_q3_base_entity_body_binding binding = {&body, fake_read, fake_write, fake_linked};
  qa_q3_base_entity_shared r;
  qa_q3_base_entity_shared_init(&r, binding);
  CHECK(r.model.kind == QA_Q3_BASE_MODEL_BOX);
  CHECK(!qa_q3_base_entity_shared_linked(&r));
  CHECK(qa_q3_base_entity_shared_linkcount(&r) == 0);
  CHECK(vec_eq(qa_q3_base_entity_shared_absmin(&r), 0, 0, 0));
  body.linked = true;
  qa_q3_base_entity_shared_capture_link(&r);
  CHECK(qa_q3_base_entity_shared_linkcount(&r) == 4);
  body.linked = false;
  CHECK(qa_q3_base_entity_shared_linkcount(&r) == 4); /* previous link */
  CHECK(vec_eq(qa_q3_base_entity_shared_absmax(&r), 10, 10, 10));
  qa_q3_base_entity_shared_set_absmin(&r, (qa_vec3){-1, -2, -3});
  CHECK(vec_eq(qa_q3_base_entity_shared_absmin(&r), -1, -2, -3));
  qa_q3_base_entity_shared_clear_bounds_overrides(&r);
  CHECK(vec_eq(qa_q3_base_entity_shared_absmin(&r), -10, -10, -10));
  qa_q3_base_entity_shared_set_mins(&r, (qa_vec3){-8, -8, -8});
  CHECK(vec_eq(qa_q3_base_entity_shared_mins(&r), -8, -8, -8));
  view_target = &r; view_seen = false;
  qa_q3_base_entity_shared_with_current_origin(&r, (qa_vec3){9, 9, 9}, NULL, view_call);
  CHECK(view_seen);
  CHECK(vec_eq(body.state.origin, 5, 5, 5)); /* set inside view writes body too */
  CHECK(vec_eq(qa_q3_base_entity_shared_current_origin(&r), 5, 5, 5));

  qa_q3_base_player_state ps;
  CHECK(qa_q3_base_create_player_state(&ps, QA_Q3_BASE_PRODUCT_BASEQ3, NULL));
  qa_q3_base_player_state_record_set_health(&ps, 100);
  ps.client_num = 3;
  qa_q3_base_player_state_record_set_origin(&ps, qa_math_vec3(10.7, -2.3, 0));
  qa_q3_base_player_state_record_add_event(&ps, QA_Q3_EV_JUMP, 11);
  qa_q3_entity_state_record es;
  qa_q3_entity_state_init(&es);
  es.pos.time = 777;
  qa_q3_base_player_state_to_entity_state(&ps, &es, true);
  CHECK(es.e_type == QA_Q3_BASE_ET_PLAYER && es.number == 3);
  CHECK(es.pos.type == QA_Q3_BASE_TR_INTERPOLATE && es.pos.time == 777);
  CHECK(vec_eq(es.pos.base, 10, -2, 0)); /* snap truncates */
  CHECK(es.event == (QA_Q3_EV_JUMP | (0 << 8)) && es.event_parm == 11);
  CHECK(ps.entity_event_sequence == 1);
  qa_q3_base_player_state_record_set_health(&ps, 0);
  qa_q3_base_player_state_to_entity_state_extrapolate(&ps, &es, 2000, false);
  CHECK(es.pos.type == QA_Q3_BASE_TR_LINEAR_STOP && es.pos.time == 2000 && es.pos.duration == 50);
  CHECK((es.e_flags & 1) != 0);
  /* Jump pad: first touch adds event + sets velocity from origin2. */
  qa_q3_base_player_state_record_set_health(&ps, 100);
  es.number = 42;
  es.origin2 = qa_math_vec3(0, 0, 500);
  double before = ps.event_sequence;
  qa_q3_base_touch_jump_pad(&ps, &es);
  CHECK(ps.event_sequence == before + 1);
  CHECK(ps.jumppad_ent == 42);
  CHECK(vec_eq(qa_q3_base_player_state_record_velocity(&ps), 0, 0, 500));
  qa_q3_base_touch_jump_pad(&ps, &es);
  CHECK(ps.event_sequence == before + 1); /* same pad: no second event */
  qa_q3_base_player_state_slots_set(&ps.powerups, QA_Q3_PW_FLIGHT, 1000);
  qa_q3_base_player_state_record_set_velocity(&ps, qa_math_vec3(1, 1, 1));
  qa_q3_base_touch_jump_pad(&ps, &es);
  CHECK(vec_eq(qa_q3_base_player_state_record_velocity(&ps), 1, 1, 1)); /* flight: no-op */
  qa_q3_base_player_state_record_destroy(&ps);
}

static void test_settings_definitions(void) {
  size_t count = 0;
  const qa_q3_base_cvar_definition *defs = qa_q3_base_game_cvar_definitions(QA_Q3_BASE_PRODUCT_BASEQ3, &count);
  CHECK(count == 45);
  CHECK(strcmp(defs[0].name, "sv_cheats") == 0 && strcmp(defs[0].value, "") == 0 && defs[0].flags == 0);
  CHECK(strcmp(defs[2].name, "g_gametype") == 0);
  CHECK(defs[2].flags == (QA_CVAR_FLAG_SERVER_INFO | QA_CVAR_FLAG_USER_INFO | QA_CVAR_FLAG_LATCH));
  defs = qa_q3_base_game_cvar_definitions(QA_Q3_BASE_PRODUCT_MISSIONPACK, &count);
  CHECK(count == 56);
  bool found_redteam = false;
  for (size_t i = 0; i < count; i++)
    if (strcmp(defs[i].name, "g_redteam") == 0) found_redteam = defs[i].track && defs[i].team_shader && strcmp(defs[i].value, "Stroggs") == 0;
  CHECK(found_redteam);
}

static void test_objectives(void) {
  const char *missing[3];
  const char *names[] = {"worldspawn", NULL, "team_CTF_redflag"};
  qa_q3_ta_objective_placement_result r =
      qa_q3_ta_check_objective_placements(QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_BASE_GT_CTF, names, 3, missing);
  CHECK(r.kind == QA_Q3_TA_OBJECTIVE_MISSING && r.classname_count == 1);
  CHECK(strcmp(r.classnames[0], "team_CTF_blueflag") == 0);
  r = qa_q3_ta_check_objective_placements(QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_BASE_GT_FFA, NULL, 0, missing);
  CHECK(r.kind == QA_Q3_TA_OBJECTIVE_READY);
  r = qa_q3_ta_check_objective_placements(QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_BASE_GT_1FCTF, NULL, 0, missing);
  CHECK(r.kind == QA_Q3_TA_OBJECTIVE_UNSUPPORTED && r.product == QA_Q3_BASE_PRODUCT_BASEQ3 && r.game_type == 5);
  r = qa_q3_ta_check_objective_placements(QA_Q3_BASE_PRODUCT_MISSIONPACK, 99, NULL, 0, missing);
  CHECK(r.kind == QA_Q3_TA_OBJECTIVE_UNSUPPORTED);

  const char *ents = "{\n\"classname\" \"worldspawn\"\n}\n"
                     "{\n\"classname\" \"item_flag_team1\"\n\"origin\" \"0 0 0\"\n}\n"
                     "{\n\"classname\" \"item_flag_team2\"\n\"origin\" \"10 0 0\"\n}\n"
                     "{\n\"classname\" \"info_player_team1\"\n\"origin\" \"1 2 3\"\n}\n";
  qa_q3_ta_foreign_objective_adaptation a =
      qa_q3_ta_adapt_foreign_q3_objectives(QA_Q3_TA_WORLD_Q1_BSP, ents, QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_BASE_GT_CTF, NULL, 0);
  CHECK(a.kind == QA_Q3_TA_OBJECTIVE_READY && a.translated == 4);
  const char *want = "{\n\"classname\" \"worldspawn\"\n}\n"
                     "{\n\"origin\" \"0 0 0\"\n\"classname\" \"team_CTF_redflag\"\n}\n"
                     "{\n\"origin\" \"10 0 0\"\n\"classname\" \"team_CTF_blueflag\"\n}\n"
                     "{\n\"origin\" \"1 2 3\"\n\"classname\" \"team_CTF_redplayer\"\n}\n"
                     "{\n\"origin\" \"1 2 3\"\n\"classname\" \"team_CTF_redspawn\"\n}\n";
  CHECK(a.entities && strcmp(a.entities, want) == 0);
  qa_q3_ta_foreign_objective_adaptation_destroy(&a);
  a = qa_q3_ta_adapt_foreign_q3_objectives(QA_Q3_TA_WORLD_Q3_BSP, ents, QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_BASE_GT_CTF, NULL, 0);
  CHECK(a.kind == QA_Q3_TA_OBJECTIVE_MISSING && a.missing_count == 2);
  qa_q3_ta_foreign_objective_adaptation_destroy(&a);
  a = qa_q3_ta_adapt_foreign_q3_objectives(QA_Q3_TA_WORLD_Q1_BSP, ents, QA_Q3_BASE_PRODUCT_BASEQ3, QA_Q3_BASE_GT_1FCTF, NULL, 0);
  CHECK(a.kind == QA_Q3_TA_OBJECTIVE_UNSUPPORTED && a.game_type == 5);
  qa_q3_ta_foreign_objective_adaptation_destroy(&a);
}

int main(void) {
  test_definitions();
  test_direction_byte();
  test_trajectory();
  test_items();
  test_player_state();
  test_entity_shared_snapshot_jumppad();
  test_settings_definitions();
  test_objectives();
  printf("q3_base_ta: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
