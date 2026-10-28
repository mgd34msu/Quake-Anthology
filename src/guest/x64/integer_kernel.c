/* Port of src/guest/x64/integer-kernel.ts. GPL-2.0-or-later. */
#include "qa/guest/x64/integer_kernel.h"

#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/x64/decoder.h"

const char *qa_x64_kernel_op_kind_name(qa_x64_kernel_op_kind kind) {
  switch (kind) {
    case QA_KERNEL_OP_NOP: return "nop";
    case QA_KERNEL_OP_MOVE: return "move";
    case QA_KERNEL_OP_REGISTER_MOVE: return "register-move";
    case QA_KERNEL_OP_REGISTER_ALU: return "register-alu";
    case QA_KERNEL_OP_EXTEND: return "extend";
    case QA_KERNEL_OP_INCREMENT: return "increment";
    case QA_KERNEL_OP_CONDITIONAL_MOVE: return "conditional-move";
    case QA_KERNEL_OP_SET_CONDITION: return "set-condition";
    case QA_KERNEL_OP_LEA: return "lea";
    case QA_KERNEL_OP_ALU: return "alu";
    case QA_KERNEL_OP_BRANCH: return "branch";
    case QA_KERNEL_OP_JUMP: return "jump";
    case QA_KERNEL_OP_CALL: return "call";
    case QA_KERNEL_OP_PUSH: return "push";
    case QA_KERNEL_OP_POP: return "pop";
    case QA_KERNEL_OP_RETURN: return "return";
    default: return "passthrough";
  }
}

static int reg_offset(qa_guest_register reg) { return (int)reg * 8; }

static qa_x64_kernel_operand kernel_operand(const qa_x64_operand *value) {
  qa_x64_kernel_operand operand;
  memset(&operand, 0, sizeof operand);
  operand.width = value->width;
  if (value->kind == QA_X64_OPERAND_REGISTER) {
    operand.is_memory = false;
    operand.offset = reg_offset(value->reg);
    operand.high_byte = value->high_byte;
  } else {
    operand.is_memory = true;
    operand.source = *value;
  }
  return operand;
}
static qa_x64_kernel_source kernel_source(const qa_x64_plan_source *value) {
  qa_x64_kernel_source source;
  memset(&source, 0, sizeof source);
  if (value->is_immediate) {
    source.is_immediate = true;
    source.low = (uint32_t)(value->immediate & 0xffffffffull);
    source.high = (uint32_t)(value->immediate >> 32);
  } else {
    source.operand = kernel_operand(&value->operand);
  }
  return source;
}
static qa_x64_kernel_target constant_target(uint64_t value) {
  qa_x64_kernel_target target;
  memset(&target, 0, sizeof target);
  target.is_constant = true;
  target.value = value;
  target.canonical = value <= 0x7fffffffffffull || value >= 0xffff800000000000ull;
  return target;
}
static bool register_operation(const qa_x64_kernel_operand *destination, const qa_x64_kernel_source *source,
                               qa_x64_kernel_word_destination *out_dest, qa_x64_kernel_word_source *out_source) {
  if (destination->is_memory || (destination->width != 32 && destination->width != 64)) return false;
  if (!source->is_immediate && (source->operand.is_memory || source->operand.width != destination->width)) return false;
  out_dest->index = destination->offset >> 2;
  out_dest->width = destination->width;
  if (source->is_immediate) {
    out_source->has_index = false;
    out_source->index = 0;
    out_source->low = source->low;
    out_source->high = source->high;
  } else {
    out_source->has_index = true;
    out_source->index = source->operand.offset >> 2;
    out_source->low = 0;
    out_source->high = 0;
  }
  return true;
}

