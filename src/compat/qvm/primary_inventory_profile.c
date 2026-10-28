/* See include/qa/compat/qvm/primary_inventory_profile.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/primary_inventory_profile.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/operations.h"
#include "qa/compat/qvm/player_record.h"
#include "qa/content/mods/qvm_items.h"
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

static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out)) qa_range_error("QVM memory allocation failed");
  return out;
}

static bool is_int32(double value) {
  return isfinite(value) && value == trunc(value) && value >= -2147483648.0 && value <= 2147483647.0;
}

static int64_t storage_constant(const qa_qvmy_region_instruction *instructions, size_t count, double instruction) {
  int64_t index = (int64_t)instruction;
  if (index < 0 || (uint64_t)index >= count || instructions[index].opcode != QA_QVMY_OP_CONST ||
      instructions[index].operand < 0)
    qa_range_error("QVM item capacity is not its declared original constant");
  return instructions[index].operand;
}

void qa_qvmy_validate_item_storage(const qa_qvm_item_storage *storage, size_t storage_count,
                                   const qa_item_id *items, size_t item_count,
                                   const qa_qvmy_region_instruction *instructions, size_t instruction_count,
                                   int64_t initialized_data_length, int64_t bss_length,
                                   qa_qvmy_item_field_check_fn field_check, void *field_ctx) {
  bool *bound = calloc(item_count == 0 ? 1 : item_count, sizeof *bound);
  if (bound == NULL) qa_range_error("QVM memory allocation failed");
  size_t bound_count = 0;
  for (size_t s = 0; s < storage_count; s++) {
    const qa_qvm_item_storage *value = &storage[s];
    field_check(field_ctx, &value->field, "storage");
    if (strcmp(value->kind, "counter") == 0) {
      bool found = false;
      for (size_t i = 0; i < item_count; i++) {
        if (strcmp(items[i], value->item) == 0 && !bound[i]) {
          bound[i] = true;
          bound_count++;
          found = true;
        }
      }
      if (!found) qa_range_error("QVM item lacks distinct declared storage");
      const qa_qvm_item_capacity *capacity = &value->capacity;
      if (strcmp(capacity->kind, "field") == 0) field_check(field_ctx, &capacity->field, "capacity");
      if (strcmp(capacity->kind, "constant") == 0 && (!is_int32(capacity->value) || capacity->value < 0))
        qa_range_error("QVM item capacity exceeds its source ABI");
      if (strcmp(capacity->kind, "source") == 0) {
        (void)storage_constant(instructions, instruction_count, capacity->instruction);
        for (size_t o = 0; o < capacity->override_count; o++) {
          const qa_qvm_item_capacity_override *override = &capacity->overrides[o];
          (void)storage_constant(instructions, instruction_count, override->instruction);
          if (!is_int32(override->value) || override->address != trunc(override->address) ||
              override->address < 0 || (int64_t)override->address % 4 != 0 ||
              override->address + 4 > (double)(initialized_data_length + bss_length))
            qa_range_error("QVM capacity selector exceeds original source storage");
        }
      }
    } else {
      if (!isfinite(value->private_mask) || value->private_mask != trunc(value->private_mask) ||
          value->private_mask < 0 || value->private_mask > 4294967295.0 || value->item_count == 0)
        qa_range_error("Invalid QVM private inventory mask");
      uint32_t mask = (uint32_t)value->private_mask;
      for (size_t i = 0; i < value->item_count; i++) {
        bool found = false;
        for (size_t j = 0; j < item_count; j++) {
          if (strcmp(items[j], value->items[i].item) == 0 && !bound[j]) {
            bound[j] = true;
            bound_count++;
            found = true;
          }
        }
        if (!found) qa_range_error("QVM item lacks distinct declared storage");
        double item_mask = value->items[i].mask;
        if (!isfinite(item_mask) || item_mask != trunc(item_mask) || item_mask < 1 ||
            item_mask > 2147483648.0 || ((uint32_t)item_mask & ((uint32_t)item_mask - 1)) != 0 ||
            (mask & (uint32_t)item_mask) != 0)
          qa_range_error("QVM packed item masks overlap");
        mask |= (uint32_t)item_mask;
      }
    }
  }
  free(bound);
  if (bound_count != item_count) qa_range_error("QVM item definition has no source storage");
}

typedef struct private_field_state {
  const qa_save_reader *reader;
  const qa_qvmy_inventory_records *records;
  /* Occupied (record, offset) pairs; TS keys are full strings. */
  const char **key_records;
  double *key_offsets;
  const char **usages;
  size_t count;
  size_t capacity;
} private_field_state;

