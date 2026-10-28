/* Port of src/guest/x64/cpu.ts. GPL-2.0-or-later. */
#include "qa/guest/x64/cpu.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/floating_point/raw_sse.h"
#include "qa/guest/floating_point/sse.h"
#include "qa/guest/x86/arithmetic.h"

static uint64_t as_uint_n(int width, uint64_t v) {
  return width >= 64 ? v : v & ((1ull << width) - 1ull);
}
static int64_t as_int_n(int width, uint64_t v) {
  v = as_uint_n(width, v);
  if (width < 64 && ((v >> (width - 1)) & 1ull) != 0) return (int64_t)v - (int64_t)(1ull << width);
  return (int64_t)v;
}
static qa_guest_throw *throw_of(qa_x64_cpu *cpu) { return cpu->memory->throw_ctx; }
_Noreturn static void pfault(qa_x64_cpu *cpu, int vector, const char *detail) {
  qa_guest_throw_processor(throw_of(cpu), vector, false, 0, "%s", detail);
}
_Noreturn static void unsupported(qa_x64_cpu *cpu, const char *format, ...) {
  char detail[512];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(detail, sizeof detail, format, args);
  va_end(args);
  qa_guest_throw_unsupported(throw_of(cpu), "%s", detail);
}
static uint64_t reg_read(qa_x64_cpu *cpu, qa_guest_register reg, int width, bool high_byte) {
  return qa_integer_register_file_read(&cpu->state->registers, reg, width, high_byte);
}
static void reg_write(qa_x64_cpu *cpu, qa_guest_register reg, int width, uint64_t value, bool high_byte) {
  qa_integer_register_file_write(&cpu->state->registers, reg, width, value, high_byte);
}
static bool flag_get(qa_x64_cpu *cpu, qa_guest_flag flag) { return qa_processor_flags_get(&cpu->state->flags, flag); }
static void flag_set(qa_x64_cpu *cpu, qa_guest_flag flag, bool value) {
  qa_processor_flags_set(&cpu->state->flags, flag, value);
}

static const qa_alu_operation k_arithmetic[8] = {QA_ALU_ADD, QA_ALU_OR, QA_ALU_ADC, QA_ALU_SBB,
                                                 QA_ALU_AND, QA_ALU_SUB, QA_ALU_XOR, QA_ALU_CMP};
static const qa_shift_operation k_shifts[8] = {QA_SHIFT_ROL, QA_SHIFT_ROR, QA_SHIFT_RCL, QA_SHIFT_RCR,
                                               QA_SHIFT_SHL, QA_SHIFT_SHR, QA_SHIFT_SHL, QA_SHIFT_SAR};

static void lock_check(qa_x64_cpu *cpu, bool lock, const qa_x64_operand *destination, bool has_destination, bool permitted) {
  if (lock && (!permitted || !has_destination || destination->kind != QA_X64_OPERAND_MEMORY))
    pfault(cpu, 6, "LOCK requires a supported memory read-modify-write operand");
}

static qa_x64_flow flow_branch(uint64_t target) {
  qa_x64_flow flow = {QA_X64_BRANCH, target, 0};
  return flow;
}
static qa_x64_flow branch_to(qa_x64_cpu *cpu, uint64_t target) {
  return flow_branch(qa_x64_canonical_address(throw_of(cpu), target));
}

static void push_value(qa_x64_cpu *cpu, uint64_t value, int width) {
  uint64_t next = reg_read(cpu, QA_REG_RSP, 64, false) - (uint64_t)(width / 8);
  qa_x64_write_memory(cpu->memory, qa_x64_guest_address(cpu->memory, next, QA_ACCESS_WRITE), width, value);
  reg_write(cpu, QA_REG_RSP, 64, next, false);
}
static uint64_t pop_value(qa_x64_cpu *cpu, int width) {
  uint64_t stack = reg_read(cpu, QA_REG_RSP, 64, false);
  uint64_t value = qa_x64_read_memory(cpu->memory, qa_x64_guest_address(cpu->memory, stack, QA_ACCESS_READ), width);
  reg_write(cpu, QA_REG_RSP, 64, stack + (uint64_t)(width / 8), false);
  return value;
}

static uint64_t source_value(qa_x64_cpu *cpu, const qa_x64_plan_source *source, uint64_t next_ip) {
  return source->is_immediate ? source->immediate
                              : qa_x64_read_operand(cpu->memory, cpu->state, &source->operand, next_ip);
}
static qa_x64_plan_source source_immediate(uint64_t value) {
  qa_x64_plan_source source;
  memset(&source, 0, sizeof source);
  source.is_immediate = true;
  source.immediate = value;
  return source;
}
static qa_x64_plan_source source_operand(const qa_x64_operand *operand) {
  qa_x64_plan_source source;
  memset(&source, 0, sizeof source);
  source.is_immediate = false;
  source.operand = *operand;
  return source;
}

