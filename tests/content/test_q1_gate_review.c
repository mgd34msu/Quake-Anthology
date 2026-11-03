/* content/q1 gate full read: checks for the divergences the read fixed.
 * Expected values come from tests/content/oracles/q1_gate_review.ts (bun). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q1_world_fixture.h"
#include "qa/content/q1/foundation/checkpoint.h"
#include "qa/content/q1/foundation/pickups.h"
#include "qa/content/q1/foundation/runtime.h"

static int checks, failures;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

#define PROP(k, v) {(char *)(k), (char *)(v)}
static const qa_bounds WORLD = {{-512, -512, -512}, {512, 512, 512}};

static qa_q1_entity worldspawn_entity(void) {
  static qa_q1_entity_property props[] = {PROP("classname", "worldspawn")};
  const qa_q1_entity entity = {props, 1};
  return entity;
}

static bool spawn_list(q1_world_fixture *fx, const qa_q1_entity *list, size_t count, const qa_bounds *bounds,
                       size_t bound_count, qa_q1_spawn_report *report) {
  return qa_q1_foundation_spawn_entities(fx->game, list, count, bounds, bound_count, "gate", NULL, 0, report);
}

/* 1. restoreFoundation assigns game.time through the Math.fround setter
 *    (oracle restored_time 0.10000000149011612). */
static void restore_time_is_binary32(void) {
  q1_world_fixture source, target;
  CHECK(q1_world_fixture_create(&source, q1_world_fixture_options()));
  CHECK(q1_world_fixture_create(&target, q1_world_fixture_options()));
  const qa_q1_entity list[] = {worldspawn_entity()};
  qa_q1_spawn_report report;
  CHECK(spawn_list(&source, list, 1, &WORLD, 1, &report));
  qa_q1_spawn_report_free(&report);
  qa_q1_foundation_checkpoint saved;
  CHECK(qa_q1_services_capture(source.game, &saved));
  saved.entity_count = 0;
  saved.has_world = false;
  saved.time = 0.1;
  qa_q1_services_restore(target.game, &saved, false);
  CHECK(target.game->time == 0.10000000149011612);
  qa_q1_checkpoint_free(&saved);
  CHECK(source.ctx_errors == 0 && target.ctx_errors == 0);
  q1_world_fixture_destroy(&target);
  q1_world_fixture_destroy(&source);
}

/* 2. A foreign world catches UnknownQ1SpawnError: a classname the Q3 pickup
 *    hook accepts spawns as a pickup; only info_gate_unknown is reported
 *    (oracle foreign_count 1, foreign 2 info_gate_unknown,
 *    pickup_model models/powerups/instant/quad.md3, pickup_touch item_touch). */
static void foreign_world_catches_unknown_spawn(void) {
  qa_q1_options options = q1_world_fixture_options();
  options.foreign_world = true;
  q1_world_fixture fx;
  CHECK(q1_world_fixture_create(&fx, options));
  fx.game->foreign_pickup.ctx = NULL;
  fx.game->foreign_pickup.fn = qa_q1_foreign_q3_pickup;
  static qa_q1_entity_property quad[] = {PROP("classname", "item_quad"), PROP("origin", "0 0 0")};
  static qa_q1_entity_property unknown[] = {PROP("classname", "info_gate_unknown"), PROP("origin", "8 0 0")};
  const qa_q1_entity list[] = {worldspawn_entity(), {quad, 2}, {unknown, 2}};
  qa_q1_spawn_report report;
  CHECK(spawn_list(&fx, list, 3, &WORLD, 1, &report));
  CHECK(report.foreign_count == 1);
  if (report.foreign_count == 1) {
    CHECK(report.foreign[0].ordinal == 2);
    CHECK(strcmp(report.foreign[0].classname, "info_gate_unknown") == 0);
  }
  qa_q1_actor *pickup = NULL;
  for (size_t i = 0; i < fx.game->entity_count; i++)
    if (strcmp(fx.game->entities[i]->classname, "item_quad") == 0) pickup = fx.game->entities[i];
  CHECK(pickup != NULL);
  if (pickup != NULL) {
    CHECK(strcmp(pickup->model, "models/powerups/instant/quad.md3") == 0);
    CHECK(pickup->touch.name != NULL && strcmp(pickup->touch.name, "item_touch") == 0);
  }
  qa_q1_spawn_report_free(&report);
  CHECK(fx.ctx_errors == 0);
  q1_world_fixture_destroy(&fx);
}

