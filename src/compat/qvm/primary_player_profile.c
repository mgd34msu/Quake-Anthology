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

static const qa_qvmy_region_instruction *insn(const qa_qvmy_artifact *artifact, int64_t index) {
  if (index < 0 || (uint64_t)index >= artifact->instruction_count) return NULL;
  return &artifact->instructions[index];
}

static int64_t read_entry(qa_save_reader *reader, const qa_qvmy_artifact *artifact) {
  int64_t value = (int64_t)qa_save_reader_integer_min(reader, 0);
  const qa_qvmy_region_instruction *instruction = insn(artifact, value);
  if (instruction == NULL || instruction->opcode != QA_QVMY_OP_ENTER)
    qa_save_reader_fail(reader, "not an original function entry");
  return value;
}

static int64_t entry_field(const qa_save_reader *reader, const char *name, const qa_qvmy_artifact *artifact) {
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

static layout read_layout(const qa_save_reader *reader, const qa_qvmy_artifact *artifact) {
  if (strcmp(artifact->role, "qagame") != 0)
    qa_save_reader_fail(reader, "primary player services require a qagame ABI");
  layout result;
  result.module = artifact->module;
  result.abi_profile = artifact->has_abi_profile ? artifact->abi_profile : "q3-modern";
  result.entity_stride = integer_field(reader, "entityStride", qa_qvm_shared_entity_bytes(result.abi_profile),
                                       (double)artifact->allocated_data_length);
  result.client_stride = integer_field(reader, "clientStride", qa_qvm_player_state_bytes(result.abi_profile),
                                       (double)artifact->allocated_data_length);
  if (result.entity_stride % 4 != 0 || result.client_stride % 4 != 0)
    qa_save_reader_fail(reader, "source record strides must be aligned");
  return result;
}

static qa_qvmy_primary_region read_region(const qa_save_reader *reader) {
  qa_qvmy_primary_region result = {integer_field(reader, "entry", 0, 9007199254740991.0),
                                   integer_field(reader, "join", 0, 9007199254740991.0)};
  return result;
}

static void read_evaluation(const qa_save_reader *reader, qa_qvm_region_evaluation *out, int64_t **out_inputs) {
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
  *out_inputs = inputs;
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

void qa_qvm_read_primary_input(const qa_save_reader *reader, const qa_qvmy_artifact *artifact,
                               qa_qvmy_input_definition *out) {
  memset(out, 0, sizeof *out);
  layout common = read_layout(reader, artifact);
  out->module = common.module;
  out->abi_profile = common.abi_profile;
  out->entity_stride = common.entity_stride;
  out->client_stride = common.client_stride;
  qa_save_reader modes = field(reader, "movementModes");
  if (!qa_save_reader_is_undefined(&modes)) {
    out->has_movement_modes = true;
    out->mode_normal = any_integer_field(&modes, "normal");
    out->mode_noclip = any_integer_field(&modes, "noclip");
    out->mode_freeze = any_integer_field(&modes, "freeze");
  }
  qa_save_reader_destroy(&modes);
  out->client_pointer = aligned_field(reader, "clientPointer", common.entity_stride, 4);
  qa_save_reader intermission = field(reader, "intermission");
  size_t count = qa_save_reader_list_count(&intermission);
  out->intermission = xmalloc(count * sizeof *out->intermission);
  out->intermission_count = count;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value = item_at(&intermission, i);
    out->intermission[i] = (int64_t)qa_save_reader_integer(&value);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&intermission);
  qa_save_reader entries = field(reader, "entries");
  out->entry_client_think = entry_field(&entries, "clientThink", artifact);
  out->entry_run_client = entry_field(&entries, "runClient", artifact);
  out->entry_client_spawn = entry_field(&entries, "clientSpawn", artifact);
  out->entry_move = entry_field(&entries, "move", artifact);
  out->entry_slice = entry_field(&entries, "slice", artifact);
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

void qa_qvm_read_primary_weapons(const qa_save_reader *reader, const qa_qvmy_artifact *artifact,
                                 const qa_qvmy_catalog_entry *catalog, size_t catalog_count, bool has_catalog,
                                 qa_qvmy_primary_weapon_profile *out) {
  memset(out, 0, sizeof *out);
  layout common = read_layout(reader, artifact);
  out->module = common.module;
  out->abi_profile = common.abi_profile;
  out->entity_stride = common.entity_stride;
  out->client_stride = common.client_stride;
  int64_t data_bytes = artifact->data_length + artifact->literal_length + artifact->bss_length;
  qa_save_reader contexts_reader = field(reader, "equipmentContexts");
  size_t context_count = qa_save_reader_list_count(&contexts_reader);
  out->equipment_contexts = xmalloc(context_count * sizeof *out->equipment_contexts);
  out->equipment_context_count = context_count;
  for (size_t i = 0; i < context_count; i++) {
    qa_save_reader value = item_at(&contexts_reader, i);
    qa_save_reader provider = field(&value, "provider");
    out->equipment_contexts[i].provider = qa_namespaced(&provider);
    qa_save_reader_destroy(&provider);
    qa_save_reader item_reader = field(&value, "item");
    if (qa_save_reader_is_null(&item_reader)) {
      out->equipment_contexts[i].has_item = false;
      out->equipment_contexts[i].item = NULL;
    } else {
      out->equipment_contexts[i].has_item = true;
      out->equipment_contexts[i].item = qa_namespaced(&item_reader);
    }
    qa_save_reader_destroy(&item_reader);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&contexts_reader);
  for (size_t i = 0; i < context_count; i++)
    for (size_t j = i + 1; j < context_count; j++)
      if (strcmp(out->equipment_contexts[i].provider, out->equipment_contexts[j].provider) == 0) {
        qa_save_reader duplicate = field(reader, "equipmentContexts");
        qa_save_reader_fail(&duplicate, "duplicate equipment source context");
      }
  qa_save_reader match_reader = field(reader, "match");
  if (!qa_save_reader_is_undefined(&match_reader)) {
    out->has_match = true;
    out->match = qa_read_source_primary_match(&match_reader, (double)common.client_stride);
  }
  qa_save_reader_destroy(&match_reader);
  out->client_pointer = aligned_field(reader, "clientPointer", common.entity_stride, 4);
  out->max_health = aligned_field(reader, "maxHealth", common.client_stride, 4);
  out->persistent_max_health = aligned_field(reader, "persistentMaxHealth", common.client_stride, 4);
  qa_save_reader stage = field(reader, "stage");
  qa_save_reader dispatcher = field(&stage, "dispatcher");
  out->dispatcher_entry = entry_field(&dispatcher, "entry", artifact);
  qa_save_reader actor_reader = field(&dispatcher, "actor");
  qa_save_reader actor_record = field(&actor_reader, "record");
  out->dispatcher_actor.record = qa_save_reader_literal_string(&actor_record, "client");
  qa_save_reader_destroy(&actor_record);
  qa_save_reader pointer_reader = field(&actor_reader, "pointer");
  out->dispatcher_actor.pointer = read_source_pointer(&pointer_reader, data_bytes);
  qa_save_reader_destroy(&pointer_reader);
  qa_save_reader_destroy(&actor_reader);
  qa_save_reader_destroy(&dispatcher);
  qa_save_reader predicates_reader = field(&stage, "predicates");
  size_t predicate_count = qa_save_reader_list_count(&predicates_reader);
  out->predicates = xmalloc(predicate_count * sizeof *out->predicates);
  out->predicate_count = predicate_count;
  for (size_t i = 0; i < predicate_count; i++) {
    qa_save_reader value = item_at(&predicates_reader, i);
    out->predicates[i].instruction = (double)integer_field(&value, "instruction", 0, 9007199254740991.0);
    qa_save_reader unselected = field(&value, "unselected");
    out->predicates[i].unselected = qa_save_reader_boolean(&unselected);
    qa_save_reader_destroy(&unselected);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&predicates_reader);
  qa_save_reader settled_reader = field(&stage, "settled");
  out->settled = read_tests(&settled_reader, common.client_stride, &out->settled_count);
  qa_save_reader_destroy(&settled_reader);
  qa_save_reader selection = field(&stage, "selection");
  qa_save_reader selection_field = field(&selection, "field");
  out->selection_field = read_client_field(&selection_field, common.client_stride);
  qa_save_reader_destroy(&selection_field);
  qa_save_reader values_reader = field(&selection, "values");
  size_t value_count = qa_save_reader_list_count(&values_reader);
  out->selection_values = xmalloc(value_count * sizeof *out->selection_values);
  out->selection_value_count = value_count;
  for (size_t i = 0; i < value_count; i++) {
    qa_save_reader value = item_at(&values_reader, i);
    out->selection_values[i].value = (double)integer_field(&value, "value", 1, 2147483647.0);
    qa_save_reader item_reader = field(&value, "item");
    out->selection_values[i].item = qa_namespaced(&item_reader);
    qa_save_reader_destroy(&item_reader);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&values_reader);
  qa_save_reader request = field(&stage, "request");
  out->request_entry = entry_field(&request, "entry", artifact);
  out->request_argument = integer_field(&request, "argument", 0, 61);
  qa_save_reader accepted_reader = field(&request, "accepted");
  out->request_accepted = read_tests(&accepted_reader, common.client_stride, &out->request_accepted_count);
  qa_save_reader_destroy(&accepted_reader);
  qa_save_reader_destroy(&request);
  qa_save_reader_destroy(&selection);
  qa_save_reader_destroy(&stage);
  qa_save_reader movement = field(reader, "equipmentMovement");
  qa_save_reader body_trace = field(&movement, "bodyTrace");
  if (!qa_save_reader_is_undefined(&body_trace)) {
    out->has_body_trace = true;
    out->body_trace_callback = aligned_field(&body_trace, "callback", artifact->allocated_data_length, 4);
    out->body_trace_mask = aligned_field(&body_trace, "mask", artifact->allocated_data_length, 4);
  }
  qa_save_reader_destroy(&body_trace);
  out->move_entry = entry_field(&movement, "move", artifact);
  out->slice_entry = entry_field(&movement, "slice", artifact);
  out->duck_entry = entry_field(&movement, "duck", artifact);
  out->movement_global = aligned_field(&movement, "movementGlobal", data_bytes, 4);
  qa_save_reader locomotion = field(&movement, "locomotion");
  out->locomotion = read_region(&locomotion);
  qa_save_reader_destroy(&locomotion);
  out->mins_offset = aligned_field(&movement, "mins", artifact->allocated_data_length, 12);
  out->maxs_offset = aligned_field(&movement, "maxs", artifact->allocated_data_length, 12);
  qa_save_reader_destroy(&movement);
  qa_save_reader availability = field(reader, "availability");
  out->availability_movement_type = aligned_field(&availability, "movementType", common.client_stride, 4);
  qa_save_reader excluded_reader = field(&availability, "excluded");
  size_t excluded_count = qa_save_reader_list_count(&excluded_reader);
  out->availability_excluded = xmalloc(excluded_count * sizeof *out->availability_excluded);
  out->availability_excluded_count = excluded_count;
  for (size_t i = 0; i < excluded_count; i++) {
    qa_save_reader value = item_at(&excluded_reader, i);
    out->availability_excluded[i] = (int64_t)qa_save_reader_integer(&value);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&excluded_reader);
  out->availability_health = aligned_field(&availability, "health", common.client_stride, 4);
  out->availability_team = aligned_field(&availability, "team", common.client_stride, 4);
  out->availability_spectator_team = any_integer_field(&availability, "spectatorTeam");
  out->availability_flags = aligned_field(&availability, "flags", common.client_stride, 4);
  out->availability_respawn_flag = integer_field(&availability, "respawnFlag", 1, 2147483647.0);
  qa_save_reader_destroy(&availability);
  qa_save_reader powers = field(reader, "powerups");
  out->powerup_quad = aligned_field(&powers, "quad", common.client_stride, 4);
  out->powerup_haste = aligned_field(&powers, "haste", common.client_stride, 4);
  out->powerup_flight = aligned_field(&powers, "flight", common.client_stride, 4);
  qa_save_reader_destroy(&powers);
  qa_save_reader torso = field(reader, "torsoAnimation");
  out->torso_entry = entry_field(&torso, "entry", artifact);
  out->torso_attack = integer_field(&torso, "attack", 0, 9007199254740991.0);
  out->torso_melee = integer_field(&torso, "melee", 0, 9007199254740991.0);
  qa_save_reader_destroy(&torso);
  qa_save_reader water = field(reader, "waterLevel");
  out->water_entity_offset = aligned_field(&water, "entityOffset", common.entity_stride, 4);
  out->water_movement_offset = aligned_field(&water, "movementOffset", artifact->allocated_data_length, 4);
  qa_save_reader_destroy(&water);
  qa_save_reader damage = field(reader, "damageFactor");
  out->damage_entry = entry_field(&damage, "entry", artifact);
  out->damage_result = aligned_field(&damage, "result", data_bytes, 4);
  qa_save_reader stop = field(&damage, "stop");
  out->damage_stop = read_region(&stop);
  qa_save_reader_destroy(&stop);
  qa_save_reader_destroy(&damage);
  qa_save_reader delay_reader = field(reader, "delay");
  read_evaluation(&delay_reader, &out->delay, &out->delay_inputs);
  qa_save_reader_destroy(&delay_reader);
  qa_save_reader delay = field(reader, "delayPlayer");
  out->delay_movement_global = aligned_field(&delay, "movementGlobal", data_bytes, 4);
  out->delay_player_offset = aligned_field(&delay, "playerOffset", artifact->allocated_data_length, 4);
  qa_save_reader_destroy(&delay);
  qa_save_reader teleport = field(reader, "teleport");
  out->teleport_entry = entry_field(&teleport, "entry", artifact);
  qa_save_reader teleport_region = field(&teleport, "region");
  read_evaluation(&teleport_region, &out->teleport_region, &out->teleport_region_inputs);
  qa_save_reader_destroy(&teleport_region);
  qa_save_reader teleport_objectives = field(&teleport, "objectives");
  read_evaluation(&teleport_objectives, &out->teleport_objectives, &out->teleport_objectives_inputs);
  qa_save_reader_destroy(&teleport_objectives);
  out->teleport_spawn = entry_field(&teleport, "spawn", artifact);
  out->teleport_view = entry_field(&teleport, "view", artifact);
  qa_save_reader_destroy(&teleport);
  qa_save_reader drop = field(reader, "drop");
  out->drop_entry = entry_field(&drop, "entry", artifact);
  out->drop_argument = integer_field(&drop, "argument", 0, 61);
  out->drop_weapon = aligned_field(&drop, "weapon", common.entity_stride, 4);
  qa_save_reader ammo = field(&drop, "ammo");
  if (ammo.value != NULL && ammo.value->kind == QA_SAVE_STRING && strcmp(ammo.value->text, "inventory") == 0) {
    (void)qa_save_reader_literal_string(&ammo, "inventory");
    out->drop_ammo_inventory = true;
  } else {
    out->drop_ammo_offset = aligned(&ammo, common.client_stride, 64);
  }
  qa_save_reader_destroy(&ammo);
  qa_save_reader drop_region = field(&drop, "region");
  out->drop_region = read_region(&drop_region);
  qa_save_reader_destroy(&drop_region);
  qa_save_reader_destroy(&drop);
  qa_save_reader give = field(reader, "give");
  out->give_entry = entry_field(&give, "entry", artifact);
  out->give_argument = integer_field(&give, "argument", 0, 61);
  out->give_weapons = integer_field(&give, "weapons", 0, 9007199254740991.0);
  out->give_ammo = integer_field(&give, "ammo", 0, 9007199254740991.0);
  qa_save_reader named = field(&give, "named");
  out->give_named_region = read_region(&named);
  out->give_named_name = integer_field(&named, "name", 0, 9007199254740991.0);
  out->give_named_item = integer_field(&named, "item", 0, 9007199254740991.0);
  qa_save_reader_destroy(&named);
  qa_save_reader_destroy(&give);
  bool repeated = false;
  for (size_t i = 0; i < value_count; i++)
    for (size_t j = i + 1; j < value_count; j++)
      if (out->selection_values[i].value == out->selection_values[j].value ||
          strcmp(out->selection_values[i].item, out->selection_values[j].item) == 0)
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
          if (catalog[j].weapon == (int64_t)out->selection_values[i].value &&
              strcmp(catalog[j].item, out->selection_values[i].item) == 0)
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
  if (!out->drop_ammo_inventory) {
    for (size_t i = 0; i < value_count; i++) {
      if (out->drop_ammo_offset + (int64_t)out->selection_values[i].value * 4 + 4 > common.client_stride) {
        qa_save_reader at = field(reader, "drop");
        qa_save_reader_fail(&at, "original drop ammo indexing exceeds its declared client record");
      }
    }
  }
  qa_qvm_weapon_stage stage_view;
  memset(&stage_view, 0, sizeof stage_view);
  stage_view.dispatcher.entry = (double)out->dispatcher_entry;
  stage_view.dispatcher.actor = out->dispatcher_actor;
  stage_view.predicates = out->predicates;
  stage_view.predicate_count = out->predicate_count;
  stage_view.settled = out->settled;
  stage_view.settled_count = out->settled_count;
  stage_view.selection.field = out->selection_field;
  stage_view.selection.values = out->selection_values;
  stage_view.selection.value_count = out->selection_value_count;
  stage_view.request.entry = (double)out->request_entry;
  stage_view.request.argument = (double)out->request_argument;
  stage_view.request.accepted = out->request_accepted;
  stage_view.request.accepted_count = out->request_accepted_count;
  qa_qvm_validate_weapon_dispatcher(&stage_view, artifact->instructions, artifact->instruction_count);
  qa_qvm_qualify_region(artifact->instructions, artifact->instruction_count, out->slice_entry,
                        out->locomotion.entry, out->locomotion.join);
  qa_qvm_qualify_region(artifact->instructions, artifact->instruction_count, out->damage_entry,
                        out->damage_stop.entry, out->damage_stop.join);
  qa_qvm_qualify_region_evaluation(artifact->instructions, artifact->instruction_count,
                                    out->dispatcher_entry, &out->delay, "source");
  qa_qvm_qualify_region_evaluation(artifact->instructions, artifact->instruction_count,
                                    out->teleport_entry, &out->teleport_region, "source");
  qa_qvm_qualify_region_evaluation(artifact->instructions, artifact->instruction_count,
                                    out->teleport_entry, &out->teleport_objectives, "source");
  qa_qvm_qualify_region(artifact->instructions, artifact->instruction_count, out->give_entry,
                        out->give_named_region.entry, out->give_named_region.join);
  qa_qvm_qualify_region(artifact->instructions, artifact->instruction_count, out->drop_entry,
                        out->drop_region.entry, out->drop_region.join);
  if (out->delay.input_count != 1 || !out->delay.has_result || out->teleport_region.input_count != 0 ||
      out->teleport_objectives.input_count != 0)
    qa_save_reader_fail(reader, "source effect region inputs differ from the primary player ABI");
  const qa_qvmy_region_instruction *give_entry = insn(artifact, out->give_entry);
  if (give_entry == NULL || give_entry->opcode != QA_QVMY_OP_ENTER) {
    qa_save_reader at = field(reader, "give");
    qa_save_reader_fail(&at, "give entry disappeared");
  }
  int64_t named_offsets[] = {out->give_named_name, out->give_named_item};
  for (int i = 0; i < 2; i++) {
    if (named_offsets[i] < 8 || named_offsets[i] % 4 != 0 || named_offsets[i] + 4 > give_entry->operand) {
      qa_save_reader at = field(reader, "give");
      qa_save_reader at_named = field(&at, "named");
      qa_save_reader_fail(&at_named, "named grant local exceeds its original function frame");
    }
  }
  int64_t entries[] = {out->dispatcher_entry, out->request_entry, out->duck_entry, out->give_entry,
                       out->drop_entry, out->damage_entry, out->teleport_entry};
  for (int i = 0; i < 7; i++)
    for (int j = i + 1; j < 7; j++)
      if (entries[i] == entries[j])
        qa_save_reader_fail(reader, "primary weapon interfaces overlap original function ownership");
  int64_t completions[] = {out->give_weapons, out->give_ammo};
  for (int i = 0; i < 2; i++) {
    int64_t pc = completions[i];
    const qa_qvmy_region_instruction *instruction = insn(artifact, pc);
    int64_t owner = pc;
    while (owner >= 0) {
      const qa_qvmy_region_instruction *candidate = insn(artifact, owner);
      if (candidate != NULL && candidate->opcode == QA_QVMY_OP_ENTER) break;
      owner--;
    }
    if (owner != out->give_entry || instruction == NULL || instruction->opcode < QA_QVMY_OP_EQ ||
        instruction->opcode > QA_QVMY_OP_GEF || pc <= out->give_entry) {
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

void qa_qvm_read_primary_combat(const qa_save_reader *reader, const qa_qvmy_artifact *artifact,
                                qa_qvmy_primary_combat_profile *out) {
  memset(out, 0, sizeof *out);
  layout common = read_layout(reader, artifact);
  out->module = common.module;
  out->abi_profile = common.abi_profile;
  out->entity_stride = common.entity_stride;
  out->client_stride = common.client_stride;
  int64_t data_bytes = artifact->data_length + artifact->literal_length + artifact->bss_length;
  qa_save_reader damage_call = field(reader, "damageCall");
  qa_save_reader damage_roles = field(&damage_call, "roles");
  const char *damage_names[] = {"target", "inflictor", "attacker", "direction", "point", "amount", "flags", "method"};
  int64_t damage_positions[8];
  for (int i = 0; i < 8; i++) {
    out->damage_roles[i].role = damage_names[i];
    out->damage_roles[i].index = (double)integer_field(&damage_roles, damage_names[i], 0, 61);
    damage_positions[i] = (int64_t)out->damage_roles[i].index;
  }
  qa_save_reader_destroy(&damage_roles);
  qa_save_reader damage_extras = field(&damage_call, "extras");
  out->damage_extras = read_extras(&damage_extras, &out->damage_extra_count);
  qa_save_reader_destroy(&damage_extras);
  qa_save_reader state = field(reader, "state");
  out->state_health_stat = integer_field(&state, "healthStat", 0, 15);
  qa_save_reader team = field(&state, "team");
  out->team_persistent_stat = integer_field(&team, "persistentStat", 0, 15);
  qa_save_reader team_values = field(&team, "values");
  size_t team_count = qa_save_reader_list_count(&team_values);
  out->team_values = xmalloc(team_count * sizeof *out->team_values);
  out->team_value_count = team_count;
  for (size_t i = 0; i < team_count; i++) {
    qa_save_reader value = item_at(&team_values, i);
    out->team_values[i].value = integer_field(&value, "value", -2147483648.0, 2147483647.0);
    qa_save_reader team_name = field(&value, "team");
    out->team_values[i].team = qa_namespaced(&team_name);
    qa_save_reader_destroy(&team_name);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&team_values);
  qa_save_reader_destroy(&team);
  qa_save_reader flags = field(&state, "flags");
  qa_save_reader *flag_readers[3];
  qa_save_reader flag_storage[3];
  const char *flag_names[] = {"notarget", "invulnerable", "noKnockback"};
  int64_t *flag_slots[] = {&out->flag_notarget, &out->flag_invulnerable, &out->flag_no_knockback};
  for (int i = 0; i < 3; i++) {
    flag_storage[i] = field(&flags, flag_names[i]);
    flag_readers[i] = &flag_storage[i];
    int64_t mask = integer(flag_readers[i], 1, 4294967295.0);
    if ((mask & (mask - 1)) != 0) qa_save_reader_fail(flag_readers[i], "expected an individual source flag");
    *flag_slots[i] = mask;
    qa_save_reader_destroy(flag_readers[i]);
  }
  qa_save_reader_destroy(&flags);
  qa_save_reader mass = field(&state, "mass");
  qa_save_reader mass_kind = field(&mass, "kind");
  static const char *const mass_kinds[] = {"constant", "entity"};
  const char *mass_kind_name = qa_save_reader_choice_string(&mass_kind, mass_kinds, 2);
  qa_save_reader_destroy(&mass_kind);
  if (strcmp(mass_kind_name, "constant") == 0) {
    out->mass_is_constant = true;
    qa_save_reader value = field(&mass, "value");
    out->mass_value = qa_save_reader_finite(&value);
    qa_save_reader_destroy(&value);
  } else {
    qa_save_reader offset = field(&mass, "offset");
    out->mass_offset = aligned(&offset, common.entity_stride, 4);
    if (out->mass_offset < qa_qvm_shared_entity_bytes(common.abi_profile))
      qa_save_reader_fail(&offset, "private field overlaps the public entity ABI");
    qa_save_reader_destroy(&offset);
    qa_save_reader storage = field(&mass, "storage");
    static const char *const storages[] = {"int32", "float32"};
    out->mass_storage = qa_save_reader_choice_string(&storage, storages, 2);
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
  int64_t *field_slots[] = {&out->field_inuse, &out->field_health, &out->field_takedamage,
                            &out->field_parent, &out->field_client};
  for (int i = 0; i < 5; i++) {
    qa_save_reader at = field(&fields, field_names[i]);
    int64_t offset = aligned(&at, common.entity_stride, 4);
    if (offset < qa_qvm_shared_entity_bytes(common.abi_profile))
      qa_save_reader_fail(&at, "private field overlaps the public entity ABI");
    *field_slots[i] = offset;
    qa_save_reader_destroy(&at);
  }
  qa_save_reader_destroy(&fields);
  qa_save_reader callbacks = field(reader, "callbacks");
  out->callback_allocate = entry_field(&callbacks, "allocate", artifact);
  out->callback_free = entry_field(&callbacks, "free", artifact);
  out->callback_damage = entry_field(&callbacks, "damage", artifact);
  qa_save_reader_destroy(&callbacks);
  qa_save_reader reactions = field(reader, "reactions");
  const char *reaction_names[] = {"flags", "pain", "die"};
  int64_t *reaction_slots[] = {&out->reaction_flags, &out->reaction_pain, &out->reaction_die};
  for (int i = 0; i < 3; i++) {
    qa_save_reader at = field(&reactions, reaction_names[i]);
    int64_t offset = aligned(&at, common.entity_stride, 4);
    if (offset < qa_qvm_shared_entity_bytes(common.abi_profile))
      qa_save_reader_fail(&at, "private field overlaps the public entity ABI");
    *reaction_slots[i] = offset;
    qa_save_reader_destroy(&at);
  }
  qa_save_reader pain_call = field(&reactions, "painCall");
  out->pain_call = read_reaction_call(&pain_call);
  qa_save_reader_destroy(&pain_call);
  qa_save_reader die_call = field(&reactions, "dieCall");
  out->die_call = read_reaction_call(&die_call);
  qa_save_reader_destroy(&die_call);
  qa_save_reader_destroy(&reactions);
  out->grapple_damage_method = integer_field(reader, "grappleDamageMethod", 0, 9007199254740991.0);
  qa_save_reader armor = field(reader, "armor");
  out->armor_check_armor = entry_field(&armor, "checkArmor", artifact);
  qa_save_reader armor_call = field(&armor, "call");
  qa_save_reader armor_roles = field(&armor_call, "roles");
  const char *armor_names[] = {"target", "amount", "flags"};
  int64_t armor_positions[3];
  for (int i = 0; i < 3; i++) {
    out->armor_roles[i].role = armor_names[i];
    out->armor_roles[i].index = (double)integer_field(&armor_roles, armor_names[i], 0, 61);
    armor_positions[i] = (int64_t)out->armor_roles[i].index;
  }
  qa_save_reader_destroy(&armor_roles);
  qa_save_reader armor_extras = field(&armor_call, "extras");
  out->armor_extras = read_extras(&armor_extras, &out->armor_extra_count);
  qa_save_reader_destroy(&armor_extras);
  out->armor_points_stat = integer_field(&armor, "pointsStat", 0, 15);
  qa_save_reader protection = field(&armor, "protection");
  out->armor_protection = read_fraction(&protection);
  qa_save_reader_destroy(&protection);
  qa_save_reader tiers = field(&armor, "tiers");
  if (!qa_save_reader_is_null(&tiers)) {
    out->has_tiers = true;
    out->tier_stat = integer_field(&tiers, "stat", 0, 15);
    qa_save_reader when_any = field(&tiers, "whenAny");
    size_t when_count = qa_save_reader_list_count(&when_any);
    out->tier_when = xmalloc(when_count * sizeof *out->tier_when);
    out->tier_when_count = when_count;
    for (size_t i = 0; i < when_count; i++) {
      qa_save_reader value = item_at(&when_any, i);
      out->tier_when[i].offset = aligned_field(&value, "offset", data_bytes, 4);
      qa_save_reader comparison = field(&value, "comparison");
      static const char *const comparisons[] = {"equal", "not-equal"};
      out->tier_when[i].comparison = qa_save_reader_choice_string(&comparison, comparisons, 2);
      qa_save_reader_destroy(&comparison);
      out->tier_when[i].value = integer_field(&value, "value", -2147483648.0, 2147483647.0);
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&when_any);
    qa_save_reader tier_values = field(&tiers, "values");
    size_t tier_count = qa_save_reader_list_count(&tier_values);
    out->tier_values = xmalloc(tier_count * sizeof *out->tier_values);
    out->tier_value_count = tier_count;
    for (size_t i = 0; i < tier_count; i++) {
      qa_save_reader value = item_at(&tier_values, i);
      out->tier_values[i].tier = integer_field(&value, "tier", -2147483648.0, 2147483647.0);
      qa_save_reader tier_protection = field(&value, "protection");
      out->tier_values[i].protection = read_fraction(&tier_protection);
      qa_save_reader_destroy(&tier_protection);
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&tier_values);
    qa_save_reader fallback = field(&tiers, "fallback");
    out->tier_fallback = read_fraction(&fallback);
    qa_save_reader_destroy(&fallback);
  }
  const char *error = NULL;
  if (!validate_combat_call(damage_positions, 8, out->damage_extras, out->damage_extra_count, data_bytes, &error))
    qa_save_reader_fail(&damage_call, "%s", error);
  if (!validate_combat_call(armor_positions, 3, out->armor_extras, out->armor_extra_count, data_bytes, &error))
    qa_save_reader_fail(&armor_call, "%s", error);
  qa_save_reader_destroy(&tiers);
  qa_save_reader_destroy(&armor_call);
  qa_save_reader_destroy(&armor);
  qa_save_reader_destroy(&damage_call);
  if (out->mass_is_constant && out->mass_value < 0) {
    qa_save_reader at = field(reader, "state");
    qa_save_reader at_mass = field(&at, "mass");
    qa_save_reader_fail(&at_mass, "source mass must be nonnegative");
  }
  for (size_t i = 0; i < team_count; i++)
    for (size_t j = i + 1; j < team_count; j++)
      if (out->team_values[i].value == out->team_values[j].value) {
        qa_save_reader at = field(reader, "state");
        qa_save_reader at_team = field(&at, "team");
        qa_save_reader_fail(&at_team, "source team values must be unique");
      }
  int64_t state_flags[] = {out->flag_notarget, out->flag_invulnerable, out->flag_no_knockback};
  for (int i = 0; i < 3; i++)
    for (int j = i + 1; j < 3; j++)
      if (state_flags[i] == state_flags[j]) qa_save_reader_fail(reader, "source combat flags overlap");
  double damage_flag_values[] = {out->damage_flags.radius, out->damage_flags.no_armor,
      out->damage_flags.no_knockback, out->damage_flags.no_protection, out->damage_flags.no_team_protection};
  for (int i = 0; i < 5; i++)
    for (int j = i + 1; j < 5; j++)
      if (damage_flag_values[i] == damage_flag_values[j]) qa_save_reader_fail(reader, "source combat flags overlap");
  if (out->has_tiers) {
    bool repeated_tier = false;
    for (size_t i = 0; i < out->tier_value_count; i++)
      for (size_t j = i + 1; j < out->tier_value_count; j++)
        if (out->tier_values[i].tier == out->tier_values[j].tier) repeated_tier = true;
    if (out->tier_stat == out->armor_points_stat || out->tier_when_count == 0 ||
        out->tier_value_count == 0 || repeated_tier) {
      qa_save_reader at = field(reader, "armor");
      qa_save_reader at_tiers = field(&at, "tiers");
      qa_save_reader_fail(&at_tiers, "armor tier declaration is empty or ambiguous");
    }
  }
}

void qa_qvmy_input_definition_free(qa_qvmy_input_definition *definition) {
  free(definition->intermission);
  memset(definition, 0, sizeof *definition);
}

void qa_qvmy_primary_weapon_profile_free(qa_qvmy_primary_weapon_profile *profile) {
  free(profile->predicates);
  free(profile->settled);
  free(profile->selection_values);
  free(profile->request_accepted);
  free(profile->availability_excluded);
  free(profile->equipment_contexts);
  free(profile->delay_inputs);
  free(profile->teleport_region_inputs);
  free(profile->teleport_objectives_inputs);
  free((void *)profile->dispatcher_actor.pointer.indirections.values);
  memset(profile, 0, sizeof *profile);
}

void qa_qvmy_primary_combat_profile_free(qa_qvmy_primary_combat_profile *profile) {
  free(profile->damage_extras);
  free(profile->team_values);
  free(profile->armor_extras);
  free(profile->tier_when);
  free(profile->tier_values);
  memset(profile, 0, sizeof *profile);
}