/* plan.ts executeX64Plan mirrored over the throw channel. */
static qa_x64_flow execute_plan(qa_x64_cpu *cpu, const qa_x64_semantic_plan *plan) {
  const qa_x64_plan_operation *op = &plan->operation;
  qa_sparse_guest_memory *memory = cpu->memory;
  qa_guest_processor_state *state = cpu->state;
  switch (op->kind) {
    case QA_X64_PLAN_NOP:
      lock_check(cpu, plan->lock, NULL, false, false);
      return qa_x64_advance;
    case QA_X64_PLAN_SHIFT: {
      int count = op->shift_count_is_cl ? (int)reg_read(cpu, QA_REG_RCX, 8, false) : op->shift_count;
      lock_check(cpu, plan->lock, NULL, false, false);
      qa_x64_writable_operand(memory, state, &op->destination, plan->next_ip);
      uint64_t result = qa_x86_shift(op->shift_operation, op->destination.width,
                                     qa_x64_read_operand(memory, state, &op->destination, plan->next_ip),
                                     count, &state->flags);
      qa_x64_write_operand(memory, state, &op->destination, plan->next_ip, result);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_MULTIPLY: {
      lock_check(cpu, plan->lock, NULL, false, false);
      uint64_t left = qa_x64_read_operand(memory, state, &op->left, plan->next_ip);
      uint64_t right = source_value(cpu, &op->right, plan->next_ip);
      qa_x64_write_operand(memory, state, &op->destination, plan->next_ip,
                           qa_x86_signed_multiply(op->destination.width, left, right, &state->flags));
      return qa_x64_advance;
    }
    case QA_X64_PLAN_CONDITIONAL_MOVE: {
      lock_check(cpu, plan->lock, NULL, false, false);
      uint64_t value = qa_x64_read_operand(memory, state, &op->source_operand, plan->next_ip);
      if (qa_x86_condition(op->condition, &state->flags))
        qa_x64_write_operand(memory, state, &op->destination, plan->next_ip, value);
      else if (op->destination.width == 32)
        qa_x64_write_operand(memory, state, &op->destination, plan->next_ip,
                             qa_x64_read_operand(memory, state, &op->destination, plan->next_ip));
      return qa_x64_advance;
    }
    case QA_X64_PLAN_SET_CONDITION:
      lock_check(cpu, plan->lock, NULL, false, false);
      qa_x64_write_operand(memory, state, &op->destination, plan->next_ip,
                           qa_x86_condition(op->condition, &state->flags) ? 1 : 0);
      return qa_x64_advance;
    case QA_X64_PLAN_EXTEND: {
      lock_check(cpu, plan->lock, NULL, false, false);
      uint64_t value = qa_x64_read_operand(memory, state, &op->source_operand, plan->next_ip);
      qa_x64_write_operand(memory, state, &op->destination, plan->next_ip,
                           op->is_signed ? (uint64_t)as_int_n(op->source_operand.width, value) : value);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_INCREMENT: {
      lock_check(cpu, plan->lock, &op->destination, true, true);
      qa_x64_writable_operand(memory, state, &op->destination, plan->next_ip);
      bool carry = flag_get(cpu, QA_FLAG_CARRY);
      uint64_t value = qa_x86_alu(op->subtract ? QA_ALU_SUB : QA_ALU_ADD, op->destination.width,
                                  qa_x64_read_operand(memory, state, &op->destination, plan->next_ip), 1, &state->flags);
      qa_x64_write_operand(memory, state, &op->destination, plan->next_ip, value);
      flag_set(cpu, QA_FLAG_CARRY, carry);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_NUMERIC: case QA_X64_PLAN_NUMERIC_MEMORY: {
      lock_check(cpu, plan->lock, NULL, false, false);
      qa_numeric_instruction instruction = op->instruction;
      if (op->kind == QA_X64_PLAN_NUMERIC_MEMORY) {
        instruction.has_operand = true;
        instruction.operand.kind = QA_OPERAND_MEMORY;
        instruction.operand.address = qa_x64_operand_address(memory, state, &op->source_operand, plan->next_ip, QA_ACCESS_READ);
        instruction.operand.index = 0;
      }
      qa_numeric_context context;
      qa_numeric_context_init(&context, state, &memory->base, &instruction);
      qa_numeric_result result;
      qa_execute_numeric_instruction(&result, &context);
      if (result.kind == QA_NUMERIC_EXCEPTION) pfault(cpu, result.vector, result.detail);
      if (result.kind == QA_NUMERIC_UNSUPPORTED) unsupported(cpu, "%s", result.detail);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_RAW_SSE: {
      lock_check(cpu, plan->lock, NULL, false, false);
      qa_numeric_operand operand;
      if (op->raw_operand_is_register) operand = op->raw_register_operand;
      else {
        operand.kind = QA_OPERAND_MEMORY;
        operand.address = qa_x64_operand_address(memory, state, &op->source_operand, plan->next_ip, QA_ACCESS_READ);
        operand.index = 0;
      }
      qa_numeric_result result;
      qa_execute_raw_sse(&result, &op->raw_operation, &operand, state, &memory->base);
      if (result.kind == QA_NUMERIC_UNSUPPORTED) unsupported(cpu, "%s", result.detail);
      if (result.kind == QA_NUMERIC_EXCEPTION) pfault(cpu, result.vector, result.detail);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_MOVE: {
      lock_check(cpu, plan->lock, NULL, false, false);
      uint64_t value = source_value(cpu, &op->source, plan->next_ip);
      qa_x64_write_operand(memory, state, &op->destination, plan->next_ip, value);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_LEA:
      lock_check(cpu, plan->lock, NULL, false, false);
      qa_x64_write_operand(memory, state, &op->destination, plan->next_ip,
                           qa_x64_effective_operand_offset(state, &op->source_operand, plan->next_ip));
      return qa_x64_advance;
    case QA_X64_PLAN_ALU: {
      uint64_t right = source_value(cpu, &op->source, plan->next_ip);
      bool writes = op->alu_operation != QA_ALU_CMP && op->alu_operation != QA_ALU_TEST;
      lock_check(cpu, plan->lock, &op->destination, true, writes);
      if (writes) qa_x64_writable_operand(memory, state, &op->destination, plan->next_ip);
      uint64_t result = qa_x86_alu(op->alu_operation, op->destination.width,
                                   qa_x64_read_operand(memory, state, &op->destination, plan->next_ip),
                                   right, &state->flags);
      if (writes) qa_x64_write_operand(memory, state, &op->destination, plan->next_ip, result);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_BRANCH:
      lock_check(cpu, plan->lock, NULL, false, false);
      return !op->has_condition || qa_x86_condition(op->condition, &state->flags)
                 ? branch_to(cpu, plan->next_ip + (uint64_t)op->displacement) : qa_x64_advance;
    case QA_X64_PLAN_JUMP: case QA_X64_PLAN_CALL: {
      lock_check(cpu, plan->lock, NULL, false, false);
      uint64_t target = qa_x64_canonical_address(throw_of(cpu), source_value(cpu, &op->source, plan->next_ip));
      if (op->kind == QA_X64_PLAN_CALL) {
        uint64_t stack = reg_read(cpu, QA_REG_RSP, 64, false) - 8ull;
        qa_sparse_write_u64(memory, qa_x64_guest_address(memory, stack, QA_ACCESS_WRITE), plan->next_ip);
        reg_write(cpu, QA_REG_RSP, 64, stack, false);
      }
      return flow_branch(target);
    }
    case QA_X64_PLAN_PUSH: {
      lock_check(cpu, plan->lock, NULL, false, false);
      uint64_t value = source_value(cpu, &op->source, plan->next_ip);
      uint64_t stack = reg_read(cpu, QA_REG_RSP, 64, false) - (op->width == 64 ? 8ull : 2ull);
      qa_guest_core_address address = qa_x64_guest_address(memory, stack, QA_ACCESS_WRITE);
      if (op->width == 64) qa_sparse_write_u64(memory, address, value);
      else qa_sparse_write_u16(memory, address, (uint32_t)(value & 0xffffull));
      reg_write(cpu, QA_REG_RSP, 64, stack, false);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_POP: {
      lock_check(cpu, plan->lock, NULL, false, false);
      uint64_t stack = reg_read(cpu, QA_REG_RSP, 64, false);
      qa_guest_core_address address = qa_x64_guest_address(memory, stack, QA_ACCESS_READ);
      uint64_t value = op->width == 64 ? qa_sparse_read_u64(memory, address) : qa_sparse_read_u16(memory, address);
      reg_write(cpu, QA_REG_RSP, 64, stack + (op->width == 64 ? 8ull : 2ull), false);
      qa_x64_write_operand(memory, state, &op->destination, plan->next_ip, value);
      return qa_x64_advance;
    }
    case QA_X64_PLAN_RETURN: {
      lock_check(cpu, plan->lock, NULL, false, false);
      uint64_t stack = reg_read(cpu, QA_REG_RSP, 64, false);
      uint64_t target = qa_sparse_read_u64(memory, qa_x64_guest_address(memory, stack, QA_ACCESS_READ));
      reg_write(cpu, QA_REG_RSP, 64, stack + 8ull, false);
      uint64_t branch = qa_x64_canonical_address(throw_of(cpu), target);
      reg_write(cpu, QA_REG_RSP, 64, reg_read(cpu, QA_REG_RSP, 64, false) + op->discard, false);
      return flow_branch(branch);
    }
  }
  return qa_x64_advance;
}

static qa_x64_flow planned(qa_x64_cpu *cpu, qa_x64_decode_cursor *cursor, const qa_x64_plan_operation *operation) {
  qa_make_x64_plan(&cursor->plan, operation, qa_x64_cursor_next_ip(cursor), cursor->lock);
  cursor->has_plan = true;
  return execute_plan(cpu, &cursor->plan);
}
static qa_x64_plan_operation plan_op(qa_x64_plan_kind kind) {
  qa_x64_plan_operation operation;
  memset(&operation, 0, sizeof operation);
  operation.kind = kind;
  return operation;
}

/* 128-bit helpers for 64-bit MUL/DIV (no __int128: -Wpedantic). */
static void mul_u64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo) {
  uint64_t a_lo = a & 0xffffffffull, a_hi = a >> 32;
  uint64_t b_lo = b & 0xffffffffull, b_hi = b >> 32;
  uint64_t p0 = a_lo * b_lo, p1 = a_lo * b_hi, p2 = a_hi * b_lo, p3 = a_hi * b_hi;
  uint64_t middle = (p0 >> 32) + (p1 & 0xffffffffull) + (p2 & 0xffffffffull);
  *lo = (p0 & 0xffffffffull) | (middle << 32);
  *hi = p3 + (p1 >> 32) + (p2 >> 32) + (middle >> 32);
}
static void divmod_u128(uint64_t hi, uint64_t lo, uint64_t divisor, uint64_t *q_hi, uint64_t *q_lo, uint64_t *rem) {
  uint64_t quotient_hi = 0, quotient_lo = 0, remainder = 0;
  for (int i = 127; i >= 0; i--) {
    uint64_t bit = i >= 64 ? (hi >> (i - 64)) & 1ull : (lo >> i) & 1ull;
    /* remainder is < divisor before the shift when the quotient fits;
     * track the bit shifted out to stay exact for any dividend. */
    uint64_t carry = remainder >> 63;
    remainder = (remainder << 1) | bit;
    if (carry != 0 || remainder >= divisor) {
      remainder -= divisor;
      if (i >= 64) quotient_hi |= 1ull << (i - 64);
      else quotient_lo |= 1ull << i;
    }
  }
  *q_hi = quotient_hi;
  *q_lo = quotient_lo;
  *rem = remainder;
}

static qa_x64_flow do_group5(qa_x64_cpu *cpu, qa_x64_decode_cursor *cursor) {
  int opcode = qa_x64_cursor_opcode(cursor);
  qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, opcode == 0xfe ? 8 : qa_x64_cursor_width(cursor));
  if (decoded.extension < 2) {
    qa_x64_plan_operation operation = plan_op(QA_X64_PLAN_INCREMENT);
    operation.destination = decoded.rm;
    operation.subtract = decoded.extension == 1;
    return planned(cpu, cursor, &operation);
  }
  lock_check(cpu, cursor->lock, NULL, false, false);
  if (opcode == 0xfe) pfault(cpu, 6, "Invalid byte INC/DEC group");
  if (decoded.extension == 2 || decoded.extension == 4) {
    qa_x64_operand operand = decoded.rm;
    operand.width = 64;
    qa_x64_plan_operation operation = plan_op(decoded.extension == 2 ? QA_X64_PLAN_CALL : QA_X64_PLAN_JUMP);
    operation.source = source_operand(&operand);
    return planned(cpu, cursor, &operation);
  }
  if (decoded.extension == 6) {
    qa_x64_operand operand = decoded.rm;
    operand.width = qa_x64_cursor_stack_width(cursor);
    qa_x64_plan_operation operation = plan_op(QA_X64_PLAN_PUSH);
    operation.source = source_operand(&operand);
    operation.width = qa_x64_cursor_stack_width(cursor);
    return planned(cpu, cursor, &operation);
  }
  unsupported(cpu, "Far or unsupported FF group /%d", decoded.extension);
}

static qa_x64_flow do_unary(qa_x64_cpu *cpu, qa_x64_decode_cursor *cursor) {
  int width = qa_x64_cursor_opcode(cursor) == 0xf6 ? 8 : qa_x64_cursor_width(cursor);
  qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, width);
  if (decoded.extension == 0) {
    qa_x64_plan_operation operation = plan_op(QA_X64_PLAN_ALU);
    operation.alu_operation = QA_ALU_TEST;
    operation.destination = decoded.rm;
    operation.source = source_immediate(qa_x64_cursor_immediate(cursor, width));
    return planned(cpu, cursor, &operation);
  }
  if (decoded.extension == 2 || decoded.extension == 3) {
    lock_check(cpu, cursor->lock, &decoded.rm, true, true);
    qa_x64_cursor_writable(cursor, &decoded.rm);
    uint64_t value = qa_x64_cursor_read(cursor, &decoded.rm);
    qa_x64_cursor_write(cursor, &decoded.rm,
                        decoded.extension == 2 ? ~value : qa_x86_alu(QA_ALU_SUB, width, 0, value, &cpu->state->flags));
    return qa_x64_advance;
  }
  lock_check(cpu, cursor->lock, NULL, false, false);
  if (decoded.extension == 1) pfault(cpu, 6, "Undefined F6/F7 group /1");
  uint64_t value = qa_x64_cursor_read(cursor, &decoded.rm);
  if (decoded.extension == 4 || decoded.extension == 5) {
    bool is_signed = decoded.extension == 5;
    uint64_t accumulator = reg_read(cpu, QA_REG_RAX, width, false);
    uint64_t low, high;
    bool overflow;
    if (width == 64) {
      mul_u64(accumulator, value, &high, &low);
      overflow = is_signed ? high != (((int64_t)low) < 0 ? UINT64_MAX : 0) : high != 0;
    } else if (is_signed) {
      int64_t full = as_int_n(width, accumulator) * as_int_n(width, value);
      low = as_uint_n(width, (uint64_t)full);
      high = as_uint_n(width, (uint64_t)(full >> width));
      overflow = as_int_n(width, low) != full;
    } else {
      uint64_t full = accumulator * value;
      low = as_uint_n(width, full);
      high = as_uint_n(width, full >> width);
      overflow = high != 0;
    }
    if (width == 8) reg_write(cpu, QA_REG_RAX, 16, (high << 8) | low, false);
    else {
      reg_write(cpu, QA_REG_RAX, width, low, false);
      reg_write(cpu, QA_REG_RDX, width, high, false);
    }
    flag_set(cpu, QA_FLAG_CARRY, overflow);
    flag_set(cpu, QA_FLAG_OVERFLOW, overflow);
    return qa_x64_advance;
  }
  bool is_signed = decoded.extension == 7;
  uint64_t quotient, remainder;
  if (width == 64) {
    uint64_t dividend_hi = reg_read(cpu, QA_REG_RDX, 64, false);
    uint64_t dividend_lo = reg_read(cpu, QA_REG_RAX, 64, false);
    if (is_signed) {
      int64_t divisor = (int64_t)value;
      if (divisor == 0) pfault(cpu, 0, "Integer divide by zero");
      bool negative_dividend = ((int64_t)dividend_hi) < 0;
      uint64_t mag_hi = dividend_hi, mag_lo = dividend_lo;
      if (negative_dividend) {
        mag_lo = ~dividend_lo + 1ull;
        mag_hi = ~dividend_hi + (mag_lo == 0 ? 1ull : 0ull);
      }
      uint64_t mag_divisor = divisor < 0 ? (uint64_t)(-(divisor + 1)) + 1ull : (uint64_t)divisor;
      uint64_t q_hi, q_lo, r;
      divmod_u128(mag_hi, mag_lo, mag_divisor, &q_hi, &q_lo, &r);
      bool negative_quotient = negative_dividend != (divisor < 0);
      if (q_hi != 0 || q_lo > (negative_quotient ? 0x8000000000000000ull : 0x7fffffffffffffffull))
        pfault(cpu, 0, "Integer quotient overflow");
      quotient = negative_quotient ? 0ull - q_lo : q_lo;
      remainder = negative_dividend ? 0ull - r : r;
    } else {
      if (value == 0) pfault(cpu, 0, "Integer divide by zero");
      uint64_t q_hi, q_lo, r;
      divmod_u128(dividend_hi, dividend_lo, value, &q_hi, &q_lo, &r);
      if (q_hi != 0) pfault(cpu, 0, "Integer quotient overflow");
      quotient = q_lo;
      remainder = r;
    }
  } else {
    uint64_t raw_dividend = width == 8 ? reg_read(cpu, QA_REG_RAX, 16, false)
                                       : (reg_read(cpu, QA_REG_RDX, width, false) << width) |
                                             reg_read(cpu, QA_REG_RAX, width, false);
    if (is_signed) {
      int64_t divisor = as_int_n(width, value);
      int64_t dividend = as_int_n(width * 2, raw_dividend);
      if (divisor == 0) pfault(cpu, 0, "Integer divide by zero");
      int64_t q = dividend / divisor;
      int64_t r = dividend % divisor;
      if (as_int_n(width, (uint64_t)q) != q) pfault(cpu, 0, "Integer quotient overflow");
      quotient = (uint64_t)q;
      remainder = (uint64_t)r;
    } else {
      if (value == 0) pfault(cpu, 0, "Integer divide by zero");
      quotient = raw_dividend / value;
      remainder = raw_dividend % value;
      if (as_uint_n(width, quotient) != quotient) pfault(cpu, 0, "Integer quotient overflow");
    }
  }
  if (width == 8) {
    reg_write(cpu, QA_REG_RAX, 8, quotient, false);
    reg_write(cpu, QA_REG_RAX, 8, remainder, true);
  } else {
    reg_write(cpu, QA_REG_RAX, width, quotient, false);
    reg_write(cpu, QA_REG_RDX, width, remainder, false);
  }
  return qa_x64_advance;
}

static qa_x64_flow do_string(qa_x64_cpu *cpu, qa_x64_decode_cursor *cursor) {
  lock_check(cpu, cursor->lock, NULL, false, false);
  int op = qa_x64_cursor_opcode(cursor);
  int width = (op & 1) == 0 ? 8 : qa_x64_cursor_width(cursor);
  int address_bits = qa_x64_cursor_address_bits(cursor);
  bool repeated = cursor->repeat != QA_X64_REPEAT_NONE;
  uint64_t count = reg_read(cpu, QA_REG_RCX, address_bits, false);
  if (repeated && count == 0) return qa_x64_advance;
  uint64_t source_offset = reg_read(cpu, QA_REG_RSI, address_bits, false);
  uint64_t destination_offset = reg_read(cpu, QA_REG_RDI, address_bits, false);
  int64_t delta = (int64_t)(width / 8) * (flag_get(cpu, QA_FLAG_DIRECTION) ? -1 : 1);
  bool reads_source = op == 0xa4 || op == 0xa5 || op == 0xa6 || op == 0xa7 || op == 0xac || op == 0xad;
  bool uses_destination = op != 0xac && op != 0xad;
  uint64_t source = 0;
  if (reads_source) {
    uint64_t base = cursor->segment == QA_X64_SEG_NONE ? 0
                    : cpu->state->segments[cursor->segment == QA_X64_SEG_FS ? QA_SEG_FS : QA_SEG_GS].base;
    source = qa_x64_read_memory(cpu->memory, qa_x64_guest_address(cpu->memory, source_offset + base, QA_ACCESS_READ), width);
  }
  if (op == 0xa4 || op == 0xa5)
    qa_x64_write_memory(cpu->memory, qa_x64_guest_address(cpu->memory, destination_offset, QA_ACCESS_WRITE), width, source);
  else if (op == 0xa6 || op == 0xa7)
    (void)qa_x86_alu(QA_ALU_CMP, width, source,
                     qa_x64_read_memory(cpu->memory, qa_x64_guest_address(cpu->memory, destination_offset, QA_ACCESS_READ), width),
                     &cpu->state->flags);
  else if (op == 0xaa || op == 0xab)
    qa_x64_write_memory(cpu->memory, qa_x64_guest_address(cpu->memory, destination_offset, QA_ACCESS_WRITE), width,
                        reg_read(cpu, QA_REG_RAX, width, false));
  else if (op == 0xac || op == 0xad)
    reg_write(cpu, QA_REG_RAX, width, source, false);
  else
    (void)qa_x86_alu(QA_ALU_CMP, width, reg_read(cpu, QA_REG_RAX, width, false),
                     qa_x64_read_memory(cpu->memory, qa_x64_guest_address(cpu->memory, destination_offset, QA_ACCESS_READ), width),
                     &cpu->state->flags);
  if (reads_source) reg_write(cpu, QA_REG_RSI, address_bits, source_offset + (uint64_t)delta, false);
  if (uses_destination) reg_write(cpu, QA_REG_RDI, address_bits, destination_offset + (uint64_t)delta, false);
  if (!repeated) return qa_x64_advance;
  count = as_uint_n(address_bits, count - 1u);
  reg_write(cpu, QA_REG_RCX, address_bits, count, false);
  bool compares = op == 0xa6 || op == 0xa7 || op == 0xae || op == 0xaf;
  bool keep = count != 0 && (!compares || flag_get(cpu, QA_FLAG_ZERO) == (cursor->repeat == QA_X64_REPEAT_F3));
  return keep ? flow_branch(qa_x64_cursor_start(cursor)) : qa_x64_advance;
}

static qa_x64_flow do_bit(qa_x64_cpu *cpu, qa_x64_decode_cursor *cursor, int opcode) {
  int width = qa_x64_cursor_width(cursor);
  qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, width);
  int operation = opcode == 0xba ? decoded.extension : opcode == 0xa3 ? 4 : opcode == 0xab ? 5 : opcode == 0xb3 ? 6 : 7;
  if (operation < 4 || operation > 7) pfault(cpu, 6, "Invalid bit-test group");
  int64_t raw_index = opcode == 0xba ? (int64_t)qa_x64_cursor_read_byte(cursor)
                                     : as_int_n(width, qa_x64_cursor_read(cursor, &decoded.reg));
  qa_x64_operand operand = decoded.rm;
  int bit_bits = width == 64 ? 6 : width == 32 ? 5 : 4;
  uint64_t bit = as_uint_n(bit_bits, (uint64_t)raw_index);
  if (operand.kind == QA_X64_OPERAND_MEMORY && opcode != 0xba) {
    int64_t index = raw_index >> bit_bits; /* TS bigint >> is arithmetic */
    operand.displacement += index * (width / 8);
  }
  lock_check(cpu, cursor->lock, &operand, true, operation != 4);
  if (operation != 4) qa_x64_cursor_writable(cursor, &operand);
  uint64_t value = qa_x64_cursor_read(cursor, &operand);
  uint64_t mask = 1ull << bit;
  flag_set(cpu, QA_FLAG_CARRY, (value & mask) != 0);
  if (operation != 4)
    qa_x64_cursor_write(cursor, &operand, operation == 5 ? value | mask : operation == 6 ? value & ~mask : value ^ mask);
  return qa_x64_advance;
}

static qa_x64_flow do_double_shift(qa_x64_cpu *cpu, qa_x64_decode_cursor *cursor, int opcode) {
  lock_check(cpu, cursor->lock, NULL, false, false);
  int width = qa_x64_cursor_width(cursor);
  qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, width);
  int count = ((opcode & 1) == 0 ? qa_x64_cursor_read_byte(cursor) : (int)reg_read(cpu, QA_REG_RCX, 8, false)) &
              (width == 64 ? 63 : 31);
  if (count == 0) {
    (void)qa_x64_cursor_read(cursor, &decoded.rm);
    return qa_x64_advance;
  }
  if (count > width) unsupported(cpu, "Undefined SHLD/SHRD count exceeds operand width");
  qa_x64_cursor_writable(cursor, &decoded.rm);
  uint64_t destination = qa_x64_cursor_read(cursor, &decoded.rm);
  uint64_t source = qa_x64_cursor_read(cursor, &decoded.reg);
  bool right = opcode >= 0xac;
  uint64_t result = right ? (destination >> count) | (source << (width - count))
                          : (destination << count) | (source >> (width - count));
  bool carry = right ? ((destination >> (count - 1)) & 1ull) != 0
                     : ((destination >> (width - count)) & 1ull) != 0;
  qa_x86_result_flags(width, result, &cpu->state->flags);
  flag_set(cpu, QA_FLAG_CARRY, carry);
  if (count == 1)
    flag_set(cpu, QA_FLAG_OVERFLOW, ((destination ^ result) & (1ull << (width - 1))) != 0);
  qa_x64_cursor_write(cursor, &decoded.rm, result);
  return qa_x64_advance;
}

static void do_numeric(qa_x64_cpu *cpu, qa_x64_decode_cursor *cursor, int secondary_opcode, bool has_secondary) {
  lock_check(cpu, cursor->lock, NULL, false, false);
  bool has_decoded = qa_x64_cursor_opcode(cursor) != 0x9b;
  qa_x64_modrm decoded;
  memset(&decoded, 0, sizeof decoded);
  if (has_decoded) decoded = qa_x64_cursor_decode_modrm(cursor, qa_x64_cursor_width(cursor));
  if (has_secondary && has_decoded) {
    qa_raw_sse_operation raw_operation;
    if (qa_prepare_raw_sse(&raw_operation, secondary_opcode, qa_x64_cursor_numeric_prefix(cursor), decoded.register_index)) {
      qa_x64_plan_operation operation = plan_op(QA_X64_PLAN_RAW_SSE);
      operation.raw_operation = raw_operation;
      if (decoded.rm.kind == QA_X64_OPERAND_MEMORY) {
        operation.source_operand = decoded.rm;
        operation.raw_operand_is_register = false;
      } else {
        operation.raw_operand_is_register = true;
        operation.raw_register_operand.kind = QA_OPERAND_REGISTER;
        operation.raw_register_operand.index = decoded.rm_index;
        operation.raw_register_operand.address.byte_offset = 0;
      }
      (void)planned(cpu, cursor, &operation);
      return;
    }
  }
  bool needs_immediate = has_secondary &&
      (secondary_opcode == 0x70 || secondary_opcode == 0x71 || secondary_opcode == 0x72 ||
       secondary_opcode == 0x73 || secondary_opcode == 0xc2 || secondary_opcode == 0xc4 ||
       secondary_opcode == 0xc5 || secondary_opcode == 0xc6);
  int immediate = needs_immediate ? qa_x64_cursor_read_byte(cursor) : 0;
  qa_numeric_instruction instruction;
  memset(&instruction, 0, sizeof instruction);
  instruction.opcode = qa_x64_cursor_opcode(cursor);
  instruction.has_secondary_opcode = has_secondary;
  instruction.secondary_opcode = secondary_opcode;
  instruction.has_modrm = has_decoded;
  instruction.modrm = has_decoded ? decoded.byte : 0;
  instruction.register_index = has_decoded ? decoded.register_index : 0;
  instruction.prefix = qa_x64_cursor_numeric_prefix(cursor);
  instruction.operand_bits = qa_x64_cursor_width(cursor);
  instruction.has_immediate = needs_immediate;
  instruction.immediate = immediate;
  qa_x64_plan_operation operation;
  if (has_decoded && decoded.rm.kind == QA_X64_OPERAND_MEMORY) {
    operation = plan_op(QA_X64_PLAN_NUMERIC_MEMORY);
    operation.instruction = instruction;
    operation.source_operand = decoded.rm;
  } else {
    operation = plan_op(QA_X64_PLAN_NUMERIC);
    if (has_decoded) {
      instruction.has_operand = true;
      instruction.operand.kind = QA_OPERAND_REGISTER;
      instruction.operand.index = decoded.rm_index;
    }
    operation.instruction = instruction;
  }
  (void)planned(cpu, cursor, &operation);
}

static qa_x64_flow do_extended(qa_x64_cpu *cpu, qa_x64_decode_cursor *cursor) {
  int op = qa_x64_cursor_read_byte(cursor);
  int width = qa_x64_cursor_width(cursor);
  if (op >= 0x80 && op <= 0x8f) {
    lock_check(cpu, cursor->lock, NULL, false, false);
    int64_t displacement = qa_x64_cursor_read_signed(cursor, 4);
    qa_x64_plan_operation operation = plan_op(QA_X64_PLAN_BRANCH);
    operation.has_condition = true;
    operation.condition = op & 15;
    operation.displacement = displacement;
    return planned(cpu, cursor, &operation);
  }
  if (op >= 0x40 && op <= 0x4f) {
    lock_check(cpu, cursor->lock, NULL, false, false);
    qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, width);
    qa_x64_plan_operation operation = plan_op(QA_X64_PLAN_CONDITIONAL_MOVE);
    operation.destination = decoded.reg;
    operation.source_operand = decoded.rm;
    operation.condition = op & 15;
    return planned(cpu, cursor, &operation);
  }
  if (op >= 0x90 && op <= 0x9f) {
    lock_check(cpu, cursor->lock, NULL, false, false);
    qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, 8);
    qa_x64_plan_operation operation = plan_op(QA_X64_PLAN_SET_CONDITION);
    operation.destination = decoded.rm;
    operation.condition = op & 15;
    return planned(cpu, cursor, &operation);
  }
  if (op >= 0xc8 && op <= 0xcf) {
    lock_check(cpu, cursor->lock, NULL, false, false);
    int bswap_width = width == 64 ? 64 : 32;
    qa_x64_operand operand = qa_x64_cursor_register(cursor, op - 0xc8 + qa_x64_cursor_rex_b(cursor), bswap_width);
    uint64_t value = qa_x64_cursor_read(cursor, &operand);
    uint64_t result = 0;
    for (int byte = 0; byte < bswap_width / 8; byte++) {
      result = (result << 8) | (value & 255ull);
      value >>= 8;
    }
    qa_x64_cursor_write(cursor, &operand, result);
    return qa_x64_advance;
  }
  switch (op) {
    case 0x0b: pfault(cpu, 6, "UD2 invalid opcode");
    case 0xa2: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      uint64_t leaf = reg_read(cpu, QA_REG_RAX, 32, false);
      uint64_t eax = 0, ebx = 0, ecx = 0, edx = 0;
      if (leaf == 0) {
        eax = 7;
        ebx = 0x6b617551ull; edx = 0x67535465ull; ecx = 0x74736575ull;
      } else if (leaf == 1) {
        eax = 0x600; ebx = 0x10000;
        edx = 0x06008101ull;
      } else if (leaf == 0x80000000ull) eax = 0x80000001ull;
      else if (leaf == 0x80000001ull) { ecx = 1; edx = 0x20000000ull; }
      reg_write(cpu, QA_REG_RAX, 32, eax, false);
      reg_write(cpu, QA_REG_RBX, 32, ebx, false);
      reg_write(cpu, QA_REG_RCX, 32, ecx, false);
      reg_write(cpu, QA_REG_RDX, 32, edx, false);
      return qa_x64_advance;
    }
    case 0x1e: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      int byte = qa_x64_cursor_read_byte(cursor);
      if (cursor->repeat != QA_X64_REPEAT_F3 || byte != 0xfa) unsupported(cpu, "Unsupported 0F 1E encoding");
      return qa_x64_advance;
    }
    case 0x1f: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      (void)qa_x64_cursor_decode_modrm(cursor, width);
      qa_x64_plan_operation operation = plan_op(QA_X64_PLAN_NOP);
      return planned(cpu, cursor, &operation);
    }
    case 0xaf: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, width);
      qa_x64_plan_operation operation = plan_op(QA_X64_PLAN_MULTIPLY);
      operation.destination = decoded.reg;
      operation.left = decoded.reg;
      operation.right = source_operand(&decoded.rm);
      return planned(cpu, cursor, &operation);
    }
    case 0xb6: case 0xb7: case 0xbe: case 0xbf: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      int source_width = (op & 1) == 0 ? 8 : 16;
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, source_width);
      qa_x64_plan_operation operation = plan_op(QA_X64_PLAN_EXTEND);
      operation.destination = qa_x64_cursor_register(cursor, decoded.register_index, width);
      operation.source_operand = decoded.rm;
      operation.is_signed = op >= 0xbe;
      return planned(cpu, cursor, &operation);
    }
    case 0xb0: case 0xb1: {
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, op == 0xb0 ? 8 : width);
      lock_check(cpu, cursor->lock, &decoded.rm, true, true);
      qa_x64_cursor_writable(cursor, &decoded.rm);
      uint64_t destination = qa_x64_cursor_read(cursor, &decoded.rm);
      uint64_t accumulator = reg_read(cpu, QA_REG_RAX, decoded.rm.width, false);
      (void)qa_x86_alu(QA_ALU_CMP, decoded.rm.width, accumulator, destination, &cpu->state->flags);
      if (accumulator == destination) qa_x64_cursor_write(cursor, &decoded.rm, qa_x64_cursor_read(cursor, &decoded.reg));
      else {
        qa_x64_cursor_write(cursor, &decoded.rm, destination);
        reg_write(cpu, QA_REG_RAX, decoded.rm.width, destination, false);
      }
      return qa_x64_advance;
    }
    case 0xc0: case 0xc1: {
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, op == 0xc0 ? 8 : width);
      lock_check(cpu, cursor->lock, &decoded.rm, true, true);
      qa_x64_cursor_writable(cursor, &decoded.rm);
      uint64_t destination = qa_x64_cursor_read(cursor, &decoded.rm);
      uint64_t sum = qa_x86_alu(QA_ALU_ADD, decoded.rm.width, destination,
                                qa_x64_cursor_read(cursor, &decoded.reg), &cpu->state->flags);
      bool is_memory = decoded.rm.kind == QA_X64_OPERAND_MEMORY;
      qa_guest_core_address address = {0};
      if (is_memory) address = qa_x64_cursor_address(cursor, &decoded.rm, QA_ACCESS_WRITE);
      qa_x64_cursor_write(cursor, &decoded.reg, destination);
      if (!is_memory) qa_x64_cursor_write(cursor, &decoded.rm, sum);
      else qa_x64_write_memory(cpu->memory, address, decoded.rm.width, sum);
      return qa_x64_advance;
    }
    case 0xbc: case 0xbd: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      if (cursor->repeat == QA_X64_REPEAT_F3)
        unsupported(cpu, "TZCNT/LZCNT require an explicit CPU feature profile");
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, width);
      uint64_t value = qa_x64_cursor_read(cursor, &decoded.rm);
      flag_set(cpu, QA_FLAG_ZERO, value == 0);
      if (value != 0) {
        int index = 0;
        if (op == 0xbc) { while ((value & 1ull) == 0) { value >>= 1; index++; } }
        else { while (value > 1) { value >>= 1; index++; } }
        qa_x64_cursor_write(cursor, &decoded.reg, (uint64_t)index);
      }
      return qa_x64_advance;
    }
    case 0xa3: case 0xab: case 0xb3: case 0xbb: case 0xba: return do_bit(cpu, cursor, op);
    case 0xa4: case 0xa5: case 0xac: case 0xad: return do_double_shift(cpu, cursor, op);
    default:
      if ((op >= 0x10 && op <= 0x17) || (op >= 0x28 && op <= 0x2f) || (op >= 0x50 && op <= 0x7f) ||
          op == 0xae || op == 0xc2 || (op >= 0xc4 && op <= 0xc6) || op >= 0xd0) {
        do_numeric(cpu, cursor, op, true);
        return qa_x64_advance;
      }
      unsupported(cpu, "Unsupported x86-64 0F opcode 0x%x", op);
  }
}

