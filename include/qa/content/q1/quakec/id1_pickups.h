/* Port of src/content/q1/quakec/id1-pickups.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_QUAKEC_ID1_PICKUPS_H
#define QA_CONTENT_Q1_QUAKEC_ID1_PICKUPS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/quakec/compat_types.h"
#include "qa/content/q1/quakec/pickup_stage.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/pickups.h"

/* TS QcPickupPolicy. original() returns false for the TS null. */
typedef bool (*qa_q1qc_pickup_counter_original_fn)(void *ctx, double count, double *out);
typedef struct qa_q1qc_pickup_policy {
  void *ctx;
  qa_item_id (*current)(void *ctx, qa_owned_actor actor); /* NULL result = TS null */
  void (*select)(void *ctx, qa_owned_actor actor, qa_item_id item);
  void (*counter)(void *ctx, qa_owned_actor actor, qa_item_id item,
                  qa_q1qc_pickup_counter_original_fn original, void *original_ctx);
} qa_q1qc_pickup_policy;
typedef const qa_q1qc_pickup_policy *(*qa_q1qc_pickup_policy_fn)(void *ctx);
typedef bool (*qa_q1qc_primary_selected_fn)(void *ctx, qa_owned_actor actor);
typedef bool (*qa_q1qc_owns_weapon_fn)(void *ctx, qa_owned_actor actor, qa_item_id item);

/* TS supply() result; ammo_storage backs the weapon offer's ammo. */
typedef struct qa_q1_id1_pickup_supply {
  qa_pickup_supply_offer offer;
  qa_pickup_ammo_grant ammo_storage;
  bool leave;
} qa_q1_id1_pickup_supply;

typedef struct qa_q1_id1_pickup_binding qa_q1_id1_pickup_binding;
/* primary_selected NULL = TS default () => true; owns_weapon /
 * policy NULL = TS undefined. declared stages are borrowed and must
 * outlive the binding (pinned stages are owned). */
qa_q1_id1_pickup_binding *qa_q1_id1_pickup_binding_create(
    qa_q1qc_source source, qa_original_pickup_admission admission,
    qa_q1qc_machine_fn machine, void *machine_ctx,
    qa_q1qc_primary_selected_fn primary_selected, void *primary_ctx,
    qa_q1qc_owns_weapon_fn owns_weapon, void *owns_ctx,
    qa_q1qc_pickup_policy_fn policy, void *policy_ctx,
    qa_q1qc_pickup_stage *declared, size_t declared_count);
void qa_q1_id1_pickup_binding_destroy(qa_q1_id1_pickup_binding *binding);
void qa_q1_id1_pickup_binding_assert_idle(const qa_q1_id1_pickup_binding *binding);
void qa_q1_id1_pickup_binding_observe_store(qa_q1_id1_pickup_binding *binding,
                                            qa_q1qc_entity_store_observation store);
bool qa_q1_id1_pickup_binding_selection_deferred(qa_q1_id1_pickup_binding *binding,
                                                 qa_original_pickup_offer offer);
qa_q1_id1_pickup_supply qa_q1_id1_pickup_binding_supply(qa_q1_id1_pickup_binding *binding,
                                                       qa_original_pickup_offer offer);
void qa_q1_id1_pickup_binding_validate(qa_q1_id1_pickup_binding *binding);
/* TS composeFunctions / composeRegions results; storage owned here. */
typedef struct qa_q1_id1_pickup_function_composed {
  qa_q1_id1_pickup_binding *owner;
  qa_q1qc_function_boundary inner;
  int64_t *functions; size_t function_count;
  qa_q1qc_function_boundary boundary;
} qa_q1_id1_pickup_function_composed;
void qa_q1_id1_pickup_compose_functions(qa_q1_id1_pickup_function_composed *composed,
                                        qa_q1_id1_pickup_binding *binding,
                                        qa_q1qc_function_boundary inner);
void qa_q1_id1_pickup_function_composed_destroy(qa_q1_id1_pickup_function_composed *composed);
typedef struct qa_q1_id1_pickup_region_composed {
  qa_q1_id1_pickup_binding *owner;
  qa_q1qc_inline_boundary inner;
  qa_q1qc_inline_region *regions; size_t region_count;
  qa_q1qc_inline_boundary boundary;
} qa_q1_id1_pickup_region_composed;
void qa_q1_id1_pickup_compose_regions(qa_q1_id1_pickup_region_composed *composed,
                                      qa_q1_id1_pickup_binding *binding,
                                      qa_q1qc_inline_boundary inner);
void qa_q1_id1_pickup_region_composed_destroy(qa_q1_id1_pickup_region_composed *composed);

#endif
