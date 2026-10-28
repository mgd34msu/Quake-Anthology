/* See include/qa/compat/qvm/mod_input.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/mod_input.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/client_state_record.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

#define QVM_USER_COMMAND_BYTES 24
#define QVM_MAX_PRIVATE_ARGUMENT_WORDS 62

static void *xmalloc(size_t size) {
  void *result = malloc(size == 0 ? 1 : size);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  return result;
}

/* Delegates to the landed owner (TS client-state-record.ts
 * readQvmUserCommand in qa/compat/qvm/client_state_record.h). */
qa_q3_wire_user_command qa_qvmy_read_user_command(const qa_qvm_writable_view *view, qa_qvm_abi_profile profile) {
  qa_qvmx_view raw = {view->bytes + view->start, (size_t)view->length};
  qa_qvmx_wire_user_command read = qa_qvm_read_user_command(raw, profile);
  qa_q3_wire_user_command result;
  result.server_time = (int32_t)read.server_time;
  result.angles[0] = (int32_t)read.angles[0];
  result.angles[1] = (int32_t)read.angles[1];
  result.angles[2] = (int32_t)read.angles[2];
  result.forwardmove = (int32_t)read.forwardmove;
  result.rightmove = (int32_t)read.rightmove;
  result.upmove = (int32_t)read.upmove;
  result.buttons = (int32_t)read.buttons;
  result.weapon = (int32_t)read.weapon;
  return result;
}

static double scalar_value(int32_t result, const char *encoding) {
  double value = strcmp(encoding, "int32") == 0 ? (double)result : (double)qa_bits_to_float32((uint32_t)result);
  if (!isfinite(value)) qa_range_error("QVM input returned a nonfinite scalar");
  return value;
}

static void validate_pointer(qa_qvm_mod_input *input, const qa_qvm_mod_input_pointer *pointer) {
  bool invalid = !isfinite(pointer->offset) || pointer->offset != trunc(pointer->offset) || pointer->offset < 0 ||
      pointer->offset > 9007199254740991.0;
  for (size_t i = 0; i < pointer->indirections.count && !invalid; i++) {
    double offset = pointer->indirections.values[i];
    if (!isfinite(offset) || offset != trunc(offset) || offset < 0 || offset > 9007199254740991.0) invalid = true;
  }
  if (invalid) qa_range_error("Invalid QVM input pointer path");
  if (!pointer->is_global) {
    if (!isfinite(pointer->index) || pointer->index != trunc(pointer->index) || pointer->index < 0 ||
        pointer->index > 9007199254740991.0 || pointer->index >= QVM_MAX_PRIVATE_ARGUMENT_WORDS)
      qa_range_error("Invalid QVM input pointer argument");
  } else {
    /* TS dataView(address, 4) rejects non-safe-integer addresses via
     * writes.range; check before the int64 cast truncates them. */
    if (!isfinite(pointer->address) || pointer->address != trunc(pointer->address) ||
        pointer->address < 0 || pointer->address > 9007199254740991.0)
      qa_range_error("QVM raw memory range exceeds allocation");
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(input->operations.module->memory, (int64_t)pointer->address, 4, &view);
    (void)view;
  }
}

static int64_t resolve_address(const qa_qvm_mod_input_pointer *pointer, qa_qvmy_function_call *call) {
  int64_t address;
  if (!pointer->is_global) {
    size_t offset = (size_t)(int64_t)pointer->index * 4;
    if (offset > call->words_length || call->words_length - offset < 4)
      qa_range_error("Offset is outside the bounds of the DataView");
    uint32_t bits = (uint32_t)call->words[offset] | ((uint32_t)call->words[offset + 1] << 8) |
                    ((uint32_t)call->words[offset + 2] << 16) | ((uint32_t)call->words[offset + 3] << 24);
    address = (int32_t)bits;
  } else {
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(call->guest, (int64_t)pointer->address, 4, &view);
    address = qa_qvm_writable_view_get_int32(&view, 0, true);
  }
  for (size_t i = 0; i < pointer->indirections.count; i++) {
    qa_qvm_writable_view view;
    qa_qvm_memory_view(call->guest, address + (int64_t)pointer->indirections.values[i], 4, 0, &view);
    address = qa_qvm_writable_view_get_int32(&view, 0, true);
  }
  return address + (int64_t)pointer->offset;
}

