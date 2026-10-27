/* Port of src/compat/q2/classic/combat-binding.ts. GPL-2.0-or-later.
 * TS RemovedNativeDamage (a throw through the guest call) becomes a
 * recorded flag checked after invoke returns, restoring the saved
 * processor snapshot through the runner mirror. */
#ifndef QA_COMPAT_Q2_CLASSIC_COMBAT_BINDING_H
#define QA_COMPAT_Q2_CLASSIC_COMBAT_BINDING_H
#include <stdbool.h>
#include "qa/compat/q2/classic/combat_profile.h"
#include "qa/compat/q2/classic/host.h"
#include "qa/contracts/world.h"

/* TS lowerNativeCombatArguments (native-combat-call.ts, landed
 * without this helper): fills out (capacity >= call arguments). */
size_t qa_compat_q2_lower_native_combat_arguments(const qa_compat_q2_native_combat_call *call, qa_compat_q2_native_combat_operation operation,
                                                  const qa_guest_call_value *values, size_t count, const qa_compat_q2_memory *memory,
                                                  bool has_image, qa_guest_address image, const qa_guest_call_value *original, size_t original_count,
                                                  qa_guest_call_value *out);

typedef struct qa_compat_q2_classic_combat_bindings qa_compat_q2_classic_combat_bindings;
/* declared NULL selects by digest; NULL return is the TS null. */
qa_compat_q2_classic_combat_bindings *qa_compat_q2_classic_combat_bindings_create(
    qa_compat_q2_classic_guest_host *host, qa_guest_address image,
    const qa_compat_q2_classic_combat_profile *declared, bool has_declared);
void qa_compat_q2_classic_combat_bindings_close(qa_compat_q2_classic_combat_bindings *b);
void qa_compat_q2_classic_combat_bindings_destroy(qa_compat_q2_classic_combat_bindings *b);
bool qa_compat_q2_classic_combat_bindings_notarget(qa_compat_q2_classic_combat_bindings *b, qa_raw_entity_view view);
/* Binds into engine.combat (TS bind returns undefined). */
void qa_compat_q2_classic_combat_bindings_bind(qa_compat_q2_classic_combat_bindings *b, qa_raw_entity_view view, qa_owned_actor actor);
qa_raw_entity_view qa_compat_q2_classic_combat_bindings_spawn_proxy_edict(qa_compat_q2_classic_combat_bindings *b);
void qa_compat_q2_classic_combat_bindings_free_proxy_edict(qa_compat_q2_classic_combat_bindings *b, qa_raw_entity_view view);
typedef struct qa_compat_q2_proxy_body { qa_vec3 origin, angles, velocity; qa_bounds bounds; } qa_compat_q2_proxy_body;
typedef struct qa_compat_q2_proxy_combat { double health; bool can_take_damage; double mass; } qa_compat_q2_proxy_combat;
void qa_compat_q2_classic_combat_bindings_sync_proxy_edict(qa_compat_q2_classic_combat_bindings *b, qa_raw_entity_view view,
                                                           qa_compat_q2_proxy_body body, const qa_compat_q2_proxy_combat *combat);
double qa_compat_q2_classic_combat_bindings_proxy_health(qa_compat_q2_classic_combat_bindings *b, qa_raw_entity_view view);
qa_vec3 qa_compat_q2_classic_combat_bindings_proxy_velocity(qa_compat_q2_classic_combat_bindings *b, qa_raw_entity_view view);
#endif
