/* Port of src/guest/runtime/system-v/stdio.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/system_v/stdio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/memory.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Guest runtime out of memory");
  return p;
}
static void plain_error(const char *message) { qa_range_error("%s", message); }
static qa_guest_core_address off(const qa_mapped_guest_memory *m, qa_guest_core_address a, int64_t d) {
  return qa_memory_offset(m, a, d);
}
static bool read_ptr(const qa_mapped_guest_memory *m, qa_guest_core_address a, qa_guest_core_address *out) {
  return qa_gmem_read_pointer(m, a, out);
}
static void write_ptr(const qa_mapped_guest_memory *m, qa_guest_core_address a, bool has,
                      qa_guest_core_address v) {
  qa_gmem_write_pointer(m, a, has, v);
}
static qa_guest_core_address field(qa_system_v_stdio *stdio, qa_guest_core_address file, int pointer_index) {
  return off(stdio->host->memory, file,
             (int64_t)pointer_index * stdio->host->memory->pointer_bytes);
}
static qa_call_result size_result(qa_system_v_stdio *stdio, size_t value) {
  return stdio->host->memory->pointer_bytes == 4 ? qa_call_uint32((uint32_t)value)
                                                : qa_call_uint64(value);
}

qa_system_v_file_layout qa_system_v_file_layout_for(int width) {
  if (width == 8) {
    qa_system_v_file_layout layout = {216, 112, 192, 136, 144, 160, 120};
    return layout;
  }
  qa_system_v_file_layout layout = {148, 56, 104, 72, 76, 88, 64};
  return layout;
}

static qa_guest_core_address create_file(qa_system_v_stdio *stdio, int descriptor, bool has_chain,
                                    qa_guest_core_address chain) {
  qa_system_v_service_host *host = stdio->host;
  qa_mapped_guest_memory *m = host->memory;
  qa_guest_core_address file = qa_system_v_allocate(host, (size_t)stdio->layout.size + (size_t)m->pointer_bytes);
  uint32_t flags = 0xfbad0000u | 0x2080u | (descriptor == 0 ? 8u : 4u) | (descriptor == 2 ? 2u : 0u);
  qa_memory_write_u32(m, file, flags);
  write_ptr(m, field(stdio, file, 13), has_chain, chain);
  qa_gmem_write_i32(m, off(m, file, stdio->layout.descriptor), descriptor);
  qa_gmem_write_i64(m, off(m, file, stdio->layout.offset), -1);
  if (m->pointer_bytes == 8) qa_gmem_write_i64(m, off(m, file, stdio->layout.old_offset), -1);
  else qa_gmem_write_i32(m, off(m, file, stdio->layout.old_offset), -1);
  qa_guest_core_address a2 = off(m, file, stdio->layout.lock);
  qa_guest_core_address v2 = qa_system_v_allocate(host, (size_t)m->pointer_bytes * 2 + 8);
  write_ptr(m, a2, true, v2);
  /* _IO_wide_data starts with eleven wchar_t pointers and an mbstate_t. */
  qa_guest_core_address a = off(m, file, stdio->layout.wide_data);
  qa_guest_core_address v = qa_system_v_allocate(host, m->pointer_bytes == 8 ? 312 : 180);
  write_ptr(m, a, true, v);
  return file;
}
static int descriptor_of(qa_system_v_stdio *stdio, qa_guest_core_address file) {
  qa_mapped_guest_memory *m = stdio->host->memory;
  if ((qa_memory_read_u32(m, file) & 0xffff0000u) != 0xfbad0000u)
    qa_range_error("Invalid guest FILE magic");
  return qa_gmem_read_i32(m, off(m, file, stdio->layout.descriptor));
}
static bool orient(qa_system_v_stdio *stdio, qa_guest_core_address file, bool wide) {
  qa_mapped_guest_memory *m = stdio->host->memory;
  qa_guest_core_address mode = off(m, file, stdio->layout.mode);
  int desired = wide ? 1 : -1;
  int current = qa_gmem_read_i32(m, mode);
  if (current == 0) qa_gmem_write_i32(m, mode, desired);
  return current == 0 || current == desired;
}
static int file_error(qa_system_v_stdio *stdio, qa_guest_core_address file, int errno_value) {
  qa_system_v_errno_set(stdio->host, errno_value);
  qa_mapped_guest_memory *m = stdio->host->memory;
  qa_memory_write_u32(m, file, qa_memory_read_u32(m, file) | 0x20u);
  return -1;
}
static qa_guest_core_address output_buffer(qa_system_v_stdio *stdio, qa_guest_core_address file) {
  qa_mapped_guest_memory *m = stdio->host->memory;
  qa_guest_core_address existing = {0};
  if (read_ptr(m, field(stdio, file, 7), &existing)) return existing;
  qa_guest_core_address start = qa_system_v_allocate(stdio->host, 8192);
  qa_guest_core_address end = off(m, start, 8192);
  const int starts[] = {4, 5, 7};
  const int ends[] = {6, 8};
  for (size_t i = 0; i < 3; i++) write_ptr(m, field(stdio, file, starts[i]), true, start);
  for (size_t i = 0; i < 2; i++) write_ptr(m, field(stdio, file, ends[i]), true, end);
  if (descriptor_of(stdio, file) == 1 && stdio->host->capabilities.output_is_terminal)
    qa_memory_write_u32(m, file, qa_memory_read_u32(m, file) | 0x200u);
  return start;
}

