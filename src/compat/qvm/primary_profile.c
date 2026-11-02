/* See include/qa/compat/qvm/primary_profile.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/primary_profile.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/equipment/combat_profile.h"
#include "qa/content/q3/equipment/inventory_profile.h"
#include "qa/content/q3/equipment/pickup_profile.h"
#include "qa/content/q3/equipment/weapon_profile.h"
#include "qa/content/q3/input_profile.h"
#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *result = malloc(size == 0 ? 1 : size);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  return result;
}

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("QVM memory allocation failed");
  return out;
}

static qa_q3_equipment_qvm_artifact equipment_artifact(const qa_qvmy_artifact *artifact) {
  qa_q3_equipment_qvm_artifact result;
  memset(&result, 0, sizeof result);
  result.module = artifact->module;
  result.role = artifact->role;
  result.abi_profile = artifact->has_abi_profile ? artifact->abi_profile : NULL;
  result.image.initialized_data = artifact->initialized_data;
  result.image.initialized_data_length = artifact->initialized_data_length;
  result.image.allocated_data_length = (double)artifact->allocated_data_length;
  result.image.data_length = (double)artifact->data_length;
  result.image.literal_length = (double)artifact->literal_length;
  result.image.bss_length = (double)artifact->bss_length;
  return result;
}

void qa_qvm_builtin_primary_profile(const qa_qvmy_artifact *artifact,
                                    const qa_q3_guest_weapon *catalog, size_t catalog_count,
                                    qa_qvm_primary_profile *out) {
  memset(out, 0, sizeof *out);
  qa_q3_equipment_qvm_artifact equipment = equipment_artifact(artifact);
  qa_q3_qvm_input_definition input;
  if (qa_q3_input_profile(&equipment, &input)) {
    out->input = xmalloc(sizeof *out->input);
    memset(out->input, 0, sizeof *out->input);
    out->input->module = input.module;
    out->input->abi_profile = artifact->has_abi_profile ? artifact->abi_profile : "q3-modern";
    out->input->entity_stride = (int64_t)input.entity_stride;
    out->input->client_stride = (int64_t)input.client_stride;
    out->input->client_pointer = (int64_t)input.client_pointer;
    out->input->has_movement_modes = true;
    out->input->mode_normal = (int64_t)input.movement_normal;
    out->input->mode_noclip = (int64_t)input.movement_noclip;
    out->input->mode_freeze = (int64_t)input.movement_freeze;
    out->input->intermission = xmalloc(input.intermission_count * sizeof *out->input->intermission);
    out->input->intermission_count = input.intermission_count;
    for (size_t i = 0; i < input.intermission_count; i++)
      out->input->intermission[i] = (int64_t)input.intermission[i];
    out->input->entry_client_think = (int64_t)input.entry_client_think;
    out->input->entry_run_client = (int64_t)input.entry_run_client;
    out->input->entry_client_spawn = (int64_t)input.entry_client_spawn;
    out->input->entry_move = (int64_t)input.entry_move;
    out->input->entry_slice = (int64_t)input.entry_slice;
  }
  qa_q3_equipment_qvm_primary_weapon_profile weapons;
  if (qa_q3_primary_weapon_profile(&equipment, catalog, catalog_count, &weapons)) {
    out->weapons = xmalloc(sizeof *out->weapons);
    memset(out->weapons, 0, sizeof *out->weapons);
    qa_qvmy_primary_weapon_profile *target = out->weapons;
    target->module = weapons.module;
    target->abi_profile = weapons.abi_profile != NULL ? weapons.abi_profile : "q3-modern";
    target->entity_stride = (int64_t)weapons.entity_stride;
    target->client_stride = (int64_t)weapons.client_stride;
    target->client_pointer = (int64_t)weapons.client_pointer;
    target->has_match = weapons.has_match;
    target->match = weapons.match;
    target->max_health = (int64_t)weapons.max_health;
    target->persistent_max_health = (int64_t)weapons.persistent_max_health;
    target->dispatcher_entry = (int64_t)weapons.stage.dispatcher.entry;
    target->dispatcher_actor = weapons.stage.dispatcher.actor;
    target->predicates = xmalloc(weapons.stage.predicate_count * sizeof *target->predicates);
    target->predicate_count = weapons.stage.predicate_count;
    memcpy(target->predicates, weapons.stage.predicates, target->predicate_count * sizeof *target->predicates);
    target->settled = xmalloc(weapons.stage.settled_count * sizeof *target->settled);
    target->settled_count = weapons.stage.settled_count;
    memcpy(target->settled, weapons.stage.settled, target->settled_count * sizeof *target->settled);
    target->selection_field = weapons.stage.selection.field;
    target->selection_values = xmalloc(weapons.stage.selection.value_count * sizeof *target->selection_values);
    target->selection_value_count = weapons.stage.selection.value_count;
    memcpy(target->selection_values, weapons.stage.selection.values,
           target->selection_value_count * sizeof *target->selection_values);
    target->request_entry = (int64_t)weapons.stage.request.entry;
    target->request_argument = (int64_t)weapons.stage.request.argument;
    target->request_accepted = xmalloc(weapons.stage.request.accepted_count * sizeof *target->request_accepted);
    target->request_accepted_count = weapons.stage.request.accepted_count;
    memcpy(target->request_accepted, weapons.stage.request.accepted,
           target->request_accepted_count * sizeof *target->request_accepted);
    target->has_body_trace = weapons.equipment_movement.has_body_trace;
    target->body_trace_callback = (int64_t)weapons.equipment_movement.body_trace_callback;
    target->body_trace_mask = (int64_t)weapons.equipment_movement.body_trace_mask;
    target->move_entry = (int64_t)weapons.equipment_movement.move;
    target->slice_entry = (int64_t)weapons.equipment_movement.slice;
    target->duck_entry = (int64_t)weapons.equipment_movement.duck;
    target->movement_global = (int64_t)weapons.equipment_movement.movement_global;
    target->locomotion.entry = (int64_t)weapons.equipment_movement.locomotion_entry;
    target->locomotion.join = (int64_t)weapons.equipment_movement.locomotion_join;
    target->mins_offset = (int64_t)weapons.equipment_movement.mins;
    target->maxs_offset = (int64_t)weapons.equipment_movement.maxs;
    target->availability_movement_type = (int64_t)weapons.availability_movement_type;
    target->availability_excluded = xmalloc(weapons.availability_excluded_count * sizeof *target->availability_excluded);
    target->availability_excluded_count = weapons.availability_excluded_count;
    for (size_t i = 0; i < weapons.availability_excluded_count; i++)
      target->availability_excluded[i] = (int64_t)weapons.availability_excluded[i];
    target->availability_health = (int64_t)weapons.availability_health;
    target->availability_team = (int64_t)weapons.availability_team;
    target->availability_spectator_team = (int64_t)weapons.availability_spectator_team;
    target->availability_flags = (int64_t)weapons.availability_flags;
    target->availability_respawn_flag = (int64_t)weapons.availability_respawn_flag;
    target->powerup_quad = (int64_t)weapons.powerup_quad;
    target->powerup_haste = (int64_t)weapons.powerup_haste;
    target->powerup_flight = (int64_t)weapons.powerup_flight;
    target->torso_entry = (int64_t)weapons.torso_animation_entry;
    target->torso_attack = (int64_t)weapons.torso_animation_attack;
    target->torso_melee = (int64_t)weapons.torso_animation_melee;
    target->water_entity_offset = (int64_t)weapons.water_level_entity_offset;
    target->water_movement_offset = (int64_t)weapons.water_level_movement_offset;
    target->damage_entry = (int64_t)weapons.damage_factor_entry;
    target->damage_result = (int64_t)weapons.damage_factor_result;
    target->damage_stop.entry = (int64_t)weapons.damage_factor_stop_entry;
    target->damage_stop.join = (int64_t)weapons.damage_factor_stop_join;
    target->equipment_contexts = xmalloc(weapons.equipment_context_count * sizeof *target->equipment_contexts);
    target->equipment_context_count = weapons.equipment_context_count;
    for (size_t i = 0; i < weapons.equipment_context_count; i++) {
      target->equipment_contexts[i].provider = weapons.equipment_contexts[i].provider;
      target->equipment_contexts[i].item = weapons.equipment_contexts[i].item;
    }
    target->delay.entry = (int64_t)weapons.delay.entry;
    target->delay.join = (int64_t)weapons.delay.join;
    target->delay.has_result = weapons.delay.has_result;
    target->delay.result = (int64_t)weapons.delay.result;
    target->delay_inputs = xmalloc(weapons.delay.input_count * sizeof *target->delay_inputs);
    target->delay.input_count = weapons.delay.input_count;
    target->delay.inputs = target->delay_inputs;
    for (size_t i = 0; i < weapons.delay.input_count; i++)
      target->delay_inputs[i] = (int64_t)weapons.delay.inputs[i];
    target->delay_movement_global = (int64_t)weapons.delay_player_movement_global;
    target->delay_player_offset = (int64_t)weapons.delay_player_player_offset;
    target->teleport_entry = (int64_t)weapons.teleport_entry;
    target->teleport_spawn = (int64_t)weapons.teleport_spawn;
    target->teleport_view = (int64_t)weapons.teleport_view;
    target->teleport_region.entry = (int64_t)weapons.teleport_region.entry;
    target->teleport_region.join = (int64_t)weapons.teleport_region.join;
    target->teleport_region.has_result = weapons.teleport_region.has_result;
    target->teleport_region.result = (int64_t)weapons.teleport_region.result;
    target->teleport_region_inputs = xmalloc(weapons.teleport_region.input_count * sizeof *target->teleport_region_inputs);
    target->teleport_region.input_count = weapons.teleport_region.input_count;
    target->teleport_region.inputs = target->teleport_region_inputs;
    for (size_t i = 0; i < weapons.teleport_region.input_count; i++)
      target->teleport_region_inputs[i] = (int64_t)weapons.teleport_region.inputs[i];
    target->teleport_objectives.entry = (int64_t)weapons.teleport_objectives.entry;
    target->teleport_objectives.join = (int64_t)weapons.teleport_objectives.join;
    target->teleport_objectives.has_result = weapons.teleport_objectives.has_result;
    target->teleport_objectives.result = (int64_t)weapons.teleport_objectives.result;
    target->teleport_objectives_inputs = xmalloc(weapons.teleport_objectives.input_count * sizeof *target->teleport_objectives_inputs);
    target->teleport_objectives.input_count = weapons.teleport_objectives.input_count;
    target->teleport_objectives.inputs = target->teleport_objectives_inputs;
    for (size_t i = 0; i < weapons.teleport_objectives.input_count; i++)
      target->teleport_objectives_inputs[i] = (int64_t)weapons.teleport_objectives.inputs[i];
    target->drop_entry = (int64_t)weapons.drop_entry;
    target->drop_argument = (int64_t)weapons.drop_argument;
    target->drop_weapon = (int64_t)weapons.drop_weapon;
    target->drop_ammo_offset = (int64_t)weapons.drop_ammo;
    target->drop_region.entry = (int64_t)weapons.drop_region_entry;
    target->drop_region.join = (int64_t)weapons.drop_region_join;
    target->give_entry = (int64_t)weapons.give_entry;
    target->give_argument = (int64_t)weapons.give_argument;
    target->give_weapons = (int64_t)weapons.give_weapons;
    target->give_ammo = (int64_t)weapons.give_ammo;
    target->give_named_region.entry = (int64_t)weapons.give_named_entry;
    target->give_named_region.join = (int64_t)weapons.give_named_join;
    target->give_named_name = (int64_t)weapons.give_named_name;
    target->give_named_item = (int64_t)weapons.give_named_item;
    qa_q3_equipment_qvm_primary_weapon_profile_destroy(&weapons);
  }
  qa_q3_equipment_qvm_inventory_profile inventory;
  if (qa_q3_native_inventory_profile(&equipment, &inventory)) {
    out->inventory = xmalloc(sizeof *out->inventory);
    memset(out->inventory, 0, sizeof *out->inventory);
    out->inventory->module = inventory.module;
    out->inventory->abi_profile = inventory.abi_profile != NULL ? inventory.abi_profile : "q3-modern";
    out->inventory->weapons_offset = (int64_t)inventory.weapons_offset;
    out->inventory->ammo_offset = (int64_t)inventory.ammo_offset;
    out->inventory->capacity_kind = "constant";
    qa_q3_equipment_qvm_memory memory = {artifact->initialized_data, artifact->initialized_data_length};
    out->inventory->capacity_constant =
        inventory.capacity != NULL ? (int64_t)inventory.capacity(inventory.capacity_ctx, memory, 0) : 0;
    qa_q3_equipment_qvm_inventory_profile_destroy(&inventory);
  }
  qa_q3_equipment_qvm_pickup_profile pickups;
  if (qa_q3_native_pickup_profile(&equipment, &pickups)) {
    out->pickups = xmalloc(sizeof *out->pickups);
    memset(out->pickups, 0, sizeof *out->pickups);
    qa_qvmy_pickup_profile *target = out->pickups;
    target->module = pickups.module;
    target->abi_profile = pickups.abi_profile != NULL ? pickups.abi_profile : "q3-modern";
    target->entity_stride = (int64_t)pickups.entity_stride;
    target->client_stride = (int64_t)pickups.client_stride;
    target->field_inuse = (int64_t)pickups.field_inuse;
    target->field_client = (int64_t)pickups.field_client;
    target->field_health = (int64_t)pickups.field_health;
    target->field_item = (int64_t)pickups.field_item;
    target->field_count = (int64_t)pickups.field_count;
    target->field_flags = (int64_t)pickups.field_flags;
    target->dropped_flag = (int64_t)pickups.dropped_flag;
    target->items.address = (int64_t)pickups.items.address;
    target->items.count = (int64_t)pickups.items.count;
    target->items.stride = (int64_t)pickups.items.stride;
    target->items.field_class_name = (int64_t)pickups.items.field_class_name;
    target->items.field_pickup_name = (int64_t)pickups.items.field_pickup_name;
    target->items.field_type = (int64_t)pickups.items.field_type;
    target->items.field_tag = (int64_t)pickups.items.field_tag;
    target->items.weapon_type = (int64_t)pickups.items.weapon_type;
    target->items.ammo_type = (int64_t)pickups.items.ammo_type;
    target->touch = (int64_t)pickups.touch;
    target->gate_entry = (int64_t)pickups.gate_entry;
    target->gate_calls = xmalloc(pickups.gate_call_count * sizeof *target->gate_calls);
    target->gate_call_count = pickups.gate_call_count;
    for (size_t i = 0; i < pickups.gate_call_count; i++) target->gate_calls[i] = (int64_t)pickups.gate_calls[i];
    target->gate_item_argument = (int64_t)pickups.gate_item_argument;
    target->gate_player_argument = (int64_t)pickups.gate_player_argument;
    target->targets_entry = (int64_t)pickups.targets_entry;
    target->targets_calls = xmalloc(pickups.targets_call_count * sizeof *target->targets_calls);
    target->targets_call_count = pickups.targets_call_count;
    for (size_t i = 0; i < pickups.targets_call_count; i++) target->targets_calls[i] = (int64_t)pickups.targets_calls[i];
    target->free_entry = (int64_t)pickups.free_entry;
    target->objective_types = xmalloc(pickups.objective_type_count * sizeof *target->objective_types);
    target->objective_type_count = pickups.objective_type_count;
    for (size_t i = 0; i < pickups.objective_type_count; i++) target->objective_types[i] = (int64_t)pickups.objective_types[i];
    target->grants = xmalloc(pickups.grant_count * sizeof *target->grants);
    target->grant_count = pickups.grant_count;
    for (size_t i = 0; i < pickups.grant_count; i++) {
      const qa_q3_equipment_qvm_pickup_grant *source = &pickups.grants[i];
      qa_qvmy_pickup_grant *grant = &target->grants[i];
      memset(grant, 0, sizeof *grant);
      grant->item_type = (int64_t)source->item_type;
      grant->entry = (int64_t)source->entry;
      grant->calls = xmalloc(source->call_count * sizeof *grant->calls);
      grant->call_count = source->call_count;
      for (size_t c = 0; c < source->call_count; c++) grant->calls[c] = (int64_t)source->calls[c];
      grant->operation_kind = source->operation_is_region ? "region" : "return";
      grant->accepted_return = (int64_t)source->accepted_return;
      grant->region_entry = (int64_t)source->region_entry;
      grant->region_join = (int64_t)source->region_join;
      grant->region_quantity = (int64_t)source->quantity;
      grant->has_weapon = source->has_weapon;
      grant->weapon_bits_offset = (int64_t)source->weapon_bits_offset;
      grant->weapon_ammo_offset = (int64_t)source->weapon_ammo_offset;
      grant->weapon_quantity.entry = (int64_t)source->weapon_quantity.entry;
      grant->weapon_quantity.join = (int64_t)source->weapon_quantity.join;
      grant->weapon_quantity.has_result = source->weapon_quantity.has_result;
      grant->weapon_quantity.result = (int64_t)source->weapon_quantity.result;
      grant->weapon_quantity_inputs = xmalloc(source->weapon_quantity.input_count * sizeof *grant->weapon_quantity_inputs);
      grant->weapon_quantity.input_count = source->weapon_quantity.input_count;
      grant->weapon_quantity.inputs = grant->weapon_quantity_inputs;
      for (size_t q = 0; q < source->weapon_quantity.input_count; q++)
        grant->weapon_quantity_inputs[q] = (int64_t)source->weapon_quantity.inputs[q];
    }
  }
  qa_q3_equipment_qvm_primary_combat_profile combat;
  if (qa_q3_native_combat_profile(&equipment, &combat)) {
    out->combat = xmalloc(sizeof *out->combat);
    memset(out->combat, 0, sizeof *out->combat);
    qa_qvmy_primary_combat_profile *target = out->combat;
    target->module = combat.grapple.module;
    target->abi_profile = combat.grapple.abi_profile != NULL ? combat.grapple.abi_profile : "q3-modern";
    target->entity_stride = (int64_t)combat.grapple.entity_stride;
    target->client_stride = (int64_t)combat.grapple.client_stride;
    for (size_t i = 0; i < combat.damage_call.role_count && i < 8; i++)
      target->damage_roles[i] = combat.damage_call.roles[i];
    target->damage_extras = xmalloc(combat.damage_call.extra_count * sizeof *target->damage_extras);
    target->damage_extra_count = combat.damage_call.extra_count;
    memcpy(target->damage_extras, combat.damage_call.extras, target->damage_extra_count * sizeof *target->damage_extras);
    target->state_health_stat = (int64_t)combat.state.health_stat;
    target->team_persistent_stat = (int64_t)combat.state.team_persistent_stat;
    target->team_values = xmalloc(combat.state.team_value_count * sizeof *target->team_values);
    target->team_value_count = combat.state.team_value_count;
    for (size_t i = 0; i < combat.state.team_value_count; i++) {
      target->team_values[i].value = (int64_t)combat.state.team_values[i].value;
      target->team_values[i].team = combat.state.team_values[i].team;
    }
    target->flag_notarget = (int64_t)combat.state.flag_notarget;
    target->flag_invulnerable = (int64_t)combat.state.flag_invulnerable;
    target->flag_no_knockback = (int64_t)combat.state.flag_no_knockback;
    target->mass_is_constant = combat.state.mass.is_constant;
    target->mass_value = combat.state.mass.value;
    target->mass_offset = (int64_t)combat.state.mass.offset;
    target->mass_storage = combat.state.mass.storage;
    target->damage_flags = combat.damage_flags;
    target->field_inuse = (int64_t)combat.grapple.fields.inuse;
    target->field_health = (int64_t)combat.grapple.fields.health;
    target->field_takedamage = (int64_t)combat.grapple.fields.takedamage;
    target->field_parent = (int64_t)combat.grapple.fields.parent;
    target->field_client = (int64_t)combat.grapple.fields.client;
    target->callback_allocate = (int64_t)combat.grapple.callbacks.allocate;
    target->callback_free = (int64_t)combat.grapple.callbacks.free;
    target->callback_damage = (int64_t)combat.grapple.callbacks.damage;
    target->reaction_flags = (int64_t)combat.reactions.flags;
    target->reaction_pain = (int64_t)combat.reactions.pain;
    target->reaction_die = (int64_t)combat.reactions.die;
    target->pain_call = combat.reactions.pain_call;
    target->die_call = combat.reactions.die_call;
    target->grapple_damage_method = (int64_t)combat.grapple.grapple_damage_method;
    target->armor_check_armor = (int64_t)combat.armor.check_armor;
    for (size_t i = 0; i < combat.armor.call.role_count && i < 3; i++)
      target->armor_roles[i] = combat.armor.call.roles[i];
    target->armor_extras = xmalloc(combat.armor.call.extra_count * sizeof *target->armor_extras);
    target->armor_extra_count = combat.armor.call.extra_count;
    memcpy(target->armor_extras, combat.armor.call.extras, target->armor_extra_count * sizeof *target->armor_extras);
    target->armor_points_stat = (int64_t)combat.armor.points_stat;
    target->armor_protection = combat.armor.protection;
    target->has_tiers = combat.armor.has_tiers;
    target->tier_stat = (int64_t)combat.armor.tiers_stat;
    target->tier_when = xmalloc(combat.armor.when_any_count * sizeof *target->tier_when);
    target->tier_when_count = combat.armor.when_any_count;
    for (size_t i = 0; i < combat.armor.when_any_count; i++) {
      target->tier_when[i].offset = (int64_t)combat.armor.when_any[i].offset;
      target->tier_when[i].comparison = combat.armor.when_any[i].comparison;
      target->tier_when[i].value = (int64_t)combat.armor.when_any[i].value;
    }
    target->tier_values = xmalloc(combat.armor.value_count * sizeof *target->tier_values);
    target->tier_value_count = combat.armor.value_count;
    for (size_t i = 0; i < combat.armor.value_count; i++) {
      target->tier_values[i].tier = (int64_t)combat.armor.values[i].tier;
      target->tier_values[i].protection = combat.armor.values[i].protection;
    }
    target->tier_fallback = combat.armor.fallback;
  }
}

void qa_qvm_read_primary_profile(const qa_save_reader *reader, const qa_qvmy_artifact *artifact,
                                 const qa_qvm_catalog_entry *catalog, size_t catalog_count, bool has_catalog,
                                 const qa_qvmy_item_layout *items,
                                 const qa_resolved_resource_reference *declaration,
                                 qa_qvm_primary_profile *out) {
  memset(out, 0, sizeof *out);
  out->declaration = declaration;
  out->input = xmalloc(sizeof *out->input);
  qa_save_reader input_reader = field(reader, "input");
  qa_qvm_read_primary_input(&input_reader, artifact, out->input);
  qa_save_reader_destroy(&input_reader);
  out->weapons = xmalloc(sizeof *out->weapons);
  qa_save_reader weapons_reader = field(reader, "weapons");
  qa_qvm_read_primary_weapons(&weapons_reader, artifact, catalog, catalog_count, has_catalog, out->weapons);
  qa_save_reader_destroy(&weapons_reader);
  qa_qvm_inventory_records records = {out->weapons->client_stride, out->weapons->entity_stride};
  out->inventory = xmalloc(sizeof *out->inventory);
  qa_save_reader inventory_reader = field(reader, "inventory");
  qa_qvm_read_primary_inventory_profile(&inventory_reader, artifact, &records, out->inventory);
  qa_save_reader_destroy(&inventory_reader);
  out->pickups = xmalloc(sizeof *out->pickups);
  qa_save_reader pickups_reader = field(reader, "pickups");
  qa_qvm_read_primary_pickup_profile(&pickups_reader, artifact, out->pickups);
  qa_save_reader_destroy(&pickups_reader);
  out->combat = xmalloc(sizeof *out->combat);
  qa_save_reader combat_reader = field(reader, "combat");
  qa_qvm_read_primary_combat(&combat_reader, artifact, out->combat);
  qa_save_reader_destroy(&combat_reader);
  if (out->input->entity_stride != out->weapons->entity_stride ||
      out->input->client_stride != out->weapons->client_stride ||
      out->input->client_pointer != out->weapons->client_pointer ||
      out->input->entity_stride != out->pickups->entity_stride ||
      out->input->client_stride != out->pickups->client_stride ||
      out->input->client_pointer != out->pickups->field_client ||
      out->input->entity_stride != out->combat->entity_stride ||
      out->input->client_stride != out->combat->client_stride ||
      out->input->client_pointer != out->combat->field_client ||
      out->input->entry_move != out->weapons->move_entry ||
      out->input->entry_slice != out->weapons->slice_entry)
    qa_save_reader_fail(reader, "primary interfaces disagree about their original player records or movement entries");
  if (!qa_qvmy_item_layout_equal(items, &out->pickups->items)) {
    qa_save_reader items_reader = field(reader, "items");
    qa_save_reader_fail(&items_reader, "primary catalog and pickup interfaces name different item tables");
  }
  if (out->inventory->is_private) {
    for (size_t i = 0; i < out->weapons->selection_value_count; i++) {
      bool stored = false;
      for (size_t s = 0; s < out->inventory->storage_count; s++) {
        const qa_qvm_item_storage *storage = &out->inventory->storage[s];
        if (strcmp(storage->kind, "counter") == 0) {
          if (strcmp(storage->item, out->weapons->selection_values[i].item) == 0) stored = true;
        } else {
          for (size_t j = 0; j < storage->item_count; j++)
            if (strcmp(storage->items[j].item, out->weapons->selection_values[i].item) == 0) stored = true;
        }
      }
      if (!stored) {
        qa_save_reader at = field(reader, "inventory");
        qa_save_reader_fail(&at, "original weapon selection lacks private inventory storage");
      }
    }
    bool bad_projection = !out->weapons->drop_ammo_inventory;
    for (size_t i = 0; i < out->pickups->grant_count; i++) {
      const qa_qvmy_pickup_grant *grant = &out->pickups->grants[i];
      if (strcmp(grant->operation_kind, "region") == 0 && grant->has_weapon && !grant->weapon_storage_inventory)
        bad_projection = true;
    }
    if (bad_projection) {
      qa_save_reader at = field(reader, "inventory");
      qa_save_reader_fail(&at, "private inventory requires original pickup/drop projection through its declared storage");
    }
  } else {
    for (size_t i = 0; i < out->weapons->selection_value_count; i++) {
      if (out->weapons->selection_values[i].value > 15) {
        qa_save_reader at = field(reader, "inventory");
        qa_save_reader_fail(&at, "original weapon selection exceeds public inventory storage");
      }
    }
  }
}

void qa_qvm_primary_profile_free(qa_qvm_primary_profile *profile) {
  if (profile->input != NULL) {
    qa_qvmy_input_definition_free(profile->input);
    free(profile->input);
  }
  if (profile->weapons != NULL) {
    qa_qvmy_primary_weapon_profile_free(profile->weapons);
    free(profile->weapons);
  }
  if (profile->inventory != NULL) {
    qa_qvmy_inventory_profile_free(profile->inventory);
    free(profile->inventory);
  }
  if (profile->pickups != NULL) {
    qa_qvmy_pickup_profile_free(profile->pickups);
    free(profile->pickups);
  }
  if (profile->combat != NULL) {
    qa_qvmy_primary_combat_profile_free(profile->combat);
    free(profile->combat);
  }
  memset(profile, 0, sizeof *profile);
}