static qa_x64_flow execute_rest(qa_x64_cpu *cpu, qa_x64_decode_cursor *cursor, int op, int width);
static qa_x64_flow execute_instruction(qa_x64_cpu *cpu, qa_x64_decode_cursor *cursor) {
  int op = qa_x64_cursor_opcode(cursor);
  int width = qa_x64_cursor_width(cursor);
  if (op < 0x40 && (op & 7) <= 5) {
    qa_alu_operation operation = k_arithmetic[op >> 3];
    int form = op & 7;
    int bits = (form & 1) == 0 ? 8 : width;
    qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_ALU);
    plan.alu_operation = operation;
    if (form < 4) {
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, bits);
      plan.destination = form < 2 ? decoded.rm : decoded.reg;
      plan.source = source_operand(form < 2 ? &decoded.reg : &decoded.rm);
    } else {
      plan.destination = qa_x64_cursor_register(cursor, 0, bits);
      plan.source = source_immediate(qa_x64_cursor_immediate(cursor, bits));
    }
    return planned(cpu, cursor, &plan);
  }
  if (op >= 0x50 && op <= 0x57) {
    qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_PUSH);
    qa_x64_operand operand = qa_x64_cursor_register(cursor, op - 0x50 + qa_x64_cursor_rex_b(cursor),
                                                    qa_x64_cursor_stack_width(cursor));
    plan.source = source_operand(&operand);
    plan.width = qa_x64_cursor_stack_width(cursor);
    return planned(cpu, cursor, &plan);
  }
  if (op >= 0x58 && op <= 0x5f) {
    qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_POP);
    plan.destination = qa_x64_cursor_register(cursor, op - 0x58 + qa_x64_cursor_rex_b(cursor),
                                              qa_x64_cursor_stack_width(cursor));
    plan.width = qa_x64_cursor_stack_width(cursor);
    return planned(cpu, cursor, &plan);
  }
  if (op >= 0x70 && op <= 0x7f) {
    lock_check(cpu, cursor->lock, NULL, false, false);
    int64_t displacement = qa_x64_cursor_read_signed(cursor, 1);
    qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_BRANCH);
    plan.has_condition = true;
    plan.condition = op & 15;
    plan.displacement = displacement;
    return planned(cpu, cursor, &plan);
  }
  if (op >= 0x90 && op <= 0x97) {
    lock_check(cpu, cursor->lock, NULL, false, false);
    int index = op - 0x90 + qa_x64_cursor_rex_b(cursor);
    if (index == 0) {
      qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_NOP);
      return planned(cpu, cursor, &plan);
    }
    qa_x64_operand other = qa_x64_cursor_register(cursor, index, width);
    uint64_t value = qa_x64_cursor_read(cursor, &other);
    qa_x64_cursor_write(cursor, &other, reg_read(cpu, QA_REG_RAX, width, false));
    reg_write(cpu, QA_REG_RAX, width, value, false);
    return qa_x64_advance;
  }
  if (op >= 0xb0 && op <= 0xbf) {
    lock_check(cpu, cursor->lock, NULL, false, false);
    int bits = op < 0xb8 ? 8 : width;
    uint64_t value = qa_x64_cursor_read_unsigned(cursor, bits / 8);
    qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_MOVE);
    plan.destination = qa_x64_cursor_register(cursor, (op & 7) + qa_x64_cursor_rex_b(cursor), bits);
    plan.source = source_immediate(value);
    return planned(cpu, cursor, &plan);
  }
  if (op >= 0xd8 && op <= 0xdf) { do_numeric(cpu, cursor, 0, false); return qa_x64_advance; }
  return execute_rest(cpu, cursor, op, width);
}

