/* Port of src/app/bootstrap/simulation/actor-execution.ts: the per-source
 * actor execution union (QuakeC, TS Q1, TS Q2, Q3 guest, Q3 source) and its
 * motion/collision/flag projections for SharedPhysics plus the per-actor
 * think/physics turn. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_ACTOR_EXECUTION_H
#define QA_APP_BOOTSTRAP_SIMULATION_ACTOR_EXECUTION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/physics.h"
#include "qa/app/bootstrap/simulation/runtime.h"
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q2/foundation/entity_services.h"
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/time.h"
#include "qa/contracts/world.h"
#include "qa/movement/q1/types.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"
#include "qa/world/scheduler.h"

/* TS Pick<QuakeCSource, "motion" | "collision" | "flags" | "writeFlags" |
 * "isReservedClient" | "runActorOnce" | "readMoveType" | "runThink" |
 * "checkWaterTransition" | "pusherServices">: the members of the QuakeC
 * source that actor execution calls. motion/collision false = TS null;
 * read_move_type false = TS null. */
typedef struct qa_app_sim_quakec_actor_source {
  void *ctx;
  bool (*motion)(void *ctx, qa_owned_actor actor, qa_body_state body, qa_q2_motion *out);
  bool (*collision)(void *ctx, qa_owned_actor actor, qa_app_sim_shared_solid *out);
  qa_app_sim_shared_physics_flags (*flags)(void *ctx, qa_owned_actor actor);
  void (*write_flags)(void *ctx, qa_owned_actor actor, qa_app_sim_shared_physics_flags changes);
  bool (*is_reserved_client)(void *ctx, qa_actor_id actor);
  bool (*run_actor_once)(void *ctx, qa_actor_id actor, qa_frame_context frame);
  bool (*read_move_type)(void *ctx, qa_actor_id actor, double *out);
  void (*run_think)(void *ctx, qa_owned_actor actor, qa_frame_context frame);
  void (*check_water_transition)(void *ctx, qa_owned_actor actor);
  qa_q1_pusher_services (*pusher_services)(void *ctx); /* TS getter */
} qa_app_sim_quakec_actor_source;

typedef enum qa_app_sim_actor_execution_kind {
  QA_APP_SIM_ACTOR_EXECUTION_QUAKEC,
  QA_APP_SIM_ACTOR_EXECUTION_Q1,
  QA_APP_SIM_ACTOR_EXECUTION_Q2,
  QA_APP_SIM_ACTOR_EXECUTION_Q3,
  QA_APP_SIM_ACTOR_EXECUTION_Q3_SOURCE
} qa_app_sim_actor_execution_kind;

/* TS ActorExecution union on kind. Members per kind:
 *  quakec:    actor, quakec (borrowed), content
 *  q1:        q1_entity, q1_services, content
 *  q2:        q2_entity, q2_services, content, read_monster (NULL return =
 *             TS undefined)
 *  q3:        actor, owner, provider, content, step
 *  q3-source: actor, provider, content, motion, collision, step
 * Closures (read_monster, step, motion, collision) share ctx. */
typedef struct qa_app_sim_actor_execution {
  qa_app_sim_actor_execution_kind kind;
  qa_content_id content;
  qa_owned_actor actor;
  const qa_app_sim_quakec_actor_source *quakec;
  qa_q1_actor *q1_entity;
  qa_q1_services *q1_services;
  qa_q2_entity *q2_entity;
  qa_q2_entity_services *q2_services;
  qa_actor_id owner;
  qa_provider_id provider;
  void *ctx;
  qa_q2m_state *(*read_monster)(void *ctx);
  void (*step)(void *ctx, double previous_milliseconds, double milliseconds);
  qa_q2_motion (*motion)(void *ctx, qa_body_state body);
  qa_app_sim_shared_solid (*collision)(void *ctx);
} qa_app_sim_actor_execution;

/* TS ActorExecutionFrame. visited is TS Set<OwnedActor> (caller-owned;
 * members compared with qa_same_owned_actor). */
typedef struct qa_app_sim_actor_execution_frame {
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_app_sim_shared_physics *physics;
  qa_frame_scheduler *scheduler;
  qa_frame_context frame;
  double time_seconds;
  double elapsed;
  qa_app_sim_actor_visited *visited;
} qa_app_sim_actor_execution_frame;

/* TS actorMotion / actorCollision / actorFlags / writeActorFlags. Owner
 * pointers in the returned solid point into the entry's entity (or the
 * entry itself for q3) and live as long as it does. */
qa_q2_motion qa_app_sim_actor_motion(const qa_app_sim_actor_execution *entry, qa_body_state body);
qa_app_sim_shared_solid qa_app_sim_actor_collision(const qa_app_sim_actor_execution *entry);
qa_app_sim_shared_physics_flags qa_app_sim_actor_flags(const qa_app_sim_actor_execution *entry);
void qa_app_sim_write_actor_flags(const qa_app_sim_actor_execution *entry, qa_app_sim_shared_physics_flags changes);
/* TS executeActor: entry kind must be quakec, q1 or q2. */
void qa_app_sim_execute_actor(const qa_app_sim_actor_execution *entry, const qa_app_sim_actor_execution_frame *context);
/* TS executeQuakeCPhysics: context reads actors, bodies, frame, elapsed and
 * physics (step). */
void qa_app_sim_execute_quake_c_physics(const qa_app_sim_quakec_actor_source *source, qa_owned_actor actor,
                                       const qa_app_sim_actor_execution_frame *context);
/* TS Set<OwnedActor>.add over the caller-owned visited set. */
void qa_app_sim_actor_visited_add(qa_app_sim_actor_visited *visited, qa_owned_actor actor);
bool qa_app_sim_actor_visited_has(const qa_app_sim_actor_visited *visited, qa_owned_actor actor);
#endif
