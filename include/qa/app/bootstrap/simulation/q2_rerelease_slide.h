/* Port of src/app/bootstrap/simulation/q2-rerelease-slide.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q2_RERELEASE_SLIDE_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q2_RERELEASE_SLIDE_H
#include <stdbool.h>
#include "qa/contracts/numeric.h"
#include "qa/contracts/scene.h"
#include "qa/movement/q2/rerelease.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"
typedef struct qa_app_sim_q2_rerelease_fly_move_services {
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  void *ctx;
  qa_trace_result (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds);
  bool (*hit_actor)(void *ctx, qa_trace_result trace, qa_actor_id *out);
  void (*impact)(void *ctx, qa_trace_result trace);
  bool (*take_kill_velocity)(void *ctx);
} qa_app_sim_q2_rerelease_fly_move_services;
typedef struct qa_app_sim_q2_rerelease_fly_move qa_app_sim_q2_rerelease_fly_move;
qa_app_sim_q2_rerelease_fly_move *qa_app_sim_create_q2_rerelease_fly_move(qa_numeric_operations numeric, qa_q2_rerelease_movement_context *context);
void qa_app_sim_q2_rerelease_fly_move_destroy(qa_app_sim_q2_rerelease_fly_move *move);
void qa_app_sim_q2_rerelease_fly_move_run(qa_app_sim_q2_rerelease_fly_move *move, qa_owned_actor actor, double elapsed, qa_app_sim_q2_rerelease_fly_move_services host);
#endif
