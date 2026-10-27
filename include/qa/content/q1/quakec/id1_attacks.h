/* Port of src/content/q1/quakec/id1-attacks.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_QUAKEC_ID1_ATTACKS_H
#define QA_CONTENT_Q1_QUAKEC_ID1_ATTACKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q1/quakec/compat_types.h"
#include "qa/content/q1/quakec/id1_damage.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"

/* TS Id1SynchronousAttack. */
typedef struct qa_q1_id1_synchronous_attack {
  qa_actor_id actor;
  qa_item_id weapon;
  double time, knockback;
  qa_vec3 direction, point, normal;
} qa_q1_id1_synchronous_attack;

typedef struct qa_q1_id1_sync_attacks qa_q1_id1_sync_attacks;
qa_q1_id1_sync_attacks *qa_q1_id1_sync_attacks_create(qa_q1qc_source source,
                                                      qa_q1qc_machine_fn machine,
                                                      void *machine_ctx);
void qa_q1_id1_sync_attacks_destroy(qa_q1_id1_sync_attacks *attacks);
void qa_q1_id1_sync_attacks_assert_idle(const qa_q1_id1_sync_attacks *attacks);
/* False return = TS null. */
bool qa_q1_id1_sync_attacks_resolve(qa_q1_id1_sync_attacks *attacks,
                                    qa_q1_id1_damage_call call,
                                    qa_q1_id1_synchronous_attack *out);
/* TS compose() result; storage owned here. */
typedef struct qa_q1_id1_sync_composed {
  qa_q1_id1_sync_attacks *owner;
  qa_q1qc_function_boundary inner;
  int64_t *functions; size_t function_count;
  qa_q1qc_function_boundary boundary;
} qa_q1_id1_sync_composed;
void qa_q1_id1_sync_attacks_compose(qa_q1_id1_sync_composed *composed,
                                    qa_q1_id1_sync_attacks *attacks,
                                    qa_q1qc_function_boundary damage);
void qa_q1_id1_sync_composed_destroy(qa_q1_id1_sync_composed *composed);

#endif