static qa_x64_flow execute_tail(qa_x64_cpu *cpu, qa_x64_decode_cursor *cursor, int op, int width);
static qa_x64_flow execute_rest(qa_x64_cpu *cpu, qa_x64_decode_cursor *cursor, int op, int width) {
  switch (op) {
    case 0x0f: return do_extended(cpu, cursor);
    case 0x63: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, width == 16 ? 16 : 32);
      qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_EXTEND);
      plan.destination = qa_x64_cursor_register(cursor, decoded.register_index, width);
      plan.source_operand = decoded.rm;
      plan.is_signed = true;
      return planned(cpu, cursor, &plan);
    }
    case 0x68: case 0x6a: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      int64_t value = qa_x64_cursor_read_signed(cursor, op == 0x6a ? 1 : qa_x64_cursor_stack_width(cursor) == 16 ? 2 : 4);
      qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_PUSH);
      plan.source = source_immediate((uint64_t)value);
      plan.width = qa_x64_cursor_stack_width(cursor);
      return planned(cpu, cursor, &plan);
    }
    case 0x69: case 0x6b: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, width);
      uint64_t immediate = op == 0x6b ? (uint64_t)qa_x64_cursor_read_signed(cursor, 1)
                                      : qa_x64_cursor_immediate(cursor, width);
      qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_MULTIPLY);
      plan.destination = decoded.reg;
      plan.left = decoded.rm;
      plan.right = source_immediate(immediate);
      return planned(cpu, cursor, &plan);
    }
    case 0x80: case 0x81: case 0x83: {
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, op == 0x80 ? 8 : width);
      qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_ALU);
      plan.alu_operation = k_arithmetic[decoded.extension];
      plan.destination = decoded.rm;
      plan.source = source_immediate(op == 0x83 ? (uint64_t)qa_x64_cursor_read_signed(cursor, 1)
                                                : qa_x64_cursor_immediate(cursor, decoded.rm.width));
      return planned(cpu, cursor, &plan);
    }
    case 0x84: case 0x85: {
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, op == 0x84 ? 8 : width);
      qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_ALU);
      plan.alu_operation = QA_ALU_TEST;
      plan.destination = decoded.rm;
      plan.source = source_operand(&decoded.reg);
      return planned(cpu, cursor, &plan);
    }
    case 0x86: case 0x87: {
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, op == 0x86 ? 8 : width);
      lock_check(cpu, cursor->lock, &decoded.rm, true, true);
      qa_x64_cursor_writable(cursor, &decoded.rm);
      uint64_t value = qa_x64_cursor_read(cursor, &decoded.rm);
      qa_x64_cursor_write(cursor, &decoded.rm, qa_x64_cursor_read(cursor, &decoded.reg));
      qa_x64_cursor_write(cursor, &decoded.reg, value);
      return qa_x64_advance;
    }
    case 0x88: case 0x89: case 0x8a: case 0x8b: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, (op & 1) == 0 ? 8 : width);
      qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_MOVE);
      plan.destination = op < 0x8a ? decoded.rm : decoded.reg;
      plan.source = source_operand(op < 0x8a ? &decoded.reg : &decoded.rm);
      return planned(cpu, cursor, &plan);
    }
    case 0x8d: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, width);
      if (decoded.rm.kind != QA_X64_OPERAND_MEMORY) pfault(cpu, 6, "LEA requires a memory addressing form");
      qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_LEA);
      plan.destination = decoded.reg;
      plan.source_operand = decoded.rm;
      return planned(cpu, cursor, &plan);
    }
    case 0x8f: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, qa_x64_cursor_stack_width(cursor));
      if (decoded.extension != 0) unsupported(cpu, "POP/XOP group /%d", decoded.extension);
      qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_POP);
      plan.destination = decoded.rm;
      plan.width = qa_x64_cursor_stack_width(cursor);
      return planned(cpu, cursor, &plan);
    }
    case 0x98: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      int source_width = width == 64 ? 32 : width == 32 ? 16 : 8;
      reg_write(cpu, QA_REG_RAX, width,
                (uint64_t)as_int_n(source_width, reg_read(cpu, QA_REG_RAX, source_width, false)), false);
      return qa_x64_advance;
    }
    case 0x99: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      uint64_t value = as_int_n(width, reg_read(cpu, QA_REG_RAX, width, false)) < 0 ? UINT64_MAX : 0;
      reg_write(cpu, QA_REG_RDX, width, value, false);
      return qa_x64_advance;
    }
    case 0x9b: do_numeric(cpu, cursor, 0, false); return qa_x64_advance;
    case 0x9c:
      lock_check(cpu, cursor->lock, NULL, false, false);
      push_value(cpu, qa_processor_flags_value(&cpu->state->flags) & ~0x30000ull, qa_x64_cursor_stack_width(cursor));
      return qa_x64_advance;
    case 0x9d: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      uint64_t value = pop_value(cpu, qa_x64_cursor_stack_width(cursor));
      uint64_t mask = qa_x64_cursor_stack_width(cursor) == 16 ? 0x4dd5ull : 0x244dd5ull;
      qa_processor_flags_set_value(&cpu->state->flags,
          (qa_processor_flags_value(&cpu->state->flags) & ~mask & ~0x10000ull) | (value & mask) | 2ull);
      return qa_x64_advance;
    }
    case 0x9e:
      lock_check(cpu, cursor->lock, NULL, false, false);
      qa_processor_flags_set_value(&cpu->state->flags,
          (qa_processor_flags_value(&cpu->state->flags) & ~0xd5ull) |
              (reg_read(cpu, QA_REG_RAX, 8, true) & 0xd5ull) | 2ull);
      return qa_x64_advance;
    case 0x9f:
      lock_check(cpu, cursor->lock, NULL, false, false);
      reg_write(cpu, QA_REG_RAX, 8, (qa_processor_flags_value(&cpu->state->flags) & 0xd5ull) | 2ull, true);
      return qa_x64_advance;
    default: break;
  }
  return execute_tail(cpu, cursor, op, width);
}

