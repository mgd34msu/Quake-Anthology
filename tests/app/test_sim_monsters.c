/* C checks for app/bootstrap/simulation monster-runtime, monster-placement,
 * monster-checkpoint, monster-sources, actor-execution and foreign-q2-world.
 * Reproduces tests/first-playable/simulation/cross-family-monster-placement
 * (nearbyMonsterPlacement), the preservesAuthoredQ1Placement mutations of
 * q1-rerelease-placement / monster-application over a synthetic world, the
 * actor-execution projections the Q1 foundation harnesses feed SharedPhysics,
 * and SelectedMonsters / readSelectedMonstersCheckpoint with expected values
 * from the TS classes run under bun on the same inputs. GPL-2.0-or-later. */
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

#include "../content/q1_world_fixture.h"
#include "sim_monsters_fixtures.h"
#include "qa/app/bootstrap/simulation/actor_execution.h"
#include "qa/app/bootstrap/simulation/foreign_q2_world.h"
#include "qa/app/bootstrap/simulation/monster_checkpoint.h"
#include "qa/app/bootstrap/simulation/monster_placement.h"
#include "qa/app/bootstrap/simulation/monster_runtime.h"
#include "qa/app/bootstrap/simulation/monster_sources.h"
#include "qa/content/q2/foundation/entity_services.h"
#include "qa/persistence/value.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/gameplay/policies.h"