typedef struct command_capture {
  const qa_qvm_mod_input_output *output;
  int64_t address;
  qa_q3_wire_user_command before;
} command_capture;

typedef struct field_capture {
  const qa_qvm_mod_input_output *output;
  qa_mod_client_input_output before;
} field_capture;

typedef struct capture {
  qa_mod_client_application *application;
  const qa_qvm_mod_input_output *selected;
  size_t selected_count;
  qa_mod_client_input_output *changes;
  size_t change_count;
  size_t change_capacity;
  field_capture *fields;
  size_t field_count;
  command_capture *commands;
  size_t command_count;
  size_t command_capacity;
  qa_mod_client_input_output *consumed;
  size_t consumed_count;
  size_t consumed_capacity;
} capture;

/* Captures are heap objects on a pointer stack: invoke/open/output
 * hold capture pointers across guest execution that may push nested
 * captures, and the TS captures are stable object references. */
static capture **capture_stack(qa_qvm_mod_input *input) { return (capture **)input->captures; }

static void free_capture(capture *value) {
  free(value->changes);
  free(value->fields);
  free(value->commands);
  free(value->consumed);
  free(value);
}

static bool capture_active(qa_qvm_mod_input *input, capture *value) {
  capture **captures = capture_stack(input);
  return input->capture_count != 0 && captures[input->capture_count - 1] == value &&
         input->application_count != 0 &&
         input->applications[input->application_count - 1] == value->application &&
         input->operations.live(input->operations.ctx, value->application->identity.actor);
}

static qa_mod_client_input_output read_field(qa_qvm_mod_input *input, const qa_qvm_mod_input_output *output,
                                             qa_actor_id actor) {
  bool view_angles = strcmp(output->value_input, "view-angles") == 0;
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(input->operations.module->memory,
      input->operations.pointer(input->operations.ctx, actor, output->record) + (int64_t)output->offset,
      view_angles ? 12 : 4, &view);
  qa_mod_client_input_output result;
  memset(&result, 0, sizeof result);
  result.kind = QA_MOD_CLIENT_INPUT_OUTPUT_SET;
  result.input = output->value_input;
  if (view_angles) {
    result.vector_value.x = (double)qa_qvm_writable_view_get_float32(&view, 0, true);
    result.vector_value.y = (double)qa_qvm_writable_view_get_float32(&view, 4, true);
    result.vector_value.z = (double)qa_qvm_writable_view_get_float32(&view, 8, true);
    if (!isfinite(result.vector_value.x) || !isfinite(result.vector_value.y) || !isfinite(result.vector_value.z))
      qa_range_error("QVM input returned nonfinite angles");
    return result;
  }
  result.number_value = scalar_value(qa_qvm_writable_view_get_int32(&view, 0, true), output->value_encoding) /
                        output->value_scale;
  return result;
}

static void push_output(qa_mod_client_input_output **list, size_t *count, size_t *capacity,
                        qa_mod_client_input_output value) {
  if (*count == *capacity) {
    *capacity = *capacity == 0 ? 8 : *capacity * 2;
    *list = realloc(*list, *capacity * sizeof **list);
    if (*list == NULL) qa_range_error("QVM memory allocation failed");
  }
  (*list)[(*count)++] = value;
}

static double command_input_value(qa_q3_wire_user_command command, const char *input_name) {
  if (strcmp(input_name, "attack") == 0) return (command.buttons & 1) == 0 ? 0 : 1;
  if (strcmp(input_name, "jump") == 0) return command.upmove >= 10 ? 1 : 0;
  if (strcmp(input_name, "forward-move") == 0) return command.forwardmove / 127.0;
  if (strcmp(input_name, "side-move") == 0) return command.rightmove / 127.0;
  return command.upmove / 127.0;
}

