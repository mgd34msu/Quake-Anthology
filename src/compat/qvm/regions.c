/* See include/qa/compat/qvm/regions.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/regions.h"

#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/operations.h"
#include "qa/foundation/support.h"

static void *xrealloc(void *ptr, size_t size) {
  void *result = realloc(ptr, size == 0 ? 1 : size);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  return result;
}

static const qa_qvm_instruction *at(const qa_qvm_instruction *instructions,
                                            size_t count, int64_t pc) {
  if (pc < 0 || (uint64_t)pc >= count) return NULL;
  return &instructions[pc];
}

int64_t qa_qvm_qualify_region(const qa_qvm_instruction *instructions, size_t count,
                              int64_t owner, int64_t entry, int64_t join) {
  const qa_qvm_instruction *first = at(instructions, count, owner);
  if (first == NULL || first->opcode != QA_QVMY_OP_ENTER)
    qa_range_error("QVM region owner is not a function");
  int64_t end = owner + 1;
  while (end < (int64_t)count) {
    const qa_qvm_instruction *ins = at(instructions, count, end);
    if (ins != NULL && ins->opcode == QA_QVMY_OP_ENTER) break;
    end++;
  }
  if (entry <= owner || join <= entry || join >= end)
    qa_range_error("QVM region is outside its owning function");

  /* depth map + pending stack as parallel dynamic arrays. */
  typedef struct { int64_t pc; int64_t depth; } item;
  item *depths = NULL;
  size_t depth_count = 0;
  item *pending = NULL;
  size_t pending_count = 0;
  pending = xrealloc(pending, sizeof *pending);
  pending[pending_count++] = (item){entry, 0};
  bool joined = false;
  while (pending_count != 0) {
    item next = pending[--pending_count];
    bool known = false;
    int64_t known_depth = 0;
    for (size_t i = 0; i < depth_count; i++) {
      if (depths[i].pc == next.pc) {
        known = true;
        known_depth = depths[i].depth;
        break;
      }
    }
    if (known) {
      if (known_depth != next.depth) qa_range_error("QVM region paths disagree on operand depth");
      continue;
    }
    depths = xrealloc(depths, (depth_count + 1) * sizeof *depths);
    depths[depth_count++] = next;
    if (next.pc == join) {
      if (next.depth != 0) qa_range_error("QVM region join retains live operands");
      joined = true;
      continue;
    }
    const qa_qvm_instruction *instruction = at(instructions, count, next.pc);
    if (instruction == NULL) qa_range_error("QVM region has no original instruction");
    int32_t opcode = instruction->opcode;
    int64_t required = 0, change = 0;
    if (opcode == QA_QVMY_OP_CONST || opcode == QA_QVMY_OP_LOCAL || opcode == QA_QVMY_OP_PUSH) change = 1;
    else if (opcode == QA_QVMY_OP_POP || opcode == QA_QVMY_OP_ARG || opcode == QA_QVMY_OP_JUMP) { required = 1; change = -1; }
    else if (opcode == QA_QVMY_OP_STORE1 || opcode == QA_QVMY_OP_STORE2 || opcode == QA_QVMY_OP_STORE4 || opcode == QA_QVMY_OP_BLOCK_COPY) { required = 2; change = -2; }
    else if (opcode >= QA_QVMY_OP_EQ && opcode <= QA_QVMY_OP_GEF) { required = 2; change = -2; }
    else if ((opcode >= QA_QVMY_OP_ADD && opcode <= QA_QVMY_OP_RSHU) ||
             (opcode >= QA_QVMY_OP_ADDF && opcode <= QA_QVMY_OP_MULF)) {
      required = opcode == QA_QVMY_OP_BCOM ? 1 : 2;
      change = opcode == QA_QVMY_OP_BCOM ? 0 : -1;
    } else if (opcode == QA_QVMY_OP_CALL || (opcode >= QA_QVMY_OP_LOAD1 && opcode <= QA_QVMY_OP_LOAD4) ||
               (opcode >= QA_QVMY_OP_SEX8 && opcode <= QA_QVMY_OP_NEGI) || opcode == QA_QVMY_OP_NEGF ||
               opcode == QA_QVMY_OP_CVIF || opcode == QA_QVMY_OP_CVFI) required = 1;
    else if (opcode != QA_QVMY_OP_IGNORE && opcode != QA_QVMY_OP_BREAK)
      qa_range_error("QVM region contains a frame or unsupported instruction");
    if (next.depth < required) qa_range_error("QVM region requires operands from outside its boundary");
    int64_t result = next.depth + change;
    if (opcode == QA_QVMY_OP_JUMP) {
      const qa_qvm_instruction *target = at(instructions, count, next.pc - 1);
      if (target == NULL || target->opcode != QA_QVMY_OP_CONST)
        qa_range_error("QVM region has an indirect jump");
      if (target->operand <= next.pc || target->operand > join)
        qa_range_error("QVM region has an escaping or backward edge");
      pending = xrealloc(pending, (pending_count + 1) * sizeof *pending);
      pending[pending_count++] = (item){target->operand, result};
    } else {
      if (next.pc + 1 <= next.pc || next.pc + 1 > join)
        qa_range_error("QVM region has an escaping or backward edge");
      pending = xrealloc(pending, (pending_count + 1) * sizeof *pending);
      pending[pending_count++] = (item){next.pc + 1, result};
      if (opcode >= QA_QVMY_OP_EQ && opcode <= QA_QVMY_OP_GEF && instruction->operand_width == 4) {
        if (instruction->operand <= next.pc || instruction->operand > join)
          qa_range_error("QVM region has an escaping or backward edge");
        pending = xrealloc(pending, (pending_count + 1) * sizeof *pending);
        pending[pending_count++] = (item){instruction->operand, result};
      }
    }
  }
  free(depths);
  free(pending);
  if (!joined) qa_range_error("QVM region does not reach its original join");
  for (int64_t pc = owner + 1; pc < end; pc++) {
    if (pc >= entry && pc < join) continue;
    const qa_qvm_instruction *instruction = at(instructions, count, pc);
    bool has_destination = false;
    int64_t destination = 0;
    if (instruction != NULL && instruction->opcode >= QA_QVMY_OP_EQ && instruction->opcode <= QA_QVMY_OP_GEF &&
        instruction->operand_width == 4) {
      has_destination = true;
      destination = instruction->operand;
    } else if (instruction != NULL && instruction->opcode == QA_QVMY_OP_JUMP) {
      const qa_qvm_instruction *target = at(instructions, count, pc - 1);
      if (target != NULL && target->opcode == QA_QVMY_OP_CONST) {
        has_destination = true;
        destination = target->operand;
      }
    }
    if (has_destination && destination > entry && destination < join)
      qa_range_error("QVM region has an incoming interior edge");
  }
  return first->operand;
}

