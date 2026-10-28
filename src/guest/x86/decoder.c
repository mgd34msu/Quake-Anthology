/* Port of src/guest/x86/decoder.ts. GPL-2.0-or-later. */
#include "qa/guest/x86/decoder.h"

#include <string.h>

#include "qa/foundation/support.h"

const char *qa_x86_repeat_name(qa_x86_repeat repeat) {
  switch (repeat) {
    case QA_X86_REPEAT_F2: return "f2";
    case QA_X86_REPEAT_F3: return "f3";
    default: return "none";
  }
}
const char *qa_guest_segment_name(qa_guest_segment_index segment) {
  switch (segment) {
    case QA_SEG_CS: return "cs";
    case QA_SEG_DS: return "ds";
    case QA_SEG_ES: return "es";
    case QA_SEG_SS: return "ss";
    case QA_SEG_FS: return "fs";
    default: return "gs";
  }
}

static uint64_t as_uint32(uint64_t v) { return v & 0xffffffffull; }
static uint64_t as_uint_n(int width, uint64_t v) {
  return width >= 64 ? v : v & ((1ull << width) - 1ull);
}
static int64_t as_int_n(int width, uint64_t v) {
  v = as_uint_n(width, v);
  if (width < 64 && ((v >> (width - 1)) & 1ull) != 0) return (int64_t)v - (int64_t)(1ull << width);
  return (int64_t)v;
}

qa_guest_core_address qa_x86_guest_address(qa_sparse_guest_memory *memory, uint64_t raw,
                                      qa_guest_access access, size_t length) {
  qa_guest_core_address result;
  if (!qa_sparse_pointer(memory, raw, &result)) {
    qa_guest_fault_access fa = access == QA_ACCESS_READ ? QA_FAULT_ACCESS_READ
                               : access == QA_ACCESS_WRITE ? QA_FAULT_ACCESS_WRITE : QA_FAULT_ACCESS_EXECUTE;
    qa_guest_throw_memory(memory->throw_ctx, QA_FAULT_NULL_ADDRESS, &memory->module, raw, length, fa,
                          "null guest address");
  }
  return result;
}

qa_x86_operand qa_x86_register_operand(int index, int width) {
  bool high_byte = width == 8 && index >= 4;
  int actual = high_byte ? index - 4 : index;
  if (actual < 0 || actual > 7) qa_range_error("Invalid i386 register index %d", index);
  qa_x86_operand operand;
  memset(&operand, 0, sizeof operand);
  operand.is_memory = false;
  operand.reg = (qa_guest_register)actual;
  operand.high_byte = high_byte;
  operand.index = index;
  return operand;
}

static qa_x86_operand memory_operand(uint64_t offset, qa_guest_segment_index segment, bool stack_pointer_base) {
  qa_x86_operand operand;
  memset(&operand, 0, sizeof operand);
  operand.is_memory = true;
  operand.offset = offset;
  operand.segment = segment;
  operand.stack_pointer_base = stack_pointer_base;
  return operand;
}

int qa_x86_decoder_byte(qa_x86_decoder *decoder) {
  if (decoder->byte_count >= 15)
    qa_guest_throw_processor(decoder->memory->throw_ctx, 13, true, 0,
                             "Instruction exceeds the 15-byte architectural limit");
  if (decoder->cursor > decoder->state->segments[QA_SEG_CS].limit)
    qa_guest_throw_processor(decoder->memory->throw_ctx, 13, true, 0, "Instruction exceeds CS limit");
  int byte = (int)qa_sparse_fetch_byte(decoder->memory,
                                       as_uint32(decoder->state->segments[QA_SEG_CS].base + decoder->cursor));
  decoder->bytes[decoder->byte_count++] = (uint8_t)byte;
  decoder->cursor = as_uint32(decoder->cursor + 1u);
  return byte;
}

