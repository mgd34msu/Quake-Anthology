/* Port of src/guest/x86/cpu.ts. GPL-2.0-or-later. */
#include "qa/guest/x86/cpu.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/floating_point/sse.h" /* qa_execute_numeric_instruction */
#include "qa/guest/x86/arithmetic.h"

static uint64_t as_uint_n(int width, uint64_t v) {
  return width >= 64 ? v : v & ((1ull << width) - 1ull);
}
static int64_t as_int_n(int width, uint64_t v) {
  v = as_uint_n(width, v);
  if (width < 64 && ((v >> (width - 1)) & 1ull) != 0) return (int64_t)v - (int64_t)(1ull << width);
  return (int64_t)v;
}
static qa_guest_throw *throw_of(qa_i386_cpu *cpu) { return cpu->memory->throw_ctx; }
_Noreturn static void pfault(qa_i386_cpu *cpu, int vector, const char *detail) {
  qa_guest_throw_processor(throw_of(cpu), vector, false, 0, "%s", detail);
}
_Noreturn static void pfault_ec(qa_i386_cpu *cpu, int vector, const char *detail) {
  qa_guest_throw_processor(throw_of(cpu), vector, true, 0, "%s", detail);
}
_Noreturn static void unsupported(qa_i386_cpu *cpu, const char *format, ...) {
  char detail[512];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(detail, sizeof detail, format, args);
  va_end(args);
  qa_guest_throw_unsupported(throw_of(cpu), "%s", detail);
}

static uint64_t reg_read(qa_i386_cpu *cpu, qa_guest_register reg, int width, bool high_byte) {
  return qa_integer_register_file_read(&cpu->state->registers, reg, width, high_byte);
}
static void reg_write(qa_i386_cpu *cpu, qa_guest_register reg, int width, uint64_t value, bool high_byte) {
  qa_integer_register_file_write(&cpu->state->registers, reg, width, value, high_byte);
}
static bool flag_get(qa_i386_cpu *cpu, qa_guest_flag flag) {
  return qa_processor_flags_get(&cpu->state->flags, flag);
}
static void flag_set(qa_i386_cpu *cpu, qa_guest_flag flag, bool value) {
  qa_processor_flags_set(&cpu->state->flags, flag, value);
}

static qa_alu_operation arithmetic_operation(qa_i386_cpu *cpu, int index) {
  switch (index) {
    case 0: return QA_ALU_ADD;
    case 1: return QA_ALU_OR;
    case 2: return QA_ALU_ADC;
    case 3: return QA_ALU_SBB;
    case 4: return QA_ALU_AND;
    case 5: return QA_ALU_SUB;
    case 6: return QA_ALU_XOR;
    case 7: return QA_ALU_CMP;
    default: qa_range_error("Invalid ALU encoding %d", index);
  }
  (void)cpu;
}
static qa_shift_operation shift_operation_of(int index) {
  switch (index) {
    case 0: return QA_SHIFT_ROL;
    case 1: return QA_SHIFT_ROR;
    case 2: return QA_SHIFT_RCL;
    case 3: return QA_SHIFT_RCR;
    case 4: case 6: return QA_SHIFT_SHL;
    case 5: return QA_SHIFT_SHR;
    case 7: return QA_SHIFT_SAR;
    default: qa_range_error("Invalid shift encoding %d", index);
  }
}

static qa_x86_operand stack_operand(uint64_t offset) {
  qa_x86_operand operand;
  memset(&operand, 0, sizeof operand);
  operand.is_memory = true;
  operand.offset = offset & 0xffffffffull;
  operand.segment = QA_SEG_SS;
  operand.stack_pointer_base = true;
  return operand;
}
static void push_value(qa_i386_cpu *cpu, qa_x86_decoder *d, int width, uint64_t value) {
  uint64_t pointer = as_uint_n(32, reg_read(cpu, QA_REG_RSP, 32, false) - (uint64_t)(width / 8));
  qa_x86_operand target = stack_operand(pointer);
  qa_x86_decoder_write(d, &target, width, value);
  reg_write(cpu, QA_REG_RSP, 32, pointer, false);
}
static uint64_t pop_value(qa_i386_cpu *cpu, qa_x86_decoder *d, int width) {
  uint64_t pointer = reg_read(cpu, QA_REG_RSP, 32, false);
  qa_x86_operand source = stack_operand(pointer);
  uint64_t value = qa_x86_decoder_read(d, &source, width);
  reg_write(cpu, QA_REG_RSP, 32, pointer + (uint64_t)(width / 8), false);
  return value;
}
static void lock_check(qa_i386_cpu *cpu, qa_x86_decoder *d, const qa_x86_operand *operand, bool allowed) {
  if (d->lock && (!allowed || !operand->is_memory))
    pfault(cpu, 6, "LOCK requires a supported read-modify-write memory destination");
}
static void do_alu(qa_i386_cpu *cpu, qa_x86_decoder *d, qa_alu_operation operation, int width,
                   const qa_x86_operand *destination, uint64_t right) {
  bool write = operation != QA_ALU_CMP && operation != QA_ALU_TEST;
  lock_check(cpu, d, destination, write);
  uint64_t left = qa_x86_decoder_read(d, destination, width);
  if (write) qa_x86_decoder_check_write(d, destination, width);
  uint64_t value = qa_x86_alu(operation, width, left, right, &cpu->state->flags);
  if (write) qa_x86_decoder_write(d, destination, width, value);
}
static void do_incdec(qa_i386_cpu *cpu, qa_x86_decoder *d, const qa_x86_operand *operand, int width, bool decrement) {
  bool carry = flag_get(cpu, QA_FLAG_CARRY);
  do_alu(cpu, d, decrement ? QA_ALU_SUB : QA_ALU_ADD, width, operand, 1);
  flag_set(cpu, QA_FLAG_CARRY, carry);
}

