/* See include/qa/compat/qvm/mod_weapon_stage.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/mod_weapon_stage.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/operations.h"
#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *result = malloc(size == 0 ? 1 : size);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  return result;
}

static const qa_qvmy_region_instruction *at(const qa_qvmy_region_instruction *instructions,
                                            size_t count, int64_t pc) {
  if (pc < 0 || (uint64_t)pc >= count) return NULL;
  return &instructions[pc];
}

static int64_t function_end(const qa_qvmy_region_instruction *instructions, size_t count, int64_t entry) {
  const qa_qvmy_region_instruction *instruction = at(instructions, count, entry);
  if (instruction == NULL || instruction->opcode != QA_QVMY_OP_ENTER)
    qa_range_error("QVM weapon entry is not an original function");
  int64_t end = entry + 1;
  while (end < (int64_t)count) {
    const qa_qvmy_region_instruction *next = at(instructions, count, end);
    if (next != NULL && next->opcode == QA_QVMY_OP_ENTER) break;
    end++;
  }
  return end;
}

void qa_qvm_validate_weapon_dispatcher(const qa_qvm_weapon_stage *stage,
                                       const qa_qvmy_region_instruction *instructions, size_t instruction_count) {
  int64_t entry = (int64_t)stage->dispatcher.entry;
  int64_t end = function_end(instructions, instruction_count, entry);
  for (size_t i = 0; i < stage->predicate_count; i++) {
    int64_t pc = (int64_t)stage->predicates[i].instruction;
    const qa_qvmy_region_instruction *instruction = at(instructions, instruction_count, pc);
    bool seen = false;
    for (size_t j = 0; j < i; j++) if ((int64_t)stage->predicates[j].instruction == pc) seen = true;
    if (pc <= entry || pc >= end || seen || instruction == NULL ||
        instruction->opcode < QA_QVMY_OP_EQ || instruction->opcode > QA_QVMY_OP_GEF)
      qa_range_error("QVM weapon predicate is not a distinct conditional in its original dispatcher");
  }
  (void)function_end(instructions, instruction_count, (int64_t)stage->request.entry);
  double argument = stage->request.argument;
  if (stage->predicate_count == 0 || stage->settled_count == 0 || stage->request.accepted_count == 0 ||
      !isfinite(argument) || argument != trunc(argument) || argument < 0 || argument >= 62)
    qa_range_error("QVM weapon stage lacks its original decisions or settlement state");
}

void qa_qvm_validate_weapon_stage(const qa_qvm_weapon_stage *stage,
                                  const qa_qvmy_region_instruction *instructions, size_t instruction_count) {
  qa_qvm_validate_weapon_dispatcher(stage, instructions, instruction_count);
  const qa_qvm_weapon_continuation *continuation = &stage->continuation;
  int64_t entry = (int64_t)continuation->entry;
  int64_t limit = function_end(instructions, instruction_count, entry);
  int64_t instruction_index = (int64_t)continuation->instruction;
  const qa_qvmy_region_instruction *branch = at(instructions, instruction_count, instruction_index);
  if (continuation->when_count == 0)
    qa_range_error("QVM weapon continuation lacks its source mode conditions");
  for (size_t i = 0; i < continuation->predicate_count; i++) {
    int64_t pc = (int64_t)continuation->predicates[i].instruction;
    const qa_qvmy_region_instruction *instruction = at(instructions, instruction_count, pc);
    bool seen = pc == instruction_index;
    for (size_t j = 0; j < i; j++) if ((int64_t)continuation->predicates[j].instruction == pc) seen = true;
    if (pc <= entry || pc >= limit || seen || instruction == NULL ||
        instruction->opcode < QA_QVMY_OP_EQ || instruction->opcode > QA_QVMY_OP_GEF)
      qa_range_error("QVM weapon continuation predicate is not a distinct original conditional");
  }
  if (instruction_index <= entry || instruction_index >= limit || branch == NULL ||
      branch->opcode < QA_QVMY_OP_EQ || branch->opcode > QA_QVMY_OP_GEF || branch->operand_width != 4)
    qa_range_error("QVM weapon continuation lacks an original conditional boundary");
  int64_t pc = continuation->original_taken ? instruction_index + 1 : branch->operand;
  int64_t *visited = NULL;
  size_t visited_count = 0;
  for (;;) {
    bool seen = false;
    for (size_t i = 0; i < visited_count; i++) if (visited[i] == pc) seen = true;
    if (pc <= entry || pc >= limit || seen) {
      free(visited);
      qa_range_error("QVM weapon continuation does not take an original return edge");
    }
    visited = realloc(visited, (visited_count + 1) * sizeof *visited);
    if (visited == NULL) qa_range_error("QVM memory allocation failed");
    visited[visited_count++] = pc;
    const qa_qvmy_region_instruction *instruction = at(instructions, instruction_count, pc);
    if (instruction != NULL && instruction->opcode == QA_QVMY_OP_LEAVE) break;
    if (instruction != NULL && instruction->opcode == QA_QVMY_OP_PUSH) {
      pc++;
      continue;
    }
    const qa_qvmy_region_instruction *next = at(instructions, instruction_count, pc + 1);
    if (instruction != NULL && instruction->opcode == QA_QVMY_OP_CONST && next != NULL &&
        next->opcode == QA_QVMY_OP_JUMP) {
      pc = instruction->operand;
      continue;
    }
    free(visited);
    qa_range_error("QVM weapon continuation return edge has source side effects");
  }
  free(visited);
  int64_t previous = instruction_index;
  for (size_t i = 0; i < continuation->call_count; i++) {
    int64_t at_index = (int64_t)continuation->calls[i].instruction;
    const qa_qvm_mod_source_call *call = &continuation->calls[i].call;
    const qa_qvmy_region_instruction *target = at(instructions, instruction_count, at_index - 1);
    const qa_qvmy_region_instruction *instruction = at(instructions, instruction_count, at_index);
    if (at_index <= previous || at_index >= limit || instruction == NULL ||
        instruction->opcode != QA_QVMY_OP_CALL || target == NULL ||
        target->opcode != QA_QVMY_OP_CONST || target->operand != (int64_t)call->entry ||
        call->argument_count != 0 || call->global_count != 0 || strcmp(call->returns, "void") != 0)
      qa_range_error("QVM weapon continuation differs from its ordered original no-argument calls");
    (void)function_end(instructions, instruction_count, (int64_t)call->entry);
    previous = at_index;
  }
  bool has_dispatcher = false;
  for (size_t i = 0; i < continuation->call_count; i++)
    if (continuation->calls[i].call.entry == stage->dispatcher.entry) has_dispatcher = true;
  if (!has_dispatcher) qa_range_error("QVM continuation omits its original weapon dispatcher");
}

static int32_t proceed_call(qa_qvmy_function_call *call) {
  return strcmp(call->execution, "synchronous") == 0 ? call->proceed(call->call_ctx)
                                                     : call->proceed_async(call->call_ctx);
}

typedef struct dispatch_scope {
  qa_actor_id actor;
  qa_qvmy_function_call *call;
} dispatch_scope;

typedef struct evaluation {
  qa_actor_id actor;
  qa_qvm_region_evaluation region;
  const int64_t *inputs;
  size_t input_count;
  bool entered;
} evaluation;

static int32_t scalar_word(qa_qvm_weapon_dispatcher_runtime *dispatcher, qa_actor_id actor, const qa_qvm_item_field *field) {
  int64_t address = dispatcher->operations.pointer(dispatcher->operations.ctx, actor, field->record) +
                    (int64_t)field->offset;
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(dispatcher->operations.module->memory, address, 4, &view);
  return qa_qvm_writable_view_get_int32(&view, 0, true);
}

/* JS ToInt32 for the bitwise mask operand. */
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double wrapped = fmod(trunc(value), 4294967296.0);
  if (wrapped >= 2147483648.0) wrapped -= 4294967296.0;
  else if (wrapped < -2147483648.0) wrapped += 4294967296.0;
  return (int32_t)wrapped;
}

