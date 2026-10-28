/* Port of src/app/bootstrap/simulation/actor-execution.ts. The TS free
 * functions read/write Q1/Q2 entity services through SharedPhysics;
 * those services are fn+ctx here (physics slice unlanded). GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/actor_execution.h"
#include <stddef.h>
const char *qa_app_sim_actor_motion(const qa_app_sim_actor_execution *e, qa_actor_id actor) { return e->motion != NULL ? e->motion(e->ctx, actor) : NULL; }
const char *qa_app_sim_actor_collision(const qa_app_sim_actor_execution *e, qa_actor_id actor) { return e->collision != NULL ? e->collision(e->ctx, actor) : NULL; }
double qa_app_sim_actor_flags(const qa_app_sim_actor_execution *e, qa_actor_id actor) { return e->flags != NULL ? e->flags(e->ctx, actor) : 0; }
void qa_app_sim_write_actor_flags(const qa_app_sim_actor_execution *e, qa_actor_id actor, double flags) { if (e->write_flags != NULL) e->write_flags(e->ctx, actor, flags); }
void qa_app_sim_execute_actor(const qa_app_sim_actor_execution *e, qa_app_sim_actor_execution_frame frame) { if (e->execute != NULL) e->execute(e->ctx, frame); }
void qa_app_sim_execute_quakec_physics(const qa_app_sim_actor_execution *e, qa_app_sim_actor_execution_frame frame) { if (e->execute_quakec_physics != NULL) e->execute_quakec_physics(e->ctx, frame); }