/* Operand proof kinds, mirroring the TS Operand union. */
typedef enum { OPERAND_LOCAL, OPERAND_LOCAL_DERIVED, OPERAND_CONSTANT, OPERAND_UNKNOWN } operand_kind;
typedef struct { operand_kind kind; int64_t value; } operand; /* value: local offset or constant */

typedef struct {
  int64_t pc;
  operand *stack;
  size_t stack_count;
  int64_t *initialized;
  size_t initialized_count;
} path;

static bool set_has(const int64_t *set, size_t count, int64_t value) {
  for (size_t i = 0; i < count; i++) if (set[i] == value) return true;
  return false;
}

static void set_add(int64_t **set, size_t *count, int64_t value) {
  if (set_has(*set, *count, value)) return;
  *set = xrealloc(*set, (*count + 1) * sizeof **set);
  (*set)[(*count)++] = value;
}

static bool is_local(operand value) {
  return value.kind == OPERAND_LOCAL || value.kind == OPERAND_LOCAL_DERIVED;
}

typedef struct {
  path *paths;
  size_t count;
} pending_paths;

static void path_add(pending_paths *pending, int64_t pc, operand *stack, size_t stack_count,
                     int64_t *initialized, size_t initialized_count) {
  for (size_t i = 0; i < pending->count; i++) {
    if (pending->paths[i].pc != pc) continue;
    path *previous = &pending->paths[i];
    /* Merge exactly like the TS add(): per-index stack merge and
     * initialized intersection. Stacks at one pc share a depth by the
     * region qualification above. */
    for (size_t s = 0; s < stack_count; s++) {
      operand value = stack[s];
      operand before = s < previous->stack_count ? previous->stack[s] : (operand){OPERAND_UNKNOWN, 0};
      bool same = (value.kind == OPERAND_LOCAL && before.kind == OPERAND_LOCAL && before.value == value.value) ||
                  (value.kind == OPERAND_CONSTANT && before.kind == OPERAND_CONSTANT && before.value == value.value);
      stack[s] = same ? value
                      : (is_local(value) || is_local(before)) ? (operand){OPERAND_LOCAL_DERIVED, 0}
                                                              : (operand){OPERAND_UNKNOWN, 0};
    }
    size_t kept = 0;
    for (size_t s = 0; s < initialized_count; s++)
      if (set_has(previous->initialized, previous->initialized_count, initialized[s]))
        initialized[kept++] = initialized[s];
    free(previous->stack);
    free(previous->initialized);
    previous->stack = stack;
    previous->stack_count = stack_count;
    previous->initialized = initialized;
    previous->initialized_count = kept;
    return;
  }
  pending->paths = xrealloc(pending->paths, (pending->count + 1) * sizeof *pending->paths);
  pending->paths[pending->count++] = (path){pc, stack, stack_count, initialized, initialized_count};
}

