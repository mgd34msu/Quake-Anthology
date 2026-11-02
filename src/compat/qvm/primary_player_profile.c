/* See include/qa/compat/qvm/primary_player_profile.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/primary_player_profile.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/mod_weapon_stage.h"
#include "qa/compat/qvm/operations.h"
#include "qa/compat/qvm/player_record.h"
#include "qa/compat/qvm/shared_entity_record.h"
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

static qa_save_reader item_at(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out)) qa_range_error("QVM memory allocation failed");
  return out;
}

static int64_t integer(qa_save_reader *reader, double minimum, double maximum) {
  double value = qa_save_reader_integer_min(reader, minimum);
  if (value > maximum) qa_save_reader_fail(reader, "value exceeds its source range");
  return (int64_t)value;
}

static int64_t integer_field(const qa_save_reader *reader, const char *name, double minimum, double maximum) {
  qa_save_reader at = field(reader, name);
  int64_t value = integer(&at, minimum, maximum);
  qa_save_reader_destroy(&at);
  return value;
}

static int64_t any_integer_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader at = field(reader, name);
  int64_t value = (int64_t)qa_save_reader_integer(&at);
  qa_save_reader_destroy(&at);
  return value;
}

static int64_t aligned(qa_save_reader *reader, int64_t bytes, int64_t size) {
  int64_t value = integer(reader, 0, (double)(bytes - size));
  if (value % 4 != 0) qa_save_reader_fail(reader, "source word is unaligned");
  return value;
}

static int64_t aligned_field(const qa_save_reader *reader, const char *name, int64_t bytes, int64_t size) {
  qa_save_reader at = field(reader, name);
  int64_t value = aligned(&at, bytes, size);
  qa_save_reader_destroy(&at);
  return value;
}

static const qa_qvm_instruction *insn(const qa_resolved_qvm_artifact *artifact, int64_t index) {
  if (index < 0 || (uint64_t)index >= artifact->image.instruction_count) return NULL;
  return &artifact->image.instructions[index];
}

static int64_t read_entry(qa_save_reader *reader, const qa_resolved_qvm_artifact *artifact) {
  int64_t value = (int64_t)qa_save_reader_integer_min(reader, 0);
  const qa_qvm_instruction *instruction = insn(artifact, value);
  if (instruction == NULL || instruction->opcode != QA_QVMY_OP_ENTER)
    qa_save_reader_fail(reader, "not an original function entry");
  return value;
}

static int64_t entry_field(const qa_save_reader *reader, const char *name, const qa_resolved_qvm_artifact *artifact) {
  qa_save_reader at = field(reader, name);
  int64_t value = read_entry(&at, artifact);
  qa_save_reader_destroy(&at);
  return value;
}

typedef struct layout {
  qa_module_identity module;
  qa_qvm_abi_profile abi_profile;
  int64_t entity_stride;
  int64_t client_stride;
} layout;

static layout read_layout(const qa_save_reader *reader, const qa_resolved_qvm_artifact *artifact) {
  if (strcmp(artifact->role, "qagame") != 0)
    qa_save_reader_fail(reader, "primary player services require a qagame ABI");
  layout result;
  result.module = artifact->module;
  result.abi_profile = artifact->has_abi_profile ? artifact->abi_profile : "q3-modern";
  result.entity_stride = integer_field(reader, "entityStride", qa_qvm_shared_entity_bytes(result.abi_profile),
                                       (double)artifact->image.allocated_data_length);
  result.client_stride = integer_field(reader, "clientStride", qa_qvm_player_state_bytes(result.abi_profile),
                                       (double)artifact->image.allocated_data_length);
  if (result.entity_stride % 4 != 0 || result.client_stride % 4 != 0)
    qa_save_reader_fail(reader, "source record strides must be aligned");
  return result;
}

typedef struct primary_region { int64_t entry, join; } primary_region;

static primary_region read_region(const qa_save_reader *reader) {
  primary_region result = {integer_field(reader, "entry", 0, 9007199254740991.0),
                                   integer_field(reader, "join", 0, 9007199254740991.0)};
  return result;
}

static void read_evaluation(const qa_save_reader *reader, qa_qvm_region_evaluation *out) {
  out->entry = integer_field(reader, "entry", 0, 9007199254740991.0);
  out->join = integer_field(reader, "join", 0, 9007199254740991.0);
  qa_save_reader inputs_reader = field(reader, "inputs");
  size_t count = qa_save_reader_list_count(&inputs_reader);
  int64_t *inputs = xmalloc(count * sizeof *inputs);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value = item_at(&inputs_reader, i);
    inputs[i] = (int64_t)qa_save_reader_integer_min(&value, 0);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&inputs_reader);
  out->inputs = inputs;
  out->input_count = count;
  qa_save_reader result_reader = field(reader, "result");
  if (qa_save_reader_is_null(&result_reader)) {
    out->has_result = false;
    out->result = 0;
  } else {
    out->has_result = true;
    out->result = (int64_t)qa_save_reader_integer_min(&result_reader, 0);
  }
  qa_save_reader_destroy(&result_reader);
}

static qa_qvm_mod_input_pointer read_source_pointer(const qa_save_reader *reader, int64_t data_bytes) {
  qa_save_reader kind_reader = field(reader, "kind");
  static const char *const kinds[] = {"argument", "global"};
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 2);
  qa_save_reader_destroy(&kind_reader);
  qa_qvm_mod_input_pointer result;
  memset(&result, 0, sizeof result);
  result.offset = (double)integer_field(reader, "offset", 0, 9007199254740991.0);
  qa_save_reader indirections_reader = field(reader, "indirections");
  size_t count = qa_save_reader_list_count(&indirections_reader);
  double *indirections = xmalloc(count * sizeof *indirections);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value = item_at(&indirections_reader, i);
    indirections[i] = qa_save_reader_integer_min(&value, 0);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&indirections_reader);
  result.indirections.values = indirections;
  result.indirections.count = count;
  bool unaligned = (int64_t)result.offset % 4 != 0;
  for (size_t i = 0; i < count; i++) if ((int64_t)indirections[i] % 4 != 0) unaligned = true;
  if (unaligned) qa_save_reader_fail(reader, "source pointer path must use aligned words");
  if (strcmp(kind, "argument") == 0) {
    result.is_global = false;
    result.index = (double)integer_field(reader, "index", 0, 61);
  } else {
    result.is_global = true;
    result.address = (double)aligned_field(reader, "address", data_bytes, 4);
  }
  return result;
}

void qa_qvm_read_primary_input(const qa_save_reader *reader, const qa_resolved_qvm_artifact *artifact,
                               qa_qvm_input_definition *out) {
  memset(out, 0, sizeof *out);
  layout common = read_layout(reader, artifact);
  out->module = common.module;
  out->entity_stride = (double)common.entity_stride;
  out->client_stride = (double)common.client_stride;
  qa_save_reader modes = field(reader, "movementModes");
  if (!qa_save_reader_is_undefined(&modes)) {
    out->has_movement_modes = true;
    out->movement_mode_normal = (double)any_integer_field(&modes, "normal");
    out->movement_mode_noclip = (double)any_integer_field(&modes, "noclip");
    out->movement_mode_freeze = (double)any_integer_field(&modes, "freeze");
  }
  qa_save_reader_destroy(&modes);
  out->client_pointer = (double)aligned_field(reader, "clientPointer", common.entity_stride, 4);
  qa_save_reader intermission = field(reader, "intermission");
  size_t count = qa_save_reader_list_count(&intermission);
  double *intermission_values = xmalloc(count * sizeof *intermission_values);
  out->intermission = intermission_values;
  out->intermission_count = count;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value = item_at(&intermission, i);
    intermission_values[i] = qa_save_reader_integer(&value);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&intermission);
  qa_save_reader entries = field(reader, "entries");
  out->entry_client_think = (double)entry_field(&entries, "clientThink", artifact);
  out->entry_run_client = (double)entry_field(&entries, "runClient", artifact);
  out->entry_client_spawn = (double)entry_field(&entries, "clientSpawn", artifact);
  out->entry_move = (double)entry_field(&entries, "move", artifact);
  out->entry_slice = (double)entry_field(&entries, "slice", artifact);
  qa_save_reader_destroy(&entries);
}

static qa_qvm_item_field read_client_field(const qa_save_reader *reader, int64_t client_stride) {
  qa_qvm_item_field result;
  qa_save_reader record = field(reader, "record");
  result.record = qa_save_reader_literal_string(&record, "client");
  qa_save_reader_destroy(&record);
  result.offset = (double)aligned_field(reader, "offset", client_stride, 4);
  return result;
}

static qa_qvm_item_test read_test(const qa_save_reader *reader, int64_t client_stride) {
  qa_qvm_item_test result;
  qa_save_reader field_reader = field(reader, "field");
  result.field = read_client_field(&field_reader, client_stride);
  qa_save_reader_destroy(&field_reader);
  qa_save_reader mask = field(reader, "mask");
  if (qa_save_reader_is_null(&mask)) {
    result.has_mask = false;
    result.mask = 0;
  } else {
    result.has_mask = true;
    result.mask = (double)integer(&mask, 0, 4294967295.0);
  }
  qa_save_reader_destroy(&mask);
  qa_save_reader comparison = field(reader, "comparison");
  static const char *const comparisons[] = {"equals", "at-most"};
  result.comparison = qa_save_reader_choice_string(&comparison, comparisons, 2);
  qa_save_reader_destroy(&comparison);
  result.value = (double)integer_field(reader, "value", -2147483648.0, 2147483647.0);
  return result;
}

static qa_qvm_item_test *read_tests(const qa_save_reader *reader, int64_t client_stride, size_t *out_count) {
  size_t count = qa_save_reader_list_count(reader);
  qa_qvm_item_test *tests = xmalloc(count * sizeof *tests);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value = item_at(reader, i);
    tests[i] = read_test(&value, client_stride);
    qa_save_reader_destroy(&value);
  }
  *out_count = count;
  return tests;
}

void qa_qvm_read_primary_weapons(const qa_save_reader *reader, const qa_resolved_qvm_artifact *artifact,
                                 const qa_qvm_catalog_entry *catalog, size_t catalog_count, bool has_catalog,
                                 qa_qvm_primary_weapon_profile *out) {
  memset(out, 0, sizeof *out);
  layout common = read_layout(reader, artifact);
  out->module = common.module;
  out->abi_profile = common.abi_profile;
  out->entity_stride = (double)common.entity_stride;
  out->client_stride = (double)common.client_stride;
  int64_t data_bytes = artifact->image.data_length + artifact->image.literal_length + artifact->image.bss_length;
  qa_save_reader contexts_reader = field(reader, "equipmentContexts");
  size_t context_count = qa_save_reader_list_count(&contexts_reader);
  qa_source_equipment_context *contexts = xmalloc(context_count * sizeof *contexts);
  out->equipment_contexts = contexts;
  out->equipment_context_count = context_count;
  for (size_t i = 0; i < context_count; i++) {
    qa_save_reader value = item_at(&contexts_reader, i);
    qa_save_reader provider = field(&value, "provider");
    contexts[i].provider = qa_namespaced(&provider);
    qa_save_reader_destroy(&provider);
    qa_save_reader item_reader = field(&value, "item");
    if (qa_save_reader_is_null(&item_reader)) {
      contexts[i].item = NULL;
    } else {
      contexts[i].item = qa_namespaced(&item_reader);
    }
    qa_save_reader_destroy(&item_reader);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&contexts_reader);
  for (size_t i = 0; i < context_count; i++)
    for (size_t j = i + 1; j < context_count; j++)
      if (strcmp(contexts[i].provider, out->equipment_contexts[j].provider) == 0) {
        qa_save_reader duplicate = field(reader, "equipmentContexts");
        qa_save_reader_fail(&duplicate, "duplicate equipment source context");
      }
  qa_save_reader match_reader = field(reader, "match");
  if (!qa_save_reader_is_undefined(&match_reader)) {
    out->has_match = true;
    out->match = qa_read_source_primary_match(&match_reader, (double)common.client_stride);
  }
  qa_save_reader_destroy(&match_reader);
  out->client_pointer = (double)aligned_field(reader, "clientPointer", common.entity_stride, 4);
  out->max_health = (double)aligned_field(reader, "maxHealth", common.client_stride, 4);
  out->persistent_max_health = (double)aligned_field(reader, "persistentMaxHealth", common.client_stride, 4);
  qa_save_reader stage = field(reader, "stage");
  qa_save_reader dispatcher = field(&stage, "dispatcher");
  out->stage.dispatcher.entry = (double)entry_field(&dispatcher, "entry", artifact);
  qa_save_reader actor_reader = field(&dispatcher, "actor");
  qa_save_reader actor_record = field(&actor_reader, "record");
  out->stage.dispatcher.actor.record = qa_save_reader_literal_string(&actor_record, "client");
  qa_save_reader_destroy(&actor_record);
  qa_save_reader pointer_reader = field(&actor_reader, "pointer");
  out->stage.dispatcher.actor.pointer = read_source_pointer(&pointer_reader, data_bytes);
  qa_save_reader_destroy(&pointer_reader);
  qa_save_reader_destroy(&actor_reader);
  qa_save_reader_destroy(&dispatcher);
  qa_save_reader predicates_reader = field(&stage, "predicates");
  size_t predicate_count = qa_save_reader_list_count(&predicates_reader);
  qa_qvm_weapon_predicate *predicates = xmalloc(predicate_count * sizeof *predicates);
  out->stage.predicates = predicates;
  out->stage.predicate_count = predicate_count;
  for (size_t i = 0; i < predicate_count; i++) {
    qa_save_reader value = item_at(&predicates_reader, i);
    predicates[i].instruction = (double)integer_field(&value, "instruction", 0, 9007199254740991.0);
    qa_save_reader unselected = field(&value, "unselected");
    predicates[i].unselected = qa_save_reader_boolean(&unselected);
    qa_save_reader_destroy(&unselected);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&predicates_reader);
  qa_save_reader settled_reader = field(&stage, "settled");
  out->stage.settled = read_tests(&settled_reader, common.client_stride, &out->stage.settled_count);
  qa_save_reader_destroy(&settled_reader);
  qa_save_reader selection = field(&stage, "selection");
  qa_save_reader selection_field = field(&selection, "field");
  out->stage.selection.field = read_client_field(&selection_field, common.client_stride);
  qa_save_reader_destroy(&selection_field);
  qa_save_reader values_reader = field(&selection, "values");
  size_t value_count = qa_save_reader_list_count(&values_reader);
  qa_qvm_weapon_selection_value *selection_values = xmalloc(value_count * sizeof *selection_values);
  out->stage.selection.values = selection_values;
  out->stage.selection.value_count = value_count;
  for (size_t i = 0; i < value_count; i++) {
    qa_save_reader value = item_at(&values_reader, i);
    selection_values[i].value = (double)integer_field(&value, "value", 1, 2147483647.0);
    qa_save_reader item_reader = field(&value, "item");
    selection_values[i].item = qa_namespaced(&item_reader);
    qa_save_reader_destroy(&item_reader);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&values_reader);
  qa_save_reader request = field(&stage, "request");
  out->stage.request.entry = (double)entry_field(&request, "entry", artifact);
  out->stage.request.argument = (double)integer_field(&request, "argument", 0, 61);
  qa_save_reader accepted_reader = field(&request, "accepted");
  out->stage.request.accepted = read_tests(&accepted_reader, common.client_stride, &out->stage.request.accepted_count);
  qa_save_reader_destroy(&accepted_reader);
  qa_save_reader_destroy(&request);
  qa_save_reader_destroy(&selection);
  qa_save_reader_destroy(&stage);
  qa_save_reader movement = field(reader, "equipmentMovement");
  qa_save_reader body_trace = field(&movement, "bodyTrace");
  if (!qa_save_reader_is_undefined(&body_trace)) {
    out->equipment_movement.has_body_trace = true;
    out->equipment_movement.body_trace_callback = (double)aligned_field(&body_trace, "callback", artifact->image.allocated_data_length, 4);
    out->equipment_movement.body_trace_mask = (double)aligned_field(&body_trace, "mask", artifact->image.allocated_data_length, 4);
  }
  qa_save_reader_destroy(&body_trace);
  out->equipment_movement.move = (double)entry_field(&movement, "move", artifact);
  out->equipment_movement.slice = (double)entry_field(&movement, "slice", artifact);
  out->equipment_movement.duck = (double)entry_field(&movement, "duck", artifact);
  out->equipment_movement.movement_global = (double)aligned_field(&movement, "movementGlobal", data_bytes, 4);
  qa_save_reader locomotion = field(&movement, "locomotion");
  primary_region locomotion_region = read_region(&locomotion);
  out->equipment_movement.locomotion_entry = (double)locomotion_region.entry;
  out->equipment_movement.locomotion_join = (double)locomotion_region.join;
  qa_save_reader_destroy(&locomotion);
  out->equipment_movement.mins = (double)aligned_field(&movement, "mins", artifact->image.allocated_data_length, 12);
  out->equipment_movement.maxs = (double)aligned_field(&movement, "maxs", artifact->image.allocated_data_length, 12);
  qa_save_reader_destroy(&movement);
  qa_save_reader availability = field(reader, "availability");
  out->availability.movement_type = (double)aligned_field(&availability, "movementType", common.client_stride, 4);
  qa_save_reader excluded_reader = field(&availability, "excluded");
  size_t excluded_count = qa_save_reader_list_count(&excluded_reader);
  double *excluded = xmalloc(excluded_count * sizeof *excluded);
  out->availability.excluded = excluded;
  out->availability.excluded_count = excluded_count;
  for (size_t i = 0; i < excluded_count; i++) {
    qa_save_reader value = item_at(&excluded_reader, i);
    excluded[i] = qa_save_reader_integer(&value);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&excluded_reader);
  out->availability.health = (double)aligned_field(&availability, "health", common.client_stride, 4);
  out->availability.team = (double)aligned_field(&availability, "team", common.client_stride, 4);
  out->availability.spectator_team = (double)any_integer_field(&availability, "spectatorTeam");
  out->availability.flags = (double)aligned_field(&availability, "flags", common.client_stride, 4);
  out->availability.respawn_flag = (double)integer_field(&availability, "respawnFlag", 1, 2147483647.0);
  qa_save_reader_destroy(&availability);
  qa_save_reader powers = field(reader, "powerups");
  out->powerups.quad = (double)aligned_field(&powers, "quad", common.client_stride, 4);
  out->powerups.haste = (double)aligned_field(&powers, "haste", common.client_stride, 4);
  out->powerups.flight = (double)aligned_field(&powers, "flight", common.client_stride, 4);
  qa_save_reader_destroy(&powers);
  qa_save_reader torso = field(reader, "torsoAnimation");
  out->torso_animation.entry = (double)entry_field(&torso, "entry", artifact);
  out->torso_animation.attack = (double)integer_field(&torso, "attack", 0, 9007199254740991.0);
  out->torso_animation.melee = (double)integer_field(&torso, "melee", 0, 9007199254740991.0);
  qa_save_reader_destroy(&torso);
  qa_save_reader water = field(reader, "waterLevel");
  out->water_level.entity_offset = (double)aligned_field(&water, "entityOffset", common.entity_stride, 4);
  out->water_level.movement_offset = (double)aligned_field(&water, "movementOffset", artifact->image.allocated_data_length, 4);
  qa_save_reader_destroy(&water);
  qa_save_reader damage = field(reader, "damageFactor");
  out->damage_factor.entry = (double)entry_field(&damage, "entry", artifact);
  out->damage_factor.result = (double)aligned_field(&damage, "result", data_bytes, 4);
  qa_save_reader stop = field(&damage, "stop");
  primary_region damage_stop = read_region(&stop);
  out->damage_factor.stop.entry = (double)damage_stop.entry;
  out->damage_factor.stop.join = (double)damage_stop.join;
  qa_save_reader_destroy(&stop);
  qa_save_reader_destroy(&damage);
  qa_save_reader delay_reader = field(reader, "delay");
  read_evaluation(&delay_reader, &out->delay);
  qa_save_reader_destroy(&delay_reader);
  qa_save_reader delay = field(reader, "delayPlayer");
  out->delay_player.movement_global = (double)aligned_field(&delay, "movementGlobal", data_bytes, 4);
  out->delay_player.player_offset = (double)aligned_field(&delay, "playerOffset", artifact->image.allocated_data_length, 4);
  qa_save_reader_destroy(&delay);
  qa_save_reader teleport = field(reader, "teleport");
  out->teleport.entry = (double)entry_field(&teleport, "entry", artifact);
  qa_save_reader teleport_region = field(&teleport, "region");
  read_evaluation(&teleport_region, &out->teleport.region);
  qa_save_reader_destroy(&teleport_region);
  qa_save_reader teleport_objectives = field(&teleport, "objectives");
  read_evaluation(&teleport_objectives, &out->teleport.objectives);
  qa_save_reader_destroy(&teleport_objectives);
  out->teleport.spawn = (double)entry_field(&teleport, "spawn", artifact);
  out->teleport.view = (double)entry_field(&teleport, "view", artifact);
  qa_save_reader_destroy(&teleport);
  qa_save_reader drop = field(reader, "drop");
  out->drop.entry = (double)entry_field(&drop, "entry", artifact);
  out->drop.argument = (double)integer_field(&drop, "argument", 0, 61);
  out->drop.weapon = (double)aligned_field(&drop, "weapon", common.entity_stride, 4);
  qa_save_reader ammo = field(&drop, "ammo");
  if (ammo.value != NULL && ammo.value->kind == QA_SAVE_STRING && strcmp(ammo.value->text, "inventory") == 0) {
    (void)qa_save_reader_literal_string(&ammo, "inventory");
    out->drop.ammo_is_inventory = true;
  } else {
    out->drop.ammo = (double)aligned(&ammo, common.client_stride, 64);
  }
  qa_save_reader_destroy(&ammo);
  qa_save_reader drop_region = field(&drop, "region");
  primary_region drop_stop = read_region(&drop_region);
  out->drop.region.entry = (double)drop_stop.entry;
  out->drop.region.join = (double)drop_stop.join;
  qa_save_reader_destroy(&drop_region);
  qa_save_reader_destroy(&drop);
  qa_save_reader give = field(reader, "give");
  out->give.entry = (double)entry_field(&give, "entry", artifact);
  out->give.argument = (double)integer_field(&give, "argument", 0, 61);
  out->give.weapons = (double)integer_field(&give, "weapons", 0, 9007199254740991.0);
  out->give.ammo = (double)integer_field(&give, "ammo", 0, 9007199254740991.0);
  qa_save_reader named = field(&give, "named");
  primary_region named_region = read_region(&named);
  out->give.named.entry = (double)named_region.entry;
  out->give.named.join = (double)named_region.join;
  out->give.named.name = (double)integer_field(&named, "name", 0, 9007199254740991.0);
  out->give.named.item = (double)integer_field(&named, "item", 0, 9007199254740991.0);
  qa_save_reader_destroy(&named);
  qa_save_reader_destroy(&give);
  bool repeated = false;
  for (size_t i = 0; i < value_count; i++)
    for (size_t j = i + 1; j < value_count; j++)
      if (selection_values[i].value == selection_values[j].value ||
          strcmp(selection_values[i].item, selection_values[j].item) == 0)
        repeated = true;
  if (repeated) {
    qa_save_reader at = field(reader, "stage");
    qa_save_reader at_selection = field(&at, "selection");
    qa_save_reader_fail(&at_selection, "weapon selection repeats source values or identities");
  }
  if (has_catalog) {
    bool differs = value_count != catalog_count;
    if (!differs) {
      for (size_t i = 0; i < value_count; i++) {
        bool found = false;
        for (size_t j = 0; j < catalog_count; j++)
          if (catalog[j].weapon == (int64_t)selection_values[i].value &&
              strcmp(catalog[j].item, selection_values[i].item) == 0)
            found = true;
        if (!found) differs = true;
      }
    }
    if (differs) {
      qa_save_reader at = field(reader, "stage");
      qa_save_reader at_selection = field(&at, "selection");
      qa_save_reader_fail(&at_selection, "weapon selection differs from the original item catalog");
    }
  }
  if (!out->drop.ammo_is_inventory) {
    for (size_t i = 0; i < value_count; i++) {
      if ((int64_t)out->drop.ammo + (int64_t)selection_values[i].value * 4 + 4 > common.client_stride) {
        qa_save_reader at = field(reader, "drop");
        qa_save_reader_fail(&at, "original drop ammo indexing exceeds its declared client record");
      }
    }
  }
  qa_qvm_validate_weapon_dispatcher(&out->stage, artifact->image.instructions, artifact->image.instruction_count);
  qa_qvm_qualify_region(artifact->image.instructions, artifact->image.instruction_count, (int64_t)out->equipment_movement.slice,
                        (int64_t)out->equipment_movement.locomotion_entry, (int64_t)out->equipment_movement.locomotion_join);
  qa_qvm_qualify_region(artifact->image.instructions, artifact->image.instruction_count, (int64_t)out->damage_factor.entry,
                        (int64_t)out->damage_factor.stop.entry, (int64_t)out->damage_factor.stop.join);
  qa_qvm_qualify_region_evaluation(artifact->image.instructions, artifact->image.instruction_count,
                                    (int64_t)out->stage.dispatcher.entry, &out->delay, "source");
  qa_qvm_qualify_region_evaluation(artifact->image.instructions, artifact->image.instruction_count,
                                    (int64_t)out->teleport.entry, &out->teleport.region, "source");
  qa_qvm_qualify_region_evaluation(artifact->image.instructions, artifact->image.instruction_count,
                                    (int64_t)out->teleport.entry, &out->teleport.objectives, "source");
  qa_qvm_qualify_region(artifact->image.instructions, artifact->image.instruction_count, (int64_t)out->give.entry,
                        (int64_t)out->give.named.entry, (int64_t)out->give.named.join);
  qa_qvm_qualify_region(artifact->image.instructions, artifact->image.instruction_count, (int64_t)out->drop.entry,
                        (int64_t)out->drop.region.entry, (int64_t)out->drop.region.join);
  if (out->delay.input_count != 1 || !out->delay.has_result || out->teleport.region.input_count != 0 ||
      out->teleport.objectives.input_count != 0)
    qa_save_reader_fail(reader, "source effect region inputs differ from the primary player ABI");
  const qa_qvm_instruction *give_entry = insn(artifact, (int64_t)out->give.entry);
  if (give_entry == NULL || give_entry->opcode != QA_QVMY_OP_ENTER) {
    qa_save_reader at = field(reader, "give");
    qa_save_reader_fail(&at, "give entry disappeared");
  }
  int64_t named_offsets[] = {(int64_t)out->give.named.name, (int64_t)out->give.named.item};
  for (int i = 0; i < 2; i++) {
    if (named_offsets[i] < 8 || named_offsets[i] % 4 != 0 || named_offsets[i] + 4 > give_entry->operand) {
      qa_save_reader at = field(reader, "give");
      qa_save_reader at_named = field(&at, "named");
      qa_save_reader_fail(&at_named, "named grant local exceeds its original function frame");
    }
  }
  int64_t entries[] = {(int64_t)out->stage.dispatcher.entry, (int64_t)out->stage.request.entry,
                       (int64_t)out->equipment_movement.duck, (int64_t)out->give.entry, (int64_t)out->drop.entry,
                       (int64_t)out->damage_factor.entry, (int64_t)out->teleport.entry};
  for (int i = 0; i < 7; i++)
    for (int j = i + 1; j < 7; j++)
      if (entries[i] == entries[j])
        qa_save_reader_fail(reader, "primary weapon interfaces overlap original function ownership");
  int64_t completions[] = {(int64_t)out->give.weapons, (int64_t)out->give.ammo};
  for (int i = 0; i < 2; i++) {
    int64_t pc = completions[i];
    const qa_qvm_instruction *instruction = insn(artifact, pc);
    int64_t owner = pc;
    while (owner >= 0) {
      const qa_qvm_instruction *candidate = insn(artifact, owner);
      if (candidate != NULL && candidate->opcode == QA_QVMY_OP_ENTER) break;
      owner--;
    }
    if (owner != (int64_t)out->give.entry || instruction == NULL || instruction->opcode < QA_QVMY_OP_EQ ||
        instruction->opcode > QA_QVMY_OP_GEF || pc <= (int64_t)out->give.entry) {
      qa_save_reader at = field(reader, "give");
      qa_save_reader_fail(&at, "give completion is not an original decision");
    }
  }
}

/* Mirrored validators (TS game-combat.ts). */
static bool validate_positions(const int64_t *positions, size_t count, int64_t words, const char **error) {
  if (words < (int64_t)count || words > 62) {
    *error = "Source combat call exceeds the QVM OP_ARG capacity or omits required arguments";
    return false;
  }
  for (size_t i = 0; i < count; i++) {
    if (positions[i] < 0 || positions[i] >= words) {
      *error = "Source combat argument positions must cover each declared role exactly once within the original call";
      return false;
    }
    for (size_t j = i + 1; j < count; j++) {
      if (positions[i] == positions[j]) {
        *error = "Source combat argument positions must cover each declared role exactly once within the original call";
        return false;
      }
    }
  }
  return true;
}

