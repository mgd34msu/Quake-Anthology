/* Phase A checks for the SharedSimulation port (runtime.ts):
 *  - qa_ss_map against JS Map / Set / RevisionMap semantics;
 *  - the runtime.ts free helpers (add, seconds, sourceModel, map name);
 *  - step()'s deadline planning against the TS planning block run under bun
 *    (oracle cases below were printed by the verbatim TS block in
 *    docs/runtime_layout.md "Oracles");
 *  - modClients.subscribe (the listener Set) keeps the caller's sentinel ctx.
 * Constructing the simulation needs the phase B methods (the constructor
 * calls registerCombat, createMonsterMovement, ... unconditionally), so
 * construction per source kind is not exercised here yet.
 * GPL-2.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/app/bootstrap/simulation/shared_simulation_internal.h"

static int failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                       \
    }                                                                   \
  } while (0)

static qa_actor_id actor(int64_t slot) { qa_actor_id id; memset(&id, 0, sizeof id); id.slot = slot; id.generation = 1; return id; }

static void test_map_semantics(void) {
  qa_ss_map map; qa_ss_map_init(&map, QA_SS_KEY_ACTOR, sizeof(double));
  qa_actor_id a = actor(3), b = actor(1), c = actor(2);
  double one = 1, two = 2, three = 3;
  qa_ss_map_set(&map, &a, &one); qa_ss_map_set(&map, &b, &two); qa_ss_map_set(&map, &c, &three);
  CHECK(map.count == 3 && map.revision == 3);
  /* Map.set on an existing key replaces in place (iteration order unchanged). */
  double four = 4;
  qa_ss_map_set(&map, &a, &four);
  CHECK(map.revision == 4 && qa_ss_map_key_at(&map, 0)->actor.slot == 3 && *(double *)qa_ss_map_value_at(&map, 0) == 4);
  /* RevisionMap.delete bumps the revision even when absent. */
  qa_actor_id missing = actor(9);
  CHECK(!qa_ss_map_delete(&map, &missing) && map.revision == 5);
  CHECK(qa_ss_map_delete(&map, &b) && map.count == 2 && qa_ss_map_key_at(&map, 1)->actor.slot == 2);
  CHECK(qa_ss_map_get(&map, &b) == NULL && *(double *)qa_ss_map_get(&map, &c) == 3);
  /* Re-adding goes to the end, as a JS Map does after delete. */
  qa_ss_map_set(&map, &b, &two);
  CHECK(qa_ss_map_key_at(&map, 2)->actor.slot == 1);
  qa_ss_map_clear(&map);
  CHECK(map.count == 0 && map.revision == 8);
  qa_ss_map_free(&map);

  /* Set<ActorId> and string keys (owned copies). */
  qa_ss_map set; qa_ss_map_init(&set, QA_SS_KEY_ACTOR, 0);
  qa_ss_map_set(&set, &a, NULL); qa_ss_map_set(&set, &a, NULL);
  CHECK(set.count == 1 && qa_ss_map_has(&set, &a) && !qa_ss_map_has(&set, &b));
  qa_ss_map_free(&set);
  qa_ss_map strings; qa_ss_map_init(&strings, QA_SS_KEY_STRING, sizeof(int));
  char key[16]; strcpy(key, "maps/e1m1.bsp");
  const char *k = key; int v = 7;
  qa_ss_map_set(&strings, &k, &v);
  strcpy(key, "changed");
  const char *lookup = "maps/e1m1.bsp";
  CHECK(qa_ss_map_get(&strings, &lookup) != NULL && *(int *)qa_ss_map_get(&strings, &lookup) == 7);
  qa_ss_map_free(&strings);

  /* number keys use SameValueZero: NaN finds NaN, -0 finds +0. */
  qa_ss_map numbers; qa_ss_map_init(&numbers, QA_SS_KEY_NUMBER, sizeof(int));
  double nan_key = NAN, zero = 0.0, negative_zero = -0.0; int x = 1, y = 2;
  qa_ss_map_set(&numbers, &nan_key, &x); qa_ss_map_set(&numbers, &zero, &y);
  CHECK(*(int *)qa_ss_map_get(&numbers, &nan_key) == 1 && *(int *)qa_ss_map_get(&numbers, &negative_zero) == 2 && numbers.count == 2);
  qa_ss_map_free(&numbers);
}

