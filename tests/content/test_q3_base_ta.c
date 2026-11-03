/*
 * Tests for content/q3/base shared + settings ports. Expected values are
 * produced by the TypeScript implementation (bun dumps of
 * shared/trajectory.ts, direction-byte.ts, items.ts; see docs/PORTING.md).
 * TypeScript throw cases run in a child process and must abort.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include "qa/foundation/js.h"
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

#include "q3_fake_entity.h"
#include "qa/content/q3/base/map_spawns.h"
#include "qa/content/q3/base/records.h"
#include "qa/content/q3/base/settings.h"
#include "qa/content/q3/team_arena/client_admission.h"
#include "qa/content/q3/team_arena/client_effects.h"
#include "qa/content/q3/team_arena/client_policy.h"
#include "qa/content/q3/team_arena/client_spawn.h"
#include "qa/content/q3/team_arena/commands.h"
#include "qa/content/q3/team_arena/foreign_objectives.h"
#include "qa/content/q3/team_arena/match.h"
#include "qa/content/q3/team_arena/objective_placement.h"
#include "qa/content/q3/team_arena/server_commands.h"
#include "qa/content/q3/team_arena/session.h"
#include "qa/content/q3/team_arena/team.h"
#include "qa/core/cvars/index.h"
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
  CHECK((qa_js_to_int32(es.e_flags) & 1) != 0);
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
  /* q3GameCvarDefinitions("baseq3").length under bun: 48 (finalCvars has pmove_ladders, sv_rankingsGameKey/ID). */
  CHECK(count == 48);
  bool found_ladders = false;
  for (size_t i = 0; i < count; i++)
    if (strcmp(defs[i].name, "pmove_ladders") == 0) found_ladders = strcmp(defs[i].value, "1") == 0 && defs[i].flags == QA_CVAR_FLAG_SYSTEM_INFO; /* bun: value "1", flags 8 */
  CHECK(found_ladders);
  CHECK(strcmp(defs[0].name, "sv_cheats") == 0 && strcmp(defs[0].value, "") == 0 && defs[0].flags == 0);
  CHECK(strcmp(defs[2].name, "g_gametype") == 0);
  CHECK(defs[2].flags == (QA_CVAR_FLAG_SERVER_INFO | QA_CVAR_FLAG_USER_INFO | QA_CVAR_FLAG_LATCH));
  defs = qa_q3_base_game_cvar_definitions(QA_Q3_BASE_PRODUCT_MISSIONPACK, &count);
  CHECK(count == 59);
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

/* ---- w6c-q3-rest slice ---- */

/* TS new PlayerStateSlots(length); release with qa_q3_base_player_state_slots_destroy. */
static void slots_init4(qa_q3_base_player_state_slots *slots, size_t length) {
  if (!qa_q3_base_player_state_slots_init(slots, (double)length, NULL, 0, false, NULL, NULL, NULL)) abort();
}

typedef struct fake_cvars {
  char names[8][32];
  char values[8][128];
  size_t count;
} fake_cvars;
static const char *fake_cvar_get(void *ctx, const char *name) {
  fake_cvars *store = ctx;
  for (size_t i = 0; i < store->count; i++)
    if (strcmp(store->names[i], name) == 0) return store->values[i];
  return "";
}
static void fake_cvar_set(void *ctx, const char *name, const char *value) {
  fake_cvars *store = ctx;
  for (size_t i = 0; i < store->count; i++)
    if (strcmp(store->names[i], name) == 0) { strcpy(store->values[i], value); return; }
  strcpy(store->names[store->count], name);
  strcpy(store->values[store->count], value);
  store->count++;
}
static void fake_print_abort(void *ctx, const char *message) {
  (void)ctx; (void)message;
  fprintf(stderr, "unexpected session print: %s", message);
  failures++;
}
static double broadcast_log[4];
static size_t broadcast_count;
static void fake_broadcast(void *ctx, double client_num, double old_team) {
  (void)ctx; (void)old_team;
  broadcast_log[broadcast_count++] = client_num;
}
static const char *empty_team(void *ctx, const char *key) { (void)ctx; (void)key; return ""; }

