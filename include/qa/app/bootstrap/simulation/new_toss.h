/* Port of src/app/bootstrap/simulation/new-toss.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NEW_TOSS_H
#define QA_APP_BOOTSTRAP_SIMULATION_NEW_TOSS_H
#include <stdbool.h>
#include "qa/content/q2/foundation/host.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/scene.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"
typedef struct qa_app_sim_q2_new_toss_services {
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_numeric_operations numeric;
  const char *edition; /* "classic"|"rerelease" */
  double world_gravity, max_velocity, stop_speed;
  bool team_slave;
  void *ctx;
  void (*water)(void *ctx, double *out_level, double *out_type);
  qa_trace_result (*trace)(void *ctx, qa_vec3 start, qa_vec3 end);
  bool (*hit_actor)(void *ctx, qa_trace_result trace, qa_actor_id *out);
  void (*fly_move)(void *ctx, double elapsed);
  void (*write_angular_velocity)(void *ctx, qa_vec3 velocity);
  void (*touch_triggers)(void *ctx);
  double (*point_contents)(void *ctx, qa_vec3 origin);
  void (*write_water)(void *ctx, double water_level, double water_type);
  void (*water_sound)(void *ctx, qa_vec3 origin);
} qa_app_sim_q2_new_toss_services;
/* Returns "moved"|"stopped"|"team-slave"|"removed". */
const char *qa_app_sim_step_q2_new_toss(qa_owned_actor actor, double elapsed, qa_q2_motion motion, qa_app_sim_q2_new_toss_services host);
#endif
