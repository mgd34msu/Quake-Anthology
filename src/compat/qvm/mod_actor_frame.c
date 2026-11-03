/* See include/qa/compat/qvm/mod_actor_frame.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/mod_actor_frame.h"

#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/operations.h"
#include "qa/foundation/support.h"

#define QVM_MAX_PRIVATE_ARGUMENT_WORDS QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS

static const qa_qvm_instruction *at(const qa_qvm_instruction *instructions,
                                            size_t count, int64_t pc) {
  if (pc < 0 || (uint64_t)pc >= count) return NULL;
  return &instructions[pc];
}

void qa_qvm_actor_bootstrap(const int64_t *stores, size_t store_count,
                            const qa_qvm_instruction *instructions, size_t instruction_count,
                            int64_t initialized_data_length, int64_t data_length, int64_t bss_length,
                            const qa_qvm_mod_actor_record *records, size_t record_count,
                            qa_qvm_actor_store *out) {
  for (size_t i = 0; i < store_count; i++) {
    int64_t pc = stores[i];
    const qa_qvm_instruction *store = at(instructions, instruction_count, pc);
    const qa_qvm_instruction *address = at(instructions, instruction_count, pc - 2);
    const qa_qvm_instruction *value = at(instructions, instruction_count, pc - 1);
    if (store == NULL || store->opcode != QA_QVMY_OP_STORE4 || address == NULL ||
        address->opcode != QA_QVMY_OP_CONST || value == NULL || value->opcode != QA_QVMY_OP_CONST)
      qa_range_error("QVM source bootstrap is not an original constant store");
    int64_t offset = address->operand;
    int64_t end = initialized_data_length + bss_length;
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) if (out[j].address == offset) duplicate = true;
    bool overlaps = false;
    for (size_t r = 0; r < record_count; r++) {
      int64_t record_end = (int64_t)(records[r].address + records[r].stride * records[r].capacity);
      if (offset < record_end && offset + 4 > (int64_t)records[r].address) overlaps = true;
    }
    if (offset < 0 || offset % 4 != 0 || offset + 4 > end ||
        (offset < initialized_data_length && offset + 4 > data_length) || duplicate || overlaps)
      qa_range_error("QVM source bootstrap overlaps projected or nonwritable storage");
    out[i].address = offset;
    out[i].value = value->operand;
  }
}

void qa_qvm_validate_mod_actor_frame(const qa_qvm_mod_actor_frame *definition,
                                     const qa_qvm_instruction *instructions, size_t instruction_count,
                                     int64_t initialized_data_length, int64_t bss_length,
                                     const qa_qvm_mod_actor_record *record, int64_t inuse) {
  int64_t entry = (int64_t)definition->call.entry;
  const qa_qvm_instruction *enter = at(instructions, instruction_count, entry);
  if (enter == NULL || enter->opcode != QA_QVMY_OP_ENTER ||
      strcmp(definition->call.returns, "void") != 0 || definition->owned_count == 0)
    qa_range_error("QVM actor frame requires its original void caller and entity loops");
  int64_t end = entry + 1;
  while (end < (int64_t)instruction_count) {
    const qa_qvm_instruction *ins = at(instructions, instruction_count, end);
    if (ins != NULL && ins->opcode == QA_QVMY_OP_ENTER) break;
    end++;
  }
  /* decisions set over at most owned_count + 1 entries. */
  size_t decision_capacity = definition->owned_count + 1;
  int64_t *decisions = malloc(decision_capacity * sizeof *decisions);
  if (decisions == NULL) qa_range_error("QVM memory allocation failed");
  size_t decision_count = 0;
  {
    int64_t pc = (int64_t)definition->end.instruction;
    const qa_qvm_instruction *ins = at(instructions, instruction_count, pc);
    if (pc <= entry || pc >= end || ins == NULL || ins->opcode < QA_QVMY_OP_EQ || ins->opcode > QA_QVMY_OP_GEF) {
      free(decisions);
      qa_range_error("QVM actor frame predicate is not a distinct original conditional");
    }
    decisions[decision_count++] = pc;
  }
  for (size_t f = 0; f < definition->owned_count; f++) {
    const qa_qvm_mod_actor_frame_owned *filter = &definition->owned[f];
    int64_t pc = (int64_t)filter->instruction;
    const qa_qvm_instruction *ins = at(instructions, instruction_count, pc);
    bool seen = false;
    for (size_t d = 0; d < decision_count; d++) if (decisions[d] == pc) seen = true;
    if (pc <= entry || pc >= end || seen || ins == NULL || ins->opcode < QA_QVMY_OP_EQ || ins->opcode > QA_QVMY_OP_GEF) {
      free(decisions);
      qa_range_error("QVM actor frame predicate is not a distinct original conditional");
    }
    decisions[decision_count++] = pc;
    int64_t local_pc = (int64_t)filter->local_instruction;
    const qa_qvm_instruction *local = at(instructions, instruction_count, local_pc);
    const qa_qvm_instruction *offset = at(instructions, instruction_count, local_pc + 2);
    const qa_qvm_instruction *zero = at(instructions, instruction_count, local_pc + 5);
    const qa_qvm_instruction *load1 = at(instructions, instruction_count, local_pc + 1);
    const qa_qvm_instruction *add = at(instructions, instruction_count, local_pc + 3);
    const qa_qvm_instruction *load2 = at(instructions, instruction_count, local_pc + 4);
    if (local_pc != pc - 6 || local == NULL || local->opcode != QA_QVMY_OP_LOCAL ||
        local->operand < 8 || local->operand + 4 > enter->operand ||
        load1 == NULL || load1->opcode != QA_QVMY_OP_LOAD4 ||
        offset == NULL || offset->opcode != QA_QVMY_OP_CONST || offset->operand != inuse ||
        add == NULL || add->opcode != QA_QVMY_OP_ADD ||
        load2 == NULL || load2->opcode != QA_QVMY_OP_LOAD4 ||
        zero == NULL || zero->opcode != QA_QVMY_OP_CONST || zero->operand != 0 ||
        ins->opcode != QA_QVMY_OP_NE || inuse + 4 > (int64_t)record->stride) {
      free(decisions);
      qa_range_error("QVM actor frame filter differs from its original local entity/in-use predicate");
    }
  }
  free(decisions);
  const qa_qvm_mod_actor_frame_clock *clock = &definition->clock;
  int64_t argument_index = (int64_t)clock->argument;
  const qa_qvm_mod_value *argument = NULL;
  if (argument_index >= 0 && (uint64_t)argument_index < definition->call.argument_count)
    argument = &definition->call.arguments[argument_index];
  int64_t store = (int64_t)clock->store;
  const qa_qvm_instruction *address = at(instructions, instruction_count, store - 3);
  const qa_qvm_instruction *local = at(instructions, instruction_count, store - 2);
  const qa_qvm_instruction *load = at(instructions, instruction_count, store - 1);
  const qa_qvm_instruction *store_ins = at(instructions, instruction_count, store);
  bool global_matches = false;
  for (size_t g = 0; g < definition->call.global_count; g++)
    if (definition->call.globals[g].address == clock->address) global_matches = true;
  if (clock->argument != (double)argument_index || argument_index < 0 ||
      argument_index >= QVM_MAX_PRIVATE_ARGUMENT_WORDS || argument == NULL ||
      strcmp(argument->kind, "time") != 0 || strcmp(argument->time_input, "time") != 0 ||
      strcmp(argument->units, "milliseconds") != 0 || strcmp(argument->encoding, "int32") != 0 ||
      clock->address != (double)(int64_t)clock->address || (int64_t)clock->address < 0 ||
      (int64_t)clock->address % 4 != 0 ||
      (int64_t)clock->address + 4 > initialized_data_length + bss_length ||
      store <= entry || store >= end || address == NULL || address->opcode != QA_QVMY_OP_CONST ||
      address->operand != (int64_t)clock->address || local == NULL || local->opcode != QA_QVMY_OP_LOCAL ||
      local->operand != enter->operand + 8 + argument_index * 4 ||
      load == NULL || load->opcode != QA_QVMY_OP_LOAD4 ||
      store_ins == NULL || store_ins->opcode != QA_QVMY_OP_STORE4 || global_matches)
    qa_range_error("QVM actor frame clock differs from its original argument store");
}