static void private_field_check(void *ctx, const qa_qvm_item_field *field, const char *usage) {
  private_field_state *state = ctx;
  int64_t bytes = strcmp(field->record, "client") == 0 ? state->records->client_stride
                  : strcmp(field->record, "entity") == 0 ? state->records->entity_stride : 0;
  const char *previous = NULL;
  for (size_t i = 0; i < state->count; i++)
    if (strcmp(state->key_records[i], field->record) == 0 && state->key_offsets[i] == field->offset)
      previous = state->usages[i];
  if (!isfinite(field->offset) || field->offset != trunc(field->offset) || field->offset < 0 ||
      (int64_t)field->offset % 4 != 0 || field->offset + 4 > (double)bytes ||
      (previous != NULL && !(strcmp(previous, "capacity") == 0 && strcmp(usage, "capacity") == 0)))
    qa_save_reader_fail(state->reader, "private inventory field is outside or overlaps its source record");
  if (state->count == state->capacity) {
    state->capacity = state->capacity == 0 ? 8 : state->capacity * 2;
    state->key_records = realloc(state->key_records, state->capacity * sizeof *state->key_records);
    state->key_offsets = realloc(state->key_offsets, state->capacity * sizeof *state->key_offsets);
    state->usages = realloc(state->usages, state->capacity * sizeof *state->usages);
    if (state->key_records == NULL || state->key_offsets == NULL || state->usages == NULL)
      qa_range_error("QVM memory allocation failed");
  }
  state->key_records[state->count] = field->record;
  state->key_offsets[state->count] = field->offset;
  state->usages[state->count++] = usage;
}

static const qa_qvmy_region_instruction *instruction_at(const qa_qvmy_artifact *artifact, int64_t index) {
  if (index < 0 || (uint64_t)index >= artifact->instruction_count) return NULL;
  return &artifact->instructions[index];
}

static int64_t read_constant(const qa_qvmy_artifact *artifact, qa_save_reader *at) {
  int64_t index = (int64_t)qa_save_reader_integer_min(at, 0);
  const qa_qvmy_region_instruction *instruction = instruction_at(artifact, index);
  if (instruction == NULL || instruction->opcode != QA_QVMY_OP_CONST)
    qa_save_reader_fail(at, "capacity operand must be an original OP_CONST");
  return instruction->operand;
}

static qa_qvmy_source_word read_word(const qa_qvmy_artifact *artifact, qa_save_reader *at) {
  qa_save_reader kind_reader = field(at, "kind");
  static const char *const kinds[] = {"constant", "weapon", "client", "entity", "client-number"};
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 5);
  qa_save_reader_destroy(&kind_reader);
  qa_qvmy_source_word result = {kind, 0};
  if (strcmp(kind, "constant") == 0) {
    qa_save_reader instruction = field(at, "instruction");
    result.value = read_constant(artifact, &instruction);
    qa_save_reader_destroy(&instruction);
  }
  return result;
}

static int32_t word_value(qa_qvmy_source_word source, int32_t weapon, const qa_qvmy_capacity_context *context) {
  if (strcmp(source.kind, "constant") == 0) return (int32_t)source.value;
  if (strcmp(source.kind, "weapon") == 0) return weapon;
  if (strcmp(source.kind, "client") == 0) return (int32_t)context->client;
  if (strcmp(source.kind, "entity") == 0) return (int32_t)context->entity;
  return (int32_t)context->client_number;
}

