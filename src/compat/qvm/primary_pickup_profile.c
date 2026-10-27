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

static const qa_qvmy_region_instruction *at_insn(const qa_qvmy_artifact *artifact, int64_t index) {
  if (index < 0 || (uint64_t)index >= artifact->instruction_count) return NULL;
  return &artifact->instructions[index];
}

static int64_t read_entry(const qa_qvmy_artifact *artifact, qa_save_reader *reader) {
  int64_t value = (int64_t)qa_save_reader_integer_min(reader, 0);
  const qa_qvmy_region_instruction *instruction = at_insn(artifact, value);
  if (instruction == NULL || instruction->opcode != QA_QVMY_OP_ENTER)
    qa_save_reader_fail(reader, "pickup requires an original function entry");
  return value;
}

static int64_t owner_end(const qa_qvmy_artifact *artifact, int64_t owner) {
  int64_t end = owner + 1;
  while (end < (int64_t)artifact->instruction_count) {
    const qa_qvmy_region_instruction *instruction = at_insn(artifact, end);
    if (instruction != NULL && instruction->opcode == QA_QVMY_OP_ENTER) break;
    end++;
  }
  return end;
}

static int64_t *read_calls(const qa_qvmy_artifact *artifact, qa_save_reader *reader, int64_t owner, size_t *out_count) {
  size_t count = qa_save_reader_list_count(reader);
  int64_t *calls = xmalloc(count * sizeof *calls);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value = item(reader, i);
    int64_t index = (int64_t)qa_save_reader_integer_min(&value, 1);
    const qa_qvmy_region_instruction *call = at_insn(artifact, index);
    const qa_qvmy_region_instruction *target = at_insn(artifact, index - 1);
    if (call == NULL || call->opcode != QA_QVMY_OP_CALL || target == NULL ||
        target->opcode != QA_QVMY_OP_CONST || target->operand != owner)
      qa_save_reader_fail(&value, "pickup call differs from its declared original target");
    calls[i] = index;
    qa_save_reader_destroy(&value);
  }
  *out_count = count;
  return calls;
}

static void read_function_calls(const qa_qvmy_artifact *artifact, qa_save_reader *reader,
                                int64_t *out_entry, int64_t **out_calls, size_t *out_count) {
  qa_save_reader entry_reader = field(reader, "entry");
  int64_t entry = read_entry(artifact, &entry_reader);
  qa_save_reader_destroy(&entry_reader);
  qa_save_reader calls_reader = field(reader, "calls");
  *out_calls = read_calls(artifact, &calls_reader, entry, out_count);
  qa_save_reader_destroy(&calls_reader);
  *out_entry = entry;
}

static int64_t read_argument(qa_save_reader *reader) {
  int64_t index = (int64_t)qa_save_reader_integer_min(reader, 0);
  if (index >= 62) qa_save_reader_fail(reader, "pickup argument is outside the source ABI");
  return index;
}

static void read_evaluation(const qa_qvmy_artifact *artifact, qa_save_reader *reader, int64_t owner,
                            qa_qvm_region_evaluation *out, int64_t **out_inputs) {
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
  *out_inputs = inputs;
  qa_save_reader result_reader = field(reader, "result");
  if (qa_save_reader_is_null(&result_reader)) {
    out->has_result = false;
    out->result = 0;
  } else {
    out->has_result = true;
    out->result = (int64_t)qa_save_reader_integer_min(&result_reader, 8);
  }
  qa_save_reader_destroy(&result_reader);
  qa_qvm_qualify_region_evaluation(artifact->instructions, artifact->instruction_count, owner, out, "source");
}

