/* Port of src/content/q3/equipment/weapon-profile.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_EQUIPMENT_WEAPON_PROFILE_H
#define QA_CONTENT_Q3_EQUIPMENT_WEAPON_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/equipment/lrctf_grapple_profile.h"
#include "qa/content/q3/equipment/pickup_profile.h"
#include "qa/content/q3/guest_items.h"
#include "qa/contracts/qvm_mod_items.h"
#include "qa/contracts/source_items.h"
#include "qa/contracts/source_match.h"
/* Mirror of QvmEquipmentMovementProfile (TS owner:
 * src/compat/qvm/game-equipment-movement.ts). */
typedef struct qa_q3_equipment_qvm_equipment_movement_profile {
  double move, slice, duck, movement_global;
  double locomotion_entry, locomotion_join;
  double mins, maxs;
  bool has_body_trace; double body_trace_callback, body_trace_mask;
} qa_q3_equipment_qvm_equipment_movement_profile;
/* Mirror of QvmWeaponDispatcherDefinition (TS owner:
 * src/compat/qvm/mod-weapon-stage.ts). */
typedef struct qa_q3_equipment_qvm_weapon_dispatcher_definition {
  qa_qvm_weapon_dispatcher dispatcher;
  const qa_qvm_weapon_predicate *predicates; size_t predicate_count;
  const qa_qvm_item_test *settled; size_t settled_count;
  qa_qvm_weapon_selection selection;
  qa_qvm_weapon_request request;
} qa_q3_equipment_qvm_weapon_dispatcher_definition;
/* Mirror of QvmPrimaryWeaponProfile (TS owner: src/compat/qvm/game-weapons.ts). */
typedef struct qa_q3_equipment_qvm_primary_weapon_profile {
  qa_module_identity module;
  bool has_match; qa_source_primary_match match;
  const char *abi_profile;
  qa_q3_equipment_qvm_equipment_movement_profile equipment_movement;
  double entity_stride, client_stride, client_pointer;
  qa_q3_equipment_qvm_weapon_dispatcher_definition stage;
  double damage_factor_entry, damage_factor_result, damage_factor_stop_entry, damage_factor_stop_join;
  const qa_source_equipment_context *equipment_contexts; size_t equipment_context_count;
  qa_q3_equipment_qvm_region_evaluation delay;
  double delay_player_movement_global, delay_player_player_offset;
  double teleport_entry, teleport_spawn, teleport_view;
  qa_q3_equipment_qvm_region_evaluation teleport_region, teleport_objectives;
  double max_health, persistent_max_health;
  double availability_movement_type;
  const double *availability_excluded; size_t availability_excluded_count;
  double availability_health, availability_team, availability_spectator_team, availability_flags, availability_respawn_flag;
  double powerup_quad, powerup_haste, powerup_flight;
  double torso_animation_entry, torso_animation_attack, torso_animation_melee;
  double water_level_entity_offset, water_level_movement_offset;
  double drop_entry, drop_argument, drop_weapon, drop_ammo, drop_region_entry, drop_region_join;
  double give_entry, give_argument, give_weapons, give_ammo;
  double give_named_entry, give_named_join, give_named_name, give_named_item;
  qa_qvm_weapon_selection_value *owned_selection_values;
} qa_q3_equipment_qvm_primary_weapon_profile;
bool qa_q3_primary_weapon_profile(const qa_q3_equipment_qvm_artifact *artifact,
    const qa_q3_guest_weapon *weapons, size_t weapon_count,
    qa_q3_equipment_qvm_primary_weapon_profile *out);
void qa_q3_equipment_qvm_primary_weapon_profile_destroy(qa_q3_equipment_qvm_primary_weapon_profile *profile);
#endif
