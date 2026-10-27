/* Port of src/guest/x86/arithmetic.ts. GPL-2.0-or-later. */
#include "qa/guest/x86/arithmetic.h"

#include <string.h>

#include "qa/foundation/support.h"

static const char *const k_alu_names[9] = {"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp", "test"};
static const char *const k_shift_names[7] = {"rol", "ror", "rcl", "rcr", "shl", "shr", "sar"};
const char *qa_alu_operation_name(qa_alu_operation op) { return k_alu_names[(int)op]; }
const char *qa_shift_operation_name(qa_shift_operation op) { return k_shift_names[(int)op]; }
bool qa_alu_operation_parse(const char *name, qa_alu_operation *out) {
  for (int i = 0; i < 9; i++) if (strcmp(name, k_alu_names[i]) == 0) { *out = (qa_alu_operation)i; return true; }
  return false;
}
bool qa_shift_operation_parse(const char *name, qa_shift_operation *out) {
  for (int i = 0; i < 7; i++) if (strcmp(name, k_shift_names[i]) == 0) { *out = (qa_shift_operation)i; return true; }
  return false;
}
static uint64_t as_uint_n(int width, uint64_t v) { return width >= 64 ? v : v & ((1ull << width) - 1ull); }
static int64_t as_int_n(int width, uint64_t v) {
  v = as_uint_n(width, v);
  if (width < 64 && ((v >> (width - 1)) & 1u)) return (int64_t)(v - (1ull << width));
  return (int64_t)v;
}
static int parity_flag(unsigned byte) {
  byte ^= byte >> 4;
  return (int)(((0x9669u >> (byte & 15u)) & 1u) << 2);
}
static int result_bits(int width, uint64_t result) {
  return (result == 0 ? 0x40 : 0) | (as_int_n(width, result) < 0 ? 0x80 : 0) | parity_flag((unsigned)(result & 255u));
}
void qa_x86_result_flags(int width, uint64_t value, qa_processor_flags *flags) {
  uint64_t cur = qa_processor_flags_value(flags);
  qa_processor_flags_set_value(flags, (cur & ~0xc4ull) | (uint64_t)result_bits(width, as_uint_n(width, value)));
}
/* TS splits 64-bit (bigint) and narrow (number) paths; both are the same
 * unsigned arithmetic here (narrow operands cannot overflow uint64_t). */