static bool test_item(qa_qvm_weapon_dispatcher_runtime *dispatcher, qa_actor_id actor, const qa_qvm_item_test *test) {
  int32_t scalar = scalar_word(dispatcher, actor, &test->field);
  int32_t value = test->has_mask ? scalar & to_int32(test->mask) : scalar;
  return strcmp(test->comparison, "equals") == 0 ? (double)value == test->value : (double)value <= test->value;
}

bool qa_qvm_weapon_dispatcher_runtime_matches(qa_qvm_weapon_dispatcher_runtime *dispatcher, qa_actor_id actor,
                                      const qa_qvm_item_test *tests, size_t test_count) {
  for (size_t i = 0; i < test_count; i++) if (!test_item(dispatcher, actor, &tests[i])) return false;
  return true;
}

bool qa_qvm_weapon_dispatcher_runtime_settled(qa_qvm_weapon_dispatcher_runtime *dispatcher, qa_actor_id actor) {
  return dispatcher->operations.live(dispatcher->operations.ctx, actor) &&
         qa_qvm_weapon_dispatcher_runtime_matches(dispatcher, actor, dispatcher->definition->settled,
                                          dispatcher->definition->settled_count);
}

qa_item_id qa_qvm_weapon_dispatcher_runtime_active(qa_qvm_weapon_dispatcher_runtime *dispatcher, qa_actor_id actor) {
  int32_t selected = scalar_word(dispatcher, actor, &dispatcher->definition->selection.field);
  if (selected == 0) return NULL;
  for (size_t i = 0; i < dispatcher->definition->selection.value_count; i++) {
    if ((int32_t)dispatcher->definition->selection.values[i].value == selected)
      return dispatcher->definition->selection.values[i].item;
  }
  qa_range_error("Original QVM selected an undeclared source weapon");
}