static qa_x64_flow execute_tail(qa_x64_cpu *cpu, qa_x64_decode_cursor *cursor, int op, int width) {
  switch (op) {
    case 0xa0: case 0xa1: case 0xa2: case 0xa3: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      int bits = (op & 1) == 0 ? 8 : width;
      uint64_t raw = qa_x64_cursor_read_unsigned(cursor, qa_x64_cursor_address_bits(cursor) / 8);
      uint64_t segment_base = cursor->segment == QA_X64_SEG_NONE ? 0
          : cpu->state->segments[cursor->segment == QA_X64_SEG_FS ? QA_SEG_FS : QA_SEG_GS].base;
      qa_guest_core_address address = qa_x64_guest_address(cpu->memory, raw + segment_base,
                                                      op < 0xa2 ? QA_ACCESS_READ : QA_ACCESS_WRITE);
      if (op < 0xa2) reg_write(cpu, QA_REG_RAX, bits, qa_x64_read_memory(cpu->memory, address, bits), false);
      else qa_x64_write_memory(cpu->memory, address, bits, reg_read(cpu, QA_REG_RAX, bits, false));
      return qa_x64_advance;
    }
    case 0xa4: case 0xa5: case 0xa6: case 0xa7: case 0xaa: case 0xab:
    case 0xac: case 0xad: case 0xae: case 0xaf:
      return do_string(cpu, cursor);
    case 0xa8: case 0xa9: {
      int bits = op == 0xa8 ? 8 : width;
      qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_ALU);
      plan.alu_operation = QA_ALU_TEST;
      plan.destination = qa_x64_cursor_register(cursor, 0, bits);
      plan.source = source_immediate(qa_x64_cursor_immediate(cursor, bits));
      return planned(cpu, cursor, &plan);
    }
    case 0xc0: case 0xc1: case 0xd0: case 0xd1: case 0xd2: case 0xd3: {
      int bits = (op & 1) == 0 ? 8 : width;
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, bits);
      qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_SHIFT);
      plan.destination = decoded.rm;
      plan.shift_operation = k_shifts[decoded.extension];
      if (op < 0xd0) { plan.shift_count = qa_x64_cursor_read_byte(cursor); plan.shift_count_is_cl = false; }
      else if (op < 0xd2) { plan.shift_count = 1; plan.shift_count_is_cl = false; }
      else plan.shift_count_is_cl = true;
      return planned(cpu, cursor, &plan);
    }
    case 0xc2: case 0xc3: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      uint64_t discard = op == 0xc2 ? qa_x64_cursor_read_unsigned(cursor, 2) : 0;
      qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_RETURN);
      plan.discard = discard;
      return planned(cpu, cursor, &plan);
    }
    case 0xc6: case 0xc7: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      qa_x64_modrm decoded = qa_x64_cursor_decode_modrm(cursor, op == 0xc6 ? 8 : width);
      if (decoded.extension != 0) unsupported(cpu, "MOV/transactional group /%d", decoded.extension);
      uint64_t value = qa_x64_cursor_immediate(cursor, decoded.rm.width);
      qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_MOVE);
      plan.destination = decoded.rm;
      plan.source = source_immediate(value);
      return planned(cpu, cursor, &plan);
    }
    case 0xc9: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      reg_write(cpu, QA_REG_RSP, 64, reg_read(cpu, QA_REG_RBP, 64, false), false);
      reg_write(cpu, QA_REG_RBP, qa_x64_cursor_stack_width(cursor),
                pop_value(cpu, qa_x64_cursor_stack_width(cursor)), false);
      return qa_x64_advance;
    }
    case 0xcc: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      qa_x64_flow trap = {QA_X64_TRAP, 0, 3};
      return trap;
    }
    case 0xcd: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      int vector = qa_x64_cursor_read_byte(cursor);
      unsupported(cpu, "Software interrupt 0x%x", vector);
    }
    case 0xe0: case 0xe1: case 0xe2: case 0xe3: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      int64_t displacement = qa_x64_cursor_read_signed(cursor, 1);
      int address_bits = qa_x64_cursor_address_bits(cursor);
      uint64_t count = reg_read(cpu, QA_REG_RCX, address_bits, false);
      if (op != 0xe3) {
        count = as_uint_n(address_bits, count - 1u);
        reg_write(cpu, QA_REG_RCX, address_bits, count, false);
      }
      bool taken = op == 0xe3 ? count == 0
                              : count != 0 && (op == 0xe2 || flag_get(cpu, QA_FLAG_ZERO) == (op == 0xe1));
      return taken ? branch_to(cpu, qa_x64_cursor_next_ip(cursor) + (uint64_t)displacement) : qa_x64_advance;
    }
    case 0xe8: case 0xe9: case 0xeb: {
      lock_check(cpu, cursor->lock, NULL, false, false);
      int64_t displacement = qa_x64_cursor_read_signed(cursor, op == 0xeb ? 1 : 4);
      if (op != 0xe8) {
        qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_BRANCH);
        plan.has_condition = false;
        plan.displacement = displacement;
        return planned(cpu, cursor, &plan);
      }
      qa_x64_plan_operation plan = plan_op(QA_X64_PLAN_CALL);
      plan.source = source_immediate(qa_x64_cursor_next_ip(cursor) + (uint64_t)displacement);
      return planned(cpu, cursor, &plan);
    }
    case 0xf4:
      lock_check(cpu, cursor->lock, NULL, false, false);
      pfault(cpu, 13, "HLT is privileged in the user-mode guest");
    case 0xf5:
      lock_check(cpu, cursor->lock, NULL, false, false);
      flag_set(cpu, QA_FLAG_CARRY, !flag_get(cpu, QA_FLAG_CARRY));
      return qa_x64_advance;
    case 0xf6: case 0xf7: return do_unary(cpu, cursor);
    case 0xf8: lock_check(cpu, cursor->lock, NULL, false, false); flag_set(cpu, QA_FLAG_CARRY, false); return qa_x64_advance;
    case 0xf9: lock_check(cpu, cursor->lock, NULL, false, false); flag_set(cpu, QA_FLAG_CARRY, true); return qa_x64_advance;
    case 0xfc: lock_check(cpu, cursor->lock, NULL, false, false); flag_set(cpu, QA_FLAG_DIRECTION, false); return qa_x64_advance;
    case 0xfd: lock_check(cpu, cursor->lock, NULL, false, false); flag_set(cpu, QA_FLAG_DIRECTION, true); return qa_x64_advance;
    case 0xfe: case 0xff: return do_group5(cpu, cursor);
    default: unsupported(cpu, "Unsupported x86-64 opcode 0x%x", op);
  }
}