uint64_t qa_x86_alu(qa_alu_operation op, int width, uint64_t left, uint64_t right, qa_processor_flags *flags) {
  uint64_t a = as_uint_n(width, left), b = as_uint_n(width, right);
  uint64_t sign_bit = width == 64 ? 0x8000000000000000ull : 1ull << (width - 1);
  uint64_t result = 0, mask = 0x8d5u;
  int bits = 0;
  switch (op) {
    case QA_ALU_ADD: case QA_ALU_ADC: {
      uint64_t incoming = op == QA_ALU_ADC && qa_processor_flags_get(flags, QA_FLAG_CARRY) ? 1u : 0u;
      uint64_t s = b + incoming;
      uint64_t full = a + s;
      bool carry = width == 64 ? (s < b || full < a) : full >= (1ull << width);
      if (carry) bits |= 1;
      if ((~(a ^ b) & (a ^ full) & sign_bit) != 0) bits |= 0x800;
      if ((a & 15u) + (b & 15u) + incoming > 15u) bits |= 0x10;
      result = as_uint_n(width, full);
      break;
    }
    case QA_ALU_SUB: case QA_ALU_CMP: case QA_ALU_SBB: {
      uint64_t incoming = op == QA_ALU_SBB && qa_processor_flags_get(flags, QA_FLAG_CARRY) ? 1u : 0u;
      uint64_t s = b + incoming;
      uint64_t full = a - s;
      if (s < b || a < s) bits |= 1;
      if (((a ^ b) & (a ^ full) & sign_bit) != 0) bits |= 0x800;
      if ((a & 15u) < (b & 15u) + incoming) bits |= 0x10;
      result = as_uint_n(width, full);
      break;
    }
    case QA_ALU_AND: case QA_ALU_TEST: result = a & b; mask = 0x8c5u; break;
    case QA_ALU_OR: result = a | b; mask = 0x8c5u; break;
    case QA_ALU_XOR: result = a ^ b; mask = 0x8c5u; break;
  }
  uint64_t cur = qa_processor_flags_value(flags);
  qa_processor_flags_set_value(flags, (cur & ~mask) | (uint64_t)(bits | result_bits(width, result)));
  return result;
}
uint64_t qa_x86_shift(qa_shift_operation op, int width, uint64_t value, int count, qa_processor_flags *flags) {
  int masked = count & (width == 64 ? 63 : 31);
  bool rotate = op == QA_SHIFT_ROL || op == QA_SHIFT_ROR || op == QA_SHIFT_RCL || op == QA_SHIFT_RCR;
  bool through = op == QA_SHIFT_RCL || op == QA_SHIFT_RCR;
  int effective = rotate ? masked % (width + (through ? 1 : 0)) : masked;
  uint64_t result = as_uint_n(width, value);
  uint64_t sign_bit = width == 64 ? 0x8000000000000000ull : 1ull << (width - 1);
  if (effective == 0) {
    if (masked != 0 && op == QA_SHIFT_ROL) qa_processor_flags_set(flags, QA_FLAG_CARRY, (result & 1u) != 0);
    if (masked != 0 && op == QA_SHIFT_ROR) qa_processor_flags_set(flags, QA_FLAG_CARRY, (result & sign_bit) != 0);
    return result;
  }
  bool original_sign = (result & sign_bit) != 0;
  bool carry = qa_processor_flags_get(flags, QA_FLAG_CARRY);
  switch (op) {
    case QA_SHIFT_ROL:
      result = as_uint_n(width, (result << effective) | (result >> (width - effective)));
      carry = (result & 1u) != 0;
      break;
    case QA_SHIFT_ROR:
      result = as_uint_n(width, (result >> effective) | (result << (width - effective)));
      carry = (result & sign_bit) != 0;
      break;
    case QA_SHIFT_RCL: case QA_SHIFT_RCR: {
      if (width < 64) {
        uint64_t ext = (result << 1) | (carry ? 1u : 0u);
        int w1 = width + 1;
        uint64_t m = (1ull << w1) - 1ull;
        uint64_t rot = op == QA_SHIFT_RCL
          ? ((ext << effective) | (ext >> (w1 - effective))) & m
          : ((ext >> effective) | (ext << (w1 - effective))) & m;
        result = rot >> 1;
        carry = (rot & 1u) != 0;
      } else {
        /* 65-bit extended value: top bit plus low 64 bits, rotated stepwise. */
        bool top = (result >> 63) != 0;
        uint64_t rest = (result << 1) | (carry ? 1u : 0u);
        for (int i = 0; i < effective; i++) {
          if (op == QA_SHIFT_RCL) {
            bool new_top = (rest >> 63) != 0;
            rest = (rest << 1) | (top ? 1u : 0u);
            top = new_top;
          } else {
            bool old_top = top;
            top = (rest & 1u) != 0;
            rest = (rest >> 1) | (old_top ? (1ull << 63) : 0u);
          }
        }
        result = (rest >> 1) | (top ? (1ull << 63) : 0u);
        carry = (rest & 1u) != 0;
      }
      break;
    }
    case QA_SHIFT_SHL:
      if (effective < width) carry = ((result >> (width - effective)) & 1u) != 0;
      result = as_uint_n(width, effective >= 64 ? 0 : result << effective);
      break;
    case QA_SHIFT_SHR:
      if (effective < width) carry = ((result >> (effective - 1)) & 1u) != 0;
      result = effective >= 64 ? 0 : result >> effective;
      break;
    case QA_SHIFT_SAR:
      carry = effective >= width ? original_sign : ((result >> (effective - 1)) & 1u) != 0;
      if (effective >= width) result = original_sign ? as_uint_n(width, UINT64_MAX) : 0;
      else result = as_uint_n(width, (uint64_t)(as_int_n(width, result) >> effective));
      break;
  }
  if (rotate || effective < width || op == QA_SHIFT_SAR) qa_processor_flags_set(flags, QA_FLAG_CARRY, carry);
  if (!rotate) qa_x86_result_flags(width, result, flags);
  if (masked == 1) {
    switch (op) {
      case QA_SHIFT_ROL: case QA_SHIFT_RCL: case QA_SHIFT_SHL:
        qa_processor_flags_set(flags, QA_FLAG_OVERFLOW, ((result & sign_bit) != 0) != carry); break;
      case QA_SHIFT_ROR: case QA_SHIFT_RCR:
        qa_processor_flags_set(flags, QA_FLAG_OVERFLOW, ((result & sign_bit) != 0) != ((result & (sign_bit >> 1)) != 0)); break;
      case QA_SHIFT_SHR: qa_processor_flags_set(flags, QA_FLAG_OVERFLOW, original_sign); break;
      case QA_SHIFT_SAR: qa_processor_flags_set(flags, QA_FLAG_OVERFLOW, false); break;
    }
  }
  return result;
}
/* 64x64 -> 128 multiply via 32-bit halves (no __int128: -Wpedantic). */
static void mul_u64_wide(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo) {
  uint64_t a0 = a & 0xffffffffu, a1 = a >> 32, b0 = b & 0xffffffffu, b1 = b >> 32;
  uint64_t p0 = a0 * b0, p1 = a0 * b1, p2 = a1 * b0, p3 = a1 * b1;
  uint64_t mid = (p0 >> 32) + (p1 & 0xffffffffu) + (p2 & 0xffffffffu);
  *lo = (p0 & 0xffffffffu) | (mid << 32);
  *hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
}
uint64_t qa_x86_signed_multiply(int width, uint64_t left, uint64_t right, qa_processor_flags *flags) {
  int64_t sl = as_int_n(width, left), sr = as_int_n(width, right);
  uint64_t ua = sl < 0 ? (uint64_t)(-(sl + 1)) + 1u : (uint64_t)sl;
  uint64_t ub = sr < 0 ? (uint64_t)(-(sr + 1)) + 1u : (uint64_t)sr;
  uint64_t hi, lo;
  mul_u64_wide(ua, ub, &hi, &lo);
  bool negative = (sl < 0) != (sr < 0);
  uint64_t limit = negative ? (width == 64 ? (1ull << 63) : (1ull << (width - 1)))
                            : (width == 64 ? (1ull << 63) - 1ull : (1ull << (width - 1)) - 1ull);
  bool overflow = hi != 0 || lo > limit;
  uint64_t result = as_uint_n(width, negative ? 0ull - lo : lo);
  qa_processor_flags_set(flags, QA_FLAG_CARRY, overflow);
  qa_processor_flags_set(flags, QA_FLAG_OVERFLOW, overflow);
  return result;
}
bool qa_x86_condition(int code, qa_processor_flags *flags) {
  switch (code) {
    case 0: return qa_processor_flags_get(flags, QA_FLAG_OVERFLOW);
    case 1: return !qa_processor_flags_get(flags, QA_FLAG_OVERFLOW);
    case 2: return qa_processor_flags_get(flags, QA_FLAG_CARRY);
    case 3: return !qa_processor_flags_get(flags, QA_FLAG_CARRY);
    case 4: return qa_processor_flags_get(flags, QA_FLAG_ZERO);
    case 5: return !qa_processor_flags_get(flags, QA_FLAG_ZERO);
    case 6: return qa_processor_flags_get(flags, QA_FLAG_CARRY) || qa_processor_flags_get(flags, QA_FLAG_ZERO);
    case 7: return !qa_processor_flags_get(flags, QA_FLAG_CARRY) && !qa_processor_flags_get(flags, QA_FLAG_ZERO);
    case 8: return qa_processor_flags_get(flags, QA_FLAG_SIGN);
    case 9: return !qa_processor_flags_get(flags, QA_FLAG_SIGN);
    case 10: return qa_processor_flags_get(flags, QA_FLAG_PARITY);
    case 11: return !qa_processor_flags_get(flags, QA_FLAG_PARITY);
    case 12: return qa_processor_flags_get(flags, QA_FLAG_SIGN) != qa_processor_flags_get(flags, QA_FLAG_OVERFLOW);
    case 13: return qa_processor_flags_get(flags, QA_FLAG_SIGN) == qa_processor_flags_get(flags, QA_FLAG_OVERFLOW);
    case 14: return qa_processor_flags_get(flags, QA_FLAG_ZERO) || qa_processor_flags_get(flags, QA_FLAG_SIGN) != qa_processor_flags_get(flags, QA_FLAG_OVERFLOW);
    case 15: return !qa_processor_flags_get(flags, QA_FLAG_ZERO) && qa_processor_flags_get(flags, QA_FLAG_SIGN) == qa_processor_flags_get(flags, QA_FLAG_OVERFLOW);
    default: qa_range_error("Invalid condition code %d", code);
  }
}