static qa_guest_segment_index segment_of(qa_i386_cpu *cpu, int index) {
  switch (index) {
    case 0: return QA_SEG_ES;
    case 1: return QA_SEG_CS;
    case 2: return QA_SEG_SS;
    case 3: return QA_SEG_DS;
    case 4: return QA_SEG_FS;
    case 5: return QA_SEG_GS;
    default: pfault(cpu, 6, "Invalid segment register encoding");
  }
}
static void set_selector(qa_i386_cpu *cpu, qa_guest_segment_index segment, uint32_t selector) {
  if (segment == QA_SEG_CS) pfault(cpu, 6, "MOV cannot load CS");
  if (selector != cpu->state->segments[segment].selector)
    unsupported(cpu, "Segment descriptor resolution for %s=0x%x is unavailable",
                qa_guest_segment_name(segment), selector);
}
static void pop_segment(qa_i386_cpu *cpu, qa_x86_decoder *d, qa_guest_segment_index segment) {
  set_selector(cpu, segment, (uint32_t)(pop_value(cpu, d, d->operand_bits) & 0xffffull));
}
static void do_enter(qa_i386_cpu *cpu, qa_x86_decoder *d) {
  uint64_t allocation = qa_x86_decoder_immediate(d, 16);
  int nesting = qa_x86_decoder_byte(d) & 31;
  int width = d->operand_bits;
  uint64_t prior_frame = reg_read(cpu, QA_REG_RBP, width, false);
  uint64_t values[33];
  int value_count = 0;
  values[value_count++] = prior_frame;
  uint64_t frame = as_uint_n(32, reg_read(cpu, QA_REG_RSP, 32, false) - (uint64_t)(width / 8));
  for (int level = 1; level < nesting; level++) {
    qa_x86_operand source = stack_operand(prior_frame - (uint64_t)(level * width / 8));
    values[value_count++] = qa_x86_decoder_read(d, &source, width);
  }
  if (nesting != 0) values[value_count++] = frame;
  uint64_t final_stack = as_uint_n(32, reg_read(cpu, QA_REG_RSP, 32, false) - (uint64_t)(value_count * width / 8));
  qa_x86_operand operand = stack_operand(final_stack);
  qa_sparse_check(cpu->memory, qa_x86_decoder_address(d, &operand, (size_t)(value_count * width / 8), QA_ACCESS_WRITE),
                  (size_t)(value_count * width / 8), QA_ACCESS_WRITE);
  for (int i = 0; i < value_count; i++) push_value(cpu, d, width, values[i]);
  reg_write(cpu, QA_REG_RBP, width, frame, false);
  reg_write(cpu, QA_REG_RSP, 32, final_stack - allocation, false);
}
static qa_x86_operand mem_operand(uint64_t offset, qa_guest_segment_index segment) {
  qa_x86_operand operand;
  memset(&operand, 0, sizeof operand);
  operand.is_memory = true;
  operand.offset = offset;
  operand.segment = segment;
  return operand;
}
static void do_string(qa_i386_cpu *cpu, qa_x86_decoder *d) {
  int op = d->opcode;
  int width = (op & 1) == 0 ? 8 : d->operand_bits;
  bool repeated = d->repeat != QA_X86_REPEAT_NONE;
  uint64_t count = reg_read(cpu, QA_REG_RCX, d->address_bits, false);
  if (repeated && count == 0) return;
  qa_x86_operand source = mem_operand(reg_read(cpu, QA_REG_RSI, d->address_bits, false),
                                      d->has_segment ? d->segment : QA_SEG_DS);
  qa_x86_operand destination = mem_operand(reg_read(cpu, QA_REG_RDI, d->address_bits, false), QA_SEG_ES);
  int64_t increment = (int64_t)(width / 8) * (flag_get(cpu, QA_FLAG_DIRECTION) ? -1 : 1);
  int category = op & 0xfe;
  if (category == 0xa4) qa_x86_decoder_write(d, &destination, width, qa_x86_decoder_read(d, &source, width));
  else if (category == 0xa6)
    (void)qa_x86_alu(QA_ALU_CMP, width, qa_x86_decoder_read(d, &source, width),
                     qa_x86_decoder_read(d, &destination, width), &cpu->state->flags);
  else if (category == 0xaa) qa_x86_decoder_write(d, &destination, width, reg_read(cpu, QA_REG_RAX, width, false));
  else if (category == 0xac) reg_write(cpu, QA_REG_RAX, width, qa_x86_decoder_read(d, &source, width), false);
  else
    (void)qa_x86_alu(QA_ALU_CMP, width, reg_read(cpu, QA_REG_RAX, width, false),
                     qa_x86_decoder_read(d, &destination, width), &cpu->state->flags);
  if (category == 0xa4 || category == 0xa6 || category == 0xac)
    reg_write(cpu, QA_REG_RSI, d->address_bits, source.offset + (uint64_t)increment, false);
  if (category != 0xac)
    reg_write(cpu, QA_REG_RDI, d->address_bits, destination.offset + (uint64_t)increment, false);
  if (repeated) {
    reg_write(cpu, QA_REG_RCX, d->address_bits, count - 1u, false);
    bool compare = category == 0xa6 || category == 0xae;
    if (count != 1 && (!compare || flag_get(cpu, QA_FLAG_ZERO) == (d->repeat == QA_X86_REPEAT_F3)))
      d->cursor = d->start;
  }
}

static void do_unary(qa_i386_cpu *cpu, qa_x86_decoder *d, int width) {
  qa_x86_modrm decoded = qa_x86_decoder_modrm(d, width);
  lock_check(cpu, d, &decoded.operand, decoded.group == 2 || decoded.group == 3);
  uint64_t operand = qa_x86_decoder_read(d, &decoded.operand, width);
  if (decoded.group == 0 || decoded.group == 1) {
    /* TS re-reads via #alu (which re-checks LOCK with write=false). */
    do_alu(cpu, d, QA_ALU_TEST, width, &decoded.operand, qa_x86_decoder_immediate(d, width));
    return;
  }
  if (decoded.group == 2) { qa_x86_decoder_write(d, &decoded.operand, width, ~operand); return; }
  if (decoded.group == 3) {
    qa_x86_decoder_check_write(d, &decoded.operand, width);
    qa_x86_decoder_write(d, &decoded.operand, width, qa_x86_alu(QA_ALU_SUB, width, 0, operand, &cpu->state->flags));
    return;
  }
  uint64_t low = reg_read(cpu, QA_REG_RAX, width, false);
  if (decoded.group == 4 || decoded.group == 5) {
    bool is_signed = decoded.group == 5;
    int64_t product_signed = as_int_n(width, low) * as_int_n(width, operand);
    uint64_t product = is_signed ? (uint64_t)product_signed : low * operand;
    bool overflow = is_signed ? as_int_n(width, (uint64_t)product_signed) != product_signed
                              : (product >> width) != 0;
    flag_set(cpu, QA_FLAG_CARRY, overflow);
    flag_set(cpu, QA_FLAG_OVERFLOW, overflow);
    if (width == 8) reg_write(cpu, QA_REG_RAX, 16, product, false);
    else {
      reg_write(cpu, QA_REG_RAX, width, product, false);
      reg_write(cpu, QA_REG_RDX, width, is_signed ? (uint64_t)(product_signed >> width) : product >> width, false);
    }
    return;
  }
  uint64_t raw_dividend = width == 8 ? reg_read(cpu, QA_REG_RAX, 16, false)
                                     : (reg_read(cpu, QA_REG_RDX, width, false) << width) | low;
  bool is_signed = decoded.group == 7;
  uint64_t quotient, remainder;
  if (is_signed) {
    int64_t divisor = as_int_n(width, operand);
    int64_t dividend = as_int_n(width * 2, raw_dividend);
    if (divisor == 0) pfault(cpu, 0, "Integer division by zero");
    if (dividend == INT64_MIN && divisor == -1) pfault(cpu, 0, "Integer division quotient overflow");
    int64_t q = dividend / divisor;
    int64_t r = dividend % divisor;
    if (as_int_n(width, (uint64_t)q) != q) pfault(cpu, 0, "Integer division quotient overflow");
    quotient = (uint64_t)q;
    remainder = (uint64_t)r;
  } else {
    if (operand == 0) pfault(cpu, 0, "Integer division by zero");
    quotient = raw_dividend / operand;
    remainder = raw_dividend % operand;
    if (as_uint_n(width, quotient) != quotient) pfault(cpu, 0, "Integer division quotient overflow");
  }
  reg_write(cpu, QA_REG_RAX, width, quotient, false);
  reg_write(cpu, width == 8 ? QA_REG_RAX : QA_REG_RDX, width, remainder, width == 8);
}

