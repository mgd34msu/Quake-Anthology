/* Port of src/guest/floating-point/x87.ts. GPL-2.0-or-later. */
#include "qa/guest/floating_point/x87.h"

#include <stdio.h>
#include <string.h>

#include "qa/guest/floating_point/trigonometric.h"
#include "qa/guest/floating_point/x87_extended.h"

static int top_of(const qa_guest_x87_state *s) { return (int)((s->status_word >> 11) & 7u); }
static int physical(const qa_guest_x87_state *s, int index) { return (top_of(s) + index) & 7; }
static int tag_of(const qa_guest_x87_state *s, int slot) { return (int)((s->tag_word >> (slot * 2)) & 3u); }
static void set_tag(qa_guest_x87_state *s, int slot, int value) {
  s->tag_word = (s->tag_word & ~(3u << (slot * 2))) | ((uint32_t)value << (slot * 2));
}
static void set_top(qa_guest_x87_state *s, int value) {
  s->status_word = (s->status_word & ~0x3800u) | (((uint32_t)value & 7u) << 11);
}
static void raise_flags(qa_numeric_context *ctx, qa_guest_x87_state *s, int flags, bool rounded_up, bool register_wrapped) {
  s->status_word = (s->status_word & ~0x200u) | (rounded_up ? 0x200u : 0u) | (uint32_t)flags;
  int unmasked = flags & ~(int)s->control_word & 63;
  if (unmasked != 0) {
    s->status_word |= 0x8080u;
    if ((unmasked & ~(QA_FP_PRECISION | (register_wrapped ? QA_FP_OVERFLOW | QA_FP_UNDERFLOW : 0))) != 0)
      qa_numeric_throw_deferred(ctx);
  }
}
static void stack_fault(qa_numeric_context *ctx, qa_guest_x87_state *s, bool overflowed) {
  raise_flags(ctx, s, QA_FP_INVALID | 64, overflowed, false);
}
static void read_register_ctx(qa_numeric_context *ctx, qa_binary_value *out, qa_guest_x87_state *s, int index) {
  int slot = physical(s, index);
  if (tag_of(s, slot) == 3) { stack_fault(ctx, s, false); *out = qa_fp_indefinite(); return; }
  qa_bigint bits; qa_bigint_init(&bits); qa_fp_read_bits(&bits, s->registers + slot * 10, 10);
  *out = qa_fp_decode_binary(&bits, 80);
  qa_bigint_free(&bits);
}
static void write_register(qa_guest_x87_state *s, int index, const qa_binary_value *value) {
  int slot = physical(s, index);
  qa_bigint bits; qa_bigint_init(&bits); qa_fp_encode_binary(&bits, value, 80);
  qa_fp_write_bits(s->registers + slot * 10, &bits, 10);
  qa_bigint_free(&bits);
  set_tag(s, slot, value->kind == QA_BINARY_FINITE ? (qa_bigint_is_zero(&value->coefficient) ? 1 : value->denormal ? 2 : 0) : 2);
}
static void raw_register(qa_numeric_context *ctx, qa_bigint *out, qa_guest_x87_state *s, int index) {
  int slot = physical(s, index);
  if (tag_of(s, slot) == 3) {
    stack_fault(ctx, s, false);
    qa_binary_value ind = qa_fp_indefinite();
    qa_fp_encode_binary(out, &ind, 80);
    qa_binary_value_free(&ind);
    return;
  }
  qa_fp_read_bits(out, s->registers + slot * 10, 10);
}
static void write_raw_register(qa_guest_x87_state *s, int index, const qa_bigint *bits) {
  qa_binary_value value = qa_fp_decode_binary(bits, 80);
  write_register(s, index, &value);
  qa_binary_value_free(&value);
  int slot = physical(s, index);
  qa_fp_write_bits(s->registers + slot * 10, bits, 10);
  /* exponent field zero but significand nonzero -> tag special (2) */
  qa_bigint exp_field; qa_bigint_init(&exp_field); qa_bigint_shr(&exp_field, bits, 64);
  uint64_t low = qa_bigint_as_uint_n(bits, 64);
  bool exp_zero = (qa_bigint_as_uint_n(&exp_field, 32) & 0x7fffu) == 0;
  qa_bigint_free(&exp_field);
  if (exp_zero && low != 0) set_tag(s, slot, 2);
}
static void push_raw(qa_numeric_context *ctx, qa_guest_x87_state *s, qa_bigint *bits) {
  int next = (top_of(s) - 1) & 7;
  if (tag_of(s, next) != 3) {
    /* TS: stackFault may throw deferred before the write; otherwise bits
     * become indefinite. With no handler the throw aborts (see binary.h). */
    jmp_buf buf; jmp_buf *saved = ctx->throw_buf;
    if (setjmp(buf) == 0) {
      ctx->throw_buf = &buf;
      stack_fault(ctx, s, true);
      ctx->throw_buf = saved;
    } else {
      ctx->throw_buf = saved;
      ctx->throw_kind = QA_THROW_DEFERRED;
      if (saved != NULL) longjmp(*saved, 1);
    }
    qa_binary_value ind = qa_fp_indefinite();
    qa_fp_encode_binary(bits, &ind, 80);
    qa_binary_value_free(&ind);
  } else s->status_word &= ~0x200u;
  set_top(s, next);
  write_raw_register(s, 0, bits);
}
static void push_value_ctx(qa_numeric_context *ctx, qa_guest_x87_state *s, qa_binary_value *value) {
  int next = (top_of(s) - 1) & 7;
  if (tag_of(s, next) != 3) {
    jmp_buf buf; jmp_buf *saved = ctx->throw_buf;
    if (setjmp(buf) == 0) {
      ctx->throw_buf = &buf;
      stack_fault(ctx, s, true);
      ctx->throw_buf = saved;
    } else {
      ctx->throw_buf = saved;
      if (saved != NULL) longjmp(*saved, 1);
    }
    qa_binary_value_free(value);
    *value = qa_fp_indefinite();
  } else s->status_word &= ~0x200u;
  set_top(s, next);
  write_register(s, 0, value);
}
static void pop_x87(qa_guest_x87_state *s) { set_tag(s, top_of(s), 3); set_top(s, top_of(s) + 1); }
void qa_pop_x87(qa_guest_x87_state *state) { pop_x87(state); }
void qa_initialize_x87(qa_guest_x87_state *state) {
  state->control_word = 0x37f; state->status_word = 0; state->tag_word = 0xffff;
  state->last_opcode = 0; state->instruction_pointer = 0; state->data_pointer = 0;
  state->instruction_selector = 0; state->data_selector = 0;
}
static qa_binary_format result_format(qa_numeric_context *ctx, qa_guest_x87_state *s) {
  int pc = (int)((s->control_word >> 8) & 3u);
  if (pc == 1) qa_numeric_throw_unsupported(ctx, "Reserved x87 precision-control encoding");
  qa_binary_format f = qa_binary80_format;
  f.precision = pc == 0 ? 24 : pc == 2 ? 53 : 64;
  return f;
}
static void commit_result(qa_numeric_context *ctx, qa_guest_x87_state *s, int index, qa_binary_result *result, bool wrapped) {
  raise_flags(ctx, s, result->flags, result->rounded_up, wrapped);
  write_register(s, index, &result->value);
}
/* Run fn under a deferred-swallowing handler for the public helpers. */
static qa_numeric_context temp_ctx(qa_guest_x87_state *state) {
  qa_numeric_context ctx;
  ctx.state = NULL; ctx.memory = NULL;
  memset(&ctx.instruction, 0, sizeof ctx.instruction);
  ctx.throw_buf = NULL; ctx.throw_kind = QA_THROW_NONE; ctx.throw_vector = 0; ctx.throw_detail[0] = 0;
  (void)state;
  return ctx;
}
void qa_read_x87_register(qa_binary_value *out, qa_guest_x87_state *state, int index) {
  qa_numeric_context ctx = temp_ctx(state);
  jmp_buf buf;
  if (setjmp(buf) == 0) { ctx.throw_buf = &buf; read_register_ctx(&ctx, out, state, index); }
  else *out = qa_fp_indefinite(); /* deferred stack fault (see header) */
}
void qa_write_x87_register(qa_guest_x87_state *state, int index, const qa_binary_value *value) {
  write_register(state, index, value);
}
void qa_push_x87(qa_guest_x87_state *state, const qa_binary_value *value) {
  qa_numeric_context ctx = temp_ctx(state);
  qa_binary_value copy = qa_fp_zero(value->sign);
  qa_binary_value_clone(&copy, value);
  jmp_buf buf;
  if (setjmp(buf) == 0) { ctx.throw_buf = &buf; push_value_ctx(&ctx, state, &copy); }
  qa_binary_value_free(&copy);
}
void qa_write_x87_return(qa_guest_x87_state *state, double value, qa_x87_storage storage) {
  uint8_t bytes[8];
  if (storage == QA_X87_FLOAT32) { float f = (float)value; memcpy(bytes, &f, 4); }
  else memcpy(bytes, &value, 8);
  qa_bigint bits; qa_bigint_init(&bits); qa_fp_read_bits(&bits, bytes, storage == QA_X87_FLOAT32 ? 4 : 8);
  qa_binary_value decoded = qa_fp_decode_binary(&bits, storage == QA_X87_FLOAT32 ? 32 : 64);
  qa_bigint_free(&bits);
  qa_binary_result converted;
  qa_fp_convert_binary(&converted, &decoded, qa_binary80_format, qa_fp_rounding_from_bits((int)(state->control_word >> 10)));
  qa_binary_value_free(&decoded);
  qa_numeric_context ctx = temp_ctx(state);
  jmp_buf buf;
  if (setjmp(buf) == 0) {
    ctx.throw_buf = &buf;
    raise_flags(&ctx, state, converted.flags, false, false);
    push_value_ctx(&ctx, state, &converted.value);
  }
  qa_binary_value_free(&converted.value);
}
double qa_read_x87_return(qa_guest_x87_state *state, qa_x87_storage storage) {
  int width = storage == QA_X87_FLOAT32 ? 32 : 64;
  qa_binary_value current;
  qa_read_x87_register(&current, state, 0);
  qa_binary_result result;
  qa_fp_convert_binary(&result, &current, qa_fp_format_for(width), qa_fp_rounding_from_bits((int)(state->control_word >> 10)));
  qa_binary_value_free(&current);
  qa_bigint bits; qa_bigint_init(&bits); qa_fp_encode_binary(&bits, &result.value, width);
  uint8_t bytes[8]; qa_fp_write_bits(bytes, &bits, (size_t)(width / 8));
  qa_bigint_free(&bits);
  qa_binary_value_free(&result.value);
  if (width == 32) { float f; memcpy(&f, bytes, 4); return (double)f; }
  double d; memcpy(&d, bytes, 8); return d;
}
static void compare_op(qa_numeric_context *ctx, const qa_binary_value *right, bool unordered_quiet, bool integer_flags) {
  qa_guest_x87_state *state = &ctx->state->x87;
  qa_binary_value left;
  read_register_ctx(ctx, &left, state, 0);
  qa_fp_comparison cmp = qa_fp_compare_binary(&left, right);
  bool invalid_operand = left.kind == QA_BINARY_UNSUPPORTED || right->kind == QA_BINARY_UNSUPPORTED
    || (left.kind == QA_BINARY_NAN && (!unordered_quiet || left.signaling))
    || (right->kind == QA_BINARY_NAN && (!unordered_quiet || right->signaling));
  int flags = invalid_operand ? QA_FP_INVALID
    : (left.kind == QA_BINARY_FINITE && left.denormal) || (right->kind == QA_BINARY_FINITE && right->denormal) ? QA_FP_DENORMAL_OPERAND : 0;
  raise_flags(ctx, state, flags, false, false);
  if (integer_flags) {
    qa_processor_flags_set(&ctx->state->flags, QA_FLAG_CARRY, cmp == QA_FP_LESS || cmp == QA_FP_UNORDERED);
    qa_processor_flags_set(&ctx->state->flags, QA_FLAG_ZERO, cmp == QA_FP_EQUAL || cmp == QA_FP_UNORDERED);
    qa_processor_flags_set(&ctx->state->flags, QA_FLAG_PARITY, cmp == QA_FP_UNORDERED);
    qa_processor_flags_set(&ctx->state->flags, QA_FLAG_OVERFLOW, false);
    qa_processor_flags_set(&ctx->state->flags, QA_FLAG_SIGN, false);
    qa_processor_flags_set(&ctx->state->flags, QA_FLAG_AUXILIARY_CARRY, false);
  } else {
    state->status_word = (state->status_word & ~0x4500u)
      | (cmp == QA_FP_LESS ? 0x100u : cmp == QA_FP_EQUAL ? 0x4000u : cmp == QA_FP_UNORDERED ? 0x4500u : 0u);
  }
  qa_binary_value_free(&left);
}
static void binary_instruction(qa_numeric_context *ctx, int group, qa_binary_value *right, int destination, bool reversed, bool pop) {
  qa_guest_x87_state *state = &ctx->state->x87;
  if (group == 2 || group == 3) {
    compare_op(ctx, right, false, false);
    if (group == 3) pop_x87(state);
    return;
  }
  qa_binary_operation operation = group == 0 ? QA_FP_ADD : group == 1 ? QA_FP_MULTIPLY
    : group == 4 || group == 5 ? QA_FP_SUBTRACT : QA_FP_DIVIDE;
  qa_binary_value left;
  read_register_ctx(ctx, &left, state, destination);
  bool reverse = (group == 5 || group == 7) != reversed;
  qa_binary_format format = result_format(ctx, state);
  qa_fp_rounding mode = qa_fp_rounding_from_bits((int)(state->control_word >> 10));
  qa_binary_result result;
  qa_fp_arithmetic(&result, operation, reverse ? right : &left, reverse ? &left : right, format, mode, QA_FP_NAN_X87);
  bool wrapped = false;
  if ((state->control_word & (QA_FP_OVERFLOW | QA_FP_UNDERFLOW)) != (QA_FP_OVERFLOW | QA_FP_UNDERFLOW)) {
    qa_binary_format unlimited = format;
    unlimited.minimum_exponent = -100000; unlimited.maximum_exponent = 100000;
    qa_binary_result unlim;
    qa_fp_arithmetic(&unlim, operation, reverse ? right : &left, reverse ? &left : right, unlimited, mode, QA_FP_NAN_X87);
    if (unlim.value.kind == QA_BINARY_FINITE && !qa_bigint_is_zero(&unlim.value.coefficient)) {
      int64_t e = (int64_t)qa_bigint_bit_length(&unlim.value.coefficient) - 1 + unlim.value.exponent;
      int exception = e > format.maximum_exponent ? QA_FP_OVERFLOW : e < format.minimum_exponent ? QA_FP_UNDERFLOW : 0;
      if ((exception & ~(int)state->control_word) != 0) {
        qa_binary_value_free(&result.value);
        result.value = qa_fp_zero(unlim.value.sign);
        qa_binary_value_clone(&result.value, &unlim.value);
        result.value.exponent = unlim.value.exponent + (exception == QA_FP_OVERFLOW ? -24576 : 24576);
        result.flags = unlim.flags | exception;
        result.rounded_up = unlim.rounded_up;
        wrapped = true;
      }
    }
    qa_binary_value_free(&unlim.value);
  }
  commit_result(ctx, state, destination, &result, wrapped);
  qa_binary_value_free(&result.value);
  qa_binary_value_free(&left);
  if (pop) pop_x87(state);
}
static void put16(uint8_t *p, size_t off, uint32_t v) { p[off] = (uint8_t)v; p[off + 1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, size_t off, uint32_t v) { put16(p, off, v & 0xffffu); put16(p, off + 2, v >> 16); }
static uint32_t get16(const uint8_t *p, size_t off) { return (uint32_t)p[off] | ((uint32_t)p[off + 1] << 8); }
static uint32_t get32(const uint8_t *p, size_t off) { return get16(p, off) | (get16(p, off + 2) << 16); }
static void environment_bytes(const qa_guest_x87_state *s, int width, uint8_t *bytes) {
  bool short_env = width == 16;
  memset(bytes, 0, short_env ? 14 : 28);
  put16(bytes, 0, s->control_word);
  put16(bytes, short_env ? 2 : 4, s->status_word);
  put16(bytes, short_env ? 4 : 8, s->tag_word);
  if (short_env) {
    put16(bytes, 6, (uint32_t)(s->instruction_pointer & 0xffffu)); put16(bytes, 8, s->instruction_selector);
    put16(bytes, 10, (uint32_t)(s->data_pointer & 0xffffu)); put16(bytes, 12, s->data_selector);
  } else {
    put32(bytes, 12, (uint32_t)(s->instruction_pointer & 0xffffffffu)); put16(bytes, 16, s->instruction_selector);
    put16(bytes, 18, s->last_opcode); put32(bytes, 20, (uint32_t)(s->data_pointer & 0xffffffffu)); put16(bytes, 24, s->data_selector);
  }
}
static void restore_environment(qa_guest_x87_state *s, const uint8_t *bytes, int width) {
  bool short_env = width == 16;
  s->control_word = get16(bytes, 0);
  s->status_word = get16(bytes, short_env ? 2 : 4);
  s->tag_word = get16(bytes, short_env ? 4 : 8);
  s->instruction_pointer = short_env ? get16(bytes, 6) : get32(bytes, 12);
  s->instruction_selector = get16(bytes, short_env ? 8 : 16);
  s->data_pointer = short_env ? get16(bytes, 10) : get32(bytes, 20);
  s->data_selector = get16(bytes, short_env ? 12 : 24);
  s->last_opcode = short_env ? 0 : get16(bytes, 18) & 0x7ffu;
  if ((s->status_word & ~s->control_word & 63u) != 0) s->status_word |= 0x8080u;
  else s->status_word &= ~0x8080u;
}
static void mem_copy(qa_numeric_context *ctx, qa_guest_core_address address, uint8_t *dest, size_t len) {
  qa_memory_copy(ctx->memory, address, dest, len);
}
static void execute_inner(qa_numeric_context *ctx) {
  qa_numeric_instruction *ins = &ctx->instruction;
  qa_guest_x87_state *state = &ctx->state->x87;
  int opcode = ins->opcode;
  bool has_modrm = ins->has_modrm;
  int modrm = ins->modrm;
  int group = !has_modrm ? 0 : (modrm >> 3) & 7;
  bool no_wait = (opcode == 0xdb && has_modrm && (modrm == 0xe2 || modrm == 0xe3))
    || (opcode == 0xdf && has_modrm && modrm == 0xe0)
    || ((opcode == 0xd9 || opcode == 0xdd) && ins->has_operand && ins->operand.kind == QA_OPERAND_MEMORY && (group == 6 || group == 7));
  if (!no_wait && (state->status_word & ~state->control_word & 63u) != 0)
    qa_numeric_fault(ctx, 16, "Pending x87 floating-point exception");
  if (opcode == 0x9b) return;
  if (!has_modrm || !ins->has_operand) qa_numeric_throw_unsupported(ctx, "x87 instruction requires ModRM and an operand");
  if (opcode == 0xdb && modrm == 0xe2) { state->status_word &= 0x7f00u; return; }
  if (opcode == 0xdb && modrm == 0xe3) { qa_initialize_x87(state); return; }
  if (opcode == 0xdf && modrm == 0xe0) {
    qa_integer_register_file_write(&ctx->state->registers, QA_REG_RAX, 16, state->status_word, false);
    return;
  }
  bool control_memory = ins->operand.kind == QA_OPERAND_MEMORY && (opcode == 0xd9 || opcode == 0xdd) && group >= 4;
  if (!control_memory) {
    state->last_opcode = (uint32_t)(((opcode & 7) << 8) | modrm);
    state->instruction_pointer = ctx->state->instruction_pointer;
    state->instruction_selector = ctx->state->segments[QA_SEG_CS].selector;
  }
  if (ins->operand.kind == QA_OPERAND_MEMORY) {
    qa_guest_core_address address = ins->operand.address;
    uint8_t bytes[108];
    if ((opcode == 0xd9 || opcode == 0xdd) && (group == 4 || group == 6)) {
      size_t size = ins->operand_bits == 16 ? 14 : 28;
      size_t total = size + (opcode == 0xdd ? 80 : 0);
      if (group == 4) {
        mem_copy(ctx, address, bytes, total);
        restore_environment(state, bytes, ins->operand_bits);
        if (opcode == 0xdd)
          for (int i = 0; i < 8; i++) memcpy(state->registers + physical(state, i) * 10, bytes + size + (size_t)i * 10, 10);
      } else {
        environment_bytes(state, ins->operand_bits, bytes);
        if (opcode == 0xdd)
          for (int i = 0; i < 8; i++) memcpy(bytes + size + (size_t)i * 10, state->registers + physical(state, i) * 10, 10);
        qa_memory_write(ctx->memory, address, bytes, total);
        if (opcode == 0xdd) qa_initialize_x87(state); else state->control_word |= 63u;
      }
      return;
    }
    if (opcode == 0xd9 && group == 5) {
      uint8_t b2[2]; mem_copy(ctx, address, b2, 2);
      state->control_word = get16(b2, 0);
      if ((state->status_word & ~state->control_word & 63u) != 0) state->status_word |= 0x8080u;
      else state->status_word &= ~0x8080u;
      return;
    }
    if ((opcode == 0xd9 || opcode == 0xdd) && group == 7) {
      uint8_t b2[2]; put16(b2, 0, opcode == 0xd9 ? state->control_word : state->status_word);
      qa_memory_write(ctx->memory, address, b2, 2);
      return;
    }
    state->data_pointer = address.byte_offset;
    state->data_selector = ctx->state->segments[QA_SEG_DS].selector;
    if (opcode == 0xd8 || opcode == 0xdc || opcode == 0xda || opcode == 0xde) {
      qa_binary_value right;
      if (opcode == 0xda || opcode == 0xde) {
        size_t n = opcode == 0xda ? 4 : 2;
        mem_copy(ctx, address, bytes, n);
        qa_bigint bits; qa_bigint_init(&bits); qa_fp_read_bits(&bits, bytes, n);
        int64_t sv = qa_bigint_as_int_n(&bits, opcode == 0xda ? 32 : 16);
        qa_bigint_free(&bits);
        right = qa_fp_from_i64(sv);
      } else {
        size_t n = opcode == 0xd8 ? 4 : 8;
        mem_copy(ctx, address, bytes, n);
        qa_bigint bits; qa_bigint_init(&bits); qa_fp_read_bits(&bits, bytes, n);
        right = qa_fp_decode_binary(&bits, opcode == 0xd8 ? 32 : 64);
        qa_bigint_free(&bits);
      }
      binary_instruction(ctx, group, &right, 0, false, false);
      qa_binary_value_free(&right);
      return;
    }
    int real_width = opcode == 0xd9 ? 32 : opcode == 0xdd ? 64 : 80;
    int integer_width = opcode == 0xdb ? 32 : opcode == 0xdf && (group == 5 || group == 7) ? 64 : 16;
    bool real = opcode == 0xd9 || opcode == 0xdd || (opcode == 0xdb && (group == 5 || group == 7));
    bool load = group == 0 || (group == 5 && (opcode == 0xdb || opcode == 0xdf));
    bool store = group == 2 || group == 3 || group == 7 || (group == 1 && (opcode == 0xdb || opcode == 0xdd || opcode == 0xdf));
    if (!load && !store) {
      char detail[96]; snprintf(detail, sizeof detail, "Unsupported x87 memory opcode %x/%d", opcode, group);
      qa_numeric_throw_unsupported(ctx, detail);
    }
    if (load) {
      if (real_width == 80 && real) {
        mem_copy(ctx, address, bytes, 10);
        qa_bigint bits; qa_bigint_init(&bits); qa_fp_read_bits(&bits, bytes, 10);
        push_raw(ctx, state, &bits);
        qa_bigint_free(&bits);
        return;
      }
      qa_binary_value value;
      if (real) {
        mem_copy(ctx, address, bytes, (size_t)(real_width / 8));
        qa_bigint bits; qa_bigint_init(&bits); qa_fp_read_bits(&bits, bytes, (size_t)(real_width / 8));
        value = qa_fp_decode_binary(&bits, real_width);
        qa_bigint_free(&bits);
      } else {
        mem_copy(ctx, address, bytes, (size_t)(integer_width / 8));
        qa_bigint bits; qa_bigint_init(&bits); qa_fp_read_bits(&bits, bytes, (size_t)(integer_width / 8));
        if (integer_width == 64) {
          qa_bigint sv; qa_bigint_init(&sv);
          int64_t v = qa_bigint_as_int_n(&bits, 64);
          qa_bigint_set_i64(&sv, v);
          value = qa_fp_from_integer(&sv);
          qa_bigint_free(&sv);
        } else value = qa_fp_from_i64(qa_bigint_as_int_n(&bits, integer_width));
        qa_bigint_free(&bits);
      }
      qa_binary_result result;
      qa_fp_convert_binary(&result, &value, qa_binary80_format, qa_fp_rounding_from_bits((int)(state->control_word >> 10)));
      int extra = (value.kind == QA_BINARY_FINITE && value.denormal) ? QA_FP_DENORMAL_OPERAND : 0;
      qa_binary_value_free(&value);
      raise_flags(ctx, state, result.flags | extra, false, false);
      push_value_ctx(ctx, state, &result.value);
      qa_binary_value_free(&result.value);
    } else {
      qa_binary_value value;
      read_register_ctx(ctx, &value, state, 0);
      if (real && group != 1) {
        qa_binary_result result;
        bool identity = real_width == 80;
        if (identity) { result.value = qa_fp_zero(value.sign); qa_binary_value_clone(&result.value, &value); result.flags = 0; result.rounded_up = false; }
        else qa_fp_convert_binary(&result, &value, qa_fp_format_for(real_width), qa_fp_rounding_from_bits((int)(state->control_word >> 10)));
        bool tiny = result.value.kind == QA_BINARY_FINITE && result.value.denormal;
        int range_exception = (result.flags & QA_FP_OVERFLOW) | (tiny || (result.flags & QA_FP_UNDERFLOW) != 0 ? QA_FP_UNDERFLOW : 0);
        if ((range_exception & ~(int)state->control_word) != 0) raise_flags(ctx, state, range_exception, false, false);
        raise_flags(ctx, state, result.flags, result.rounded_up, false);
        qa_bigint bits; qa_bigint_init(&bits);
        if (real_width == 80) raw_register(ctx, &bits, state, 0);
        else qa_fp_encode_binary(&bits, &result.value, real_width);
        qa_fp_write_bits(bytes, &bits, (size_t)(real_width / 8));
        qa_bigint_free(&bits);
        qa_memory_write(ctx->memory, address, bytes, (size_t)(real_width / 8));
        qa_binary_value_free(&result.value);
      } else {
        int width = (opcode == 0xdd && group == 1) ? 64 : integer_width;
        qa_fp_integer_conversion conv;
        qa_fp_integer_conversion_apply(&conv, &value, width,
          group == 1 ? QA_FP_ZERO : qa_fp_rounding_from_bits((int)(state->control_word >> 10)));
        raise_flags(ctx, state, conv.flags, conv.rounded_up, false);
        qa_fp_write_bits(bytes, &conv.value, (size_t)(width / 8));
        qa_bigint_free(&conv.value);
        qa_memory_write(ctx->memory, address, bytes, (size_t)(width / 8));
      }
      qa_binary_value_free(&value);
      if (group == 1 || group == 3 || group == 7) pop_x87(state);
    }
    return;
  }
  int index = modrm & 7;
  if (opcode == 0xd8) {
    qa_binary_value right; read_register_ctx(ctx, &right, state, index);
    binary_instruction(ctx, group, &right, 0, false, false);
    qa_binary_value_free(&right);
    return;
  }
  if (opcode == 0xdc || opcode == 0xde) {
    if (opcode == 0xde && modrm == 0xd9) {
      qa_binary_value right; read_register_ctx(ctx, &right, state, 1);
      compare_op(ctx, &right, false, false);
      qa_binary_value_free(&right);
      pop_x87(state); pop_x87(state);
      return;
    }
    if (group == 2 || group == 3) qa_numeric_throw_unsupported(ctx, "Reserved x87 register comparison encoding");
    qa_binary_value right; read_register_ctx(ctx, &right, state, 0);
    binary_instruction(ctx, group, &right, index, true, opcode == 0xde);
    qa_binary_value_free(&right);
    return;
  }
  if (opcode == 0xd9) {
    if (group == 0) {
      qa_bigint bits; qa_bigint_init(&bits); raw_register(ctx, &bits, state, index);
      push_raw(ctx, state, &bits);
      qa_bigint_free(&bits);
      return;
    }
    if (group == 1) {
      qa_bigint a; qa_bigint_init(&a); raw_register(ctx, &a, state, 0);
      qa_bigint b; qa_bigint_init(&b); raw_register(ctx, &b, state, index);
      write_raw_register(state, 0, &b);
      write_raw_register(state, index, &a);
      qa_bigint_free(&a); qa_bigint_free(&b);
      return;
    }
    if (modrm == 0xd0) return;
    if (modrm == 0xe0 || modrm == 0xe1) {
      qa_bigint bits; qa_bigint_init(&bits); raw_register(ctx, &bits, state, 0);
      qa_bigint sign_bit; qa_bigint_init(&sign_bit);
      { qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1); qa_bigint_shl(&sign_bit, &one, 79); qa_bigint_free(&one); }
      if (modrm == 0xe0) {
        /* bits ^ (1<<79): toggle bit 79 */
        uint8_t raw[10]; qa_fp_write_bits(raw, &bits, 10); raw[9] ^= 0x80u;
        qa_fp_read_bits(&bits, raw, 10);
      } else {
        uint8_t raw[10]; qa_fp_write_bits(raw, &bits, 10); raw[9] &= 0x7fu;
        qa_fp_read_bits(&bits, raw, 10);
      }
      qa_bigint_free(&sign_bit);
      write_raw_register(state, 0, &bits);
      qa_bigint_free(&bits);
      return;
    }
    if (modrm == 0xe4) { qa_binary_value z = qa_fp_zero(0); compare_op(ctx, &z, false, false); qa_binary_value_free(&z); return; }
    if (modrm == 0xe5) {
      bool empty = tag_of(state, top_of(state)) == 3;
      qa_binary_value value;
      if (empty) value = qa_fp_zero(0); else read_register_ctx(ctx, &value, state, 0);
      uint32_t bits = empty ? 0x4100u : value.kind == QA_BINARY_UNSUPPORTED ? 0u
        : value.kind == QA_BINARY_NAN ? 0x100u : value.kind == QA_BINARY_INFINITY ? 0x500u
        : qa_bigint_is_zero(&value.coefficient) ? 0x4000u : value.denormal ? 0x4400u : 0x400u;
      state->status_word = (state->status_word & ~0x4700u) | bits | (value.sign == 1 ? 0x200u : 0u);
      qa_binary_value_free(&value);
      return;
    }
    if (modrm == 0xeb) {
      qa_binary_value constant = qa_fp_zero(0);
      qa_bigint_set_hex(&constant.coefficient, "c90fdaa22168c234c");
      constant.exponent = -66;
      qa_binary_result converted;
      qa_fp_convert_binary(&converted, &constant, qa_binary80_format, qa_fp_rounding_from_bits((int)(state->control_word >> 10)));
      qa_binary_value_free(&constant);
      push_value_ctx(ctx, state, &converted.value);
      qa_binary_value_free(&converted.value);
      return;
    }
    if (modrm == 0xe8 || modrm == 0xee) {
      qa_binary_value v = modrm == 0xe8 ? qa_fp_from_i64(1) : qa_fp_zero(0);
      push_value_ctx(ctx, state, &v);
      qa_binary_value_free(&v);
      return;
    }
    if (modrm == 0xf6 || modrm == 0xf7) { set_top(state, top_of(state) + (modrm == 0xf6 ? -1 : 1)); state->status_word &= ~0x200u; return; }
    if (modrm == 0xf3) {
      qa_binary_value y; read_register_ctx(ctx, &y, state, 1);
      qa_binary_value x; read_register_ctx(ctx, &x, state, 0);
      qa_binary_result result;
      qa_x87_arctangent(&result, &y, &x, qa_fp_rounding_from_bits((int)(state->control_word >> 10)));
      qa_binary_value_free(&y); qa_binary_value_free(&x);
      commit_result(ctx, state, 1, &result, false);
      qa_binary_value_free(&result.value);
      pop_x87(state);
      return;
    }
    if (modrm == 0xfa) {
      qa_binary_value v; read_register_ctx(ctx, &v, state, 0);
      qa_binary_result result;
      qa_fp_square_root(&result, &v, result_format(ctx, state), qa_fp_rounding_from_bits((int)(state->control_word >> 10)));
      qa_binary_value_free(&v);
      commit_result(ctx, state, 0, &result, false);
      qa_binary_value_free(&result.value);
      return;
    }
    if (modrm == 0xfe || modrm == 0xff) {
      qa_binary_value v; read_register_ctx(ctx, &v, state, 0);
      qa_x87_trig_result result;
      qa_x87_trigonometric(&result, &v, modrm == 0xff, qa_fp_rounding_from_bits((int)(state->control_word >> 10)));
      qa_binary_value_free(&v);
      if (result.kind == QA_X87_TRIG_OUT_OF_RANGE) { state->status_word |= 0x400u; return; }
      state->status_word &= ~0x400u;
      commit_result(ctx, state, 0, &result.result, false);
      qa_binary_value_free(&result.result.value);
      return;
    }
    if (modrm == 0xfc) {
      qa_binary_value v; read_register_ctx(ctx, &v, state, 0);
      qa_binary_result result;
      qa_fp_round_integral(&result, &v, qa_fp_rounding_from_bits((int)(state->control_word >> 10)));
      qa_binary_value_free(&v);
      commit_result(ctx, state, 0, &result, false);
      qa_binary_value_free(&result.value);
      return;
    }
    const qa_fp_rounding mode = qa_fp_rounding_from_bits((int)(state->control_word >> 10));
    if (modrm == 0xf0) {
      qa_binary_value v; read_register_ctx(ctx, &v, state, 0);
      qa_binary_result result;
      qa_x87_two_power_minus_one(&result, &v, mode);
      qa_binary_value_free(&v);
      commit_result(ctx, state, 0, &result, false);
      qa_binary_value_free(&result.value);
      return;
    }
    if (modrm == 0xf1 || modrm == 0xf9) {
      qa_binary_value x; read_register_ctx(ctx, &x, state, 0);
      qa_binary_value y; read_register_ctx(ctx, &y, state, 1);
      qa_binary_result result;
      if (modrm == 0xf1) qa_x87_log2(&result, &y, &x, mode);
      else qa_x87_log2_plus_one(&result, &y, &x, mode);
      qa_binary_value_free(&x); qa_binary_value_free(&y);
      commit_result(ctx, state, 1, &result, false);
      qa_binary_value_free(&result.value);
      pop_x87(state);
      return;
    }
    if (modrm == 0xf2 || modrm == 0xfb) {
      qa_binary_value v; read_register_ctx(ctx, &v, state, 0);
      qa_binary_result sine, cosine, tangent;
      const bool has_pair = modrm == 0xfb && qa_x87_sine_cosine(&sine, &cosine, &v, mode);
      const bool has_tangent = modrm == 0xf2 && qa_x87_tangent(&tangent, &v, mode);
      qa_binary_value_free(&v);
      if (!has_pair && !has_tangent) { state->status_word |= 0x400u; return; }
      state->status_word &= ~0x400u;
      if (has_pair) {
        commit_result(ctx, state, 0, &sine, false);
        qa_binary_value_free(&sine.value);
        push_value_ctx(ctx, state, &cosine.value);
        raise_flags(ctx, state, cosine.flags, cosine.rounded_up, false);
        qa_binary_value_free(&cosine.value);
      } else {
        commit_result(ctx, state, 0, &tangent, false);
        qa_binary_value_free(&tangent.value);
        qa_binary_value one = qa_fp_from_i64(1);
        push_value_ctx(ctx, state, &one);
        qa_binary_value_free(&one);
      }
      return;
    }
    if (modrm == 0xf4) {
      qa_binary_value v; read_register_ctx(ctx, &v, state, 0);
      qa_x87_extract_result parts;
      qa_x87_extract(&parts, &v);
      qa_binary_value_free(&v);
      raise_flags(ctx, state, parts.flags, false, false);
      write_register(state, 0, &parts.exponent);
      push_value_ctx(ctx, state, &parts.significand);
      qa_binary_value_free(&parts.exponent);
      qa_binary_value_free(&parts.significand);
      return;
    }
    if (modrm == 0xf5 || modrm == 0xf8) {
      qa_binary_value dividend; read_register_ctx(ctx, &dividend, state, 0);
      qa_binary_value divisor; read_register_ctx(ctx, &divisor, state, 1);
      qa_x87_remainder_result outcome;
      qa_x87_remainder(&outcome, &dividend, &divisor, modrm == 0xf5, mode);
      qa_binary_value_free(&dividend); qa_binary_value_free(&divisor);
      commit_result(ctx, state, 0, &outcome.result, false);
      qa_binary_value_free(&outcome.result.value);
      /* C2 reports an incomplete reduction; otherwise C0, C3 and C1 carry quotient bits 2, 1 and 0. */
      const int q = outcome.quotient;
      state->status_word = (state->status_word & ~0x4700u) |
          (outcome.complete ? ((q & 4) != 0 ? 0x100u : 0u) | ((q & 2) != 0 ? 0x4000u : 0u) | ((q & 1) != 0 ? 0x200u : 0u) : 0x400u);
      return;
    }
    if (modrm == 0xfd) {
      qa_binary_value v; read_register_ctx(ctx, &v, state, 0);
      qa_binary_value e; read_register_ctx(ctx, &e, state, 1);
      qa_binary_result result;
      qa_x87_scale(&result, &v, &e, mode);
      qa_binary_value_free(&v); qa_binary_value_free(&e);
      commit_result(ctx, state, 0, &result, false);
      qa_binary_value_free(&result.value);
      return;
    }
    { char detail[96]; snprintf(detail, sizeof detail, "Unsupported x87 special opcode d9 %x", modrm); qa_numeric_throw_unsupported(ctx, detail); }
  }
  if (opcode == 0xdd) {
    if (group == 0) { set_tag(state, physical(state, index), 3); return; }
    if (group == 2 || group == 3) {
      qa_bigint bits; qa_bigint_init(&bits); raw_register(ctx, &bits, state, 0);
      write_raw_register(state, index, &bits);
      qa_bigint_free(&bits);
      if (group == 3) pop_x87(state);
      return;
    }
    if (group == 4 || group == 5) {
      qa_binary_value right; read_register_ctx(ctx, &right, state, index);
      compare_op(ctx, &right, true, false);
      qa_binary_value_free(&right);
      if (group == 5) pop_x87(state);
      return;
    }
  }
  if (opcode == 0xda && modrm == 0xe9) {
    qa_binary_value right; read_register_ctx(ctx, &right, state, 1);
    compare_op(ctx, &right, true, false);
    qa_binary_value_free(&right);
    pop_x87(state); pop_x87(state);
    return;
  }
  if ((opcode == 0xdb || opcode == 0xdf) && (group == 5 || group == 6)) {
    qa_binary_value right; read_register_ctx(ctx, &right, state, index);
    compare_op(ctx, &right, group == 5, true);
    qa_binary_value_free(&right);
    if (opcode == 0xdf) pop_x87(state);
    return;
  }
  if ((opcode == 0xda || opcode == 0xdb) && group <= 3) {
    qa_processor_flags *flags = &ctx->state->flags;
    bool condition = group == 0 ? qa_processor_flags_get(flags, QA_FLAG_CARRY)
      : group == 1 ? qa_processor_flags_get(flags, QA_FLAG_ZERO)
      : group == 2 ? qa_processor_flags_get(flags, QA_FLAG_CARRY) || qa_processor_flags_get(flags, QA_FLAG_ZERO)
      : qa_processor_flags_get(flags, QA_FLAG_PARITY);
    qa_binary_value source; read_register_ctx(ctx, &source, state, index);
    qa_binary_value discarded; read_register_ctx(ctx, &discarded, state, 0);
    qa_binary_value_free(&discarded);
    if (condition == (opcode == 0xda)) write_register(state, 0, &source);
    qa_binary_value_free(&source);
    return;
  }
  { char detail[96]; snprintf(detail, sizeof detail, "Unsupported x87 opcode %x %x", opcode, modrm); qa_numeric_throw_unsupported(ctx, detail); }
}
void qa_execute_x87(qa_numeric_result *out, qa_numeric_context *ctx) {
  jmp_buf buf;
  jmp_buf *saved = ctx->throw_buf;
  if (setjmp(buf) == 0) {
    ctx->throw_buf = &buf;
    execute_inner(ctx);
    ctx->throw_buf = saved;
    qa_numeric_set_result(out, QA_NUMERIC_EXECUTED, 0, "");
    return;
  }
  ctx->throw_buf = saved;
  if (ctx->throw_kind == QA_THROW_DEFERRED) { qa_numeric_set_result(out, QA_NUMERIC_EXECUTED, 0, ""); return; }
  if (ctx->throw_kind == QA_THROW_UNSUPPORTED) { qa_numeric_set_result(out, QA_NUMERIC_UNSUPPORTED, 0, ctx->throw_detail); return; }
  /* NumericFault propagates in TS; rethrow to an outer dispatcher if any. */
  if (saved != NULL) longjmp(*saved, 1);
  qa_numeric_set_result(out, QA_NUMERIC_EXCEPTION, ctx->throw_vector, ctx->throw_detail);
}