typedef struct predicate_state {
  qa_qvm_weapon_dispatcher_runtime *dispatcher;
  qa_actor_id actor;
  qa_qvmy_cancellation_scope cancellation;
  bool unselected;
  bool *reached;
} predicate_state;

static bool decide_predicate(void *ctx, bool original, void *call_ctx, qa_qvmy_cancel_fn cancel) {
  predicate_state *state = ctx;
  if (!state->dispatcher->operations.live(state->dispatcher->operations.ctx, state->actor)) {
    cancel(call_ctx, state->cancellation);
  }
  *state->reached = true;
  return state->dispatcher->operations.selected(state->dispatcher->operations.ctx, state->actor)
      ? original : state->unselected;
}

static void push_scope(qa_qvm_weapon_dispatcher_runtime *dispatcher, dispatch_scope scope) {
  if (dispatcher->dispatcher_count == dispatcher->dispatcher_capacity) {
    dispatcher->dispatcher_capacity = dispatcher->dispatcher_capacity == 0 ? 8 : dispatcher->dispatcher_capacity * 2;
    dispatcher->dispatchers = realloc(dispatcher->dispatchers, dispatcher->dispatcher_capacity * sizeof(dispatch_scope));
    if (dispatcher->dispatchers == NULL) qa_range_error("QVM memory allocation failed");
  }
  ((dispatch_scope *)dispatcher->dispatchers)[dispatcher->dispatcher_count++] = scope;
}

static void remove_scope(qa_qvm_weapon_dispatcher_runtime *dispatcher, qa_qvmy_function_call *call) {
  dispatch_scope *scopes = dispatcher->dispatchers;
  for (size_t i = dispatcher->dispatcher_count; i-- > 0;) {
    if (scopes[i].call == call) {
      memmove(&scopes[i], &scopes[i + 1], (dispatcher->dispatcher_count - i - 1) * sizeof *scopes);
      dispatcher->dispatcher_count--;
      return;
    }
  }
}

