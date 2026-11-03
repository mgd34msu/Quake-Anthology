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

typedef struct equipment_view {
  qa_q3_equipment_qvm_artifact artifact;
  qa_q3_equipment_known_artifact known;
  qa_q3_equipment_qvm_instruction *instructions; /* owned */
} equipment_view;

/* The content profiles read the same artifact through their own mirror types. */
static void equipment_artifact_init(equipment_view *view, const qa_resolved_qvm_artifact *artifact) {
  memset(view, 0, sizeof *view);
  qa_q3_equipment_qvm_artifact *result = &view->artifact;
  result->module = artifact->module;
  result->role = artifact->role;
  result->abi_profile = artifact->has_abi_profile ? artifact->abi_profile : NULL;
  if (artifact->known != NULL) {
    view->known.product = artifact->known->product;
    result->known = &view->known;
  }
  result->image.initialized_data = artifact->image.initialized_data;
  result->image.initialized_data_length = artifact->image.initialized_length;
  result->image.allocated_data_length = (double)artifact->image.allocated_data_length;
  result->image.data_length = (double)artifact->image.data_length;
  result->image.literal_length = (double)artifact->image.literal_length;
  result->image.bss_length = (double)artifact->image.bss_length;
  view->instructions = xmalloc(artifact->image.instruction_count * sizeof *view->instructions);
  for (size_t i = 0; i < artifact->image.instruction_count; i++) {
    const qa_qvm_instruction *instruction = &artifact->image.instructions[i];
    view->instructions[i].byte_offset = (double)instruction->byte_offset;
    view->instructions[i].opcode = (double)instruction->opcode;
    view->instructions[i].operand_width = (double)instruction->operand_width;
    view->instructions[i].operand = (double)instruction->operand;
  }
  result->image.instructions = view->instructions;
  result->image.instruction_count = artifact->image.instruction_count;
}

/* TS QvmInventoryProfile capacity from the content profile, over live memory. */
static double content_capacity(void *ctx, qa_qvm_memory *memory, double weapon) {
  qa_q3_equipment_qvm_inventory_profile *content = ctx;
  qa_q3_equipment_qvm_memory view = {memory->bytes, memory->length};
  return content->capacity(content->capacity_ctx, view, weapon);
}
static void content_capacity_release(void *ctx) {
  qa_q3_equipment_qvm_inventory_profile_destroy(ctx);
  free(ctx);
}

/* TS QvmPickupGrant.eligible from the content profile: its answer replaces the
 * source gate, as the TS closure returns without proceeding. */
typedef struct content_eligibility {
  void *ctx;
  bool (*eligible)(void *ctx, qa_q3_equipment_qvm_pickup_eligibility context);
} content_eligibility;

static bool content_eligible(void *ctx, qa_qvm_pickup_eligibility context) {
  content_eligibility *source = ctx;
  qa_q3_equipment_qvm_pickup_eligibility view = {
      {context.call->words, context.call->words_length},
      {context.item.bytes + context.item.start, (size_t)context.item.length},
      {context.player.bytes + context.player.start, (size_t)context.player.length}};
  return source->eligible(source->ctx, view);
}

