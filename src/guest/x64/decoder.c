/* Port of src/guest/x64/decoder.ts. GPL-2.0-or-later. */
#include "qa/guest/x64/decoder.h"

#include <string.h>

#include "qa/foundation/support.h"

const char *qa_x64_repeat_name(qa_x64_repeat repeat) {
  switch (repeat) {
    case QA_X64_REPEAT_F2: return "f2";
    case QA_X64_REPEAT_F3: return "f3";
    default: return "none";
  }
}

static uint64_t as_uint_n(int width, uint64_t v) {
  return width >= 64 ? v : v & ((1ull << width) - 1ull);
}
static int64_t as_int_n(int width, uint64_t v) {
  v = as_uint_n(width, v);
  if (width < 64 && ((v >> (width - 1)) & 1ull) != 0) return (int64_t)v - (int64_t)(1ull << width);
  return (int64_t)v;
}

qa_guest_register qa_x64_register_name_at(qa_guest_throw *throw_ctx, int index) {
  if (index < 0 || index > 15)
    qa_guest_throw_processor(throw_ctx, 6, false, 0, "Invalid register index %d", index);
  return (qa_guest_register)index;
}

uint64_t qa_x64_canonical_address(qa_guest_throw *throw_ctx, uint64_t value) {
  if (value <= 0x7fffffffffffull) return value;
  if (value > 0x7fffffffffffull && value < 0xffff800000000000ull)
    qa_guest_throw_processor(throw_ctx, 13, false, 0, "Noncanonical 48-bit virtual address 0x%llx",
                             (unsigned long long)value);
  return value;
}

qa_guest_address qa_x64_guest_address(qa_sparse_guest_memory *memory, uint64_t raw, qa_guest_access access) {
  uint64_t value = qa_x64_canonical_address(memory->throw_ctx, raw);
  qa_guest_fault_access fa = access == QA_ACCESS_READ ? QA_FAULT_ACCESS_READ
                             : access == QA_ACCESS_WRITE ? QA_FAULT_ACCESS_WRITE : QA_FAULT_ACCESS_EXECUTE;
  if (value == 0)
    qa_guest_throw_memory(memory->throw_ctx, QA_FAULT_NULL_ADDRESS, &memory->module, value, 1, fa,
                          "null guest address");
  if (memory->base.pointer_bytes == 8) {
    qa_guest_address address = {value};
    return address;
  }
  qa_guest_address address;
  if (!qa_sparse_pointer(memory, value, &address))
    qa_guest_throw_memory(memory->throw_ctx, QA_FAULT_NULL_ADDRESS, &memory->module, value, 1, fa,
                          "null guest address");
  return address;
}

uint64_t qa_x64_read_memory(qa_sparse_guest_memory *memory, qa_guest_address address, int width) {
  switch (width) {
    case 8: return qa_sparse_read_u8(memory, address);
    case 16: return qa_sparse_read_u16(memory, address);
    case 32: return qa_sparse_read_u32(memory, address);
    default: return qa_sparse_read_u64(memory, address);
  }
}
void qa_x64_write_memory(qa_sparse_guest_memory *memory, qa_guest_address address, int width, uint64_t value) {
  uint64_t normalized = as_uint_n(width, value);
  switch (width) {
    case 8: qa_sparse_write_u8(memory, address, (uint32_t)normalized); break;
    case 16: qa_sparse_write_u16(memory, address, (uint32_t)normalized); break;
    case 32: qa_sparse_write_u32(memory, address, (uint32_t)normalized); break;
    default: qa_sparse_write_u64(memory, address, normalized); break;
  }
}