/* Mirrored parseQvmItemLayout (TS item-catalog.ts). */
static void parse_item_layout(const qa_save_reader *reader, qa_qvmy_item_layout *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader fields = field(reader, "fields");
  out->stride = integer_field(reader, "stride", 4);
  const char *names[] = {"className", "pickupName", "type", "tag"};
  int64_t *slots[] = {&out->field_class_name, &out->field_pickup_name, &out->field_type, &out->field_tag};
  for (int i = 0; i < 4; i++) {
    qa_save_reader at = field(&fields, names[i]);
    int64_t value = (int64_t)qa_save_reader_integer_min(&at, 0);
    if (value % 4 != 0 || value + 4 > out->stride)
      qa_save_reader_fail(&at, "item field exceeds its record or is unaligned");
    *slots[i] = value;
    qa_save_reader_destroy(&at);
  }
  qa_save_reader_destroy(&fields);
  qa_save_reader pointer = field(reader, "address");
  if (pointer.value != NULL && pointer.value->kind == QA_SAVE_NUMBER) {
    out->address = (int64_t)qa_save_reader_integer_min(&pointer, 4);
  } else {
    out->address_is_global = true;
    out->address = integer_field(&pointer, "global", 0);
  }
  qa_save_reader_destroy(&pointer);
  qa_save_reader count = field(reader, "count");
  if (count.value != NULL && count.value->kind == QA_SAVE_NUMBER) {
    out->count = (int64_t)qa_save_reader_integer_min(&count, 1);
  } else {
    out->count_is_global = true;
    out->count = integer_field(&count, "global", 0);
    out->count_maximum = integer_field(&count, "maximum", 1);
  }
  qa_save_reader_destroy(&count);
  qa_save_reader source = field(reader, "source");
  if (!qa_save_reader_is_undefined(&source)) {
    (void)qa_save_reader_literal_string(&source, "live");
    out->has_source = true;
  }
  qa_save_reader_destroy(&source);
  out->weapon_type = integer_field(reader, "weaponType", 0);
  out->ammo_type = integer_field(reader, "ammoType", 0);
  if (out->address % 4 != 0 || (out->count_is_global && out->count % 4 != 0) ||
      out->stride % 4 != 0 || out->weapon_type == out->ammo_type)
    qa_save_reader_fail(reader, "invalid item table layout");
  if (!out->has_source && (out->address_is_global || out->count_is_global))
    qa_save_reader_fail(reader, "runtime item table locations require live source storage");
}

bool qa_qvmy_item_layout_equal(const qa_qvmy_item_layout *left, const qa_qvmy_item_layout *right) {
  return left->address_is_global == right->address_is_global && left->address == right->address &&
         left->count_is_global == right->count_is_global && left->count == right->count &&
         left->count_maximum == right->count_maximum && left->has_source == right->has_source &&
         left->stride == right->stride && left->field_class_name == right->field_class_name &&
         left->field_pickup_name == right->field_pickup_name && left->field_type == right->field_type &&
         left->field_tag == right->field_tag && left->weapon_type == right->weapon_type &&
         left->ammo_type == right->ammo_type;
}

typedef struct branch_decision {
  bool taken;
} branch_decision;

static bool decide_branch(void *ctx, bool original_taken, void *call_ctx, qa_qvmy_cancel_fn cancel) {
  (void)original_taken;
  (void)call_ctx;
  (void)cancel;
  return ((branch_decision *)ctx)->taken;
}

bool qa_qvmy_pickup_grant_eligible(const qa_qvmy_pickup_grant *grant, qa_qvmy_function_call *call) {
  qa_qvmy_branch_binding *bindings = xmalloc(grant->branch_count * sizeof *bindings);
  branch_decision *decisions = xmalloc(grant->branch_count * sizeof *decisions);
  for (size_t i = 0; i < grant->branch_count; i++) {
    decisions[i].taken = grant->branches[i].taken;
    bindings[i].instruction_index = grant->branches[i].instruction_index;
    bindings[i].ctx = &decisions[i];
    bindings[i].decide = decide_branch;
  }
  call->branches(call->call_ctx, bindings, grant->branch_count);
  int32_t result = strcmp(call->execution, "asynchronous") == 0 ? call->proceed_async(call->call_ctx)
                                                                : call->proceed(call->call_ctx);
  free(bindings);
  free(decisions);
  return result != 0;
}