int qa_system_v_stdio_flush(qa_system_v_stdio *stdio, qa_guest_core_address file,
                            const qa_guest_core_call_context *context) {
  (void)context;
  qa_mapped_guest_memory *m = stdio->host->memory;
  int descriptor = descriptor_of(stdio, file);
  qa_guest_core_address base = {0}, next = {0};
  bool has_base = read_ptr(m, field(stdio, file, 4), &base);
  bool has_next = read_ptr(m, field(stdio, file, 5), &next);
  if (descriptor == 0) return 0;
  if (descriptor != 1 && descriptor != 2) return file_error(stdio, file, 9);
  if (has_base && has_next && next.byte_offset > base.byte_offset) {
    if (stdio->host->capabilities.standard_output == NULL)
      qa_unsupported_system_v_service("libc.so.6", "fflush", NULL,
                                      "no standard output capability supplied");
    size_t length = (size_t)(next.byte_offset - base.byte_offset);
    uint8_t *bytes = xmalloc(length);
    qa_memory_copy(m, base, bytes, length);
    size_t written = 0;
    while (written < length) {
      int count = stdio->host->capabilities.standard_output(
          stdio->host->capabilities.ctx,
          descriptor == 1 ? QA_SYSTEM_V_STDOUT : QA_SYSTEM_V_STDERR, bytes + written,
          length - written);
      if (count <= 0 || (size_t)count > length - written) {
        qa_memory_write(m, base, bytes + written, length - written);
        write_ptr(m, field(stdio, file, 5), true, off(m, base, (int64_t)(length - written)));
        free(bytes);
        return file_error(stdio, file, 5);
      }
      written += (size_t)count;
    }
    free(bytes);
    write_ptr(m, field(stdio, file, 5), true, base);
  }
  int result = stdio->host->capabilities.standard_flush != NULL
      ? stdio->host->capabilities.standard_flush(
            stdio->host->capabilities.ctx,
            descriptor == 1 ? QA_SYSTEM_V_STDOUT : QA_SYSTEM_V_STDERR)
      : 0;
  return result == 0 ? 0 : file_error(stdio, file, 5);
}
static int put_byte(qa_system_v_stdio *stdio, qa_guest_core_address file, int value,
                    const qa_guest_core_call_context *context) {
  qa_mapped_guest_memory *m = stdio->host->memory;
  if ((qa_memory_read_u32(m, file) & 8u) != 0) return file_error(stdio, file, 9);
  (void)output_buffer(stdio, file);
  qa_guest_core_address next = {0}, end = {0};
  bool has_next = read_ptr(m, field(stdio, file, 5), &next);
  bool has_end = read_ptr(m, field(stdio, file, 6), &end);
  if (!has_next || !has_end) plain_error("Missing FILE output buffer");
  if (next.byte_offset >= end.byte_offset) {
    if (qa_system_v_stdio_flush(stdio, file, context) != 0) return -1;
    if (!read_ptr(m, field(stdio, file, 5), &next)) plain_error("Missing FILE write pointer");
  }
  qa_memory_write_u8(m, next, (uint8_t)value);
  write_ptr(m, field(stdio, file, 5), true, off(m, next, 1));
  uint32_t flags = qa_memory_read_u32(m, file);
  if ((flags & 2u) != 0 || ((flags & 0x200u) != 0 && value == 10)) {
    if (qa_system_v_stdio_flush(stdio, file, context) != 0) return -1;
  }
  return value;
}
int qa_system_v_stdio_put(qa_system_v_stdio *stdio, qa_guest_core_address file, int value, bool wide,
                          const qa_guest_core_call_context *context) {
  (void)descriptor_of(stdio, file);
  if (!orient(stdio, file, wide)) return -1;
  if (wide && (value < 0 || value > 127)) return file_error(stdio, file, 84);
  return put_byte(stdio, file, value & 255, context) < 0 ? -1 : value;
}
size_t qa_system_v_stdio_write(qa_system_v_stdio *stdio, qa_guest_core_address file,
                               const uint8_t *bytes, size_t len,
                               const qa_guest_core_call_context *context) {
  (void)descriptor_of(stdio, file);
  if (!orient(stdio, file, false)) return 0;
  size_t written = 0;
  for (size_t i = 0; i < len; i++) {
    if (put_byte(stdio, file, bytes[i], context) < 0) break;
    written++;
  }
  return written;
}
int qa_system_v_stdio_get(qa_system_v_stdio *stdio, qa_guest_core_address file, bool wide,
                          const qa_guest_core_call_context *context) {
  (void)context;
  qa_mapped_guest_memory *m = stdio->host->memory;
  if (descriptor_of(stdio, file) != 0 || (qa_memory_read_u32(m, file) & 4u) != 0)
    return file_error(stdio, file, 9);
  if (!orient(stdio, file, wide)) return -1;
  qa_guest_core_address next = {0}, end = {0};
  bool has_next = read_ptr(m, field(stdio, file, 1), &next);
  bool has_end = read_ptr(m, field(stdio, file, 2), &end);
  if (!has_next || !has_end || next.byte_offset >= end.byte_offset) {
    if (stdio->host->capabilities.standard_input == NULL)
      qa_unsupported_system_v_service("libc.so.6", "fgetc", NULL,
                                      "no standard input capability supplied");
    uint8_t bytes[8192];
    /* The capability writes at most the requested count into the buffer. */
    size_t length = stdio->host->capabilities.standard_input(stdio->host->capabilities.ctx,
                                                             8192, bytes);
    if (length > 8192) qa_range_error("Input capability exceeded requested count");
    if (length == 0) {
      qa_memory_write_u32(m, file, qa_memory_read_u32(m, file) | 0x10u);
      return -1;
    }
    qa_guest_core_address buffer = {0};
    if (!read_ptr(m, field(stdio, file, 7), &buffer))
      buffer = qa_system_v_allocate(stdio->host, 8192);
    qa_memory_write(m, buffer, bytes, length);
    write_ptr(m, field(stdio, file, 7), true, buffer);
    write_ptr(m, field(stdio, file, 8), true, off(m, buffer, 8192));
    next = buffer;
    end = off(m, buffer, (int64_t)length);
    write_ptr(m, field(stdio, file, 3), true, buffer);
    write_ptr(m, field(stdio, file, 2), true, end);
  }
  uint8_t byte = qa_memory_read_u8(m, next);
  write_ptr(m, field(stdio, file, 1), true, off(m, next, 1));
  if (wide && byte > 127) return file_error(stdio, file, 84);
  return byte;
}
int qa_system_v_stdio_unget(qa_system_v_stdio *stdio, qa_guest_core_address file, int value, bool wide) {
  if (value == -1 || (uint32_t)value == 0xffffffffu || !orient(stdio, file, wide)) return -1;
  qa_mapped_guest_memory *m = stdio->host->memory;
  qa_guest_core_address next = {0}, base = {0};
  bool has_next = read_ptr(m, field(stdio, file, 1), &next);
  bool has_base = read_ptr(m, field(stdio, file, 3), &base);
  if (!has_next || !has_base || next.byte_offset <= base.byte_offset) return -1;
  qa_guest_core_address prior = off(m, next, -1);
  qa_memory_write_u8(m, prior, (uint8_t)(value & 255));
  write_ptr(m, field(stdio, file, 1), true, prior);
  qa_memory_write_u32(m, file, qa_memory_read_u32(m, file) & ~0x10u);
  return value;
}