/* Port of tests/gameplay/q3/modes/session.test.ts case 1. */
static void test_session_manager(void) {
  qa_q3_game_client red, blue, joining;
  qa_q3_game_client_init(&red, QA_Q3_BASE_PRODUCT_MISSIONPACK);
  qa_q3_game_client_init(&blue, QA_Q3_BASE_PRODUCT_MISSIONPACK);
  qa_q3_game_client_init(&joining, QA_Q3_BASE_PRODUCT_MISSIONPACK);
  red.pers.connected = blue.pers.connected = QA_Q3_CONN_CONNECTED;
  red.sess.session_team = QA_Q3_BASE_TEAM_RED;
  blue.sess.session_team = QA_Q3_BASE_TEAM_BLUE;
  qa_q3_base_player_state_slots scores;
  slots_init4(&scores, 4);
  qa_q3_base_player_state_slots_set(&scores, QA_Q3_BASE_TEAM_BLUE, 2);
  qa_q3_game_client *clients[] = {&red, &blue, &joining};
  qa_q3_ta_session_world_state world;
  memset(&world, 0, sizeof world);
  world.clients = clients; world.client_count = 3; world.max_clients = 3;
  world.team_scores = &scores;
  world.game_type = QA_Q3_BASE_GT_CTF; world.team_auto_join = true;
  world.time = 24000; world.num_non_spectator_clients = 2;
  fake_cvars store;
  memset(&store, 0, sizeof store);
  broadcast_count = 0;
  qa_q3_ta_session_services services;
  memset(&services, 0, sizeof services);
  services.cvars.ctx = &store; services.cvars.get = fake_cvar_get; services.cvars.set = fake_cvar_set;
  services.print = fake_print_abort; services.broadcast_team_change = fake_broadcast;
  qa_q3_ta_game_session_manager manager;
  qa_q3_ta_game_session_manager_init(&manager, &world, services);
  CHECK(qa_q3_ta_pick_team(&world, -1) == QA_Q3_BASE_TEAM_RED);
  qa_q3_ta_session_userinfo info = {NULL, empty_team};
  qa_q3_ta_game_session_manager_initialize_client(&manager, 2, info);
  CHECK(joining.sess.session_team == QA_Q3_BASE_TEAM_RED);
  CHECK(broadcast_count == 1 && broadcast_log[0] == 2);
  CHECK(strcmp(fake_cvar_get(&store, "session2"), "1 24000 1 0 0 0 0") == 0);
  joining.sess.wins = 3; joining.sess.losses = 1; joining.sess.team_leader = 1;
  qa_q3_ta_game_session_manager_write_client(&manager, 2);
  joining.sess.wins = joining.sess.losses = joining.sess.team_leader = 0;
  qa_q3_ta_game_session_manager_read_client(&manager, 2);
  CHECK(joining.sess.wins == 3 && joining.sess.losses == 1 && joining.sess.team_leader == 1);
  CHECK_ABORTS(qa_q3_ta_game_session_manager_read_client(&manager, 7));
  /* Duel admission (TS case 2). */
  qa_q3_game_client duel;
  qa_q3_game_client_init(&duel, QA_Q3_BASE_PRODUCT_BASEQ3);
  qa_q3_base_player_state_slots scores2;
  slots_init4(&scores2, 4);
  qa_q3_game_client *duel_clients[] = {&duel};
  qa_q3_ta_session_world_state world2;
  memset(&world2, 0, sizeof world2);
  world2.clients = duel_clients; world2.client_count = 1; world2.max_clients = 1;
  world2.team_scores = &scores2;
  world2.game_type = QA_Q3_BASE_GT_TOURNAMENT; world2.time = 100;
  world2.num_non_spectator_clients = 2;
  qa_q3_ta_game_session_manager manager2;
  qa_q3_ta_game_session_manager_init(&manager2, &world2, services);
  qa_q3_ta_game_session_manager_initialize_client(&manager2, 0, info);
  CHECK(duel.sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR);
  CHECK(duel.sess.spectator_state == QA_Q3_SPEC_FREE);
  duel.buttons = QA_Q3_BUTTON_ATTACK;
  duel.pers.cmd.buttons = QA_Q3_BUTTON_ATTACK;
  qa_q3_ta_client_intermission_think(&duel);
  CHECK(!duel.ready_to_exit);
  duel.pers.cmd.buttons = 0;
  qa_q3_ta_client_intermission_think(&duel);
  duel.pers.cmd.buttons = QA_Q3_BUTTON_ATTACK;
  qa_q3_ta_client_intermission_think(&duel);
  CHECK(duel.ready_to_exit);
  qa_q3_game_client_destroy(&red); qa_q3_game_client_destroy(&blue);
  qa_q3_game_client_destroy(&joining); qa_q3_game_client_destroy(&duel);
  qa_q3_base_player_state_slots_destroy(&scores); qa_q3_base_player_state_slots_destroy(&scores2);
}