void qa_qvm_read_primary_pickup_profile(const qa_save_reader *reader, const qa_qvmy_artifact *artifact,
                                        qa_qvmy_pickup_profile *out) {
  memset(out, 0, sizeof *out);
  out->module = artifact->module;
  out->abi_profile = artifact->has_abi_profile ? artifact->abi_profile : "q3-modern";
  if (strcmp(artifact->role, "qagame") != 0)
    qa_save_reader_fail(reader, "primary pickup declarations require a qagame ABI");
  qa_save_reader gate_reader = field(reader, "gate");
  read_function_calls(artifact, &gate_reader, &out->gate_entry, &out->gate_calls, &out->gate_call_count);
  qa_save_reader item_argument = field(&gate_reader, "itemArgument");
  out->gate_item_argument = read_argument(&item_argument);
  qa_save_reader_destroy(&item_argument);
  qa_save_reader player_argument = field(&gate_reader, "playerArgument");
  out->gate_player_argument = read_argument(&player_argument);
  qa_save_reader_destroy(&player_argument);
  qa_save_reader_destroy(&gate_reader);

  qa_save_reader grants_reader = field(reader, "grants");
  size_t grant_count = qa_save_reader_list_count(&grants_reader);
  out->grants = xmalloc(grant_count * sizeof *out->grants);
  out->grant_count = grant_count;
  for (size_t g = 0; g < grant_count; g++) {
    qa_save_reader at = item(&grants_reader, g);
    qa_qvmy_pickup_grant *grant = &out->grants[g];
    memset(grant, 0, sizeof *grant);
    read_function_calls(artifact, &at, &grant->entry, &grant->calls, &grant->call_count);
    qa_save_reader op = field(&at, "operation");
    qa_save_reader op_kind = field(&op, "kind");
    static const char *const op_kinds[] = {"return", "region"};
    const char *kind = qa_save_reader_choice_string(&op_kind, op_kinds, 2);
    qa_save_reader_destroy(&op_kind);
    grant->operation_kind = kind;
    if (strcmp(kind, "return") == 0) {
      grant->accepted_return = integer_field(&op, "acceptedReturn", (double)(grant->entry + 1));
      const qa_qvmy_region_instruction *value = at_insn(artifact, grant->accepted_return);
      const qa_qvmy_region_instruction *leave = at_insn(artifact, grant->accepted_return + 1);
      if (grant->accepted_return + 1 >= owner_end(artifact, grant->entry) || value == NULL ||
          value->opcode != QA_QVMY_OP_CONST || value->operand == 0 || leave == NULL ||
          leave->opcode != QA_QVMY_OP_LEAVE)
        qa_save_reader_fail(&op, "pickup accepted return must be original nonzero CONST/LEAVE in its owning function");
    } else {
      grant->region_entry = integer_field(&op, "entry", 0);
      grant->region_join = integer_field(&op, "join", 0);
      grant->region_quantity = integer_field(&op, "quantity", 8);
      int64_t frame = qa_qvm_qualify_region(artifact->instructions, artifact->instruction_count,
                                            grant->entry, grant->region_entry, grant->region_join);
      if (grant->region_quantity % 4 != 0 || grant->region_quantity + 4 > frame)
        qa_save_reader_fail(&op, "pickup quantity is outside the original local frame");
      qa_save_reader weapon = field(&op, "weapon");
      if (!qa_save_reader_is_undefined(&weapon)) {
        grant->has_weapon = true;
        qa_save_reader storage = field(&weapon, "storage");
        if (!qa_save_reader_is_undefined(&storage)) {
          (void)qa_save_reader_literal_string(&storage, "inventory");
          grant->weapon_storage_inventory = true;
        } else {
          grant->weapon_bits_offset = integer_field(&weapon, "bitsOffset", 0);
          grant->weapon_ammo_offset = integer_field(&weapon, "ammoOffset", 0);
        }
        qa_save_reader_destroy(&storage);
        qa_save_reader quantity = field(&weapon, "quantity");
        read_evaluation(artifact, &quantity, grant->entry, &grant->weapon_quantity, &grant->weapon_quantity_inputs);
        qa_save_reader_destroy(&quantity);
      }
      qa_save_reader_destroy(&weapon);
    }
    qa_save_reader_destroy(&op);
    qa_save_reader eligibility = field(&at, "eligibility");
    qa_save_reader branches_reader = field(&eligibility, "branches");
    size_t branch_count = qa_save_reader_list_count(&branches_reader);
    grant->branches = xmalloc(branch_count * sizeof *grant->branches);
    grant->branch_count = branch_count;
    for (size_t b = 0; b < branch_count; b++) {
      qa_save_reader value = item(&branches_reader, b);
      int64_t instruction_index = integer_field(&value, "instructionIndex", (double)(out->gate_entry + 1));
      const qa_qvmy_region_instruction *instruction = at_insn(artifact, instruction_index);
      bool seen = false;
      for (size_t j = 0; j < b; j++) if (grant->branches[j].instruction_index == instruction_index) seen = true;
      if (instruction_index >= owner_end(artifact, out->gate_entry) || instruction == NULL ||
          instruction->opcode < QA_QVMY_OP_EQ || instruction->opcode > QA_QVMY_OP_GEF || seen)
        qa_save_reader_fail(&value, "eligibility override must name a distinct original conditional inside the gate");
      grant->branches[b].instruction_index = instruction_index;
      qa_save_reader taken = field(&value, "taken");
      grant->branches[b].taken = qa_save_reader_boolean(&taken);
      qa_save_reader_destroy(&taken);
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&branches_reader);
    qa_save_reader_destroy(&eligibility);
    grant->item_type = integer_field(&at, "itemType", 0);
    qa_save_reader_destroy(&at);
  }
  qa_save_reader_destroy(&grants_reader);

  out->entity_stride = integer_field(reader, "entityStride", qa_qvm_shared_entity_bytes(out->abi_profile));
  out->client_stride = integer_field(reader, "clientStride", qa_qvm_player_state_bytes(out->abi_profile));
  if (out->entity_stride % 4 != 0 || out->client_stride % 4 != 0)
    qa_save_reader_fail(reader, "source record strides must be aligned");
  qa_save_reader fields = field(reader, "fields");
  const char *names[] = {"inuse", "client", "health", "item", "count", "flags"};
  int64_t *slots[] = {&out->field_inuse, &out->field_client, &out->field_health,
                      &out->field_item, &out->field_count, &out->field_flags};
  for (int i = 0; i < 6; i++) {
    qa_save_reader at = field(&fields, names[i]);
    int64_t value = (int64_t)qa_save_reader_integer_min(&at, 0);
    if (value % 4 != 0 || value + 4 > out->entity_stride)
      qa_save_reader_fail(&at, "pickup field is outside its aligned entity record");
    *slots[i] = value;
    qa_save_reader_destroy(&at);
  }
  qa_save_reader_destroy(&fields);
  out->dropped_flag = integer_field(reader, "droppedFlag", 0);
  qa_save_reader items_reader = field(reader, "items");
  parse_item_layout(&items_reader, &out->items);
  qa_save_reader_destroy(&items_reader);
  qa_save_reader touch_reader = field(reader, "touch");
  out->touch = read_entry(artifact, &touch_reader);
  qa_save_reader_destroy(&touch_reader);
  qa_save_reader targets_reader = field(reader, "targets");
  read_function_calls(artifact, &targets_reader, &out->targets_entry, &out->targets_calls, &out->targets_call_count);
  qa_save_reader_destroy(&targets_reader);
  qa_save_reader free_reader = field(reader, "free");
  out->free_entry = read_entry(artifact, &free_reader);
  qa_save_reader_destroy(&free_reader);
  qa_save_reader objectives_reader = field(reader, "objectiveTypes");
  size_t objective_count = qa_save_reader_list_count(&objectives_reader);
  out->objective_types = xmalloc(objective_count * sizeof *out->objective_types);
  out->objective_type_count = objective_count;
  for (size_t i = 0; i < objective_count; i++) {
    qa_save_reader value = item(&objectives_reader, i);
    out->objective_types[i] = (int64_t)qa_save_reader_integer_min(&value, 0);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&objectives_reader);
}

void qa_qvmy_pickup_profile_free(qa_qvmy_pickup_profile *profile) {
  free(profile->gate_calls);
  free(profile->targets_calls);
  free(profile->objective_types);
  for (size_t i = 0; i < profile->grant_count; i++) {
    free(profile->grants[i].calls);
    free(profile->grants[i].weapon_quantity_inputs);
    free(profile->grants[i].branches);
  }
  free(profile->grants);
  memset(profile, 0, sizeof *profile);
}