static int32_t dispatch_hook(void *ctx, qa_qvmy_function_call *call) {
  qa_qvm_weapon_dispatcher_runtime *dispatcher = ctx;
  qa_actor_id actor;
  bool has_actor = dispatcher->operations.actor(dispatcher->operations.ctx,
                                                &dispatcher->definition->dispatcher.actor, call, &actor);
  evaluation *evaluations = dispatcher->evaluations;
  if (dispatcher->evaluation_count != 0 && !evaluations[dispatcher->evaluation_count - 1].entered) {
    evaluation *current = &evaluations[dispatcher->evaluation_count - 1];
    current->entered = true;
    if (!has_actor || !qa_same_actor(actor, current->actor) ||
        !dispatcher->operations.live(dispatcher->operations.ctx, actor))
      qa_range_error("QVM weapon evaluation lost its original source actor");
    return call->evaluate_region(call->call_ctx, &current->region, current->inputs, current->input_count);
  }
  if (!has_actor) return proceed_call(call);
  qa_qvmy_cancellation_scope cancellation =
      dispatcher->operations.cancellation(dispatcher->operations.ctx, actor, call);
  bool reached = false;
  push_scope(dispatcher, (dispatch_scope){actor, call});
  size_t count = dispatcher->definition->predicate_count;
  qa_qvmy_branch_binding *bindings = xmalloc(count * sizeof *bindings);
  predicate_state *states = xmalloc(count * sizeof *states);
  for (size_t i = 0; i < count; i++) {
    states[i] = (predicate_state){dispatcher, actor, cancellation,
                                  dispatcher->definition->predicates[i].unselected, &reached};
    bindings[i].instruction_index = (int64_t)dispatcher->definition->predicates[i].instruction;
    bindings[i].ctx = &states[i];
    bindings[i].decide = decide_predicate;
  }
  call->branches(call->call_ctx, bindings, count);
  void (*cleanup)(void *) = NULL;
  void *cleanup_ctx = NULL;
  if (dispatcher->operations.prepare != NULL)
    dispatcher->operations.prepare(dispatcher->operations.ctx, actor, call, &cleanup, &cleanup_ctx);
  int32_t result = proceed_call(call);
  if (cleanup != NULL) cleanup(cleanup_ctx);
  if (dispatcher->operations.live(dispatcher->operations.ctx, actor))
    dispatcher->operations.completed(dispatcher->operations.ctx, actor, reached);
  remove_scope(dispatcher, call);
  free(bindings);
  free(states);
  return result;
}

