/* Implementations for include/qa/content/q1/quakec/compat_types.h
 * (mirrors of src/compat/qc program/memory/machine/source-call
 * helpers). GPL-2.0-or-later. */
#include "qa/content/q1/quakec/compat_types.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

noreturn void qa_q1qc_error(const char *source, const char *format, ...) {
  char message[1024];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(message, sizeof message, format, args);
  va_end(args);
  qa_range_error("%s: %s", source, message);
}
noreturn void qa_q1qc_program_error(const qa_q1qc_program *program, const char *format, ...) {
  char message[1024];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(message, sizeof message, format, args);
  va_end(args);
  qa_range_error("%s: %s", program->source, message);
}
int64_t qa_q1qc_signed_branch(int64_t word) {
  return (int64_t)(int16_t)(word & 0xffff);
}
static const qa_q1qc_definition *definition_by_name(const qa_q1qc_definition *definitions,
                                                    size_t count, const char *name) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(definitions[i].name, name) == 0) return &definitions[i];
  return NULL;
}
const qa_q1qc_definition *qa_q1qc_global_by_name(const qa_q1qc_program *program, const char *name) {
  return definition_by_name(program->globals, program->global_count, name);
}
const qa_q1qc_definition *qa_q1qc_field_by_name(const qa_q1qc_program *program, const char *name) {
  return definition_by_name(program->fields, program->field_count, name);
}
const qa_q1qc_function *qa_q1qc_function_at_or_null(const qa_q1qc_program *program, int64_t index) {
  if (index <= 0 || (size_t)index >= program->function_count) return NULL;
  return &program->functions[index];
}
const qa_q1qc_function *qa_q1qc_function_at(const qa_q1qc_program *program, int64_t index) {
  const qa_q1qc_function *fn = qa_q1qc_function_at_or_null(program, index);
  if (fn == NULL) qa_q1qc_program_error(program, "invalid function %lld", (long long)index);
  return fn;
}
const qa_q1qc_function *qa_q1qc_function_named_or_null(const qa_q1qc_program *program, const char *name) {
  for (size_t i = 0; i < program->function_count; i++)
    if (strcmp(program->functions[i].name, name) == 0)
      return program->functions[i].index == 0 ? NULL : &program->functions[i];
  return NULL;
}
const qa_q1qc_function *qa_q1qc_function_named(const qa_q1qc_program *program, const char *name) {
  const qa_q1qc_function *fn = qa_q1qc_function_named_or_null(program, name);
  if (fn == NULL) qa_q1qc_program_error(program, "missing function %s", name);
  return fn;
}
int64_t qa_q1qc_function_end(const qa_q1qc_program *program, const qa_q1qc_function *fn) {
  int64_t end = (int64_t)program->statement_count;
  for (size_t i = 0; i < program->function_count; i++)
    if (program->functions[i].first_statement > fn->first_statement &&
        program->functions[i].first_statement < end)
      end = program->functions[i].first_statement;
  return end;
}
static int32_t read_i32_le(const uint8_t *bytes, size_t offset) {
  uint32_t value = (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8) |
                   ((uint32_t)bytes[offset + 2] << 16) | ((uint32_t)bytes[offset + 3] << 24);
  int32_t result;
  memcpy(&result, &value, sizeof result);
  return result;
}
static float read_f32_le(const uint8_t *bytes, size_t offset) {
  uint32_t bits = (uint32_t)read_i32_le(bytes, offset);
  float result;
  memcpy(&result, &bits, sizeof result);
  return result;
}
static void write_i32_le(uint8_t *bytes, size_t offset, int32_t value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  bytes[offset] = (uint8_t)bits;
  bytes[offset + 1] = (uint8_t)(bits >> 8);
  bytes[offset + 2] = (uint8_t)(bits >> 16);
  bytes[offset + 3] = (uint8_t)(bits >> 24);
}
int32_t qa_q1qc_initial_int32(const qa_q1qc_program *program, int64_t word) {
  return read_i32_le(program->initial_globals, (size_t)word * 4);
}
float qa_q1qc_initial_float32(const qa_q1qc_program *program, int64_t word) {
  return read_f32_le(program->initial_globals, (size_t)word * 4);
}