static bool validate_combat_call(const int64_t *roles, size_t role_count,
                                 const qa_qvm_combat_extra *extras, size_t extra_count,
                                 int64_t data_bytes, const char **error) {
  size_t count = role_count + extra_count;
  int64_t *positions = xmalloc(count * sizeof *positions);
  memcpy(positions, roles, role_count * sizeof *positions);
  for (size_t i = 0; i < extra_count; i++) positions[role_count + i] = (int64_t)extras[i].index;
  bool ok = validate_positions(positions, count, (int64_t)count, error);
  free(positions);
  if (!ok) return false;
  for (size_t i = 0; i < extra_count; i++) {
    const qa_qvm_combat_extra *extra = &extras[i];
    if (strcmp(extra->kind, "float32") == 0) {
      if (!isfinite(extra->value) || !isfinite((double)(float)extra->value)) {
        *error = "Source combat extra requires a finite binary32 value";
        return false;
      }
    } else if (strcmp(extra->kind, "address") == 0) {
      if (!isfinite(extra->value) || extra->value != trunc(extra->value) || extra->value < 0 ||
          extra->value >= (double)data_bytes) {
        *error = "Source combat extra address is outside its artifact data";
        return false;
      }
    } else if (!isfinite(extra->value) || extra->value != trunc(extra->value) ||
               extra->value < -2147483648.0 || extra->value > 2147483647.0) {
      *error = "Source combat extra requires a signed integer word";
      return false;
    }
  }
  return true;
}