static void test_helpers(void) {
  /* add() stages through Math.fround. */
  qa_vec3 sum = qa_ss_add((qa_vec3){0.1, 0.2, 1e-46}, (qa_vec3){0.2, 0.0, 0.0});
  CHECK(sum.x == (double)(float)(0.1 + 0.2) && sum.y == (double)(float)0.2 && sum.z == 0);
  CHECK(qa_ss_seconds((qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 1500}) == 1.5);
  CHECK(qa_ss_seconds((qa_source_time){QA_SOURCE_TIME_SECONDS, 2.25}) == 2.25);
  double model = 0;
  CHECK(qa_ss_source_model("*12", &model) && model == 12);
  CHECK(!qa_ss_source_model("*", &model) && !qa_ss_source_model("*1a", &model) && !qa_ss_source_model("models/x.mdl", &model));
  CHECK(qa_ss_is_q1_barrel("misc_explobox") && qa_ss_is_q1_barrel("misc_explobox2") && !qa_ss_is_q1_barrel("misc_explobox3"));
  char *name = qa_ss_map_name("maps/e1m1.bsp"); CHECK(strcmp(name, "e1m1") == 0); free(name);
  name = qa_ss_map_name("base1"); CHECK(strcmp(name, "base1") == 0); free(name);
  name = qa_ss_map_name("dm/maps/q3dm1.bsp"); CHECK(strcmp(name, "dm/maps/q3dm1") == 0); free(name);
  char buffer[32];
  CHECK(strcmp(qa_ss_number_text(800, buffer), "800") == 0 && strcmp(qa_ss_number_text(0.5, buffer), "0.5") == 0);
}

/* step() planning vs the TS block (oracle: bun plan.ts). */
typedef struct plan_case {
  const char *name;
  qa_clock_profile_kind clock; double frame_ms, server_ms;
  const char *settlement;
  double time_seconds; bool time_ms;
  double scheduling, elapsed_input; bool paused;
  bool selected_q2; double q2_map, q2_next, q2_interval;
  bool selected_q3; double q3_server_ms, q3_next, selected_ms;
  bool foreign; const char *foreign_edition; double foreign_seconds;
  /* expected */
  bool has_fixed; double fixed; bool map_run; double elapsed, map_start, map_end;
  size_t count; double ms[8]; bool map[8], q2[8], q3[8];
} plan_case;
static const plan_case cases[] = {
  {"q1 elapsed clamp", QA_CLOCK_Q1_NETQUAKE, 0, 0, "none", 1, false, 1250, 250, false, false, 0, 0, 0, false, 0, 0, 0, false, NULL, 0,
   false, 0, true, 0.1, 1000, 1100, 1, {1100}, {true}, {false}, {false}},
  {"q2 classic catch-up", QA_CLOCK_Q2_CLASSIC, 100, 0, "none", 0.3, false, 900, 600, false, false, 0, 0, 0, false, 0, 0, 0, false, NULL, 0,
   true, 100, true, 0.1, 300, 500, 2, {400, 500}, {true, true}, {false, false}, {false, false}},
  {"q2 rerelease due", QA_CLOCK_Q2_RERELEASE, 25, 0, "none", 50, true, 110, 60, false, false, 0, 0, 0, false, 0, 0, 0, true, "classic", 0,
   true, 25, true, 0.025, 50, 125, 3, {75, 100, 125}, {true, true, true}, {false, false, false}, {false, false, false}},
  {"q3 server frames + selected q3", QA_CLOCK_Q3, 0, 50, "none", 100, true, 260, 160, false, false, 0, 0, 0, true, 50, 125, 260, false, NULL, 0,
   true, 50, true, 0.05, 100, 250, 6, {125, 150, 175, 200, 225, 250}, {false, true, false, true, false, true},
   {false, false, false, false, false, false}, {true, false, true, false, true, false}},
  {"q1 selected q2 weapons", QA_CLOCK_Q1_NETQUAKE, 0, 0, "none", 2, false, 2100, 100, false, true, 2000, 2000, 100, false, 0, 0, 0, false, NULL, 0,
   false, 0, true, 0.1, 2000, 2100, 2, {2000, 2100}, {false, true}, {true, true}, {false, false}},
  {"paused", QA_CLOCK_Q2_CLASSIC, 100, 0, "none", 1, false, 1000, 100, true, false, 0, 0, 0, true, 50, 900, 1000, false, NULL, 0,
   true, 100, false, 0.1, 1000, 1000, 1, {1000}, {false}, {false}, {false}},
  {"settlement active", QA_CLOCK_Q3, 0, 50, "active", 3000, true, 3000, 100, false, false, 0, 0, 0, false, 0, 0, 0, false, NULL, 0,
   true, 50, true, 0.1, 3000, 3000, 1, {3000}, {true}, {false}, {false}},
};
static void test_step_planning(void) {
  for (size_t index = 0; index < sizeof cases / sizeof *cases; index++) {
    const plan_case *c = &cases[index];
    qa_app_shared_simulation sim; memset(&sim, 0, sizeof sim);
    qa_provider_timing timing[2]; memset(timing, 0, sizeof timing);
    timing[0].provider = "test:map"; timing[0].clock.kind = c->clock;
    timing[0].clock.frame_milliseconds = c->frame_ms; timing[0].clock.server_frame_milliseconds = c->server_ms;
    timing[1].provider = "test:weapons"; timing[1].clock.kind = QA_CLOCK_Q3; timing[1].clock.server_frame_milliseconds = c->q3_server_ms;
    sim.recipe.map.entities.provider = "test:map"; sim.recipe.timing = timing; sim.recipe.timing_count = 2;
    sim.weapon_provider.provider = "test:weapons";
    sim.source_round_settlement.kind = c->settlement;
    sim.source_frame.time = (qa_source_time){c->time_ms ? QA_SOURCE_TIME_MILLISECONDS : QA_SOURCE_TIME_SECONDS, c->time_seconds};
    sim.source_scheduling_milliseconds = c->scheduling;
    if (c->selected_q2) {
      sim.has_selected_weapon_source = true; sim.selected_weapon_source.kind = "q2";
      sim.selected_weapon_source.map_milliseconds = c->q2_map; sim.selected_weapon_source.next_milliseconds = c->q2_next;
      sim.selected_weapon_source.interval_milliseconds = c->q2_interval;
    }
    /* Only the presence of selectedQ3Source is read by the planner. */
    if (c->selected_q3) { sim.selected_q3_source = (qa_app_q3_selected_source *)&sim; sim.selected_q3_next = c->q3_next; sim.selected_milliseconds = c->selected_ms; }
    if (c->foreign) {
      sim.has_foreign_world = true; sim.foreign_world.edition = c->foreign_edition;
      sim.foreign_world.clock.frame.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, c->foreign_seconds};
    }
    qa_ss_map_init(&sim.monster_sources, QA_SS_KEY_STRING, sizeof(void *));
    qa_input_batch input = {c->elapsed_input, NULL, 0};
    qa_ss_step_plan plan = qa_app_shared_sim_plan_step(&sim, input, c->paused);
    bool ok = plan.has_fixed == c->has_fixed && (!c->has_fixed || plan.fixed == c->fixed) && plan.map_run == c->map_run
        && plan.elapsed == c->elapsed && plan.map_start_milliseconds == c->map_start && plan.map_end_milliseconds == c->map_end
        && plan.boundary_count == c->count;
    for (size_t i = 0; ok && i < c->count; i++)
      ok = plan.boundaries[i].milliseconds == c->ms[i] && plan.boundaries[i].map == c->map[i]
          && plan.boundaries[i].q2 == c->q2[i] && plan.boundaries[i].q3 == c->q3[i];
    if (!ok) fprintf(stderr, "planning case \"%s\" differs from the TS oracle\n", c->name);
    CHECK(ok);
    qa_ss_step_plan_free(&plan);
    qa_ss_map_free(&sim.monster_sources);
  }
}