static int checks, failures, ctx_errors;
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
    fflush(NULL);                                                       \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0);                                  \
      if (freopen("/dev/null", "w", stderr) == NULL) _exit(2);          \
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
static bool same(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

/* ---------------- nearbyMonsterPlacement ---------------- */
typedef struct placement_case {
  int magic;
  bool center_blocked;  /* first test */
  bool synthetic_floor; /* second test trace */
  int mode;             /* 0 plain, 1 point fraction 0, 2 descending fraction 1 */
  bool medium;
} placement_case;
#define PLACEMENT_MAGIC 0x504c4143
static placement_case *placement_of(void *ctx) {
  placement_case *c = ctx;
  if (c == NULL || c->magic != PLACEMENT_MAGIC) { ctx_errors++; abort(); }
  return c;
}
static bool is_point(const qa_trace_query *q) { return same(q->shape.kind, "point"); }
static qa_trace_result blank_result(qa_trace_query query) {
  qa_trace_result r;
  memset(&r, 0, sizeof r);
  r.kind = "q1"; r.end = query.end; r.in_open = true;
  r.source_plane_q1.normal = (qa_vec3){0, 0, 1};
  return r;
}
/* First TS test trace. */
static qa_trace_result overlap_trace(placement_case *c, qa_trace_query query) {
  qa_trace_result r = blank_result(query);
  bool point = is_point(&query);
  bool start_solid = !point && query.start.x == 0 && query.start.y == 0;
  bool end_solid = !point && query.end.x == 0 && query.end.y == 0;
  r.fraction = point && c->center_blocked ? 0 : 1;
  r.start_solid = start_solid;
  r.all_solid = start_solid && end_solid;
  return r;
}
/* Second TS test trace: solid where x + min.x < -0.5 or z + min.z < -8. */
static qa_trace_result floor_trace(qa_trace_query query) {
  qa_trace_result r = blank_result(query);
  qa_vec3 min = is_point(&query) ? (qa_vec3){0, 0, 0} : query.shape.bounds.min;
#define SOLID(px, pz) ((px) + min.x < -0.5 || (pz) + min.z < -8)
  bool start_solid = SOLID(query.start.x, query.start.z), end_solid = SOLID(query.end.x, query.end.z);
#undef SOLID
  double floor_fraction = query.end.z < query.start.z ? (-8 - min.z - query.start.z) / (query.end.z - query.start.z) : 1;
  double fraction = !start_solid && floor_fraction >= 0 && floor_fraction < 1 ? floor_fraction : 1;
  r.fraction = fraction;
  r.end = (qa_vec3){query.start.x + (query.end.x - query.start.x) * fraction, query.start.y + (query.end.y - query.start.y) * fraction,
                    query.start.z + (query.end.z - query.start.z) * fraction};
  r.start_solid = start_solid;
  r.all_solid = start_solid && end_solid;
  if (fraction < 1) {
    r.contact.has_plane = true;
    r.contact.plane.normal = (qa_vec3){0, 0, 1};
    r.contact.plane.distance = -8;
    r.hit.kind = QA_TRACE_HIT_WORLD;
  }
  r.source_plane_q1.distance = -8;
  return r;
}
static qa_trace_result placement_trace(void *ctx, qa_trace_query query) {
  placement_case *c = placement_of(ctx);
  if (!c->synthetic_floor) return overlap_trace(c, query);
  qa_trace_result r = floor_trace(query);
  if (c->mode == 1 && is_point(&query)) r.fraction = 0;
  if (c->mode == 2 && query.end.z < query.start.z) r.fraction = 1;
  return r;
}
static bool placement_medium(void *ctx, qa_vec3 origin) { (void)origin; return placement_of(ctx)->medium; }
static qa_trace_query placement_query(void) {
  qa_trace_query query;
  memset(&query, 0, sizeof query);
  query.policy.kind = "q1";
  query.policy.move = "normal";
  query.numeric.id = "test:binary32";
  return query;
}
static void test_nearby_placement(void) {
  qa_bounds bounds = {{-1, -1, -1}, {1, 1, 1}};
  for (int blocked = 0; blocked < 2; blocked++) {
    placement_case c = {PLACEMENT_MAGIC, blocked != 0, false, 0, true};
    qa_app_sim_nearby_placement_input input;
    memset(&input, 0, sizeof input);
    input.body.bounds = bounds;
    input.locomotion = "fly";
    input.authored.bounds = bounds;
    input.authored.locomotion = "fly";
    input.query = placement_query();
    input.ctx = &c;
    input.same_medium = placement_medium;
    input.trace = placement_trace;
    qa_app_sim_nearby_placement placed = qa_app_sim_nearby_monster_placement(&input);
    CHECK(placed.found == !blocked);
  }
  static const char *const locomotions[3] = {"walk", "fly", "swim"};
  for (int a = 0; a < 3; a++) for (int s = 0; s < 3; s++) for (int overlap = 0; overlap < 2; overlap++) {
    placement_case c = {PLACEMENT_MAGIC, false, true, 0, true};
    qa_app_sim_nearby_placement_input input;
    memset(&input, 0, sizeof input);
    qa_vec3 origin = {overlap ? 0 : 4, 0, 0};
    input.body.origin = origin;
    input.body.bounds = bounds;
    input.locomotion = locomotions[s];
    input.authored.origin = origin;
    input.authored.bounds = bounds;
    input.authored.locomotion = locomotions[a];
    input.query = placement_query();
    input.ctx = &c;
    input.same_medium = placement_medium;
    input.trace = placement_trace;
    qa_app_sim_nearby_placement placed = qa_app_sim_nearby_monster_placement(&input);
    CHECK(placed.found);
    if (placed.found) {
      qa_trace_query fit = input.query;
      fit.start = placed.origin; fit.end = placed.origin; fit.shape.kind = "box"; fit.shape.bounds = bounds;
      qa_trace_result result = placement_trace(&c, fit);
      CHECK(!result.start_solid); CHECK(!result.all_solid);
      if (s == 0) CHECK(placed.origin.z == -7);
      CHECK(!placed.has_ground); /* worldActor null */
    }
    c.medium = false;
    CHECK(!qa_app_sim_nearby_monster_placement(&input).found);
    c.medium = true;
    if (overlap) { c.mode = 1; CHECK(!qa_app_sim_nearby_monster_placement(&input).found); }
    if (s == 0) { c.mode = 2; CHECK(!qa_app_sim_nearby_monster_placement(&input).found); }
  }
  size_t n = 0;
  qa_app_sim_placement_offset *offsets = qa_app_sim_placement_offsets_within(8, &n);
  /* radius 8: x,y in -2..2 with (x^2+y^2)*16 <= 64 -> 13 offsets, nearest first. */
  CHECK(n == 13 && offsets[0].x == 0 && offsets[0].y == 0 && offsets[1].x == 0 && offsets[1].y == -4);
  CHECK(offsets[2].x == -4 && offsets[2].y == 0 && offsets[12].x == 0 && offsets[12].y == 8);
  free(offsets);
  offsets = qa_app_sim_placement_offsets_within(NAN, &n);
  CHECK(n == 0);
  free(offsets);
}

/* ---------------- SelectedMonsters over a Q1 map ---------------- */
#define BEHAVIOR_MAGIC 0x42454856
typedef struct behavior_log {
  int magic;
  char lines[32][160];
  size_t count;
  const qa_monster_mission *mission;
} behavior_log;
static behavior_log *behavior_of(void *ctx) {
  behavior_log *log = ctx;
  if (log == NULL || log->magic != BEHAVIOR_MAGIC) { ctx_errors++; abort(); }
  return log;
}
static void log_line(behavior_log *log, const char *text) {
  if (log->count < 32) snprintf(log->lines[log->count++], sizeof log->lines[0], "%s", text);
}
static const char *last_line(behavior_log *log) { return log->count == 0 ? "" : log->lines[log->count - 1]; }
static void b_attach(void *ctx, qa_owned_actor actor, qa_monster_definition_reference definition, const qa_monster_mission *mission) {
  behavior_log *log = behavior_of(ctx);
  char text[160];
  snprintf(text, sizeof text, "attach %lld %s ambush=%s", (long long)actor.id.slot, definition.classname, mission->ambush ? "true" : "false");
  log_line(log, text);
  log->mission = mission;
}
static bool b_validate(void *ctx, qa_authored_monster *entry, qa_monster_definition_reference definition, const qa_vec3 *relocated) {
  behavior_log *log = behavior_of(ctx);
  (void)entry;
  char text[160];
  if (relocated == NULL) snprintf(text, sizeof text, "validate %s -", definition.classname);
  else snprintf(text, sizeof text, "validate %s {\"x\":%g,\"y\":%g,\"z\":%g}", definition.classname, relocated->x, relocated->y, relocated->z);
  log_line(log, text);
  return true;
}
static bool b_ready(void *ctx, qa_authored_monster *entry) { (void)entry; (void)behavior_of(ctx); return true; }
static bool b_enemy(void *ctx, qa_actor_id actor, qa_actor_id *out) { (void)actor; (void)out; (void)behavior_of(ctx); return false; }
static void b_set_route(void *ctx, qa_actor_id actor, const qa_actor_id *goal, double pause) {
  behavior_log *log = behavior_of(ctx);
  char text[160];
  if (goal == NULL) snprintf(text, sizeof text, "setRoute %lld null %g", (long long)actor.slot, pause);
  else snprintf(text, sizeof text, "setRoute %lld %lld %g", (long long)actor.slot, (long long)goal->slot, pause);
  log_line(log, text);
}
static void b_resume(void *ctx, qa_actor_id actor, const qa_actor_id *activator) {
  behavior_log *log = behavior_of(ctx);
  char text[160];
  snprintf(text, sizeof text, "resume %lld %lld", (long long)actor.slot, activator == NULL ? -1LL : (long long)activator->slot);
  log_line(log, text);
}
static qa_app_sim_selected_monster_behavior stub_behavior(behavior_log *log) {
  qa_app_sim_selected_monster_behavior behavior = {log, b_attach, b_validate, b_ready, b_enemy, b_enemy, b_set_route, b_resume};
  return behavior;
}
static void set_text(char **slot, const char *text) {
  free(*slot);
  size_t n = strlen(text) + 1;
  *slot = malloc(n);
  memcpy(*slot, text, n);
}
static qa_monster_definition_reference soldier_reference(void) {
  qa_monster_definition_reference reference = {{"q2:monsters/classic/baseq2", "q2:classic:baseq2:test"}, "monster_soldier"};
  return reference;
}
static qa_enemy_selection selection_for(qa_monster_by_classname *dog) {
  qa_enemy_selection selection;
  memset(&selection, 0, sizeof selection);
  selection.kind = QA_ENEMY_SELECTION_REPLACE;
  selection.default_target.reference = soldier_reference();
  dog->classname = "monster_dog";
  dog->target.is_map_defined = true;
  selection.by_classname = dog;
  selection.by_classname_count = 1;
  return selection;
}
static qa_q1_entity_property prop(const char *key, const char *value) {
  qa_q1_entity_property p = {(char *)key, (char *)value};
  return p;
}
typedef struct resolve_args { qa_app_sim_selected_monsters *selected; const char *classname; const char *flags; } resolve_args;
static void resolve_with(resolve_args args) {
  qa_q1_entity_property props[1] = {prop("spawnflags", args.flags)};
  qa_q1_entity entity = {props, 1};
  qa_app_sim_monster_fields fields = {&entity, NULL};
  qa_monster_definition_reference out;
  (void)qa_app_sim_selected_monsters_resolve(args.selected, args.classname, fields, &out);
}

static void test_selected_q1(void) {
  q1_world_fixture fx;
  CHECK(q1_world_fixture_create(&fx, q1_world_fixture_options()));
  qa_q1_services *game = fx.game;
  qa_monster_by_classname dog;
  qa_enemy_selection selection = selection_for(&dog);
  behavior_log log;
  memset(&log, 0, sizeof log);
  log.magic = BEHAVIOR_MAGIC;
  qa_app_sim_monster_map map = {"q1", game, NULL, NULL};
  qa_app_sim_selected_monsters *selected = qa_app_sim_selected_monsters_create(&selection, map, stub_behavior(&log));
  qa_monster_definition_reference resolved;
  qa_q1_entity_property flag_props[1] = {prop("spawnflags", "1")};
  qa_q1_entity flag_entity = {flag_props, 1};
  qa_app_sim_monster_fields empty = {NULL, NULL}, flagged = {&flag_entity, NULL};
  CHECK(!qa_app_sim_selected_monsters_resolve(selected, "info_player_start", empty, &resolved));
  CHECK(!qa_app_sim_selected_monsters_resolve(selected, "monster_dog", empty, &resolved));
  CHECK(qa_app_sim_selected_monsters_resolve(selected, "monster_army", flagged, &resolved));
  CHECK(same(resolved.classname, "monster_soldier") && same(resolved.source.provider, "q2:monsters/classic/baseq2"));
  flag_props[0].value = "256";
  CHECK(qa_app_sim_selected_monsters_resolve(selected, "monster_army", flagged, &resolved));
  CHECK_ABORTS(resolve_with((resolve_args){selected, "monster_army", "2"}));
  CHECK_ABORTS(resolve_with((resolve_args){selected, "monster_zombie", "1"}));
  CHECK_ABORTS(resolve_with((resolve_args){selected, "monster_boss", "0"}));

  qa_q1_actor *corner = qa_q1_create(game, "path_corner", NULL, NULL);
  set_text(&corner->targetname, "p1");
  qa_q1_actor *victim = qa_q1_create(game, "info_null", NULL, NULL);
  set_text(&victim->targetname, "victim");
  qa_owned_actor actor = qa_session_actor_registry_allocate(fx.actors, qa_q1_services_provider(game), "q2:monsters/classic/baseq2/monster_soldier");
  qa_body_state body;
  memset(&body, 0, sizeof body);
  body.origin = (qa_vec3){1, 2, 3};
  body.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  qa_shared_body_table_create_body(fx.bodies, actor, body);
  CHECK(corner->actor.id.slot == 0 && victim->actor.id.slot == 1 && actor.id.slot == 2);
  qa_q1_entity_property props[9] = {prop("classname", "monster_army"), prop("target", "p1"), prop("killtarget", "victim"),
    prop("message", ""), prop("spawnflags", "1"), prop("deathtarget", "dt"), prop("item", "it"), prop("delay", "0"), prop("target", "p1")};
  qa_q1_entity source = {props, 9};
  qa_app_sim_selected_monsters_admit_q1(selected, actor, &source, 7, soldier_reference());
  CHECK(log.count == 1 && same(log.lines[0], "attach 2 monster_soldier ambush=true"));
  const qa_monster_mission *mission = log.mission;
  CHECK(mission != NULL && mission == qa_app_sim_selected_monsters_mission(selected, actor.id));
  qa_authored_monster *entry = qa_app_sim_selected_monsters_authored(selected, actor.id);
  CHECK(entry != NULL && qa_q1_authored_target(game, actor.id) == &entry->base);
  mission->spawned(mission->ctx);
  CHECK(game->total_monsters == 1);
  const qa_actor_id *goal = mission->route(mission->ctx);
  CHECK(goal != NULL && goal->slot == 0 && same(entry->route, "p1") && entry->route_resolved);
  qa_app_sim_q1_path_follower follower;
  CHECK(qa_app_sim_selected_monsters_q1_path_follower(selected, actor.id, &follower));
  CHECK(same(follower.targetname, "p1") && !follower.has_enemy);
  follower.advance(follower.ctx, "p2", NULL, 5);
  CHECK(same(entry->route, "") && entry->route_goal == NULL && same(last_line(&log), "setRoute 2 null 5"));
  follower.advance(follower.ctx, "p3", &corner->actor.id, 6);
  CHECK(same(entry->route, "p3") && entry->route_goal != NULL && entry->route_goal->slot == 0 && same(last_line(&log), "setRoute 2 0 6"));
  CHECK(same(follower.targetname, "p1")); /* the follower keeps its creation-time value */
  CHECK(!mission->use(mission->ctx, NULL));
  mission->killed(mission->ctx, NULL);
  size_t killed_events = 0;
  for (size_t i = 0; i < fx.event_count; i++)
    if (same(fx.events[i].kind, "monster-killed")) {
      killed_events++;
      CHECK(fx.events[i].actor.slot == 2 && fx.events[i].total == 1 && fx.events[i].found == 1);
    }
  CHECK(killed_events == 1 && game->killed_monsters == 1 && !qa_session_actor_registry_is_live(fx.actors, victim->actor.id));
  mission->killed(mission->ctx, NULL);
  CHECK(game->killed_monsters == 1);
  qa_monster_combat_route combat = mission->combat_route(mission->ctx);
  CHECK(combat.goal == NULL && !combat.stand_ground);
  mission->found_target(mission->ctx);
  CHECK(qa_app_sim_selected_monsters_active(selected, actor.id));
  CHECK(qa_app_sim_selected_monsters_before_turn(selected, actor.id));
  qa_app_sim_saved_authored_monsters saved = qa_app_sim_selected_monsters_capture(selected);
  CHECK(saved.count == 1 && saved.entries[0].has_route_goal && saved.entries[0].route_goal.slot == 0);
  qa_app_sim_saved_authored_monsters_free(&saved);
  follower.advance(follower.ctx, "p4", NULL, 7);
  saved = qa_app_sim_selected_monsters_capture(selected);
  CHECK(saved.count == 1);
  const qa_app_sim_saved_authored_monster *s = &saved.entries[0];
  CHECK(s->actor.slot == 2 && s->actor.generation == 0 && same(s->classname, "monster_army") && s->source_ordinal == 7 && s->spawnflags == 1);
  CHECK(same(s->death_target, "dt") && same(s->drop_item, "it") && same(s->targetname, "") && same(s->target, "p1"));
  CHECK(same(s->killtarget, "victim") && same(s->message, "") && s->delay == 0 && same(s->route, "") && !s->has_route_goal);
  CHECK(s->route_resolved && s->counted_death && same(s->combat_target, "") && !s->has_combat_goal && !s->stand_ground);
  CHECK(same(s->placement.kind, "ready") && same(s->activation.kind, "active") && same(s->definition.classname, "monster_soldier"));
  /* teleport placement */
  entry->placement.kind = "teleport";
  entry->placement.origin = (qa_vec3){1, 2, 3};
  CHECK(qa_app_sim_selected_monsters_before_turn(selected, actor.id));
  body.origin = (qa_vec3){9, 2, 3};
  qa_shared_body_table_write(fx.bodies, actor, body);
  CHECK(qa_app_sim_selected_monsters_before_turn(selected, actor.id));
  CHECK(same(entry->placement.kind, "ready") && same(last_line(&log), "validate monster_soldier {\"x\":9,\"y\":2,\"z\":3}"));
  qa_app_sim_selected_monsters_restore(selected, saved.entries, saved.count);
  qa_app_sim_saved_authored_monsters_free(&saved);
  saved = qa_app_sim_selected_monsters_capture(selected);
  CHECK(saved.count == 1 && same(saved.entries[0].route, "") && saved.entries[0].counted_death && same(saved.entries[0].killtarget, "victim"));
  qa_app_sim_saved_authored_monsters_free(&saved);
  CHECK(qa_app_sim_selected_monsters_count(selected) == 1);
  mission->started(mission->ctx);
  CHECK(same(last_line(&log), "validate monster_soldier -"));
  qa_session_actor_registry_release(fx.actors, actor);
  CHECK(qa_app_sim_selected_monsters_count(selected) == 0 && qa_app_sim_selected_monsters_active(selected, actor.id));
  CHECK_ABORTS(mission->started(mission->ctx));
  CHECK_ABORTS(entry->activation.kind = "dormant"; (void)mission->use(mission->ctx, NULL));

  /* actor-execution projections (TS actorCollision / actorFlags / writeActorFlags / actorMotion) */
  qa_q1_actor *wall = qa_q1_create(game, "func_wall", NULL, NULL);
  set_text(&wall->model, "*12");
  wall->solid = "bsp";
  wall->has_owner = true;
  wall->owner = corner->actor.id;
  qa_app_sim_actor_execution exec;
  memset(&exec, 0, sizeof exec);
  exec.kind = QA_APP_SIM_ACTOR_EXECUTION_Q1;
  exec.q1_services = game;
  exec.content = "c";
  exec.q1_entity = wall;
  qa_app_sim_shared_solid solid = qa_app_sim_actor_collision(&exec);
  CHECK(same(solid.family, "q1") && same(solid.solid, "brush") && solid.has_model && solid.model == 12 && solid.owner != NULL
        && solid.owner->slot == 0 && solid.has_monster && !solid.monster && solid.has_item && !solid.item);
  qa_q1_actor *corpse = qa_q1_create(game, "corpse", NULL, NULL);
  corpse->solid = "corpse";
  exec.q1_entity = corpse;
  solid = qa_app_sim_actor_collision(&exec);
  CHECK(same(solid.solid, "box") && solid.q1_corpse && !solid.has_model && solid.owner == NULL && !solid.has_monster && !solid.has_item);
  game->options.physics_edition = "classic";
  solid = qa_app_sim_actor_collision(&exec);
  CHECK(same(solid.solid, "none") && !solid.q1_corpse);
  game->options.physics_edition = NULL;
  qa_q1_actor *item = qa_q1_create(game, "item_x", NULL, NULL);
  item->solid = "trigger";
  item->movement_flags = 256 | 1 | 1024;
  item->water_level = 2;
  item->water_type = -3;
  set_text(&item->original_model, "*3");
  set_text(&item->model, "");
  exec.q1_entity = item;
  solid = qa_app_sim_actor_collision(&exec);
  CHECK(same(solid.solid, "trigger") && solid.has_model && solid.model == 3 && solid.item && !solid.monster);
  qa_app_sim_shared_physics_flags flags = qa_app_sim_actor_flags(&exec);
  CHECK(flags.has_fly && flags.fly && flags.has_swim && !flags.swim && flags.partial_ground && flags.water_level == 2
        && flags.water_type == -3 && flags.has_enemy && flags.enemy == NULL);
  qa_app_sim_shared_physics_flags changes;
  memset(&changes, 0, sizeof changes);
  changes.has_water_level = true; changes.water_level = 1;
  changes.has_water_type = true; changes.water_type = -7;
  qa_app_sim_write_actor_flags(&exec, changes);
  CHECK(item->water_level == 1 && item->water_type == -3);
  memset(&changes, 0, sizeof changes);
  changes.has_water_type = true; changes.water_type = -5;
  qa_app_sim_write_actor_flags(&exec, changes);
  CHECK(item->water_level == 1 && item->water_type == -5);
  qa_q1_actor *missile = qa_q1_create(game, "missile", NULL, NULL);
  missile->movement = "flymissile";
  CHECK(qa_q1_actor_field_set(missile, "gravity", "0.25"));
  missile->angular_velocity = (qa_vec3){1, 2, 3};
  exec.q1_entity = missile;
  qa_body_state moving;
  memset(&moving, 0, sizeof moving);
  moving.velocity = (qa_vec3){5, 6, 7};
  qa_q2_motion motion = qa_app_sim_actor_motion(&exec, moving);
  CHECK(qa_same_owned_actor(motion.actor, missile->actor));
  CHECK(same(motion.kind, "fly-missile") && motion.gravity == 0.25 && motion.velocity.z == 7 && motion.angular_velocity.y == 2
        && motion.gravity_vector.z == -1 && motion.clip_mask == 100663299 && !motion.has_owner);
  missile->movement = "noclip";
  CHECK(qa_q1_actor_field_set(missile, "gravity", "0"));
  motion = qa_app_sim_actor_motion(&exec, moving);
  CHECK(same(motion.kind, "stationary") && motion.gravity == 1);
  missile->movement = "gib";
  CHECK(same(qa_app_sim_actor_motion(&exec, moving).kind, "bounce"));
  game->options.physics_edition = "classic";
  CHECK_ABORTS((void)qa_app_sim_actor_motion(&exec, moving));
  game->options.physics_edition = NULL;

  /* preservesAuthoredQ1Placement */
  qa_q1_actor *wizard = qa_q1_create(game, "monster_wizard", NULL, NULL);
  wizard->movement = "step";
  set_text(&wizard->model, "progs/wizard.mdl");
  wizard->movement_flags = 1 | 4;
  qa_body_state wizard_body;
  memset(&wizard_body, 0, sizeof wizard_body);
  wizard_body.origin = (qa_vec3){10, 20, 30};
  wizard_body.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 40}};
  qa_resolved_map rmap;
  memset(&rmap, 0, sizeof rmap);
  rmap.geometry_content = "q1:rerelease:id1:test";
  rmap.entities.provider = "q1:official";
  rmap.entities.content = "q1:rerelease:id1:test";
  qa_monster_definition_reference wizard_def = {{"q1:monsters/rerelease/id1", "q1:rerelease:id1:test"}, "monster_wizard"};
  qa_q1_entity_property wizard_props[2] = {prop("classname", "monster_wizard"), prop("origin", "10 20 30")};
  qa_q1_entity wizard_source = {wizard_props, 2};
  qa_app_sim_q1_placement_input input = {rmap, &wizard_source, wizard_def, game, wizard, wizard_body};
  CHECK(qa_app_sim_preserves_authored_q1_placement(input));
  qa_app_sim_q1_placement_input variant = input;
  variant.body.bounds.max.x = 32;
  CHECK(!qa_app_sim_preserves_authored_q1_placement(variant));
  variant = input; variant.body.origin.x = 11;
  CHECK(!qa_app_sim_preserves_authored_q1_placement(variant));
  variant = input; variant.definition.classname = "monster_ogre";
  CHECK(!qa_app_sim_preserves_authored_q1_placement(variant));
  variant = input; variant.definition.source.provider = "q1:monsters/classic/id1"; variant.definition.source.content = "q1:classic:id1:test";
  CHECK(!qa_app_sim_preserves_authored_q1_placement(variant));
  variant = input; variant.authored = NULL;
  CHECK(!qa_app_sim_preserves_authored_q1_placement(variant));
  qa_q1_actor *demon = qa_q1_create(game, "monster_demon1", NULL, NULL);
  demon->movement = "step";
  set_text(&demon->model, "progs/demon.mdl");
  demon->movement_flags = 32;
  demon->solid = "slidebox";
  qa_body_state demon_body;
  memset(&demon_body, 0, sizeof demon_body);
  demon_body.origin = (qa_vec3){0, 0, 25};
  demon_body.bounds = (qa_bounds){{-32, -32, -24}, {32, 32, 64}};
  qa_q1_entity_property demon_props[2] = {prop("classname", "monster_demon1"), prop("origin", "0 0 24")};
  qa_q1_entity demon_source = {demon_props, 2};
  qa_monster_definition_reference demon_def = wizard_def;
  demon_def.classname = "monster_demon1";
  input = (qa_app_sim_q1_placement_input){rmap, &demon_source, demon_def, game, demon, demon_body};
  CHECK(qa_app_sim_preserves_authored_q1_placement(input));
  variant = input; variant.body.bounds.max.x = 48;
  CHECK(!qa_app_sim_preserves_authored_q1_placement(variant));
  variant = input; variant.body.origin.x = 1;
  CHECK(!qa_app_sim_preserves_authored_q1_placement(variant));
  variant = input; variant.body.ground = &corner->actor.id;
  CHECK(!qa_app_sim_preserves_authored_q1_placement(variant));
  CHECK(!qa_app_sim_is_q1_teleport_staging(game, demon_body));

  qa_app_sim_selected_monsters_destroy(selected);
  CHECK(fx.ctx_errors == 0);
  q1_world_fixture_destroy(&fx);
}