static int32_t request_hook(void *ctx, qa_qvmy_function_call *call) {
  qa_qvm_weapon_dispatcher_runtime *dispatcher = ctx;
  if (dispatcher->dispatcher_count == 0) return proceed_call(call);
  dispatch_scope *scopes = dispatcher->dispatchers;
  dispatch_scope scope = scopes[dispatcher->dispatcher_count - 1];
  if (!dispatcher->operations.live(dispatcher->operations.ctx, scope.actor)) return proceed_call(call);
  bool accepted = qa_qvm_weapon_dispatcher_runtime_matches(dispatcher, scope.actor,
      dispatcher->definition->request.accepted, dispatcher->definition->request.accepted_count);
  size_t offset = (size_t)(int64_t)dispatcher->definition->request.argument * 4;
  if (offset > call->words_length || call->words_length - offset < 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  int32_t requested;
  memcpy(&requested, call->words + offset, 4);
  if (!accepted) dispatcher->operations.attempted(dispatcher->operations.ctx, scope.actor, requested);
  int32_t result = proceed_call(call);
  if (!accepted && dispatcher->operations.live(dispatcher->operations.ctx, scope.actor) &&
      qa_qvm_weapon_dispatcher_runtime_matches(dispatcher, scope.actor,
          dispatcher->definition->request.accepted, dispatcher->definition->request.accepted_count))
    dispatcher->operations.accepted(dispatcher->operations.ctx, scope.actor, requested);
  return result;
}

static size_t bind_dispatcher(qa_qvm_weapon_dispatcher_runtime *dispatcher, double entry, qa_qvmy_function_hook_fn hook) {
  qa_guest_callback_reference reference;
  memset(&reference, 0, sizeof reference);
  reference.kind = QA_GUEST_CALLBACK_QVM;
  reference.module = qa_qvm_module_get_profile(dispatcher->operations.module)->module;
  reference.instruction_index = entry;
  return qa_qvm_module_bind_invocation(dispatcher->operations.module, &reference, hook, dispatcher);
}

void qa_qvm_weapon_dispatcher_runtime_init(qa_qvm_weapon_dispatcher_runtime *dispatcher,
                                   const qa_qvm_weapon_stage *definition,
                                   const qa_qvm_weapon_dispatcher_ops *operations) {
  memset(dispatcher, 0, sizeof *dispatcher);
  dispatcher->definition = definition;
  dispatcher->operations = *operations;
  dispatcher->removal_ids[0] = bind_dispatcher(dispatcher, definition->dispatcher.entry, dispatch_hook);
  dispatcher->removal_ids[1] = bind_dispatcher(dispatcher, definition->request.entry, request_hook);
}

void qa_qvm_weapon_dispatcher_runtime_destroy(qa_qvm_weapon_dispatcher_runtime *dispatcher) {
  free(dispatcher->dispatchers);
  free(dispatcher->evaluations);
  memset(dispatcher, 0, sizeof *dispatcher);
}

int32_t qa_qvm_weapon_dispatcher_runtime_evaluate(qa_qvm_weapon_dispatcher_runtime *dispatcher, qa_actor_id actor,
                                          const qa_qvm_region_evaluation *region,
                                          const int64_t *inputs, size_t input_count) {
  if (!dispatcher->operations.live(dispatcher->operations.ctx, actor))
    qa_range_error("QVM weapon evaluation requires its current source actor");
  if (dispatcher->evaluation_count == dispatcher->evaluation_capacity) {
    dispatcher->evaluation_capacity = dispatcher->evaluation_capacity == 0 ? 8 : dispatcher->evaluation_capacity * 2;
    dispatcher->evaluations = realloc(dispatcher->evaluations, dispatcher->evaluation_capacity * sizeof(evaluation));
    if (dispatcher->evaluations == NULL) qa_range_error("QVM memory allocation failed");
  }
  evaluation *current = &((evaluation *)dispatcher->evaluations)[dispatcher->evaluation_count++];
  current->actor = actor;
  current->region = *region;
  current->inputs = inputs;
  current->input_count = input_count;
  current->entered = false;
  int32_t result = qa_qvm_module_call(dispatcher->operations.module, NULL, 0,
                                      (int64_t)dispatcher->definition->dispatcher.entry);
  dispatcher->evaluation_count--;
  return result;
}

void qa_qvm_weapon_dispatcher_runtime_close(qa_qvm_weapon_dispatcher_runtime *dispatcher) {
  qa_qvm_module_unbind(dispatcher->operations.module, dispatcher->removal_ids[1]);
  qa_qvm_module_unbind(dispatcher->operations.module, dispatcher->removal_ids[0]);
  dispatcher->dispatcher_count = 0;
}

/* QvmModWeaponStage. */
typedef struct stage_input {
  qa_actor_id actor;
  bool has_cancellation;
  qa_qvmy_cancellation_scope cancellation;
  bool entered;
} stage_input;

static int64_t resolve_address(qa_qvm_memory *memory, const qa_qvm_mod_input_pointer *pointer, qa_qvmy_function_call *call) {
  int32_t value;
  if (!pointer->is_global) {
    size_t offset = (size_t)(int64_t)pointer->index * 4;
    if (offset > call->words_length || call->words_length - offset < 4)
      qa_range_error("Offset is outside the bounds of the DataView");
    memcpy(&value, call->words + offset, 4);
  } else {
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(memory, (int64_t)pointer->address, 4, &view);
    value = qa_qvm_writable_view_get_int32(&view, 0, true);
  }
  for (size_t i = 0; i < pointer->indirections.count; i++) {
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(memory, (int64_t)value + (int64_t)pointer->indirections.values[i], 4, &view);
    value = qa_qvm_writable_view_get_int32(&view, 0, true);
  }
  return (int64_t)value + (int64_t)pointer->offset;
}

static bool stage_actor_lookup(qa_qvm_mod_weapon_stage *stage, const qa_qvm_weapon_actor *source,
                               qa_qvmy_function_call *call, qa_actor_id *out) {
  if (stage->application_count == 0) return false;
  qa_actor_id actor = stage->applications[stage->application_count - 1]->identity.actor;
  if (!stage->operations.live(stage->operations.ctx, actor)) return false;
  int64_t address = resolve_address(stage->operations.module->memory, &source->pointer, call);
  if (address != stage->operations.pointer(stage->operations.ctx, actor, source->record)) return false;
  *out = actor;
  return true;
}

static qa_qvmy_cancellation_scope stage_cancellation(qa_qvm_mod_weapon_stage *stage, qa_actor_id actor,
                                                     qa_qvmy_function_call *call) {
  stage_input *inputs = stage->inputs;
  for (size_t i = stage->input_count; i-- > 0;) {
    if (qa_same_actor(inputs[i].actor, actor) && inputs[i].has_cancellation)
      return inputs[i].cancellation;
  }
  return call->cancellation_scope(call->call_ctx);
}

typedef struct stage_adapter {
  qa_qvm_mod_weapon_stage *stage;
} stage_adapter;

static bool adapter_actor(void *ctx, const qa_qvm_weapon_actor *source, qa_qvmy_function_call *call, qa_actor_id *out) {
  return stage_actor_lookup(((stage_adapter *)ctx)->stage, source, call, out);
}

static qa_qvmy_cancellation_scope adapter_cancellation(void *ctx, qa_actor_id actor, qa_qvmy_function_call *call) {
  return stage_cancellation(((stage_adapter *)ctx)->stage, actor, call);
}


static int32_t input_hook(void *ctx, qa_qvmy_function_call *call) {
  qa_qvm_mod_weapon_stage *stage = ctx;
  if (stage->input_count == 0) return proceed_call(call);
  stage_input *inputs = stage->inputs;
  stage_input *input = &inputs[stage->input_count - 1];
  if (input->entered) return proceed_call(call);
  input->entered = true;
  input->has_cancellation = true;
  input->cancellation = call->cancellation_scope(call->call_ctx);
  if (!stage->operations.live(stage->operations.ctx, input->actor)) {
    call->cancel_function(call->call_ctx, input->cancellation);
    qa_range_error("QVM cancellation requires the current source invocation");
  }
  int32_t result = proceed_call(call);
  if (!stage->operations.live(stage->operations.ctx, input->actor)) {
    call->cancel_function(call->call_ctx, input->cancellation);
    qa_range_error("QVM cancellation requires the current source invocation");
  }
  return result;
}

typedef struct continuation_state {
  qa_qvm_mod_weapon_stage *stage;
  qa_actor_id actor;
  qa_qvmy_cancellation_scope cancellation;
  bool unselected;
  bool *continued;
} continuation_state;

static bool decide_continuation_predicate(void *ctx, bool original, void *call_ctx, qa_qvmy_cancel_fn cancel) {
  continuation_state *state = ctx;
  if (!state->stage->operations.live(state->stage->operations.ctx, state->actor))
    cancel(call_ctx, state->cancellation);
  return state->stage->operations.selected(state->stage->operations.ctx, state->actor)
      ? original : state->unselected;
}

static bool decide_continuation_boundary(void *ctx, bool original, void *call_ctx, qa_qvmy_cancel_fn cancel) {
  continuation_state *state = ctx;
  qa_qvm_mod_weapon_stage *stage = state->stage;
  if (!stage->operations.live(stage->operations.ctx, state->actor))
    cancel(call_ctx, state->cancellation);
  if (original != stage->definition->continuation.original_taken ||
      !qa_qvm_weapon_dispatcher_runtime_matches(&stage->dispatcher, state->actor,
          stage->definition->continuation.when, stage->definition->continuation.when_count))
    return original;
  *state->continued = true;
  return !original;
}

static void write_vector_at(qa_qvm_memory *memory, int64_t address, qa_vec3 value) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(memory, address, 12, &view);
  qa_qvm_writable_view_set_float32(&view, 0, value.x, true);
  qa_qvm_writable_view_set_float32(&view, 4, value.y, true);
  qa_qvm_writable_view_set_float32(&view, 8, value.z, true);
}