typedef struct branch_state {
  qa_qvm_mod_actor_frame_runtime *frame;
  qa_qvm_module_function_call *call;
  int64_t stack;
  size_t filter_index;
  bool is_end;
  qa_qvm_cancellation_scope cancellation;
} branch_state;

static bool decide_owned(void *ctx, bool taken, void *call_ctx, qa_qvm_cancel_fn cancel) {
  (void)call_ctx;
  (void)cancel;
  branch_state *state = ctx;
  if (!taken) return false;
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(state->call->guest,
                          state->stack + state->frame->local_offsets[state->filter_index], 4, &view);
  int32_t pointer = qa_qvm_writable_view_get_int32(&view, 0, true);
  return state->frame->owned(state->frame->owned_ctx, pointer);
}

static bool decide_end(void *ctx, bool taken, void *call_ctx, qa_qvm_cancel_fn cancel) {
  branch_state *state = ctx;
  if (taken == state->frame->definition->end.completed_taken) {
    state->frame->active_completed = true;
    { cancel(call_ctx, state->cancellation); qa_range_error("QVM function scope cancelled"); }
  }
  return taken;
}

typedef struct hook_storage {
  qa_qvm_module_branch_binding *bindings;
  branch_state *states;
} hook_storage;

static void hook_storage_unwind(void *ctx) {
  hook_storage *storage = ctx;
  free(storage->bindings);
  free(storage->states);
}

