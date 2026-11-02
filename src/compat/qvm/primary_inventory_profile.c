/* See include/qa/compat/qvm/primary_inventory_profile.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/primary_inventory_profile.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/item_storage.h"
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

typedef struct private_field_state {
  const qa_save_reader *reader;
  const qa_qvm_inventory_records *records;
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

/* Adapter onto the landed qa_qvm_validate_item_storage validator
 * shape: usage NULL is the TS undefined, defaulted to "storage". */
static void private_field_validator(void *ctx, qa_qvm_item_field field, const char *usage) {
  private_field_check(ctx, &field, usage == NULL ? "storage" : usage);
}

static const qa_qvm_instruction *instruction_at(const qa_resolved_qvm_artifact *artifact, int64_t index) {
  if (index < 0 || (uint64_t)index >= artifact->image.instruction_count) return NULL;
  return &artifact->image.instructions[index];
}

static int64_t read_constant(const qa_resolved_qvm_artifact *artifact, qa_save_reader *at) {
  int64_t index = (int64_t)qa_save_reader_integer_min(at, 0);
  const qa_qvm_instruction *instruction = instruction_at(artifact, index);
  if (instruction == NULL || instruction->opcode != QA_QVMY_OP_CONST)
    qa_save_reader_fail(at, "capacity operand must be an original OP_CONST");
  return instruction->operand;
}

static qa_qvm_source_word read_word(const qa_resolved_qvm_artifact *artifact, qa_save_reader *at) {
  qa_save_reader kind_reader = field(at, "kind");
  static const char *const kinds[] = {"constant", "weapon", "client", "entity", "client-number"};
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 5);
  qa_save_reader_destroy(&kind_reader);
  qa_qvm_source_word result = {kind, 0};
  if (strcmp(kind, "constant") == 0) {
    qa_save_reader instruction = field(at, "instruction");
    result.value = read_constant(artifact, &instruction);
    qa_save_reader_destroy(&instruction);
  }
  return result;
}

/* Parsed public capacity declaration captured by the landed
 * qa_qvm_public_inventory_profile capacity closure. */
typedef struct capacity_state {
  const char *kind; /* constant|global|region|counter */
  int64_t constant;
  int64_t address;
  int64_t function;
  int64_t *functions; /* owned */
  size_t function_count;
  qa_qvm_source_word *arguments; /* owned */
  size_t argument_count;
  qa_qvm_region_evaluation region;
  qa_qvm_source_word *inputs; /* owned */
  size_t input_count;
  bool has_stack;
  qa_qvm_evaluation_stack stack;
  int64_t ammo_offset;
} capacity_state;

static int32_t word_value(qa_qvm_source_word source, int32_t weapon, const qa_qvm_capacity_context *context) {
  if (strcmp(source.kind, "constant") == 0) return (int32_t)source.value;
  if (strcmp(source.kind, "weapon") == 0) return weapon;
  if (strcmp(source.kind, "client") == 0) return (int32_t)context->client;
  if (strcmp(source.kind, "entity") == 0) return (int32_t)context->entity;
  return (int32_t)context->client_number;
}

static double capacity_of(void *ctx, qa_qvm_memory *memory, double weapon_value,
                          qa_qvm_capacity_context context);