static void write_word_at(qa_qvm_memory *memory, int64_t address, int32_t value) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(memory, address, 4, &view);
  qa_qvm_writable_view_set_int32(&view, 0, value, true);
}

static int32_t continue_hook(void *ctx, qa_qvmy_function_call *call) {
  qa_qvm_mod_weapon_stage *stage = ctx;
  qa_actor_id actor;
  if (!stage_actor_lookup(stage, &stage->definition->continuation.actor, call, &actor))
    return proceed_call(call);
  qa_qvmy_cancellation_scope cancellation = stage_cancellation(stage, actor, call);
  int64_t movement = resolve_address(stage->operations.module->memory,
                                     &stage->definition->continuation.projection.movement, call);
  bool continued = false;
  size_t count = stage->definition->continuation.predicate_count + 1;
  qa_qvmy_branch_binding *bindings = xmalloc(count * sizeof *bindings);
  continuation_state *states = xmalloc(count * sizeof *states);
  for (size_t i = 0; i + 1 < count; i++) {
    states[i] = (continuation_state){stage, actor, cancellation,
        stage->definition->continuation.predicates[i].unselected, &continued};
    bindings[i].instruction_index = (int64_t)stage->definition->continuation.predicates[i].instruction;
    bindings[i].ctx = &states[i];
    bindings[i].decide = decide_continuation_predicate;
  }
  states[count - 1] = (continuation_state){stage, actor, cancellation, false, &continued};
  bindings[count - 1].instruction_index = (int64_t)stage->definition->continuation.instruction;
  bindings[count - 1].ctx = &states[count - 1];
  bindings[count - 1].decide = decide_continuation_boundary;
  call->branches(call->call_ctx, bindings, count);
  int32_t result = proceed_call(call);
  if (continued) {
    const qa_qvm_weapon_projection *projection = &stage->definition->continuation.projection;
    qa_qvm_posture posture = stage->operations.posture(stage->operations.ctx, actor);
    write_vector_at(stage->operations.module->memory, movement + (int64_t)projection->minimum, posture.bounds.min);
    write_vector_at(stage->operations.module->memory, movement + (int64_t)projection->maximum, posture.bounds.max);
    write_word_at(stage->operations.module->memory,
        stage->operations.pointer(stage->operations.ctx, actor, projection->view_height.record) +
            (int64_t)projection->view_height.offset, posture.view_height);
    write_word_at(stage->operations.module->memory,
        stage->operations.pointer(stage->operations.ctx, actor, projection->ground.record) +
            (int64_t)projection->ground.offset, posture.ground);
    for (size_t i = 0; i < stage->definition->continuation.call_count; i++) {
      if (!stage->operations.live(stage->operations.ctx, actor)) {
        call->cancel_function(call->call_ctx, cancellation);
        qa_range_error("QVM cancellation requires the current source invocation");
      }
      (void)stage->operations.invoke(stage->operations.ctx, actor, &stage->definition->continuation.calls[i].call);
    }
  }
  free(bindings);
  free(states);
  return result;
}

