/* Port of src/guest/floating-point/sse.ts. GPL-2.0-or-later. */
#include "qa/guest/floating_point/sse.h"

#include <stdio.h>
#include <string.h>

#include "qa/guest/floating_point/raw_sse.h"
#include "qa/guest/floating_point/x87.h"

static qa_guest_register general_register(qa_numeric_context *ctx, int index) {
  if (index < 0 || index > 15) qa_numeric_throw_unsupported(ctx, "Invalid general register index");
  return (qa_guest_register)index;
}
/* Returns XMM byte offset or throws unsupported. */
static int xmm_offset(qa_numeric_context *ctx, int index) {
  if (index < 0 || (size_t)(index + 1) * 16u > ctx->state->simd.xmm_len)
    qa_numeric_throw_unsupported(ctx, "Invalid XMM register index");
  return index * 16;
}
static const qa_numeric_operand *operand_of(qa_numeric_context *ctx) {
  if (!ctx->instruction.has_operand) qa_numeric_throw_unsupported(ctx, "SSE instruction requires an operand");
  return &ctx->instruction.operand;
}
static void aligned_check(qa_numeric_context *ctx) {
  const qa_numeric_operand *source = operand_of(ctx);
  if (source->kind == QA_OPERAND_MEMORY && source->address.byte_offset % 16u != 0)
    qa_numeric_fault(ctx, 13, "Unaligned 16-byte SIMD operand");
}
/* TS load(): register slice copy or memory copy into caller buffer. */
static void load_bytes(qa_numeric_context *ctx, uint8_t *dest, int byte_length, bool require_alignment) {
  const qa_numeric_operand *source = operand_of(ctx);
  if (require_alignment && byte_length == 16) aligned_check(ctx);
  if (source->kind == QA_OPERAND_REGISTER) {
    int off = xmm_offset(ctx, source->index);
    memcpy(dest, ctx->state->simd.xmm + off, (size_t)byte_length);
  } else qa_memory_copy(ctx->memory, source->address, dest, (size_t)byte_length);
}
static void signal_flags(qa_numeric_context *ctx, int flags) {
  qa_guest_simd_state *state = &ctx->state->simd;
  state->mxcsr |= (uint32_t)flags;
  if ((flags & ~(int)(state->mxcsr >> 7) & 63) != 0)
    qa_numeric_fault(ctx, 19, "Unmasked SIMD floating-point exception");
}
static void source_value(qa_numeric_context *ctx, qa_binary_value *out, const uint8_t *bytes, int width, int offset) {
  if (width == 32) {
    uint32_t w = (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8)
               | ((uint32_t)bytes[offset + 2] << 16) | ((uint32_t)bytes[offset + 3] << 24);
    *out = qa_fp_decode_binary32(w);
  } else {
    qa_bigint bits; qa_bigint_init(&bits); qa_fp_read_bits(&bits, bytes + offset, 8);
    *out = qa_fp_decode_binary(&bits, 64);
    qa_bigint_free(&bits);
  }
  if (out->kind == QA_BINARY_FINITE && out->denormal && (ctx->state->simd.mxcsr & 64u) != 0) {
    qa_fp_sign sign = out->sign;
    qa_binary_value_free(out);
    *out = qa_fp_zero(sign);
  }
}
static void finish_result(qa_numeric_context *ctx, qa_binary_result *result) {
  bool flush = (ctx->state->simd.mxcsr & 0x8000u) != 0 && (ctx->state->simd.mxcsr & 0x800u) != 0;
  if (flush && result->value.kind == QA_BINARY_FINITE && result->value.denormal && (result->flags & QA_FP_UNDERFLOW) != 0) {
    qa_fp_sign sign = result->value.sign;
    int flags = result->flags | QA_FP_UNDERFLOW | QA_FP_PRECISION;
    qa_binary_value_free(&result->value);
    result->value = qa_fp_zero(sign);
    result->flags = flags;
    result->rounded_up = false;
  }
}
static uint64_t lane_u64(const uint8_t *bytes, int index, int width) {
  int offset = index * width / 8;
  uint64_t v = 0;
  for (int i = width / 8 - 1; i >= 0; i--) v = (v << 8) | (uint64_t)bytes[offset + i];
  return v;
}
static void set_lane_u64(uint8_t *bytes, int index, int width, uint64_t value) {
  int offset = index * width / 8;
  for (int i = 0; i < width / 8; i++) { bytes[offset + i] = (uint8_t)value; value >>= 8; }
}
static void set_lane_big(uint8_t *bytes, int index, int width, const qa_bigint *value) {
  qa_fp_write_bits(bytes + index * width / 8, value, (size_t)(width / 8));
}
static int64_t as_int_n64(int width, uint64_t v) {
  if (width < 64) {
    v &= (1ull << width) - 1ull;
    if (((v >> (width - 1)) & 1u) != 0) return (int64_t)(v - (1ull << width));
    return (int64_t)v;
  }
  return (int64_t)v;
}
static int64_t integer_source(qa_numeric_context *ctx, int width) {
  const qa_numeric_operand *source = operand_of(ctx);
  uint64_t raw;
  if (source->kind == QA_OPERAND_REGISTER)
    raw = qa_integer_register_file_read(&ctx->state->registers, general_register(ctx, source->index), width, false);
  else {
    uint8_t b[8]; qa_memory_copy(ctx->memory, source->address, b, (size_t)(width / 8));
    raw = lane_u64(b, 0, width);
  }
  return as_int_n64(width, raw);
}
static void floating_op(qa_numeric_context *ctx, int opcode) {
  qa_numeric_instruction *ins = &ctx->instruction;
  bool scalar = ins->prefix == QA_PREFIX_F2 || ins->prefix == QA_PREFIX_F3;
  int width = ins->prefix == QA_PREFIX_66 || ins->prefix == QA_PREFIX_F2 ? 64 : 32;
  int count = scalar ? 1 : 128 / width;
  int dest_off = xmm_offset(ctx, ins->register_index);
  uint8_t left_bytes[16], right_bytes[16], output[16];
  memcpy(left_bytes, ctx->state->simd.xmm + dest_off, 16);
  load_bytes(ctx, right_bytes, scalar ? width / 8 : 16, true);
  memcpy(output, ctx->state->simd.xmm + dest_off, 16);
  qa_fp_rounding mode = qa_fp_rounding_from_bits((int)(ctx->state->simd.mxcsr >> 13));
  int flags = 0;
  for (int index = 0; index < count; index++) {
    qa_binary_value left, right;
    source_value(ctx, &left, left_bytes, width, index * width / 8);
    source_value(ctx, &right, right_bytes, width, index * width / 8);
    if (opcode == 0xc2) {
      if (!ins->has_immediate) { qa_binary_value_free(&left); qa_binary_value_free(&right); qa_numeric_throw_unsupported(ctx, "CMP requires an immediate predicate"); }
      int predicate = ins->immediate & 7;
      qa_fp_comparison cmp = qa_fp_compare_binary(&left, &right);
      bool unordered = cmp == QA_FP_UNORDERED, less = cmp == QA_FP_LESS, equal = cmp == QA_FP_EQUAL;
      bool yes = predicate == 0 ? equal : predicate == 1 ? less : predicate == 2 ? less || equal
        : predicate == 3 ? unordered : predicate == 4 ? !equal : predicate == 5 ? !less
        : predicate == 6 ? !(less || equal) : !unordered;
      bool signaling_pred = predicate == 1 || predicate == 2 || predicate == 5 || predicate == 6;
      if ((left.kind == QA_BINARY_NAN && (left.signaling || signaling_pred)) || (right.kind == QA_BINARY_NAN && (right.signaling || signaling_pred))) flags |= QA_FP_INVALID;
      else if ((left.kind == QA_BINARY_FINITE && left.denormal) || (right.kind == QA_BINARY_FINITE && right.denormal)) flags |= QA_FP_DENORMAL_OPERAND;
      set_lane_u64(output, index, width, yes ? (width == 64 ? UINT64_MAX : (1ull << width) - 1ull) : 0);
      qa_binary_value_free(&left); qa_binary_value_free(&right);
      continue;
    }
    qa_binary_result result;
    if (opcode == 0x51) qa_fp_square_root(&result, &right, qa_fp_format_for(width), mode);
    else if (opcode == 0x5d || opcode == 0x5f) {
      qa_fp_comparison cmp = qa_fp_compare_binary(&left, &right);
      const qa_binary_value *selected = cmp == QA_FP_UNORDERED || cmp == QA_FP_EQUAL ? &right
        : (opcode == 0x5d ? cmp == QA_FP_LESS : cmp == QA_FP_GREATER) ? &left : &right;
      bool special = (left.kind == QA_BINARY_NAN && left.signaling) || (right.kind == QA_BINARY_NAN && right.signaling);
      result.value = qa_fp_zero(selected->sign);
      qa_binary_value_clone(&result.value, selected);
      result.flags = special ? QA_FP_INVALID
        : (left.kind == QA_BINARY_FINITE && left.denormal) || (right.kind == QA_BINARY_FINITE && right.denormal) ? QA_FP_DENORMAL_OPERAND : 0;
      result.rounded_up = false;
    } else {
      qa_binary_operation operation = opcode == 0x58 ? QA_FP_ADD : opcode == 0x59 ? QA_FP_MULTIPLY
        : opcode == 0x5c ? QA_FP_SUBTRACT : QA_FP_DIVIDE;
      qa_fp_arithmetic(&result, operation, &left, &right, qa_fp_format_for(width), mode, QA_FP_NAN_SSE);
    }
    finish_result(ctx, &result);
    flags |= result.flags;
    qa_bigint bits; qa_bigint_init(&bits); qa_fp_encode_binary(&bits, &result.value, width);
    set_lane_big(output, index, width, &bits);
    qa_bigint_free(&bits);
    qa_binary_value_free(&result.value);
    qa_binary_value_free(&left); qa_binary_value_free(&right);
  }
  signal_flags(ctx, flags);
  memcpy(ctx->state->simd.xmm + dest_off, output, 16);
}
static void conversions(qa_numeric_context *ctx, int opcode) {
  qa_numeric_instruction *ins = &ctx->instruction;
  int dest_off = xmm_offset(ctx, ins->register_index);
  qa_fp_rounding mode = qa_fp_rounding_from_bits((int)(ctx->state->simd.mxcsr >> 13));
  if (opcode == 0x2a) {
    if (ins->prefix != QA_PREFIX_F2 && ins->prefix != QA_PREFIX_F3) qa_numeric_throw_unsupported(ctx, "MMX integer conversion is unsupported");
    int width = ins->prefix == QA_PREFIX_F2 ? 64 : 32;
    qa_binary_value iv = qa_fp_from_i64(integer_source(ctx, ins->operand_bits == 64 ? 64 : 32));
    qa_binary_result result;
    qa_fp_convert_binary(&result, &iv, qa_fp_format_for(width), mode);
    qa_binary_value_free(&iv);
    finish_result(ctx, &result);
    signal_flags(ctx, result.flags);
    qa_bigint bits; qa_bigint_init(&bits); qa_fp_encode_binary(&bits, &result.value, width);
    set_lane_big(ctx->state->simd.xmm + dest_off, 0, width, &bits);
    qa_bigint_free(&bits);
    qa_binary_value_free(&result.value);
    return;
  }
  if (opcode == 0x2c || opcode == 0x2d) {
    if (ins->prefix != QA_PREFIX_F2 && ins->prefix != QA_PREFIX_F3) qa_numeric_throw_unsupported(ctx, "MMX float conversion is unsupported");
    int width = ins->prefix == QA_PREFIX_F2 ? 64 : 32;
    int output_width = ins->operand_bits == 64 ? 64 : 32;
    uint8_t bytes[8];
    load_bytes(ctx, bytes, width / 8, true);
    qa_binary_value input;
    source_value(ctx, &input, bytes, width, 0);
    qa_fp_integer_conversion conv;
    qa_fp_integer_conversion_apply(&conv, &input, output_width, opcode == 0x2c ? QA_FP_ZERO : mode);
    qa_binary_value_free(&input);
    uint64_t raw = qa_bigint_as_uint_n(&conv.value, output_width);
    int flags = conv.flags;
    qa_bigint_free(&conv.value);
    signal_flags(ctx, flags);
    qa_integer_register_file_write(&ctx->state->registers, general_register(ctx, ins->register_index), output_width, raw, false);
    return;
  }
  uint8_t output[16];
  memcpy(output, ctx->state->simd.xmm + dest_off, 16);
  int flags = 0;
  if (opcode == 0x5a) {
    bool scalar = ins->prefix == QA_PREFIX_F2 || ins->prefix == QA_PREFIX_F3;
    int source_width = ins->prefix == QA_PREFIX_66 || ins->prefix == QA_PREFIX_F2 ? 64 : 32;
    int target_width = source_width == 32 ? 64 : 32;
    int count = scalar ? 1 : 2;
    uint8_t source[16];
    load_bytes(ctx, source, count * source_width / 8, true);
    if (!scalar) memset(output, 0, 16);
    for (int index = 0; index < count; index++) {
      qa_binary_value value;
      source_value(ctx, &value, source, source_width, index * source_width / 8);
      qa_binary_result result;
      qa_fp_convert_binary(&result, &value, qa_fp_format_for(target_width), mode);
      finish_result(ctx, &result);
      flags |= result.flags | (value.kind == QA_BINARY_FINITE && value.denormal ? QA_FP_DENORMAL_OPERAND : 0);
      qa_bigint bits; qa_bigint_init(&bits); qa_fp_encode_binary(&bits, &result.value, target_width);
      set_lane_big(output, index, target_width, &bits);
      qa_bigint_free(&bits);
      qa_binary_value_free(&result.value);
      qa_binary_value_free(&value);
    }
  } else if (opcode == 0x5b) {
    uint8_t source[16];
    load_bytes(ctx, source, 16, true);
    for (int index = 0; index < 4; index++) {
      if (ins->prefix == QA_PREFIX_NONE) {
        qa_binary_value iv = qa_fp_from_i64(as_int_n64(32, lane_u64(source, index, 32)));
        qa_binary_result result;
        qa_fp_convert_binary(&result, &iv, qa_fp_format_for(32), mode);
        qa_binary_value_free(&iv);
        flags |= result.flags;
        qa_bigint bits; qa_bigint_init(&bits); qa_fp_encode_binary(&bits, &result.value, 32);
        set_lane_big(output, index, 32, &bits);
        qa_bigint_free(&bits);
        qa_binary_value_free(&result.value);
      } else if (ins->prefix == QA_PREFIX_66 || ins->prefix == QA_PREFIX_F3) {
        qa_binary_value value;
        source_value(ctx, &value, source, 32, index * 4);
        qa_fp_integer_conversion conv;
        qa_fp_integer_conversion_apply(&conv, &value, 32, ins->prefix == QA_PREFIX_F3 ? QA_FP_ZERO : mode);
        qa_binary_value_free(&value);
        flags |= conv.flags;
        set_lane_big(output, index, 32, &conv.value);
        qa_bigint_free(&conv.value);
      } else qa_numeric_throw_unsupported(ctx, "Reserved 5B conversion prefix");
    }
  } else if (opcode == 0xe6) {
    if (ins->prefix == QA_PREFIX_F3) {
      uint8_t source[8];
      load_bytes(ctx, source, 8, true);
      for (int index = 0; index < 2; index++) {
        qa_binary_value iv = qa_fp_from_i64(as_int_n64(32, lane_u64(source, index, 32)));
        qa_binary_result result;
        qa_fp_convert_binary(&result, &iv, qa_fp_format_for(64), mode);
        qa_binary_value_free(&iv);
        flags |= result.flags;
        qa_bigint bits; qa_bigint_init(&bits); qa_fp_encode_binary(&bits, &result.value, 64);
        set_lane_big(output, index, 64, &bits);
        qa_bigint_free(&bits);
        qa_binary_value_free(&result.value);
      }
    } else if (ins->prefix == QA_PREFIX_66 || ins->prefix == QA_PREFIX_F2) {
      uint8_t source[16];
      load_bytes(ctx, source, 16, true);
      memset(output, 0, 16);
      for (int index = 0; index < 2; index++) {
        qa_binary_value value;
        source_value(ctx, &value, source, 64, index * 8);
        qa_fp_integer_conversion conv;
        qa_fp_integer_conversion_apply(&conv, &value, 32, ins->prefix == QA_PREFIX_66 ? QA_FP_ZERO : mode);
        qa_binary_value_free(&value);
        flags |= conv.flags;
        set_lane_big(output, index, 32, &conv.value);
        qa_bigint_free(&conv.value);
      }
    } else qa_numeric_throw_unsupported(ctx, "Reserved E6 conversion prefix");
  } else qa_numeric_throw_unsupported(ctx, "Unsupported conversion opcode");
  signal_flags(ctx, flags);
  memcpy(ctx->state->simd.xmm + dest_off, output, 16);
}
static void packed_integer(qa_numeric_context *ctx, int opcode) {
  qa_numeric_instruction *ins = &ctx->instruction;
  if (ins->prefix != QA_PREFIX_66) qa_numeric_throw_unsupported(ctx, "MMX packed integer instruction is unsupported");
  int dest_off = xmm_offset(ctx, ins->register_index);
  uint8_t a[16], b[16], output[16];
  memcpy(a, ctx->state->simd.xmm + dest_off, 16);
  load_bytes(ctx, b, 16, true);
  memset(output, 0, 16);
  int add_width = opcode == 0xfc ? 8 : opcode == 0xfd ? 16 : opcode == 0xfe ? 32 : opcode == 0xd4 ? 64 : 0;
  int sub_width = opcode == 0xf8 ? 8 : opcode == 0xf9 ? 16 : opcode == 0xfa ? 32 : opcode == 0xfb ? 64 : 0;
  if (add_width != 0 || sub_width != 0) {
    int width = add_width != 0 ? add_width : sub_width;
    for (int i = 0; i < 128 / width; i++)
      set_lane_u64(output, i, width, add_width != 0 ? lane_u64(a, i, width) + lane_u64(b, i, width)
                                                   : lane_u64(a, i, width) - lane_u64(b, i, width));
  } else if ((opcode >= 0x64 && opcode <= 0x66) || (opcode >= 0x74 && opcode <= 0x76)) {
    int width = opcode % 16 == 4 ? 8 : opcode % 16 == 5 ? 16 : 32;
    for (int i = 0; i < 128 / width; i++) {
      int64_t l = as_int_n64(width, lane_u64(a, i, width)), r = as_int_n64(width, lane_u64(b, i, width));
      set_lane_u64(output, i, width, (opcode < 0x70 ? l > r : l == r) ? UINT64_MAX : 0);
    }
  } else if ((opcode >= 0x60 && opcode <= 0x62) || (opcode >= 0x68 && opcode <= 0x6a) || opcode == 0x6c || opcode == 0x6d) {
    int width = opcode == 0x6c || opcode == 0x6d ? 64 : (opcode & 7) == 0 ? 8 : (opcode & 7) == 1 ? 16 : 32;
    bool high = opcode >= 0x68 && opcode != 0x6c;
    int start = high ? 64 / width : 0;
    for (int i = 0; i < 64 / width; i++) {
      set_lane_u64(output, i * 2, width, lane_u64(a, start + i, width));
      set_lane_u64(output, i * 2 + 1, width, lane_u64(b, start + i, width));
    }
  } else if (opcode == 0xd5 || opcode == 0xe4 || opcode == 0xe5) {
    for (int i = 0; i < 8; i++) {
      uint64_t res;
      if (opcode == 0xe5) {
        int64_t l = as_int_n64(16, lane_u64(a, i, 16)), r = as_int_n64(16, lane_u64(b, i, 16));
        res = (uint64_t)((l * r) >> 16);
      } else {
        uint64_t l = lane_u64(a, i, 16), r = lane_u64(b, i, 16);
        res = opcode == 0xd5 ? l * r : (l * r) >> 16;
      }
      set_lane_u64(output, i, 16, res);
    }
  } else if (opcode == 0xf4) {
    set_lane_u64(output, 0, 64, lane_u64(a, 0, 32) * lane_u64(b, 0, 32));
    set_lane_u64(output, 1, 64, lane_u64(a, 2, 32) * lane_u64(b, 2, 32));
  } else if (opcode == 0xf5) {
    for (int i = 0; i < 4; i++) {
      int64_t v = as_int_n64(16, lane_u64(a, i * 2, 16)) * as_int_n64(16, lane_u64(b, i * 2, 16))
                + as_int_n64(16, lane_u64(a, i * 2 + 1, 16)) * as_int_n64(16, lane_u64(b, i * 2 + 1, 16));
      set_lane_u64(output, i, 32, (uint64_t)v);
    }
  } else if (opcode == 0xf6) {
    for (int half = 0; half < 2; half++) {
      uint64_t sum = 0;
      for (int i = half * 8; i < half * 8 + 8; i++) {
        int64_t delta = (int64_t)lane_u64(a, i, 8) - (int64_t)lane_u64(b, i, 8);
        sum += (uint64_t)(delta < 0 ? -delta : delta);
      }
      set_lane_u64(output, half, 64, sum);
    }
  } else if (opcode == 0xd1 || opcode == 0xd2 || opcode == 0xd3 || opcode == 0xe1 || opcode == 0xe2 || opcode == 0xf1 || opcode == 0xf2 || opcode == 0xf3) {
    int width = (opcode & 15) == 1 ? 16 : (opcode & 15) == 2 ? 32 : 64;
    uint64_t count = lane_u64(b, 0, 64);
    for (int i = 0; i < 128 / width; i++) {
      uint64_t value = lane_u64(a, i, width);
      int64_t signed_v = as_int_n64(width, value);
      uint64_t shifted;
      if (opcode >= 0xf0) shifted = count >= (uint64_t)width ? 0 : value << count;
      else if (opcode >= 0xe0) shifted = count >= (uint64_t)width ? (signed_v < 0 ? UINT64_MAX : 0) : (uint64_t)(signed_v >> count);
      else shifted = count >= (uint64_t)width ? 0 : value >> count;
      set_lane_u64(output, i, width, shifted);
    }
  } else {
    char detail[96]; snprintf(detail, sizeof detail, "Unsupported packed integer opcode %x", opcode);
    qa_numeric_throw_unsupported(ctx, detail);
  }
  memcpy(ctx->state->simd.xmm + dest_off, output, 16);
}
static bool in_list(int opcode, const int *list, int n) {
  for (int i = 0; i < n; i++) if (list[i] == opcode) return true;
  return false;
}
static void execute_inner(qa_numeric_context *ctx) {
  qa_numeric_instruction *ins = &ctx->instruction;
  if (!ins->has_secondary_opcode) qa_numeric_throw_unsupported(ctx, "Missing SSE secondary opcode");
  int opcode = ins->secondary_opcode;
  static const int k_reserved_prefix[] = {0x14, 0x15, 0x28, 0x29, 0x2e, 0x2f, 0x50, 0x54, 0x55, 0x56, 0x57, 0xc6};
  if (in_list(opcode, k_reserved_prefix, 12) && ins->prefix != QA_PREFIX_NONE && ins->prefix != QA_PREFIX_66)
    qa_numeric_throw_unsupported(ctx, "Reserved mandatory prefix for SSE instruction");
  if (opcode == 0xae) {
    const qa_numeric_operand *source = operand_of(ctx);
    if (source->kind != QA_OPERAND_MEMORY || !ins->has_modrm) qa_numeric_throw_unsupported(ctx, "MXCSR instruction requires memory");
    int group = (ins->modrm >> 3) & 7;
    if (group == 2) {
      uint32_t value = qa_memory_read_u32(ctx->memory, source->address);
      if ((value & ~ctx->state->simd.mxcsr_mask) != 0) qa_numeric_fault(ctx, 13, "Reserved MXCSR bits are set");
      ctx->state->simd.mxcsr = value;
    } else if (group == 3) {
      uint8_t b[4]; qa_bigint bits; qa_bigint_init(&bits); qa_bigint_set_u64(&bits, ctx->state->simd.mxcsr);
      qa_fp_write_bits(b, &bits, 4); qa_bigint_free(&bits);
      qa_memory_write(ctx->memory, source->address, b, 4);
    } else qa_numeric_throw_unsupported(ctx, "FXSAVE/FXRSTOR/fence opcode is unsupported by the numeric executor");
    return;
  }
  if (opcode == 0x2a || opcode == 0x2c || opcode == 0x2d || opcode == 0x5a || opcode == 0x5b || opcode == 0xe6) { conversions(ctx, opcode); return; }
  qa_raw_sse_operation raw;
  if (qa_prepare_raw_sse(&raw, opcode, ins->prefix, ins->register_index)) {
    qa_numeric_result result;
    qa_execute_raw_sse(&result, &raw, operand_of(ctx), ctx->state, ctx->memory);
    if (result.kind == QA_NUMERIC_UNSUPPORTED) qa_numeric_throw_unsupported(ctx, result.detail);
    if (result.kind == QA_NUMERIC_EXCEPTION) qa_numeric_fault(ctx, result.vector, result.detail);
    return;
  }
  int dest_off = xmm_offset(ctx, ins->register_index);
  uint8_t *destination = ctx->state->simd.xmm + dest_off;
  if (opcode == 0x6f || opcode == 0x7f) qa_numeric_throw_unsupported(ctx, "MMX move is unsupported");
  if (opcode == 0x6e || opcode == 0x7e) {
    if (opcode == 0x7e && ins->prefix == QA_PREFIX_F3) {
      uint8_t bytes[8]; load_bytes(ctx, bytes, 8, true);
      memset(destination, 0, 16); memcpy(destination, bytes, 8);
      return;
    }
    if (ins->prefix != QA_PREFIX_66) qa_numeric_throw_unsupported(ctx, "MMX MOVD/MOVQ is unsupported");
    int width = ins->operand_bits == 64 ? 64 : 32;
    if (opcode == 0x6e) {
      uint64_t raw_value = (uint64_t)integer_source(ctx, width);
      memset(destination, 0, 16);
      set_lane_u64(destination, 0, width, raw_value);
    } else {
      const qa_numeric_operand *target = operand_of(ctx);
      uint64_t bits = lane_u64(destination, 0, width);
      if (target->kind == QA_OPERAND_REGISTER)
        qa_integer_register_file_write(&ctx->state->registers, general_register(ctx, target->index), width, bits, false);
      else { uint8_t b[8]; set_lane_u64(b, 0, width, bits); qa_memory_write(ctx->memory, target->address, b, (size_t)(width / 8)); }
    }
    return;
  }
  if (opcode == 0xd6 && ins->prefix == QA_PREFIX_66) {
    const qa_numeric_operand *target = operand_of(ctx);
    uint8_t bytes[8]; memcpy(bytes, destination, 8);
    if (target->kind == QA_OPERAND_REGISTER) {
      int off = xmm_offset(ctx, target->index);
      memset(ctx->state->simd.xmm + off, 0, 16);
      memcpy(ctx->state->simd.xmm + off, bytes, 8);
    } else qa_memory_write(ctx->memory, target->address, bytes, 8);
    return;
  }
  if (opcode == 0x12 || opcode == 0x13 || opcode == 0x16 || opcode == 0x17) {
    if (ins->prefix != QA_PREFIX_NONE && ins->prefix != QA_PREFIX_66) qa_numeric_throw_unsupported(ctx, "SSE3 duplicate move is unsupported");
    const qa_numeric_operand *source = operand_of(ctx);
    bool high = opcode == 0x16 || opcode == 0x17;
    if (opcode == 0x13 || opcode == 0x17) {
      if (source->kind != QA_OPERAND_MEMORY) qa_numeric_throw_unsupported(ctx, "MOVL/MOVH store requires memory");
      qa_memory_write(ctx->memory, source->address, destination + (high ? 8 : 0), 8);
    } else if (source->kind == QA_OPERAND_MEMORY) {
      qa_memory_copy(ctx->memory, source->address, destination + (high ? 8 : 0), 8);
    } else {
      if (ins->prefix == QA_PREFIX_66) qa_numeric_throw_unsupported(ctx, "MOVLPD/MOVHPD load requires memory");
      int off = xmm_offset(ctx, source->index);
      memmove(destination + (high ? 8 : 0), ctx->state->simd.xmm + off + (high ? 0 : 8), 8);
    }
    return;
  }
  if (opcode == 0x14 || opcode == 0x15 || opcode == 0xc6 || opcode == 0x70) {
    uint8_t source[16], original[16], output[16];
    load_bytes(ctx, source, 16, true);
    memcpy(original, destination, 16);
    memcpy(output, destination, 16);
    if (opcode == 0x70) {
      if (!ins->has_immediate) qa_numeric_throw_unsupported(ctx, "Shuffle requires immediate");
      if (ins->prefix == QA_PREFIX_66) {
        for (int i = 0; i < 4; i++) set_lane_u64(output, i, 32, lane_u64(source, (ins->immediate >> (2 * i)) & 3, 32));
      } else if (ins->prefix == QA_PREFIX_F2 || ins->prefix == QA_PREFIX_F3) {
        memcpy(output, source, 16);
        int start = ins->prefix == QA_PREFIX_F3 ? 4 : 0;
        for (int i = 0; i < 4; i++) set_lane_u64(output, start + i, 16, lane_u64(source, start + ((ins->immediate >> (2 * i)) & 3), 16));
      } else qa_numeric_throw_unsupported(ctx, "MMX shuffle is unsupported");
    } else {
      int width = ins->prefix == QA_PREFIX_66 ? 64 : 32;
      if (opcode == 0xc6) {
        if (!ins->has_immediate) qa_numeric_throw_unsupported(ctx, "Shuffle requires immediate");
        for (int i = 0; i < 128 / width; i++) {
          const uint8_t *from = i < 64 / width ? original : source;
          int selected = (ins->immediate >> (i * (width == 32 ? 2 : 1))) & (width == 32 ? 3 : 1);
          set_lane_u64(output, i, width, lane_u64(from, selected, width));
        }
      } else {
        int start = opcode == 0x15 ? 64 / width : 0;
        for (int i = 0; i < 64 / width; i++) {
          set_lane_u64(output, i * 2, width, lane_u64(original, start + i, width));
          set_lane_u64(output, i * 2 + 1, width, lane_u64(source, start + i, width));
        }
      }
    }
    memcpy(destination, output, 16);
    return;
  }
  if (opcode == 0x50 || opcode == 0xd7) {
    if (operand_of(ctx)->kind != QA_OPERAND_REGISTER) qa_numeric_throw_unsupported(ctx, "MOVMSK/PMOVMSKB requires a register source");
    uint8_t source[16]; load_bytes(ctx, source, 16, true);
    int width = opcode == 0xd7 ? 8 : ins->prefix == QA_PREFIX_66 ? 64 : 32;
    if (opcode == 0xd7 && ins->prefix != QA_PREFIX_66) qa_numeric_throw_unsupported(ctx, "MMX PMOVMSKB is unsupported");
    uint64_t mask = 0;
    for (int i = 0; i < 128 / width; i++)
      if (((lane_u64(source, i, width) >> (width - 1)) & 1u) != 0) mask |= 1ull << i;
    qa_integer_register_file_write(&ctx->state->registers, general_register(ctx, ins->register_index), 32, mask, false);
    return;
  }
  if (opcode == 0x2e || opcode == 0x2f) {
    int width = ins->prefix == QA_PREFIX_66 ? 64 : 32;
    qa_binary_value left, right;
    source_value(ctx, &left, destination, width, 0);
    uint8_t bytes[8]; load_bytes(ctx, bytes, width / 8, true);
    source_value(ctx, &right, bytes, width, 0);
    qa_fp_comparison cmp = qa_fp_compare_binary(&left, &right);
    bool nan = (left.kind == QA_BINARY_NAN && (opcode == 0x2f || left.signaling))
      || (right.kind == QA_BINARY_NAN && (opcode == 0x2f || right.signaling));
    int sflags = nan ? QA_FP_INVALID
      : (left.kind == QA_BINARY_FINITE && left.denormal) || (right.kind == QA_BINARY_FINITE && right.denormal) ? QA_FP_DENORMAL_OPERAND : 0;
    /* signal() runs before flags writes in TS; free after to keep order. */
    qa_binary_value_free(&left); qa_binary_value_free(&right);
    signal_flags(ctx, sflags);
    qa_processor_flags *flags = &ctx->state->flags;
    qa_processor_flags_set(flags, QA_FLAG_CARRY, cmp == QA_FP_LESS || cmp == QA_FP_UNORDERED);
    qa_processor_flags_set(flags, QA_FLAG_ZERO, cmp == QA_FP_EQUAL || cmp == QA_FP_UNORDERED);
    qa_processor_flags_set(flags, QA_FLAG_PARITY, cmp == QA_FP_UNORDERED);
    qa_processor_flags_set(flags, QA_FLAG_OVERFLOW, false);
    qa_processor_flags_set(flags, QA_FLAG_AUXILIARY_CARRY, false);
    qa_processor_flags_set(flags, QA_FLAG_SIGN, false);
    return;
  }
  static const int k_float_ops[] = {0x51, 0x58, 0x59, 0x5c, 0x5d, 0x5e, 0x5f, 0xc2};
  if (in_list(opcode, k_float_ops, 8)) { floating_op(ctx, opcode); return; }
  if (opcode == 0xc4 || opcode == 0xc5) {
    if (ins->prefix != QA_PREFIX_66 || !ins->has_immediate) qa_numeric_throw_unsupported(ctx, "PINSRW/PEXTRW requires SSE2 prefix and immediate");
    int index = ins->immediate & 7;
    if (opcode == 0xc4) {
      const qa_numeric_operand *source = operand_of(ctx);
      uint64_t value;
      if (source->kind == QA_OPERAND_REGISTER)
        value = qa_integer_register_file_read(&ctx->state->registers, general_register(ctx, source->index), 32, false);
      else { uint8_t b[2]; qa_memory_copy(ctx->memory, source->address, b, 2); value = lane_u64(b, 0, 16); }
      set_lane_u64(destination, index, 16, value);
    } else {
      if (operand_of(ctx)->kind != QA_OPERAND_REGISTER) qa_numeric_throw_unsupported(ctx, "PEXTRW requires XMM source");
      uint8_t source[16]; load_bytes(ctx, source, 16, true);
      qa_integer_register_file_write(&ctx->state->registers, general_register(ctx, ins->register_index), 32, lane_u64(source, index, 16), false);
    }
    return;
  }
  if (opcode == 0x71 || opcode == 0x72 || opcode == 0x73) {
    const qa_numeric_operand *target = operand_of(ctx);
    if (ins->prefix != QA_PREFIX_66 || !ins->has_immediate || !ins->has_modrm || target->kind != QA_OPERAND_REGISTER)
      qa_numeric_throw_unsupported(ctx, "Immediate packed shift requires SSE2 register and immediate");
    int group = (ins->modrm >> 3) & 7;
    int count = ins->immediate;
    int reg_off = xmm_offset(ctx, target->index);
    uint8_t *reg = ctx->state->simd.xmm + reg_off;
    uint8_t original[16]; memcpy(original, reg, 16);
    if (opcode == 0x73 && (group == 3 || group == 7)) {
      qa_bigint bits; qa_bigint_init(&bits); qa_fp_read_bits(&bits, original, 16);
      if (count >= 16) qa_bigint_set_u64(&bits, 0);
      else if (group == 3) qa_bigint_shr(&bits, &bits, (int64_t)count * 8);
      else qa_bigint_shl(&bits, &bits, (int64_t)count * 8);
      qa_fp_write_bits(reg, &bits, 16);
      qa_bigint_free(&bits);
      return;
    }
    if (group != 2 && group != 4 && group != 6) qa_numeric_throw_unsupported(ctx, "Reserved packed shift group");
    int width = opcode == 0x71 ? 16 : opcode == 0x72 ? 32 : 64;
    if (width == 64 && group == 4) qa_numeric_throw_unsupported(ctx, "PSRAQ is unavailable in legacy SSE2");
    for (int i = 0; i < 128 / width; i++) {
      uint64_t value = lane_u64(original, i, width);
      int64_t signed_v = as_int_n64(width, value);
      uint64_t shifted = group == 4 ? (count >= width ? (signed_v < 0 ? UINT64_MAX : 0) : (uint64_t)(signed_v >> count))
        : count >= width ? 0 : group == 2 ? value >> count : value << count;
      set_lane_u64(reg, i, width, shifted);
    }
    return;
  }
  packed_integer(ctx, opcode);
}
void qa_execute_sse(qa_numeric_result *out, qa_numeric_context *ctx) {
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
  if (ctx->throw_kind == QA_THROW_UNSUPPORTED) { qa_numeric_set_result(out, QA_NUMERIC_UNSUPPORTED, 0, ctx->throw_detail); return; }
  if (saved != NULL) longjmp(*saved, 1);
  qa_numeric_set_result(out, QA_NUMERIC_EXCEPTION, ctx->throw_vector, ctx->throw_detail);
}
/* floating-point/index.ts executeNumericInstruction. */
void qa_execute_numeric_instruction(qa_numeric_result *out, qa_numeric_context *ctx) {
  jmp_buf buf;
  jmp_buf *saved = ctx->throw_buf;
  if (setjmp(buf) != 0) {
    ctx->throw_buf = saved;
    qa_numeric_set_result(out, QA_NUMERIC_EXCEPTION, ctx->throw_vector, ctx->throw_detail);
    return;
  }
  ctx->throw_buf = &buf;
  int opcode = ctx->instruction.opcode;
  if (opcode == 0x9b || (opcode >= 0xd8 && opcode <= 0xdf)) qa_execute_x87(out, ctx);
  else if (opcode == 0x0f) qa_execute_sse(out, ctx);
  else qa_numeric_set_result(out, QA_NUMERIC_UNSUPPORTED, 0, "Unsupported numeric instruction family");
  ctx->throw_buf = saved;
}
