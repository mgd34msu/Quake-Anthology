/*
 * Port of src/compat/qvm/mod-actors.ts. TS plain Error throws abort
 * through qa_range_error with the same messages (docs/PORTING.md).
 * Accommodations:
 *  - The Options closures (pointer/actor/invoke/scratch) are fn + ctx;
 *    the owned map is the owned_has predicate. pointer takes a NULL
 *    actor pointer for the TS null actor; actor returns false for null.
 *  - scratch<T> is used with T = void (touch) and number (damage);
 *    the C form always returns the execute callback's int32 result.
 *  - services is the landed qa_mod_host_services; only the members
 *    mod-actors.ts reads (callbacks, combat, bodies, actors, match,
 *    time, damage_context) are touched. damage_context supplies only
 *    the five provenance fields its landed signature writes; the
 *    remaining AttackProvenance fields stay at their zero values,
 *    and a NULL damage_context is the TS undefined context error.
 *  - DamageRequest attacker/inflictor pointers are re-pointed at
 *    frame-owned storage when a request enters a damage frame.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MOD_ACTORS_H
#define QA_COMPAT_QVM_MOD_ACTORS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/module.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/world/session/mods.h"

/* artifact is QvmModuleOptions["artifact"] (module.h mirror). */
void qa_qvm_validate_mod_actors(const qa_qvmy_artifact *artifact,
                                const qa_qvm_mod_callback_declaration *declaration);

/* One invoke() input (TS ReadonlyMap<ModCallbackInput, ModRuntimeValue>). */
typedef struct qa_qvm_mod_actor_input {
  qa_mod_callback_input name;
  qa_mod_runtime_value value;
} qa_qvm_mod_actor_input;

typedef int32_t (*qa_qvm_mod_actor_scratch_fn)(void *ctx, int32_t address);

/* TS Options. All pointers are borrowed. */
typedef struct qa_qvm_mod_actors_options {
  qa_qvm_module *module;
  const qa_qvm_mod_callback_declaration *declaration;
  const qa_mod_host_services *services;
  void *ctx;
  bool (*owned_has)(void *ctx, qa_actor_id actor);
  int32_t (*pointer)(void *ctx, const qa_actor_id *actor); /* NULL is TS null */
  bool (*actor)(void *ctx, int32_t pointer, qa_actor_id *out); /* false is TS null */
  int32_t (*invoke)(void *ctx, const qa_qvm_mod_source_call *call,
                    const qa_qvm_mod_actor_input *inputs, size_t input_count);
  int32_t (*scratch)(void *ctx, int64_t size, qa_qvm_mod_actor_scratch_fn execute,
                     void *execute_ctx);
} qa_qvm_mod_actors_options;

typedef struct qa_qvm_mod_actors {
  qa_qvm_mod_actors_options options;
  qa_qvm_mod_combat_calls calls;
  qa_qvm_damage_flags flags;
  int64_t call_bytes_damage, call_bytes_touch, call_bytes_use, call_bytes_pain, call_bytes_die;
  bool has_combat_hook;
  size_t combat_hook;
  size_t resolver_id;
  int64_t release_id;
  void *pointers; /* opaque: pointer -> owned actor */
  size_t pointer_count, pointer_capacity;
  void *actor_pointers; /* opaque: actor id -> pointer */
  size_t actor_pointer_count, actor_pointer_capacity;
  void *damage_frames; /* opaque: stack of owned frames */
  size_t damage_frame_count, damage_frame_capacity;
  void *bindings; /* opaque: per-actor combat binding contexts */
  size_t binding_count, binding_capacity;
} qa_qvm_mod_actors;

/* TS constructor: aborts when shared actor callbacks are missing. */
void qa_qvm_mod_actors_init(qa_qvm_mod_actors *actors, const qa_qvm_mod_actors_options *options);
void qa_qvm_mod_actors_destroy(qa_qvm_mod_actors *actors);
void qa_qvm_mod_actors_admit(qa_qvm_mod_actors *actors, qa_owned_actor actor);
void qa_qvm_mod_actors_clear_actors(qa_qvm_mod_actors *actors);
void qa_qvm_mod_actors_before_release(qa_qvm_mod_actors *actors, qa_actor_id actor);
void qa_qvm_mod_actors_close(qa_qvm_mod_actors *actors);

#endif
