/* Port of src/compat/q2/native-mod-combat.ts. Original damage and callback
 * bodies remain guest-owned; only their shared boundaries compose.
 * GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_COMBAT_H
#define QA_COMPAT_Q2_NATIVE_MOD_COMBAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_mod_armor.h"
#include "qa/compat/q2/native_mod_deferred.h"
#include "qa/compat/q2/native_mod_host_api.h"
#include "qa/compat/q2/native_mod_invocations.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/foundation/support.h"
#include "qa/world/session/mods.h"

/* TS NativeModCombatCalls. Closures run in invoke(invoke_ctx); their results
 * are left in that context. owner's false is TS null. */
typedef struct qa_compat_q2_native_mod_combat_calls {
  void *ctx;
  bool (*eligible)(void *ctx, qa_actor_id actor);
  qa_actor_id (*actor)(void *ctx, qa_guest_address address);
  qa_guest_address (*address)(void *ctx, qa_actor_id actor);
  bool (*owner)(void *ctx, double slot, qa_owned_actor *out);
  double (*scalar)(void *ctx, qa_guest_address base, qa_native_mod_scalar_field field, bool has_value, double value);
  void (*transfer)(void *ctx, void (*invoke)(void *), void *invoke_ctx);
  /* actor NULL is TS null. */
  qa_compat_q2_native_mod_invocation_kind (*source_execution)(void *ctx, const qa_actor_id *actor, void (*invoke)(void *), void *invoke_ctx);
  void (*synchronize)(void *ctx);
} qa_compat_q2_native_mod_combat_calls;

typedef struct qa_compat_q2_native_mod_combat_state qa_compat_q2_native_mod_combat_state;
qa_compat_q2_native_mod_combat_state *qa_compat_q2_native_mod_combat_create(const qa_native_mod_source_actors *definition, const qa_native_mod_declaration *declaration,
    const qa_compat_q2_native_mod_host *host, const qa_mod_host_services *services, qa_provider_id instance, qa_compat_q2_native_mod_combat_calls calls);
/* TS bind(slot, actor): rebinds combat state and returns the actor callbacks,
 * whose ctx lives until close. */
qa_actor_callbacks qa_compat_q2_native_mod_combat_bind(qa_compat_q2_native_mod_combat_state *s, double slot, qa_owned_actor actor);
void qa_compat_q2_native_mod_combat_validate(qa_compat_q2_native_mod_combat_state *s);
void qa_compat_q2_native_mod_combat_release(qa_compat_q2_native_mod_combat_state *s, qa_actor_id actor);
/* Malloc'd rows; NULL with count 0 without deferred storage. */
qa_compat_q2_saved_native_deferred_damage *qa_compat_q2_native_mod_combat_checkpoint(qa_compat_q2_native_mod_combat_state *s, size_t *out_count);
void qa_compat_q2_native_mod_combat_validate_saved(qa_compat_q2_native_mod_combat_state *s, const qa_compat_q2_saved_native_deferred_damage *records, size_t count);
void qa_compat_q2_native_mod_combat_restore(qa_compat_q2_native_mod_combat_state *s, const qa_compat_q2_saved_native_deferred_damage *records, size_t count);
void qa_compat_q2_native_mod_combat_suspend(qa_compat_q2_native_mod_combat_state *s, bool value);
void qa_compat_q2_native_mod_combat_close(qa_compat_q2_native_mod_combat_state *s);
/* Closes when needed, then frees. */
void qa_compat_q2_native_mod_combat_destroy(qa_compat_q2_native_mod_combat_state *s);
#endif
