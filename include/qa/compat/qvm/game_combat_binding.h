/* Port of src/compat/qvm/game-combat-binding.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_GAME_COMBAT_BINDING_H
#define QA_COMPAT_QVM_GAME_COMBAT_BINDING_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/compat/qvm/game_combat.h"
#include "qa/compat/qvm/game_combat_scope.h"
#include "qa/contracts/qvm_combat.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"

typedef struct qa_qvm_primary_combat_reactions {
  double flags, pain, die;
  qa_qvm_reaction_call pain_call, die_call;
} qa_qvm_primary_combat_reactions;

typedef struct qa_qvm_primary_combat_state {
  double health_stat;
  struct {
    double persistent_stat;
    const qa_qvm_combat_team *values;
    size_t value_count;
  } team;
  struct { double notarget, invulnerable, no_knockback; } flags;
  qa_qvm_combat_mass mass;
} qa_qvm_primary_combat_state;

typedef struct qa_qvm_primary_combat_profile {
  qa_qvm_game_combat_definition base;
  double field_client;
  qa_qvm_game_armor_definition armor;
  qa_qvm_primary_combat_reactions reactions;
  double grapple_damage_method;
  qa_qvm_primary_combat_state state;
  qa_qvm_damage_flags damage_flags;
} qa_qvm_primary_combat_profile;

typedef struct qa_qvm_combat_source {
  qa_session_actor_registry *actors;
  void *ctx;
  /* False is the TS null actor. */
  bool (*actor)(void *ctx, double slot, qa_owned_actor *out);
  /* TS provenance minus attacker/inflictor/cause: fills those fields of out. */
  void (*provenance)(void *ctx, const qa_actor_id *attacker, const qa_actor_id *inflictor,
                     qa_actor_id target, qa_attack_provenance *out);
  void (*after_free)(void *ctx, double pointer, qa_qvmy_function_call *call); /* optional */
} qa_qvm_combat_source;

typedef struct qa_qvm_combat_bindings_options {
  qa_qvm_game *game;
  const qa_resolved_qvm_artifact *artifact;
  qa_qvm_primary_combat_profile definition;
  qa_shared_body_table *bodies;
  qa_gameplay_authority *combat;
  const qa_qvm_combat_source *source; /* NULL is TS undefined */
  void *ctx;
  bool (*slot)(void *ctx, qa_actor_id actor, double *out);
} qa_qvm_combat_bindings_options;

typedef struct qa_qvm_combat_bindings qa_qvm_combat_bindings;

/* NULL only on OOM. */
qa_qvm_combat_bindings *qa_qvm_combat_bindings_create(qa_qvm_combat_bindings_options options);
void qa_qvm_combat_bindings_destroy(qa_qvm_combat_bindings *bindings);
double qa_qvm_combat_bindings_spawn_proxy_entity(qa_qvm_combat_bindings *bindings);
void qa_qvm_combat_bindings_bind_proxy(qa_qvm_combat_bindings *bindings, double slot,
                                       qa_owned_actor actor);
void qa_qvm_combat_bindings_sync_proxy_entity(qa_qvm_combat_bindings *bindings, double slot,
                                              qa_body_state body);
void qa_qvm_combat_bindings_close(qa_qvm_combat_bindings *bindings);
/* False (with *out unset) is the TS null. */
bool qa_qvm_combat_bindings_notarget(qa_qvm_combat_bindings *bindings, qa_actor_id actor, bool *out);
qa_armor_state qa_qvm_combat_bindings_normalize_legacy_armor(qa_qvm_combat_bindings *bindings,
                                                             qa_actor_id actor,
                                                             qa_armor_state saved);
void qa_qvm_combat_bindings_admit(qa_qvm_combat_bindings *bindings, qa_owned_actor actor);

#endif