size_t qa_q1qc_words_length(const qa_q1qc_words *words) { return words->byte_length / 4; }
static size_t word_offset(const qa_q1qc_words *words, int64_t word) {
  if (word < 0 || (size_t)word >= qa_q1qc_words_length(words))
    qa_q1qc_error("progs.dat", "word %lld outside %zu-word storage",
                  (long long)word, qa_q1qc_words_length(words));
  return (size_t)word * 4;
}
int32_t qa_q1qc_words_int(const qa_q1qc_words *words, int64_t word) {
  return read_i32_le(words->bytes, word_offset(words, word));
}
double qa_q1qc_words_float(const qa_q1qc_words *words, int64_t word) {
  return (double)read_f32_le(words->bytes, word_offset(words, word));
}
void qa_q1qc_words_set_int(qa_q1qc_words *words, int64_t word, int64_t value) {
  write_i32_le(words->bytes, word_offset(words, word), (int32_t)value);
}
void qa_q1qc_words_set_float(qa_q1qc_words *words, int64_t word, double value) {
  float narrowed = (float)value;
  uint32_t bits;
  memcpy(&bits, &narrowed, sizeof bits);
  int32_t as_int;
  memcpy(&as_int, &bits, sizeof as_int);
  write_i32_le(words->bytes, word_offset(words, word), as_int);
}
qa_vec3 qa_q1qc_words_vector(const qa_q1qc_words *words, int64_t word) {
  qa_vec3 result = {qa_q1qc_words_float(words, word), qa_q1qc_words_float(words, word + 1),
                    qa_q1qc_words_float(words, word + 2)};
  return result;
}
void qa_q1qc_words_set_vector(qa_q1qc_words *words, int64_t word, qa_vec3 value) {
  qa_q1qc_words_set_float(words, word, value.x);
  qa_q1qc_words_set_float(words, word + 1, value.y);
  qa_q1qc_words_set_float(words, word + 2, value.z);
}

static void check_slot(const qa_q1qc_entity_memory *memory, int64_t slot) {
  if (slot < 0 || slot >= memory->count)
    qa_q1qc_error("progs.dat", "invalid entity slot %lld", (long long)slot);
}
int64_t qa_q1qc_entities_reference(const qa_q1qc_entity_memory *memory, int64_t slot) {
  check_slot(memory, slot);
  return slot * memory->stride_bytes;
}
int64_t qa_q1qc_entities_slot(const qa_q1qc_entity_memory *memory, int64_t reference) {
  if (reference < 0 || reference % memory->stride_bytes != 0)
    qa_q1qc_error("progs.dat", "invalid entity reference %lld", (long long)reference);
  int64_t slot = reference / memory->stride_bytes;
  check_slot(memory, slot);
  return slot;
}
void qa_q1qc_entities_at(const qa_q1qc_entity_memory *memory, int64_t slot, qa_q1qc_words *out) {
  check_slot(memory, slot);
  size_t offset = (size_t)(slot * memory->stride_bytes + memory->variables_offset_bytes);
  out->bytes = memory->bytes + offset;
  out->byte_length = (size_t)memory->field_words * 4;
}
void qa_q1qc_entities_from_reference(const qa_q1qc_entity_memory *memory, int64_t reference, qa_q1qc_words *out) {
  qa_q1qc_entities_at(memory, qa_q1qc_entities_slot(memory, reference), out);
}