void qa_qvm_read_primary_inventory_profile(const qa_save_reader *reader, const qa_qvmy_artifact *artifact,
                                           const qa_qvmy_inventory_records *records,
                                           qa_qvmy_inventory_profile *out) {
  memset(out, 0, sizeof *out);
  out->module = artifact->module;
  out->abi_profile = artifact->has_abi_profile ? artifact->abi_profile : "q3-modern";
  if (strcmp(artifact->role, "qagame") != 0)
    qa_save_reader_fail(reader, "primary inventory declarations require a qagame ABI");
  qa_save_reader storage_reader = field(reader, "storage");
  if (!qa_save_reader_is_undefined(&storage_reader)) {
    if (records == NULL)
      qa_save_reader_fail(reader, "private inventory requires its qualified primary source records");
    size_t count = qa_save_reader_list_count(&storage_reader);
    out->storage = xmalloc(count * sizeof *out->storage);
    out->storage_count = count;
    qa_item_id *items = NULL;
    size_t item_count = 0;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&storage_reader, i);
      out->storage[i] = qa_read_qvm_item_storage(&entry);
      qa_save_reader_destroy(&entry);
      if (strcmp(out->storage[i].kind, "counter") == 0) {
        items = realloc(items, (item_count + 1) * sizeof *items);
        if (items == NULL) qa_range_error("QVM memory allocation failed");
        items[item_count++] = out->storage[i].item;
      } else {
        for (size_t j = 0; j < out->storage[i].item_count; j++) {
          items = realloc(items, (item_count + 1) * sizeof *items);
          if (items == NULL) qa_range_error("QVM memory allocation failed");
          items[item_count++] = out->storage[i].items[j].item;
        }
      }
    }
    /* TS builds a Set; duplicates collapse before validation. */
    size_t unique = 0;
    for (size_t i = 0; i < item_count; i++) {
      bool seen = false;
      for (size_t j = 0; j < unique; j++) if (strcmp(items[j], items[i]) == 0) seen = true;
      if (!seen) items[unique++] = items[i];
    }
    private_field_state state = {reader, records, NULL, NULL, NULL, 0, 0};
    qa_qvmy_validate_item_storage(out->storage, count, items, unique, artifact->instructions,
                                  artifact->instruction_count, (int64_t)artifact->initialized_data_length,
                                  artifact->bss_length, private_field_check, &state);
    free(state.key_records);
    free(state.key_offsets);
    free(state.usages);
    free(items);
    qa_save_reader_destroy(&storage_reader);
    out->is_private = true;
    out->client_stride = records->client_stride;
    out->entity_stride = records->entity_stride;
    return;
  }
  qa_save_reader_destroy(&storage_reader);
  qa_save_reader ammo_reader = field(reader, "ammoOffset");
  int64_t declared_ammo_offset = (int64_t)qa_save_reader_integer_min(&ammo_reader, 0);
  qa_save_reader_destroy(&ammo_reader);
  qa_save_reader capacity = field(reader, "capacity");
  qa_save_reader kind_reader = field(&capacity, "kind");
  static const char *const kinds[] = {"constant", "global", "region", "counter"};
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 4);
  qa_save_reader_destroy(&kind_reader);
  out->capacity_kind = kind;
  qa_save_reader stack_reader = field(&capacity, "stack");
  if (!qa_save_reader_is_undefined(&stack_reader)) {
    qa_save_reader start = field(&stack_reader, "start");
    int64_t start_value = (int64_t)qa_save_reader_integer_min(&start, 0);
    qa_save_reader_destroy(&start);
    qa_save_reader end = field(&stack_reader, "end");
    int64_t end_value = (int64_t)qa_save_reader_integer_min(&end, 0);
    qa_save_reader_destroy(&end);
    out->has_stack = true;
    out->stack.start = start_value;
    out->stack.end = end_value;
    if ((strcmp(kind, "counter") != 0 && strcmp(kind, "region") != 0) || start_value % 4 != 0 ||
        end_value % 4 != 0 || start_value < artifact->data_length + artifact->literal_length ||
        start_value >= end_value || end_value > artifact->allocated_data_length)
      qa_save_reader_fail(&stack_reader, "capacity stack must declare an aligned original BSS reservation for a source query");
  }
  qa_save_reader_destroy(&stack_reader);
  if (strcmp(kind, "constant") == 0) {
    qa_save_reader instruction = field(&capacity, "instruction");
    out->capacity_constant = read_constant(artifact, &instruction);
    qa_save_reader_destroy(&instruction);
    if (out->capacity_constant < 0) qa_save_reader_fail(&capacity, "source ammo capacity cannot be negative");
  } else if (strcmp(kind, "global") == 0) {
    qa_save_reader instruction = field(&capacity, "addressInstruction");
    out->capacity_address = read_constant(artifact, &instruction);
    qa_save_reader_destroy(&instruction);
    if (out->capacity_address < 0 || out->capacity_address % 4 != 0 ||
        out->capacity_address + 4 > artifact->allocated_data_length)
      qa_save_reader_fail(&capacity, "source capacity word is outside aligned module data");
  } else if (strcmp(kind, "counter") == 0) {
    qa_save_reader function_reader = field(&capacity, "function");
    out->capacity_function = (int64_t)qa_save_reader_integer_min(&function_reader, 0);
    qa_save_reader_destroy(&function_reader);
    qa_save_reader functions_reader = field(&capacity, "functions");
    size_t function_count = qa_save_reader_list_count(&functions_reader);
    out->capacity_functions = xmalloc(function_count * sizeof *out->capacity_functions);
    out->capacity_function_count = function_count;
    bool includes_owner = false;
    for (size_t i = 0; i < function_count; i++) {
      qa_save_reader entry = item(&functions_reader, i);
      int64_t index = (int64_t)qa_save_reader_integer_min(&entry, 0);
      const qa_qvmy_region_instruction *instruction = instruction_at(artifact, index);
      if (instruction == NULL || instruction->opcode != QA_QVMY_OP_ENTER)
        qa_save_reader_fail(&entry, "counter operation requires original function entries");
      out->capacity_functions[i] = index;
      if (index == out->capacity_function) includes_owner = true;
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&functions_reader);
    bool distinct = true;
    for (size_t i = 0; i < function_count; i++)
      for (size_t j = i + 1; j < function_count; j++)
        if (out->capacity_functions[i] == out->capacity_functions[j]) distinct = false;
    if (!includes_owner || !distinct)
      qa_save_reader_fail(&capacity, "counter operation must include its entry and distinct helpers");
    qa_save_reader arguments_reader = field(&capacity, "arguments");
    size_t argument_count = qa_save_reader_list_count(&arguments_reader);
    out->capacity_arguments = xmalloc(argument_count * sizeof *out->capacity_arguments);
    out->capacity_argument_count = argument_count;
    for (size_t i = 0; i < argument_count; i++) {
      qa_save_reader entry = item(&arguments_reader, i);
      qa_save_reader entry_kind = field(&entry, "kind");
      bool maximum = !qa_save_reader_is_undefined(&entry_kind) && entry_kind.value != NULL &&
                     entry_kind.value->kind == QA_SAVE_STRING &&
                     strcmp(entry_kind.value->text, "maximum-grant") == 0;
      qa_save_reader_destroy(&entry_kind);
      if (maximum) {
        qa_save_reader literal = field(&entry, "kind");
        (void)qa_save_reader_literal_string(&literal, "maximum-grant");
        qa_save_reader_destroy(&literal);
        out->capacity_arguments[i] = (qa_qvmy_source_word){"maximum-grant", 0};
      } else {
        out->capacity_arguments[i] = read_word(artifact, &entry);
      }
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&arguments_reader);
    if (argument_count > 62)
      qa_save_reader_fail(&capacity, "counter arguments exceed the private source invocation");
    out->ammo_offset = declared_ammo_offset;
  } else {
    qa_save_reader function_reader = field(&capacity, "function");
    out->capacity_function = (int64_t)qa_save_reader_integer_min(&function_reader, 0);
    qa_save_reader_destroy(&function_reader);
    qa_save_reader source = field(&capacity, "region");
    qa_save_reader entry_reader = field(&source, "entry");
    out->capacity_region.entry = (int64_t)qa_save_reader_integer_min(&entry_reader, 0);
    qa_save_reader_destroy(&entry_reader);
    qa_save_reader join_reader = field(&source, "join");
    out->capacity_region.join = (int64_t)qa_save_reader_integer_min(&join_reader, 0);
    qa_save_reader_destroy(&join_reader);
    qa_save_reader inputs_reader = field(&source, "inputs");
    size_t input_count = qa_save_reader_list_count(&inputs_reader);
    out->capacity_region_inputs = xmalloc(input_count * sizeof *out->capacity_region_inputs);
    out->capacity_region.inputs = out->capacity_region_inputs;
    out->capacity_region.input_count = input_count;
    for (size_t i = 0; i < input_count; i++) {
      qa_save_reader entry = item(&inputs_reader, i);
      out->capacity_region_inputs[i] = (int64_t)qa_save_reader_integer_min(&entry, 8);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&inputs_reader);
    qa_save_reader result_reader = field(&source, "result");
    out->capacity_region.has_result = true;
    out->capacity_region.result = (int64_t)qa_save_reader_integer_min(&result_reader, 8);
    qa_save_reader_destroy(&result_reader);
    qa_save_reader_destroy(&source);
    qa_qvm_qualify_region_evaluation(artifact->instructions, artifact->instruction_count,
                                     out->capacity_function, &out->capacity_region, "read-only");
    qa_save_reader arguments_reader = field(&capacity, "arguments");
    size_t argument_count = qa_save_reader_list_count(&arguments_reader);
    out->capacity_arguments = xmalloc(argument_count * sizeof *out->capacity_arguments);
    out->capacity_argument_count = argument_count;
    for (size_t i = 0; i < argument_count; i++) {
      qa_save_reader entry = item(&arguments_reader, i);
      out->capacity_arguments[i] = read_word(artifact, &entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&arguments_reader);
    qa_save_reader live_ins = field(&capacity, "inputs");
    size_t live_count = qa_save_reader_list_count(&live_ins);
    out->capacity_inputs = xmalloc(live_count * sizeof *out->capacity_inputs);
    out->capacity_input_count = live_count;
    for (size_t i = 0; i < live_count; i++) {
      qa_save_reader entry = item(&live_ins, i);
      out->capacity_inputs[i] = read_word(artifact, &entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&live_ins);
    if (argument_count > 62 || live_count != input_count)
      qa_save_reader_fail(&capacity, "capacity arguments and live-ins differ from the original source frame");
  }
  qa_save_reader_destroy(&capacity);
  const char *names[] = {"weaponsOffset", "ammoOffset"};
  int64_t offsets[2];
  for (int i = 0; i < 2; i++) {
    qa_save_reader at = field(reader, names[i]);
    int64_t result = (int64_t)qa_save_reader_integer_min(&at, 0);
    if (result % 4 != 0 || result + (i == 1 ? 64 : 4) > qa_qvm_player_state_bytes(out->abi_profile))
      qa_save_reader_fail(&at, "inventory field exceeds its aligned public player record");
    offsets[i] = result;
    qa_save_reader_destroy(&at);
  }
  out->weapons_offset = offsets[0];
  out->ammo_offset = offsets[1];
}

int32_t qa_qvmy_inventory_capacity(const qa_qvmy_inventory_profile *profile, qa_qvm_memory *memory,
                                   int32_t weapon, const qa_qvmy_capacity_context *context) {
  if (strcmp(profile->capacity_kind, "constant") == 0) return (int32_t)profile->capacity_constant;
  if (strcmp(profile->capacity_kind, "global") == 0) {
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(memory, profile->capacity_address, 4, &view);
    return qa_qvm_writable_view_get_int32(&view, 0, true);
  }
  int32_t *args = xmalloc(profile->capacity_argument_count * sizeof *args);
  for (size_t i = 0; i < profile->capacity_argument_count; i++) {
    args[i] = strcmp(profile->capacity_arguments[i].kind, "maximum-grant") == 0
        ? 0x7fffffff : word_value(profile->capacity_arguments[i], weapon, context);
  }
  int32_t result;
  if (strcmp(profile->capacity_kind, "counter") == 0) {
    result = qa_qvm_module_evaluate_counter(context->module, args, profile->capacity_argument_count,
        profile->capacity_function, context->client + profile->ammo_offset + (int64_t)weapon * 4,
        profile->capacity_functions, profile->capacity_function_count,
        profile->has_stack ? &profile->stack : NULL);
  } else {
    int64_t *inputs = xmalloc(profile->capacity_input_count * sizeof *inputs);
    for (size_t i = 0; i < profile->capacity_input_count; i++)
      inputs[i] = word_value(profile->capacity_inputs[i], weapon, context);
    result = qa_qvm_module_evaluate_region(context->module, args, profile->capacity_argument_count,
        profile->capacity_function, &profile->capacity_region, inputs, profile->capacity_input_count,
        profile->has_stack ? &profile->stack : NULL);
    free(inputs);
  }
  free(args);
  return result;
}

void qa_qvmy_inventory_profile_free(qa_qvmy_inventory_profile *profile) {
  free(profile->capacity_functions);
  free(profile->capacity_arguments);
  free(profile->capacity_region_inputs);
  free(profile->capacity_inputs);
  free(profile->storage);
  memset(profile, 0, sizeof *profile);
}