static void do_bit(qa_i386_cpu *cpu, qa_x86_decoder *d, int op) {
  int width = d->operand_bits;
  qa_x86_modrm decoded = qa_x86_decoder_modrm(d, width);
  int operation = op == 0xba ? decoded.group : op == 0xa3 ? 4 : op == 0xab ? 5 : op == 0xb3 ? 6 : 7;
  if (operation < 4) pfault(cpu, 6, "Invalid bit-operation group selector");
  lock_check(cpu, d, &decoded.operand, operation != 4);
  int64_t index = op == 0xba ? (int64_t)qa_x86_decoder_byte(d)
                             : as_int_n(width, qa_x86_decoder_read(d, &decoded.register_operand, width));
  uint64_t bit = as_uint_n(width == 16 ? 4 : 5, (uint64_t)index);
  qa_x86_operand operand = decoded.operand;
  if (operand.is_memory && op != 0xba) {
    /* TS: ((index - bit) / width) * (width/8); index-bit is a multiple
     * of width's power-of-two bit count (16 or 32), so this is exact. */
    int64_t adjust = ((index - (int64_t)bit) / width) * (width / 8);
    operand.offset = as_uint_n(d->address_bits, (uint64_t)((int64_t)operand.offset + adjust));
  }
  uint64_t value = qa_x86_decoder_read(d, &operand, width);
  uint64_t mask = 1ull << bit;
  if (operation != 4) qa_x86_decoder_check_write(d, &operand, width);
  flag_set(cpu, QA_FLAG_CARRY, (value & mask) != 0);
  if (operation != 4)
    qa_x86_decoder_write(d, &operand, width, operation == 5 ? value | mask : operation == 6 ? value & ~mask : value ^ mask);
}

static void do_double_shift(qa_i386_cpu *cpu, qa_x86_decoder *d, int op) {
  int width = d->operand_bits;
  qa_x86_modrm decoded = qa_x86_decoder_modrm(d, width);
  int count = (op & 1) == 0 ? qa_x86_decoder_byte(d) & 31 : (int)reg_read(cpu, QA_REG_RCX, 8, false) & 31;
  if (count > width) unsupported(cpu, "SHLD/SHRD count exceeding operand width has undefined behavior");
  uint64_t value = qa_x86_decoder_read(d, &decoded.operand, width);
  uint64_t source = qa_x86_decoder_read(d, &decoded.register_operand, width);
  if (count == 0) return;
  qa_x86_decoder_check_write(d, &decoded.operand, width);
  bool left = op < 0xac;
  uint64_t result = as_uint_n(width, left ? (value << count) | (source >> (width - count))
                                          : (value >> count) | (source << (width - count)));
  bool carry = ((left ? value >> (width - count) : value >> (count - 1)) & 1ull) != 0;
  bool old_overflow = flag_get(cpu, QA_FLAG_OVERFLOW);
  bool old_auxiliary = flag_get(cpu, QA_FLAG_AUXILIARY_CARRY);
  (void)qa_x86_alu(QA_ALU_OR, width, result, 0, &cpu->state->flags);
  flag_set(cpu, QA_FLAG_CARRY, carry);
  flag_set(cpu, QA_FLAG_AUXILIARY_CARRY, old_auxiliary);
  flag_set(cpu, QA_FLAG_OVERFLOW,
           count == 1 ? ((value ^ result) & (1ull << (width - 1))) != 0 : old_overflow);
  qa_x86_decoder_write(d, &decoded.operand, width, result);
}

static void do_cmpxchg8(qa_i386_cpu *cpu, qa_x86_decoder *d) {
  qa_x86_modrm decoded = qa_x86_decoder_modrm(d, 32);
  if (decoded.group != 1 || !decoded.operand.is_memory)
    unsupported(cpu, "Only memory CMPXCHG8B is supported in 0F C7 group");
  qa_guest_address address = qa_x86_decoder_address(d, &decoded.operand, 8, QA_ACCESS_READ);
  uint64_t value = qa_sparse_read_u64(cpu->memory, address);
  qa_sparse_check(cpu->memory, address, 8, QA_ACCESS_WRITE);
  uint64_t expected = (reg_read(cpu, QA_REG_RDX, 32, false) << 32) | reg_read(cpu, QA_REG_RAX, 32, false);
  bool equal = value == expected;
  qa_sparse_write_u64(cpu->memory, address,
                      equal ? (reg_read(cpu, QA_REG_RCX, 32, false) << 32) | reg_read(cpu, QA_REG_RBX, 32, false) : value);
  flag_set(cpu, QA_FLAG_ZERO, equal);
  if (!equal) {
    reg_write(cpu, QA_REG_RAX, 32, value, false);
    reg_write(cpu, QA_REG_RDX, 32, value >> 32, false);
  }
}