void qa_x64_cpu_init(qa_x64_cpu *cpu, qa_guest_processor_state *state, qa_sparse_guest_memory *memory,
                     bool (*is_host_call)(void *ctx, qa_guest_core_address address), void *is_host_call_ctx,
                     qa_guest_callback_table *callbacks) {
  if (state->architecture != QA_GUEST_X86_64 || memory->base.pointer_bytes != 8)
    qa_range_error("X64Cpu requires x86-64 processor state and 64-bit guest memory");
  if (callbacks != NULL && (callbacks->memory != memory || is_host_call != NULL))
    qa_range_error("X64Cpu callback table must exclusively own this memory's instruction entries");
  cpu->state = state;
  cpu->memory = memory;
  cpu->is_host_call = is_host_call;
  cpu->is_host_call_ctx = is_host_call_ctx;
  cpu->callbacks = callbacks;
}

typedef struct x64_run_state {
  int64_t instructions;
  uint64_t start;
  uint8_t registers[128];
  uint64_t flags;
  qa_x64_decode_cursor cursor;
  bool has_cursor;
  qa_guest_execution_stop stop;
  bool have_stop;
} x64_run_state;

static qa_guest_execution_stop make_stop(qa_guest_execution_stop_kind kind, int64_t instructions) {
  qa_guest_execution_stop stop;
  memset(&stop, 0, sizeof stop);
  stop.kind = kind;
  stop.instructions = instructions;
  return stop;
}
static qa_guest_core_address evidence_address(uint64_t byte_offset) {
  qa_guest_core_address address = {byte_offset};
  return address;
}