static qa_qvm_reaction_call read_reaction_call(const qa_save_reader *reader) {
  qa_qvm_reaction_call result;
  result.arguments = (double)integer_field(reader, "arguments", 2, 62);
  qa_save_reader roles = field(reader, "roles");
  result.role_target = (double)integer_field(&roles, "target", 0, 61);
  result.role_amount = (double)integer_field(&roles, "amount", 0, 61);
  qa_save_reader_destroy(&roles);
  int64_t positions[] = {(int64_t)result.role_target, (int64_t)result.role_amount};
  const char *error = NULL;
  if (!validate_positions(positions, 2, (int64_t)result.arguments, &error))
    qa_save_reader_fail(reader, "%s", error);
  return result;
}

static qa_qvm_combat_extra *read_extras(const qa_save_reader *reader, size_t *out_count) {
  size_t count = qa_save_reader_list_count(reader);
  qa_qvm_combat_extra *extras = xmalloc(count * sizeof *extras);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader extra = item_at(reader, i);
    extras[i].index = (double)integer_field(&extra, "index", 0, 61);
    qa_save_reader kind = field(&extra, "kind");
    static const char *const kinds[] = {"int32", "float32", "address"};
    extras[i].kind = qa_save_reader_choice_string(&kind, kinds, 3);
    qa_save_reader_destroy(&kind);
    qa_save_reader value = field(&extra, "value");
    extras[i].value = qa_save_reader_finite(&value);
    qa_save_reader_destroy(&value);
    qa_save_reader_destroy(&extra);
  }
  *out_count = count;
  return extras;
}