static void do_floating(qa_i386_cpu *cpu, qa_x86_decoder *d, int secondary_opcode, bool has_secondary) {
  bool has_decoded = !(d->opcode == 0x9b || (has_secondary && secondary_opcode == 0x77));
  qa_x86_modrm decoded;
  memset(&decoded, 0, sizeof decoded);
  if (has_decoded) decoded = qa_x86_decoder_modrm(d, d->operand_bits);
  qa_numeric_instruction instruction;
  memset(&instruction, 0, sizeof instruction);
  instruction.opcode = d->opcode;
  instruction.has_secondary_opcode = has_secondary;
  instruction.secondary_opcode = secondary_opcode;
  instruction.has_modrm = has_decoded;
  instruction.modrm = has_decoded ? decoded.byte : 0;
  instruction.register_index = has_decoded ? decoded.group : 0;
  instruction.prefix = d->repeat == QA_X86_REPEAT_F2 ? QA_PREFIX_F2
                       : d->repeat == QA_X86_REPEAT_F3 ? QA_PREFIX_F3
                       : d->operand_bits == 16 ? QA_PREFIX_66 : QA_PREFIX_NONE;
  instruction.operand_bits = d->operand_bits;
  if (has_decoded) {
    instruction.has_operand = true;
    if (decoded.operand.is_memory) {
      instruction.operand.kind = QA_OPERAND_MEMORY;
      instruction.operand.address = qa_x86_decoder_address(d, &decoded.operand, 1, QA_ACCESS_READ);
    } else {
      instruction.operand.kind = QA_OPERAND_REGISTER;
      instruction.operand.index = decoded.operand.index;
    }
  }
  bool needs_immediate = has_secondary &&
      (secondary_opcode == 0x70 || secondary_opcode == 0x71 || secondary_opcode == 0x72 ||
       secondary_opcode == 0x73 || secondary_opcode == 0xc2 || secondary_opcode == 0xc4 ||
       secondary_opcode == 0xc5 || secondary_opcode == 0xc6);
  instruction.has_immediate = needs_immediate;
  instruction.immediate = needs_immediate ? qa_x86_decoder_byte(d) : 0;
  qa_numeric_context context;
  qa_numeric_context_init(&context, cpu->state, &cpu->memory->base, &instruction);
  qa_numeric_result result;
  if (cpu->numeric != NULL) cpu->numeric(cpu->numeric_ctx, &result, &context);
  else qa_execute_numeric_instruction(&result, &context);
  if (result.kind == QA_NUMERIC_UNSUPPORTED) unsupported(cpu, "%s", result.detail);
  if (result.kind == QA_NUMERIC_EXCEPTION) pfault(cpu, result.vector, result.detail);
}

static void do_extended(qa_i386_cpu *cpu, qa_x86_decoder *d) {
  int op = qa_x86_decoder_byte(d);
  int width = d->operand_bits;
  if (d->lock && !(op == 0xab || op == 0xb0 || op == 0xb1 || op == 0xb3 || op == 0xba ||
                   op == 0xbb || op == 0xc0 || op == 0xc1 || op == 0xc7))
    pfault(cpu, 6, "LOCK is invalid for this 0F opcode");
  if (op >= 0x80 && op <= 0x8f) {
    int64_t relative = qa_x86_decoder_signed(d, width);
    if (qa_x86_condition(op & 15, &cpu->state->flags))
      d->cursor = as_uint_n(width, (uint64_t)((int64_t)d->cursor + relative));
    return;
  }
  if (op >= 0x90 && op <= 0x9f) {
    qa_x86_modrm decoded = qa_x86_decoder_modrm(d, 8);
    qa_x86_decoder_write(d, &decoded.operand, 8, qa_x86_condition(op & 15, &cpu->state->flags) ? 1 : 0);
    return;
  }
  if (op >= 0x40 && op <= 0x4f) {
    qa_x86_modrm decoded = qa_x86_decoder_modrm(d, width);
    uint64_t value = qa_x86_decoder_read(d, &decoded.operand, width);
    if (qa_x86_condition(op & 15, &cpu->state->flags))
      qa_x86_decoder_write(d, &decoded.register_operand, width, value);
    return;
  }
  if (op >= 0xc8 && op <= 0xcf) {
    if (width != 32) unsupported(cpu, "16-bit BSWAP has undefined architectural behavior");
    qa_x86_operand operand = qa_x86_register_operand(op & 7, 32);
    uint64_t value = qa_x86_decoder_read(d, &operand, 32);
    qa_x86_decoder_write(d, &operand, 32,
                         ((value & 255ull) << 24) | ((value & 0xff00ull) << 8) |
                             ((value >> 8) & 0xff00ull) | ((value >> 24) & 255ull));
    return;
  }
  switch (op) {
    case 0x0b: pfault(cpu, 6, "UD2 invalid opcode");
    case 0x1f: {
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, width);
      if (decoded.group != 0) pfault(cpu, 6, "Invalid multi-byte NOP selector");
      return;
    }
    case 0xa0: push_value(cpu, d, width, cpu->state->segments[QA_SEG_FS].selector); return;
    case 0xa1: pop_segment(cpu, d, QA_SEG_FS); return;
    case 0xa8: push_value(cpu, d, width, cpu->state->segments[QA_SEG_GS].selector); return;
    case 0xa9: pop_segment(cpu, d, QA_SEG_GS); return;
    case 0xaf: {
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, width);
      qa_x86_decoder_write(d, &decoded.register_operand, width,
                           qa_x86_signed_multiply(width, qa_x86_decoder_read(d, &decoded.register_operand, width),
                                                  qa_x86_decoder_read(d, &decoded.operand, width), &cpu->state->flags));
      return;
    }
    case 0xb6: case 0xb7: case 0xbe: case 0xbf: {
      int bits = (op & 1) == 0 ? 8 : 16;
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, bits);
      uint64_t value = qa_x86_decoder_read(d, &decoded.operand, bits);
      qa_x86_operand destination = qa_x86_register_operand(decoded.group, width);
      qa_x86_decoder_write(d, &destination, width, op >= 0xbe ? (uint64_t)as_int_n(bits, value) : value);
      return;
    }
    case 0xbc: case 0xbd: {
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, width);
      uint64_t value = qa_x86_decoder_read(d, &decoded.operand, width);
      flag_set(cpu, QA_FLAG_ZERO, value == 0);
      if (value != 0) {
        int bit = op == 0xbc ? 0 : width - 1;
        while ((value & (1ull << bit)) == 0) bit += op == 0xbc ? 1 : -1;
        qa_x86_decoder_write(d, &decoded.register_operand, width, (uint64_t)bit);
      }
      return;
    }
    case 0xb0: case 0xb1: case 0xc0: case 0xc1: {
      int bits = (op & 1) == 0 ? 8 : width;
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, bits);
      lock_check(cpu, d, &decoded.operand, true);
      uint64_t destination = qa_x86_decoder_read(d, &decoded.operand, bits);
      uint64_t source = qa_x86_decoder_read(d, &decoded.register_operand, bits);
      qa_x86_decoder_check_write(d, &decoded.operand, bits);
      if (op < 0xc0) {
        uint64_t accumulator = reg_read(cpu, QA_REG_RAX, bits, false);
        (void)qa_x86_alu(QA_ALU_CMP, bits, accumulator, destination, &cpu->state->flags);
        if (accumulator == destination) qa_x86_decoder_write(d, &decoded.operand, bits, source);
        else {
          qa_x86_decoder_write(d, &decoded.operand, bits, destination);
          reg_write(cpu, QA_REG_RAX, bits, destination, false);
        }
      } else {
        uint64_t result = qa_x86_alu(QA_ALU_ADD, bits, destination, source, &cpu->state->flags);
        qa_x86_decoder_write(d, &decoded.register_operand, bits, destination);
        qa_x86_decoder_write(d, &decoded.operand, bits, result);
      }
      return;
    }
    case 0xa3: case 0xab: case 0xb3: case 0xbb: case 0xba: do_bit(cpu, d, op); return;
    case 0xa4: case 0xa5: case 0xac: case 0xad: do_double_shift(cpu, d, op); return;
    case 0xc7: do_cmpxchg8(cpu, d); return;
    default:
      if ((op >= 0x10 && op <= 0x17) || (op >= 0x28 && op <= 0x2f) || (op >= 0x50 && op <= 0x7f) ||
          (op >= 0xc2 && op <= 0xc6) || op >= 0xd0 || op == 0xae) {
        do_floating(cpu, d, op, true);
        return;
      }
      unsupported(cpu, "Unsupported i386 opcode 0f %02x at 0x%llx", op, (unsigned long long)d->start);
  }
}