/* ---------------- monster-checkpoint readers ---------------- */
static qa_save_value *parse(const char *text) {
  return qa_parse_json_value((const uint8_t *)text, strlen(text), false);
}
static void read_selected_text(const char *text) {
  qa_save_value *value = parse(text);
  qa_save_reader reader;
  qa_save_reader_init(&reader, value, NULL);
  qa_app_sim_selected_monsters_checkpoint checkpoint = qa_app_sim_read_selected_monsters_checkpoint(&reader);
  qa_app_sim_selected_monsters_checkpoint_free(&checkpoint);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(value);
}
static char *replace_once(const char *text, const char *from, const char *to) {
  const char *at = strstr(text, from);
  if (at == NULL) return NULL;
  size_t head = (size_t)(at - text), n = strlen(text) - strlen(from) + strlen(to) + 1;
  char *out = malloc(n);
  memcpy(out, text, head);
  strcpy(out + head, to);
  strcat(out, at + strlen(from));
  return out;
}
static void test_checkpoints(void) {
  qa_save_value *value = parse(SELECTED_MONSTERS_JSON);
  CHECK(value != NULL);
  qa_save_reader reader;
  qa_save_reader_init(&reader, value, NULL);
  qa_app_sim_selected_monsters_checkpoint checkpoint = qa_app_sim_read_selected_monsters_checkpoint(&reader);
  CHECK(checkpoint.version == 2 && checkpoint.authored_count == 2 && checkpoint.source_count == 1);
  const qa_app_sim_saved_authored_monster *a = &checkpoint.authored[0];
  CHECK(same(a->placement.kind, "waiting") && a->placement.barrier_count == 1 && a->placement.barriers[0].actor.slot == 1
        && a->placement.barriers[0].origin.y == 2 && !a->placement.has_activator);
  CHECK(same(a->activation.kind, "scheduled") && a->activation.at == 2.5 && a->activation.has_activator
        && a->activation.activator.slot == 3 && a->activation.activator.generation == 1);
  CHECK(same(a->route, "") && a->source_ordinal == 7 && same(a->definition.source.content, "q2:classic:baseq2:test"));
  const qa_app_sim_saved_authored_monster *b = &checkpoint.authored[1];
  CHECK(same(b->placement.kind, "teleport") && b->placement.origin.z == 6 && same(b->activation.kind, "dormant"));
  const qa_app_sim_monster_source_checkpoint *source = &checkpoint.sources[0];
  CHECK(same(source->kind, "q1") && source->frame.frame == 3 && source->frame.time.value == 0.3 && source->frame.elapsed.value == 0.1);
  CHECK(source->random.kind == QA_RANDOM_STATE_GLIBC_RANDOM && source->q1_entities.entity_count == 7 && source->q1_entities.total_monsters == 1);
  qa_app_sim_selected_monsters_checkpoint_free(&checkpoint);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(value);
  char *bad = replace_once(SELECTED_MONSTERS_JSON, "\"kind\":\"glibc-random\"", "\"kind\":\"q3-lcg\",\"seed\":1");
  CHECK(bad != NULL);
  CHECK_ABORTS(read_selected_text(bad));
  free(bad);

  value = parse(FOREIGN_Q1_JSON);
  qa_save_reader_init(&reader, value, NULL);
  qa_app_sim_foreign_q1_world_checkpoint foreign = qa_app_sim_read_foreign_q1_world_checkpoint(&reader);
  CHECK(foreign.version == 1 && same(foreign.program, "mg3") && same(foreign.edition, "rerelease") && foreign.has_adjunct
        && foreign.adjunct.flags == 5 && foreign.adjunct.skill == 2 && foreign.adjunct.cvar_count == 1
        && same(foreign.adjunct.cvars[0].name, "a") && same(foreign.adjunct.cvars[0].value, "1") && foreign.entities.entity_count == 7);
  qa_app_sim_foreign_q1_world_checkpoint_free(&foreign);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(value);
  char *older = replace_once(FOREIGN_Q1_JSON, ",\"adjunct\":{\"flags\":5,\"skill\":2,\"cvars\":[{\"name\":\"a\",\"value\":\"1\"}]}", "");
  CHECK(older != NULL);
  value = parse(older);
  qa_save_reader_init(&reader, value, NULL);
  foreign = qa_app_sim_read_foreign_q1_world_checkpoint(&reader);
  CHECK(!foreign.has_adjunct);
  qa_app_sim_foreign_q1_world_checkpoint_free(&foreign);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(value);
  free(older);
}