static void process_commands(qa_qvm_mod_input *input, capture *capture, command_capture *commands,
                             size_t count, bool publish) {
  for (size_t c = 0; c < count; c++) {
    command_capture *command = &commands[c];
    qa_qvm_writable_view view;
    qa_qvm_memory_view(input->operations.module->memory, command->address, QVM_USER_COMMAND_BYTES, 0, &view);
    qa_q3_wire_user_command before = command->before;
    qa_q3_wire_user_command after = qa_qvmy_read_user_command(&view, input->operations.abi_profile);
    command->before = after;
    if (!publish) continue;
    for (size_t i = 0; i < command->output->input_count; i++) {
      const char *name = command->output->inputs[i];
      if (strcmp(name, "view-angles") == 0) {
        double storage[64];
        qa_q3_player_state state;
        memset(&state, 0, sizeof state);
        state.stats.values = storage;
        state.persistent.values = storage + 16;
        state.powerups.values = storage + 32;
        state.ammo.values = storage + 48;
        input->operations.player_state(input->operations.ctx, capture->application->identity.actor, &state);
        bool changed = false;
        for (int a = 0; a < 3; a++) if (before.angles[a] != after.angles[a]) changed = true;
        if (changed) {
          qa_mod_client_input_output change;
          memset(&change, 0, sizeof change);
          change.kind = QA_MOD_CLIENT_INPUT_OUTPUT_SET;
          change.input = name;
          change.vector_value.x = (double)(((int64_t)after.angles[0] + (int64_t)state.delta_angle_words.values[0]) & 65535) * 360.0 / 65536.0;
          change.vector_value.y = (double)(((int64_t)after.angles[1] + (int64_t)state.delta_angle_words.values[1]) & 65535) * 360.0 / 65536.0;
          change.vector_value.z = (double)(((int64_t)after.angles[2] + (int64_t)state.delta_angle_words.values[2]) & 65535) * 360.0 / 65536.0;
          push_output(&capture->changes, &capture->change_count, &capture->change_capacity, change);
        }
        continue;
      }
      if (command_input_value(before, name) != command_input_value(after, name)) {
        qa_mod_client_input_output change;
        memset(&change, 0, sizeof change);
        change.kind = QA_MOD_CLIENT_INPUT_OUTPUT_SET;
        change.input = name;
        change.number_value = command_input_value(after, name);
        push_output(&capture->changes, &capture->change_count, &capture->change_capacity, change);
      }
    }
  }
}

static bool same_number(double a, double b) {
  /* isDeepStrictEqual: NaN equals NaN, +0 differs from -0. */
  if (a != a && b != b) return true;
  uint64_t x, y;
  memcpy(&x, &a, sizeof x);
  memcpy(&y, &b, sizeof y);
  return x == y;
}

static bool output_equal(qa_mod_client_input_output a, qa_mod_client_input_output b) {
  return a.kind == b.kind && strcmp(a.input, b.input) == 0 && same_number(a.number_value, b.number_value) &&
         same_number(a.vector_value.x, b.vector_value.x) && same_number(a.vector_value.y, b.vector_value.y) &&
         same_number(a.vector_value.z, b.vector_value.z);
}

static void reconcile(qa_qvm_mod_input *input, capture *capture, bool publish) {
  for (size_t i = 0; i < capture->field_count; i++) {
    qa_mod_client_input_output after = read_field(input, capture->fields[i].output,
                                                  capture->application->identity.actor);
    if (publish && !output_equal(capture->fields[i].before, after))
      push_output(&capture->changes, &capture->change_count, &capture->change_capacity, after);
    capture->fields[i].before = after;
  }
  process_commands(input, capture, capture->commands, capture->command_count, publish);
}

static int32_t proceed_call(qa_qvmy_function_call *call) {
  return strcmp(call->execution, "synchronous") == 0 ? call->proceed(call->call_ctx)
                                                     : call->proceed_async(call->call_ctx);
}