static bool execute_rest(qa_i386_cpu *cpu, qa_x86_decoder *d, int op, int width);
static bool execute_tail(qa_i386_cpu *cpu, qa_x86_decoder *d, int op, int width);
static bool execute(qa_i386_cpu *cpu, qa_x86_decoder *d) {
  int op = d->opcode;
  int width = d->operand_bits;
  if (d->lock && !(op <= 0x3b && (op & 7) <= 3) &&
      !(op == 0x80 || op == 0x81 || op == 0x82 || op == 0x83 || op == 0x86 || op == 0x87 ||
        op == 0xf6 || op == 0xf7 || op == 0xfe || op == 0xff || op == 0x0f))
    qa_guest_throw_processor(throw_of(cpu), 6, false, 0, "LOCK is invalid for opcode 0x%x", op);
  if (op <= 0x3d && (op & 7) <= 5) {
    qa_alu_operation operation = arithmetic_operation(cpu, op >> 3);
    int form = op & 7;
    int bits = (form & 1) == 0 ? 8 : width;
    if (form <= 3) {
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, bits);
      const qa_x86_operand *destination = form < 2 ? &decoded.operand : &decoded.register_operand;
      const qa_x86_operand *source = form < 2 ? &decoded.register_operand : &decoded.operand;
      do_alu(cpu, d, operation, bits, destination, qa_x86_decoder_read(d, source, bits));
    } else {
      qa_x86_operand accumulator = qa_x86_register_operand(0, bits);
      do_alu(cpu, d, operation, bits, &accumulator, qa_x86_decoder_immediate(d, bits));
    }
    return false;
  }
  if (op >= 0x40 && op <= 0x4f) {
    qa_x86_operand operand = qa_x86_register_operand(op & 7, width);
    do_incdec(cpu, d, &operand, width, op >= 0x48);
    return false;
  }
  if (op >= 0x50 && op <= 0x57) {
    qa_x86_operand operand = qa_x86_register_operand(op & 7, width);
    push_value(cpu, d, width, qa_x86_decoder_read(d, &operand, width));
    return false;
  }
  if (op >= 0x58 && op <= 0x5f) {
    qa_x86_operand operand = qa_x86_register_operand(op & 7, width);
    qa_x86_decoder_write(d, &operand, width, pop_value(cpu, d, width));
    return false;
  }
  if (op >= 0x70 && op <= 0x7f) {
    int64_t relative = qa_x86_decoder_signed(d, 8);
    if (qa_x86_condition(op & 15, &cpu->state->flags))
      d->cursor = as_uint_n(width, (uint64_t)((int64_t)d->cursor + relative));
    return false;
  }
  if (op >= 0x91 && op <= 0x97) {
    qa_x86_operand operand = qa_x86_register_operand(op & 7, width);
    qa_x86_operand accumulator = qa_x86_register_operand(0, width);
    uint64_t left = qa_x86_decoder_read(d, &accumulator, width);
    qa_x86_decoder_write(d, &accumulator, width, qa_x86_decoder_read(d, &operand, width));
    qa_x86_decoder_write(d, &operand, width, left);
    return false;
  }
  if (op >= 0xb0 && op <= 0xbf) {
    int bits = op < 0xb8 ? 8 : width;
    qa_x86_operand operand = qa_x86_register_operand(op & 7, bits);
    qa_x86_decoder_write(d, &operand, bits, qa_x86_decoder_immediate(d, bits));
    return false;
  }
  if (op >= 0xd8 && op <= 0xdf) { do_floating(cpu, d, 0, false); return false; }
  switch (op) {
    case 0x0f: do_extended(cpu, d); return false;
    case 0x06: push_value(cpu, d, width, cpu->state->segments[QA_SEG_ES].selector); return false;
    case 0x0e: push_value(cpu, d, width, cpu->state->segments[QA_SEG_CS].selector); return false;
    case 0x16: push_value(cpu, d, width, cpu->state->segments[QA_SEG_SS].selector); return false;
    case 0x1e: push_value(cpu, d, width, cpu->state->segments[QA_SEG_DS].selector); return false;
    case 0x07: pop_segment(cpu, d, QA_SEG_ES); return false;
    case 0x17: pop_segment(cpu, d, QA_SEG_SS); return false;
    case 0x1f: pop_segment(cpu, d, QA_SEG_DS); return false;
    case 0x60: {
      uint64_t initial_sp = reg_read(cpu, QA_REG_RSP, width, false);
      uint64_t stack = reg_read(cpu, QA_REG_RSP, 32, false);
      qa_x86_operand target = stack_operand(stack - (uint64_t)width);
      qa_sparse_check(cpu->memory, qa_x86_decoder_address(d, &target, (size_t)width, QA_ACCESS_WRITE),
                      (size_t)width, QA_ACCESS_WRITE);
      for (int index = 0; index < 8; index++) {
        qa_x86_operand operand = qa_x86_register_operand(index, width);
        push_value(cpu, d, width, index == 4 ? initial_sp : qa_x86_decoder_read(d, &operand, width));
      }
      return false;
    }
    case 0x61:
      for (int index = 7; index >= 0; index--) {
        uint64_t value = pop_value(cpu, d, width);
        if (index != 4) {
          qa_x86_operand operand = qa_x86_register_operand(index, width);
          qa_x86_decoder_write(d, &operand, width, value);
        }
      }
      return false;
    case 0x68: push_value(cpu, d, width, qa_x86_decoder_immediate(d, width)); return false;
    case 0x6a: push_value(cpu, d, width, (uint64_t)qa_x86_decoder_signed(d, 8)); return false;
    case 0x69: case 0x6b: {
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, width);
      int64_t immediate = op == 0x6b ? qa_x86_decoder_signed(d, 8) : qa_x86_decoder_signed(d, width);
      qa_x86_decoder_write(d, &decoded.register_operand, width,
                           qa_x86_signed_multiply(width, qa_x86_decoder_read(d, &decoded.operand, width),
                                                  (uint64_t)immediate, &cpu->state->flags));
      return false;
    }
    default: break;
  }
  return execute_rest(cpu, d, op, width);
}