/* ---------------- Q2 map: SelectedMonsters and the foreign Q2 world ---------------- */
static int HOST_SENTINEL, LINK_SENTINEL, COMBAT_SENTINEL, POLICY_SENTINEL, ARMOR_SENTINEL, WEAPON_SENTINEL, OPTIONS_SENTINEL, ITEM_SENTINEL;
#define EXPECT_CTX(ctx, sentinel) do { if ((ctx) != (void *)&(sentinel)) { ctx_errors++; fprintf(stderr, "FAIL ctx %s:%d\n", __FILE__, __LINE__); } } while (0)
typedef struct q2_world {
  qa_identity_owner identities;
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_actor_callback_table *callbacks;
  qa_gameplay_authority *combat;
  qa_combat_policy policy;
  qa_victim_armor_policy armor;
  qa_shared_inventory_table *inventory;
  qa_q2_foundation_host host;
  qa_q2_game_options options;
  double now;
} q2_world;
static q2_world *QW;
static double h_now(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return QW->now; }
static double h_frame(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return 0.1; }
static double h_gravity(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return 800; }
static double h_random(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return 0.5; }
static void h_schedule(void *c, qa_owned_actor actor, const double *due) { EXPECT_CTX(c, HOST_SENTINEL); (void)actor; (void)due; }
static void h_touch_triggers(void *c, qa_owned_actor a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; }
static qa_trace_result h_trace(void *c, qa_q2_trace_request r) {
  EXPECT_CTX(c, HOST_SENTINEL);
  qa_trace_result out;
  memset(&out, 0, sizeof out);
  out.kind = "q2"; out.fraction = 1; out.end = r.end; out.hit.kind = QA_TRACE_HIT_NONE;
  return out;
}
static double h_contents(void *c, qa_vec3 p) { EXPECT_CTX(c, HOST_SENTINEL); (void)p; return 0; }
static bool h_true2(void *c, qa_vec3 a, qa_vec3 b) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; (void)b; return true; }
static const qa_actor_id *h_nearby(void *c, qa_vec3 o, double r, size_t *n) { EXPECT_CTX(c, HOST_SENTINEL); (void)o; (void)r; *n = 0; return NULL; }
static const qa_actor_id *h_players(void *c, size_t *n) { EXPECT_CTX(c, HOST_SENTINEL); *n = 0; return NULL; }
static bool h_is_player(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; return false; }
static bool h_is_monster(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; return true; }
static qa_bounds h_inline_bounds(void *c, double m) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; qa_bounds b; memset(&b, 0, sizeof b); return b; }
static void h_set_solid(void *c, qa_owned_actor a, const char *s, const double *m) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; (void)s; (void)m; }
static void h_set_motion(void *c, qa_q2_motion m) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; }
static void h_portal(void *c, double p, bool o) { EXPECT_CTX(c, HOST_SENTINEL); (void)p; (void)o; }
static void h_emit(void *c, qa_q2_presentation_event e) { EXPECT_CTX(c, HOST_SENTINEL); (void)e; }
static qa_q2_player_view_result h_view(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; qa_q2_player_view_result r; memset(&r, 0, sizeof r); return r; }
static void h_key(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; }
static void h_prepare(void *c, const char *m, const qa_q2_landmark_carry *l, double f) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; (void)l; (void)f; }
static void h_transition(void *c, qa_transition_intent t) { EXPECT_CTX(c, HOST_SENTINEL); (void)t; }
static void h_diagnostic(void *c, const char *m) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; }
static qa_bounds link_bounds(void *c, qa_owned_actor a, qa_body_state b) { EXPECT_CTX(c, LINK_SENTINEL); return qa_translated_body_bounds(a, b); }
static void link_on(void *c, qa_linked_body b) { EXPECT_CTX(c, LINK_SENTINEL); (void)b; }
static void link_off(void *c, qa_actor_id a) { EXPECT_CTX(c, LINK_SENTINEL); (void)a; }
static void combat_impulse(void *c, qa_owned_actor a, qa_vec3 i, qa_provider_id m) { EXPECT_CTX(c, COMBAT_SENTINEL); (void)a; (void)i; (void)m; }
static void combat_before(void *c, qa_owned_actor a, qa_damage_decision d) { EXPECT_CTX(c, COMBAT_SENTINEL); (void)a; (void)d; }
static void combat_confirmed(void *c, qa_damage_outcome o) { EXPECT_CTX(c, COMBAT_SENTINEL); (void)o; }
static qa_q2_combat_context policy_context(void *c, qa_damage_request r, qa_combat_state t, const qa_combat_state *a) {
  EXPECT_CTX(c, POLICY_SENTINEL); (void)t; (void)a; (void)r;
  qa_q2_combat_context out;
  memset(&out, 0, sizeof out);
  out.arithmetic = "binary64"; out.monster = true;
  return out;
}
static qa_victim_armor_context armor_context(void *c, qa_damage_request r) {
  EXPECT_CTX(c, ARMOR_SENTINEL); (void)r;
  qa_victim_armor_context out;
  memset(&out, 0, sizeof out);
  out.screen_facing_dot = 1; out.arithmetic = "binary64";
  return out;
}
static void q2_world_create(qa_q2_edition edition) {
  QW = calloc(1, sizeof *QW);
  qa_identity_owner_create(&QW->identities, "q2-mw");
  QW->actors = qa_session_actor_registry_create(&QW->identities, 4096);
  qa_body_link_hooks link = {&LINK_SENTINEL, link_bounds, link_on, link_off};
  QW->bodies = qa_shared_body_table_create(QW->actors, link);
  QW->callbacks = qa_actor_callback_table_create(QW->actors);
  qa_gameplay_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.ctx = &COMBAT_SENTINEL; hooks.impulse = combat_impulse; hooks.before_reaction = combat_before; hooks.confirmed = combat_confirmed;
  QW->combat = qa_gameplay_authority_create(QW->actors, QW->callbacks, hooks);
  QW->inventory = qa_shared_inventory_table_create(QW->actors);
  qa_q2_combat_policy_options policy;
  memset(&policy, 0, sizeof policy);
  policy.id = "q2:combat"; policy.ctx = &POLICY_SENTINEL; policy.context = policy_context;
  QW->armor = policy.armor = qa_native_victim_armor(armor_context, &ARMOR_SENTINEL);
  QW->policy = qa_create_q2_combat_policy(policy);
  (void)qa_gameplay_authority_register(QW->combat, QW->policy);
  qa_q2_foundation_host *h = &QW->host;
  h->ctx = &HOST_SENTINEL;
  h->actors = QW->actors; h->bodies = QW->bodies; h->callbacks = QW->callbacks; h->combat = QW->combat; h->inventory = QW->inventory;
  h->now = h_now; h->frame_seconds = h_frame; h->gravity = h_gravity; h->random = h_random; h->schedule = h_schedule;
  h->touch_triggers = h_touch_triggers; h->trace = h_trace; h->point_contents = h_contents; h->in_pvs = h_true2;
  h->in_phs = h_true2; h->areas_connected = h_true2; h->nearby = h_nearby; h->players = h_players;
  h->is_player = h_is_player; h->is_monster = h_is_monster; h->inline_model_bounds = h_inline_bounds;
  h->set_solid = h_set_solid; h->set_motion = h_set_motion; h->set_area_portal = h_portal; h->emit = h_emit;
  h->player_view_state = h_view; h->key_consumed = h_key; h->prepare_level_change = h_prepare;
  h->transition = h_transition; h->diagnostic = h_diagnostic;
  qa_q2_game_options *options = &QW->options;
  options->edition = edition; options->map_name = "base1"; options->skill = 1;
  options->mode = QA_Q2_MODE_SINGLEPLAYER; options->max_clients = 4; options->provider = "q2:official";
  options->campaign = "q2:base"; options->combat_provider = "q2:combat"; options->inventory_provider = "q2:inventory";
  options->movement_provider = "q1:movement";
}
static void q2_world_destroy(void) {
  qa_shared_inventory_table_destroy(QW->inventory);
  qa_gameplay_authority_destroy(QW->combat);
  qa_combat_policy_free(QW->policy);
  qa_victim_armor_policy_free(QW->armor);
  qa_actor_callback_table_destroy(QW->callbacks);
  qa_shared_body_table_destroy(QW->bodies);
  qa_session_actor_registry_close(QW->actors);
  qa_session_actor_registry_destroy(QW->actors);
  qa_identity_owner_destroy(&QW->identities);
  free(QW);
  QW = NULL;
}
static qa_owned_actor q2_monster(void) {
  qa_owned_actor actor = qa_session_actor_registry_allocate(QW->actors, "q2:official", "q2:monsters/classic/baseq2/monster_soldier");
  qa_body_state body;
  memset(&body, 0, sizeof body);
  body.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  qa_shared_body_table_create_body(QW->bodies, actor, body);
  qa_combat_state state;
  memset(&state, 0, sizeof state);
  state.health = 20; state.mass = 200; state.can_take_damage = true;
  qa_gameplay_authority_create_state(QW->combat, actor, state, NULL, NULL);
  return actor;
}
static bool can_take_damage(qa_owned_actor actor) {
  qa_combat_state_result state = qa_gameplay_authority_read(QW->combat, actor.id);
  return state.has_value && state.value.can_take_damage;
}
typedef struct q2_resolve_args { qa_app_sim_selected_monsters *selected; const char *flags; } q2_resolve_args;
static void q2_resolve_with(q2_resolve_args args) {
  qa_q2_spawn_fields fields;
  memset(&fields, 0, sizeof fields);
  (void)qa_q2_spawn_set(&fields, "spawnflags", args.flags);
  qa_app_sim_monster_fields lookup = {NULL, &fields};
  qa_monster_definition_reference out;
  (void)qa_app_sim_selected_monsters_resolve(args.selected, "monster_soldier", lookup, &out);
}
static bool q2_resolves(qa_app_sim_selected_monsters *selected, const char *flags) {
  qa_q2_spawn_fields fields;
  memset(&fields, 0, sizeof fields);
  (void)qa_q2_spawn_set(&fields, "spawnflags", flags);
  qa_app_sim_monster_fields lookup = {NULL, &fields};
  qa_monster_definition_reference out;
  bool resolved = qa_app_sim_selected_monsters_resolve(selected, "monster_soldier", lookup, &out);
  qa_q2_spawn_fields_free(&fields);
  return resolved;
}
static bool q2_enemy_on;
static qa_actor_id q2_enemy;
static bool b_q2_enemy(void *ctx, qa_actor_id actor, qa_actor_id *out) {
  (void)actor; (void)behavior_of(ctx);
  if (q2_enemy_on) *out = q2_enemy;
  return q2_enemy_on;
}

