/* Port of src/compat/qvm/game-combat.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_GAME_COMBAT_H
#define QA_COMPAT_QVM_GAME_COMBAT_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/game.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/math.h"
#include "qa/contracts/qvm_combat.h"
#include "qa/contracts/world.h"

typedef struct qa_qvm_combat_role_value {
  const char *role;
  double value;
} qa_qvm_combat_role_value;

/* TS qvmCombatWords: writes role_count+extra_count words into out. */
void qa_qvm_combat_words(qa_qvm_combat_call call, const qa_qvm_combat_role_value *values,
                         size_t value_count, double *out);
size_t qa_qvm_combat_word_count(qa_qvm_combat_call call);

double qa_qvm_canonical_damage_flags(qa_qvm_damage_flags masks, double flags);
double qa_qvm_source_damage_flags(qa_qvm_damage_flags masks, qa_damage_request request,
                                  double original);
void qa_qvm_validate_combat_positions(const double *positions, size_t position_count, double words);
void qa_qvm_validate_combat_call(qa_qvm_combat_call call, double data_bytes);

typedef struct qa_qvm_armor_tier_test {
  double offset;
  const char *comparison; /* "equal" | "not-equal" */
  double value;
} qa_qvm_armor_tier_test;
typedef struct qa_qvm_armor_tier_value { double tier, protection; } qa_qvm_armor_tier_value;
typedef struct qa_qvm_armor_tiers {
  double stat;
  const qa_qvm_armor_tier_test *when_any;
  size_t when_any_count;
  const qa_qvm_armor_tier_value *values;
  size_t value_count;
  double fallback;
} qa_qvm_armor_tiers;

typedef struct qa_qvm_game_armor_definition {
  double check_armor;
  qa_qvm_combat_call call;
  double points_stat, protection;
  bool has_tiers;
  qa_qvm_armor_tiers tiers;
} qa_qvm_game_armor_definition;

typedef struct qa_qvm_game_combat_definition {
  qa_qvm_combat_call damage_call;
  qa_module_identity module;
  qa_qvm_abi_profile abi_profile;
  double entity_stride, client_stride;
  double field_inuse, field_health, field_takedamage, field_parent;
  double callback_allocate, callback_free, callback_damage;
} qa_qvm_game_combat_definition;

typedef enum qa_qvm_game_inflictor_kind {
  QA_QVM_INFLICTOR_NONE,
  QA_QVM_INFLICTOR_ENTITY,
  QA_QVM_INFLICTOR_FOREIGN
} qa_qvm_game_inflictor_kind;

typedef struct qa_qvm_game_damage {
  double target;
  bool has_attacker;
  double attacker;
  qa_qvm_game_inflictor_kind inflictor_kind;
  double inflictor_slot;
  qa_body_state inflictor_body;
  qa_vec3 direction, point;
  double amount, flags, method;
} qa_qvm_game_damage;

typedef struct qa_qvm_game_damage_state {
  double health;
  bool damageable;
} qa_qvm_game_damage_state;

typedef struct qa_qvm_game_combat {
  qa_qvm_game *game; /* borrowed */
  qa_qvm_game_combat_definition definition;
  int64_t scratch;
  double *arguments; /* owned */
  size_t argument_count;
} qa_qvm_game_combat;

/* False only on OOM. */
bool qa_qvm_game_combat_init(qa_qvm_game_combat *combat, qa_qvm_game *game,
                             const qa_resolved_qvm_artifact *artifact,
                             qa_qvm_game_combat_definition definition);
void qa_qvm_game_combat_destroy(qa_qvm_game_combat *combat);
/* False is the TS null state. */
bool qa_qvm_game_combat_state(qa_qvm_game_combat *combat, double slot,
                              qa_qvm_game_damage_state *out);
typedef struct qa_qvm_damage_invoke {
  void *ctx;
  void (*call)(void *ctx, const double *words, size_t count);
} qa_qvm_damage_invoke;
/* invoke NULL uses the TS default (module.call at callbacks.damage). */
void qa_qvm_game_combat_damage(qa_qvm_game_combat *combat, qa_qvm_game_damage hit,
                               const qa_qvm_damage_invoke *invoke);

#endif