/* 3. linkDoors has no group limit: 70 overlapping doors form one group
 *    (oracle door_count 70, master_group 70, last_group 70). */
static void link_doors_groups_every_door(void) {
  q1_world_fixture fx;
  CHECK(q1_world_fixture_create(&fx, q1_world_fixture_options()));
  static qa_q1_entity_property master[] = {PROP("classname", "func_door"), PROP("model", "*1"),
                                           PROP("targetname", "gate_doors")};
  static qa_q1_entity_property door[] = {PROP("classname", "func_door"), PROP("model", "*1")};
  qa_q1_entity list[71];
  list[0] = worldspawn_entity();
  list[1] = (qa_q1_entity){master, 3};
  for (size_t i = 2; i < 71; i++) list[i] = (qa_q1_entity){door, 2};
  const qa_bounds bounds[2] = {WORLD, {{0, 0, 0}, {64, 8, 64}}};
  qa_q1_spawn_report report;
  CHECK(spawn_list(&fx, list, 71, bounds, 2, &report));
  qa_q1_spawn_report_free(&report);
  qa_q1_actor *doors[80];
  size_t count = 0;
  for (size_t i = 0; i < fx.game->entity_count && count < 80; i++)
    if (strcmp(fx.game->entities[i]->classname, "func_door") == 0) doors[count++] = fx.game->entities[i];
  CHECK(count == 70);
  if (count == 70) {
    CHECK(doors[0]->door_group_count == 70);
    CHECK(doors[69]->door_group_count == 70);
    CHECK(doors[69]->door_group[69] == doors[69]);
  }
  CHECK(fx.ctx_errors == 0);
  q1_world_fixture_destroy(&fx);
}

/* 4. adjoin and spawnEntity restore their scoped state when the callee raises
 *    (oracle adjoin_error gate adjoin, adjoin_duplicate Q1 spawn handler already
 *    registered: gate_owned, spawn_error gate spawn, deathmatch_after 0). */
static void spawn_noop(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; (void)game; (void)entity;
}
static void spawn_raises(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; (void)game; (void)entity;
  qa_raise("Error", 0, 0, "gate spawn");
}
static void adjoin_raises(void *ctx, qa_q1_services *game) {
  (void)ctx; (void)game;
  qa_raise("Error", 0, 0, "gate adjoin");
}
typedef struct gate_call { qa_q1_services *game; qa_q1_actor *actor; } gate_call;
static void call_adjoin(void *ctx) {
  const gate_call *call = ctx;
  qa_q1_adjoin(call->game, adjoin_raises, NULL);
}
static void call_register_owned(void *ctx) {
  const gate_call *call = ctx;
  qa_q1_register_spawn(call->game, "gate_owned", spawn_noop, NULL);
}
static void call_spawn_context(void *ctx) {
  const gate_call *call = ctx;
  const double deathmatch = 3;
  qa_q1_spawn_entity_context(call->game, call->actor, &deathmatch);
}
static void scoped_state_survives_raise(void) {
  q1_world_fixture fx;
  CHECK(q1_world_fixture_create(&fx, q1_world_fixture_options()));
  qa_q1_register_spawn(fx.game, "gate_owned", spawn_noop, NULL);
  gate_call call = {fx.game, NULL};
  qa_caught_error caught;
  CHECK(!qa_try(call_adjoin, &call, &caught));
  CHECK(strcmp(caught.message, "gate adjoin") == 0);
  CHECK(!qa_try(call_register_owned, &call, &caught));
  CHECK(strcmp(caught.message, "Q1 spawn handler already registered: gate_owned") == 0);
  qa_q1_register_spawn(fx.game, "gate_throws", spawn_raises, NULL);
  const qa_q1_entity list[] = {worldspawn_entity()};
  qa_q1_spawn_report report;
  CHECK(spawn_list(&fx, list, 1, &WORLD, 1, &report));
  qa_q1_spawn_report_free(&report);
  call.actor = qa_q1_create(fx.game, "gate_throws", NULL, NULL);
  CHECK(!qa_try(call_spawn_context, &call, &caught));
  CHECK(strcmp(caught.message, "gate spawn") == 0);
  CHECK(fx.game->options.deathmatch == 0);
  CHECK(fx.ctx_errors == 0);
  q1_world_fixture_destroy(&fx);
}

