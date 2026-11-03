/* Port of src/compat/q2/native-mod-deferred.ts. The original accumulator owns
 * batching; these records retain the originating canonical attack. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_DEFERRED_H
#define QA_COMPAT_Q2_NATIVE_MOD_DEFERRED_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_mod_armor.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/value.h"
#include "qa/world/session/mods.h"

typedef struct qa_compat_q2_saved_native_deferred_damage {
  qa_saved_actor_id target; qa_q2_attack_checkpoint attack;
  double amount, knockback_req; qa_vec3 point_req, direction, normal; const char *delivery;
  double blood, knockback; qa_vec3 point; double mod[3]; bool has_attacker, has_inflictor; double attacker_slot, inflictor_slot;
} qa_compat_q2_saved_native_deferred_damage;
qa_compat_q2_saved_native_deferred_damage qa_compat_q2_read_native_deferred_damage(const qa_save_reader *reader);
/* The checkpoint row as a checkpoint value tree (owned). */
qa_save_value *qa_compat_q2_native_deferred_damage_value(const qa_compat_q2_saved_native_deferred_damage *record);

/* The NativeModCombatCalls pick this state reads. */
typedef struct qa_compat_q2_native_deferred_calls {
  void *ctx;
  bool (*owner)(void *ctx, double slot, qa_owned_actor *out); /* false is TS null */
  double (*scalar)(void *ctx, qa_guest_address base, qa_native_mod_scalar_field field, bool has_value, double value);
} qa_compat_q2_native_deferred_calls;
/* False is TS null; *out is valid for the call. */
typedef bool (*qa_compat_q2_native_deferred_current_fn)(void *ctx, qa_damage_request *out);
typedef struct qa_compat_q2_native_deferred_pending qa_compat_q2_native_deferred_pending;
typedef struct qa_compat_q2_native_mod_deferred_damage_state {
  const qa_native_mod_deferred_damage *definition; qa_native_mod_scalar_field health;
  const qa_compat_q2_native_mod_host *host; const qa_mod_host_services *services;
  qa_compat_q2_native_deferred_calls calls;
  void *current_ctx; qa_compat_q2_native_deferred_current_fn current;
  qa_compat_q2_native_deferred_pending **pending; size_t pending_count, pending_capacity;
  qa_compat_q2_native_deferred_pending **processing; size_t processing_count, processing_capacity;
} qa_compat_q2_native_mod_deferred_damage_state;
void qa_compat_q2_native_mod_deferred_damage_state_init(qa_compat_q2_native_mod_deferred_damage_state *s, const qa_native_mod_deferred_damage *definition, qa_native_mod_scalar_field health,
    const qa_compat_q2_native_mod_host *host, const qa_mod_host_services *services, qa_compat_q2_native_deferred_calls calls, void *current_ctx, qa_compat_q2_native_deferred_current_fn current);
void qa_compat_q2_native_mod_deferred_damage_state_destroy(qa_compat_q2_native_mod_deferred_damage_state *s);
void qa_compat_q2_native_mod_deferred_damage_state_changed(qa_compat_q2_native_mod_deferred_damage_state *s, double slot, qa_compat_q2_written_range range);
/* False is TS null. *out's pointers stay valid while the frame is processing. */
bool qa_compat_q2_native_mod_deferred_damage_state_attack(qa_compat_q2_native_mod_deferred_damage_state *s, qa_actor_id actor, qa_damage_request *out);
/* TS process(actor, slot, original). */
void qa_compat_q2_native_mod_deferred_damage_state_process(qa_compat_q2_native_mod_deferred_damage_state *s, qa_actor_id actor, double slot, void *original_ctx, void (*original)(void *));
void qa_compat_q2_native_mod_deferred_damage_state_release(qa_compat_q2_native_mod_deferred_damage_state *s, qa_actor_id actor);
/* Malloc'd rows; caller frees the array. */
qa_compat_q2_saved_native_deferred_damage *qa_compat_q2_native_mod_deferred_damage_state_checkpoint(qa_compat_q2_native_mod_deferred_damage_state *s, size_t *out_count);
void qa_compat_q2_native_mod_deferred_damage_state_validate_saved(qa_compat_q2_native_mod_deferred_damage_state *s, const qa_compat_q2_saved_native_deferred_damage *records, size_t count);
void qa_compat_q2_native_mod_deferred_damage_state_restore(qa_compat_q2_native_mod_deferred_damage_state *s, const qa_compat_q2_saved_native_deferred_damage *records, size_t count);
void qa_compat_q2_native_mod_deferred_damage_state_clear(qa_compat_q2_native_mod_deferred_damage_state *s);
#endif
