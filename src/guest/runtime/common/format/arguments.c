/* Port of src/guest/runtime/common/format/arguments.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/common/format/arguments.h"

#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/memory.h"

static void type_error(const char *message) { qa_range_error("%s", message); }

const char *qa_format_dialect_name(qa_format_dialect dialect) {
  return dialect == QA_FORMAT_WINDOWS ? "windows" : "system-v";
}
const char *qa_format_argument_type_name(qa_format_argument_type type) {
  switch (type) {
    case QA_FORMAT_ARG_INT: return "int";
    case QA_FORMAT_ARG_INT64: return "int64";
    case QA_FORMAT_ARG_POINTER: return "pointer";
    case QA_FORMAT_ARG_DOUBLE: return "double";
    case QA_FORMAT_ARG_LONG_DOUBLE: return "long-double";
  }
  return "";
}
void qa_format_argument_free(qa_format_argument *argument) {
  if (argument != NULL && argument->is_float) qa_binary_value_free(&argument->float_value);
}

void qa_format_arguments_init(qa_format_arguments *args, qa_mapped_guest_memory *memory,
                              qa_format_dialect dialect, bool has_address, qa_guest_core_address address) {
  memset(args, 0, sizeof *args);
  args->memory = memory;
  args->dialect = dialect;
  args->has_descriptor = dialect == QA_FORMAT_SYSTEM_V && memory->pointer_bytes == 8 && has_address;
  if (args->has_descriptor) {
    args->descriptor = address;
    args->gp = qa_memory_read_u32(memory, address);
    args->fp = qa_memory_read_u32(memory, qa_memory_offset(memory, address, 4));
    args->has_cursor = qa_gmem_read_pointer(memory, qa_memory_offset(memory, address, 8), &args->cursor);
    args->has_registers = qa_gmem_read_pointer(memory, qa_memory_offset(memory, address, 16), &args->registers);
    if (args->gp > 48 || args->gp % 8 != 0 || args->fp < 48 || args->fp > 176 || (args->fp - 48) % 16 != 0)
      qa_range_error("Invalid System V x64 va_list register offsets");
  } else {
    args->has_descriptor = false;
    args->has_cursor = has_address;
    args->cursor = address;
  }
}

qa_format_argument qa_format_arguments_next(qa_format_arguments *args, qa_format_argument_type type) {
  qa_mapped_guest_memory *m = args->memory;
  bool extended = type == QA_FORMAT_ARG_LONG_DOUBLE && args->dialect == QA_FORMAT_SYSTEM_V;
  bool floating = type == QA_FORMAT_ARG_DOUBLE || type == QA_FORMAT_ARG_LONG_DOUBLE;
  int size = extended ? (m->pointer_bytes == 8 ? 16 : 12)
      : (floating || type == QA_FORMAT_ARG_INT64) ? 8
      : type == QA_FORMAT_ARG_POINTER ? m->pointer_bytes : 4;
  qa_guest_core_address address;
  if (args->has_descriptor && !extended && (floating ? args->fp < 176 : args->gp < 48)) {
    if (!args->has_registers) type_error("va_list register save area is null");
    address = qa_memory_offset(m, args->registers, floating ? (int64_t)args->fp : (int64_t)args->gp);
    if (floating) {
      args->fp += 16;
      qa_memory_write_u32(m, qa_memory_offset(m, args->descriptor, 4), args->fp);
    } else {
      args->gp += 8;
      qa_memory_write_u32(m, args->descriptor, args->gp);
    }
  } else {
    if (!args->has_cursor) type_error("va_list argument storage is null");
    uint64_t alignment = (args->has_descriptor && extended) ? 16 : (uint64_t)m->pointer_bytes;
    uint64_t aligned = (args->cursor.byte_offset + alignment - 1) / alignment * alignment;
    address = qa_memory_offset(m, args->cursor, (int64_t)(aligned - args->cursor.byte_offset));
    int slot = m->pointer_bytes == 8 ? ((size + 7) / 8) * 8 : ((size + 3) / 4) * 4;
    args->cursor = qa_memory_offset(m, address, slot);
    args->has_cursor = true;
    if (args->has_descriptor)
      qa_gmem_write_pointer(m, qa_memory_offset(m, args->descriptor, 8), true, args->cursor);
  }
  qa_format_argument result; memset(&result, 0, sizeof result);
  if (floating) {
    result.is_float = true;
    qa_bigint bits; qa_bigint_init(&bits);
    if (extended) {
      uint8_t bytes[10];
      qa_memory_copy(m, address, bytes, 10);
      qa_fp_read_bits(&bits, bytes, 10);
      result.float_value = qa_fp_decode_binary(&bits, 80);
    } else {
      qa_bigint_set_u64(&bits, qa_read_unsigned(m, address, 8));
      result.float_value = qa_fp_decode_binary(&bits, 64);
    }
    qa_bigint_free(&bits);
  } else {
    result.integer = qa_read_unsigned(m, address, size);
  }
  return result;
}