/* modClients.subscribe keeps the listener's own ctx (sentinel). */
static int sentinel_value;
static void *seen_ctx;
static void sentinel_listener(void *ctx, qa_mod_client_event event) { (void)event; seen_ctx = ctx; }
static void test_listener_sentinel(void) {
  qa_app_shared_simulation sim; memset(&sim, 0, sizeof sim);
  qa_mod_client_unsubscribe stop = qa_app_shared_sim_subscribe_listener(&sim, sentinel_listener, &sentinel_value);
  CHECK(sim.mod_client_listener_count == 1 && sim.mod_client_listeners[0].ctx == &sentinel_value);
  /* Set semantics: the same listener and ctx are added once. */
  qa_app_shared_sim_add_mod_client_listener(&sim, sentinel_listener, &sentinel_value);
  CHECK(sim.mod_client_listener_count == 1);
  qa_mod_client_event event; memset(&event, 0, sizeof event); event.kind = "admitted";
  sim.mod_client_listeners[0].listener(sim.mod_client_listeners[0].ctx, event);
  CHECK(seen_ctx == &sentinel_value);
  stop.invoke(stop.ctx);
  CHECK(sim.mod_client_listener_count == 0);
  free(sim.mod_client_listeners);
}

int main(void) {
  test_map_semantics();
  test_helpers();
  test_step_planning();
  test_listener_sentinel();
  if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
  puts("test_shared_simulation_core: ok");
  return 0;
}
