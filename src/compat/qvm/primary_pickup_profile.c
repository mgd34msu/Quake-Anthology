/* See include/qa/compat/qvm/primary_pickup_profile.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/primary_pickup_profile.h"

#include <stdlib.h>
#include <string.h>

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

static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out)) qa_range_error("QVM memory allocation failed");
  return out;
}

static int64_t integer_field(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader at = field(reader, name);
  int64_t value = (int64_t)qa_save_reader_integer_min(&at, minimum);
  qa_save_reader_destroy(&at);
  return value;
}

static const qa_qvm_instruction *at_insn(const qa_resolved_qvm_artifact *artifact, int64_t index) {
  if (index < 0 || (uint64_t)index >= artifact->image.instruction_count) return NULL;
  return &artifact->image.instructions[index];
}

static int64_t read_entry(const qa_resolved_qvm_artifact *artifact, qa_save_reader *reader) {
  int64_t value = (int64_t)qa_save_reader_integer_min(reader, 0);
  const qa_qvm_instruction *instruction = at_insn(artifact, value);
  if (instruction == NULL || instruction->opcode != QA_QVMY_OP_ENTER)
    qa_save_reader_fail(reader, "pickup requires an original function entry");
  return value;
}

static int64_t owner_end(const qa_resolved_qvm_artifact *artifact, int64_t owner) {
  int64_t end = owner + 1;
  while (end < (int64_t)artifact->image.instruction_count) {
    const qa_qvm_instruction *instruction = at_insn(artifact, end);
    if (instruction != NULL && instruction->opcode == QA_QVMY_OP_ENTER) break;
    end++;
  }
  return end;
}

static double *read_calls(const qa_resolved_qvm_artifact *artifact, qa_save_reader *reader, int64_t owner, size_t *out_count) {
  size_t count = qa_save_reader_list_count(reader);
  double *calls = xmalloc(count * sizeof *calls);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value = item(reader, i);
    int64_t index = (int64_t)qa_save_reader_integer_min(&value, 1);
    const qa_qvm_instruction *call = at_insn(artifact, index);
    const qa_qvm_instruction *target = at_insn(artifact, index - 1);
    if (call == NULL || call->opcode != QA_QVMY_OP_CALL || target == NULL ||
        target->opcode != QA_QVMY_OP_CONST || target->operand != owner)
      qa_save_reader_fail(&value, "pickup call differs from its declared original target");
    calls[i] = (double)index;
    qa_save_reader_destroy(&value);
  }
  *out_count = count;
  return calls;
}

static void read_function_calls(const qa_resolved_qvm_artifact *artifact, qa_save_reader *reader,
                                double *out_entry, const double **out_calls, size_t *out_count) {
  qa_save_reader entry_reader = field(reader, "entry");
  int64_t entry = read_entry(artifact, &entry_reader);
  qa_save_reader_destroy(&entry_reader);
  qa_save_reader calls_reader = field(reader, "calls");
  *out_calls = read_calls(artifact, &calls_reader, entry, out_count);
  qa_save_reader_destroy(&calls_reader);
  *out_entry = (double)entry;
}

static int64_t read_argument(qa_save_reader *reader) {
  int64_t index = (int64_t)qa_save_reader_integer_min(reader, 0);
  if (index >= QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS) qa_save_reader_fail(reader, "pickup argument is outside the source ABI");
  return index;
}

static void read_evaluation(const qa_resolved_qvm_artifact *artifact, qa_save_reader *reader, int64_t owner,
                            qa_qvm_region_evaluation *out) {
  out->entry = integer_field(reader, "entry", 0);
  out->join = integer_field(reader, "join", 0);
  qa_save_reader inputs_reader = field(reader, "inputs");
  size_t count = qa_save_reader_list_count(&inputs_reader);
  int64_t *inputs = xmalloc(count * sizeof *inputs);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value = item(&inputs_reader, i);
    inputs[i] = (int64_t)qa_save_reader_integer_min(&value, 8);
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
    out->result = (int64_t)qa_save_reader_integer_min(&result_reader, 8);
  }
  qa_save_reader_destroy(&result_reader);
  qa_qvm_qualify_region_evaluation(artifact->image.instructions, artifact->image.instruction_count, owner, out, "source");
}

typedef struct branch_decision {
  bool taken;
} branch_decision;

static bool decide_branch(void *ctx, bool original_taken, void *call_ctx, qa_qvm_cancel_fn cancel) {
  (void)original_taken;
  (void)call_ctx;
  (void)cancel;
  return ((branch_decision *)ctx)->taken;
}

bool qa_qvm_primary_pickup_grant_eligible(void *ctx, qa_qvm_pickup_eligibility context) {
  qa_qvm_pickup_branches *captured = ctx;
  qa_qvm_module_function_call *call = context.call;
  size_t branch_count = captured == NULL ? 0 : captured->branch_count;
  qa_qvm_module_branch_binding *bindings = xmalloc(branch_count * sizeof *bindings);
  branch_decision *decisions = xmalloc(branch_count * sizeof *decisions);
  for (size_t i = 0; i < branch_count; i++) {
    decisions[i].taken = captured->branches[i].taken;
    bindings[i].instruction_index = captured->branches[i].instruction_index;
    bindings[i].ctx = &decisions[i];
    bindings[i].decide = decide_branch;
  }
  call->branches(call->call_ctx, bindings, branch_count);
  int32_t result = strcmp(call->execution, "asynchronous") == 0 ? call->proceed_async(call->call_ctx)
                                                                : call->proceed(call->call_ctx);
  free(bindings);
  free(decisions);
  return result != 0;
}

void qa_qvm_read_primary_pickup_profile(const qa_save_reader *reader, const qa_resolved_qvm_artifact *artifact,
                                        qa_qvm_pickup_profile *out) {
  memset(out, 0, sizeof *out);
  out->module = artifact->module;
  out->abi_profile = artifact->has_abi_profile ? artifact->abi_profile : "q3-modern";
  if (strcmp(artifact->role, "qagame") != 0)
    qa_save_reader_fail(reader, "primary pickup declarations require a qagame ABI");
  qa_save_reader gate_reader = field(reader, "gate");
  read_function_calls(artifact, &gate_reader, &out->gate_entry, &out->gate_calls, &out->gate_call_count);
  qa_save_reader item_argument = field(&gate_reader, "itemArgument");
  out->gate_item_argument = (double)read_argument(&item_argument);
  qa_save_reader_destroy(&item_argument);
  qa_save_reader player_argument = field(&gate_reader, "playerArgument");
  out->gate_player_argument = (double)read_argument(&player_argument);
  qa_save_reader_destroy(&player_argument);
  qa_save_reader_destroy(&gate_reader);

  qa_save_reader grants_reader = field(reader, "grants");
  size_t grant_count = qa_save_reader_list_count(&grants_reader);
  qa_qvm_pickup_grant *grants = xmalloc(grant_count * sizeof *grants);
  out->grants = grants;
  out->grant_count = grant_count;
  for (size_t g = 0; g < grant_count; g++) {
    qa_save_reader at = item(&grants_reader, g);
    qa_qvm_pickup_grant *grant = &grants[g];
    memset(grant, 0, sizeof *grant);
    read_function_calls(artifact, &at, &grant->entry, &grant->calls, &grant->call_count);
    qa_save_reader op = field(&at, "operation");
    qa_save_reader op_kind = field(&op, "kind");
    static const char *const op_kinds[] = {"return", "region"};
    const char *kind = qa_save_reader_choice_string(&op_kind, op_kinds, 2);
    qa_save_reader_destroy(&op_kind);
    grant->is_region = strcmp(kind, "region") == 0;
    if (strcmp(kind, "return") == 0) {
      grant->accepted_return = (double)integer_field(&op, "acceptedReturn", grant->entry + 1);
      const qa_qvm_instruction *value = at_insn(artifact, (int64_t)grant->accepted_return);
      const qa_qvm_instruction *leave = at_insn(artifact, (int64_t)grant->accepted_return + 1);
      if ((int64_t)grant->accepted_return + 1 >= owner_end(artifact, (int64_t)grant->entry) || value == NULL ||
          value->opcode != QA_QVMY_OP_CONST || value->operand == 0 || leave == NULL ||
          leave->opcode != QA_QVMY_OP_LEAVE)
        qa_save_reader_fail(&op, "pickup accepted return must be original nonzero CONST/LEAVE in its owning function");
    } else {
      grant->region_entry = (double)integer_field(&op, "entry", 0);
      grant->region_join = (double)integer_field(&op, "join", 0);
      grant->region_quantity = (double)integer_field(&op, "quantity", 8);
      int64_t frame = qa_qvm_qualify_region(artifact->image.instructions, artifact->image.instruction_count,
                                            (int64_t)grant->entry, (int64_t)grant->region_entry, (int64_t)grant->region_join);
      if ((int64_t)grant->region_quantity % 4 != 0 || (int64_t)grant->region_quantity + 4 > frame)
        qa_save_reader_fail(&op, "pickup quantity is outside the original local frame");
      qa_save_reader weapon = field(&op, "weapon");
      if (!qa_save_reader_is_undefined(&weapon)) {
        grant->has_weapon = true;
        qa_save_reader storage = field(&weapon, "storage");
        if (!qa_save_reader_is_undefined(&storage)) {
          (void)qa_save_reader_literal_string(&storage, "inventory");
          grant->weapon_storage_inventory = true;
        } else {
          grant->weapon_bits_offset = (double)integer_field(&weapon, "bitsOffset", 0);
          grant->weapon_ammo_offset = (double)integer_field(&weapon, "ammoOffset", 0);
        }
        qa_save_reader_destroy(&storage);
        qa_save_reader quantity = field(&weapon, "quantity");
        read_evaluation(artifact, &quantity, (int64_t)grant->entry, &grant->weapon_quantity);
        qa_save_reader_destroy(&quantity);
      }
      qa_save_reader_destroy(&weapon);
    }
    qa_save_reader_destroy(&op);
    qa_save_reader eligibility = field(&at, "eligibility");
    qa_save_reader branches_reader = field(&eligibility, "branches");
    size_t branch_count = qa_save_reader_list_count(&branches_reader);
    qa_qvm_pickup_branches *captured = xmalloc(sizeof *captured);
    captured->branches = xmalloc(branch_count * sizeof *captured->branches);
    captured->branch_count = branch_count;
    grant->eligible_ctx = captured;
    grant->eligible = qa_qvm_primary_pickup_grant_eligible;
    for (size_t b = 0; b < branch_count; b++) {
      qa_save_reader value = item(&branches_reader, b);
      int64_t instruction_index = integer_field(&value, "instructionIndex", out->gate_entry + 1);
      const qa_qvm_instruction *instruction = at_insn(artifact, instruction_index);
      bool seen = false;
      for (size_t j = 0; j < b; j++) if (captured->branches[j].instruction_index == instruction_index) seen = true;
      if (instruction_index >= owner_end(artifact, (int64_t)out->gate_entry) || instruction == NULL ||
          instruction->opcode < QA_QVMY_OP_EQ || instruction->opcode > QA_QVMY_OP_GEF || seen)
        qa_save_reader_fail(&value, "eligibility override must name a distinct original conditional inside the gate");
      captured->branches[b].instruction_index = instruction_index;
      qa_save_reader taken = field(&value, "taken");
      captured->branches[b].taken = qa_save_reader_boolean(&taken);
      qa_save_reader_destroy(&taken);
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&branches_reader);
    qa_save_reader_destroy(&eligibility);
    grant->item_type = (double)integer_field(&at, "itemType", 0);
    qa_save_reader_destroy(&at);
  }
  qa_save_reader_destroy(&grants_reader);

  out->entity_stride = (double)integer_field(reader, "entityStride", qa_qvm_shared_entity_bytes(out->abi_profile));
  out->client_stride = (double)integer_field(reader, "clientStride", qa_qvm_player_state_bytes(out->abi_profile));
  if ((int64_t)out->entity_stride % 4 != 0 || (int64_t)out->client_stride % 4 != 0)
    qa_save_reader_fail(reader, "source record strides must be aligned");
  qa_save_reader fields = field(reader, "fields");
  const char *names[] = {"inuse", "client", "health", "item", "count", "flags"};
  double *slots[] = {&out->field_inuse, &out->field_client, &out->field_health,
                     &out->field_item, &out->field_count, &out->field_flags};
  for (int i = 0; i < 6; i++) {
    qa_save_reader at = field(&fields, names[i]);
    int64_t value = (int64_t)qa_save_reader_integer_min(&at, 0);
    if (value % 4 != 0 || value + 4 > (int64_t)out->entity_stride)
      qa_save_reader_fail(&at, "pickup field is outside its aligned entity record");
    *slots[i] = (double)value;
    qa_save_reader_destroy(&at);
  }
  qa_save_reader_destroy(&fields);
  out->dropped_flag = (double)integer_field(reader, "droppedFlag", 0);
  qa_save_reader items_reader = field(reader, "items");
  out->items = qa_qvm_parse_item_layout(&items_reader);
  qa_save_reader_destroy(&items_reader);
  qa_save_reader touch_reader = field(reader, "touch");
  out->touch = (double)read_entry(artifact, &touch_reader);
  qa_save_reader_destroy(&touch_reader);
  qa_save_reader targets_reader = field(reader, "targets");
  read_function_calls(artifact, &targets_reader, &out->targets_entry, &out->targets_calls, &out->targets_call_count);
  qa_save_reader_destroy(&targets_reader);
  qa_save_reader free_reader = field(reader, "free");
  out->free_entry = (double)read_entry(artifact, &free_reader);
  qa_save_reader_destroy(&free_reader);
  qa_save_reader objectives_reader = field(reader, "objectiveTypes");
  size_t objective_count = qa_save_reader_list_count(&objectives_reader);
  double *objective_types = xmalloc(objective_count * sizeof *objective_types);
  out->objective_types = objective_types;
  out->objective_type_count = objective_count;
  for (size_t i = 0; i < objective_count; i++) {
    qa_save_reader value = item(&objectives_reader, i);
    objective_types[i] = qa_save_reader_integer_min(&value, 0);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&objectives_reader);
}

void qa_qvm_primary_pickup_profile_free(qa_qvm_pickup_profile *profile) {
  free((void *)profile->gate_calls);
  free((void *)profile->targets_calls);
  free((void *)profile->objective_types);
  qa_qvm_pickup_grant *grants = (qa_qvm_pickup_grant *)profile->grants;
  for (size_t i = 0; i < profile->grant_count; i++) {
    free((void *)grants[i].calls);
    free((void *)grants[i].weapon_quantity.inputs);
    if (grants[i].eligible == qa_qvm_primary_pickup_grant_eligible) {
      qa_qvm_pickup_branches *captured = grants[i].eligible_ctx;
      if (captured != NULL) free(captured->branches);
    }
    free(grants[i].eligible_ctx);
  }
  free(grants);
  memset(profile, 0, sizeof *profile);
}
