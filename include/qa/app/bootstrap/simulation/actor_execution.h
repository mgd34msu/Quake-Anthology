/* Port of src/app/bootstrap/simulation/actor-execution.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_ACTOR_EXECUTION_H
#define QA_APP_BOOTSTRAP_SIMULATION_ACTOR_EXECUTION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/identity.h"
typedef struct qa_app_sim_actor_execution_frame {
  qa_actor_id actor; double time_seconds; double elapsed_seconds; const char *phase;
} qa_app_sim_actor_execution_frame;
typedef struct qa_app_sim_actor_execution {
  void *ctx;
  const char *(*motion)(void *ctx, qa_actor_id actor);
  const char *(*collision)(void *ctx, qa_actor_id actor);
  double (*flags)(void *ctx, qa_actor_id actor);
  void (*write_flags)(void *ctx, qa_actor_id actor, double flags);
  void (*execute)(void *ctx, qa_app_sim_actor_execution_frame frame);
  void (*execute_quakec_physics)(void *ctx, qa_app_sim_actor_execution_frame frame);
} qa_app_sim_actor_execution;
const char *qa_app_sim_actor_motion(const qa_app_sim_actor_execution *e, qa_actor_id actor);
const char *qa_app_sim_actor_collision(const qa_app_sim_actor_execution *e, qa_actor_id actor);
double qa_app_sim_actor_flags(const qa_app_sim_actor_execution *e, qa_actor_id actor);
void qa_app_sim_write_actor_flags(const qa_app_sim_actor_execution *e, qa_actor_id actor, double flags);
void qa_app_sim_execute_actor(const qa_app_sim_actor_execution *e, qa_app_sim_actor_execution_frame frame);
void qa_app_sim_execute_quakec_physics(const qa_app_sim_actor_execution *e, qa_app_sim_actor_execution_frame frame);
#endif
