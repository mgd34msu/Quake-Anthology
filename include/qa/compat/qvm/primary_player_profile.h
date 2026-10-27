/*
 * Port of src/compat/qvm/primary-player-profile.ts. Mirrored types
 * (parallel slice): qa_qvmy_input_definition mirrors
 * QvmInputDefinition (TS owner src/compat/qvm/game-input.ts),
 * qa_qvmy_primary_weapon_profile mirrors QvmPrimaryWeaponProfile
 * (TS owner src/compat/qvm/game-weapons.ts), and
 * qa_qvmy_primary_combat_profile mirrors QvmPrimaryCombatProfile
 * (TS owner src/compat/qvm/game-combat-binding.ts). The combat call
 * validators are mirrored from src/compat/qvm/game-combat.ts. Nested
 * declaration objects flatten into the profile structs; lists are
 * owned arrays freed by the *_free functions.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_PRIMARY_PLAYER_PROFILE_H
#define QA_COMPAT_QVM_PRIMARY_PLAYER_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/module.h"
#include "qa/content/mods/match.h"
#include "qa/contracts/qvm_combat.h"
#include "qa/contracts/qvm_mod_items.h"
#include "qa/persistence/value.h"

typedef struct qa_qvmy_input_definition {
  qa_module_identity module;
  qa_qvm_abi_profile abi_profile;
  int64_t entity_stride;
  int64_t client_stride;
  bool has_movement_modes;
  int64_t mode_normal, mode_noclip, mode_freeze;
  int64_t client_pointer;
  int64_t *intermission; /* owned */
  size_t intermission_count;
  int64_t entry_client_think, entry_run_client, entry_client_spawn, entry_move, entry_slice;
} qa_qvmy_input_definition;

typedef struct qa_qvmy_catalog_entry {
  int64_t weapon;
  qa_item_id item;
} qa_qvmy_catalog_entry;

typedef struct qa_qvmy_equipment_context {
  qa_provider_id provider;
  bool has_item;
  qa_item_id item;
} qa_qvmy_equipment_context;

typedef struct qa_qvmy_primary_region {
  int64_t entry;
  int64_t join;
} qa_qvmy_primary_region;

typedef struct qa_qvmy_primary_weapon_profile {
  qa_module_identity module;
  qa_qvm_abi_profile abi_profile;
  int64_t entity_stride;
  int64_t client_stride;
  bool has_match;
  qa_source_primary_match match;
  int64_t client_pointer;
  int64_t max_health;
  int64_t persistent_max_health;
  /* stage (continuation unused by this declaration form) */
  int64_t dispatcher_entry;
  qa_qvm_weapon_actor dispatcher_actor;
  qa_qvm_weapon_predicate *predicates; /* owned */
  size_t predicate_count;
  qa_qvm_item_test *settled; /* owned */
  size_t settled_count;
  qa_qvm_item_field selection_field;
  qa_qvm_weapon_selection_value *selection_values; /* owned */
  size_t selection_value_count;
  int64_t request_entry;
  int64_t request_argument;
  qa_qvm_item_test *request_accepted; /* owned */
  size_t request_accepted_count;
  /* equipment movement */
  bool has_body_trace;
  int64_t body_trace_callback, body_trace_mask;
  int64_t move_entry, slice_entry, duck_entry;
  int64_t movement_global;
  qa_qvmy_primary_region locomotion;
  int64_t mins_offset, maxs_offset;
  /* availability / powerups / torso */
  int64_t availability_movement_type;
  int64_t *availability_excluded; /* owned */
  size_t availability_excluded_count;
  int64_t availability_health, availability_team, availability_spectator_team;
  int64_t availability_flags, availability_respawn_flag;
  int64_t powerup_quad, powerup_haste, powerup_flight;
  int64_t torso_entry, torso_attack, torso_melee;
  int64_t water_entity_offset, water_movement_offset;
  int64_t damage_entry, damage_result;
  qa_qvmy_primary_region damage_stop;
  qa_qvmy_equipment_context *equipment_contexts; /* owned */
  size_t equipment_context_count;
  qa_qvm_region_evaluation delay;
  int64_t *delay_inputs; /* owned */
  int64_t delay_movement_global, delay_player_offset;
  int64_t teleport_entry;
  qa_qvm_region_evaluation teleport_region;
  int64_t *teleport_region_inputs; /* owned */
  qa_qvm_region_evaluation teleport_objectives;
  int64_t *teleport_objectives_inputs; /* owned */
  int64_t teleport_spawn, teleport_view;
  int64_t drop_entry, drop_argument, drop_weapon;
  bool drop_ammo_inventory;
  int64_t drop_ammo_offset;
  qa_qvmy_primary_region drop_region;
  int64_t give_entry, give_argument, give_weapons, give_ammo;
  qa_qvmy_primary_region give_named_region;
  int64_t give_named_name, give_named_item;
} qa_qvmy_primary_weapon_profile;