static double read_fraction(qa_save_reader *reader) {
  double result = qa_save_reader_finite(reader);
  if (result < 0 || result > 1 || (double)(float)result != result)
    qa_save_reader_fail(reader, "expected an original binary32 armor fraction");
  return result;
}

void qa_qvm_read_primary_combat(const qa_save_reader *reader, const qa_resolved_qvm_artifact *artifact,
                                qa_qvm_primary_combat_profile *out) {
  memset(out, 0, sizeof *out);
  layout common = read_layout(reader, artifact);
  out->base.module = common.module;
  out->base.abi_profile = common.abi_profile;
  out->base.entity_stride = (double)common.entity_stride;
  out->base.client_stride = (double)common.client_stride;
  int64_t data_bytes = artifact->image.data_length + artifact->image.literal_length + artifact->image.bss_length;
  qa_save_reader damage_call = field(reader, "damageCall");
  qa_save_reader damage_roles = field(&damage_call, "roles");
  const char *damage_names[] = {"target", "inflictor", "attacker", "direction", "point", "amount", "flags", "method"};
  int64_t damage_positions[8];
  qa_qvm_combat_role_index *damage_role_values = xmalloc(8 * sizeof *damage_role_values);
  out->base.damage_call.roles = damage_role_values;
  out->base.damage_call.role_count = 8;
  for (int i = 0; i < 8; i++) {
    damage_role_values[i].role = damage_names[i];
    damage_role_values[i].index = (double)integer_field(&damage_roles, damage_names[i], 0, 61);
    damage_positions[i] = (int64_t)damage_role_values[i].index;
  }
  qa_save_reader_destroy(&damage_roles);
  qa_save_reader damage_extras = field(&damage_call, "extras");
  out->base.damage_call.extras = read_extras(&damage_extras, &out->base.damage_call.extra_count);
  qa_save_reader_destroy(&damage_extras);
  qa_save_reader state = field(reader, "state");
  out->state.health_stat = (double)integer_field(&state, "healthStat", 0, 15);
  qa_save_reader team = field(&state, "team");
  out->state.team.persistent_stat = (double)integer_field(&team, "persistentStat", 0, 15);
  qa_save_reader team_values = field(&team, "values");
  size_t team_count = qa_save_reader_list_count(&team_values);
  qa_qvm_combat_team *team_entries = xmalloc(team_count * sizeof *team_entries);
  out->state.team.values = team_entries;
  out->state.team.value_count = team_count;
  for (size_t i = 0; i < team_count; i++) {
    qa_save_reader value = item_at(&team_values, i);
    team_entries[i].value = (double)integer_field(&value, "value", -2147483648.0, 2147483647.0);
    qa_save_reader team_name = field(&value, "team");
    team_entries[i].team = qa_namespaced(&team_name);
    qa_save_reader_destroy(&team_name);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&team_values);
  qa_save_reader_destroy(&team);
  qa_save_reader flags = field(&state, "flags");
  qa_save_reader *flag_readers[3];
  qa_save_reader flag_storage[3];
  const char *flag_names[] = {"notarget", "invulnerable", "noKnockback"};
  double *flag_slots[] = {&out->state.flags.notarget, &out->state.flags.invulnerable, &out->state.flags.no_knockback};
  for (int i = 0; i < 3; i++) {
    flag_storage[i] = field(&flags, flag_names[i]);
    flag_readers[i] = &flag_storage[i];
    int64_t mask = integer(flag_readers[i], 1, 4294967295.0);
    if ((mask & (mask - 1)) != 0) qa_save_reader_fail(flag_readers[i], "expected an individual source flag");
    *flag_slots[i] = (double)mask;
    qa_save_reader_destroy(flag_readers[i]);
  }
  qa_save_reader_destroy(&flags);
  qa_save_reader mass = field(&state, "mass");
  qa_save_reader mass_kind = field(&mass, "kind");
  static const char *const mass_kinds[] = {"constant", "entity"};
  const char *mass_kind_name = qa_save_reader_choice_string(&mass_kind, mass_kinds, 2);
  qa_save_reader_destroy(&mass_kind);
  if (strcmp(mass_kind_name, "constant") == 0) {
    out->state.mass.is_constant = true;
    qa_save_reader value = field(&mass, "value");
    out->state.mass.value = qa_save_reader_finite(&value);
    qa_save_reader_destroy(&value);
  } else {
    qa_save_reader offset = field(&mass, "offset");
    out->state.mass.offset = (double)aligned(&offset, common.entity_stride, 4);
    if (out->state.mass.offset < (double)qa_qvm_shared_entity_bytes(common.abi_profile))
      qa_save_reader_fail(&offset, "private field overlaps the public entity ABI");
    qa_save_reader_destroy(&offset);
    qa_save_reader storage = field(&mass, "storage");
    static const char *const storages[] = {"int32", "float32"};
    out->state.mass.storage = qa_save_reader_choice_string(&storage, storages, 2);
    qa_save_reader_destroy(&storage);
  }
  qa_save_reader_destroy(&mass);
  qa_save_reader_destroy(&state);
  qa_save_reader damage_flags = field(reader, "damageFlags");
  const char *damage_flag_names[] = {"radius", "noArmor", "noKnockback", "noProtection", "noTeamProtection"};
  double *damage_flag_slots[] = {&out->damage_flags.radius, &out->damage_flags.no_armor,
      &out->damage_flags.no_knockback, &out->damage_flags.no_protection, &out->damage_flags.no_team_protection};
  for (int i = 0; i < 5; i++) {
    qa_save_reader at = field(&damage_flags, damage_flag_names[i]);
    int64_t mask = integer(&at, 1, 4294967295.0);
    if ((mask & (mask - 1)) != 0) qa_save_reader_fail(&at, "expected an individual source flag");
    *damage_flag_slots[i] = (double)mask;
    qa_save_reader_destroy(&at);
  }
  qa_save_reader_destroy(&damage_flags);
  qa_save_reader fields = field(reader, "fields");
  const char *field_names[] = {"inuse", "health", "takedamage", "parent", "client"};
  double *field_slots[] = {&out->base.field_inuse, &out->base.field_health, &out->base.field_takedamage,
                           &out->base.field_parent, &out->field_client};
  for (int i = 0; i < 5; i++) {
    qa_save_reader at = field(&fields, field_names[i]);
    int64_t offset = aligned(&at, common.entity_stride, 4);
    if (offset < qa_qvm_shared_entity_bytes(common.abi_profile))
      qa_save_reader_fail(&at, "private field overlaps the public entity ABI");
    *field_slots[i] = (double)offset;
    qa_save_reader_destroy(&at);
  }
  qa_save_reader_destroy(&fields);
  qa_save_reader callbacks = field(reader, "callbacks");
  out->base.callback_allocate = (double)entry_field(&callbacks, "allocate", artifact);
  out->base.callback_free = (double)entry_field(&callbacks, "free", artifact);
  out->base.callback_damage = (double)entry_field(&callbacks, "damage", artifact);
  qa_save_reader_destroy(&callbacks);
  qa_save_reader reactions = field(reader, "reactions");
  const char *reaction_names[] = {"flags", "pain", "die"};
  double *reaction_slots[] = {&out->reactions.flags, &out->reactions.pain, &out->reactions.die};
  for (int i = 0; i < 3; i++) {
    qa_save_reader at = field(&reactions, reaction_names[i]);
    int64_t offset = aligned(&at, common.entity_stride, 4);
    if (offset < qa_qvm_shared_entity_bytes(common.abi_profile))
      qa_save_reader_fail(&at, "private field overlaps the public entity ABI");
    *reaction_slots[i] = (double)offset;
    qa_save_reader_destroy(&at);
  }
  qa_save_reader pain_call = field(&reactions, "painCall");
  out->reactions.pain_call = read_reaction_call(&pain_call);
  qa_save_reader_destroy(&pain_call);
  qa_save_reader die_call = field(&reactions, "dieCall");
  out->reactions.die_call = read_reaction_call(&die_call);
  qa_save_reader_destroy(&die_call);
  qa_save_reader_destroy(&reactions);
  out->grapple_damage_method = (double)integer_field(reader, "grappleDamageMethod", 0, 9007199254740991.0);
  qa_save_reader armor = field(reader, "armor");
  out->armor.check_armor = (double)entry_field(&armor, "checkArmor", artifact);
  qa_save_reader armor_call = field(&armor, "call");
  qa_save_reader armor_roles = field(&armor_call, "roles");
  const char *armor_names[] = {"target", "amount", "flags"};
  int64_t armor_positions[3];
  qa_qvm_combat_role_index *armor_role_values = xmalloc(3 * sizeof *armor_role_values);
  out->armor.call.roles = armor_role_values;
  out->armor.call.role_count = 3;
  for (int i = 0; i < 3; i++) {
    armor_role_values[i].role = armor_names[i];
    armor_role_values[i].index = (double)integer_field(&armor_roles, armor_names[i], 0, 61);
    armor_positions[i] = (int64_t)armor_role_values[i].index;
  }
  qa_save_reader_destroy(&armor_roles);
  qa_save_reader armor_extras = field(&armor_call, "extras");
  out->armor.call.extras = read_extras(&armor_extras, &out->armor.call.extra_count);
  qa_save_reader_destroy(&armor_extras);
  out->armor.points_stat = (double)integer_field(&armor, "pointsStat", 0, 15);
  qa_save_reader protection = field(&armor, "protection");
  out->armor.protection = read_fraction(&protection);
  qa_save_reader_destroy(&protection);
  qa_save_reader tiers = field(&armor, "tiers");
  if (!qa_save_reader_is_null(&tiers)) {
    out->armor.has_tiers = true;
    out->armor.tiers.stat = (double)integer_field(&tiers, "stat", 0, 15);
    qa_save_reader when_any = field(&tiers, "whenAny");
    size_t when_count = qa_save_reader_list_count(&when_any);
    qa_qvm_armor_tier_test *when_entries = xmalloc(when_count * sizeof *when_entries);
    out->armor.tiers.when_any = when_entries;
    out->armor.tiers.when_any_count = when_count;
    for (size_t i = 0; i < when_count; i++) {
      qa_save_reader value = item_at(&when_any, i);
      when_entries[i].offset = (double)aligned_field(&value, "offset", data_bytes, 4);
      qa_save_reader comparison = field(&value, "comparison");
      static const char *const comparisons[] = {"equal", "not-equal"};
      when_entries[i].comparison = qa_save_reader_choice_string(&comparison, comparisons, 2);
      qa_save_reader_destroy(&comparison);
      when_entries[i].value = (double)integer_field(&value, "value", -2147483648.0, 2147483647.0);
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&when_any);
    qa_save_reader tier_values = field(&tiers, "values");
    size_t tier_count = qa_save_reader_list_count(&tier_values);
    qa_qvm_armor_tier_value *tier_entries = xmalloc(tier_count * sizeof *tier_entries);
    out->armor.tiers.values = tier_entries;
    out->armor.tiers.value_count = tier_count;
    for (size_t i = 0; i < tier_count; i++) {
      qa_save_reader value = item_at(&tier_values, i);
      tier_entries[i].tier = (double)integer_field(&value, "tier", -2147483648.0, 2147483647.0);
      qa_save_reader tier_protection = field(&value, "protection");
      tier_entries[i].protection = read_fraction(&tier_protection);
      qa_save_reader_destroy(&tier_protection);
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&tier_values);
    qa_save_reader fallback = field(&tiers, "fallback");
    out->armor.tiers.fallback = read_fraction(&fallback);
    qa_save_reader_destroy(&fallback);
  }
  const char *error = NULL;
  if (!validate_combat_call(damage_positions, 8, out->base.damage_call.extras, out->base.damage_call.extra_count, data_bytes, &error))
    qa_save_reader_fail(&damage_call, "%s", error);
  if (!validate_combat_call(armor_positions, 3, out->armor.call.extras, out->armor.call.extra_count, data_bytes, &error))
    qa_save_reader_fail(&armor_call, "%s", error);
  qa_save_reader_destroy(&tiers);
  qa_save_reader_destroy(&armor_call);
  qa_save_reader_destroy(&armor);
  qa_save_reader_destroy(&damage_call);
  if (out->state.mass.is_constant && out->state.mass.value < 0) {
    qa_save_reader at = field(reader, "state");
    qa_save_reader at_mass = field(&at, "mass");
    qa_save_reader_fail(&at_mass, "source mass must be nonnegative");
  }
  for (size_t i = 0; i < team_count; i++)
    for (size_t j = i + 1; j < team_count; j++)
      if (out->state.team.values[i].value == out->state.team.values[j].value) {
        qa_save_reader at = field(reader, "state");
        qa_save_reader at_team = field(&at, "team");
        qa_save_reader_fail(&at_team, "source team values must be unique");
      }
  double state_flags[] = {out->state.flags.notarget, out->state.flags.invulnerable, out->state.flags.no_knockback};
  for (int i = 0; i < 3; i++)
    for (int j = i + 1; j < 3; j++)
      if (state_flags[i] == state_flags[j]) qa_save_reader_fail(reader, "source combat flags overlap");
  double damage_flag_values[] = {out->damage_flags.radius, out->damage_flags.no_armor,
      out->damage_flags.no_knockback, out->damage_flags.no_protection, out->damage_flags.no_team_protection};
  for (int i = 0; i < 5; i++)
    for (int j = i + 1; j < 5; j++)
      if (damage_flag_values[i] == damage_flag_values[j]) qa_save_reader_fail(reader, "source combat flags overlap");
  if (out->armor.has_tiers) {
    bool repeated_tier = false;
    for (size_t i = 0; i < out->armor.tiers.value_count; i++)
      for (size_t j = i + 1; j < out->armor.tiers.value_count; j++)
        if (out->armor.tiers.values[i].tier == out->armor.tiers.values[j].tier) repeated_tier = true;
    if (out->armor.tiers.stat == out->armor.points_stat || out->armor.tiers.when_any_count == 0 ||
        out->armor.tiers.value_count == 0 || repeated_tier) {
      qa_save_reader at = field(reader, "armor");
      qa_save_reader at_tiers = field(&at, "tiers");
      qa_save_reader_fail(&at_tiers, "armor tier declaration is empty or ambiguous");
    }
  }
}