static void test_admission_pure(void) {
  char *value = qa_q3_ta_client_info_value("\\name\\Fox\\team\\red", "TEAM");
  CHECK(strcmp(value, "red") == 0); free(value);
  value = qa_q3_ta_client_info_value("name\\Fox", "name");
  CHECK(strcmp(value, "Fox") == 0); free(value);
  value = qa_q3_ta_client_info_value("\\name\\Fox", "missing");
  CHECK(strcmp(value, "") == 0); free(value);
  value = qa_q3_ta_clean_client_name("  ^1Red  Fox  ");
  CHECK(strcmp(value, "^1Red  Fox  ") == 0); free(value);
  value = qa_q3_ta_clean_client_name("^7^0");
  CHECK(strcmp(value, "UnnamedPlayer") == 0); free(value);
  value = qa_q3_ta_clean_client_name("a    b");
  CHECK(strcmp(value, "a   b") == 0); free(value);
  qa_q3_game_client client;
  qa_q3_game_client_init(&client, QA_Q3_BASE_PRODUCT_BASEQ3);
  free(client.pers.netname);
  client.pers.netname = strdup("Fox");
  client.pers.max_health = 100;
  char *config = qa_q3_ta_client_presentation_config(&client,
      "\\name\\Fox\\model\\sarge\\headmodel\\sarge", QA_Q3_BASE_GT_FFA, false, QA_Q3_BASE_TEAM_FREE);
  CHECK(strcmp(config, "n\\Fox\\t\\0\\model\\sarge\\hmodel\\sarge\\g_redteam\\\\g_blueteam\\\\c1\\\\c2\\\\hc\\100\\w\\0\\l\\0\\tt\\0\\tl\\0") == 0);
  free(config);
  qa_q3_game_client_destroy(&client);
}

static void test_commands_pure(void) {
  const char *argv[] = {"say", "hello", "world"};
  char *joined = qa_q3_ta_concat_command_args(argv, 3, 1);
  CHECK(strcmp(joined, "hello world") == 0); free(joined);
  const char *one[] = {"a"};
  joined = qa_q3_ta_concat_command_args(one, 1, 5);
  CHECK(strcmp(joined, "") == 0); free(joined);
  CHECK_ABORTS(free(qa_q3_ta_concat_command_args(one, 1, -1)));
  CHECK_ABORTS(free(qa_q3_ta_concat_command_args(one, 1, 0.5)));
}