/* Service implementations. State is the SystemVStdio object. */
typedef struct stdio_state { qa_system_v_stdio *stdio; int kind; char *name; } stdio_state;
enum { S_FFLUSH, S_PUTC, S_GETC, S_UNGETC, S_FWRITE, S_FREAD, S_FWIDE, S_SYNC, S_OVERFLOW };
static qa_call_result stdio_impl(void *ctx, const qa_guest_core_call_context *context,
                                 const qa_call_value *args, size_t count) {
  stdio_state *state = ctx;
  qa_system_v_stdio *stdio = state->stdio;
  qa_mapped_guest_memory *m = stdio->host->memory;
  switch (state->kind) {
    case S_FFLUSH: {
      qa_guest_core_address file = {0};
      if (qa_guest_pointer_argument(args, count, 0, &file))
        return qa_call_int32(qa_system_v_stdio_flush(stdio, file, context));
      int first = qa_system_v_stdio_flush(stdio, stdio->stdout_, context);
      int second = qa_system_v_stdio_flush(stdio, stdio->stderr_, context);
      return qa_call_int32(first == 0 && second == 0 ? 0 : -1);
    }
    case S_PUTC:
      return qa_call_int32(qa_system_v_stdio_put(
          stdio, qa_guest_required_pointer(args, count, 1),
          (int)(qa_guest_integer_u64(args, count, 0) & 255u), false, context));
    case S_GETC:
      return qa_call_int32(qa_system_v_stdio_get(stdio, qa_guest_required_pointer(args, count, 0),
                                                 false, context));
    case S_UNGETC:
      return qa_call_int32(qa_system_v_stdio_unget(
          stdio, qa_guest_required_pointer(args, count, 1), (int)qa_guest_integer(args, count, 0),
          false));
    case S_FWRITE: {
      size_t size = qa_guest_count(args, count, 1);
      size_t total = size * qa_guest_count(args, count, 2);
      size_t written = 0;
      if (total != 0) {
        uint8_t *bytes = xmalloc(total);
        qa_memory_copy(m, qa_guest_required_pointer(args, count, 0), bytes, total);
        written = qa_system_v_stdio_write(stdio, qa_guest_required_pointer(args, count, 3),
                                          bytes, total, context);
        free(bytes);
      }
      return size_result(stdio, size == 0 ? 0 : written / size);
    }
    case S_FREAD: {
      size_t size = qa_guest_count(args, count, 1);
      size_t total = size * qa_guest_count(args, count, 2);
      if (total == 0) return size_result(stdio, 0);
      qa_guest_core_address destination = qa_guest_required_pointer(args, count, 0);
      qa_guest_core_address file = qa_guest_required_pointer(args, count, 3);
      size_t read_count = 0;
      for (; read_count < total; read_count++) {
        int value = qa_system_v_stdio_get(stdio, file, false, context);
        if (value < 0) break;
        qa_memory_write_u8(m, off(m, destination, (int64_t)read_count), (uint8_t)value);
      }
      return size_result(stdio, read_count / size);
    }
    case S_FWIDE: {
      qa_guest_core_address file = qa_guest_required_pointer(args, count, 0);
      qa_guest_core_address slot = off(m, file, stdio->layout.mode);
      if (qa_gmem_read_i32(m, slot) == 0) {
        int64_t value = qa_guest_integer(args, count, 1);
        qa_gmem_write_i32(m, slot, value > 0 ? 1 : value < 0 ? -1 : 0);
      }
      return qa_call_int32(qa_gmem_read_i32(m, slot));
    }
    case S_SYNC:
      return qa_call_int32(qa_system_v_stdio_flush(
          stdio, qa_guest_required_pointer(args, count, 0), context));
    case S_OVERFLOW: {
      qa_guest_core_address file = qa_guest_required_pointer(args, count, 0);
      int value = (int)qa_guest_integer(args, count, 1);
      return qa_call_int32(value == -1
          ? (qa_system_v_stdio_flush(stdio, file, context) == 0 ? 0 : -1)
          : qa_system_v_stdio_put(stdio, file, value & 255, false, context));
    }
    default: break;
  }
  return qa_call_void();
}
static void reg(qa_system_v_stdio *stdio, int kind, const char *name,
                const char *const *versions, size_t version_count,
                const qa_abi_storage *parameters, size_t parameter_count, bool result_is_void,
                qa_abi_storage result) {
  stdio_state *state = xmalloc(sizeof *state);
  state->stdio = stdio; state->kind = kind; state->name = NULL;
  qa_system_v_implementation impl = {state, stdio_impl};
  qa_system_v_service(stdio->host, "libc.so.6", name, versions, version_count, parameters,
                      parameter_count, result_is_void, result, impl);
}