void qa_qvm_input_definition_free(qa_qvm_input_definition *definition) {
  free((void *)definition->intermission);
  memset(definition, 0, sizeof *definition);
}

void qa_qvm_primary_weapon_profile_free(qa_qvm_primary_weapon_profile *profile) {
  free((void *)profile->stage.predicates);
  free((void *)profile->stage.settled);
  free((void *)profile->stage.selection.values);
  free((void *)profile->stage.request.accepted);
  free((void *)profile->availability.excluded);
  free((void *)profile->equipment_contexts);
  free((void *)profile->delay.inputs);
  free((void *)profile->teleport.region.inputs);
  free((void *)profile->teleport.objectives.inputs);
  free((void *)profile->stage.dispatcher.actor.pointer.indirections.values);
  memset(profile, 0, sizeof *profile);
}

void qa_qvm_primary_combat_profile_free(qa_qvm_primary_combat_profile *profile) {
  free((void *)profile->base.damage_call.roles);
  free((void *)profile->base.damage_call.extras);
  free((void *)profile->state.team.values);
  free((void *)profile->armor.call.roles);
  free((void *)profile->armor.call.extras);
  free((void *)profile->armor.tiers.when_any);
  free((void *)profile->armor.tiers.values);
  memset(profile, 0, sizeof *profile);
}