static void test_effects_pure(void) {
  const qa_q3_ta_ammo_regeneration_rule *rule = qa_q3_ta_ammo_regeneration_rule_for(QA_Q3_WP_MACHINEGUN);
  CHECK(rule->max == 50 && rule->increment == 4 && rule->time == 1000);
  qa_q3_ta_ammo_regeneration_step step = qa_q3_ta_step_ammo_regeneration(rule, 10, 500, 600);
  CHECK(step.has_count && step.count == 14 && step.milliseconds == 100);
  step = qa_q3_ta_step_ammo_regeneration(rule, 50, 500, 600);
  CHECK(!step.has_count && step.milliseconds == 0);
  step = qa_q3_ta_step_ammo_regeneration(qa_q3_ta_ammo_regeneration_rule_for(QA_Q3_WP_SHOTGUN), 9, 0, 3200);
  CHECK(step.has_count && step.count == 10 && step.milliseconds == 200);
  CHECK_ABORTS((void)qa_q3_ta_ammo_regeneration_rule_for(QA_Q3_WP_GAUNTLET));
  qa_q3_base_player_state ps;
  CHECK(qa_q3_base_create_player_state(&ps, QA_Q3_BASE_PRODUCT_BASEQ3, NULL));
  CHECK(qa_q3_ta_client_speed_multiplier(&ps) == 1);
  qa_q3_base_player_state_slots_set(&ps.powerups, QA_Q3_PW_HASTE, 1);
  CHECK(qa_q3_ta_client_speed_multiplier(&ps) == 1.3);
  qa_q3_base_player_state_record_destroy(&ps);
}

static void test_team_pure(void) {
  CHECK(qa_q3_ta_other_team(QA_Q3_BASE_TEAM_RED) == QA_Q3_BASE_TEAM_BLUE);
  CHECK(qa_q3_ta_other_team(QA_Q3_BASE_TEAM_FREE) == QA_Q3_BASE_TEAM_FREE);
  CHECK(strcmp(qa_q3_ta_team_name(QA_Q3_BASE_TEAM_BLUE), "BLUE") == 0);
  CHECK(strcmp(qa_q3_ta_other_team_name(QA_Q3_BASE_TEAM_RED), "BLUE") == 0);
  qa_q3_ta_team_game_state state;
  qa_q3_ta_team_game_state_init(&state);
  CHECK(state.red_status == 0 && state.blue_status == 0 && state.last_flag_capture == 0);
}

/* Fake entity pool over static storage for pool-driven modules. */
static qa_q3_game_entity fake_entities[QA_Q3_MAX_GENTITIES];
static qa_q3_game_client fake_clients[QA_Q3_MAX_CLIENTS];
static q3_fake_entity fake_bodies[QA_Q3_MAX_GENTITIES];
static qa_q3_game_entity *fake_get(void *ctx, double slot) {
  (void)ctx;
  if (slot < 0 || slot >= QA_Q3_MAX_GENTITIES || slot != (double)(int)slot) return NULL;
  return &fake_entities[(int)slot];
}
static qa_q3_game_client *fake_client(void *ctx, double slot) {
  (void)ctx;
  return &fake_clients[(int)slot];
}
static qa_q3_game_entity *fake_activate(void *ctx, double slot) {
  (void)ctx;
  fake_bodies[(int)slot].active = true;
  return &fake_entities[(int)slot];
}
static void fake_release(void *ctx, qa_q3_game_entity *entity) {
  (void)ctx;
  fake_bodies[(int)entity->slot].active = false;
}
static void fake_deactivate(void *ctx, double slot) { (void)ctx; fake_bodies[(int)slot].active = false; }
static double fake_time;
static double fake_pool_time(void *ctx) { (void)ctx; return fake_time; }
static void fake_pool_print(void *ctx, const char *message) { (void)ctx; (void)message; }
static qa_q3_entity_pool fake_pool;
static bool fake_pool_ready;
static void make_fake_pool(double max_clients) {
  if (fake_pool_ready) {
    qa_q3_entity_pool_destroy(&fake_pool);
    for (int i = 0; i < QA_Q3_MAX_GENTITIES; i++) qa_q3_game_entity_destroy(&fake_entities[i]);
    for (int i = 0; i < QA_Q3_MAX_CLIENTS; i++) qa_q3_game_client_destroy(&fake_clients[i]);
  }
  for (int slot = 0; slot < QA_Q3_MAX_GENTITIES; slot++)
    q3_fake_entity_init(&fake_entities[slot], &fake_bodies[slot], slot);
  for (int slot = 0; slot < QA_Q3_MAX_CLIENTS; slot++)
    qa_q3_game_client_init(&fake_clients[slot], QA_Q3_BASE_PRODUCT_BASEQ3);
  qa_q3_entity_pool_options options;
  memset(&options, 0, sizeof options);
  options.records.ctx = NULL;
  options.records.get = fake_get;
  options.records.native_by_actor = NULL;
  options.records.client = fake_client;
  options.records.activate = fake_activate;
  options.records.release = fake_release;
  options.records.deactivate_client = fake_deactivate;
  options.product = QA_Q3_BASE_PRODUCT_BASEQ3;
  options.max_clients = max_clients;
  options.map_start_time = 0;
  options.ctx = NULL;
  options.time = fake_pool_time;
  options.print = fake_pool_print;
  qa_q3_entity_pool_init(&fake_pool, options);
  fake_pool_ready = true;
}

