/* Port of src/guest/floating-point/raw-sse.ts. GPL-2.0-or-later. */
#include "qa/guest/floating_point/raw_sse.h"

#include <string.h>

#include "qa/foundation/support.h"

const char *qa_raw_sse_logic_op_name(qa_raw_sse_logic_op op) {
  switch (op) { case QA_SSE_AND: return "and"; case QA_SSE_AND_NOT: return "and-not"; case QA_SSE_OR: return "or"; default: return "xor"; }
}
bool qa_prepare_raw_sse(qa_raw_sse_operation *out, int opcode, qa_numeric_prefix prefix, int register_index) {
  /* Operator precedence preserved from TS (&& binds tighter than ||). */
  if (opcode == 0x10 || opcode == 0x11 || ((opcode == 0x28 || opcode == 0x29) && (prefix == QA_PREFIX_NONE || prefix == QA_PREFIX_66))
      || ((opcode == 0x6f || opcode == 0x7f) && (prefix == QA_PREFIX_66 || prefix == QA_PREFIX_F3))) {
    bool scalar = (opcode == 0x10 || opcode == 0x11) && (prefix == QA_PREFIX_F2 || prefix == QA_PREFIX_F3);
    out->kind = QA_RAW_SSE_MOVE;
    out->register_index = register_index;
    out->size = scalar ? (prefix == QA_PREFIX_F2 ? 8 : 4) : 16;
    out->store = opcode == 0x11 || opcode == 0x29 || opcode == 0x7f;
    out->aligned = opcode == 0x28 || opcode == 0x29 || ((opcode == 0x6f || opcode == 0x7f) && prefix == QA_PREFIX_66);
    out->operation = QA_SSE_AND;
    return true;
  }
  if ((opcode >= 0x54 && opcode <= 0x57 && (prefix == QA_PREFIX_NONE || prefix == QA_PREFIX_66))
      || ((opcode == 0xdb || opcode == 0xdf || opcode == 0xeb || opcode == 0xef) && prefix == QA_PREFIX_66)) {
    out->kind = QA_RAW_SSE_LOGIC;
    out->register_index = register_index;
    out->size = 16; out->store = false; out->aligned = false;
    out->operation = opcode == 0x54 || opcode == 0xdb ? QA_SSE_AND
                     : opcode == 0x55 || opcode == 0xdf ? QA_SSE_AND_NOT
                     : opcode == 0x56 || opcode == 0xeb ? QA_SSE_OR : QA_SSE_XOR;
    return true;
  }
  return false;
}
static bool valid_register(size_t byte_len, int index) {
  return index >= 0 && (size_t)(index + 1) * 16u <= byte_len;
}
static uint32_t load32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void store32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

void qa_execute_raw_sse(qa_numeric_result *out, const qa_raw_sse_operation *operation,
                        const qa_numeric_operand *operand, qa_guest_processor_state *state,
                        qa_mapped_guest_memory *memory) {
  uint8_t *registers = state->simd.xmm;
  size_t byte_len = state->simd.xmm_len;
  int destination = operation->register_index * 16;
  if (!valid_register(byte_len, operation->register_index)) {
    qa_numeric_set_result(out, QA_NUMERIC_UNSUPPORTED, 0, "Invalid XMM register index"); return;
  }
  if (operand->kind == QA_OPERAND_MEMORY && (operation->kind == QA_RAW_SSE_LOGIC || operation->aligned)
      && operand->address.byte_offset % 16u != 0) {
    qa_numeric_set_result(out, QA_NUMERIC_EXCEPTION, 13, "Unaligned 16-byte SIMD operand"); return;
  }
  if (operand->kind == QA_OPERAND_REGISTER && !valid_register(byte_len, operand->index)) {
    qa_numeric_set_result(out, QA_NUMERIC_UNSUPPORTED, 0, "Invalid XMM register index"); return;
  }
  if (operation->kind == QA_RAW_SSE_MOVE) {
    int size = operation->size;
    if (operand->kind == QA_OPERAND_MEMORY && size < 16 && memory->managed_sparse) {
      if (operation->store) {
        uint32_t low = load32(registers + destination);
        if (size == 4) qa_memory_write_u32(memory, operand->address, low);
        else qa_memory_write_u64_words(memory, operand->address, low, load32(registers + destination + 4));
      } else {
        if (size == 4) {
          uint32_t value = qa_memory_read_u32(memory, operand->address);
          store32(registers + destination, value);
          store32(registers + destination + 4, 0);
        } else {
          uint32_t low, high;
          qa_memory_read_u64_words(memory, operand->address, &low, &high);
          store32(registers + destination, low);
          store32(registers + destination + 4, high);
        }
        store32(registers + destination + 8, 0);
        store32(registers + destination + 12, 0);
      }
    } else if (operand->kind == QA_OPERAND_REGISTER) {
      int other = operand->index * 16;
      /* Uint8Array.copyWithin semantics: memmove. */
      if (operation->store) memmove(registers + other, registers + destination, (size_t)size);
      else memmove(registers + destination, registers + other, (size_t)size);
    } else if (operation->store) {
      qa_memory_write(memory, operand->address, registers + destination, (size_t)size);
    } else {
      qa_memory_copy_into(memory, operand->address, registers, (size_t)destination, (size_t)size);
      if (size < 16) memset(registers + destination + size, 0, (size_t)(16 - size));
    }
    qa_numeric_set_result(out, QA_NUMERIC_EXECUTED, 0, "");
    return;
  }
  const uint8_t *right;
  int right_offset = 0;
  uint8_t retained_local[16];
  if (operand->kind == QA_OPERAND_REGISTER) { right = registers; right_offset = operand->index * 16; }
  else {
    if (!state->simd.sse_scratch_valid) { memset(state->simd.sse_scratch, 0, 16); state->simd.sse_scratch_valid = true; }
    qa_memory_copy_into(memory, operand->address, state->simd.sse_scratch, 0, 16);
    memcpy(retained_local, state->simd.sse_scratch, 16);
    right = retained_local;
  }
  for (int index = 0; index < 16; index++) {
    uint8_t lb = registers[destination + index], rb = right[right_offset + index];
    registers[destination + index] = operation->operation == QA_SSE_AND ? (uint8_t)(lb & rb)
      : operation->operation == QA_SSE_AND_NOT ? (uint8_t)(~lb & rb)
      : operation->operation == QA_SSE_OR ? (uint8_t)(lb | rb) : (uint8_t)(lb ^ rb);
  }
  qa_numeric_set_result(out, QA_NUMERIC_EXECUTED, 0, "");
}