void qa_x86_decoder_init(qa_x86_decoder *decoder, qa_guest_processor_state *state, qa_sparse_guest_memory *memory) {
  memset(decoder, 0, sizeof *decoder);
  decoder->state = state;
  decoder->memory = memory;
  decoder->start = state->instruction_pointer;
  decoder->cursor = decoder->start;
  decoder->operand_bits = 32;
  decoder->address_bits = 32;
  decoder->repeat = QA_X86_REPEAT_NONE;
  for (;;) {
    int byte = qa_x86_decoder_byte(decoder);
    switch (byte) {
      case 0x66: decoder->operand_bits = 16; continue;
      case 0x67: decoder->address_bits = 16; continue;
      case 0xf0: decoder->lock = true; continue;
      case 0xf2: decoder->repeat = QA_X86_REPEAT_F2; continue;
      case 0xf3: decoder->repeat = QA_X86_REPEAT_F3; continue;
      case 0x26: decoder->has_segment = true; decoder->segment = QA_SEG_ES; continue;
      case 0x2e: decoder->has_segment = true; decoder->segment = QA_SEG_CS; continue;
      case 0x36: decoder->has_segment = true; decoder->segment = QA_SEG_SS; continue;
      case 0x3e: decoder->has_segment = true; decoder->segment = QA_SEG_DS; continue;
      case 0x64: decoder->has_segment = true; decoder->segment = QA_SEG_FS; continue;
      case 0x65: decoder->has_segment = true; decoder->segment = QA_SEG_GS; continue;
      default: decoder->opcode = byte; return;
    }
  }
}

uint64_t qa_x86_decoder_immediate(qa_x86_decoder *decoder, int width) {
  uint64_t value = 0;
  for (int offset = 0; offset < width; offset += 8)
    value |= (uint64_t)qa_x86_decoder_byte(decoder) << offset;
  return value;
}
int64_t qa_x86_decoder_signed(qa_x86_decoder *decoder, int width) {
  return as_int_n(width, qa_x86_decoder_immediate(decoder, width));
}
uint64_t qa_x86_decoder_register_value(qa_x86_decoder *decoder, int index, int width) {
  qa_x86_operand operand = qa_x86_register_operand(index, width);
  return qa_integer_register_file_read(&decoder->state->registers, operand.reg, width, false);
}

qa_x86_modrm qa_x86_decoder_modrm(qa_x86_decoder *decoder, int width) {
  int byte = qa_x86_decoder_byte(decoder);
  int mode = byte >> 6, group = (byte >> 3) & 7, rm = byte & 7;
  qa_x86_modrm result;
  memset(&result, 0, sizeof result);
  result.byte = byte;
  result.group = group;
  result.register_operand = qa_x86_register_operand(group, width);
  if (mode == 3) {
    result.operand = qa_x86_register_operand(rm, width);
    return result;
  }
  int64_t offset = 0;
  bool stack = false, stack_pointer_base = false;
  if (decoder->address_bits == 16) {
    int64_t bx = (int64_t)qa_x86_decoder_register_value(decoder, 3, 16);
    int64_t bp = (int64_t)qa_x86_decoder_register_value(decoder, 5, 16);
    int64_t si = (int64_t)qa_x86_decoder_register_value(decoder, 6, 16);
    int64_t di = (int64_t)qa_x86_decoder_register_value(decoder, 7, 16);
    switch (rm) {
      case 0: offset = bx + si; break;
      case 1: offset = bx + di; break;
      case 2: offset = bp + si; stack = true; break;
      case 3: offset = bp + di; stack = true; break;
      case 4: offset = si; break;
      case 5: offset = di; break;
      case 6:
        if (mode == 0) offset = (int64_t)qa_x86_decoder_immediate(decoder, 16);
        else { offset = bp; stack = true; }
        break;
      default: offset = bx; break;
    }
    if (mode == 1) offset += qa_x86_decoder_signed(decoder, 8);
    if (mode == 2) offset += qa_x86_decoder_signed(decoder, 16);
  } else {
    if (rm == 4) {
      int sib = qa_x86_decoder_byte(decoder);
      int scale = sib >> 6, index = (sib >> 3) & 7, base = sib & 7;
      if (index != 4) offset += (int64_t)(qa_x86_decoder_register_value(decoder, index, 32) << scale);
      if (base == 5 && mode == 0) offset += (int64_t)qa_x86_decoder_immediate(decoder, 32);
      else {
        offset += (int64_t)qa_x86_decoder_register_value(decoder, base, 32);
        stack = base == 4 || base == 5;
        stack_pointer_base = base == 4;
      }
    } else if (rm == 5 && mode == 0) {
      offset = (int64_t)qa_x86_decoder_immediate(decoder, 32);
    } else {
      offset = (int64_t)qa_x86_decoder_register_value(decoder, rm, 32);
      stack = rm == 5;
    }
    if (mode == 1) offset += qa_x86_decoder_signed(decoder, 8);
    if (mode == 2) offset += qa_x86_decoder_signed(decoder, 32);
  }
  result.operand = memory_operand(as_uint_n(decoder->address_bits, (uint64_t)offset),
                                  decoder->has_segment ? decoder->segment : (stack ? QA_SEG_SS : QA_SEG_DS),
                                  stack_pointer_base);
  return result;
}