bool qa_prepare_x64_integer_plan(qa_x64_integer_plan *out, const qa_x64_semantic_plan *plan) {
  memset(out, 0, sizeof *out);
  out->original = plan;
  const qa_x64_plan_operation *op = &plan->operation;
  qa_x64_integer_operation *result = &out->operation;
  switch (op->kind) {
    case QA_X64_PLAN_NOP:
      result->kind = QA_KERNEL_OP_NOP;
      return true;
    case QA_X64_PLAN_RAW_SSE: case QA_X64_PLAN_NUMERIC: case QA_X64_PLAN_NUMERIC_MEMORY:
    case QA_X64_PLAN_SHIFT: case QA_X64_PLAN_MULTIPLY:
      result->kind = QA_KERNEL_OP_PASSTHROUGH;
      return true;
    case QA_X64_PLAN_CONDITIONAL_MOVE: {
      qa_x64_kernel_operand destination = kernel_operand(&op->destination);
      if (destination.is_memory) return false;
      result->kind = QA_KERNEL_OP_CONDITIONAL_MOVE;
      result->destination = destination;
      result->source_operand = kernel_operand(&op->source_operand);
      result->condition = op->condition;
      result->has_condition = true;
      return true;
    }
    case QA_X64_PLAN_SET_CONDITION:
      result->kind = QA_KERNEL_OP_SET_CONDITION;
      result->destination = kernel_operand(&op->destination);
      result->condition = op->condition;
      result->has_condition = true;
      return true;
    case QA_X64_PLAN_EXTEND: {
      qa_x64_kernel_operand destination = kernel_operand(&op->destination);
      if (destination.is_memory) return false;
      result->kind = QA_KERNEL_OP_EXTEND;
      result->destination = destination;
      result->source_operand = kernel_operand(&op->source_operand);
      result->is_signed = op->is_signed;
      return true;
    }
    case QA_X64_PLAN_INCREMENT:
      result->kind = QA_KERNEL_OP_INCREMENT;
      result->destination = kernel_operand(&op->destination);
      result->subtract = op->subtract;
      return true;
    case QA_X64_PLAN_MOVE: {
      qa_x64_kernel_operand destination = kernel_operand(&op->destination);
      qa_x64_kernel_source input = kernel_source(&op->source);
      if (register_operation(&destination, &input, &result->word_destination, &result->word_source)) {
        result->kind = QA_KERNEL_OP_REGISTER_MOVE;
      } else {
        result->kind = QA_KERNEL_OP_MOVE;
        result->destination = destination;
        result->source = input;
      }
      return true;
    }
    case QA_X64_PLAN_ALU: {
      qa_x64_kernel_operand destination = kernel_operand(&op->destination);
      qa_x64_kernel_source input = kernel_source(&op->source);
      result->alu_operation = op->alu_operation;
      if (register_operation(&destination, &input, &result->word_destination, &result->word_source)) {
        result->kind = QA_KERNEL_OP_REGISTER_ALU;
      } else {
        result->kind = QA_KERNEL_OP_ALU;
        result->destination = destination;
        result->source = input;
      }
      return true;
    }
    case QA_X64_PLAN_BRANCH:
      result->kind = QA_KERNEL_OP_BRANCH;
      result->has_condition = op->has_condition;
      result->condition = op->condition;
      result->target = constant_target(plan->next_ip + (uint64_t)op->displacement);
      return true;
    case QA_X64_PLAN_JUMP: case QA_X64_PLAN_CALL:
      result->kind = op->kind == QA_X64_PLAN_JUMP ? QA_KERNEL_OP_JUMP : QA_KERNEL_OP_CALL;
      if (op->source.is_immediate) result->target = constant_target(op->source.immediate);
      else {
        result->target.is_constant = false;
        result->target.operand = kernel_operand(&op->source.operand);
      }
      return true;
    case QA_X64_PLAN_PUSH:
      result->kind = QA_KERNEL_OP_PUSH;
      result->source = kernel_source(&op->source);
      result->width = op->width;
      return true;
    case QA_X64_PLAN_POP:
      result->kind = QA_KERNEL_OP_POP;
      result->destination = kernel_operand(&op->destination);
      result->width = op->width;
      return true;
    case QA_X64_PLAN_RETURN:
      result->kind = QA_KERNEL_OP_RETURN;
      result->discard = op->discard;
      return true;
    case QA_X64_PLAN_LEA: {
      qa_x64_kernel_operand destination = kernel_operand(&op->destination);
      if (destination.is_memory) return false;
      const qa_x64_operand *value = &op->source_operand;
      int shift = value->scale == 1 ? 0 : value->scale == 2 ? 1 : value->scale == 4 ? 2 : value->scale == 8 ? 3 : -1;
      if (shift < 0) return false;
      uint64_t displacement = (uint64_t)value->displacement + (value->rip_relative ? plan->next_ip : 0);
      result->kind = QA_KERNEL_OP_LEA;
      result->destination = destination;
      result->lea_source.has_base = !value->rip_relative && value->has_base;
      result->lea_source.base = value->has_base ? reg_offset(value->base) : 0;
      result->lea_source.has_index = value->has_index;
      result->lea_source.index = value->has_index ? reg_offset(value->index) : 0;
      result->lea_source.shift = shift;
      result->lea_source.low = (uint32_t)(displacement & 0xffffffffull);
      result->lea_source.high = (uint32_t)(displacement >> 32);
      result->lea_source.address_bits = value->address_bits;
      return true;
    }
  }
  return false;
}

bool qa_x64_integer_block_safe(const qa_x64_integer_plan *plan) {
  const qa_x64_integer_operation *operation = &plan->operation;
  switch (operation->kind) {
    case QA_KERNEL_OP_NOP: case QA_KERNEL_OP_EXTEND: case QA_KERNEL_OP_REGISTER_MOVE:
    case QA_KERNEL_OP_REGISTER_ALU: case QA_KERNEL_OP_CONDITIONAL_MOVE:
      return true;
    case QA_KERNEL_OP_SET_CONDITION:
      return !operation->destination.is_memory;
    case QA_KERNEL_OP_PASSTHROUGH: {
      const qa_x64_plan_operation *original = &plan->original->operation;
      switch (original->kind) {
        case QA_X64_PLAN_SHIFT: return original->destination.kind == QA_X64_OPERAND_REGISTER;
        case QA_X64_PLAN_MULTIPLY: return true;
        case QA_X64_PLAN_NUMERIC: case QA_X64_PLAN_NUMERIC_MEMORY: {
          const qa_numeric_instruction *instruction = &original->instruction;
          if (instruction->opcode != 0x0f || !instruction->has_secondary_opcode) return false;
          switch (instruction->secondary_opcode) {
            case 0x2a: case 0x2c: case 0x2d: case 0x2e: case 0x2f: case 0x50: case 0x51:
            case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e:
            case 0x5f: case 0xc2: case 0xe6:
              return true;
            default: return false;
          }
        }
        case QA_X64_PLAN_RAW_SSE:
          return original->raw_operation.kind != QA_RAW_SSE_MOVE || !original->raw_operation.store ||
                 original->raw_operand_is_register;
        default: return false;
      }
    }
    case QA_KERNEL_OP_INCREMENT: return !operation->destination.is_memory;
    case QA_KERNEL_OP_MOVE: return !operation->destination.is_memory;
    case QA_KERNEL_OP_LEA: case QA_KERNEL_OP_BRANCH: case QA_KERNEL_OP_RETURN: case QA_KERNEL_OP_JUMP:
      return true;
    case QA_KERNEL_OP_CALL: case QA_KERNEL_OP_PUSH: return false;
    case QA_KERNEL_OP_POP: return !operation->destination.is_memory;
    case QA_KERNEL_OP_ALU:
      return !operation->destination.is_memory || operation->alu_operation == QA_ALU_CMP ||
             operation->alu_operation == QA_ALU_TEST;
  }
  return false;
}