typedef struct declaration_group {
  qa_qvm_mod_input *input;
  const qa_qvm_mod_input_output *outputs; /* borrowed declaration storage */
  const qa_qvm_mod_input_output **declarations;
  size_t count;
} declaration_group;

static bool is_selected(const capture *capture, const qa_qvm_mod_input_output *output) {
  for (size_t i = 0; i < capture->selected_count; i++)
    if (&capture->selected[i] == output) return true;
  return false;
}

static int32_t invoke_hook(void *ctx, qa_qvmy_function_call *call) {
  declaration_group *group = ctx;
  qa_qvm_mod_input *input = group->input;
  capture **captures = capture_stack(input);
  if (input->capture_count == 0) return proceed_call(call);
  capture *capture = captures[input->capture_count - 1];
  if (!capture_active(input, capture)) return proceed_call(call);
  qa_actor_id actor = capture->application->identity.actor;
  const qa_qvm_mod_input_output **selected = xmalloc(group->count * sizeof *selected);
  size_t selected_count = 0;
  for (size_t i = 0; i < group->count; i++) {
    const qa_qvm_mod_input_output *output = group->declarations[i];
    if (is_selected(capture, output) &&
        resolve_address(&output->actor_pointer, call) ==
            input->operations.pointer(input->operations.ctx, actor, output->actor_record))
      selected[selected_count++] = output;
  }
  command_capture *local = xmalloc(selected_count * sizeof *local);
  size_t local_count = 0;
  size_t start = capture->command_count;
  for (size_t i = 0; i < selected_count; i++) {
    if (strcmp(selected[i]->kind, "command") != 0) continue;
    int64_t address = resolve_address(&selected[i]->command, call);
    qa_qvm_writable_view view;
    qa_qvm_memory_view(call->guest, address, QVM_USER_COMMAND_BYTES, 0, &view);
    local[local_count].output = selected[i];
    local[local_count].address = address;
    local[local_count].before = qa_qvmy_read_user_command(&view, input->operations.abi_profile);
    if (capture->command_count == capture->command_capacity) {
      capture->command_capacity = capture->command_capacity == 0 ? 8 : capture->command_capacity * 2;
      capture->commands = realloc(capture->commands, capture->command_capacity * sizeof *capture->commands);
      if (capture->commands == NULL) qa_range_error("QVM memory allocation failed");
    }
    capture->commands[capture->command_count++] = local[local_count];
    local_count++;
  }
  int32_t result = proceed_call(call);
  if (capture_active(input, capture)) {
    for (size_t i = 0; i < selected_count; i++) {
      if (strcmp(selected[i]->kind, "handler") != 0) continue;
      if (selected[i]->has_returns) {
        double expected = strcmp(selected[i]->returns_encoding, "float32") == 0
            ? (double)(float)selected[i]->returns_value : selected[i]->returns_value;
        if (scalar_value(result, selected[i]->returns_encoding) != expected) continue;
      }
      qa_mod_client_input_output consumed;
      memset(&consumed, 0, sizeof consumed);
      consumed.kind = QA_MOD_CLIENT_INPUT_OUTPUT_CONSUME;
      consumed.inputs = selected[i]->inputs;
      consumed.input_count = selected[i]->input_count;
      push_output(&capture->consumed, &capture->consumed_count, &capture->consumed_capacity, consumed);
    }
    /* Refresh the local befores from the capture copies, as in TS. */
    for (size_t i = 0; i < local_count; i++) local[i] = capture->commands[start + i];
    process_commands(input, capture, local, local_count, true);
    for (size_t i = 0; i < local_count; i++) capture->commands[start + i].before = local[i].before;
  }
  /* Remove this invocation's commands from the capture list. */
  capture->command_count = start;
  free(selected);
  free(local);
  return result;
}