static void dummy_die_one(void *ctx, qa_q3_game_entity *self, qa_q3_damage_inflictor inflictor,
    qa_q3_damage_participant attacker, double damage, double method) {
  (void)ctx; (void)self; (void)inflictor; (void)attacker; (void)damage; (void)method;
}
static void dummy_die_two(void *ctx, qa_q3_game_entity *self, qa_q3_damage_inflictor inflictor,
    qa_q3_damage_participant attacker, double damage, double method) {
  (void)ctx; (void)self; (void)inflictor; (void)attacker; (void)damage; (void)method;
}
static qa_q3_entity_callback dummy_die_callback(int which) {
  qa_q3_entity_callback cb = {NULL, NULL};
  qa_q3_entity_die_fn fn = which == 1 ? dummy_die_one : dummy_die_two;
  memcpy(&cb.fn, &fn, sizeof fn < sizeof cb.fn ? sizeof fn : sizeof cb.fn);
  return cb;
}
static void test_find_teams_and_spawn_points(void) {
  fake_time = 0;
  make_fake_pool(4);
  fake_pool.num_entities = 70;
  /* findQ3EntityTeams: slots 64+1.. use indices 65,66 sharing a team. */
  for (int slot = 65; slot <= 67; slot++) {
    fake_bodies[slot].active = true;
    fake_entities[slot].team = strdup("alpha");
  }
  fake_entities[66].targetname = strdup("shared");
  qa_q3_base_entity_teams result = qa_q3_base_find_entity_teams(&fake_pool);
  CHECK(result.teams == 1 && result.entities == 3);
  CHECK(fake_entities[65].teammaster == &fake_entities[65]);
  CHECK(fake_entities[65].targetname && strcmp(fake_entities[65].targetname, "shared") == 0);
  CHECK(fake_entities[66].targetname == NULL);
  CHECK((int32_t)fake_entities[66].flags & QA_Q3_GAME_FLAG_TEAMSLAVE);
  /* Spawn selection + view angle + deathmatch flags. */
  fake_bodies[68].active = fake_bodies[69].active = true;
  qa_q3_game_entity_set_classname(&fake_entities[68], "info_player_deathmatch");
  qa_q3_game_entity_set_classname(&fake_entities[69], "info_player_deathmatch");
  fake_entities[68].s.origin = (qa_vec3){100, 0, 0};
  fake_entities[69].s.origin = (qa_vec3){10, 0, 0};
  qa_q3_ta_client_spawn_host spawn_host;
  memset(&spawn_host, 0, sizeof spawn_host);
  spawn_host.pool = &fake_pool;
  spawn_host.think_pool = &fake_pool;
  spawn_host.player_die = dummy_die_callback(1);
  spawn_host.body_die = dummy_die_callback(2);
  qa_q3_ta_client_spawn_runtime spawns;
  qa_q3_ta_client_spawn_runtime_init(&spawns, spawn_host, NULL);
  CHECK(qa_q3_ta_client_spawn_runtime_select_nearest(&spawns, (qa_vec3){0, 0, 0}) == &fake_entities[69]);
  qa_q3_spawn_pair pairs[] = {{"nobots", "1"}, {"nohumans", "0"}};
  qa_q3_spawn_variables vars;
  qa_q3_spawn_variables_init(&vars, pairs, 2);
  qa_q3_ta_spawn_deathmatch_point(&fake_entities[69], &vars);
  CHECK((int32_t)fake_entities[69].flags & QA_Q3_GAME_FLAG_NO_BOTS);
  CHECK(!((int32_t)fake_entities[69].flags & QA_Q3_GAME_FLAG_NO_HUMANS));
  qa_q3_spawn_variables_destroy(&vars);
  fake_entities[0].client = &fake_clients[0];
  qa_q3_ta_set_client_view_angle(&fake_entities[0], (qa_vec3){90, 0, 0});
  CHECK(fake_clients[0].ps.delta_angles.x == 16384);
}