/* --- kernel execution --- */
static uint32_t word_at(const qa_x64_integer_kernel *kernel, int index) {
  const uint8_t *p = kernel->state->registers.bytes + (size_t)index * 4u;
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void set_word_at(qa_x64_integer_kernel *kernel, int index, uint32_t value) {
  uint8_t *p = kernel->state->registers.bytes + (size_t)index * 4u;
  p[0] = (uint8_t)(value & 0xffu);
  p[1] = (uint8_t)((value >> 8) & 0xffu);
  p[2] = (uint8_t)((value >> 16) & 0xffu);
  p[3] = (uint8_t)((value >> 24) & 0xffu);
}
static uint64_t qword_at(const qa_x64_integer_kernel *kernel, int byte_offset) {
  return (uint64_t)word_at(kernel, byte_offset >> 2) | ((uint64_t)word_at(kernel, (byte_offset >> 2) + 1) << 32);
}
static void set_qword_at(qa_x64_integer_kernel *kernel, int byte_offset, uint64_t value) {
  set_word_at(kernel, byte_offset >> 2, (uint32_t)value);
  set_word_at(kernel, (byte_offset >> 2) + 1, (uint32_t)(value >> 32));
}
static qa_guest_throw *kernel_throw(qa_x64_integer_kernel *kernel) { return kernel->memory->throw_ctx; }
static void lock_none(qa_x64_integer_kernel *kernel, bool lock, bool permitted) {
  (void)kernel;
  qa_x64_lock(lock, NULL, false, permitted);
}

static void read_memory_kernel(qa_x64_integer_kernel *kernel, qa_guest_core_address address, int width) {
  if (width == 64) {
    qa_sparse_read_u64_words(kernel->memory, address, &kernel->memory_low, &kernel->memory_high);
    kernel->low = kernel->memory_low;
    kernel->high = kernel->memory_high;
  } else {
    kernel->low = width == 8 ? qa_sparse_read_u8(kernel->memory, address)
                  : width == 16 ? qa_sparse_read_u16(kernel->memory, address)
                                : qa_sparse_read_u32(kernel->memory, address);
    kernel->high = 0;
  }
}
static void write_memory_kernel(qa_x64_integer_kernel *kernel, qa_guest_core_address address, int width) {
  if (width == 64) qa_sparse_write_u64_words(kernel->memory, address, kernel->low, kernel->high);
  else if (width == 32) qa_sparse_write_u32(kernel->memory, address, kernel->low);
  else if (width == 16) qa_sparse_write_u16(kernel->memory, address, kernel->low);
  else qa_sparse_write_u8(kernel->memory, address, kernel->low);
}
static void read_source(qa_x64_integer_kernel *kernel, const qa_x64_kernel_source *value, uint64_t next_ip);
static void read_operand_kernel(qa_x64_integer_kernel *kernel, const qa_x64_kernel_operand *value, uint64_t next_ip) {
  if (value->is_memory) {
    qa_guest_core_address address = qa_x64_operand_address(kernel->memory, kernel->state, &value->source, next_ip, QA_ACCESS_READ);
    read_memory_kernel(kernel, address, value->width);
    return;
  }
  uint32_t low = word_at(kernel, value->offset >> 2);
  kernel->low = value->width == 8 ? (low >> (value->high_byte ? 8 : 0)) & 255u
                : value->width == 16 ? low & 65535u : low;
  kernel->high = value->width == 64 ? word_at(kernel, (value->offset >> 2) + 1) : 0;
}
static void read_source(qa_x64_integer_kernel *kernel, const qa_x64_kernel_source *value, uint64_t next_ip) {
  if (value->is_immediate) {
    kernel->low = value->low;
    kernel->high = value->high;
    return;
  }
  read_operand_kernel(kernel, &value->operand, next_ip);
}
static void write_operand_kernel(qa_x64_integer_kernel *kernel, const qa_x64_kernel_operand *destination, uint64_t next_ip) {
  if (destination->is_memory) {
    qa_guest_core_address address = qa_x64_operand_address(kernel->memory, kernel->state, &destination->source, next_ip, QA_ACCESS_WRITE);
    write_memory_kernel(kernel, address, destination->width);
    return;
  }
  int index = destination->offset >> 2;
  uint32_t previous = word_at(kernel, index);
  if (destination->width == 8)
    set_word_at(kernel, index, destination->high_byte ? (previous & ~0xff00u) | ((kernel->low & 255u) << 8)
                                                      : (previous & ~255u) | (kernel->low & 255u));
  else if (destination->width == 16)
    set_word_at(kernel, index, (previous & 0xffff0000u) | (kernel->low & 65535u));
  else {
    set_word_at(kernel, index, kernel->low);
    set_word_at(kernel, index + 1, destination->width == 64 ? kernel->high : 0);
  }
}

static bool condition_of(qa_x64_integer_kernel *kernel, int code) {
  uint32_t bits = kernel->state->flags.low;
  bool carry = (bits & 1u) != 0, zero = (bits & 0x40u) != 0;
  bool sign = (bits & 0x80u) != 0, overflow = (bits & 0x800u) != 0;
  switch (code) {
    case 0: return overflow;
    case 1: return !overflow;
    case 2: return carry;
    case 3: return !carry;
    case 4: return zero;
    case 5: return !zero;
    case 6: return carry || zero;
    case 7: return !carry && !zero;
    case 8: return sign;
    case 9: return !sign;
    case 10: return (bits & 4u) != 0;
    case 11: return (bits & 4u) == 0;
    case 12: return sign != overflow;
    case 13: return sign == overflow;
    case 14: return zero || sign != overflow;
    case 15: return !zero && sign == overflow;
    default: qa_range_error("Invalid condition code %d", code);
  }
}

static void alu_kernel(qa_x64_integer_kernel *kernel, qa_alu_operation operation, int width,
                       uint32_t right_low, uint32_t right_high) {
  uint64_t limit = width == 8 ? 256ull : width == 16 ? 65536ull : 0x100000000ull;
  uint32_t a = kernel->low;
  uint32_t b = width == 8 ? right_low & 255u : width == 16 ? right_low & 65535u : right_low;
  uint32_t ah = kernel->high, bh = right_high;
  uint32_t sign = width == 8 ? 0x80u : width == 16 ? 0x8000u : 0x80000000u;
  uint32_t low = 0, high = 0, bits = 0, mask = 0x8d5u;
  switch (operation) {
    case QA_ALU_ADD: case QA_ALU_ADC: {
      uint32_t incoming = operation == QA_ALU_ADC ? kernel->state->flags.low & 1u : 0;
      uint64_t sum = (uint64_t)a + (uint64_t)b + incoming;
      low = width == 64 || width == 32 ? (uint32_t)sum : (uint32_t)(sum & (limit - 1u));
      if (width == 64) {
        uint64_t upper = (uint64_t)ah + (uint64_t)bh + (sum >= limit ? 1u : 0u);
        high = (uint32_t)upper;
        if (upper >= limit) bits |= 1u;
        if ((~(ah ^ bh) & (ah ^ high) & sign) != 0) bits |= 0x800u;
      } else {
        if (sum >= limit) bits |= 1u;
        if ((~(a ^ b) & (a ^ low) & sign) != 0) bits |= 0x800u;
      }
      if ((a & 15u) + (b & 15u) + incoming > 15u) bits |= 0x10u;
      break;
    }
    case QA_ALU_SUB: case QA_ALU_CMP: case QA_ALU_SBB: {
      uint32_t incoming = operation == QA_ALU_SBB ? kernel->state->flags.low & 1u : 0;
      int64_t difference = (int64_t)a - (int64_t)b - (int64_t)incoming;
      low = width == 64 || width == 32 ? (uint32_t)difference : (uint32_t)((uint32_t)difference & (uint32_t)(limit - 1u));
      if (width == 64) {
        int64_t upper = (int64_t)ah - (int64_t)bh - (difference < 0 ? 1 : 0);
        high = (uint32_t)upper;
        if (upper < 0) bits |= 1u;
        if (((ah ^ bh) & (ah ^ high) & sign) != 0) bits |= 0x800u;
      } else {
        if (difference < 0) bits |= 1u;
        if (((a ^ b) & (a ^ low) & sign) != 0) bits |= 0x800u;
      }
      if ((a & 15u) < (b & 15u) + incoming) bits |= 0x10u;
      break;
    }
    case QA_ALU_AND: case QA_ALU_TEST: low = a & b; high = ah & bh; mask = 0x8c5u; break;
    case QA_ALU_OR: low = a | b; high = ah | bh; mask = 0x8c5u; break;
    case QA_ALU_XOR: low = a ^ b; high = ah ^ bh; mask = 0x8c5u; break;
  }
  if (width != 64) high = 0;
  uint32_t byte = low & 255u;
  byte ^= byte >> 4;
  bits |= (low == 0 && high == 0 ? 0x40u : 0u) | (((width == 64 ? high : low) & sign) != 0 ? 0x80u : 0u) |
          (((0x9669u >> (byte & 15u)) & 1u) << 2);
  qa_processor_flags_write_low_word(&kernel->state->flags, (kernel->state->flags.low & ~mask) | bits);
  kernel->low = low;
  kernel->high = high;
}

static void register_move(qa_x64_integer_kernel *kernel, const qa_x64_kernel_word_destination *destination,
                          const qa_x64_kernel_word_source *source) {
  uint32_t low = !source->has_index ? source->low : word_at(kernel, source->index);
  uint32_t high = destination->width == 32 ? 0 : !source->has_index ? source->high : word_at(kernel, source->index + 1);
  set_word_at(kernel, destination->index, low);
  set_word_at(kernel, destination->index + 1, high);
}
static void register_alu(qa_x64_integer_kernel *kernel, qa_alu_operation operation,
                         const qa_x64_kernel_word_destination *destination, const qa_x64_kernel_word_source *source, bool lock) {
  uint32_t right_low = !source->has_index ? source->low : word_at(kernel, source->index);
  uint32_t right_high = destination->width == 32 ? 0 : !source->has_index ? source->high : word_at(kernel, source->index + 1);
  lock_none(kernel, lock, false);
  kernel->low = word_at(kernel, destination->index);
  kernel->high = destination->width == 64 ? word_at(kernel, destination->index + 1) : 0;
  alu_kernel(kernel, operation, destination->width, right_low, right_high);
  if (operation != QA_ALU_CMP && operation != QA_ALU_TEST) {
    set_word_at(kernel, destination->index, kernel->low);
    set_word_at(kernel, destination->index + 1, kernel->high);
  }
}
static void extend_op(qa_x64_integer_kernel *kernel, const qa_x64_integer_operation *operation, uint64_t next_ip) {
  read_operand_kernel(kernel, &operation->source_operand, next_ip);
  if (operation->is_signed) {
    int width = operation->source_operand.width;
    if (width == 8) kernel->low = (uint32_t)(((int32_t)kernel->low << 24) >> 24);
    else if (width == 16) kernel->low = (uint32_t)(((int32_t)kernel->low << 16) >> 16);
    if (width != 64) kernel->high = kernel->low >= 0x80000000u ? 0xffffffffu : 0;
  }
  write_operand_kernel(kernel, &operation->destination, next_ip);
}
static void conditional_move(qa_x64_integer_kernel *kernel, const qa_x64_integer_operation *operation,
                             const qa_x64_semantic_plan *original) {
  lock_none(kernel, original->lock, false);
  read_operand_kernel(kernel, &operation->source_operand, original->next_ip);
  if (condition_of(kernel, operation->condition)) write_operand_kernel(kernel, &operation->destination, original->next_ip);
  else if (operation->destination.width == 32) {
    read_operand_kernel(kernel, &operation->destination, original->next_ip);
    write_operand_kernel(kernel, &operation->destination, original->next_ip);
  }
}
static void pop_kernel(qa_x64_integer_kernel *kernel, int width) {
  uint64_t stack = qword_at(kernel, reg_offset(QA_REG_RSP));
  read_memory_kernel(kernel, qa_x64_guest_address(kernel->memory, stack, QA_ACCESS_READ), width);
  set_qword_at(kernel, reg_offset(QA_REG_RSP), stack + (width == 64 ? 8ull : 2ull));
}
static uint64_t return_target(qa_x64_integer_kernel *kernel, uint64_t discard) {
  uint64_t stack = qword_at(kernel, reg_offset(QA_REG_RSP));
  uint64_t target = qa_x64_canonical_address(kernel_throw(kernel),
      qa_sparse_read_u64(kernel->memory, qa_x64_guest_address(kernel->memory, stack, QA_ACCESS_READ)));
  set_qword_at(kernel, reg_offset(QA_REG_RSP), stack + 8ull + discard);
  return target;
}
static uint64_t target_of(qa_x64_integer_kernel *kernel, const qa_x64_kernel_target *target, uint64_t next_ip) {
  if (target->is_constant)
    return target->canonical ? target->value : qa_x64_canonical_address(kernel_throw(kernel), target->value);
  if (target->operand.width == 64) {
    uint64_t value = !target->operand.is_memory
        ? qword_at(kernel, target->operand.offset)
        : qa_sparse_read_u64(kernel->memory,
              qa_x64_operand_address(kernel->memory, kernel->state, &target->operand.source, next_ip, QA_ACCESS_READ));
    return qa_x64_canonical_address(kernel_throw(kernel), value);
  }
  read_operand_kernel(kernel, &target->operand, next_ip);
  return (uint64_t)kernel->low;
}
static void address_of(qa_x64_integer_kernel *kernel, const qa_x64_kernel_effective_address *value) {
  uint32_t a = !value->has_base ? 0 : word_at(kernel, value->base >> 2);
  uint32_t ah = !value->has_base || value->address_bits == 32 ? 0 : word_at(kernel, (value->base >> 2) + 1);
  uint32_t b = !value->has_index ? 0 : word_at(kernel, value->index >> 2);
  uint32_t bh = !value->has_index || value->address_bits == 32 ? 0 : word_at(kernel, (value->index >> 2) + 1);
  uint32_t low_index = value->shift == 0 ? b : (uint32_t)(b << value->shift);
  uint32_t high_index = value->shift == 0 ? bh : (uint32_t)((bh << value->shift) | (b >> (32 - value->shift)));
  uint64_t low = (uint64_t)a + (uint64_t)low_index + (uint64_t)value->low;
  kernel->low = (uint32_t)low;
  kernel->high = value->address_bits == 32 ? 0
                 : (uint32_t)((uint64_t)ah + (uint64_t)high_index + (uint64_t)value->high + low / 0x100000000ull);
}

bool qa_x64_integer_kernel_init(qa_x64_integer_kernel *kernel, qa_guest_processor_state *state,
                                qa_sparse_guest_memory *memory) {
  memset(kernel, 0, sizeof *kernel);
  if (state->architecture != QA_GUEST_X86_64) return false;
  kernel->state = state;
  kernel->memory = memory;
  return true;
}

static qa_x64_flow flow_branch(uint64_t target) {
  qa_x64_flow flow = {QA_X64_BRANCH, target, 0};
  return flow;
}

qa_x64_flow qa_x64_integer_kernel_execute(qa_x64_integer_kernel *kernel, const qa_x64_integer_plan *plan) {
  const qa_x64_integer_operation *op = &plan->operation;
  const qa_x64_semantic_plan *original = plan->original;
  switch (op->kind) {
    case QA_KERNEL_OP_REGISTER_MOVE:
      lock_none(kernel, original->lock, false);
      register_move(kernel, &op->word_destination, &op->word_source);
      return qa_x64_advance;
    case QA_KERNEL_OP_REGISTER_ALU:
      register_alu(kernel, op->alu_operation, &op->word_destination, &op->word_source, original->lock);
      return qa_x64_advance;
    case QA_KERNEL_OP_CONDITIONAL_MOVE:
      conditional_move(kernel, op, original);
      return qa_x64_advance;
    case QA_KERNEL_OP_SET_CONDITION:
      lock_none(kernel, original->lock, false);
      kernel->low = condition_of(kernel, op->condition) ? 1 : 0;
      kernel->high = 0;
      write_operand_kernel(kernel, &op->destination, original->next_ip);
      return qa_x64_advance;
    case QA_KERNEL_OP_PASSTHROUGH:
      return qa_execute_x64_plan(original, &kernel->memory->base, kernel->state);
    case QA_KERNEL_OP_NOP:
      lock_none(kernel, original->lock, false);
      return qa_x64_advance;
    case QA_KERNEL_OP_EXTEND:
      lock_none(kernel, original->lock, false);
      extend_op(kernel, op, original->next_ip);
      return qa_x64_advance;
    case QA_KERNEL_OP_INCREMENT: {
      qa_x64_lock(original->lock, op->destination.is_memory ? &op->destination.source : NULL,
                  op->destination.is_memory, true);
      bool has_address = op->destination.is_memory;
      qa_guest_core_address address = {0};
      if (has_address) {
        address = qa_x64_operand_address(kernel->memory, kernel->state, &op->destination.source, original->next_ip, QA_ACCESS_WRITE);
        qa_sparse_check(kernel->memory, address, (size_t)(op->destination.width / 8), QA_ACCESS_WRITE);
      }
      uint32_t carry = kernel->state->flags.low & 1u;
      if (has_address) read_memory_kernel(kernel, address, op->destination.width);
      else read_operand_kernel(kernel, &op->destination, original->next_ip);
      alu_kernel(kernel, op->subtract ? QA_ALU_SUB : QA_ALU_ADD, op->destination.width, 1, 0);
      if (has_address) write_memory_kernel(kernel, address, op->destination.width);
      else write_operand_kernel(kernel, &op->destination, original->next_ip);
      qa_processor_flags_write_low_word(&kernel->state->flags, (kernel->state->flags.low & ~1u) | carry);
      return qa_x64_advance;
    }
    case QA_KERNEL_OP_MOVE:
      lock_none(kernel, original->lock, false);
      read_source(kernel, &op->source, original->next_ip);
      write_operand_kernel(kernel, &op->destination, original->next_ip);
      return qa_x64_advance;
    case QA_KERNEL_OP_LEA:
      lock_none(kernel, original->lock, false);
      address_of(kernel, &op->lea_source);
      write_operand_kernel(kernel, &op->destination, original->next_ip);
      return qa_x64_advance;
    case QA_KERNEL_OP_ALU: {
      read_source(kernel, &op->source, original->next_ip);
      uint32_t right_low = kernel->low, right_high = kernel->high;
      bool writes = op->alu_operation != QA_ALU_CMP && op->alu_operation != QA_ALU_TEST;
      qa_x64_lock(original->lock, op->destination.is_memory ? &op->destination.source : NULL,
                  op->destination.is_memory, writes);
      bool has_address = op->destination.is_memory;
      qa_guest_core_address address = {0};
      if (has_address) {
        address = qa_x64_operand_address(kernel->memory, kernel->state, &op->destination.source, original->next_ip,
                                         writes ? QA_ACCESS_WRITE : QA_ACCESS_READ);
        if (writes) qa_sparse_check(kernel->memory, address, (size_t)(op->destination.width / 8), QA_ACCESS_WRITE);
        read_memory_kernel(kernel, address, op->destination.width);
      } else read_operand_kernel(kernel, &op->destination, original->next_ip);
      alu_kernel(kernel, op->alu_operation, op->destination.width, right_low, right_high);
      if (writes) {
        if (has_address) write_memory_kernel(kernel, address, op->destination.width);
        else write_operand_kernel(kernel, &op->destination, original->next_ip);
      }
      return qa_x64_advance;
    }
    case QA_KERNEL_OP_BRANCH:
      lock_none(kernel, original->lock, false);
      return !op->has_condition || condition_of(kernel, op->condition)
                 ? flow_branch(target_of(kernel, &op->target, original->next_ip)) : qa_x64_advance;
    case QA_KERNEL_OP_RETURN:
      lock_none(kernel, original->lock, false);
      return flow_branch(return_target(kernel, op->discard));
    case QA_KERNEL_OP_JUMP: case QA_KERNEL_OP_CALL: {
      lock_none(kernel, original->lock, false);
      uint64_t target = target_of(kernel, &op->target, original->next_ip);
      if (op->kind == QA_KERNEL_OP_CALL) {
        uint64_t stack = qword_at(kernel, reg_offset(QA_REG_RSP)) - 8ull;
        qa_sparse_write_u64(kernel->memory, qa_x64_guest_address(kernel->memory, stack, QA_ACCESS_WRITE), original->next_ip);
        set_qword_at(kernel, reg_offset(QA_REG_RSP), stack);
      }
      return flow_branch(target);
    }
    case QA_KERNEL_OP_PUSH: {
      lock_none(kernel, original->lock, false);
      read_source(kernel, &op->source, original->next_ip);
      uint64_t stack = qword_at(kernel, reg_offset(QA_REG_RSP)) - (op->width == 64 ? 8ull : 2ull);
      write_memory_kernel(kernel, qa_x64_guest_address(kernel->memory, stack, QA_ACCESS_WRITE), op->width);
      set_qword_at(kernel, reg_offset(QA_REG_RSP), stack);
      return qa_x64_advance;
    }
    case QA_KERNEL_OP_POP:
      lock_none(kernel, original->lock, false);
      pop_kernel(kernel, op->width);
      write_operand_kernel(kernel, &op->destination, original->next_ip);
      return qa_x64_advance;
  }
  return qa_x64_advance;
}

typedef struct block_state {
  int64_t instructions;
  uint64_t current;
  uint64_t next;
  bool returned_early;
  bool finished;
  uint32_t low_flags, high_flags;
  qa_guest_throw rethrow;
} block_state;

qa_x64_integer_block_result qa_x64_integer_kernel_execute_block(
    qa_x64_integer_kernel *kernel, const qa_x64_integer_step *steps, size_t step_count, int64_t budget,
    bool has_returned, uint64_t returned, bool (*after_store)(void *ctx), void *after_store_ctx) {
  block_state state;
  block_state *bs = &state;
  memset(bs, 0, sizeof *bs);
  bs->current = kernel->state->instruction_pointer;
  bs->next = bs->current;
  jmp_buf outer_buf;
  qa_guest_throw outer_throw;
  qa_guest_throw *outer = &outer_throw;
  qa_guest_throw_init(outer, &outer_buf);
  qa_guest_throw *previous = kernel->memory->throw_ctx;
  kernel->memory->throw_ctx = outer;
  qa_x64_integer_block_result result;
  memset(&result, 0, sizeof result);
  if (setjmp(outer_buf) != 0) {
    kernel->state->instruction_pointer = bs->current;
    kernel->memory->throw_ctx = previous;
    result.kind = QA_KERNEL_BLOCK_FAULT;
    result.instructions = bs->instructions;
    result.fault = *outer;
    result.fault.buf = previous != NULL ? previous->buf : NULL;
    return result;
  }
  /* volatile: the outer setjmp handler above must see a defined counter
   * after a longjmp rethrow out of a step (it returns without resuming). */
  for (volatile size_t step_index = 0; step_index < step_count; step_index++) {
    const qa_x64_integer_step *step = &steps[step_index];
    if (bs->instructions == budget) break;
    bs->current = step->start;
    if (has_returned && bs->current == returned) {
      kernel->state->instruction_pointer = bs->current;
      bs->returned_early = true;
      break;
    }
    const qa_x64_integer_operation *op = &step->integer->operation;
    const qa_x64_semantic_plan *original = step->integer->original;
    bs->next = original->next_ip;
    if (!step->safe) {
      kernel->state->instruction_pointer = bs->current;
      bool observed = qa_sparse_has_write_observers(kernel->memory);
      /* TS excludes pop/numeric/numeric-memory/shift/multiply here. */
      bool passthrough_like = op->kind == QA_KERNEL_OP_PASSTHROUGH &&
          original->operation.kind != QA_X64_PLAN_RAW_SSE;
      if (!observed && op->kind != QA_KERNEL_OP_POP && !passthrough_like) {
        qa_x64_flow flow = qa_x64_integer_kernel_execute(kernel, step->integer);
        bs->instructions++;
        if (flow.kind == QA_X64_BRANCH) { bs->next = flow.target; bs->finished = true; break; }
        if (!after_store(after_store_ctx)) { bs->finished = true; break; }
        continue;
      }
      (void)qa_integer_register_file_checkpoint(&kernel->state->registers, kernel->checkpoint, sizeof kernel->checkpoint);
      bs->low_flags = kernel->state->flags.low;
      bs->high_flags = kernel->state->flags.high;
      /* Nested handler: roll back this step, then rethrow outward. */
      jmp_buf inner_buf;
      qa_guest_throw inner_throw;
      qa_guest_throw_init(&inner_throw, &inner_buf);
      kernel->memory->throw_ctx = &inner_throw;
      if (setjmp(inner_buf) == 0) {
        qa_x64_flow flow = qa_x64_integer_kernel_execute(kernel, step->integer);
        kernel->memory->throw_ctx = outer;
        if (flow.kind == QA_X64_BRANCH) { bs->next = flow.target; bs->instructions++; bs->finished = true; break; }
      } else {
        qa_guest_throw caught = inner_throw;
        qa_integer_register_file_restore(&kernel->state->registers, kernel->checkpoint, sizeof kernel->checkpoint);
        qa_processor_flags_restore_words(&kernel->state->flags, bs->low_flags, bs->high_flags);
        *outer = caught;
        kernel->memory->throw_ctx = outer;
        longjmp(outer_buf, 1);
      }
      bs->instructions++;
      if (observed || !after_store(after_store_ctx)) { bs->finished = true; break; }
      continue;
    }
    switch (op->kind) {
      case QA_KERNEL_OP_REGISTER_MOVE:
        lock_none(kernel, original->lock, false);
        register_move(kernel, &op->word_destination, &op->word_source);
        break;
      case QA_KERNEL_OP_REGISTER_ALU:
        register_alu(kernel, op->alu_operation, &op->word_destination, &op->word_source, original->lock);
        break;
      case QA_KERNEL_OP_CONDITIONAL_MOVE:
        conditional_move(kernel, op, original);
        break;
      case QA_KERNEL_OP_SET_CONDITION:
        lock_none(kernel, original->lock, false);
        kernel->low = condition_of(kernel, op->condition) ? 1 : 0;
        kernel->high = 0;
        write_operand_kernel(kernel, &op->destination, original->next_ip);
        break;
      case QA_KERNEL_OP_PASSTHROUGH:
        (void)qa_execute_x64_plan(original, &kernel->memory->base, kernel->state);
        break;
      case QA_KERNEL_OP_NOP:
        lock_none(kernel, original->lock, false);
        break;
      case QA_KERNEL_OP_EXTEND:
        lock_none(kernel, original->lock, false);
        extend_op(kernel, op, original->next_ip);
        break;
      case QA_KERNEL_OP_INCREMENT: {
        lock_none(kernel, original->lock, true);
        uint32_t carry = kernel->state->flags.low & 1u;
        read_operand_kernel(kernel, &op->destination, original->next_ip);
        alu_kernel(kernel, op->subtract ? QA_ALU_SUB : QA_ALU_ADD, op->destination.width, 1, 0);
        write_operand_kernel(kernel, &op->destination, original->next_ip);
        qa_processor_flags_write_low_word(&kernel->state->flags, (kernel->state->flags.low & ~1u) | carry);
        break;
      }
      case QA_KERNEL_OP_MOVE:
        lock_none(kernel, original->lock, false);
        read_source(kernel, &op->source, original->next_ip);
        write_operand_kernel(kernel, &op->destination, original->next_ip);
        break;
      case QA_KERNEL_OP_LEA:
        lock_none(kernel, original->lock, false);
        address_of(kernel, &op->lea_source);
        write_operand_kernel(kernel, &op->destination, original->next_ip);
        break;
      case QA_KERNEL_OP_ALU: {
        read_source(kernel, &op->source, original->next_ip);
        uint32_t right_low = kernel->low, right_high = kernel->high;
        bool writes = op->alu_operation != QA_ALU_CMP && op->alu_operation != QA_ALU_TEST;
        qa_x64_lock(original->lock, op->destination.is_memory ? &op->destination.source : NULL,
                    op->destination.is_memory, writes);
        read_operand_kernel(kernel, &op->destination, original->next_ip);
        alu_kernel(kernel, op->alu_operation, op->destination.width, right_low, right_high);
        if (writes) write_operand_kernel(kernel, &op->destination, original->next_ip);
        break;
      }
      case QA_KERNEL_OP_BRANCH:
        lock_none(kernel, original->lock, false);
        if (!op->has_condition || condition_of(kernel, op->condition))
          bs->next = target_of(kernel, &op->target, original->next_ip);
        break;
      case QA_KERNEL_OP_RETURN:
        lock_none(kernel, original->lock, false);
        bs->next = return_target(kernel, op->discard);
        break;
      case QA_KERNEL_OP_JUMP:
        lock_none(kernel, original->lock, false);
        bs->next = target_of(kernel, &op->target, original->next_ip);
        break;
      case QA_KERNEL_OP_POP:
        lock_none(kernel, original->lock, false);
        pop_kernel(kernel, op->width);
        write_operand_kernel(kernel, &op->destination, original->next_ip);
        break;
      case QA_KERNEL_OP_PUSH: case QA_KERNEL_OP_CALL:
        qa_range_error("A memory-writing instruction cannot run in a read-only integer block");
    }
    bs->instructions++;
    (void)bs->finished;
  }
  kernel->memory->throw_ctx = previous;
  if (bs->returned_early) {
    result.kind = QA_KERNEL_BLOCK_RETURN;
    result.instructions = bs->instructions;
    return result;
  }
  kernel->state->instruction_pointer = bs->next;
  result.kind = QA_KERNEL_BLOCK_COMPLETE;
  result.instructions = bs->instructions;
  return result;
}