void qa_qvm_mod_input_init(qa_qvm_mod_input *input, const qa_qvm_mod_input_operations *operations,
                           const qa_qvm_mod_input_output *outputs, size_t output_count) {
  memset(input, 0, sizeof *input);
  input->operations = *operations;
  double *entries_seen = xmalloc(output_count * sizeof *entries_seen);
  size_t entry_count = 0;
  for (size_t i = 0; i < output_count; i++) {
    const qa_qvm_mod_input_output *output = &outputs[i];
    const char *record_id = strcmp(output->kind, "field") == 0 ? output->record : output->actor_record;
    const qa_qvm_mod_actor_record *record = NULL;
    for (size_t r = 0; r < operations->record_count; r++) {
      if (strcmp(operations->records[r].id, record_id) == 0) {
        record = &operations->records[r]; /* TS Array.find: first */
        break;
      }
    }
    if (record == NULL) qa_range_error("QVM input output names an undeclared actor record");
    if (strcmp(output->kind, "field") == 0) {
      double size = strcmp(output->value_input, "view-angles") == 0 ? 12 : 4;
      if (!isfinite(output->offset) || output->offset != trunc(output->offset) || output->offset < 0 ||
          output->offset > 9007199254740991.0 || output->offset + size > record->stride)
        qa_range_error("QVM input output exceeds its actor record");
      if (strcmp(output->value_input, "view-angles") != 0 &&
          (!isfinite(output->value_scale) || output->value_scale <= 0))
        qa_range_error("QVM input field scale must be positive");
      continue;
    }
    validate_pointer(input, &output->actor_pointer);
    if (strcmp(output->kind, "command") == 0) validate_pointer(input, &output->command);
    if (strcmp(output->kind, "handler") == 0 && output->has_returns) {
      double value = output->returns_value;
      if (!isfinite(value) ||
          (strcmp(output->returns_encoding, "float32") == 0
               ? !isfinite((double)(float)value)
               : value != trunc(value) || value < -2147483648.0 || value > 2147483647.0))
        qa_range_error("Invalid QVM input handler return value");
    }
    bool seen = false;
    for (size_t e = 0; e < entry_count; e++) if (entries_seen[e] == output->entry) seen = true;
    if (!seen) entries_seen[entry_count++] = output->entry;
  }
  input->removal_ids = xmalloc(entry_count * sizeof *input->removal_ids);
  input->groups = xmalloc(entry_count * sizeof *input->groups);
  input->group_count = entry_count;
  for (size_t e = 0; e < entry_count; e++) {
    declaration_group *group = xmalloc(sizeof *group);
    input->groups[e] = group;
    group->input = input;
    group->outputs = outputs;
    group->declarations = xmalloc(output_count * sizeof *group->declarations);
    group->count = 0;
    for (size_t i = 0; i < output_count; i++)
      if (strcmp(outputs[i].kind, "field") != 0 && outputs[i].entry == entries_seen[e])
        group->declarations[group->count++] = &outputs[i];
    qa_guest_callback_reference reference;
    memset(&reference, 0, sizeof reference);
    reference.kind = QA_GUEST_CALLBACK_QVM;
    reference.module = qa_qvm_module_get_profile(operations->module)->module;
    reference.instruction_index = entries_seen[e];
    input->removal_ids[input->removal_count++] =
        qa_qvm_module_bind_invocation(operations->module, &reference, invoke_hook, group);
  }
  free(entries_seen);
}

void qa_qvm_mod_input_destroy(qa_qvm_mod_input *input) {
  if (input->groups != NULL) {
    for (size_t i = 0; i < input->group_count; i++) {
      declaration_group *group = input->groups[i];
      free((void *)group->declarations);
      free(group);
    }
    free(input->groups);
  }
  free(input->removal_ids);
  free(input->applications);
  capture **captures = capture_stack(input);
  for (size_t i = 0; i < input->capture_count; i++) free_capture(captures[i]);
  free(input->captures);
  memset(input, 0, sizeof *input);
}

typedef struct open_state {
  qa_qvm_mod_input *input;
  qa_mod_client_application *application;
  capture *suspended;
} open_state;

