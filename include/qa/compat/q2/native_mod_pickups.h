/* Port of src/compat/q2/native-mod-pickups.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_PICKUPS_H
#define QA_COMPAT_Q2_NATIVE_MOD_PICKUPS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/contracts/original_pickups.h"
#include "qa/foundation/support.h"
#include "qa/world/session/mods.h"

void qa_compat_q2_validate_native_mod_pickups(const qa_native_mod_declaration *declaration);
/* TS Inputs map, as rows; the seven inputs are in TS insertion order. */
typedef struct qa_compat_q2_native_pickup_input { qa_mod_callback_input name; qa_mod_runtime_value value; } qa_compat_q2_native_pickup_input;
typedef struct qa_compat_q2_native_pickup_inputs { const qa_compat_q2_native_pickup_input *rows; size_t count; } qa_compat_q2_native_pickup_inputs;
/** TS Operations. context/observe run execute(execute_ctx) inside their scope.
 * invoke returns false for the TS null. */
typedef struct qa_compat_q2_native_mod_pickup_operations {
  void *ctx;
  void (*current)(void *ctx);
  bool (*eligible)(void *ctx, qa_actor_id actor);
  void (*context)(void *ctx, const qa_native_mod_pickup *definition, const qa_original_pickup_offer *offer, qa_compat_q2_native_pickup_inputs inputs, void *execute_ctx, void (*execute)(void *execute_ctx));
  void (*observe)(void *ctx, qa_actor_id actor, const qa_original_pickup_execution *observer, void *execute_ctx, void (*execute)(void *execute_ctx));
  bool (*invoke)(void *ctx, const qa_native_mod_source_call *call, qa_compat_q2_native_pickup_inputs inputs, double *out);
} qa_compat_q2_native_mod_pickup_operations;
typedef struct qa_compat_q2_native_mod_pickups qa_compat_q2_native_mod_pickups;
qa_compat_q2_native_mod_pickups *qa_compat_q2_native_mod_pickups_create(const qa_native_mod_pickup *definitions, size_t definition_count, const qa_mod_host_services *services, qa_provider_id owner, qa_compat_q2_native_mod_pickup_operations operations);
/* TS protection(): the array stays valid until the next call. */
const qa_original_pickup_rule *qa_compat_q2_native_mod_pickups_protection(qa_compat_q2_native_mod_pickups *s, qa_actor_id actor, qa_protection_channel channel, size_t *out_count);
void qa_compat_q2_native_mod_pickups_activate(qa_compat_q2_native_mod_pickups *s);
void qa_compat_q2_native_mod_pickups_bind_actor(qa_compat_q2_native_mod_pickups *s, qa_actor_id actor);
void qa_compat_q2_native_mod_pickups_assert_idle(const qa_compat_q2_native_mod_pickups *s);
void qa_compat_q2_native_mod_pickups_release(qa_compat_q2_native_mod_pickups *s, qa_actor_id actor);
void qa_compat_q2_native_mod_pickups_close(qa_compat_q2_native_mod_pickups *s);
void qa_compat_q2_native_mod_pickups_destroy(qa_compat_q2_native_mod_pickups *s);
#endif