uint64_t qa_x64_effective_operand_offset(const qa_guest_processor_state *state, const qa_x64_operand *operand, uint64_t next_ip) {
  uint64_t base = operand->rip_relative ? next_ip
                  : !operand->has_base ? 0
                  : qa_integer_register_file_read(&state->registers, operand->base, operand->address_bits, false);
  uint64_t index = !operand->has_index ? 0
                   : qa_integer_register_file_read(&state->registers, operand->index, operand->address_bits, false) *
                         operand->scale;
  return as_uint_n(operand->address_bits, base + index + (uint64_t)operand->displacement);
}
qa_guest_address qa_x64_operand_address(qa_sparse_guest_memory *memory, const qa_guest_processor_state *state,
                                        const qa_x64_operand *operand, uint64_t next_ip, qa_guest_access access) {
  uint64_t segment_base = operand->segment == QA_X64_SEG_NONE ? 0
                          : state->segments[operand->segment == QA_X64_SEG_FS ? QA_SEG_FS : QA_SEG_GS].base;
  return qa_x64_guest_address(memory, qa_x64_effective_operand_offset(state, operand, next_ip) + segment_base, access);
}
uint64_t qa_x64_read_operand(qa_sparse_guest_memory *memory, const qa_guest_processor_state *state,
                             const qa_x64_operand *operand, uint64_t next_ip) {
  return operand->kind == QA_X64_OPERAND_REGISTER
             ? qa_integer_register_file_read(&state->registers, operand->reg, operand->width, operand->high_byte)
             : qa_x64_read_memory(memory, qa_x64_operand_address(memory, state, operand, next_ip, QA_ACCESS_READ),
                                  operand->width);
}
void qa_x64_write_operand(qa_sparse_guest_memory *memory, qa_guest_processor_state *state,
                          const qa_x64_operand *operand, uint64_t next_ip, uint64_t value) {
  if (operand->kind == QA_X64_OPERAND_REGISTER)
    qa_integer_register_file_write(&state->registers, operand->reg, operand->width, value, operand->high_byte);
  else
    qa_x64_write_memory(memory, qa_x64_operand_address(memory, state, operand, next_ip, QA_ACCESS_WRITE),
                        operand->width, value);
}
void qa_x64_writable_operand(qa_sparse_guest_memory *memory, const qa_guest_processor_state *state,
                             const qa_x64_operand *operand, uint64_t next_ip) {
  if (operand->kind == QA_X64_OPERAND_MEMORY)
    qa_sparse_check(memory, qa_x64_operand_address(memory, state, operand, next_ip, QA_ACCESS_WRITE),
                    (size_t)(operand->width / 8), QA_ACCESS_WRITE);
}

bool qa_x64_decoded_unchanged(qa_x64_decoded_instruction *decoded) {
  return qa_guest_executable_guard_unchanged(&decoded->guard);
}

static qa_guest_throw *cursor_throw(qa_x64_decode_cursor *cursor) { return cursor->memory->throw_ctx; }

uint64_t qa_x64_cursor_start(const qa_x64_decode_cursor *cursor) { return cursor->start; }
int qa_x64_cursor_opcode(const qa_x64_decode_cursor *cursor) { return cursor->opcode_value; }
uint64_t qa_x64_cursor_next_ip(const qa_x64_decode_cursor *cursor) {
  return cursor->start + (uint64_t)cursor->position; /* BigInt.asUintN(64, ...) */
}
int qa_x64_cursor_width(const qa_x64_decode_cursor *cursor) {
  return ((cursor->has_rex ? cursor->rex : 0) & 8) != 0 ? 64 : cursor->operand_override ? 16 : 32;
}
int qa_x64_cursor_stack_width(const qa_x64_decode_cursor *cursor) { return cursor->operand_override ? 16 : 64; }
int qa_x64_cursor_address_bits(const qa_x64_decode_cursor *cursor) { return cursor->address_override ? 32 : 64; }
int qa_x64_cursor_rex_b(const qa_x64_decode_cursor *cursor) { return ((cursor->has_rex ? cursor->rex : 0) & 1) != 0 ? 8 : 0; }
int qa_x64_cursor_rex_x(const qa_x64_decode_cursor *cursor) { return ((cursor->has_rex ? cursor->rex : 0) & 2) != 0 ? 8 : 0; }
int qa_x64_cursor_rex_r(const qa_x64_decode_cursor *cursor) { return ((cursor->has_rex ? cursor->rex : 0) & 4) != 0 ? 8 : 0; }
qa_numeric_prefix qa_x64_cursor_numeric_prefix(const qa_x64_decode_cursor *cursor) {
  return cursor->repeat == QA_X64_REPEAT_F2 ? QA_PREFIX_F2
         : cursor->repeat == QA_X64_REPEAT_F3 ? QA_PREFIX_F3
         : cursor->operand_override ? QA_PREFIX_66 : QA_PREFIX_NONE;
}
size_t qa_x64_cursor_bytes(const qa_x64_decode_cursor *cursor, uint8_t out[15]) {
  if (cursor->decoded != NULL) {
    size_t n = (size_t)cursor->position;
    memcpy(out, cursor->decoded->bytes, n);
    return n;
  }
  memcpy(out, cursor->record_bytes, cursor->record_byte_count);
  return cursor->record_byte_count;
}