static void test_selected_q2(void) {
  q2_world_create(QA_Q2_EDITION_CLASSIC);
  qa_q2_foundation *foundation = qa_q2_entity_services_create(&QW->host, QW->options, NULL, 0);
  qa_q2_game_services *game = qa_q2_entity_services_game(foundation);
  qa_q2_item_hooks item_hooks;
  memset(&item_hooks, 0, sizeof item_hooks);
  item_hooks.ctx = &ITEM_SENTINEL;
  item_hooks.edition = QA_Q2_EDITION_CLASSIC;
  qa_q2_item_module *items = qa_q2_create_item_module(item_hooks);
  qa_enemy_selection selection;
  memset(&selection, 0, sizeof selection);
  selection.kind = QA_ENEMY_SELECTION_REPLACE;
  selection.default_target.reference = soldier_reference();
  behavior_log log;
  memset(&log, 0, sizeof log);
  log.magic = BEHAVIOR_MAGIC;
  qa_app_sim_selected_monster_behavior behavior = stub_behavior(&log);
  behavior.enemy = b_q2_enemy;
  qa_app_sim_monster_map map = {"q2", NULL, foundation, items};
  qa_app_sim_selected_monsters *selected = qa_app_sim_selected_monsters_create(&selection, map, behavior);
  CHECK(q2_resolves(selected, "3") && q2_resolves(selected, "256"));
  CHECK_ABORTS(q2_resolve_with((q2_resolve_args){selected, "4"}));
  CHECK_ABORTS(q2_resolve_with((q2_resolve_args){selected, "8192"}));
  qa_q2_entity *corner = game->create(game->ctx, "path_corner", NULL, 0);
  corner->targetname = "t1";
  qa_q2_entity *pc = game->create(game->ctx, "point_combat", NULL, 0);
  pc->targetname = "pc";
  qa_owned_actor other = qa_session_actor_registry_allocate(QW->actors, "q2:test", "q2:test/other");
  qa_owned_actor actor = q2_monster();
  CHECK(corner->actor.id.slot == 0 && pc->actor.id.slot == 1 && other.id.slot == 2 && actor.id.slot == 3);
  qa_q2_spawn_fields fields;
  memset(&fields, 0, sizeof fields);
  fields.ordinal = 5;
  fields.classname = "monster_soldier";
  (void)qa_q2_spawn_set(&fields, "classname", "monster_soldier");
  (void)qa_q2_spawn_set(&fields, "spawnflags", "2");
  (void)qa_q2_spawn_set(&fields, "target", "t1");
  (void)qa_q2_spawn_set(&fields, "deathtarget", "dt");
  (void)qa_q2_spawn_set(&fields, "item", "unknown_item");
  (void)qa_q2_spawn_set(&fields, "targetname", "tn");
  qa_app_sim_selected_monsters_admit_q2(selected, actor, &fields, soldier_reference());
  qa_q2_spawn_fields_free(&fields);
  const qa_monster_mission *mission = log.mission;
  qa_authored_monster *entry = qa_app_sim_selected_monsters_authored(selected, actor.id);
  CHECK(log.count == 1 && same(log.lines[0], "attach 3 monster_soldier ambush=false"));
  CHECK(same(entry->activation.kind, "dormant") && !can_take_damage(actor) && !qa_app_sim_selected_monsters_active(selected, actor.id));
  size_t authored_count = 0;
  qa_authored_target *const *authored = qa_q2_entity_services_authored_targets(foundation, &authored_count);
  CHECK(authored_count == 1 && authored[0] == &entry->base);
  CHECK(mission->use(mission->ctx, &other.id));
  CHECK(same(entry->activation.kind, "scheduled") && entry->activation.at == 0.1 && entry->activation.activator != NULL
        && entry->activation.activator->slot == 2);
  CHECK(mission->use(mission->ctx, NULL) && entry->activation.activator->slot == 2);
  CHECK(!qa_app_sim_selected_monsters_before_turn(selected, actor.id));
  const qa_q2m_combat_follower *early = qa_app_sim_selected_monsters_q2_combat_follower(selected, actor.id);
  qa_actor_id seen;
  CHECK(early->activator(early->ctx, &seen) && seen.slot == 2);
  QW->now = 0.2;
  CHECK(qa_app_sim_selected_monsters_before_turn(selected, actor.id));
  qa_body_state_result moved = qa_shared_body_table_read(QW->bodies, actor.id);
  CHECK(same(entry->activation.kind, "active") && can_take_damage(actor) && moved.has_value && moved.value.origin.z == 1
        && same(last_line(&log), "resume 3 2"));
  const qa_actor_id *goal = mission->route(mission->ctx);
  CHECK(goal != NULL && goal->slot == 0 && same(entry->base.target, "") && same(entry->route, "t1"));
  const qa_q2m_path_follower *path = qa_app_sim_selected_monsters_q2_path_follower(selected, actor.id);
  CHECK(path->actor.id.slot == 3 && path->move_target(path->ctx, &seen) && seen.slot == 0 && !path->enemy(path->ctx, &seen));
  path->advance(path->ctx, "t2", NULL, 3);
  CHECK(same(entry->route, "t2") && entry->route_goal == NULL && same(last_line(&log), "setRoute 3 null 3"));
  CHECK(path->move_target(path->ctx, &seen) && seen.slot == 0); /* creation-time value */
  const qa_q2m_combat_follower *follower = qa_app_sim_selected_monsters_q2_combat_follower(selected, actor.id);
  follower->advance(follower->ctx, "ct", NULL, &corner->actor.id);
  CHECK(same(entry->base.target, "ct") && entry->combat_goal != NULL && entry->combat_goal->slot == 0);
  CHECK(follower->move_target(follower->ctx, &seen) && seen.slot == 0 && follower->walking && !follower->activator(follower->ctx, &seen));
  follower->hold(follower->ctx);
  CHECK(entry->stand_ground);
  follower->finish(follower->ctx);
  CHECK(same(entry->base.target, "") && entry->combat_goal == NULL && !follower->move_target(follower->ctx, &seen));
  mission->spawned(mission->ctx);
  mission->killed(mission->ctx, NULL);
  mission->killed(mission->ctx, NULL);
  qa_q2_counters *counters = qa_q2_entity_services_counters(foundation);
  CHECK(counters->total_monsters == 1 && counters->killed_monsters == 1 && same(entry->base.target, "dt") && entry->counted_death);
  qa_owned_actor second = q2_monster();
  memset(&fields, 0, sizeof fields);
  fields.ordinal = 6;
  fields.classname = "monster_soldier";
  (void)qa_q2_spawn_set(&fields, "classname", "monster_soldier");
  (void)qa_q2_spawn_set(&fields, "target", "pc");
  qa_app_sim_selected_monsters_admit_q2(selected, second, &fields, soldier_reference());
  qa_q2_spawn_fields_free(&fields);
  const qa_monster_mission *second_mission = log.mission;
  qa_authored_monster *second_entry = qa_app_sim_selected_monsters_authored(selected, second.id);
  CHECK(same(second_entry->activation.kind, "active") && !second_mission->ambush);
  CHECK(second_mission->route(second_mission->ctx) == NULL && same(second_entry->combat_target, "pc")
        && same(second_entry->base.target, "") && same(second_entry->route, ""));
  second_mission->found_target(second_mission->ctx);
  CHECK(same(second_entry->combat_target, "pc"));
  q2_enemy_on = true;
  q2_enemy = other.id;
  second_mission->found_target(second_mission->ctx);
  qa_monster_combat_route route = second_mission->combat_route(second_mission->ctx);
  CHECK(same(second_entry->combat_target, "") && second_entry->combat_goal != NULL && second_entry->combat_goal->slot == 1
        && same(pc->targetname, "") && route.goal != NULL && route.goal->slot == 1 && !route.stand_ground);
  q2_enemy_on = false;
  qa_app_sim_saved_authored_monsters saved = qa_app_sim_selected_monsters_capture(selected);
  CHECK(saved.count == 2 && saved.entries[0].actor.slot == 3 && same(saved.entries[0].activation.kind, "active")
        && !saved.entries[0].has_combat_goal && same(saved.entries[0].target, "dt") && same(saved.entries[0].route, "t2")
        && same(saved.entries[0].death_target, "dt") && same(saved.entries[0].drop_item, "unknown_item") && same(saved.entries[0].targetname, "tn"));
  CHECK(saved.entries[1].actor.slot == 4 && saved.entries[1].has_combat_goal && saved.entries[1].combat_goal.slot == 1
        && same(saved.entries[1].target, "") && same(saved.entries[1].route, "") && same(saved.entries[1].drop_item, ""));
  qa_app_sim_saved_authored_monsters_free(&saved);
  qa_app_sim_selected_monsters_destroy(selected);
  qa_q2_entity_services_destroy(foundation);
  qa_q2_item_module_destroy(items);
  q2_world_destroy();
}