qa_guest_execution_stop qa_x64_cpu_run(qa_x64_cpu *cpu, int64_t instruction_budget,
                                       bool has_return_address, qa_guest_core_address return_address) {
  if (instruction_budget < 0 || instruction_budget > QA_MAX_SAFE_INTEGER)
    qa_range_error("Instruction budget must be a nonnegative safe integer");
  x64_run_state state;
  x64_run_state *rs = &state;
  memset(rs, 0, sizeof *rs);
  jmp_buf buf;
  qa_guest_throw throw_ctx;
  qa_guest_throw *tc = &throw_ctx;
  qa_guest_throw_init(tc, &buf);
  qa_guest_throw *previous = cpu->memory->throw_ctx;
  cpu->memory->throw_ctx = tc;
  for (; rs->instructions < instruction_budget; rs->instructions++) {
    rs->start = cpu->state->instruction_pointer;
    if (has_return_address && return_address.byte_offset == rs->start) {
      rs->stop = make_stop(QA_STOP_RETURN, rs->instructions);
      rs->stop.address = evidence_address(rs->start);
      rs->have_stop = true;
      break;
    }
    (void)qa_integer_register_file_checkpoint(&cpu->state->registers, rs->registers, sizeof rs->registers);
    rs->flags = qa_processor_flags_value(&cpu->state->flags);
    rs->has_cursor = false;
    if (setjmp(buf) == 0) {
      (void)qa_x64_canonical_address(tc, rs->start);
      qa_guest_core_address address = evidence_address(rs->start);
      bool host = cpu->callbacks != NULL
                      ? qa_guest_callback_table_enter(cpu->callbacks, address)
                      : cpu->is_host_call != NULL && cpu->is_host_call(cpu->is_host_call_ctx, address);
      if (host) {
        rs->stop = make_stop(QA_STOP_HOST_CALL, rs->instructions);
        rs->stop.address = address;
        rs->have_stop = true;
        break;
      }
      /* Entry observers may have redirected IP: decode from the live IP
       * (TS cursor reset likewise re-reads state.instructionPointer). */
      qa_x64_decode_cursor_init(&rs->cursor, cpu->memory, cpu->state, NULL);
      rs->has_cursor = true;
      qa_x64_flow flow = execute_instruction(cpu, &rs->cursor);
      uint64_t next_ip = qa_x64_cursor_next_ip(&rs->cursor);
      cpu->state->instruction_pointer = flow.kind == QA_X64_BRANCH ? flow.target : next_ip;
      if (flow.kind == QA_X64_HALT) {
        rs->stop = make_stop(QA_STOP_HALT, rs->instructions + 1);
        rs->stop.address = address;
        rs->have_stop = true;
        break;
      }
      if (flow.kind == QA_X64_TRAP) {
        rs->stop = make_stop(QA_STOP_EXCEPTION, rs->instructions + 1);
        rs->stop.exception.kind = QA_EXCEPTION_PROCESSOR;
        rs->stop.exception.vector = flow.vector;
        rs->stop.exception.has_error_code = false;
        rs->stop.exception.instruction = address;
        rs->stop.exception.detail = "Software breakpoint";
        rs->have_stop = true;
        break;
      }
    } else {
      qa_integer_register_file_restore(&cpu->state->registers, rs->registers, sizeof rs->registers);
      qa_processor_flags_set_value(&cpu->state->flags, rs->flags);
      cpu->state->instruction_pointer = rs->start;
      qa_guest_core_address address = evidence_address(rs->start);
      if (tc->kind == QA_GUEST_THROW_MEMORY) {
        rs->stop = make_stop(QA_STOP_EXCEPTION, rs->instructions);
        rs->stop.exception.kind = QA_EXCEPTION_MEMORY;
        rs->stop.exception.access = tc->memory.access == QA_FAULT_ACCESS_EXECUTE ? QA_ACCESS_EXECUTE
                                    : tc->memory.access == QA_FAULT_ACCESS_WRITE ? QA_ACCESS_WRITE : QA_ACCESS_READ;
        rs->stop.exception.address = evidence_address(tc->memory.address);
        rs->stop.exception.byte_length = (int64_t)tc->memory.byte_length;
        (void)snprintf(cpu->stop_detail, sizeof cpu->stop_detail, "%.511s", tc->memory.message);
        rs->stop.exception.detail = cpu->stop_detail;
      } else if (tc->kind == QA_GUEST_THROW_PROCESSOR) {
        rs->stop = make_stop(QA_STOP_EXCEPTION, rs->instructions);
        rs->stop.exception.kind = QA_EXCEPTION_PROCESSOR;
        rs->stop.exception.vector = tc->vector;
        rs->stop.exception.has_error_code = tc->vector == 13;
        rs->stop.exception.error_code = 0;
        rs->stop.exception.instruction = address;
        (void)snprintf(cpu->stop_detail, sizeof cpu->stop_detail, "%s", tc->detail);
        rs->stop.exception.detail = cpu->stop_detail;
      } else if (tc->kind == QA_GUEST_THROW_UNSUPPORTED) {
        rs->stop = make_stop(QA_STOP_UNSUPPORTED, rs->instructions);
        rs->stop.instruction_address = address;
        if (rs->has_cursor) {
          rs->stop.instruction_bytes_len = qa_x64_cursor_bytes(&rs->cursor, cpu->stop_bytes);
          rs->stop.instruction_bytes = cpu->stop_bytes;
          (void)snprintf(cpu->stop_mnemonic, sizeof cpu->stop_mnemonic,
                         "opcode %x", qa_x64_cursor_opcode(&rs->cursor));
        } else {
          (void)snprintf(cpu->stop_mnemonic, sizeof cpu->stop_mnemonic, "opcode unknown");
        }
        rs->stop.mnemonic = cpu->stop_mnemonic;
        (void)snprintf(cpu->stop_detail, sizeof cpu->stop_detail, "%s", tc->detail);
        rs->stop.detail = cpu->stop_detail;
      } else {
        cpu->memory->throw_ctx = previous;
        qa_range_error("Guest throw channel is inconsistent");
      }
      rs->have_stop = true;
      break;
    }
  }
  cpu->memory->throw_ctx = previous;
  if (rs->have_stop) return rs->stop;
  if (has_return_address && return_address.byte_offset == cpu->state->instruction_pointer) {
    qa_guest_execution_stop stop = make_stop(QA_STOP_RETURN, instruction_budget);
    stop.address = evidence_address(cpu->state->instruction_pointer);
    return stop;
  }
  return make_stop(QA_STOP_BUDGET, instruction_budget);
}