int qa_x64_cursor_read_byte(qa_x64_decode_cursor *cursor) {
  if (cursor->position >= 15)
    qa_guest_throw_processor(cursor_throw(cursor), 13, false, 0, "Instruction exceeds 15 bytes");
  if (cursor->decoded != NULL) {
    if ((size_t)cursor->position >= cursor->decoded->byte_count)
      qa_range_error("Decoded instruction byte is missing");
    return cursor->decoded->bytes[cursor->position++];
  }
  int byte = !cursor->has_fetch
                 ? (int)qa_sparse_fetch_byte(cursor->memory,
                         qa_x64_canonical_address(cursor_throw(cursor), qa_x64_cursor_next_ip(cursor)))
                 : (int)qa_guest_fetch_sequence_next(&cursor->fetch);
  cursor->record_bytes[cursor->record_byte_count++] = (uint8_t)byte;
  cursor->position++;
  return byte;
}

void qa_x64_decode_cursor_reset(qa_x64_decode_cursor *cursor, qa_x64_decoded_instruction *decoded) {
  cursor->start = cursor->state->instruction_pointer;
  cursor->has_plan = false;
  cursor->decoded = decoded;
  cursor->record_byte_count = 0;
  memset(cursor->record_operands, 0, sizeof cursor->record_operands);
  memset(cursor->record_immediates, 0, sizeof cursor->record_immediates);
  if (decoded != NULL) {
    cursor->has_fetch = false;
    cursor->prefix_length = decoded->prefix_length;
    cursor->position = decoded->prefix_length;
    cursor->opcode_value = decoded->opcode;
    cursor->has_rex = decoded->has_rex;
    cursor->rex = decoded->rex;
    cursor->operand_override = decoded->operand_override;
    cursor->address_override = decoded->address_override;
    cursor->lock = decoded->lock;
    cursor->repeat = decoded->repeat;
    cursor->segment = decoded->segment;
    return;
  }
  cursor->position = 0;
  cursor->has_rex = false;
  cursor->rex = 0;
  cursor->operand_override = false;
  cursor->address_override = false;
  cursor->lock = false;
  cursor->repeat = QA_X64_REPEAT_NONE;
  cursor->segment = QA_X64_SEG_NONE;
  cursor->has_fetch = (cursor->start > 0 && cursor->start <= 0x7ffffffffff1ull) ||
                      (cursor->start >= 0xffff800000000000ull && cursor->start <= 0xfffffffffffffff1ull);
  if (cursor->has_fetch) qa_sparse_fetch_sequence_init(&cursor->fetch, cursor->memory, cursor->start);
  for (;;) {
    int byte = qa_x64_cursor_read_byte(cursor);
    if (byte >= 0x40 && byte <= 0x4f) { cursor->has_rex = true; cursor->rex = byte; continue; }
    if (byte == 0x66) cursor->operand_override = true;
    else if (byte == 0x67) cursor->address_override = true;
    else if (byte == 0xf0) cursor->lock = true;
    else if (byte == 0xf2) cursor->repeat = QA_X64_REPEAT_F2;
    else if (byte == 0xf3) cursor->repeat = QA_X64_REPEAT_F3;
    else if (byte == 0x64) cursor->segment = QA_X64_SEG_FS;
    else if (byte == 0x65) cursor->segment = QA_X64_SEG_GS;
    else if (byte == 0x2e || byte == 0x36 || byte == 0x3e || byte == 0x26) cursor->segment = QA_X64_SEG_NONE;
    else { cursor->opcode_value = byte; break; }
    cursor->has_rex = false;
    cursor->rex = 0;
  }
  cursor->prefix_length = cursor->position;
}
void qa_x64_decode_cursor_init(qa_x64_decode_cursor *cursor, qa_sparse_guest_memory *memory,
                               qa_guest_processor_state *state, qa_x64_decoded_instruction *decoded) {
  memset(cursor, 0, sizeof *cursor);
  cursor->memory = memory;
  cursor->state = state;
  qa_x64_decode_cursor_reset(cursor, decoded);
}