void qa_qvm_builtin_primary_profile(const qa_resolved_qvm_artifact *artifact,
                                    const qa_q3_guest_weapon *catalog, size_t catalog_count,
                                    qa_qvm_primary_profile *out) {
  memset(out, 0, sizeof *out);
  equipment_view view;
  equipment_artifact_init(&view, artifact);
  const qa_q3_equipment_qvm_artifact equipment = view.artifact;
  qa_q3_qvm_input_definition input;
  if (qa_q3_input_profile(&equipment, &input)) {
    out->input = xmalloc(sizeof *out->input);
    memset(out->input, 0, sizeof *out->input);
    out->input->module = input.module;
    out->input->entity_stride = input.entity_stride;
    out->input->client_stride = input.client_stride;
    out->input->client_pointer = input.client_pointer;
    out->input->has_movement_modes = true;
    out->input->movement_mode_normal = input.movement_normal;
    out->input->movement_mode_noclip = input.movement_noclip;
    out->input->movement_mode_freeze = input.movement_freeze;
    double *intermission = xmalloc(input.intermission_count * sizeof *intermission);
    out->input->intermission = intermission;
    out->input->intermission_count = input.intermission_count;
    for (size_t i = 0; i < input.intermission_count; i++)
      intermission[i] = input.intermission[i];
    out->input->entry_client_think = input.entry_client_think;
    out->input->entry_run_client = input.entry_run_client;
    out->input->entry_client_spawn = input.entry_client_spawn;
    out->input->entry_move = input.entry_move;
    out->input->entry_slice = input.entry_slice;
  }
  qa_q3_equipment_qvm_primary_weapon_profile weapons;
  if (qa_q3_primary_weapon_profile(&equipment, catalog, catalog_count, &weapons)) {
    out->weapons = xmalloc(sizeof *out->weapons);
    memset(out->weapons, 0, sizeof *out->weapons);
    qa_qvm_primary_weapon_profile *target = out->weapons;
    target->module = weapons.module;
    target->abi_profile = weapons.abi_profile != NULL ? weapons.abi_profile : "q3-modern";
    target->entity_stride = weapons.entity_stride;
    target->client_stride = weapons.client_stride;
    target->client_pointer = weapons.client_pointer;
    target->has_match = weapons.has_match;
    target->match = weapons.match;
    target->max_health = weapons.max_health;
    target->persistent_max_health = weapons.persistent_max_health;
    target->stage.dispatcher.entry = weapons.stage.dispatcher.entry;
    target->stage.dispatcher.actor = weapons.stage.dispatcher.actor;
    /* The profile owns its pointer path (qa_qvm_primary_weapon_profile_free). */
    size_t indirection_count = weapons.stage.dispatcher.actor.pointer.indirections.count;
    double *indirections = xmalloc(indirection_count * sizeof *indirections);
    for (size_t i = 0; i < indirection_count; i++)
      indirections[i] = weapons.stage.dispatcher.actor.pointer.indirections.values[i];
    target->stage.dispatcher.actor.pointer.indirections.values = indirections;
    qa_qvm_weapon_predicate *predicates = xmalloc(weapons.stage.predicate_count * sizeof *predicates);
    memcpy(predicates, weapons.stage.predicates, weapons.stage.predicate_count * sizeof *predicates);
    target->stage.predicates = predicates;
    target->stage.predicate_count = weapons.stage.predicate_count;
    qa_qvm_item_test *settled = xmalloc(weapons.stage.settled_count * sizeof *settled);
    memcpy(settled, weapons.stage.settled, weapons.stage.settled_count * sizeof *settled);
    target->stage.settled = settled;
    target->stage.settled_count = weapons.stage.settled_count;
    target->stage.selection.field = weapons.stage.selection.field;
    qa_qvm_weapon_selection_value *selection_values =
        xmalloc(weapons.stage.selection.value_count * sizeof *selection_values);
    memcpy(selection_values, weapons.stage.selection.values,
           weapons.stage.selection.value_count * sizeof *selection_values);
    target->stage.selection.values = selection_values;
    target->stage.selection.value_count = weapons.stage.selection.value_count;
    target->stage.request.entry = weapons.stage.request.entry;
    target->stage.request.argument = weapons.stage.request.argument;
    qa_qvm_item_test *accepted = xmalloc(weapons.stage.request.accepted_count * sizeof *accepted);
    memcpy(accepted, weapons.stage.request.accepted, weapons.stage.request.accepted_count * sizeof *accepted);
    target->stage.request.accepted = accepted;
    target->stage.request.accepted_count = weapons.stage.request.accepted_count;
    target->equipment_movement.move = weapons.equipment_movement.move;
    target->equipment_movement.slice = weapons.equipment_movement.slice;
    target->equipment_movement.duck = weapons.equipment_movement.duck;
    target->equipment_movement.movement_global = weapons.equipment_movement.movement_global;
    target->equipment_movement.locomotion_entry = weapons.equipment_movement.locomotion_entry;
    target->equipment_movement.locomotion_join = weapons.equipment_movement.locomotion_join;
    target->equipment_movement.mins = weapons.equipment_movement.mins;
    target->equipment_movement.maxs = weapons.equipment_movement.maxs;
    target->equipment_movement.has_body_trace = weapons.equipment_movement.has_body_trace;
    target->equipment_movement.body_trace_callback = weapons.equipment_movement.body_trace_callback;
    target->equipment_movement.body_trace_mask = weapons.equipment_movement.body_trace_mask;
    target->availability.movement_type = weapons.availability_movement_type;
    double *excluded = xmalloc(weapons.availability_excluded_count * sizeof *excluded);
    for (size_t i = 0; i < weapons.availability_excluded_count; i++)
      excluded[i] = weapons.availability_excluded[i];
    target->availability.excluded = excluded;
    target->availability.excluded_count = weapons.availability_excluded_count;
    target->availability.health = weapons.availability_health;
    target->availability.team = weapons.availability_team;
    target->availability.spectator_team = weapons.availability_spectator_team;
    target->availability.flags = weapons.availability_flags;
    target->availability.respawn_flag = weapons.availability_respawn_flag;
    target->powerups.quad = weapons.powerup_quad;
    target->powerups.haste = weapons.powerup_haste;
    target->powerups.flight = weapons.powerup_flight;
    target->torso_animation.entry = weapons.torso_animation_entry;
    target->torso_animation.attack = weapons.torso_animation_attack;
    target->torso_animation.melee = weapons.torso_animation_melee;
    target->water_level.entity_offset = weapons.water_level_entity_offset;
    target->water_level.movement_offset = weapons.water_level_movement_offset;
    target->damage_factor.entry = weapons.damage_factor_entry;
    target->damage_factor.result = weapons.damage_factor_result;
    target->damage_factor.stop.entry = weapons.damage_factor_stop_entry;
    target->damage_factor.stop.join = weapons.damage_factor_stop_join;
    qa_source_equipment_context *contexts = xmalloc(weapons.equipment_context_count * sizeof *contexts);
    for (size_t i = 0; i < weapons.equipment_context_count; i++) {
      contexts[i].provider = weapons.equipment_contexts[i].provider;
      contexts[i].item = weapons.equipment_contexts[i].item;
    }
    target->equipment_contexts = contexts;
    target->equipment_context_count = weapons.equipment_context_count;
    target->delay.entry = (int64_t)weapons.delay.entry;
    target->delay.join = (int64_t)weapons.delay.join;
    target->delay.has_result = weapons.delay.has_result;
    target->delay.result = (int64_t)weapons.delay.result;
    int64_t *delay_inputs = xmalloc(weapons.delay.input_count * sizeof *delay_inputs);
    for (size_t i = 0; i < weapons.delay.input_count; i++)
      delay_inputs[i] = (int64_t)weapons.delay.inputs[i];
    target->delay.inputs = delay_inputs;
    target->delay.input_count = weapons.delay.input_count;
    target->delay_player.movement_global = weapons.delay_player_movement_global;
    target->delay_player.player_offset = weapons.delay_player_player_offset;
    target->teleport.entry = weapons.teleport_entry;
    target->teleport.spawn = weapons.teleport_spawn;
    target->teleport.view = weapons.teleport_view;
    target->teleport.region.entry = (int64_t)weapons.teleport_region.entry;
    target->teleport.region.join = (int64_t)weapons.teleport_region.join;
    target->teleport.region.has_result = weapons.teleport_region.has_result;
    target->teleport.region.result = (int64_t)weapons.teleport_region.result;
    int64_t *region_inputs = xmalloc(weapons.teleport_region.input_count * sizeof *region_inputs);
    for (size_t i = 0; i < weapons.teleport_region.input_count; i++)
      region_inputs[i] = (int64_t)weapons.teleport_region.inputs[i];
    target->teleport.region.inputs = region_inputs;
    target->teleport.region.input_count = weapons.teleport_region.input_count;
    target->teleport.objectives.entry = (int64_t)weapons.teleport_objectives.entry;
    target->teleport.objectives.join = (int64_t)weapons.teleport_objectives.join;
    target->teleport.objectives.has_result = weapons.teleport_objectives.has_result;
    target->teleport.objectives.result = (int64_t)weapons.teleport_objectives.result;
    int64_t *objective_inputs = xmalloc(weapons.teleport_objectives.input_count * sizeof *objective_inputs);
    for (size_t i = 0; i < weapons.teleport_objectives.input_count; i++)
      objective_inputs[i] = (int64_t)weapons.teleport_objectives.inputs[i];
    target->teleport.objectives.inputs = objective_inputs;
    target->teleport.objectives.input_count = weapons.teleport_objectives.input_count;
    target->drop.entry = weapons.drop_entry;
    target->drop.argument = weapons.drop_argument;
    target->drop.weapon = weapons.drop_weapon;
    target->drop.ammo = weapons.drop_ammo;
    target->drop.region.entry = weapons.drop_region_entry;
    target->drop.region.join = weapons.drop_region_join;
    target->give.entry = weapons.give_entry;
    target->give.argument = weapons.give_argument;
    target->give.weapons = weapons.give_weapons;
    target->give.ammo = weapons.give_ammo;
    target->give.named.entry = weapons.give_named_entry;
    target->give.named.join = weapons.give_named_join;
    target->give.named.name = weapons.give_named_name;
    target->give.named.item = weapons.give_named_item;
    qa_q3_equipment_qvm_primary_weapon_profile_destroy(&weapons);
  }
  qa_q3_equipment_qvm_inventory_profile *inventory = xmalloc(sizeof *inventory);
  if (qa_q3_native_inventory_profile(&equipment, inventory)) {
    out->inventory = xmalloc(sizeof *out->inventory);
    qa_qvm_primary_inventory_profile_source(inventory->module,
        inventory->abi_profile != NULL ? inventory->abi_profile : "q3-modern",
        inventory->weapons_offset, inventory->ammo_offset, content_capacity, inventory,
        content_capacity_release, out->inventory);
  } else {
    free(inventory);
  }
  qa_q3_equipment_qvm_pickup_profile pickups;
  if (qa_q3_native_pickup_profile(&equipment, &pickups)) {
    out->pickups = xmalloc(sizeof *out->pickups);
    memset(out->pickups, 0, sizeof *out->pickups);
    qa_qvm_pickup_profile *target = out->pickups;
    target->module = pickups.module;
    target->abi_profile = pickups.abi_profile != NULL ? pickups.abi_profile : "q3-modern";
    target->entity_stride = pickups.entity_stride;
    target->client_stride = pickups.client_stride;
    target->field_inuse = pickups.field_inuse;
    target->field_client = pickups.field_client;
    target->field_health = pickups.field_health;
    target->field_item = pickups.field_item;
    target->field_count = pickups.field_count;
    target->field_flags = pickups.field_flags;
    target->dropped_flag = pickups.dropped_flag;
    target->items.address_is_number = true;
    target->items.address = pickups.items.address;
    target->items.count_is_number = true;
    target->items.count = pickups.items.count;
    target->items.stride = pickups.items.stride;
    target->items.field_class_name = pickups.items.field_class_name;
    target->items.field_pickup_name = pickups.items.field_pickup_name;
    target->items.field_type = pickups.items.field_type;
    target->items.field_tag = pickups.items.field_tag;
    target->items.weapon_type = pickups.items.weapon_type;
    target->items.ammo_type = pickups.items.ammo_type;
    target->touch = pickups.touch;
    target->gate_entry = pickups.gate_entry;
    double *gate_calls = xmalloc(pickups.gate_call_count * sizeof *gate_calls);
    for (size_t i = 0; i < pickups.gate_call_count; i++) gate_calls[i] = pickups.gate_calls[i];
    target->gate_calls = gate_calls;
    target->gate_call_count = pickups.gate_call_count;
    target->gate_item_argument = pickups.gate_item_argument;
    target->gate_player_argument = pickups.gate_player_argument;
    target->targets_entry = pickups.targets_entry;
    double *targets_calls = xmalloc(pickups.targets_call_count * sizeof *targets_calls);
    for (size_t i = 0; i < pickups.targets_call_count; i++) targets_calls[i] = pickups.targets_calls[i];
    target->targets_calls = targets_calls;
    target->targets_call_count = pickups.targets_call_count;
    target->free_entry = pickups.free_entry;
    double *objective_types = xmalloc(pickups.objective_type_count * sizeof *objective_types);
    for (size_t i = 0; i < pickups.objective_type_count; i++) objective_types[i] = pickups.objective_types[i];
    target->objective_types = objective_types;
    target->objective_type_count = pickups.objective_type_count;
    qa_qvm_pickup_grant *grants = xmalloc(pickups.grant_count * sizeof *grants);
    target->grants = grants;
    target->grant_count = pickups.grant_count;
    for (size_t i = 0; i < pickups.grant_count; i++) {
      const qa_q3_equipment_qvm_pickup_grant *source = &pickups.grants[i];
      qa_qvm_pickup_grant *grant = &grants[i];
      memset(grant, 0, sizeof *grant);
      grant->item_type = source->item_type;
      grant->entry = source->entry;
      double *calls = xmalloc(source->call_count * sizeof *calls);
      for (size_t c = 0; c < source->call_count; c++) calls[c] = source->calls[c];
      grant->calls = calls;
      grant->call_count = source->call_count;
      grant->is_region = source->operation_is_region;
      grant->accepted_return = source->accepted_return;
      grant->region_entry = source->region_entry;
      grant->region_join = source->region_join;
      grant->region_quantity = source->quantity;
      grant->has_weapon = source->has_weapon;
      grant->weapon_bits_offset = source->weapon_bits_offset;
      grant->weapon_ammo_offset = source->weapon_ammo_offset;
      grant->weapon_quantity.entry = (int64_t)source->weapon_quantity.entry;
      grant->weapon_quantity.join = (int64_t)source->weapon_quantity.join;
      grant->weapon_quantity.has_result = source->weapon_quantity.has_result;
      grant->weapon_quantity.result = (int64_t)source->weapon_quantity.result;
      int64_t *quantity_inputs = xmalloc(source->weapon_quantity.input_count * sizeof *quantity_inputs);
      for (size_t q = 0; q < source->weapon_quantity.input_count; q++)
        quantity_inputs[q] = (int64_t)source->weapon_quantity.inputs[q];
      grant->weapon_quantity.inputs = quantity_inputs;
      grant->weapon_quantity.input_count = source->weapon_quantity.input_count;
      content_eligibility *eligibility = xmalloc(sizeof *eligibility);
      eligibility->ctx = source->eligible_ctx;
      eligibility->eligible = source->eligible;
      grant->eligible_ctx = eligibility;
      grant->eligible = content_eligible;
    }
  }
  qa_q3_equipment_qvm_primary_combat_profile combat;
  if (qa_q3_native_combat_profile(&equipment, &combat)) {
    out->combat = xmalloc(sizeof *out->combat);
    memset(out->combat, 0, sizeof *out->combat);
    qa_qvm_primary_combat_profile *target = out->combat;
    target->base.module = combat.grapple.module;
    target->base.abi_profile = combat.grapple.abi_profile != NULL ? combat.grapple.abi_profile : "q3-modern";
    target->base.entity_stride = combat.grapple.entity_stride;
    target->base.client_stride = combat.grapple.client_stride;
    size_t damage_role_count = combat.damage_call.role_count < 8 ? combat.damage_call.role_count : 8;
    qa_qvm_combat_role_index *damage_roles = xmalloc(damage_role_count * sizeof *damage_roles);
    for (size_t i = 0; i < damage_role_count; i++) damage_roles[i] = combat.damage_call.roles[i];
    target->base.damage_call.roles = damage_roles;
    target->base.damage_call.role_count = damage_role_count;
    qa_qvm_combat_extra *damage_extras = xmalloc(combat.damage_call.extra_count * sizeof *damage_extras);
    memcpy(damage_extras, combat.damage_call.extras, combat.damage_call.extra_count * sizeof *damage_extras);
    target->base.damage_call.extras = damage_extras;
    target->base.damage_call.extra_count = combat.damage_call.extra_count;
    target->state.health_stat = combat.state.health_stat;
    target->state.team.persistent_stat = combat.state.team_persistent_stat;
    qa_qvm_combat_team *team_values = xmalloc(combat.state.team_value_count * sizeof *team_values);
    for (size_t i = 0; i < combat.state.team_value_count; i++) {
      team_values[i].value = combat.state.team_values[i].value;
      team_values[i].team = combat.state.team_values[i].team;
    }
    target->state.team.values = team_values;
    target->state.team.value_count = combat.state.team_value_count;
    target->state.flags.notarget = combat.state.flag_notarget;
    target->state.flags.invulnerable = combat.state.flag_invulnerable;
    target->state.flags.no_knockback = combat.state.flag_no_knockback;
    target->state.mass.is_constant = combat.state.mass.is_constant;
    target->state.mass.value = combat.state.mass.value;
    target->state.mass.offset = combat.state.mass.offset;
    target->state.mass.storage = combat.state.mass.storage;
    target->damage_flags = combat.damage_flags;
    target->base.field_inuse = combat.grapple.fields.inuse;
    target->base.field_health = combat.grapple.fields.health;
    target->base.field_takedamage = combat.grapple.fields.takedamage;
    target->base.field_parent = combat.grapple.fields.parent;
    target->field_client = combat.grapple.fields.client;
    target->base.callback_allocate = combat.grapple.callbacks.allocate;
    target->base.callback_free = combat.grapple.callbacks.free;
    target->base.callback_damage = combat.grapple.callbacks.damage;
    target->reactions.flags = combat.reactions.flags;
    target->reactions.pain = combat.reactions.pain;
    target->reactions.die = combat.reactions.die;
    target->reactions.pain_call = combat.reactions.pain_call;
    target->reactions.die_call = combat.reactions.die_call;
    target->grapple_damage_method = combat.grapple.grapple_damage_method;
    target->armor.check_armor = combat.armor.check_armor;
    size_t armor_role_count = combat.armor.call.role_count < 3 ? combat.armor.call.role_count : 3;
    qa_qvm_combat_role_index *armor_roles = xmalloc(armor_role_count * sizeof *armor_roles);
    for (size_t i = 0; i < armor_role_count; i++) armor_roles[i] = combat.armor.call.roles[i];
    target->armor.call.roles = armor_roles;
    target->armor.call.role_count = armor_role_count;
    qa_qvm_combat_extra *armor_extras = xmalloc(combat.armor.call.extra_count * sizeof *armor_extras);
    memcpy(armor_extras, combat.armor.call.extras, combat.armor.call.extra_count * sizeof *armor_extras);
    target->armor.call.extras = armor_extras;
    target->armor.call.extra_count = combat.armor.call.extra_count;
    target->armor.points_stat = combat.armor.points_stat;
    target->armor.protection = combat.armor.protection;
    target->armor.has_tiers = combat.armor.has_tiers;
    target->armor.tiers.stat = combat.armor.tiers_stat;
    qa_qvm_armor_tier_test *tier_when = xmalloc(combat.armor.when_any_count * sizeof *tier_when);
    for (size_t i = 0; i < combat.armor.when_any_count; i++) {
      tier_when[i].offset = combat.armor.when_any[i].offset;
      tier_when[i].comparison = combat.armor.when_any[i].comparison;
      tier_when[i].value = combat.armor.when_any[i].value;
    }
    target->armor.tiers.when_any = tier_when;
    target->armor.tiers.when_any_count = combat.armor.when_any_count;
    qa_qvm_armor_tier_value *tier_values = xmalloc(combat.armor.value_count * sizeof *tier_values);
    for (size_t i = 0; i < combat.armor.value_count; i++) {
      tier_values[i].tier = combat.armor.values[i].tier;
      tier_values[i].protection = combat.armor.values[i].protection;
    }
    target->armor.tiers.values = tier_values;
    target->armor.tiers.value_count = combat.armor.value_count;
    target->armor.tiers.fallback = combat.armor.fallback;
  }
  free(view.instructions);
}