void qa_system_v_stdio_init(qa_system_v_stdio *stdio, qa_system_v_service_host *host) {
  qa_mapped_guest_memory *m = host->memory;
  stdio->host = host;
  stdio->layout = qa_system_v_file_layout_for(m->pointer_bytes);
  qa_guest_core_address zero = {0};
  stdio->stdin_ = create_file(stdio, 0, false, zero);
  stdio->stdout_ = create_file(stdio, 1, true, stdio->stdin_);
  stdio->stderr_ = create_file(stdio, 2, true, stdio->stdout_);
  const char *version = m->pointer_bytes == 4 ? "GLIBC_2.0" : "GLIBC_2.2.5";
  const char *names[] = {"stdin", "stdout", "stderr"};
  qa_guest_core_address values[] = {stdio->stdin_, stdio->stdout_, stdio->stderr_};
  for (size_t i = 0; i < 3; i++) {
    qa_guest_core_address slot = qa_system_v_allocate(host, (size_t)m->pointer_bytes);
    write_ptr(m, slot, true, values[i]);
    qa_system_v_data(host, "libc.so.6", names[i], version, slot, (size_t)m->pointer_bytes);
    char io_name[32];
    (void)snprintf(io_name, sizeof io_name, "_IO_2_1_%s_", names[i]);
    char *io_copy = xmalloc(strlen(io_name) + 1);
    strcpy(io_copy, io_name);
    qa_system_v_data(host, "libc.so.6", io_copy, m->pointer_bytes == 4 ? "GLIBC_2.1" : version,
                     values[i], (size_t)stdio->layout.size + (size_t)m->pointer_bytes);
  }
  const char *versions[] = {version, NULL};
  qa_abi_storage ptr = QA_STORAGE_POINTER, i32 = QA_STORAGE_INT32;
  qa_abi_storage putc_params[] = {i32, ptr};
  qa_abi_storage write_params[] = {ptr, host->pointer_storage, host->pointer_storage, ptr};
  qa_abi_storage fwide_params[] = {ptr, i32};
  reg(stdio, S_FFLUSH, "fflush", versions, 2, &ptr, 1, false, i32);
  reg(stdio, S_PUTC, "fputc", versions, 2, putc_params, 2, false, i32);
  reg(stdio, S_PUTC, "putc", versions, 2, putc_params, 2, false, i32);
  reg(stdio, S_GETC, "fgetc", versions, 2, &ptr, 1, false, i32);
  reg(stdio, S_GETC, "getc", versions, 2, &ptr, 1, false, i32);
  reg(stdio, S_UNGETC, "ungetc", versions, 2, putc_params, 2, false, i32);
  reg(stdio, S_FWRITE, "fwrite", versions, 2, write_params, 4, false, host->pointer_storage);
  reg(stdio, S_FREAD, "fread", versions, 2, write_params, 4, false, host->pointer_storage);
  const char *fwide_versions[] = {m->pointer_bytes == 4 ? "GLIBC_2.1" : version, NULL};
  reg(stdio, S_FWIDE, "fwide", fwide_versions, 2, fwide_params, 2, false, i32);
  qa_guest_core_address table = qa_system_v_allocate(host, 21 * (size_t)m->pointer_bytes);
  qa_system_v_data(host, "libc.so.6", "_IO_file_jumps",
                   m->pointer_bytes == 4 ? "GLIBC_2.1" : version, table,
                   21 * (size_t)m->pointer_bytes);
  static const char *const operations[] = {"finish", "overflow", "underflow", "uflow",
      "pbackfail", "xsputn", "xsgetn", "seekoff", "seekpos", "setbuf", "sync", "doallocate",
      "read", "write", "seek", "close", "stat", "showmanyc", "imbue"};
  const char *null_versions[] = {NULL};
  for (size_t index = 0; index < 19; index++) {
    char name_buffer[64];
    (void)snprintf(name_buffer, sizeof name_buffer, "__guest_IO_file_%s", operations[index]);
    char *name = xmalloc(strlen(name_buffer) + 1);
    strcpy(name, name_buffer);
    qa_guest_core_address address;
    if (strcmp(operations[index], "sync") == 0) {
      reg(stdio, S_SYNC, name, null_versions, 1, &ptr, 1, false, i32);
      if (!qa_system_v_resolve_address(host, "libc.so.6", name, NULL, &address))
        plain_error("Missing FILE sync");
    } else if (strcmp(operations[index], "overflow") == 0) {
      qa_abi_storage params[] = {ptr, i32};
      reg(stdio, S_OVERFLOW, name, null_versions, 1, params, 2, false, i32);
      if (!qa_system_v_resolve_address(host, "libc.so.6", name, NULL, &address))
        plain_error("Missing FILE overflow");
    } else {
      address = qa_system_v_unavailable(host, "libc.so.6", name, &ptr, 1, true, i32,
                                        "this FILE virtual operation is not implemented");
    }
    write_ptr(m, off(m, table, (int64_t)(index + 2) * m->pointer_bytes), true, address);
  }
  qa_guest_core_address wide_table = qa_system_v_allocate(host, 21 * (size_t)m->pointer_bytes);
  qa_system_v_data(host, "libc.so.6", "_IO_wfile_jumps",
                   m->pointer_bytes == 4 ? "GLIBC_2.1" : version, wide_table,
                   21 * (size_t)m->pointer_bytes);
  for (size_t index = 0; index < 19; index++) {
    qa_guest_core_address address = {0};
    bool has = false;
    if (strcmp(operations[index], "sync") == 0) {
      has = read_ptr(m, off(m, table, (int64_t)(index + 2) * m->pointer_bytes), &address);
    } else {
      char name_buffer[64];
      (void)snprintf(name_buffer, sizeof name_buffer, "__guest_IO_wfile_%s", operations[index]);
      char *name = xmalloc(strlen(name_buffer) + 1);
      strcpy(name, name_buffer);
      address = qa_system_v_unavailable(host, "libc.so.6", name, &ptr, 1, true, i32,
                                        "this wide FILE virtual operation is not implemented");
      has = true;
    }
    write_ptr(m, off(m, wide_table, (int64_t)(index + 2) * m->pointer_bytes), has, address);
  }
  for (size_t i = 0; i < 3; i++) {
    write_ptr(m, off(m, values[i], stdio->layout.size), true, table);
    qa_guest_core_address wide = {0};
    if (!read_ptr(m, off(m, values[i], stdio->layout.wide_data), &wide))
      plain_error("Missing wide FILE data");
    write_ptr(m, off(m, wide, m->pointer_bytes == 4 ? 176 : 304), true, wide_table);
  }
}