static size_t bind_stage(qa_qvm_mod_weapon_stage *stage, double entry, qa_qvmy_function_hook_fn hook) {
  qa_guest_callback_reference reference;
  memset(&reference, 0, sizeof reference);
  reference.kind = QA_GUEST_CALLBACK_QVM;
  reference.module = qa_qvm_module_get_profile(stage->operations.module)->module;
  reference.instruction_index = entry;
  return qa_qvm_module_bind_invocation(stage->operations.module, &reference, hook, stage);
}

/* Dispatcher operation forwards onto the stage operations. */
static qa_qvm_mod_weapon_stage *stage_of(void *ctx) { return ((stage_adapter *)ctx)->stage; }
static int64_t fwd_pointer(void *ctx, qa_actor_id actor, const char *record) {
  qa_qvm_mod_weapon_stage *stage = stage_of(ctx);
  return stage->operations.pointer(stage->operations.ctx, actor, record);
}
static bool fwd_live(void *ctx, qa_actor_id actor) {
  qa_qvm_mod_weapon_stage *stage = stage_of(ctx);
  return stage->operations.live(stage->operations.ctx, actor);
}
static bool fwd_selected(void *ctx, qa_actor_id actor) {
  qa_qvm_mod_weapon_stage *stage = stage_of(ctx);
  return stage->operations.selected(stage->operations.ctx, actor);
}
static void fwd_attempted(void *ctx, qa_actor_id actor, int32_t value) {
  qa_qvm_mod_weapon_stage *stage = stage_of(ctx);
  stage->operations.attempted(stage->operations.ctx, actor, value);
}
static void fwd_accepted(void *ctx, qa_actor_id actor, int32_t value) {
  qa_qvm_mod_weapon_stage *stage = stage_of(ctx);
  stage->operations.accepted(stage->operations.ctx, actor, value);
}
static void fwd_completed(void *ctx, qa_actor_id actor, bool reached) {
  (void)reached;
  qa_qvm_mod_weapon_stage *stage = stage_of(ctx);
  stage->operations.completed(stage->operations.ctx, actor);
}

void qa_qvm_mod_weapon_stage_init(qa_qvm_mod_weapon_stage *stage, const qa_qvm_weapon_stage *definition,
                                  int64_t input_entry, const qa_qvm_mod_weapon_stage_operations *operations) {
  memset(stage, 0, sizeof *stage);
  stage->definition = definition;
  stage->operations = *operations;
  stage_adapter *adapter = xmalloc(sizeof *adapter);
  adapter->stage = stage;
  qa_qvm_weapon_dispatcher_ops dispatcher_ops;
  memset(&dispatcher_ops, 0, sizeof dispatcher_ops);
  dispatcher_ops.module = operations->module;
  dispatcher_ops.ctx = adapter;
  dispatcher_ops.actor = adapter_actor;
  dispatcher_ops.pointer = fwd_pointer;
  dispatcher_ops.live = fwd_live;
  dispatcher_ops.selected = fwd_selected;
  dispatcher_ops.cancellation = adapter_cancellation;
  dispatcher_ops.attempted = fwd_attempted;
  dispatcher_ops.accepted = fwd_accepted;
  dispatcher_ops.completed = fwd_completed;
  qa_qvm_weapon_dispatcher_runtime_init(&stage->dispatcher, definition, &dispatcher_ops);
  stage->removal_ids[0] = bind_stage(stage, (double)input_entry, input_hook);
  stage->removal_ids[1] = bind_stage(stage, definition->continuation.entry, continue_hook);
}

