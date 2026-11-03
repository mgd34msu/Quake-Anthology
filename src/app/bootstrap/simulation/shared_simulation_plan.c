/* Port of src/app/bootstrap/simulation/runtime.ts step() deadline planning
 * (the block from `const fixed = ...` to `const boundaries = ...`), kept in
 * its own object so its checks link without the rest of the class.
 * GPL-2.0-or-later. */
#include "shared_simulation_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static double ss_time_seconds(const qa_app_shared_simulation *sim) { return qa_ss_seconds(sim->source_frame.time); }
static void ss_fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static int ss_double_order(const void *a, const void *b) {
  double left = *(const double *)a, right = *(const double *)b;
  return (left > right) - (left < right);
}
typedef struct ss_deadlines { double *values; size_t count, capacity; } ss_deadlines;
static void ss_deadline_add(ss_deadlines *set, double value) {
  for (size_t i = 0; i < set->count; i++) if (set->values[i] == value) return;
  if (set->count == set->capacity) {
    set->capacity = set->capacity ? set->capacity * 2 : 16;
    double *grown = realloc(set->values, set->capacity * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    set->values = grown;
  }
  set->values[set->count++] = value;
}
static bool ss_deadline_has(const ss_deadlines *set, double value) {
  for (size_t i = 0; i < set->count; i++) if (set->values[i] == value) return true;
  return false;
}
/* ---- step planning (5244-5296) ---- */
qa_ss_step_plan qa_app_shared_sim_plan_step(qa_app_shared_simulation *sim, qa_input_batch input, bool paused) {
  qa_ss_step_plan plan; memset(&plan, 0, sizeof plan);
  bool active = qa_ss_is(sim->source_round_settlement.kind, "active");
  qa_clock_profile profile = qa_app_sim_provider_timing(sim->recipe, sim->recipe.map.entities.provider).clock;
  plan.has_fixed = profile.kind == QA_CLOCK_Q2_CLASSIC || profile.kind == QA_CLOCK_Q2_RERELEASE || profile.kind == QA_CLOCK_Q3;
  plan.fixed = profile.kind == QA_CLOCK_Q2_CLASSIC ? 100 : profile.kind == QA_CLOCK_Q2_RERELEASE ? profile.frame_milliseconds
      : profile.kind == QA_CLOCK_Q3 ? profile.server_frame_milliseconds : 0;
  /* Frame clocks accumulate fractional seconds; deadline math is integral. */
  double now_milliseconds = qa_js_round(ss_time_seconds(sim) * 1000);
  plan.map_run = active || (!paused && input.elapsed_milliseconds > 0 && (!plan.has_fixed
      || sim->source_scheduling_milliseconds >= now_milliseconds + (profile.kind == QA_CLOCK_Q3 ? profile.server_frame_milliseconds : 0)));
  plan.elapsed = active ? 0.1 : !plan.has_fixed ? qa_js_min(0.1, qa_js_max(0.001, input.elapsed_milliseconds / 1000)) : plan.fixed / 1000;
  qa_ss_selected_weapon_source *selected_q2 = sim->has_selected_weapon_source && qa_ss_is(sim->selected_weapon_source.kind, "q2")
      ? &sim->selected_weapon_source : NULL;
  plan.map_start_milliseconds = selected_q2 ? selected_q2->map_milliseconds : now_milliseconds;
  ss_deadlines map = {0}, all = {0}, q3 = {0}, weapons = {0};
  if (active) ss_deadline_add(&map, plan.map_start_milliseconds);
  else if (plan.map_run && profile.kind == QA_CLOCK_Q3) {
    for (double due = now_milliseconds + profile.server_frame_milliseconds; due <= sim->source_scheduling_milliseconds; due += profile.server_frame_milliseconds)
      ss_deadline_add(&map, due);
  } else if (plan.map_run && plan.has_fixed) {
    /* Q2 runs when the current tick is due, one interval ahead of its host clock.
     * Bound shared-host work, not elapsed time: outstanding debt survives this bundle. */
    const double maximum_q2_catchup_milliseconds = 200;
    for (double due = plan.map_start_milliseconds; due <= sim->source_scheduling_milliseconds
         && due - plan.map_start_milliseconds < maximum_q2_catchup_milliseconds; due += plan.fixed) ss_deadline_add(&map, due + plan.fixed);
  } else if (plan.map_run) ss_deadline_add(&map, plan.map_start_milliseconds + plan.elapsed * 1000);
  double end = plan.map_start_milliseconds;
  if (map.count) { end = map.values[0]; for (size_t i = 1; i < map.count; i++) end = qa_js_max(end, map.values[i]); }
  plan.map_end_milliseconds = end;
  if (map.count == 0) ss_deadline_add(&all, end);
  else for (size_t i = 0; i < map.count; i++) ss_deadline_add(&all, map.values[i]);
  if (sim->selected_q3_source) {
    qa_clock_profile clock = qa_app_sim_provider_timing(sim->recipe, sim->weapon_provider.provider).clock;
    if (clock.kind != QA_CLOCK_Q3) ss_fail("Selected Q3 source clock changed");
    if (!paused) for (double due = sim->selected_q3_next; due <= sim->selected_milliseconds; due += clock.server_frame_milliseconds) {
      ss_deadline_add(&all, due); ss_deadline_add(&q3, due);
    }
  }
  if (plan.map_run) {
    if (selected_q2) for (double due = selected_q2->next_milliseconds; due <= end; due += selected_q2->interval_milliseconds) {
      ss_deadline_add(&all, due); ss_deadline_add(&weapons, due);
    }
    for (size_t i = 0; i < sim->monster_sources.count; i++) {
      qa_app_sim_selected_monster_source *source = *(qa_app_sim_selected_monster_source **)qa_ss_map_value_at(&sim->monster_sources, i);
      if (!qa_ss_is(source->kind, "q2")) continue;
      qa_q2_entity_services *game = source->q2->game;
      double interval = qa_q2_entity_services_game(game)->options.edition == QA_Q2_EDITION_CLASSIC ? 100 : 25;
      for (double due = qa_js_round(qa_ss_seconds(source->clock.frame.time) * 1000) + interval; due <= end; due += interval) ss_deadline_add(&all, due);
    }
    if (sim->has_foreign_world) {
      double interval = qa_ss_is(sim->foreign_world.edition, "classic") ? 100 : 25;
      for (double due = qa_js_round(qa_ss_seconds(sim->foreign_world.clock.frame.time) * 1000) + interval; due <= end; due += interval) ss_deadline_add(&all, due);
    }
  }
  qsort(all.values, all.count, sizeof *all.values, ss_double_order);
  plan.boundary_count = all.count;
  plan.boundaries = calloc(all.count ? all.count : 1, sizeof *plan.boundaries);
  if (!plan.boundaries) qa_range_error("Out of memory");
  for (size_t i = 0; i < all.count; i++)
    plan.boundaries[i] = (qa_ss_boundary){all.values[i], ss_deadline_has(&map, all.values[i]), ss_deadline_has(&weapons, all.values[i]),
                                          ss_deadline_has(&q3, all.values[i])};
  free(map.values); free(all.values); free(q3.values); free(weapons.values);
  return plan;
}
void qa_ss_step_plan_free(qa_ss_step_plan *plan) { free(plan->boundaries); plan->boundaries = NULL; plan->boundary_count = 0; }