void qa_qvm_read_primary_inventory_profile(const qa_save_reader *reader, const qa_resolved_qvm_artifact *artifact,
                                           const qa_qvm_inventory_records *records,
                                           qa_qvm_inventory_profile *out) {
  memset(out, 0, sizeof *out);
  qa_module_identity module = artifact->module;
  qa_qvm_abi_profile abi_profile = artifact->has_abi_profile ? artifact->abi_profile : "q3-modern";
  if (strcmp(artifact->role, "qagame") != 0)
    qa_save_reader_fail(reader, "primary inventory declarations require a qagame ABI");
  qa_save_reader storage_reader = field(reader, "storage");
  if (!qa_save_reader_is_undefined(&storage_reader)) {
    if (records == NULL)
      qa_save_reader_fail(reader, "private inventory requires its qualified primary source records");
    size_t count = qa_save_reader_list_count(&storage_reader);
    qa_qvm_item_storage *storage = xmalloc(count * sizeof *storage);
    qa_item_id *items = NULL;
    size_t item_count = 0;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&storage_reader, i);
      storage[i] = qa_read_qvm_item_storage(&entry);
      qa_save_reader_destroy(&entry);
      if (strcmp(storage[i].kind, "counter") == 0) {
        items = realloc(items, (item_count + 1) * sizeof *items);
        if (items == NULL) qa_range_error("QVM memory allocation failed");
        items[item_count++] = storage[i].item;
      } else {
        for (size_t j = 0; j < storage[i].item_count; j++) {
          items = realloc(items, (item_count + 1) * sizeof *items);
          if (items == NULL) qa_range_error("QVM memory allocation failed");
          items[item_count++] = storage[i].items[j].item;
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
    qa_qvm_image image;
    memset(&image, 0, sizeof image);
    image.instructions = (qa_qvm_instruction *)artifact->image.instructions;
    image.instruction_count = artifact->image.instruction_count;
    image.initialized_length = artifact->image.initialized_length;
    image.bss_length = artifact->image.bss_length;
    qa_qvm_item_field_validator validator = {&state, private_field_validator};
    qa_qvm_validate_item_storage(storage, count, items, unique, &image, validator);
    free(state.key_records);
    free(state.key_offsets);
    free(state.usages);
    free(items);
    qa_save_reader_destroy(&storage_reader);
    qa_qvm_private_inventory_profile *private_profile = xmalloc(sizeof *private_profile);
    memset(private_profile, 0, sizeof *private_profile);
    private_profile->module = module;
    private_profile->abi_profile = abi_profile;
    private_profile->entity_stride = (double)records->entity_stride;
    private_profile->client_stride = (double)records->client_stride;
    private_profile->image = &artifact->image;
    private_profile->storage = storage;
    private_profile->storage_count = count;
    out->private_profile = private_profile;
    return;
  }
  qa_save_reader_destroy(&storage_reader);
  capacity_state *state = xmalloc(sizeof *state);
  memset(state, 0, sizeof *state);
  qa_save_reader capacity = field(reader, "capacity");
  qa_save_reader kind_reader = field(&capacity, "kind");
  static const char *const kinds[] = {"constant", "global", "region", "counter"};
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 4);
  qa_save_reader_destroy(&kind_reader);
  state->kind = kind;
  qa_save_reader stack_reader = field(&capacity, "stack");
  if (!qa_save_reader_is_undefined(&stack_reader)) {
    qa_save_reader start = field(&stack_reader, "start");
    int64_t start_value = (int64_t)qa_save_reader_integer_min(&start, 0);
    qa_save_reader_destroy(&start);
    qa_save_reader end = field(&stack_reader, "end");
    int64_t end_value = (int64_t)qa_save_reader_integer_min(&end, 0);
    qa_save_reader_destroy(&end);
    state->has_stack = true;
    state->stack.start = (double)start_value;
    state->stack.end = (double)end_value;
    if ((strcmp(kind, "counter") != 0 && strcmp(kind, "region") != 0) || start_value % 4 != 0 ||
        end_value % 4 != 0 || start_value < artifact->image.data_length + artifact->image.literal_length ||
        start_value >= end_value || end_value > artifact->image.allocated_data_length)
      qa_save_reader_fail(&stack_reader, "capacity stack must declare an aligned original BSS reservation for a source query");
  }
  qa_save_reader_destroy(&stack_reader);
  if (strcmp(kind, "constant") == 0) {
    qa_save_reader instruction = field(&capacity, "instruction");
    state->constant = read_constant(artifact, &instruction);
    qa_save_reader_destroy(&instruction);
    if (state->constant < 0) qa_save_reader_fail(&capacity, "source ammo capacity cannot be negative");
  } else if (strcmp(kind, "global") == 0) {
    qa_save_reader instruction = field(&capacity, "addressInstruction");
    state->address = read_constant(artifact, &instruction);
    qa_save_reader_destroy(&instruction);
    if (state->address < 0 || state->address % 4 != 0 ||
        state->address + 4 > artifact->image.allocated_data_length)
      qa_save_reader_fail(&capacity, "source capacity word is outside aligned module data");
  } else if (strcmp(kind, "counter") == 0) {
    qa_save_reader function_reader = field(&capacity, "function");
    state->function = (int64_t)qa_save_reader_integer_min(&function_reader, 0);
    qa_save_reader_destroy(&function_reader);
    qa_save_reader functions_reader = field(&capacity, "functions");
    size_t function_count = qa_save_reader_list_count(&functions_reader);
    state->functions = xmalloc(function_count * sizeof *state->functions);
    state->function_count = function_count;
    bool includes_owner = false;
    for (size_t i = 0; i < function_count; i++) {
      qa_save_reader entry = item(&functions_reader, i);
      int64_t index = (int64_t)qa_save_reader_integer_min(&entry, 0);
      const qa_qvm_instruction *instruction = instruction_at(artifact, index);
      if (instruction == NULL || instruction->opcode != QA_QVMY_OP_ENTER)
        qa_save_reader_fail(&entry, "counter operation requires original function entries");
      state->functions[i] = index;
      if (index == state->function) includes_owner = true;
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&functions_reader);
    bool distinct = true;
    for (size_t i = 0; i < function_count; i++)
      for (size_t j = i + 1; j < function_count; j++)
        if (state->functions[i] == state->functions[j]) distinct = false;
    if (!includes_owner || !distinct)
      qa_save_reader_fail(&capacity, "counter operation must include its entry and distinct helpers");
    qa_save_reader arguments_reader = field(&capacity, "arguments");
    size_t argument_count = qa_save_reader_list_count(&arguments_reader);
    state->arguments = xmalloc(argument_count * sizeof *state->arguments);
    state->argument_count = argument_count;
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
        state->arguments[i] = (qa_qvm_source_word){"maximum-grant", 0};
      } else {
        state->arguments[i] = read_word(artifact, &entry);
      }
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&arguments_reader);
    if (argument_count > 62)
      qa_save_reader_fail(&capacity, "counter arguments exceed the private source invocation");
    /* TS reads ammoOffset here, inside the counter branch only. */
    qa_save_reader ammo_reader = field(reader, "ammoOffset");
    state->ammo_offset = (int64_t)qa_save_reader_integer_min(&ammo_reader, 0);
    qa_save_reader_destroy(&ammo_reader);
  } else {
    qa_save_reader function_reader = field(&capacity, "function");
    state->function = (int64_t)qa_save_reader_integer_min(&function_reader, 0);
    qa_save_reader_destroy(&function_reader);
    qa_save_reader source = field(&capacity, "region");
    qa_save_reader entry_reader = field(&source, "entry");
    state->region.entry = (int64_t)qa_save_reader_integer_min(&entry_reader, 0);
    qa_save_reader_destroy(&entry_reader);
    qa_save_reader join_reader = field(&source, "join");
    state->region.join = (int64_t)qa_save_reader_integer_min(&join_reader, 0);
    qa_save_reader_destroy(&join_reader);
    qa_save_reader inputs_reader = field(&source, "inputs");
    size_t input_count = qa_save_reader_list_count(&inputs_reader);
    int64_t *region_inputs = xmalloc(input_count * sizeof *region_inputs);
    state->region.inputs = region_inputs;
    state->region.input_count = input_count;
    for (size_t i = 0; i < input_count; i++) {
      qa_save_reader entry = item(&inputs_reader, i);
      region_inputs[i] = (int64_t)qa_save_reader_integer_min(&entry, 8);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&inputs_reader);
    qa_save_reader result_reader = field(&source, "result");
    state->region.has_result = true;
    state->region.result = (int64_t)qa_save_reader_integer_min(&result_reader, 8);
    qa_save_reader_destroy(&result_reader);
    qa_save_reader_destroy(&source);
    qa_qvm_qualify_region_evaluation(artifact->image.instructions, artifact->image.instruction_count,
                                     state->function, &state->region, "read-only");
    qa_save_reader arguments_reader = field(&capacity, "arguments");
    size_t argument_count = qa_save_reader_list_count(&arguments_reader);
    state->arguments = xmalloc(argument_count * sizeof *state->arguments);
    state->argument_count = argument_count;
    for (size_t i = 0; i < argument_count; i++) {
      qa_save_reader entry = item(&arguments_reader, i);
      state->arguments[i] = read_word(artifact, &entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&arguments_reader);
    qa_save_reader live_ins = field(&capacity, "inputs");
    size_t live_count = qa_save_reader_list_count(&live_ins);
    state->inputs = xmalloc(live_count * sizeof *state->inputs);
    state->input_count = live_count;
    for (size_t i = 0; i < live_count; i++) {
      qa_save_reader entry = item(&live_ins, i);
      state->inputs[i] = read_word(artifact, &entry);
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
    if (result % 4 != 0 || result + (i == 1 ? 64 : 4) > qa_qvm_player_state_bytes(abi_profile))
      qa_save_reader_fail(&at, "inventory field exceeds its aligned public player record");
    offsets[i] = result;
    qa_save_reader_destroy(&at);
  }
  state->ammo_offset = offsets[1];
  qa_qvm_public_inventory_profile *public_profile = xmalloc(sizeof *public_profile);
  memset(public_profile, 0, sizeof *public_profile);
  public_profile->module = module;
  public_profile->abi_profile = abi_profile;
  public_profile->weapons_offset = (double)offsets[0];
  public_profile->ammo_offset = (double)offsets[1];
  public_profile->ctx = state;
  public_profile->capacity = capacity_of;
  out->public_profile = public_profile;
}

static double capacity_of(void *ctx, qa_qvm_memory *memory, double weapon_value,
                          qa_qvm_capacity_context context_value) {
  const capacity_state *state = ctx;
  const qa_qvm_capacity_context *context = &context_value;
  int32_t weapon = (int32_t)weapon_value;
  if (strcmp(state->kind, "constant") == 0) return (double)(int32_t)state->constant;
  if (strcmp(state->kind, "global") == 0) {
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(memory, state->address, 4, &view);
    return (double)qa_qvm_writable_view_get_int32(&view, 0, true);
  }
  int32_t *args = xmalloc(state->argument_count * sizeof *args);
  for (size_t i = 0; i < state->argument_count; i++) {
    args[i] = strcmp(state->arguments[i].kind, "maximum-grant") == 0
        ? 0x7fffffff : word_value(state->arguments[i], weapon, context);
  }
  int32_t result;
  if (strcmp(state->kind, "counter") == 0) {
    result = qa_qvm_module_evaluate_counter(context->module, args, state->argument_count,
        state->function, (int64_t)context->client + state->ammo_offset + (int64_t)weapon * 4,
        state->functions, state->function_count,
        state->has_stack ? &state->stack : NULL);
  } else {
    int64_t *inputs = xmalloc(state->input_count * sizeof *inputs);
    for (size_t i = 0; i < state->input_count; i++)
      inputs[i] = word_value(state->inputs[i], weapon, context);
    result = qa_qvm_module_evaluate_region(context->module, args, state->argument_count,
        state->function, &state->region, inputs, state->input_count,
        state->has_stack ? &state->stack : NULL);
    free(inputs);
  }
  free(args);
  return (double)result;
}

void qa_qvm_primary_inventory_profile_constant(qa_module_identity module, qa_qvm_abi_profile abi_profile,
                                               double weapons_offset, double ammo_offset, int32_t constant,
                                               qa_qvm_inventory_profile *out) {
  memset(out, 0, sizeof *out);
  capacity_state *state = xmalloc(sizeof *state);
  memset(state, 0, sizeof *state);
  state->kind = "constant";
  state->constant = constant;
  qa_qvm_public_inventory_profile *public_profile = xmalloc(sizeof *public_profile);
  memset(public_profile, 0, sizeof *public_profile);
  public_profile->module = module;
  public_profile->abi_profile = abi_profile;
  public_profile->weapons_offset = weapons_offset;
  public_profile->ammo_offset = ammo_offset;
  public_profile->ctx = state;
  public_profile->capacity = capacity_of;
  out->public_profile = public_profile;
}

void qa_qvm_primary_inventory_profile_free(qa_qvm_inventory_profile *profile) {
  if (profile->public_profile != NULL) {
    capacity_state *state = profile->public_profile->ctx;
    if (state != NULL) {
      free(state->functions);
      free(state->arguments);
      free((void *)state->region.inputs);
      free(state->inputs);
      free(state);
    }
    free((void *)profile->public_profile);
  }
  if (profile->private_profile != NULL) {
    free((void *)profile->private_profile->storage);
    free((void *)profile->private_profile);
  }
  memset(profile, 0, sizeof *profile);
}