bool qa_x64_cursor_cache(qa_x64_decode_cursor *cursor, qa_x64_decoded_instruction *out) {
  if (cursor->decoded != NULL) { *out = *cursor->decoded; return true; }
  memset(out, 0, sizeof *out);
  if (!qa_sparse_retain_executable_bytes(cursor->memory, cursor->start, cursor->record_bytes,
                                         cursor->record_byte_count, &out->guard)) return false;
  memcpy(out->bytes, cursor->record_bytes, cursor->record_byte_count);
  out->byte_count = cursor->record_byte_count;
  out->prefix_length = cursor->prefix_length;
  out->opcode = cursor->opcode_value;
  out->has_rex = cursor->has_rex;
  out->rex = cursor->rex;
  out->operand_override = cursor->operand_override;
  out->address_override = cursor->address_override;
  out->lock = cursor->lock;
  out->repeat = cursor->repeat;
  out->segment = cursor->segment;
  memcpy(out->operands, cursor->record_operands, sizeof out->operands);
  memcpy(out->immediates, cursor->record_immediates, sizeof out->immediates);
  return true;
}

static uint64_t unsigned_bytes(qa_x64_decode_cursor *cursor, int byte_length) {
  uint64_t result = 0;
  for (int index = 0; index < byte_length; index++)
    result |= (uint64_t)qa_x64_cursor_read_byte(cursor) << (index * 8);
  return result;
}
uint64_t qa_x64_cursor_read_unsigned(qa_x64_decode_cursor *cursor, int byte_length) {
  int start = cursor->position;
  if (cursor->decoded != NULL && start < 15) {
    const qa_x64_decoded_immediate_record *saved = &cursor->decoded->immediates[start];
    if (saved->valid && saved->length == byte_length) {
      cursor->position += byte_length;
      return saved->value;
    }
  }
  uint64_t value = unsigned_bytes(cursor, byte_length);
  if (cursor->decoded == NULL && start < 15) {
    cursor->record_immediates[start].valid = true;
    cursor->record_immediates[start].length = byte_length;
    cursor->record_immediates[start].value = value;
  }
  return value;
}
int64_t qa_x64_cursor_read_signed(qa_x64_decode_cursor *cursor, int byte_length) {
  return as_int_n(byte_length * 8, qa_x64_cursor_read_unsigned(cursor, byte_length));
}
uint64_t qa_x64_cursor_immediate(qa_x64_decode_cursor *cursor, int width) {
  return width == 64 ? (uint64_t)qa_x64_cursor_read_signed(cursor, 4)
                     : qa_x64_cursor_read_unsigned(cursor, width / 8);
}

static qa_x64_operand register_operand(qa_guest_register reg, int width, bool high_byte) {
  qa_x64_operand operand;
  memset(&operand, 0, sizeof operand);
  operand.kind = QA_X64_OPERAND_REGISTER;
  operand.width = width;
  operand.reg = reg;
  operand.high_byte = high_byte;
  operand.scale = 1;
  operand.address_bits = 64;
  return operand;
}
qa_x64_operand qa_x64_cursor_register(qa_x64_decode_cursor *cursor, int index, int width) {
  if (width == 8 && !cursor->has_rex && index >= 4 && index < 8)
    return register_operand(qa_x64_register_name_at(cursor_throw(cursor), index - 4), width, true);
  return register_operand(qa_x64_register_name_at(cursor_throw(cursor), index), width, false);
}