typedef struct foreign_probe {
  int calls;
  size_t module_count;
  qa_q2_spawn_module modules[16];
  qa_q2_foundation *game;
} foreign_probe;
static foreign_probe *active_probe;
static qa_q2_foundation *f_create_game(void *ctx, qa_q2m_monsters *monsters, const qa_q2_spawn_module *modules, size_t count) {
  (void)monsters;
  EXPECT_CTX(ctx, OPTIONS_SENTINEL);
  foreign_probe *probe = active_probe;
  probe->calls++;
  probe->module_count = count;
  memcpy(probe->modules, modules, count * sizeof *modules);
  probe->game = qa_q2_entity_services_create(&QW->host, QW->options, modules, count);
  return probe->game;
}
static double f_gravity(void *ctx) { EXPECT_CTX(ctx, OPTIONS_SENTINEL); return 800; }
static qa_q2mpm_powerups f_powerups(void *ctx, qa_actor_id actor) { (void)actor; EXPECT_CTX(ctx, OPTIONS_SENTINEL); qa_q2mpm_powerups p = {0, 0, 0}; return p; }
static void f_effect(void *ctx, qa_q2mp_player_effect e) { (void)e; EXPECT_CTX(ctx, OPTIONS_SENTINEL); }
static void f_teleport(void *ctx, qa_actor_id a, qa_vec3 o, qa_vec3 g) { (void)a; (void)o; (void)g; EXPECT_CTX(ctx, OPTIONS_SENTINEL); }
static void f_push(void *ctx, qa_actor_id a, qa_vec3 v) { (void)a; (void)v; EXPECT_CTX(ctx, OPTIONS_SENTINEL); }
static void f_set_gravity(void *ctx, qa_actor_id a, double g) { (void)a; (void)g; EXPECT_CTX(ctx, OPTIONS_SENTINEL); }
static int weapon_emits, weapon_noises;
static void f_weapon_emit(void *ctx, qa_q2w_event e) { (void)e; EXPECT_CTX(ctx, WEAPON_SENTINEL); weapon_emits++; }
static void f_weapon_noise(void *ctx, qa_actor_id a, qa_vec3 o, bool s) { (void)a; (void)o; (void)s; EXPECT_CTX(ctx, WEAPON_SENTINEL); weapon_noises++; }
static const char *module_role(qa_app_sim_foreign_q2_world_modules *world, qa_q2_spawn_module module) {
  for (size_t i = 0; i < world->pack_count; i++) {
    if (module.ctx == (void *)&world->packs[i].entities) return "pack-entities";
    if (module.ctx == (void *)&world->packs[i].monsters) return "pack-monsters";
  }
  qa_q2_spawn_module base = qa_q2_base_entity_module_as_spawn(world->base_entities);
  if (module.ctx == base.ctx && module.spawn == base.spawn) return "base-entities";
  if (module.ctx == (void *)world->movers) return "movers";
  if (module.ctx == (void *)world->items) return "items";
  return "other";
}
static void test_foreign_q2_world(void) {
  static const struct { qa_q2_edition edition; const char *program; size_t packs; const char *order; } cases[] = {
    {QA_Q2_EDITION_CLASSIC, "baseq2", 0, "base-entities other movers other items other"},
    {QA_Q2_EDITION_CLASSIC, "xatrix", 1, "pack-entities base-entities other movers other items other pack-monsters"},
    {QA_Q2_EDITION_CLASSIC, "rogue", 1, "pack-entities base-entities other movers other items other pack-monsters"},
    {QA_Q2_EDITION_CLASSIC, "mg2", 0, "base-entities other movers other items other"},
    {QA_Q2_EDITION_RERELEASE, "baseq2", 2, "pack-entities pack-entities base-entities other movers other items other pack-monsters pack-monsters other"},
    {QA_Q2_EDITION_RERELEASE, "mg2", 2, "pack-entities pack-entities base-entities other movers other items other pack-monsters pack-monsters other"},
  };
  for (size_t c = 0; c < sizeof cases / sizeof *cases; c++) {
    q2_world_create(cases[c].edition);
    foreign_probe probe;
    memset(&probe, 0, sizeof probe);
    active_probe = &probe;
    qa_app_sim_foreign_q2_world_options options;
    memset(&options, 0, sizeof options);
    options.edition = cases[c].edition;
    options.program = cases[c].program;
    options.weapons.ctx = &WEAPON_SENTINEL;
    options.weapons.emit = f_weapon_emit;
    options.weapons.noise = f_weapon_noise;
    options.item_hooks.ctx = &ITEM_SENTINEL;
    options.ctx = &OPTIONS_SENTINEL;
    options.create_game = f_create_game;
    options.gravity = f_gravity;
    options.powerups = f_powerups;
    options.player_effect = f_effect;
    options.teleport_player = f_teleport;
    options.player_push = f_push;
    options.set_actor_gravity = f_set_gravity;
    qa_app_sim_foreign_q2_world_modules *world = qa_app_sim_create_foreign_q2_world_modules(options);
    CHECK(probe.calls == 1 && world->game == probe.game && world->pack_count == cases[c].packs);
    char order[256] = "";
    for (size_t i = 0; i < probe.module_count; i++) {
      if (i != 0) strcat(order, " ");
      strcat(order, module_role(world, probe.modules[i]));
    }
    CHECK(same(order, cases[c].order));
    if (cases[c].packs == 2) CHECK(same(world->packs[0].pack, "xatrix") && same(world->packs[1].pack, "rogue") && world->has_rerelease);
    /* {...weapons, noise}: the caller's leaves keep their ctx; noise reaches the world's monsters. */
    qa_q2w_event event;
    memset(&event, 0, sizeof event);
    world->ballistics.hooks.emit(world->ballistics.hooks.ctx, event);
    world->ballistics.hooks.noise(world->ballistics.hooks.ctx, (qa_actor_id){0}, (qa_vec3){0, 0, 0}, false);
    CHECK(weapon_emits == (int)c + 1 && weapon_noises == 0 && world->ballistics.hooks.dodge == NULL);
    qa_q2_entity_services_destroy(world->game);
    qa_app_sim_foreign_q2_world_modules_destroy(world);
    q2_world_destroy();
  }
}

int main(void) {
  test_nearby_placement();
  test_selected_q1();
  test_checkpoints();
  test_selected_q2();
  test_foreign_q2_world();
  CHECK(ctx_errors == 0);
  printf("test_sim_monsters: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