static bool execute_rest(qa_i386_cpu *cpu, qa_x86_decoder *d, int op, int width) {
  switch (op) {
    case 0x80: case 0x81: case 0x82: case 0x83: {
      int bits = op == 0x80 || op == 0x82 ? 8 : width;
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, bits);
      uint64_t right = op == 0x83 ? (uint64_t)qa_x86_decoder_signed(d, 8) : qa_x86_decoder_immediate(d, bits);
      do_alu(cpu, d, arithmetic_operation(cpu, decoded.group), bits, &decoded.operand, right);
      return false;
    }
    case 0x84: case 0x85: {
      int bits = op == 0x84 ? 8 : width;
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, bits);
      do_alu(cpu, d, QA_ALU_TEST, bits, &decoded.operand, qa_x86_decoder_read(d, &decoded.register_operand, bits));
      return false;
    }
    case 0x86: case 0x87: {
      int bits = op == 0x86 ? 8 : width;
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, bits);
      uint64_t old = qa_x86_decoder_read(d, &decoded.operand, bits);
      uint64_t replacement = qa_x86_decoder_read(d, &decoded.register_operand, bits);
      lock_check(cpu, d, &decoded.operand, true);
      qa_x86_decoder_write(d, &decoded.operand, bits, replacement);
      qa_x86_decoder_write(d, &decoded.register_operand, bits, old);
      return false;
    }
    case 0x88: case 0x89: case 0x8a: case 0x8b: {
      int bits = (op & 1) == 0 ? 8 : width;
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, bits);
      const qa_x86_operand *destination = op < 0x8a ? &decoded.operand : &decoded.register_operand;
      const qa_x86_operand *source = op < 0x8a ? &decoded.register_operand : &decoded.operand;
      qa_x86_decoder_write(d, destination, bits, qa_x86_decoder_read(d, source, bits));
      return false;
    }
    case 0x8c: case 0x8e: {
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, 16);
      qa_guest_segment_index segment = segment_of(cpu, decoded.group);
      if (op == 0x8c)
        qa_x86_decoder_write(d, &decoded.operand, !decoded.operand.is_memory ? width : 16,
                             cpu->state->segments[segment].selector);
      else set_selector(cpu, segment, (uint32_t)qa_x86_decoder_read(d, &decoded.operand, 16));
      return false;
    }
    case 0x8d: {
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, width);
      if (!decoded.operand.is_memory) pfault(cpu, 6, "LEA requires a memory addressing form");
      qa_x86_decoder_write(d, &decoded.register_operand, width, decoded.operand.offset);
      return false;
    }
    case 0x8f: {
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, width);
      if (decoded.group != 0) pfault(cpu, 6, "Invalid POP group selector");
      uint64_t value = pop_value(cpu, d, width);
      qa_x86_operand target = decoded.operand;
      if (target.is_memory && target.stack_pointer_base)
        target.offset = as_uint_n(d->address_bits, target.offset + (uint64_t)(width / 8));
      qa_x86_decoder_write(d, &target, width, value);
      return false;
    }
    case 0x90: return false;
    case 0x98:
      reg_write(cpu, QA_REG_RAX, width,
                (uint64_t)as_int_n(width == 16 ? 8 : 16, reg_read(cpu, QA_REG_RAX, width, false)), false);
      return false;
    case 0x99:
      reg_write(cpu, QA_REG_RDX, width,
                as_int_n(width, reg_read(cpu, QA_REG_RAX, width, false)) < 0 ? UINT64_MAX : 0, false);
      return false;
    case 0x9b: do_floating(cpu, d, 0, false); return false;
    case 0x9c:
      push_value(cpu, d, width, (qa_processor_flags_value(&cpu->state->flags) & ~0x30000ull) | 2ull);
      return false;
    case 0x9d: {
      uint64_t value = pop_value(cpu, d, width);
      uint64_t mask = width == 16 ? 0x4dd5ull : 0x244dd5ull;
      qa_processor_flags_set_value(&cpu->state->flags,
          ((qa_processor_flags_value(&cpu->state->flags) & ~mask) | (value & mask) | 2ull) & ~0x10000ull);
      return false;
    }
    case 0x9e: {
      uint64_t value = reg_read(cpu, QA_REG_RAX, 8, true);
      qa_processor_flags_set_value(&cpu->state->flags,
          (qa_processor_flags_value(&cpu->state->flags) & ~0xd5ull) | (value & 0xd5ull));
      return false;
    }
    case 0x9f:
      reg_write(cpu, QA_REG_RAX, 8, (qa_processor_flags_value(&cpu->state->flags) & 0xd5ull) | 2ull, true);
      return false;
    case 0xa0: case 0xa1: case 0xa2: case 0xa3: {
      int bits = (op & 1) == 0 ? 8 : width;
      qa_x86_operand operand = mem_operand(qa_x86_decoder_immediate(d, d->address_bits),
                                           d->has_segment ? d->segment : QA_SEG_DS);
      qa_x86_operand accumulator = qa_x86_register_operand(0, bits);
      const qa_x86_operand *destination = op < 0xa2 ? &accumulator : &operand;
      const qa_x86_operand *source = op < 0xa2 ? &operand : &accumulator;
      qa_x86_decoder_write(d, destination, bits, qa_x86_decoder_read(d, source, bits));
      return false;
    }
    case 0xa4: case 0xa5: case 0xa6: case 0xa7: case 0xaa: case 0xab:
    case 0xac: case 0xad: case 0xae: case 0xaf:
      do_string(cpu, d);
      return false;
    case 0xa8: case 0xa9: {
      int bits = op == 0xa8 ? 8 : width;
      qa_x86_operand accumulator = qa_x86_register_operand(0, bits);
      do_alu(cpu, d, QA_ALU_TEST, bits, &accumulator, qa_x86_decoder_immediate(d, bits));
      return false;
    }
    default: break;
  }
  return execute_tail(cpu, d, op, width);
}