static operand *copy_stack(const operand *stack, size_t count) {
  operand *result = xrealloc(NULL, (count == 0 ? 1 : count) * sizeof *result);
  memcpy(result, stack, count * sizeof *result);
  return result;
}

static int64_t *copy_set(const int64_t *set, size_t count) {
  int64_t *result = xrealloc(NULL, (count == 0 ? 1 : count) * sizeof *result);
  memcpy(result, set, count * sizeof *result);
  return result;
}

int64_t qa_qvm_qualify_region_evaluation(const qa_qvm_instruction *instructions, size_t count,
                                         int64_t owner, const qa_qvm_region_evaluation *region,
                                         const char *access) {
  if (access == NULL) access = "source";
  bool read_only = strcmp(access, "read-only") == 0;
  int64_t frame = qa_qvm_qualify_region(instructions, count, owner, region->entry, region->join);
  for (size_t i = 0; i < region->input_count; i++) {
    int64_t offset = region->inputs[i];
    if (offset < 8 || offset % 4 != 0 || offset + 4 > frame)
      qa_range_error("QVM region live-in or result is outside its source frame");
    for (size_t j = i + 1; j < region->input_count; j++)
      if (region->inputs[j] == offset)
        qa_range_error("QVM region live-in or result is outside its source frame");
  }
  if (region->has_result &&
      (region->result < 8 || region->result % 4 != 0 || region->result + 4 > frame))
    qa_range_error("QVM region live-in or result is outside its source frame");

  pending_paths pending = {NULL, 0};
  int64_t *initial = copy_set(region->inputs, region->input_count);
  path_add(&pending, region->entry, copy_stack(NULL, 0), 0, initial, region->input_count);
  while (pending.count != 0) {
    size_t best = 0;
    for (size_t i = 1; i < pending.count; i++)
      if (pending.paths[i].pc < pending.paths[best].pc) best = i;
    path current = pending.paths[best];
    pending.paths[best] = pending.paths[--pending.count];
    int64_t pc = current.pc;
    if (pc == region->join) {
      if (region->has_result && !set_has(current.initialized, current.initialized_count, region->result)) {
        qa_range_error("QVM region result is not initialized on every source path");
      }
      free(current.stack);
      free(current.initialized);
      continue;
    }
    const qa_qvm_instruction *instruction = at(instructions, count, pc);
    if (instruction == NULL) qa_range_error("Missing QVM region instruction");
    operand *stack = current.stack;
    size_t stack_count = current.stack_count;
    int64_t *initialized = current.initialized;
    size_t initialized_count = current.initialized_count;
    int32_t opcode = instruction->opcode;
    if (read_only && (opcode == QA_QVMY_OP_CALL || opcode == QA_QVMY_OP_ARG ||
                      opcode == QA_QVMY_OP_BLOCK_COPY || opcode == QA_QVMY_OP_BREAK))
      qa_range_error("Read-only QVM region cannot call, publish arguments, copy memory or break");
    bool jumped = false;
    if (opcode == QA_QVMY_OP_LOCAL) {
      if (instruction->operand < 8 || instruction->operand % 4 != 0 || instruction->operand + 4 > frame + 48)
        qa_range_error("QVM region local address exceeds its source frame and arguments");
      stack = xrealloc(stack, (stack_count + 1) * sizeof *stack);
      stack[stack_count++] = (operand){OPERAND_LOCAL, instruction->operand};
    } else if (opcode == QA_QVMY_OP_CONST) {
      stack = xrealloc(stack, (stack_count + 1) * sizeof *stack);
      stack[stack_count++] = (operand){OPERAND_CONSTANT, instruction->operand};
    } else if (opcode == QA_QVMY_OP_PUSH) {
      stack = xrealloc(stack, (stack_count + 1) * sizeof *stack);
      stack[stack_count++] = (operand){OPERAND_UNKNOWN, 0};
    } else if (opcode == QA_QVMY_OP_POP || opcode == QA_QVMY_OP_ARG) {
      if (stack_count == 0) qa_range_error("Invalid QVM region operand proof");
      stack_count--;
      if (opcode == QA_QVMY_OP_ARG) set_add(&initialized, &initialized_count, instruction->operand);
    } else if (opcode >= QA_QVMY_OP_LOAD1 && opcode <= QA_QVMY_OP_LOAD4) {
      if (stack_count == 0) qa_range_error("Invalid QVM region operand proof");
      operand address = stack[--stack_count];
      if (address.kind == OPERAND_LOCAL_DERIVED)
        qa_range_error("QVM region reads an unresolved source local address");
      if (address.kind == OPERAND_LOCAL && address.value < frame &&
          !set_has(initialized, initialized_count, address.value))
        qa_range_error("QVM region reads undeclared source local %lld", (long long)address.value);
      stack = xrealloc(stack, (stack_count + 1) * sizeof *stack);
      stack[stack_count++] = (operand){OPERAND_UNKNOWN, 0};
    } else if (opcode >= QA_QVMY_OP_STORE1 && opcode <= QA_QVMY_OP_STORE4) {
      if (stack_count == 0) qa_range_error("Invalid QVM region operand proof");
      operand value = stack[--stack_count];
      if (is_local(value)) qa_range_error("QVM region stores an escaping source local pointer");
      if (stack_count == 0) qa_range_error("Invalid QVM region operand proof");
      operand address = stack[--stack_count];
      if (read_only && (address.kind != OPERAND_LOCAL || address.value < 8 || address.value + 4 > frame))
        qa_range_error("Read-only QVM region cannot write outside its own local frame");
      if (address.kind == OPERAND_LOCAL && opcode == QA_QVMY_OP_STORE4)
        set_add(&initialized, &initialized_count, address.value);
    } else if (opcode == QA_QVMY_OP_BLOCK_COPY) {
      if (stack_count == 0) qa_range_error("Invalid QVM region operand proof");
      operand source = stack[--stack_count];
      if (source.kind == OPERAND_LOCAL_DERIVED)
        qa_range_error("QVM region copies an unresolved source local address");
      if (source.kind == OPERAND_LOCAL) {
        for (int64_t offset = 0; offset < instruction->operand; offset += 4)
          if (!set_has(initialized, initialized_count, source.value + offset))
            qa_range_error("QVM region copies an undeclared source local");
      }
      if (stack_count == 0) qa_range_error("Invalid QVM region operand proof");
      operand address = stack[--stack_count];
      if (address.kind == OPERAND_LOCAL) {
        for (int64_t offset = 0; offset + 4 <= instruction->operand; offset += 4)
          set_add(&initialized, &initialized_count, address.value + offset);
      }
    } else if (opcode == QA_QVMY_OP_JUMP) {
      if (stack_count == 0) qa_range_error("Invalid QVM region operand proof");
      operand destination = stack[--stack_count];
      if (destination.kind != OPERAND_CONSTANT) qa_range_error("QVM region jump lost its source target");
      path_add(&pending, destination.value, copy_stack(stack, stack_count), stack_count,
               copy_set(initialized, initialized_count), initialized_count);
      jumped = true;
    } else if (opcode >= QA_QVMY_OP_EQ && opcode <= QA_QVMY_OP_GEF && instruction->operand_width == 4) {
      if (stack_count < 2) qa_range_error("Invalid QVM region operand proof");
      stack_count -= 2;
      path_add(&pending, instruction->operand, copy_stack(stack, stack_count), stack_count,
               copy_set(initialized, initialized_count), initialized_count);
    } else if (opcode == QA_QVMY_OP_CALL) {
      if (stack_count == 0) qa_range_error("Invalid QVM region operand proof");
      stack[--stack_count] = (operand){OPERAND_UNKNOWN, 0};
      stack_count++;
    } else if (((opcode >= QA_QVMY_OP_ADD && opcode <= QA_QVMY_OP_RSHU && opcode != QA_QVMY_OP_BCOM) ||
                (opcode >= QA_QVMY_OP_ADDF && opcode <= QA_QVMY_OP_MULF))) {
      if (stack_count < 2) qa_range_error("Invalid QVM region operand proof");
      operand right = stack[--stack_count];
      operand left = stack[--stack_count];
      operand merged;
      if ((opcode == QA_QVMY_OP_ADD || opcode == QA_QVMY_OP_SUB) && left.kind == OPERAND_LOCAL &&
          right.kind == OPERAND_CONSTANT)
        merged = (operand){OPERAND_LOCAL, left.value + (opcode == QA_QVMY_OP_ADD ? right.value : -right.value)};
      else if (is_local(left) || is_local(right))
        merged = (operand){OPERAND_LOCAL_DERIVED, 0};
      else
        merged = (operand){OPERAND_UNKNOWN, 0};
      stack = xrealloc(stack, (stack_count + 1) * sizeof *stack);
      stack[stack_count++] = merged;
    } else if (opcode != QA_QVMY_OP_IGNORE && opcode != QA_QVMY_OP_BREAK) {
      if (stack_count == 0) qa_range_error("Invalid QVM region operand proof");
      operand value = stack[--stack_count];
      stack = xrealloc(stack, (stack_count + 1) * sizeof *stack);
      stack[stack_count++] = is_local(value) ? (operand){OPERAND_LOCAL_DERIVED, 0}
                                             : (operand){OPERAND_UNKNOWN, 0};
    }
    if (!jumped) path_add(&pending, pc + 1, stack, stack_count, initialized, initialized_count);
    else {
      free(stack);
      free(initialized);
    }
  }
  free(pending.paths);
  return frame;
}