qa_guest_core_address qa_x86_decoder_address(qa_x86_decoder *decoder, const qa_x86_operand *operand,
                                        size_t width_bytes, qa_guest_access access) {
  const qa_guest_segment *segment = &decoder->state->segments[operand->segment];
  if (operand->offset + (uint64_t)width_bytes - 1u > segment->limit)
    qa_guest_throw_processor(decoder->memory->throw_ctx, operand->segment == QA_SEG_SS ? 12 : 13, true, 0,
                             "%s segment limit exceeded", qa_guest_segment_name(operand->segment));
  return qa_x86_guest_address(decoder->memory, as_uint32(segment->base + operand->offset), access, width_bytes);
}

uint64_t qa_x86_decoder_read(qa_x86_decoder *decoder, const qa_x86_operand *operand, int width) {
  if (!operand->is_memory)
    return qa_integer_register_file_read(&decoder->state->registers, operand->reg, width, operand->high_byte);
  qa_guest_core_address address = qa_x86_decoder_address(decoder, operand, (size_t)(width / 8), QA_ACCESS_READ);
  switch (width) {
    case 8: return qa_sparse_read_u8(decoder->memory, address);
    case 16: return qa_sparse_read_u16(decoder->memory, address);
    default: return qa_sparse_read_u32(decoder->memory, address);
  }
}
void qa_x86_decoder_check_write(qa_x86_decoder *decoder, const qa_x86_operand *operand, int width) {
  if (operand->is_memory)
    qa_sparse_check(decoder->memory, qa_x86_decoder_address(decoder, operand, (size_t)(width / 8), QA_ACCESS_WRITE),
                    (size_t)(width / 8), QA_ACCESS_WRITE);
}
void qa_x86_decoder_write(qa_x86_decoder *decoder, const qa_x86_operand *operand, int width, uint64_t value) {
  if (!operand->is_memory) {
    qa_integer_register_file_write(&decoder->state->registers, operand->reg, width, value, operand->high_byte);
    return;
  }
  qa_guest_core_address address = qa_x86_decoder_address(decoder, operand, (size_t)(width / 8), QA_ACCESS_WRITE);
  uint64_t normalized = as_uint_n(width, value);
  switch (width) {
    case 8: qa_sparse_write_u8(decoder->memory, address, (uint32_t)normalized); break;
    case 16: qa_sparse_write_u16(decoder->memory, address, (uint32_t)normalized); break;
    default: qa_sparse_write_u32(decoder->memory, address, (uint32_t)normalized); break;
  }
}