static qa_x64_modrm modrm_impl(qa_x64_decode_cursor *cursor, int width) {
  int byte = qa_x64_cursor_read_byte(cursor);
  int mode = byte >> 6;
  int extension = (byte >> 3) & 7;
  int low_rm = byte & 7;
  int register_index = extension + qa_x64_cursor_rex_r(cursor);
  int rm_index = low_rm + qa_x64_cursor_rex_b(cursor);
  qa_x64_modrm result;
  memset(&result, 0, sizeof result);
  result.byte = byte;
  result.extension = extension;
  result.register_index = register_index;
  result.rm_index = rm_index;
  result.reg = qa_x64_cursor_register(cursor, register_index, width);
  if (mode == 3) {
    result.rm = qa_x64_cursor_register(cursor, rm_index, width);
    return result;
  }
  qa_x64_operand memory;
  memset(&memory, 0, sizeof memory);
  memory.kind = QA_X64_OPERAND_MEMORY;
  memory.width = width;
  memory.scale = 1;
  memory.address_bits = qa_x64_cursor_address_bits(cursor);
  memory.segment = cursor->segment;
  int64_t displacement = 0;
  if (low_rm == 4) {
    int sib = qa_x64_cursor_read_byte(cursor);
    int low_base = sib & 7;
    int low_index = (sib >> 3) & 7;
    memory.scale = 1ull << (sib >> 6);
    if (low_index != 4 || qa_x64_cursor_rex_x(cursor) != 0) {
      memory.has_index = true;
      memory.index = qa_x64_register_name_at(cursor_throw(cursor), low_index + qa_x64_cursor_rex_x(cursor));
    }
    if (mode == 0 && low_base == 5) displacement = qa_x64_cursor_read_signed(cursor, 4);
    else {
      memory.has_base = true;
      memory.base = qa_x64_register_name_at(cursor_throw(cursor), low_base + qa_x64_cursor_rex_b(cursor));
    }
  } else if (mode == 0 && low_rm == 5) {
    memory.rip_relative = true;
    displacement = qa_x64_cursor_read_signed(cursor, 4);
  } else {
    memory.has_base = true;
    memory.base = qa_x64_register_name_at(cursor_throw(cursor), rm_index);
  }
  if (mode == 1) displacement = qa_x64_cursor_read_signed(cursor, 1);
  else if (mode == 2) displacement = qa_x64_cursor_read_signed(cursor, 4);
  memory.displacement = displacement;
  result.rm = memory;
  return result;
}

qa_x64_modrm qa_x64_cursor_decode_modrm(qa_x64_decode_cursor *cursor, int width) {
  int start = cursor->position;
  if (cursor->decoded != NULL && start < 15) {
    const qa_x64_decoded_operand_record *saved = &cursor->decoded->operands[start];
    if (saved->valid && saved->width == width) {
      cursor->position = saved->end;
      return saved->value;
    }
  }
  qa_x64_modrm value = modrm_impl(cursor, width);
  if (cursor->decoded == NULL && start < 15) {
    cursor->record_operands[start].valid = true;
    cursor->record_operands[start].width = width;
    cursor->record_operands[start].end = cursor->position;
    cursor->record_operands[start].value = value;
  }
  return value;
}

static uint64_t cursor_next_for(const qa_x64_decode_cursor *cursor, const qa_x64_operand *operand) {
  return operand->kind == QA_X64_OPERAND_MEMORY && operand->rip_relative ? qa_x64_cursor_next_ip(cursor) : 0;
}
uint64_t qa_x64_cursor_effective_offset(qa_x64_decode_cursor *cursor, const qa_x64_operand *operand,
                                        bool has_next_ip, uint64_t next_ip) {
  return qa_x64_effective_operand_offset(cursor->state, operand,
                                         operand->rip_relative ? (has_next_ip ? next_ip : qa_x64_cursor_next_ip(cursor)) : 0);
}
qa_guest_address qa_x64_cursor_address(qa_x64_decode_cursor *cursor, const qa_x64_operand *operand, qa_guest_access access) {
  return qa_x64_operand_address(cursor->memory, cursor->state, operand,
                                operand->rip_relative ? qa_x64_cursor_next_ip(cursor) : 0, access);
}
uint64_t qa_x64_cursor_read(qa_x64_decode_cursor *cursor, const qa_x64_operand *operand) {
  return qa_x64_read_operand(cursor->memory, cursor->state, operand, cursor_next_for(cursor, operand));
}
void qa_x64_cursor_write(qa_x64_decode_cursor *cursor, const qa_x64_operand *operand, uint64_t value) {
  qa_x64_write_operand(cursor->memory, cursor->state, operand, cursor_next_for(cursor, operand), value);
}
void qa_x64_cursor_writable(qa_x64_decode_cursor *cursor, const qa_x64_operand *operand) {
  qa_x64_writable_operand(cursor->memory, cursor->state, operand, cursor_next_for(cursor, operand));
}