/* 5. Raise paths that must release what they allocated (run under ASan's leak
 *    check): oracle inline_error Missing inline model *5, extension_error
 *    Unknown or duplicate Q1 saved extension: gate:nope. */
typedef struct gate_spawn { q1_world_fixture *fx; const qa_q1_entity *list; size_t count; qa_q1_spawn_report report; } gate_spawn;
static void call_spawn_list(void *ctx) {
  gate_spawn *spawn = ctx;
  (void)spawn_list(spawn->fx, spawn->list, spawn->count, &WORLD, 1, &spawn->report);
}
typedef struct gate_restore { q1_world_fixture *fx; const qa_q1_foundation_checkpoint *saved; } gate_restore;
static void call_restore(void *ctx) {
  const gate_restore *restore = ctx;
  qa_q1_services_restore(restore->fx->game, restore->saved, false);
}
static void raise_paths_release_storage(void) {
  q1_world_fixture fx;
  CHECK(q1_world_fixture_create(&fx, q1_world_fixture_options()));
  static qa_q1_entity_property wall[] = {PROP("classname", "func_wall"), PROP("model", "*5")};
  const qa_q1_entity list[] = {worldspawn_entity(), {wall, 2}};
  gate_spawn spawn = {&fx, list, 2, {0}};
  qa_caught_error caught;
  CHECK(!qa_try(call_spawn_list, &spawn, &caught));
  CHECK(strcmp(caught.message, "Missing inline model *5") == 0);
  q1_world_fixture_destroy(&fx);

  q1_world_fixture source, target;
  CHECK(q1_world_fixture_create(&source, q1_world_fixture_options()));
  CHECK(q1_world_fixture_create(&target, q1_world_fixture_options()));
  const qa_q1_entity world_only[] = {worldspawn_entity()};
  qa_q1_spawn_report report;
  CHECK(spawn_list(&source, world_only, 1, &WORLD, 1, &report));
  qa_q1_spawn_report_free(&report);
  qa_q1_foundation_checkpoint saved;
  CHECK(qa_q1_services_capture(source.game, &saved));
  saved.entity_count = 0;
  saved.has_world = false;
  void *captured_extensions = saved.extensions;
  const size_t captured_count = saved.extension_count;
  saved.extensions = calloc(1, sizeof *saved.extensions);
  CHECK(saved.extensions != NULL);
  if (saved.extensions != NULL) {
    saved.extensions[0].id = "gate:nope";
    saved.extensions[0].bytes = NULL;
    saved.extensions[0].len = 0;
    saved.extension_count = 1;
    gate_restore restore = {&target, &saved};
    CHECK(!qa_try(call_restore, &restore, &caught));
    CHECK(strcmp(caught.message, "Unknown or duplicate Q1 saved extension: gate:nope") == 0);
    free(saved.extensions);
  }
  saved.extensions = captured_extensions;
  saved.extension_count = captured_count;
  qa_q1_checkpoint_free(&saved);
  q1_world_fixture_destroy(&target);
  q1_world_fixture_destroy(&source);
}

int main(void) {
  restore_time_is_binary32();
  foreign_world_catches_unknown_spawn();
  link_doors_groups_every_door();
  scoped_state_survives_raise();
  raise_paths_release_storage();
  printf("test_q1_gate_review: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