void qa_q1qc_execution_run(const qa_q1qc_function_execution *execution) {
  execution->run(execution->ctx, NULL, NULL);
}
bool qa_q1qc_boundary_has(const qa_q1qc_function_boundary *boundary, int64_t function_index) {
  for (size_t i = 0; i < boundary->function_count; i++)
    if (boundary->functions[i] == function_index) return true;
  return false;
}
noreturn void qa_q1qc_machine_fail(qa_q1qc_machine *machine, const char *format, ...) {
  char message[1024];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(message, sizeof message, format, args);
  va_end(args);
  if (machine->fail != NULL) machine->fail(machine->ctx, message);
  qa_range_error("%s", message);
}
const char *qa_q1qc_machine_string(qa_q1qc_machine *machine, int64_t reference, char *buf, size_t size) {
  if (machine->strings_get != NULL) return machine->strings_get(machine->ctx, reference, buf, size);
  if (reference < 0) qa_q1qc_error("progs.dat", "invalid engine string %lld", (long long)reference);
  if ((size_t)reference >= machine->strings_length)
    qa_q1qc_error("progs.dat", "invalid string offset %lld", (long long)reference);
  size_t length = 0;
  for (size_t cursor = (size_t)reference; cursor < machine->strings_length; cursor++) {
    uint8_t byte = machine->strings[cursor];
    if (byte == 0) {
      if (length < size) buf[length] = '\0';
      else if (size > 0) buf[size - 1] = '\0';
      return buf;
    }
    if (length + 1 < size) buf[length] = (char)byte;
    length++;
  }
  qa_q1qc_error("progs.dat", "unterminated string at %lld", (long long)reference);
}
int64_t qa_q1qc_machine_global_offset(qa_q1qc_machine *machine, const char *name) {
  const qa_q1qc_definition *definition = qa_q1qc_global_by_name(machine->program, name);
  if (definition == NULL) qa_q1qc_machine_fail(machine, "missing global %s", name);
  return definition->offset;
}
int64_t qa_q1qc_machine_field_offset(qa_q1qc_machine *machine, const char *name) {
  const qa_q1qc_definition *definition = qa_q1qc_field_by_name(machine->program, name);
  if (definition == NULL) qa_q1qc_machine_fail(machine, "missing entity field %s", name);
  return definition->offset;
}
static int64_t parameter_offset(qa_q1qc_machine *machine, int64_t index) {
  if (index < 0 || index >= 8) qa_q1qc_machine_fail(machine, "invalid parameter %lld", (long long)index);
  return 4 + index * 3;
}
double qa_q1qc_machine_arg_float(qa_q1qc_machine *machine, int64_t index) {
  return qa_q1qc_words_float(&machine->globals, parameter_offset(machine, index));
}
int64_t qa_q1qc_machine_arg_int(qa_q1qc_machine *machine, int64_t index) {
  return qa_q1qc_words_int(&machine->globals, parameter_offset(machine, index));
}

bool qa_q1qc_actor_equal(qa_actor_id a, qa_actor_id b) { return qa_same_actor(a, b); }
bool qa_q1qc_owned_equal(qa_owned_actor a, qa_owned_actor b) {
  return qa_same_owned_actor(a, b);
}