static int32_t frame_hook(void *ctx, qa_qvm_module_function_call *call) {
  qa_qvm_mod_actor_frame_runtime *frame = ctx;
  if (!frame->has_active)
    return strcmp(call->execution, "synchronous") == 0 ? call->proceed(call->call_ctx)
                                                       : call->proceed_async(call->call_ctx);
  qa_qvm_cancellation_scope cancellation = call->cancellation_scope(call->call_ctx);
  int64_t stack = call->words_offset - 8 - frame->enter_operand;
  size_t count = frame->local_count + 1;
  qa_qvm_module_branch_binding *bindings = malloc(count * sizeof *bindings);
  branch_state *states = malloc(count * sizeof *states);
  if (bindings == NULL || states == NULL) qa_range_error("QVM memory allocation failed");
  for (size_t i = 0; i < frame->local_count; i++) {
    states[i] = (branch_state){frame, call, stack, i, false, cancellation};
    bindings[i].instruction_index = (int64_t)frame->definition->owned[i].instruction;
    bindings[i].ctx = &states[i];
    bindings[i].decide = decide_owned;
  }
  states[frame->local_count] = (branch_state){frame, call, stack, 0, true, cancellation};
  bindings[frame->local_count].instruction_index = (int64_t)frame->definition->end.instruction;
  bindings[frame->local_count].ctx = &states[frame->local_count];
  bindings[frame->local_count].decide = decide_end;
  call->branches(call->call_ctx, bindings, count);
  hook_storage storage = {bindings, states};
  qa_cleanup guard;
  qa_cleanup_push(&guard, hook_storage_unwind, &storage);
  int32_t result = strcmp(call->execution, "synchronous") == 0 ? call->proceed(call->call_ctx)
                                                                : call->proceed_async(call->call_ctx);
  qa_cleanup_pop(&guard);
  hook_storage_unwind(&storage);
  return result;
}

void qa_qvm_mod_actor_frame_runtime_init(qa_qvm_mod_actor_frame_runtime *frame,
                                         const qa_qvm_mod_actor_frame *definition, qa_qvm_module *module,
                                         const qa_qvm_instruction *instructions, size_t instruction_count,
                                         qa_qvm_actor_owned_fn owned, void *owned_ctx) {
  memset(frame, 0, sizeof *frame);
  const qa_qvm_instruction *enter = at(instructions, instruction_count, (int64_t)definition->call.entry);
  if (enter == NULL || enter->opcode != QA_QVMY_OP_ENTER) qa_range_error("Missing original actor frame");
  frame->definition = definition;
  frame->module = module;
  frame->owned = owned;
  frame->owned_ctx = owned_ctx;
  frame->enter_operand = enter->operand;
  frame->local_count = definition->owned_count;
  frame->local_offsets = malloc((frame->local_count == 0 ? 1 : frame->local_count) * sizeof *frame->local_offsets);
  if (frame->local_offsets == NULL) qa_range_error("QVM memory allocation failed");
  for (size_t i = 0; i < frame->local_count; i++) {
    const qa_qvm_instruction *local =
        at(instructions, instruction_count, (int64_t)definition->owned[i].local_instruction);
    if (local == NULL || local->opcode != QA_QVMY_OP_LOCAL) qa_range_error("Missing original actor loop local");
    frame->local_offsets[i] = local->operand;
  }
  qa_guest_callback_reference reference;
  memset(&reference, 0, sizeof reference);
  reference.kind = QA_GUEST_CALLBACK_QVM;
  reference.module = qa_qvm_module_get_profile(module)->module;
  reference.instruction_index = definition->call.entry;
  frame->removal_id = qa_qvm_module_bind_invocation(module, &reference, frame_hook, frame);
}

/* TS finally { this.active = null; }. */
static void run_unwind(void *ctx) {
  qa_qvm_mod_actor_frame_runtime *frame = ctx;
  frame->has_active = false;
}

bool qa_qvm_mod_actor_frame_runtime_run(qa_qvm_mod_actor_frame_runtime *frame,
                                        qa_qvm_actor_invoke_fn invoke, void *invoke_ctx) {
  if (frame->has_active) qa_range_error("QVM actor frame is already executing");
  frame->has_active = true;
  frame->active_completed = false;
  qa_cleanup guard;
  qa_cleanup_push(&guard, run_unwind, frame);
  invoke(invoke_ctx);
  qa_cleanup_pop(&guard);
  bool completed = frame->active_completed;
  frame->has_active = false;
  return completed;
}

void qa_qvm_mod_actor_frame_runtime_close(qa_qvm_mod_actor_frame_runtime *frame) {
  qa_qvm_module_unbind(frame->module, frame->removal_id);
}

void qa_qvm_mod_actor_frame_runtime_destroy(qa_qvm_mod_actor_frame_runtime *frame) {
  free(frame->local_offsets);
  memset(frame, 0, sizeof *frame);
}
