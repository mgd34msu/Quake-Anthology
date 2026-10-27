/* Port of src/content/q1/quakec/id1-damage.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_QUAKEC_ID1_DAMAGE_H
#define QA_CONTENT_Q1_QUAKEC_ID1_DAMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/content/q1/quakec/compat_types.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/mod_callbacks.h"
#include "qa/world/gameplay/authority.h"

/* TS Id1DamageCall. */
typedef struct qa_q1_id1_damage_call {
  qa_q1qc_call_site call;
  qa_actor_id target, inflictor, attacker;
  double amount;
} qa_q1_id1_damage_call;

/* TS Id1DamageProjection (whole table optional; admit/reaction/
 * completed may be NULL). reference's NULL actor is TS null. */
typedef struct qa_q1_id1_damage_projection {
  void *ctx;
  bool (*admit)(void *ctx, qa_damage_request request);
  qa_actor_id (*actor)(void *ctx, int64_t reference);
  int64_t (*reference)(void *ctx, const qa_actor_id *actor);
  void (*reaction)(void *ctx, qa_damage_request request,
                   qa_source_damage_result result, qa_q1qc_function_execution execute);
  void (*completed)(void *ctx, qa_damage_request request, qa_damage_outcome outcome);
} qa_q1_id1_damage_projection;
typedef qa_damage_request (*qa_q1_id1_resolve_request_fn)(void *ctx, qa_q1_id1_damage_call call);

typedef struct qa_q1_id1_damage_binding qa_q1_id1_damage_binding;
/* projection/declared_armor NULL = TS undefined. declared_scale NULL
 * means no declared scale (declared_scale_call then ignored). */
qa_q1_id1_damage_binding *qa_q1_id1_damage_binding_create(
    qa_q1qc_source source, qa_gameplay_authority *authority,
    qa_q1qc_machine_fn machine, void *machine_ctx,
    qa_q1_id1_resolve_request_fn resolve_request, void *resolve_ctx,
    const qa_q1_id1_damage_projection *projection,
    const qa_mod_qc_armor_stage *declared_armor,
    const qa_mod_source_call *declared_scale_call,
    const qa_mod_qc_damage_scale *declared_scale);
void qa_q1_id1_damage_binding_destroy(qa_q1_id1_damage_binding *binding);
const qa_q1qc_function_boundary *qa_q1_id1_damage_function_boundary(
    const qa_q1_id1_damage_binding *binding);
const qa_q1qc_inline_boundary *qa_q1_id1_damage_inline_boundary(
    const qa_q1_id1_damage_binding *binding);
/* False return = TS null. */
bool qa_q1_id1_damage_multiplier_of(qa_q1_id1_damage_binding *binding, qa_actor_id actor,
                                    int64_t reference, double seconds, double *out);
bool qa_q1_id1_damage_amount_of(qa_q1_id1_damage_binding *binding, qa_actor_id actor,
                                int64_t reference, double seconds, double amount, double *out);
/* False return = TS null. The stage's ctx is binding-owned. */
bool qa_q1_id1_damage_protection_stage(qa_q1_id1_damage_binding *binding,
                                       qa_owned_actor actor, qa_protection_channel channel,
                                       qa_source_armor_stage *out);
qa_armor_state qa_q1_id1_damage_read_armor(const qa_q1_id1_damage_binding *binding,
                                          const qa_q1qc_words *words);
void qa_q1_id1_damage_observe_call(qa_q1_id1_damage_binding *binding, qa_q1qc_call_site call);
void qa_q1_id1_damage_observe_entity_store(qa_q1_id1_damage_binding *binding,
                                           qa_q1qc_entity_store_observation store);

#endif
