/* Port of src/guest/x64/plan.ts. GPL-2.0-or-later. */
#include "qa/guest/x64/plan.h"

#include "qa/foundation/support.h"
#include "qa/guest/floating_point/sse.h"

const qa_x64_flow qa_x64_advance = {QA_X64_ADVANCE, 0, 0};
const char *qa_x64_plan_kind_name(qa_x64_plan_kind kind) {
  switch (kind) {
    case QA_X64_PLAN_NOP: return "nop";
    case QA_X64_PLAN_MOVE: return "move";
    case QA_X64_PLAN_EXTEND: return "extend";
    case QA_X64_PLAN_INCREMENT: return "increment";
    case QA_X64_PLAN_SHIFT: return "shift";
    case QA_X64_PLAN_MULTIPLY: return "multiply";
    case QA_X64_PLAN_CONDITIONAL_MOVE: return "conditional-move";
    case QA_X64_PLAN_SET_CONDITION: return "set-condition";
    case QA_X64_PLAN_LEA: return "lea";
    case QA_X64_PLAN_ALU: return "alu";
    case QA_X64_PLAN_BRANCH: return "branch";
    case QA_X64_PLAN_JUMP: return "jump";
    case QA_X64_PLAN_CALL: return "call";
    case QA_X64_PLAN_PUSH: return "push";
    case QA_X64_PLAN_POP: return "pop";
    case QA_X64_PLAN_RETURN: return "return";
    case QA_X64_PLAN_NUMERIC: return "numeric";
    case QA_X64_PLAN_NUMERIC_MEMORY: return "numeric-memory";
    default: return "raw-sse";
  }
}
const char *qa_x64_flow_kind_name(qa_x64_flow_kind kind) {
  switch (kind) {
    case QA_X64_ADVANCE: return "advance";
    case QA_X64_BRANCH: return "branch";
    case QA_X64_HALT: return "halt";
    default: return "trap";
  }
}
/* --- decoder.ts mirrors (unported module) --- */
static uint64_t as_uint_n(int width, uint64_t v) { return width >= 64 ? v : v & ((1ull << width) - 1ull); }
static uint64_t canonical_address(uint64_t value) {
  /* TS compares the signed bigint first; plans carry uint64 bit patterns,
   * which matches for every value the decoder produces. */
  if (value <= 0x7fffffffffffull) return value;
  if (value > 0x7fffffffffffull && value < 0xffff800000000000ull)
    qa_range_error("Noncanonical 48-bit virtual address 0x%llx", (unsigned long long)value);
  return value;
}
static qa_guest_address guest_address(qa_mapped_guest_memory *memory, uint64_t raw) {
  uint64_t value = canonical_address(raw);
  if (value == 0) qa_range_error("null guest address");
  if (memory->pointer_bytes == 8) { qa_guest_address a = {value}; return a; }
  qa_guest_address a;
  if (!qa_memory_pointer(memory, value, &a)) qa_range_error("null guest address");
  return a;
}
static uint64_t effective_operand_offset(const qa_guest_processor_state *state, const qa_x64_operand *operand, uint64_t next_ip) {
  uint64_t base = operand->rip_relative ? next_ip
    : !operand->has_base ? 0
    : qa_integer_register_file_read(&state->registers, operand->base, operand->address_bits, false);
  uint64_t index = !operand->has_index ? 0
    : qa_integer_register_file_read(&state->registers, operand->index, operand->address_bits, false) * operand->scale;
  return as_uint_n(operand->address_bits, base + index + (uint64_t)operand->displacement);
}
static qa_guest_address operand_address(qa_mapped_guest_memory *memory, const qa_guest_processor_state *state,
                                        const qa_x64_operand *operand, uint64_t next_ip) {
  uint64_t segment_base = operand->segment == QA_X64_SEG_NONE ? 0
    : state->segments[operand->segment == QA_X64_SEG_FS ? QA_SEG_FS : QA_SEG_GS].base;
  return guest_address(memory, effective_operand_offset(state, operand, next_ip) + segment_base);
}
static uint64_t read_memory_width(qa_mapped_guest_memory *memory, qa_guest_address address, int width) {
  switch (width) {
    case 8: return qa_memory_read_u8(memory, address);
    case 16: return qa_memory_read_u16(memory, address);
    case 32: return qa_memory_read_u32(memory, address);
    default: return qa_memory_read_u64(memory, address);
  }
}
static void write_memory_width(qa_mapped_guest_memory *memory, qa_guest_address address, int width, uint64_t value) {
  uint64_t normalized = as_uint_n(width, value);
  switch (width) {
    case 8: qa_memory_write_u8(memory, address, (uint8_t)normalized); break;
    case 16: qa_memory_write_u16(memory, address, (uint16_t)normalized); break;
    case 32: qa_memory_write_u32(memory, address, (uint32_t)normalized); break;
    default: qa_memory_write_u64(memory, address, normalized); break;
  }
}
static uint64_t read_operand(qa_mapped_guest_memory *memory, const qa_guest_processor_state *state,
                             const qa_x64_operand *operand, uint64_t next_ip) {
  return operand->kind == QA_X64_OPERAND_REGISTER
    ? qa_integer_register_file_read(&state->registers, operand->reg, operand->width, operand->high_byte)
    : read_memory_width(memory, operand_address(memory, state, operand, next_ip), operand->width);
}
static void write_operand(qa_mapped_guest_memory *memory, qa_guest_processor_state *state,
                          const qa_x64_operand *operand, uint64_t next_ip, uint64_t value) {
  if (operand->kind == QA_X64_OPERAND_REGISTER)
    qa_integer_register_file_write(&state->registers, operand->reg, operand->width, value, operand->high_byte);
  else write_memory_width(memory, operand_address(memory, state, operand, next_ip), operand->width, value);
}
static void writable_operand(qa_mapped_guest_memory *memory, const qa_guest_processor_state *state,
                             const qa_x64_operand *operand, uint64_t next_ip) {
  if (operand->kind == QA_X64_OPERAND_MEMORY)
    qa_memory_check(memory, operand_address(memory, state, operand, next_ip), (size_t)(operand->width / 8), QA_ACCESS_WRITE);
}
static uint64_t source_value(qa_mapped_guest_memory *memory, const qa_guest_processor_state *state,
                             const qa_x64_plan_source *source, uint64_t next_ip) {
  return source->is_immediate ? source->immediate : read_operand(memory, state, &source->operand, next_ip);
}
/* --- plan.ts --- */
void qa_x64_lock(bool lock, const qa_x64_operand *destination, bool has_destination, bool permitted) {
  if (lock && (!permitted || !has_destination || destination->kind != QA_X64_OPERAND_MEMORY))
    qa_range_error("LOCK requires a supported memory read-modify-write operand");
}
void qa_make_x64_plan(qa_x64_semantic_plan *out, const qa_x64_plan_operation *operation, uint64_t next_ip, bool lock) {
  bool ends = operation->kind == QA_X64_PLAN_BRANCH || operation->kind == QA_X64_PLAN_RETURN
    || operation->kind == QA_X64_PLAN_NUMERIC_MEMORY || operation->kind == QA_X64_PLAN_CALL
    || operation->kind == QA_X64_PLAN_JUMP || operation->kind == QA_X64_PLAN_PUSH
    || (operation->kind == QA_X64_PLAN_POP && operation->destination.kind == QA_X64_OPERAND_MEMORY)
    || (operation->kind == QA_X64_PLAN_INCREMENT && operation->destination.kind == QA_X64_OPERAND_MEMORY)
    || (((operation->kind == QA_X64_PLAN_SHIFT || operation->kind == QA_X64_PLAN_SET_CONDITION)
         && operation->destination.kind == QA_X64_OPERAND_MEMORY))
    || (operation->kind == QA_X64_PLAN_RAW_SSE && operation->raw_operation.kind == QA_RAW_SSE_MOVE
        && operation->raw_operation.store && !operation->raw_operand_is_register)
    || (((operation->kind == QA_X64_PLAN_MOVE || operation->kind == QA_X64_PLAN_ALU)
         && operation->destination.kind == QA_X64_OPERAND_MEMORY
         && (operation->kind != QA_X64_PLAN_ALU
             || (operation->alu_operation != QA_ALU_CMP && operation->alu_operation != QA_ALU_TEST))));
  out->operation = *operation;
  out->next_ip = next_ip;
  out->lock = lock;
  out->ends_block = ends;
}
static qa_x64_flow branch_to(uint64_t target) { qa_x64_flow f = {QA_X64_BRANCH, target, 0}; return f; }
static int64_t sign_extend(int width, uint64_t v) {
  v = as_uint_n(width, v);
  if (width < 64 && ((v >> (width - 1)) & 1u) != 0) return (int64_t)(v - (1ull << width));
  return (int64_t)v;
}
qa_x64_flow qa_execute_x64_plan(const qa_x64_semantic_plan *plan, qa_mapped_guest_memory *memory,
                                qa_guest_processor_state *state) {
  const qa_x64_plan_operation *op = &plan->operation;
  switch (op->kind) {
    case QA_X64_PLAN_NOP:
      qa_x64_lock(plan->lock, NULL, false, false);
      return qa_x64_advance;
    case QA_X64_PLAN_SHIFT: {
      int count = op->shift_count_is_cl
        ? (int)qa_integer_register_file_read(&state->registers, QA_REG_RCX, 8, false) : op->shift_count;
      qa_x64_lock(plan->lock, NULL, false, false);
      writable_operand(memory, state, &op->destination, plan->next_ip);
      uint64_t result = qa_x86_shift(op->shift_operation, op->destination.width,
                                     read_operand(memory, state, &op->destination, plan->next_ip), count, &state->flags);
      write_operand(memory, state, &op->destination, plan->next_ip, result);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_MULTIPLY: {
      qa_x64_lock(plan->lock, NULL, false, false);
      uint64_t left = read_operand(memory, state, &op->left, plan->next_ip);
      uint64_t right = source_value(memory, state, &op->right, plan->next_ip);
      write_operand(memory, state, &op->destination, plan->next_ip,
                    qa_x86_signed_multiply(op->destination.width, left, right, &state->flags));
      return qa_x64_advance;
    }
    case QA_X64_PLAN_CONDITIONAL_MOVE: {
      qa_x64_lock(plan->lock, NULL, false, false);
      uint64_t value = read_operand(memory, state, &op->source_operand, plan->next_ip);
      if (qa_x86_condition(op->condition, &state->flags))
        write_operand(memory, state, &op->destination, plan->next_ip, value);
      else if (op->destination.width == 32)
        write_operand(memory, state, &op->destination, plan->next_ip,
                      read_operand(memory, state, &op->destination, plan->next_ip));
      return qa_x64_advance;
    }
    case QA_X64_PLAN_SET_CONDITION:
      qa_x64_lock(plan->lock, NULL, false, false);
      write_operand(memory, state, &op->destination, plan->next_ip, qa_x86_condition(op->condition, &state->flags) ? 1 : 0);
      return qa_x64_advance;
    case QA_X64_PLAN_EXTEND: {
      qa_x64_lock(plan->lock, NULL, false, false);
      uint64_t value = read_operand(memory, state, &op->source_operand, plan->next_ip);
      write_operand(memory, state, &op->destination, plan->next_ip,
                    op->is_signed ? (uint64_t)sign_extend(op->source_operand.width, value) : value);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_INCREMENT: {
      qa_x64_lock(plan->lock, &op->destination, true, true);
      writable_operand(memory, state, &op->destination, plan->next_ip);
      bool carry = qa_processor_flags_get(&state->flags, QA_FLAG_CARRY);
      uint64_t value = qa_x86_alu(op->subtract ? QA_ALU_SUB : QA_ALU_ADD, op->destination.width,
                                  read_operand(memory, state, &op->destination, plan->next_ip), 1, &state->flags);
      write_operand(memory, state, &op->destination, plan->next_ip, value);
      qa_processor_flags_set(&state->flags, QA_FLAG_CARRY, carry);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_NUMERIC: case QA_X64_PLAN_NUMERIC_MEMORY: {
      qa_x64_lock(plan->lock, NULL, false, false);
      qa_numeric_instruction instruction = op->instruction;
      if (op->kind == QA_X64_PLAN_NUMERIC_MEMORY) {
        instruction.has_operand = true;
        instruction.operand.kind = QA_OPERAND_MEMORY;
        instruction.operand.address = operand_address(memory, state, &op->source_operand, plan->next_ip);
        instruction.operand.index = 0;
      }
      qa_numeric_context ctx;
      qa_numeric_context_init(&ctx, state, memory, &instruction);
      qa_numeric_result result;
      qa_execute_numeric_instruction(&result, &ctx);
      if (result.kind == QA_NUMERIC_EXCEPTION) qa_range_error("%s", result.detail);
      if (result.kind == QA_NUMERIC_UNSUPPORTED) qa_range_error("%s", result.detail);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_RAW_SSE: {
      qa_x64_lock(plan->lock, NULL, false, false);
      qa_numeric_operand operand;
      if (op->raw_operand_is_register) operand = op->raw_register_operand;
      else {
        operand.kind = QA_OPERAND_MEMORY;
        operand.address = operand_address(memory, state, &op->source_operand, plan->next_ip);
        operand.index = 0;
      }
      qa_numeric_result result;
      qa_execute_raw_sse(&result, &op->raw_operation, &operand, state, memory);
      if (result.kind == QA_NUMERIC_UNSUPPORTED) qa_range_error("%s", result.detail);
      if (result.kind == QA_NUMERIC_EXCEPTION) qa_range_error("%s", result.detail);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_MOVE: {
      qa_x64_lock(plan->lock, NULL, false, false);
      uint64_t value = source_value(memory, state, &op->source, plan->next_ip);
      write_operand(memory, state, &op->destination, plan->next_ip, value);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_LEA:
      qa_x64_lock(plan->lock, NULL, false, false);
      write_operand(memory, state, &op->destination, plan->next_ip,
                    effective_operand_offset(state, &op->source_operand, plan->next_ip));
      return qa_x64_advance;
    case QA_X64_PLAN_ALU: {
      uint64_t right = source_value(memory, state, &op->source, plan->next_ip);
      bool writes = op->alu_operation != QA_ALU_CMP && op->alu_operation != QA_ALU_TEST;
      qa_x64_lock(plan->lock, &op->destination, true, writes);
      if (writes) writable_operand(memory, state, &op->destination, plan->next_ip);
      uint64_t result = qa_x86_alu(op->alu_operation, op->destination.width,
                                   read_operand(memory, state, &op->destination, plan->next_ip), right, &state->flags);
      if (writes) write_operand(memory, state, &op->destination, plan->next_ip, result);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_BRANCH:
      qa_x64_lock(plan->lock, NULL, false, false);
      return !op->has_condition || qa_x86_condition(op->condition, &state->flags)
        ? branch_to(canonical_address(plan->next_ip + (uint64_t)op->displacement)) : qa_x64_advance;
    case QA_X64_PLAN_JUMP: case QA_X64_PLAN_CALL: {
      qa_x64_lock(plan->lock, NULL, false, false);
      uint64_t target = canonical_address(source_value(memory, state, &op->source, plan->next_ip));
      if (op->kind == QA_X64_PLAN_CALL) {
        uint64_t stack = qa_integer_register_file_read(&state->registers, QA_REG_RSP, 64, false) - 8u;
        qa_memory_write_u64(memory, guest_address(memory, stack), plan->next_ip);
        qa_integer_register_file_write(&state->registers, QA_REG_RSP, 64, stack, false);
      }
      return branch_to(target);
    }
    case QA_X64_PLAN_PUSH: {
      qa_x64_lock(plan->lock, NULL, false, false);
      uint64_t value = source_value(memory, state, &op->source, plan->next_ip);
      uint64_t stack = qa_integer_register_file_read(&state->registers, QA_REG_RSP, 64, false) - (op->width == 64 ? 8u : 2u);
      qa_guest_address address = guest_address(memory, stack);
      if (op->width == 64) qa_memory_write_u64(memory, address, value);
      else qa_memory_write_u16(memory, address, (uint16_t)(value & 0xffffu));
      qa_integer_register_file_write(&state->registers, QA_REG_RSP, 64, stack, false);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_POP: {
      qa_x64_lock(plan->lock, NULL, false, false);
      uint64_t stack = qa_integer_register_file_read(&state->registers, QA_REG_RSP, 64, false);
      qa_guest_address address = guest_address(memory, stack);
      uint64_t value = op->width == 64 ? qa_memory_read_u64(memory, address) : qa_memory_read_u16(memory, address);
      qa_integer_register_file_write(&state->registers, QA_REG_RSP, 64, stack + (op->width == 64 ? 8u : 2u), false);
      write_operand(memory, state, &op->destination, plan->next_ip, value);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_RETURN: {
      qa_x64_lock(plan->lock, NULL, false, false);
      uint64_t stack = qa_integer_register_file_read(&state->registers, QA_REG_RSP, 64, false);
      uint64_t target = qa_memory_read_u64(memory, guest_address(memory, stack));
      qa_integer_register_file_write(&state->registers, QA_REG_RSP, 64, stack + 8u, false);
      uint64_t branch = canonical_address(target);
      uint64_t rsp = qa_integer_register_file_read(&state->registers, QA_REG_RSP, 64, false);
      qa_integer_register_file_write(&state->registers, QA_REG_RSP, 64, rsp + op->discard, false);
      return branch_to(branch);
    }
  }
  return qa_x64_advance;
}