static bool execute_tail(qa_i386_cpu *cpu, qa_x86_decoder *d, int op, int width) {
  switch (op) {
    case 0xc0: case 0xc1: case 0xd0: case 0xd1: case 0xd2: case 0xd3: {
      int bits = (op & 1) == 0 ? 8 : width;
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, bits);
      int count = op < 0xd0 ? qa_x86_decoder_byte(d) : op < 0xd2 ? 1 : (int)reg_read(cpu, QA_REG_RCX, 8, false);
      uint64_t value = qa_x86_decoder_read(d, &decoded.operand, bits);
      qa_x86_decoder_check_write(d, &decoded.operand, bits);
      qa_x86_decoder_write(d, &decoded.operand, bits,
                           qa_x86_shift(shift_operation_of(decoded.group), bits, value, count, &cpu->state->flags));
      return false;
    }
    case 0xc2: case 0xc3: {
      uint64_t adjustment = op == 0xc2 ? qa_x86_decoder_immediate(d, 16) : 0;
      d->cursor = as_uint_n(width, pop_value(cpu, d, width));
      reg_write(cpu, QA_REG_RSP, 32, reg_read(cpu, QA_REG_RSP, 32, false) + adjustment, false);
      return false;
    }
    case 0xc6: case 0xc7: {
      int bits = op == 0xc6 ? 8 : width;
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, bits);
      if (decoded.group != 0) pfault(cpu, 6, "Invalid MOV immediate group selector");
      qa_x86_decoder_write(d, &decoded.operand, bits, qa_x86_decoder_immediate(d, bits));
      return false;
    }
    case 0xc8: do_enter(cpu, d); return false;
    case 0xc9:
      reg_write(cpu, QA_REG_RSP, 32, reg_read(cpu, QA_REG_RBP, 32, false), false);
      reg_write(cpu, QA_REG_RBP, width, pop_value(cpu, d, width), false);
      return false;
    case 0xcc: pfault(cpu, 3, "INT3 breakpoint");
    case 0xcd: {
      int vector = qa_x86_decoder_byte(d);
      char detail[128];
      (void)snprintf(detail, sizeof detail, "INT %d requires a guest interrupt service", vector);
      pfault_ec(cpu, 13, detail);
    }
    case 0xce:
      if (flag_get(cpu, QA_FLAG_OVERFLOW)) pfault(cpu, 4, "INTO overflow trap");
      return false;
    case 0xd4: {
      uint64_t base = (uint64_t)qa_x86_decoder_byte(d);
      if (base == 0) pfault(cpu, 0, "AAM divide by zero");
      uint64_t value = reg_read(cpu, QA_REG_RAX, 8, false);
      reg_write(cpu, QA_REG_RAX, 16, ((value / base) << 8) | (value % base), false);
      qa_x86_result_flags(8, value % base, &cpu->state->flags);
      return false;
    }
    case 0xd5: {
      uint64_t base = (uint64_t)qa_x86_decoder_byte(d);
      uint64_t result = as_uint_n(8, reg_read(cpu, QA_REG_RAX, 8, false) +
                                         reg_read(cpu, QA_REG_RAX, 8, true) * base);
      reg_write(cpu, QA_REG_RAX, 16, result, false);
      qa_x86_result_flags(8, result, &cpu->state->flags);
      return false;
    }
    case 0xd7: {
      uint64_t offset = as_uint_n(d->address_bits,
          reg_read(cpu, QA_REG_RBX, d->address_bits, false) + reg_read(cpu, QA_REG_RAX, 8, false));
      qa_x86_operand operand = mem_operand(offset, d->has_segment ? d->segment : QA_SEG_DS);
      reg_write(cpu, QA_REG_RAX, 8, qa_x86_decoder_read(d, &operand, 8), false);
      return false;
    }
    case 0xe0: case 0xe1: case 0xe2: case 0xe3: {
      int64_t displacement = qa_x86_decoder_signed(d, 8);
      uint64_t count = reg_read(cpu, QA_REG_RCX, d->address_bits, false);
      if (op != 0xe3) {
        count = as_uint_n(d->address_bits, count - 1u);
        reg_write(cpu, QA_REG_RCX, d->address_bits, count, false);
      }
      bool take = op == 0xe3 ? count == 0
                             : count != 0 && (op == 0xe2 || flag_get(cpu, QA_FLAG_ZERO) == (op == 0xe1));
      if (take) d->cursor = as_uint_n(width, (uint64_t)((int64_t)d->cursor + displacement));
      return false;
    }
    case 0xe8: {
      int64_t displacement = qa_x86_decoder_signed(d, width);
      uint64_t next = d->cursor;
      push_value(cpu, d, width, next);
      d->cursor = as_uint_n(width, (uint64_t)((int64_t)next + displacement));
      return false;
    }
    case 0xe9: case 0xeb: {
      int64_t displacement = qa_x86_decoder_signed(d, op == 0xeb ? 8 : width);
      d->cursor = as_uint_n(width, (uint64_t)((int64_t)d->cursor + displacement));
      return false;
    }
    case 0xf4: return true;
    case 0xf5: flag_set(cpu, QA_FLAG_CARRY, !flag_get(cpu, QA_FLAG_CARRY)); return false;
    case 0xf6: case 0xf7: do_unary(cpu, d, op == 0xf6 ? 8 : width); return false;
    case 0xf8: flag_set(cpu, QA_FLAG_CARRY, false); return false;
    case 0xf9: flag_set(cpu, QA_FLAG_CARRY, true); return false;
    case 0xfa: case 0xfb: pfault_ec(cpu, 13, "CLI/STI requires privileged guest execution");
    case 0xfc: flag_set(cpu, QA_FLAG_DIRECTION, false); return false;
    case 0xfd: flag_set(cpu, QA_FLAG_DIRECTION, true); return false;
    case 0xfe: case 0xff: {
      int bits = op == 0xfe ? 8 : width;
      qa_x86_modrm decoded = qa_x86_decoder_modrm(d, bits);
      if (decoded.group <= 1) do_incdec(cpu, d, &decoded.operand, bits, decoded.group == 1);
      else {
        lock_check(cpu, d, &decoded.operand, false);
        if (op == 0xfe) pfault(cpu, 6, "Invalid FE group selector");
        uint64_t target = qa_x86_decoder_read(d, &decoded.operand, width);
        if (decoded.group == 2) { push_value(cpu, d, width, d->cursor); d->cursor = target; }
        else if (decoded.group == 4) d->cursor = target;
        else if (decoded.group == 6) push_value(cpu, d, width, target);
        else unsupported(cpu, "FF group /%d far transfer is unsupported", decoded.group);
      }
      return false;
    }
    default:
      unsupported(cpu, "Unsupported i386 opcode 0x%02x at 0x%llx", op, (unsigned long long)d->start);
  }
}

