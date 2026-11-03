/* Port of src/content/q1/quakec/id1-environment.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_QUAKEC_ID1_ENVIRONMENT_H
#define QA_CONTENT_Q1_QUAKEC_ID1_ENVIRONMENT_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/content/q1/quakec/compat_types.h"
#include "qa/content/q1/quakec/id1_damage.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"

/* TS Id1PhysicsCallback. kind: "blocked" | "touch". */
typedef struct qa_q1_id1_physics_callback {
  const char *kind;
  qa_actor_id actor, other;
  int64_t function_index;
} qa_q1_id1_physics_callback;
/* TS Id1EnvironmentalDamage. death_type storage backs cause.death_type. */
typedef struct qa_q1_id1_environmental_damage {
  qa_attack_cause cause;
  char death_type[128];
  double time, knockback;
  qa_vec3 direction, point;
} qa_q1_id1_environmental_damage;

typedef struct qa_q1_id1_environment qa_q1_id1_environment;
qa_q1_id1_environment *qa_q1_id1_environment_create(qa_q1qc_source source,
                                                    qa_q1qc_machine_fn machine,
                                                    void *machine_ctx);
/* TS constructor with the optional foreignActor/foreignReference source
 * members; foreign NULL = both absent. The table is copied. */
qa_q1_id1_environment *qa_q1_id1_environment_create_foreign(qa_q1qc_source source,
                                                            const qa_q1qc_foreign_source *foreign,
                                                            qa_q1qc_machine_fn machine,
                                                            void *machine_ctx);
void qa_q1_id1_environment_destroy(qa_q1_id1_environment *environment);
/* callback NULL = TS null. False return = TS null. */
bool qa_q1_id1_environment_resolve(qa_q1_id1_environment *environment,
                                   qa_q1_id1_damage_call call,
                                   const qa_q1_id1_physics_callback *callback,
                                   qa_q1_id1_environmental_damage *out);

#endif
