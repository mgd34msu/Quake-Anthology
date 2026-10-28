/* Port of src/compat/q2/native-mod-deferred.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_DEFERRED_H
#define QA_COMPAT_Q2_NATIVE_MOD_DEFERRED_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/persistence/q2_foundation.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"

typedef struct qa_compat_q2_saved_native_deferred_damage {
  qa_saved_actor_id target; qa_persist_q2_attack_checkpoint attack;
  double amount, knockback_req; qa_vec3 point_req, direction, normal; const char *delivery;
  double blood, knockback; qa_vec3 point; double mod[3]; bool has_attacker, has_inflictor; double attacker_slot, inflictor_slot;
} qa_compat_q2_saved_native_deferred_damage;
qa_compat_q2_saved_native_deferred_damage qa_compat_q2_read_native_deferred_damage(const qa_save_reader *reader);
typedef struct qa_compat_q2_native_mod_deferred_damage_state { void *host_ctx; size_t pending_count; } qa_compat_q2_native_mod_deferred_damage_state;
void qa_compat_q2_native_mod_deferred_damage_state_clear(qa_compat_q2_native_mod_deferred_damage_state *s);
#endif