void qa_i386_cpu_init(qa_i386_cpu *cpu, qa_guest_processor_state *state, qa_sparse_guest_memory *memory,
                      bool (*host_call)(void *ctx, qa_guest_address address), void *host_call_ctx,
                      void (*numeric)(void *ctx, qa_numeric_result *out, qa_numeric_context *context), void *numeric_ctx) {
  if (state->architecture != QA_GUEST_I386 || memory->base.pointer_bytes != 4)
    qa_range_error("I386Cpu requires i386 state and a 32-bit guest address space");
  cpu->state = state;
  cpu->memory = memory;
  cpu->host_call = host_call;
  cpu->host_call_ctx = host_call_ctx;
  cpu->numeric = numeric;
  cpu->numeric_ctx = numeric_ctx;
}

typedef struct run_state {
  int64_t instructions;
  uint64_t original_ip;
  uint8_t registers[64];
  uint64_t flags;
  qa_x86_decoder decoder;
  bool has_decoder;
  qa_guest_address address;
  qa_guest_execution_stop stop;
  bool have_stop;
} run_state;

static qa_guest_execution_stop make_stop(qa_guest_execution_stop_kind kind, int64_t instructions) {
  qa_guest_execution_stop stop;
  memset(&stop, 0, sizeof stop);
  stop.kind = kind;
  stop.instructions = instructions;
  return stop;
}

qa_guest_execution_stop qa_i386_cpu_run(qa_i386_cpu *cpu, int64_t instruction_budget,
                                        bool has_return_address, qa_guest_address return_address) {
  if (instruction_budget < 0 || instruction_budget > QA_MAX_SAFE_INTEGER)
    qa_range_error("Instruction budget must be a nonnegative safe integer");
  /* TS also checks the return address's addressSpace; C addresses
   * carry no space token (registers.h), so that check is dropped. */
  run_state state;
  run_state *rs = &state;
  memset(rs, 0, sizeof *rs);
  jmp_buf buf;
  qa_guest_throw throw_ctx;
  qa_guest_throw *tc = &throw_ctx;
  qa_guest_throw_init(tc, &buf);
  qa_guest_throw *previous = cpu->memory->throw_ctx;
  cpu->memory->throw_ctx = tc;
  while (rs->instructions < instruction_budget) {
    rs->original_ip = cpu->state->instruction_pointer;
    (void)qa_integer_register_file_checkpoint(&cpu->state->registers, rs->registers, sizeof rs->registers);
    rs->flags = qa_processor_flags_value(&cpu->state->flags);
    rs->has_decoder = false;
    if (setjmp(buf) == 0) {
      rs->address = qa_x86_guest_address(cpu->memory,
          as_uint_n(32, cpu->state->segments[QA_SEG_CS].base + rs->original_ip), QA_ACCESS_EXECUTE, 1);
      if (has_return_address && rs->address.byte_offset == return_address.byte_offset) {
        rs->stop = make_stop(QA_STOP_RETURN, rs->instructions);
        rs->stop.address = rs->address;
        rs->have_stop = true;
        break;
      }
      if (cpu->host_call != NULL && cpu->host_call(cpu->host_call_ctx, rs->address)) {
        rs->stop = make_stop(QA_STOP_HOST_CALL, rs->instructions);
        rs->stop.address = rs->address;
        rs->have_stop = true;
        break;
      }
      qa_x86_decoder_init(&rs->decoder, cpu->state, cpu->memory);
      rs->has_decoder = true;
      bool halt = execute(cpu, &rs->decoder);
      cpu->state->instruction_pointer = rs->decoder.cursor;
      rs->instructions++;
      if (halt) {
        rs->stop = make_stop(QA_STOP_HALT, rs->instructions);
        rs->stop.address = rs->address;
        rs->have_stop = true;
        break;
      }
    } else {
      qa_integer_register_file_restore(&cpu->state->registers, rs->registers, sizeof rs->registers);
      qa_processor_flags_set_value(&cpu->state->flags, rs->flags);
      cpu->state->instruction_pointer = rs->original_ip;
      uint64_t instruction_offset = as_uint_n(32, cpu->state->segments[QA_SEG_CS].base + rs->original_ip);
      if (tc->kind == QA_GUEST_THROW_MEMORY) {
        rs->stop = make_stop(QA_STOP_EXCEPTION, rs->instructions);
        rs->stop.exception.kind = QA_EXCEPTION_MEMORY;
        rs->stop.exception.access = tc->memory.access == QA_FAULT_ACCESS_EXECUTE ? QA_ACCESS_EXECUTE
                                    : tc->memory.access == QA_FAULT_ACCESS_WRITE ? QA_ACCESS_WRITE : QA_ACCESS_READ;
        rs->stop.exception.address.byte_offset = tc->memory.address;
        rs->stop.exception.byte_length = (int64_t)tc->memory.byte_length;
        (void)snprintf(cpu->stop_detail, sizeof cpu->stop_detail, "%.511s", tc->memory.message);
        rs->stop.exception.detail = cpu->stop_detail;
      } else if (tc->kind == QA_GUEST_THROW_PROCESSOR) {
        if ((tc->vector == 3 || tc->vector == 4) && rs->has_decoder) {
          cpu->state->instruction_pointer = rs->decoder.cursor;
          rs->instructions++;
        }
        rs->stop = make_stop(QA_STOP_EXCEPTION, rs->instructions);
        rs->stop.exception.kind = QA_EXCEPTION_PROCESSOR;
        rs->stop.exception.vector = tc->vector;
        rs->stop.exception.has_error_code = tc->has_error_code;
        rs->stop.exception.error_code = (int64_t)tc->error_code;
        rs->stop.exception.instruction.byte_offset = instruction_offset;
        (void)snprintf(cpu->stop_detail, sizeof cpu->stop_detail, "%s", tc->detail);
        rs->stop.exception.detail = cpu->stop_detail;
      } else if (tc->kind == QA_GUEST_THROW_UNSUPPORTED) {
        rs->stop = make_stop(QA_STOP_UNSUPPORTED, rs->instructions);
        rs->stop.instruction_address.byte_offset = instruction_offset;
        if (rs->has_decoder) {
          rs->stop.instruction_bytes_len = rs->decoder.byte_count;
          memcpy(cpu->stop_bytes, rs->decoder.bytes, rs->decoder.byte_count);
          rs->stop.instruction_bytes = cpu->stop_bytes;
        }
        rs->stop.mnemonic = "unsupported";
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
  return make_stop(QA_STOP_BUDGET, rs->instructions);
}