typedef struct match_capture {
  qa_q3_ta_match_settings settings;
  char config[24][64];
  char console[256];
  int ranks;
} match_capture;
static qa_q3_ta_match_settings match_settings_of(void *ctx) { return ((match_capture *)ctx)->settings; }
static void match_config(void *ctx, double index, const char *text) {
  match_capture *cap = ctx;
  if (index >= 0 && index < 24) strcpy(cap->config[(int)index], text);
}
static void match_console(void *ctx, const char *text) { strcpy(((match_capture *)ctx)->console, text); }
static void match_send_noop(void *ctx, double client_num, const char *text) { (void)ctx; (void)client_num; (void)text; }
static void match_log_noop(void *ctx, const char *text) { (void)ctx; (void)text; }

static void test_match_ranks_and_vote(void) {
  make_fake_pool(4);
  fake_time = 5000;
  qa_q3_ta_match_state state;
  qa_q3_ta_match_state_init(&state);
  state.time = 5000;
  qa_q3_base_player_state_slots scores;
  slots_init4(&scores, 4);
  match_capture cap;
  memset(&cap, 0, sizeof cap);
  cap.settings.game_type = QA_Q3_BASE_GT_FFA;
  qa_game_random random;
  qa_game_random_init(&random, 1);
  qa_q3_ta_match_host host;
  memset(&host, 0, sizeof host);
  host.product = QA_Q3_BASE_PRODUCT_BASEQ3;
  host.state = &state;
  host.pool = &fake_pool;
  host.team_scores = &scores;
  host.random = &random;
  host.ctx = &cap;
  host.settings = match_settings_of;
  host.set_configstring = match_config;
  host.append_console_command = match_console;
  host.send_server_command = match_send_noop;
  host.log = match_log_noop;
  qa_q3_ta_match_runtime match;
  qa_q3_ta_match_runtime_init(&match, host, NULL);
  for (int i = 0; i < 2; i++) {
    fake_clients[i].pers.connected = QA_Q3_CONN_CONNECTED;
    fake_clients[i].sess.session_team = QA_Q3_BASE_TEAM_FREE;
    fake_clients[i].sess.spectator_client = 0;
  }
  qa_q3_base_player_state_slots_set(&fake_clients[0].ps.persistant, QA_Q3_BASE_PERS_SCORE, 3);
  qa_q3_base_player_state_slots_set(&fake_clients[1].ps.persistant, QA_Q3_BASE_PERS_SCORE, 7);
  qa_q3_ta_match_runtime_calculate_ranks(&match);
  CHECK(state.num_playing_clients == 2);
  CHECK(state.sorted_clients[0] == 1 && state.sorted_clients[1] == 0);
  CHECK(strcmp(cap.config[6], "7") == 0 && strcmp(cap.config[7], "3") == 0);
  CHECK(!qa_q3_ta_match_runtime_score_is_tied(&match));
  /* Vote passes then executes after the delay. */
  state.vote.time = 5000;
  state.vote.yes = 2;
  state.num_voting_clients = 2;
  free(state.vote.string);
  state.vote.string = strdup("map q3dm1");
  qa_q3_ta_match_runtime_check_vote(&match);
  CHECK(state.vote.time == 0 && state.vote.execute_time == 8000);
  state.time = 8001;
  qa_q3_ta_match_runtime_check_vote(&match);
  CHECK(strcmp(cap.console, "map q3dm1\n") == 0);
  qa_q3_ta_match_state_destroy(&state);
  qa_q3_base_player_state_slots_destroy(&scores);
}

