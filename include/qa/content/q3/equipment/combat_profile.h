/* Port of src/content/q3/equipment/combat-profile.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_EQUIPMENT_COMBAT_PROFILE_H
#define QA_CONTENT_Q3_EQUIPMENT_COMBAT_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/equipment/lrctf_grapple_profile.h"
#include "qa/contracts/qvm_combat.h"
/* Mirror of QvmGameArmorDefinition (TS owner: src/compat/qvm/game-combat.ts). */
typedef struct qa_q3_equipment_armor_when { double offset; const char *comparison; double value; } qa_q3_equipment_armor_when;
typedef struct qa_q3_equipment_armor_tier { double tier, protection; } qa_q3_equipment_armor_tier;
typedef struct qa_q3_equipment_qvm_game_armor_definition {
  double check_armor; qa_qvm_combat_call call; double points_stat, protection;
  bool has_tiers; /* false is TS tiers:null */
  double tiers_stat;
  const qa_q3_equipment_armor_when *when_any; size_t when_any_count;
  const qa_q3_equipment_armor_tier *values; size_t value_count;
  double fallback;
} qa_q3_equipment_qvm_game_armor_definition;
/* Mirror of QvmPrimaryCombatProfile (TS owner:
 * src/compat/qvm/game-combat-binding.ts); the grapple profile spread
 * supplies module/abi/strides/fields/callbacks/grappleDamageMethod. */
typedef struct qa_q3_equipment_qvm_primary_combat_profile {
  struct qa_qvm_grapple_definition grapple;
  qa_qvm_combat_call damage_call;
  qa_q3_equipment_qvm_game_armor_definition armor;
  struct { double flags, pain, die; qa_qvm_reaction_call pain_call, die_call; } reactions;
  struct {
    double health_stat;
    double team_persistent_stat;
    const qa_qvm_combat_team *team_values; size_t team_value_count;
    double flag_notarget, flag_invulnerable, flag_no_knockback;
    qa_qvm_combat_mass mass;
  } state;
  qa_qvm_damage_flags damage_flags;
} qa_q3_equipment_qvm_primary_combat_profile;
bool qa_q3_native_combat_profile(const qa_q3_equipment_qvm_artifact *artifact,
    qa_q3_equipment_qvm_primary_combat_profile *out);
#endif