typedef struct qa_qvmy_team_value {
  int64_t value;
  const char *team;
} qa_qvmy_team_value;

typedef struct qa_qvmy_armor_when {
  int64_t offset;
  const char *comparison;
  int64_t value;
} qa_qvmy_armor_when;

typedef struct qa_qvmy_armor_tier_value {
  int64_t tier;
  double protection;
} qa_qvmy_armor_tier_value;

typedef struct qa_qvmy_primary_combat_profile {
  qa_module_identity module;
  qa_qvm_abi_profile abi_profile;
  int64_t entity_stride;
  int64_t client_stride;
  qa_qvm_combat_role_index damage_roles[8];
  qa_qvm_combat_extra *damage_extras; /* owned */
  size_t damage_extra_count;
  int64_t state_health_stat;
  int64_t team_persistent_stat;
  qa_qvmy_team_value *team_values; /* owned */
  size_t team_value_count;
  int64_t flag_notarget, flag_invulnerable, flag_no_knockback;
  bool mass_is_constant;
  double mass_value;
  int64_t mass_offset;
  const char *mass_storage;
  qa_qvm_damage_flags damage_flags;
  int64_t field_inuse, field_health, field_takedamage, field_parent, field_client;
  int64_t callback_allocate, callback_free, callback_damage;
  int64_t reaction_flags, reaction_pain, reaction_die;
  qa_qvm_reaction_call pain_call, die_call;
  int64_t grapple_damage_method;
  int64_t armor_check_armor;
  qa_qvm_combat_role_index armor_roles[3];
  qa_qvm_combat_extra *armor_extras; /* owned */
  size_t armor_extra_count;
  int64_t armor_points_stat;
  double armor_protection;
  bool has_tiers;
  int64_t tier_stat;
  qa_qvmy_armor_when *tier_when; /* owned */
  size_t tier_when_count;
  qa_qvmy_armor_tier_value *tier_values; /* owned */
  size_t tier_value_count;
  double tier_fallback;
} qa_qvmy_primary_combat_profile;

void qa_qvm_read_primary_input(const qa_save_reader *reader, const qa_qvmy_artifact *artifact,
                               qa_qvmy_input_definition *out);
void qa_qvm_read_primary_weapons(const qa_save_reader *reader, const qa_qvmy_artifact *artifact,
                                 const qa_qvmy_catalog_entry *catalog, size_t catalog_count, bool has_catalog,
                                 qa_qvmy_primary_weapon_profile *out);
void qa_qvm_read_primary_combat(const qa_save_reader *reader, const qa_qvmy_artifact *artifact,
                                qa_qvmy_primary_combat_profile *out);
void qa_qvmy_input_definition_free(qa_qvmy_input_definition *definition);
void qa_qvmy_primary_weapon_profile_free(qa_qvmy_primary_weapon_profile *profile);
void qa_qvmy_primary_combat_profile_free(qa_qvmy_primary_combat_profile *profile);

#endif