typedef struct server_capture {
  char printed[1024];
  char banned[64];
} server_capture;
static qa_cvar_snapshot server_read_cvar(void *ctx, const char *name) {
  server_capture *cap = ctx;
  qa_cvar_snapshot snap;
  memset(&snap, 0, sizeof snap);
  snap.name = strdup(name);
  snap.value = strdup(strcmp(name, "g_banIPs") == 0 ? cap->banned : "");
  snap.reset_value = strdup("");
  snap.integer_value = strcmp(name, "dedicated") == 0 ? 1 : strcmp(name, "g_filterBan") == 0 ? 1 : 0;
  return snap;
}
static void server_print(void *ctx, const char *text) {
  strcat(((server_capture *)ctx)->printed, text);
}
static void test_server_commands(void) {
  make_fake_pool(2);
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "server-commands-test"));
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context.session = owner.session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&options);
  CHECK(cvars != NULL);
  qa_cvar_snapshot snap;
  if (qa_cvar_registry_register(cvars, "g_banIPs", "", 0, &snap)) qa_cvar_read_free(&snap);
  server_capture cap;
  memset(&cap, 0, sizeof cap);
  qa_q3_ta_game_server_command_host host;
  memset(&host, 0, sizeof host);
  host.ctx = &cap;
  host.read_vm_cvar = server_read_cvar;
  host.print = server_print;
  host.bots.kind = QA_Q3_TA_CAP_UNAVAILABLE; host.bots.reason = "no bots";
  host.memory.kind = QA_Q3_TA_CAP_UNAVAILABLE; host.memory.reason = "no memory";
  host.podium.kind = QA_Q3_TA_CAP_UNAVAILABLE; host.podium.reason = "no podium";
  qa_q3_ta_game_server_command_runtime runtime;
  qa_q3_ta_game_server_command_runtime_init(&runtime, &fake_pool, cvars, host, NULL);
  CHECK(!qa_q3_ta_game_server_command_runtime_filter_packet(&runtime, "10.0.0.5:27960"));
  const char *add[] = {"addip", "10.0.0.*"};
  CHECK(qa_q3_ta_game_server_command_runtime_console_command(&runtime, add, 2));
  CHECK(qa_q3_ta_game_server_command_runtime_filter_packet(&runtime, "10.0.0.5:27960"));
  CHECK(!qa_q3_ta_game_server_command_runtime_filter_packet(&runtime, "192.168.0.5"));
  const char *remove[] = {"removeip", "10.0.0.*"};
  CHECK(qa_q3_ta_game_server_command_runtime_console_command(&runtime, remove, 2));
  CHECK(strstr(cap.printed, "Removed.\n") != NULL);
  CHECK(!qa_q3_ta_game_server_command_runtime_filter_packet(&runtime, "10.0.0.5"));
  fake_clients[0].pers.connected = QA_Q3_CONN_CONNECTED;
  CHECK(qa_q3_ta_game_server_command_runtime_client_for_string(&runtime, "0") == &fake_clients[0]);
  qa_q3_ta_game_server_command_runtime_destroy(&runtime);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&owner);
}

