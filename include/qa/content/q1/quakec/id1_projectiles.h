/* Port of src/content/q1/quakec/id1-projectiles.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_QUAKEC_ID1_PROJECTILES_H
#define QA_CONTENT_Q1_QUAKEC_ID1_PROJECTILES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/quakec/compat_types.h"
#include "qa/content/q1/quakec/id1_damage.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"

/* TS Id1ProjectileAttack (launch/trace nullable via has_ flags). */
typedef struct qa_q1_id1_projectile_attack {
  qa_item_id weapon;
  double time;
  bool has_launch; qa_actor_id launch_owner; double launch_emitted_at;
  bool has_trace; qa_vec3 trace_point, trace_normal;
} qa_q1_id1_projectile_attack;
/* TS capture() entry. */
typedef struct qa_q1_id1_projectile_capture {
  qa_saved_actor_id actor, owner;
  double emitted_at;
  qa_item_id weapon;
} qa_q1_id1_projectile_capture;

typedef struct qa_q1_id1_projectile_attacks qa_q1_id1_projectile_attacks;
qa_q1_id1_projectile_attacks *qa_q1_id1_projectile_attacks_create(
    qa_q1qc_source source, qa_q1qc_machine_fn machine, void *machine_ctx);
void qa_q1_id1_projectile_attacks_destroy(qa_q1_id1_projectile_attacks *attacks);
/* Malloc'd capture entries (free with free()). */
qa_q1_id1_projectile_capture *qa_q1_id1_projectile_attacks_capture(
    qa_q1_id1_projectile_attacks *attacks, size_t *out_count);
/* TS restore(unknown) over a checkpoint value tree. */
void qa_q1_id1_projectile_attacks_restore(qa_q1_id1_projectile_attacks *attacks,
                                          const qa_save_value *value);
void qa_q1_id1_projectile_attacks_observe_store(qa_q1_id1_projectile_attacks *attacks,
                                                qa_q1qc_entity_store_observation store);
/* False return = TS null. */
bool qa_q1_id1_projectile_attacks_resolve(qa_q1_id1_projectile_attacks *attacks,
                                          qa_q1_id1_damage_call call,
                                          qa_q1_id1_projectile_attack *out);
/* TS compose() result; storage owned here. */
typedef struct qa_q1_id1_projectile_composed {
  qa_q1_id1_projectile_attacks *owner;
  qa_q1qc_function_boundary inner;
  int64_t *functions; size_t function_count;
  qa_q1qc_function_boundary boundary;
} qa_q1_id1_projectile_composed;
void qa_q1_id1_projectile_attacks_compose(qa_q1_id1_projectile_composed *composed,
                                          qa_q1_id1_projectile_attacks *attacks,
                                          qa_q1qc_function_boundary inner);
void qa_q1_id1_projectile_composed_destroy(qa_q1_id1_projectile_composed *composed);

#endif