/* TS isDeepStrictEqual(items, pickups.items) over QvmItemLayout. */
static bool item_layout_equal(const qa_qvm_item_layout *left, const qa_qvm_item_layout *right) {
  return left->address_is_number == right->address_is_number && left->address == right->address &&
         left->address_global == right->address_global &&
         left->count_is_number == right->count_is_number && left->count == right->count &&
         left->count_global == right->count_global && left->count_maximum == right->count_maximum &&
         left->has_source == right->has_source &&
         (!left->has_source || strcmp(left->source, right->source) == 0) &&
         left->stride == right->stride && left->field_class_name == right->field_class_name &&
         left->field_pickup_name == right->field_pickup_name && left->field_type == right->field_type &&
         left->field_tag == right->field_tag && left->weapon_type == right->weapon_type &&
         left->ammo_type == right->ammo_type;
}

void qa_qvm_read_primary_profile(const qa_save_reader *reader, const qa_resolved_qvm_artifact *artifact,
                                 const qa_qvm_catalog_entry *catalog, size_t catalog_count, bool has_catalog,
                                 const qa_qvm_item_layout *items,
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
  qa_qvm_inventory_records records = {(int64_t)out->weapons->client_stride, (int64_t)out->weapons->entity_stride};
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
      out->input->entity_stride != out->combat->base.entity_stride ||
      out->input->client_stride != out->combat->base.client_stride ||
      out->input->client_pointer != out->combat->field_client ||
      out->input->entry_move != out->weapons->equipment_movement.move ||
      out->input->entry_slice != out->weapons->equipment_movement.slice)
    qa_save_reader_fail(reader, "primary interfaces disagree about their original player records or movement entries");
  if (!item_layout_equal(items, &out->pickups->items)) {
    qa_save_reader items_reader = field(reader, "items");
    qa_save_reader_fail(&items_reader, "primary catalog and pickup interfaces name different item tables");
  }
  if (out->inventory->private_profile != NULL) {
    for (size_t i = 0; i < out->weapons->stage.selection.value_count; i++) {
      bool stored = false;
      for (size_t s = 0; s < out->inventory->private_profile->storage_count; s++) {
        const qa_qvm_item_storage *storage = &out->inventory->private_profile->storage[s];
        if (strcmp(storage->kind, "counter") == 0) {
          if (strcmp(storage->item, out->weapons->stage.selection.values[i].item) == 0) stored = true;
        } else {
          for (size_t j = 0; j < storage->item_count; j++)
            if (strcmp(storage->items[j].item, out->weapons->stage.selection.values[i].item) == 0) stored = true;
        }
      }
      if (!stored) {
        qa_save_reader at = field(reader, "inventory");
        qa_save_reader_fail(&at, "original weapon selection lacks private inventory storage");
      }
    }
    bool bad_projection = !out->weapons->drop.ammo_is_inventory;
    for (size_t i = 0; i < out->pickups->grant_count; i++) {
      const qa_qvm_pickup_grant *grant = &out->pickups->grants[i];
      if (grant->is_region && grant->has_weapon && !grant->weapon_storage_inventory)
        bad_projection = true;
    }
    if (bad_projection) {
      qa_save_reader at = field(reader, "inventory");
      qa_save_reader_fail(&at, "private inventory requires original pickup/drop projection through its declared storage");
    }
  } else {
    for (size_t i = 0; i < out->weapons->stage.selection.value_count; i++) {
      if (out->weapons->stage.selection.values[i].value > 15) {
        qa_save_reader at = field(reader, "inventory");
        qa_save_reader_fail(&at, "original weapon selection exceeds public inventory storage");
      }
    }
  }
}

void qa_qvm_primary_profile_free(qa_qvm_primary_profile *profile) {
  if (profile->input != NULL) {
    qa_qvm_input_definition_free(profile->input);
    free(profile->input);
  }
  if (profile->weapons != NULL) {
    qa_qvm_primary_weapon_profile_free(profile->weapons);
    free(profile->weapons);
  }
  if (profile->inventory != NULL) {
    qa_qvm_primary_inventory_profile_free(profile->inventory);
    free(profile->inventory);
  }
  if (profile->pickups != NULL) {
    qa_qvm_primary_pickup_profile_free(profile->pickups);
    free(profile->pickups);
  }
  if (profile->combat != NULL) {
    qa_qvm_primary_combat_profile_free(profile->combat);
    free(profile->combat);
  }
  memset(profile, 0, sizeof *profile);
}