static qa_bounds hook_bounds(void *ctx, qa_owned_actor actor, qa_body_state state) {
  (void)ctx; (void)actor;
  qa_bounds bounds = {state.origin, state.origin};
  return bounds;
}
static void hook_noop_link(void *ctx, qa_linked_body body) { (void)ctx; (void)body; }
static void hook_noop_unlink(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; }
static bool hook_damage_call(void *ctx, qa_q3_damage_call *out) { (void)ctx; (void)out; return false; }
static qa_q3_game_entity *hook_foreign(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return NULL; }
static bool hook_is_player(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return false; }
static void hook_schedule(void *ctx, qa_owned_actor actor, bool has_due, double due) {
  (void)ctx; (void)actor; (void)has_due; (void)due;
}
static void hook_run_think(void *ctx, qa_owned_actor actor, double time) { (void)ctx; (void)actor; (void)time; }

static void test_records(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "records-test"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&owner, 2048);
  CHECK(actors != NULL);
  qa_body_link_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.absolute_bounds = hook_bounds;
  hooks.on_link = hook_noop_link;
  hooks.on_unlink = hook_noop_unlink;
  qa_shared_body_table *bodies = qa_shared_body_table_create(actors, hooks);
  CHECK(bodies != NULL);
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(actors);
  CHECK(callbacks != NULL);
  qa_gameplay_hooks gameplay_hooks;
  memset(&gameplay_hooks, 0, sizeof gameplay_hooks);
  qa_gameplay_authority *combat = qa_gameplay_authority_create(actors, callbacks, gameplay_hooks);
  CHECK(combat != NULL);
  qa_shared_inventory_table *inventory = qa_shared_inventory_table_create(actors);
  CHECK(inventory != NULL);
  qa_q3_base_record_host host;
  memset(&host, 0, sizeof host);
  host.actors = actors; host.bodies = bodies; host.combat = combat;
  host.inventory = inventory; host.callbacks = callbacks;
  host.schedule = hook_schedule; host.run_think = hook_run_think;
  host.damage_call = hook_damage_call; host.foreign = hook_foreign; host.is_player = hook_is_player;
  qa_q3_base_entity_records *records_ptr = qa_q3_base_entity_records_create(host, "q3:test", QA_Q3_BASE_PRODUCT_BASEQ3);
  CHECK(records_ptr != NULL);
  qa_q3_base_entity_records *records = records_ptr;
  CHECK(qa_q3_base_entity_records_get(records, 1024) == NULL);
  qa_q3_game_entity *entity = qa_q3_base_entity_records_activate(records, 100);
  CHECK(entity != NULL && entity->slot == 100);
  qa_actor_id actor = qa_q3_game_entity_actor(entity).id;
  CHECK(qa_q3_base_entity_records_native_by_actor(records, true, actor) == entity);
  CHECK(qa_q3_base_entity_records_by_actor(records, false, actor) == NULL);
  qa_q3_damage_inflictor inflictor = qa_q3_base_entity_records_damage_inflictor(records, false, actor);
  CHECK(inflictor.entity == qa_q3_base_entity_records_get(records, 1022));
  double stats[16], ammo[16];
  qa_q3_base_entity_records_capture_client_backing(records, 0, stats, ammo);
  stats[4] = 42;
  qa_q3_base_entity_records_restore_client_backing(records, 0, stats, 16, ammo, 16);
  double stats2[16], ammo2[16];
  qa_q3_base_entity_records_capture_client_backing(records, 0, stats2, ammo2);
  CHECK(stats2[4] == 42);
  CHECK_ABORTS(qa_q3_base_entity_records_restore_client_backing(records, 0, stats, 3, ammo, 16));
  qa_q3_base_entity_records_close(records);
  qa_q3_base_entity_records_free(records);
  qa_shared_inventory_table_destroy(inventory);
  qa_gameplay_authority_destroy(combat);
  qa_actor_callback_table_destroy(callbacks);
  qa_shared_body_table_destroy(bodies);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&owner);
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
  test_session_manager();
  test_admission_pure();
  test_commands_pure();
  test_effects_pure();
  test_team_pure();
  test_find_teams_and_spawn_points();
  test_match_ranks_and_vote();
  test_server_commands();
  test_records();
  printf("q3_base_ta: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