void qa_qvm_mod_weapon_stage_destroy(qa_qvm_mod_weapon_stage *stage) {
  free(stage->dispatcher.operations.ctx);
  qa_qvm_weapon_dispatcher_runtime_destroy(&stage->dispatcher);
  free(stage->applications);
  free(stage->inputs);
  memset(stage, 0, sizeof *stage);
}

typedef struct open_state {
  qa_qvm_mod_weapon_stage *stage;
  qa_mod_client_application *application;
} open_state;

static void run_open_remove(void *ctx) {
  open_state *state = ctx;
  qa_qvm_mod_weapon_stage *stage = state->stage;
  for (size_t i = stage->application_count; i-- > 0;) {
    if (stage->applications[i] == state->application) {
      memmove(&stage->applications[i], &stage->applications[i + 1],
              (stage->application_count - i - 1) * sizeof *stage->applications);
      stage->application_count--;
      break;
    }
  }
  free(state);
}

void qa_qvm_mod_weapon_stage_open(qa_qvm_mod_weapon_stage *stage, qa_mod_client_application *application,
                                  void (**remove)(void *ctx), void **remove_ctx) {
  if (stage->application_count == stage->application_capacity) {
    stage->application_capacity = stage->application_capacity == 0 ? 8 : stage->application_capacity * 2;
    stage->applications = realloc(stage->applications, stage->application_capacity * sizeof *stage->applications);
    if (stage->applications == NULL) qa_range_error("QVM memory allocation failed");
  }
  stage->applications[stage->application_count++] = application;
  open_state *state = xmalloc(sizeof *state);
  state->stage = stage;
  state->application = application;
  *remove = run_open_remove;
  *remove_ctx = state;
}

int32_t qa_qvm_mod_weapon_stage_apply(qa_qvm_mod_weapon_stage *stage, qa_actor_id actor,
                                      qa_qvm_weapon_run_fn run, void *run_ctx) {
  if (stage->input_count == stage->input_capacity) {
    stage->input_capacity = stage->input_capacity == 0 ? 8 : stage->input_capacity * 2;
    stage->inputs = realloc(stage->inputs, stage->input_capacity * sizeof(stage_input));
    if (stage->inputs == NULL) qa_range_error("QVM memory allocation failed");
  }
  stage_input *input = &((stage_input *)stage->inputs)[stage->input_count++];
  memset(input, 0, sizeof *input);
  input->actor = actor;
  int32_t result = run(run_ctx);
  stage->input_count--;
  return result;
}

void qa_qvm_mod_weapon_stage_cancel_retired(qa_qvm_mod_weapon_stage *stage, qa_actor_id actor) {
  if (stage->operations.live(stage->operations.ctx, actor)) return;
  stage_input *inputs = stage->inputs;
  for (size_t i = stage->input_count; i-- > 0;) {
    if (qa_same_actor(inputs[i].actor, actor) && inputs[i].has_cancellation)
      qa_qvm_module_cancel_function(stage->operations.module, inputs[i].cancellation);
  }
}

bool qa_qvm_mod_weapon_stage_settled(qa_qvm_mod_weapon_stage *stage, qa_actor_id actor) {
  return qa_qvm_weapon_dispatcher_runtime_settled(&stage->dispatcher, actor);
}

qa_item_id qa_qvm_mod_weapon_stage_active(qa_qvm_mod_weapon_stage *stage, qa_actor_id actor) {
  return qa_qvm_weapon_dispatcher_runtime_active(&stage->dispatcher, actor);
}

void qa_qvm_mod_weapon_stage_close(qa_qvm_mod_weapon_stage *stage) {
  qa_qvm_module_unbind(stage->operations.module, stage->removal_ids[1]);
  qa_qvm_module_unbind(stage->operations.module, stage->removal_ids[0]);
  stage->application_count = 0;
  qa_qvm_weapon_dispatcher_runtime_close(&stage->dispatcher);
}