const char *qa_q1qc_source_value_type(const qa_mod_callback_value *value) {
  if (strcmp(value->kind, "input") != 0) return value->kind;
  const char *name = value->name;
  if (strcmp(name, "self") == 0 || strcmp(name, "other") == 0 || strcmp(name, "activator") == 0 ||
      strcmp(name, "attacker") == 0 || strcmp(name, "inflictor") == 0) return "entity";
  if (strcmp(name, "point") == 0 || strcmp(name, "direction") == 0 || strcmp(name, "normal") == 0 ||
      strcmp(name, "view-angles") == 0) return "vector";
  if (strcmp(name, "item") == 0) return "string";
  return "float";
}
static bool value_equal(const qa_mod_callback_value *a, const qa_mod_callback_value *b) {
  if (strcmp(a->kind, b->kind) != 0) return false;
  if (strcmp(a->kind, "input") == 0) return strcmp(a->name, b->name) == 0;
  if (strcmp(a->kind, "float") == 0) return a->number_value == b->number_value;
  if (strcmp(a->kind, "string") == 0) return strcmp(a->string_value, b->string_value) == 0;
  if (strcmp(a->kind, "vector") == 0)
    return a->vector_value.x == b->vector_value.x && a->vector_value.y == b->vector_value.y &&
           a->vector_value.z == b->vector_value.z;
  return false;
}
bool qa_q1qc_source_call_equal(const qa_mod_source_call *a, const qa_mod_source_call *b) {
  if (a == b) return true;
  if (a == NULL || b == NULL) return false;
  if (strcmp(a->function, b->function) != 0) return false;
  if (a->argument_count != b->argument_count || a->global_count != b->global_count) return false;
  for (size_t i = 0; i < a->argument_count; i++)
    if (!value_equal(&a->arguments[i], &b->arguments[i])) return false;
  for (size_t i = 0; i < a->global_count; i++) {
    if (strcmp(a->globals[i].name, b->globals[i].name) != 0) return false;
    if (!value_equal(&a->globals[i].value, &b->globals[i].value)) return false;
  }
  return true;
}
bool qa_q1qc_validate_source_call_check(const qa_q1qc_program *program,
                                        const qa_mod_source_call *call,
                                        const char *const *available, size_t available_count,
                                        const char *label, char *error, size_t error_size) {
  for (int pass = 0; pass < 2; pass++) {
    size_t count = pass == 0 ? call->argument_count : call->global_count;
    for (size_t i = 0; i < count; i++) {
      const qa_mod_callback_value *value = pass == 0 ? &call->arguments[i] : &call->globals[i].value;
      if (strcmp(value->kind, "input") != 0) continue;
      bool found = false;
      for (size_t a = 0; a < available_count; a++)
        if (strcmp(available[a], value->name) == 0) found = true;
      if (!found) {
        (void)snprintf(error, error_size, "Mod %s cannot read %s", label, value->name);
        return false;
      }
    }
  }
  const qa_q1qc_function *fn = qa_q1qc_function_named_or_null(program, call->function);
  bool compatible = fn != NULL && fn->index != 0 && fn->first_statement > 0 &&
                    fn->parameter_size_count == call->argument_count;
  if (compatible) {
    for (size_t i = 0; i < call->argument_count; i++) {
      int64_t expected = strcmp(qa_q1qc_source_value_type(&call->arguments[i]), "vector") == 0 ? 3 : 1;
      if (fn->parameter_sizes[i] != expected) compatible = false;
    }
  }
  if (!compatible) {
    (void)snprintf(error, error_size, "Mod callback %s has an incompatible source signature", call->function);
    return false;
  }
  for (size_t i = 0; i < call->global_count; i++) {
    bool duplicate = false;
    for (size_t j = 0; j < i; j++)
      if (strcmp(call->globals[j].name, call->globals[i].name) == 0) duplicate = true;
    const qa_q1qc_definition *definition = qa_q1qc_global_by_name(program, call->globals[i].name);
    if (duplicate || definition == NULL ||
        strcmp(definition->type, qa_q1qc_source_value_type(&call->globals[i].value)) != 0) {
      (void)snprintf(error, error_size, "Mod callback global %s is duplicated or has an incompatible type",
                     call->globals[i].name);
      return false;
    }
  }
  return true;
}
void qa_q1qc_validate_source_call(const qa_q1qc_program *program,
                                  const qa_mod_source_call *call,
                                  const char *const *available, size_t available_count,
                                  const char *label) {
  char error[512];
  if (!qa_q1qc_validate_source_call_check(program, call, available, available_count, label, error, sizeof error)) {
    /* TS program.functionNamed() raises its own "missing function" error after
     * the input check and before the signature check. */
    if (strncmp(error, "Mod callback ", 13) == 0 && qa_q1qc_function_named_or_null(program, call->function) == NULL)
      (void)qa_q1qc_function_named(program, call->function);
    qa_range_error("%s", error);
  }
}
static void write_source_value(qa_q1qc_machine *machine, qa_q1qc_words *words, int64_t offset,
                               const qa_mod_runtime_value *value,
                               qa_q1qc_actor_reference_fn reference, void *reference_ctx) {
  if (strcmp(value->kind, "float") == 0) {
    if (!isfinite((double)(float)value->number_value))
      qa_range_error("Mod callback number exceeds binary32 range");
    qa_q1qc_words_set_float(words, offset, value->number_value);
  } else if (strcmp(value->kind, "vector") == 0) {
    qa_vec3 vector = value->vector_value;
    if (!isfinite((double)(float)vector.x) || !isfinite((double)(float)vector.y) ||
        !isfinite((double)(float)vector.z))
      qa_range_error("Mod callback vector exceeds binary32 range");
    qa_q1qc_words_set_vector(words, offset, vector);
  } else if (strcmp(value->kind, "string") == 0) {
    char name[512];
    (void)snprintf(name, sizeof name, "mod-value:%s", value->string_value);
    size_t capacity = strlen(value->string_value) + 1;
    if (capacity < 128) capacity = 128;
    if (machine->strings_set_engine == NULL) qa_range_error("Missing QC engine string storage");
    int64_t id = machine->strings_set_engine(machine->ctx, name, value->string_value, capacity);
    qa_q1qc_words_set_int(words, offset, id);
  } else if (strcmp(value->kind, "actor") == 0) {
    qa_q1qc_words_set_int(words, offset, reference(reference_ctx, value->actor_value));
  }
}
static const qa_mod_runtime_value *resolve_input(const qa_mod_source_call *call,
                                                 const qa_mod_callback_value *value,
                                                 const qa_q1qc_source_input *inputs, size_t input_count,
                                                 qa_mod_runtime_value *scratch) {
  if (strcmp(value->kind, "input") != 0) {
    memset(scratch, 0, sizeof *scratch);
    scratch->kind = value->kind;
    scratch->number_value = value->number_value;
    scratch->string_value = value->string_value;
    scratch->vector_value = value->vector_value;
    return scratch;
  }
  for (size_t i = 0; i < input_count; i++)
    if (strcmp(inputs[i].name, value->name) == 0) return &inputs[i].value;
  qa_range_error("Gameplay callback %s has no %s input", call->function, value->name);
}
typedef struct source_call_saved { size_t offset; uint8_t bytes[12]; size_t length; } source_call_saved;
typedef struct source_call_restore {
  qa_q1qc_machine *machine;
  uint8_t staging[108];
  source_call_saved saved[32];
  size_t count;
} source_call_restore;
static void restore_source_call(void *arg) {
  source_call_restore *restore = arg;
  memcpy(restore->machine->globals.bytes + 4, restore->staging, sizeof restore->staging);
  for (size_t i = 0; i < restore->count; i++)
    memcpy(restore->machine->globals.bytes + restore->saved[i].offset, restore->saved[i].bytes,
           restore->saved[i].length);
}
double qa_q1qc_with_source_call(qa_q1qc_machine *machine, const qa_mod_source_call *call,
                                const qa_q1qc_source_input *inputs, size_t input_count,
                                qa_q1qc_actor_reference_fn reference, void *reference_ctx,
                                double (*execute)(void *ctx, int64_t argument_count), void *execute_ctx) {
  qa_mod_runtime_value args[16];
  qa_mod_runtime_value global_values[32];
  qa_mod_runtime_value scratch;
  if (call->argument_count > 16 || call->global_count > 32) qa_range_error("out of memory");
  for (size_t i = 0; i < call->argument_count; i++)
    args[i] = *resolve_input(call, &call->arguments[i], inputs, input_count, &scratch);
  const qa_q1qc_definition *definitions[32];
  for (size_t i = 0; i < call->global_count; i++) {
    definitions[i] = qa_q1qc_global_by_name(machine->program, call->globals[i].name);
    global_values[i] = *resolve_input(call, &call->globals[i].value, inputs, input_count, &scratch);
  }
  source_call_restore restore;
  restore.machine = machine;
  restore.count = call->global_count;
  memcpy(restore.staging, machine->globals.bytes + 4, sizeof restore.staging);
  for (size_t i = 0; i < call->global_count; i++) {
    if (definitions[i] == NULL) qa_range_error("Missing validated callback global");
    restore.saved[i].offset = (size_t)definitions[i]->offset * 4;
    restore.saved[i].length = (strcmp(definitions[i]->type, "vector") == 0 ? 3 : 1) * 4;
    memcpy(restore.saved[i].bytes, machine->globals.bytes + restore.saved[i].offset, restore.saved[i].length);
  }
  /* TS restores in a finally; a raise caught above this frame runs the same restore. */
  qa_cleanup node;
  qa_cleanup_push(&node, restore_source_call, &restore);
  for (size_t i = 0; i < call->argument_count; i++)
    write_source_value(machine, &machine->globals, 4 + (int64_t)i * 3, &args[i], reference, reference_ctx);
  for (size_t i = 0; i < call->global_count; i++)
    write_source_value(machine, &machine->globals, definitions[i]->offset, &global_values[i], reference, reference_ctx);
  double result = execute(execute_ctx, (int64_t)call->argument_count);
  qa_cleanup_pop(&node);
  restore_source_call(&restore);
  return result;
}