static void run_open_remove(void *ctx) {
  open_state *state = ctx;
  qa_qvm_mod_input *input = state->input;
  bool found = false;
  for (size_t i = input->application_count; i-- > 0;) {
    if (input->applications[i] == state->application) {
      memmove(&input->applications[i], &input->applications[i + 1],
              (input->application_count - i - 1) * sizeof *input->applications);
      input->application_count--;
      found = true;
      break;
    }
  }
  if (found && state->suspended != NULL && capture_active(input, state->suspended))
    reconcile(input, state->suspended, false);
  free(state);
}

void qa_qvm_mod_input_open(qa_qvm_mod_input *input, qa_mod_client_application *application,
                           qa_qvm_input_remove_fn *out_remove, void **out_remove_ctx) {
  capture *suspended = NULL;
  if (input->capture_count != 0) {
    capture **captures = capture_stack(input);
    suspended = captures[input->capture_count - 1];
    if (capture_active(input, suspended)) reconcile(input, suspended, true);
  }
  if (input->application_count == input->application_capacity) {
    input->application_capacity = input->application_capacity == 0 ? 8 : input->application_capacity * 2;
    input->applications = realloc(input->applications, input->application_capacity * sizeof *input->applications);
    if (input->applications == NULL) qa_range_error("QVM memory allocation failed");
  }
  input->applications[input->application_count++] = application;
  open_state *state = xmalloc(sizeof *state);
  state->input = input;
  state->application = application;
  state->suspended = suspended;
  *out_remove = run_open_remove;
  *out_remove_ctx = state;
}

qa_mod_client_input_output *qa_qvm_mod_input_run_output(qa_qvm_mod_input *input,
                                                    const qa_qvm_mod_input_output *outputs, size_t output_count,
                                                    qa_mod_client_application *application,
                                                    qa_qvm_mod_input_run_fn run, void *run_ctx, size_t *out_count) {
  if (input->application_count == 0 || input->applications[input->application_count - 1] != application)
    qa_range_error("QVM input output requires its live application");
  qa_actor_id actor = application->identity.actor;
  if (input->capture_count == input->capture_capacity) {
    input->capture_capacity = input->capture_capacity == 0 ? 4 : input->capture_capacity * 2;
    input->captures = realloc(input->captures, input->capture_capacity * sizeof(capture *));
    if (input->captures == NULL) qa_range_error("QVM memory allocation failed");
  }
  capture *created = xmalloc(sizeof *created);
  memset(created, 0, sizeof *created);
  capture_stack(input)[input->capture_count++] = created;
  created->application = application;
  created->selected = outputs;
  created->selected_count = output_count;
  size_t field_count = 0;
  for (size_t i = 0; i < output_count; i++) if (strcmp(outputs[i].kind, "field") == 0) field_count++;
  created->fields = xmalloc(field_count * sizeof *created->fields);
  for (size_t i = 0; i < output_count; i++) {
    if (strcmp(outputs[i].kind, "field") == 0) {
      created->fields[created->field_count].output = &outputs[i];
      created->fields[created->field_count].before = read_field(input, &outputs[i], actor);
      created->field_count++;
    }
  }
  run(run_ctx);
  qa_mod_client_input_output *result = NULL;
  *out_count = 0;
  if (capture_active(input, created)) {
    reconcile(input, created, true);
    size_t total = created->change_count + created->consumed_count;
    result = xmalloc(total * sizeof *result);
    memcpy(result, created->changes, created->change_count * sizeof *result);
    memcpy(result + created->change_count, created->consumed, created->consumed_count * sizeof *result);
    *out_count = total;
  }
  input->capture_count--;
  free_capture(created);
  return result;
}

void qa_qvm_mod_input_close(qa_qvm_mod_input *input) {
  for (size_t i = input->removal_count; i-- > 0;)
    qa_qvm_module_unbind(input->operations.module, input->removal_ids[i]);
  input->removal_count = 0;
  input->application_count = 0;
  capture **captures = capture_stack(input);
  for (size_t i = 0; i < input->capture_count; i++) free_capture(captures[i]);
  input->capture_count = 0;
}
