/* Port of src/guest/runtime/windows/msvc/streams.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/windows/msvc/streams.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/memory.h"
#include "qa/guest/runtime/windows/msvc/locale.h"

static const char *const library = "msvcp140.dll";
static const char *const streambuf_name = "?$basic_streambuf@DU?$char_traits@D@std@@@std@@";
static const char *const ios_name = "?$basic_ios@DU?$char_traits@D@std@@@std@@";
static const char *const ostream_name = "?$basic_ostream@DU?$char_traits@D@std@@@std@@";
static const char *const iostream_name = "?$basic_iostream@DU?$char_traits@D@std@@@std@@";

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Guest runtime out of memory");
  return p;
}
static char *xstrdup(const char *text) {
  char *copy = xmalloc(strlen(text) + 1);
  strcpy(copy, text);
  return copy;
}
static char *concat3(const char *a, const char *b, const char *c) {
  char *out = xmalloc(strlen(a) + strlen(b) + strlen(c) + 1);
  strcpy(out, a); strcat(out, b); strcat(out, c);
  return out;
}
static void plain_error(const char *message) { qa_range_error("%s", message); }
static void type_error(const char *message) { qa_range_error("%s", message); }

typedef struct streams_ctx {
  qa_windows_service_host *host;
  qa_msvc_locale *locale; /* owned */
  qa_guest_address ios_table, ostream_table, iostream_table, buffer_table;
  qa_guest_address ostream_vb, iostream_vb;
} streams_ctx;

static qa_guest_address at(streams_ctx *s, qa_guest_address a, int64_t o) {
  return qa_memory_offset(s->host->memory, a, o);
}
static qa_guest_address ref(streams_ctx *s, qa_guest_address a) {
  qa_guest_address out = {0};
  if (!qa_gmem_read_pointer(s->host->memory, a, &out)) plain_error("Missing MSVC object pointer");
  return out;
}
static bool read_ptr(streams_ctx *s, qa_guest_address a, qa_guest_address *out) {
  return qa_gmem_read_pointer(s->host->memory, a, out);
}
static void write_ptr(streams_ctx *s, qa_guest_address a, bool has, qa_guest_address v) {
  qa_gmem_write_pointer(s->host->memory, a, has, v);
}
static qa_call_result virtual_call(streams_ctx *s, const qa_guest_call_context *context,
                                   qa_guest_address object, int slot_index,
                                   const qa_abi_storage *parameters, size_t parameter_count,
                                   bool result_is_void, qa_abi_storage result,
                                   const qa_call_value *extra, size_t extra_count) {
  qa_guest_address target = ref(s, at(s, ref(s, object), (int64_t)slot_index * 8));
  qa_abi_storage *all_params = xmalloc((parameter_count + 1) * sizeof *all_params);
  all_params[0] = QA_STORAGE_POINTER;
  for (size_t i = 0; i < parameter_count; i++) all_params[i + 1] = parameters[i];
  qa_call_value *all_args = xmalloc((extra_count + 1) * sizeof *all_args);
  all_args[0] = qa_call_pointer(true, object);
  for (size_t i = 0; i < extra_count; i++) all_args[i + 1] = extra[i];
  qa_call_result value = qa_windows_invoke(s->host, context, target, all_params,
                                           parameter_count + 1, result_is_void, result,
                                           all_args, extra_count + 1, false);
  free(all_params); free(all_args);
  return value;
}
static int64_t result_integer(qa_call_result result) {
  if (result.kind != QA_CALL_INT32 && result.kind != QA_CALL_UINT32 &&
      result.kind != QA_CALL_INT64 && result.kind != QA_CALL_UINT64)
    type_error("MSVC integer result required");
  return qa_call_value_bigint_i64(&result);
}
static qa_guest_address make_table(streams_ctx *s, const char *const *names, size_t count,
                                   const char *label) {
  qa_mapped_guest_memory *memory = s->host->memory;
  qa_guest_address result = qa_gmem_allocate(s->host->memory_ext, count * 8, false, 0, false,
                                             QA_PERM_READ_WRITE, label);
  for (size_t i = 0; i < count; i++) {
    qa_guest_address target = {0};
    if (!qa_windows_resolve_address(s->host, library, names[i], &target))
      qa_range_error("Missing MSVC method %s", names[i]);
    write_ptr(s, at(s, result, (int64_t)i * 8), true, target);
  }
  qa_gmem_protect(s->host->memory_ext, result, count * 8, QA_PERM_READ);
  (void)memory;
  return result;
}
static qa_guest_address virtual_ios(streams_ctx *s, qa_guest_address object) {
  return at(s, object, qa_gmem_read_i32(s->host->memory, at(s, ref(s, object), 4)));
}
static void stream_setstate(streams_ctx *s, const qa_guest_call_context *context,
                     qa_guest_address object, int state) {
  (void)context;
  qa_mapped_guest_memory *memory = s->host->memory;
  qa_guest_address buffer = {0};
  bool has_buffer = read_ptr(s, at(s, object, 72), &buffer);
  int value = (qa_gmem_read_i32(memory, at(s, object, 16)) | state | (has_buffer ? 0 : 4)) & 0x17;
  qa_gmem_write_i32(memory, at(s, object, 16), value);
  if ((value & qa_gmem_read_i32(memory, at(s, object, 20))) != 0)
    qa_unsupported_windows_import(library, "ios_base::failure",
                                  "stream exception requires guest C++ exception dispatch");
}
static void basic_dtor(streams_ctx *s, const qa_guest_call_context *context,
                       qa_guest_address object) {
  qa_mapped_guest_memory *memory = s->host->memory;
  write_ptr(s, object, true, s->ios_table);
  if (qa_memory_read_u64(memory, at(s, object, 8)) != 0)
    qa_unsupported_windows_import(library, "ios_base destructor",
                                  "standard stream ownership is not implemented");
  /* _Callfns(erase_event), then destroy the sparse iword/pword and event lists. */
  qa_guest_address call = {0};
  bool has_call = read_ptr(s, at(s, object, 56), &call);
  while (has_call) {
    qa_abi_storage params[] = {QA_STORAGE_INT32, QA_STORAGE_POINTER, QA_STORAGE_INT32};
    qa_call_value invoke_args[] = {qa_call_int32(0), qa_call_pointer(true, object),
                                   qa_call_int32(qa_gmem_read_i32(memory, at(s, call, 8)))};
    qa_call_result result = qa_windows_invoke(s->host, context, ref(s, at(s, call, 16)),
                                              params, 3, true, QA_STORAGE_INT32, invoke_args, 3,
                                              false);
    qa_call_value_free(&result);
    has_call = read_ptr(s, call, &call);
  }
  const int offsets[] = {48, 56};
  for (size_t i = 0; i < 2; i++) {
    qa_guest_address node = {0};
    bool has_node = read_ptr(s, at(s, object, offsets[i]), &node);
    while (has_node) {
      qa_guest_address next = {0};
      bool has_next = read_ptr(s, node, &next);
      if (!qa_windows_free(s->host, true, node, 0))
        plain_error("Invalid ios_base list allocation");
      node = next; has_node = has_next;
    }
    qa_guest_address none = {0};
    write_ptr(s, at(s, object, offsets[i]), false, none);
  }
  qa_guest_address locale_address = {0};
  bool has_locale = read_ptr(s, at(s, object, 64), &locale_address);
  qa_msvc_locale_destroy(s->locale, context, has_locale, locale_address);
  qa_guest_address none = {0};
  write_ptr(s, at(s, object, 64), false, none);
}
static void construct_ios(streams_ctx *s, qa_guest_address object) {
  uint8_t zeros[96] = {0};
  qa_memory_write(s->host->memory, object, zeros, sizeof zeros);
  write_ptr(s, object, true, s->ios_table);
}
static void initialize_ios(streams_ctx *s, qa_guest_address object, bool has_buffer,
                           qa_guest_address buffer) {
  qa_mapped_guest_memory *memory = s->host->memory;
  qa_guest_address none = {0};
  qa_memory_write_u64(memory, at(s, object, 8), 0);
  qa_gmem_write_i32(memory, at(s, object, 16), has_buffer ? 0 : 4);
  qa_gmem_write_i32(memory, at(s, object, 20), 0);
  qa_gmem_write_i32(memory, at(s, object, 24), 0x201);
  qa_gmem_write_i64(memory, at(s, object, 32), 6);
  qa_gmem_write_i64(memory, at(s, object, 40), 0);
  write_ptr(s, at(s, object, 48), false, none);
  write_ptr(s, at(s, object, 56), false, none);
  write_ptr(s, at(s, object, 64), true, qa_msvc_locale_create(s->locale));
  write_ptr(s, at(s, object, 72), has_buffer, buffer);
  write_ptr(s, at(s, object, 80), false, none);
  qa_memory_write_u8(memory, at(s, object, 88), 32);
}
static void buffer_dtor(streams_ctx *s, const qa_guest_call_context *context,
                        qa_guest_address object) {
  write_ptr(s, object, true, s->buffer_table);
  qa_guest_address locale_address = {0};
  bool has_locale = read_ptr(s, at(s, object, 96), &locale_address);
  qa_msvc_locale_destroy(s->locale, context, has_locale, locale_address);
  qa_guest_address none = {0};
  write_ptr(s, at(s, object, 96), false, none);
}
static bool field_ptr(streams_ctx *s, qa_guest_address object, int offset,
                      qa_guest_address *out) {
  return read_ptr(s, ref(s, at(s, object, offset)), out);
}
static int available(streams_ctx *s, qa_guest_address object, bool input) {
  qa_guest_address value = {0};
  if (!field_ptr(s, object, input ? 56 : 64, &value)) return 0;
  return qa_gmem_read_i32(s->host->memory, ref(s, at(s, object, input ? 80 : 88)));
}
static qa_guest_address bump(streams_ctx *s, qa_guest_address object, int amount, bool input) {
  qa_mapped_guest_memory *memory = s->host->memory;
  qa_guest_address next_slot = ref(s, at(s, object, input ? 56 : 64));
  qa_guest_address next = ref(s, next_slot);
  qa_guest_address count_slot = ref(s, at(s, object, input ? 80 : 88));
  write_ptr(s, next_slot, true, at(s, next, amount));
  qa_gmem_write_i32(memory, count_slot, qa_gmem_read_i32(memory, count_slot) - amount);
  return next;
}
static int get_char(streams_ctx *s, const qa_guest_call_context *context,
                    qa_guest_address object, bool consume) {
  if (available(s, object, true) > 0)
    return qa_memory_read_u8(s->host->memory,
                             consume ? bump(s, object, 1, true)
                                     : ref(s, ref(s, at(s, object, 56))));
  return (int)result_integer(virtual_call(s, context, object, consume ? 7 : 6, NULL, 0, false,
                                          QA_STORAGE_INT32, NULL, 0));
}
static int put_char(streams_ctx *s, const qa_guest_call_context *context,
                    qa_guest_address object, int character) {
  if (available(s, object, false) > 0) {
    qa_memory_write_u8(s->host->memory, bump(s, object, 1, false), (uint8_t)(character & 255));
    return character & 255;
  }
  qa_abi_storage params[] = {QA_STORAGE_INT32};
  qa_call_value extra[] = {qa_call_int32(character & 255)};
  return (int)result_integer(virtual_call(s, context, object, 3, params, 1, false,
                                          QA_STORAGE_INT32, extra, 1));
}
static void flush_stream(streams_ctx *s, const qa_guest_call_context *context,
                         qa_guest_address object);
static void suffix(streams_ctx *s, const qa_guest_call_context *context,
                   qa_guest_address object) {
  qa_mapped_guest_memory *memory = s->host->memory;
  qa_guest_address base = virtual_ios(s, object);
  if (qa_gmem_read_i32(memory, at(s, base, 16)) != 0 ||
      (qa_gmem_read_i32(memory, at(s, base, 24)) & 2) == 0)
    return;
  qa_guest_address buffer = {0};
  if (read_ptr(s, at(s, base, 72), &buffer) &&
      result_integer(virtual_call(s, context, buffer, 13, NULL, 0, false, QA_STORAGE_INT32,
                                  NULL, 0)) == -1)
    stream_setstate(s, context, base, 4);
}
static void flush_stream(streams_ctx *s, const qa_guest_call_context *context,
                         qa_guest_address object) {
  qa_mapped_guest_memory *memory = s->host->memory;
  qa_guest_address base = virtual_ios(s, object);
  qa_guest_address buffer = {0};
  if (!read_ptr(s, at(s, base, 72), &buffer)) return;
  qa_call_result lock_result = virtual_call(s, context, buffer, 1, NULL, 0, true,
                                            QA_STORAGE_INT32, NULL, 0);
  qa_call_value_free(&lock_result);
  /* TS try/finally: the unlock also runs on throw; in C a throw aborts. */
  if (qa_gmem_read_i32(memory, at(s, base, 16)) == 0) {
    qa_guest_address tied = {0};
    if (read_ptr(s, at(s, base, 80), &tied) && tied.byte_offset != object.byte_offset)
      flush_stream(s, context, tied);
    if (qa_gmem_read_i32(memory, at(s, base, 16)) == 0 &&
        result_integer(virtual_call(s, context, buffer, 13, NULL, 0, false,
                                      QA_STORAGE_INT32, NULL, 0)) == -1)
      stream_setstate(s, context, base, 4);
  }
  suffix(s, context, object);
  qa_call_result unlock_result = virtual_call(s, context, buffer, 2, NULL, 0, true,
                                              QA_STORAGE_INT32, NULL, 0);
  qa_call_value_free(&unlock_result);
}

typedef struct stream_state {
  streams_ctx *s;
  int kind;
  int number; /* field offset / bits / input flag */
} stream_state;
enum {
  ST_BASIC_DTOR, ST_BASIC_DELETE, ST_IOS_CTOR, ST_RDBUF, ST_SETSTATE, ST_GOOD,
  ST_OSTREAM_CTOR, ST_IOSTREAM_CTOR, ST_OSTREAM_DTOR, ST_IOSTREAM_DTOR, ST_OSTREAM_DELETE,
  ST_BUFFER_DTOR, ST_BUFFER_DELETE, ST_VOID0, ST_MINUS1_CH, ST_MINUS1, ST_SHOWMANYC, ST_SYNC,
  ST_SETBUF, ST_FIELD, ST_EGPTR, ST_UFLOW, ST_SPUTC, ST_XS, ST_SPUTN, ST_SEEK, ST_BUFFER_CTOR,
  ST_FLUSH, ST_OSFX, ST_UNCAUGHT, ST_TELLP, ST_INSERT_INT, ST_INSERT_BUFFER
};
static qa_call_result stream_impl(void *ctx, const qa_guest_call_context *context,
                                  const qa_call_value *args, size_t count) {
  stream_state *state = ctx;
  streams_ctx *s = state->s;
  qa_mapped_guest_memory *memory = s->host->memory;
  qa_guest_address none = {0};
  switch (state->kind) {
    case ST_BASIC_DTOR:
      basic_dtor(s, context, qa_guest_required_pointer(args, count, 0));
      return qa_call_void();
    case ST_BASIC_DELETE: {
      qa_guest_address object = qa_guest_required_pointer(args, count, 0);
      basic_dtor(s, context, object);
      if ((qa_guest_integer_u64(args, count, 1) & 1u) != 0 &&
          !qa_windows_free(s->host, true, object, 0))
        plain_error("Invalid basic_ios allocation");
      return qa_call_pointer(true, object);
    }
    case ST_IOS_CTOR: {
      qa_guest_address object = qa_guest_required_pointer(args, count, 0);
      construct_ios(s, object);
      return qa_call_pointer(true, object);
    }
    case ST_RDBUF: {
      qa_guest_address buffer = {0};
      bool has = read_ptr(s, at(s, qa_guest_required_pointer(args, count, 0), 72), &buffer);
      return qa_call_pointer(has, buffer);
    }
    case ST_SETSTATE:
      stream_setstate(s, context, qa_guest_required_pointer(args, count, 0),
               (int)qa_guest_integer(args, count, 1));
      return qa_call_void();
    case ST_GOOD:
      return qa_call_uint32(
          qa_gmem_read_i32(memory, at(s, qa_guest_required_pointer(args, count, 0), 16)) == 0
              ? 1 : 0);
    case ST_OSTREAM_CTOR: {
      qa_guest_address object = qa_guest_required_pointer(args, count, 0);
      if (qa_guest_integer(args, count, 2) != 0)
        qa_unsupported_windows_import(library, "basic_ostream constructor",
                                      "standard stream registration is not implemented");
      if (qa_guest_integer(args, count, 3) != 0) {
        write_ptr(s, object, true, s->ostream_vb);
        construct_ios(s, at(s, object, 16));
      }
      qa_guest_address base = virtual_ios(s, object);
      write_ptr(s, base, true, s->ostream_table);
      qa_gmem_write_i32(memory, at(s, base, -4),
                        (int)(base.byte_offset - object.byte_offset) - 16);
      qa_guest_address buffer = {0};
      bool has_buffer = qa_guest_pointer_argument(args, count, 1, &buffer);
      initialize_ios(s, base, has_buffer, buffer);
      return qa_call_pointer(true, object);
    }
    case ST_IOSTREAM_CTOR: {
      qa_guest_address object = qa_guest_required_pointer(args, count, 0);
      if (qa_guest_integer(args, count, 2) != 0) {
        write_ptr(s, object, true, s->iostream_vb);
        write_ptr(s, at(s, object, 16), true, at(s, s->iostream_vb, 8));
        construct_ios(s, at(s, object, 32));
      }
      qa_memory_write_u64(memory, at(s, object, 8), 0);
      qa_guest_address base = virtual_ios(s, object);
      write_ptr(s, base, true, s->iostream_table);
      qa_gmem_write_i32(memory, at(s, base, -4),
                        (int)(base.byte_offset - object.byte_offset) - 32);
      qa_guest_address buffer = {0};
      bool has_buffer = qa_guest_pointer_argument(args, count, 1, &buffer);
      initialize_ios(s, base, has_buffer, buffer);
      return qa_call_pointer(true, object);
    }
    case ST_OSTREAM_DTOR: {
      /* MSVC base destructors receive this adjusted to the vfptr's static
       * offset, without the dynamic virtual-base displacement. */
      qa_guest_address object = at(s, qa_guest_required_pointer(args, count, 0), -16);
      qa_guest_address base = virtual_ios(s, object);
      write_ptr(s, base, true, s->ostream_table);
      qa_gmem_write_i32(memory, at(s, base, -4),
                        (int)(base.byte_offset - object.byte_offset) - 16);
      return qa_call_void();
    }
    case ST_IOSTREAM_DTOR: {
      qa_guest_address object = at(s, qa_guest_required_pointer(args, count, 0), -32);
      qa_guest_address base = virtual_ios(s, object);
      write_ptr(s, base, true, s->iostream_table);
      qa_gmem_write_i32(memory, at(s, base, -4),
                        (int)(base.byte_offset - object.byte_offset) - 32);
      return qa_call_void();
    }
    case ST_OSTREAM_DELETE:
      qa_range_error(
          "Unsupported Windows guest import msvcp140.dll!basic_ostream deleting destructor: "
          "standalone deleting destructor at %llx is not implemented",
          (unsigned long long)qa_guest_required_pointer(args, count, 0).byte_offset);
      return qa_call_void();
    case ST_BUFFER_DTOR:
      buffer_dtor(s, context, qa_guest_required_pointer(args, count, 0));
      return qa_call_void();
    case ST_BUFFER_DELETE: {
      qa_guest_address object = qa_guest_required_pointer(args, count, 0);
      buffer_dtor(s, context, object);
      if ((qa_guest_integer_u64(args, count, 1) & 1u) != 0 &&
          !qa_windows_free(s->host, true, object, 0))
        plain_error("Invalid streambuf allocation");
      return qa_call_pointer(true, object);
    }
    case ST_VOID0: return qa_call_void();
    case ST_MINUS1: case ST_MINUS1_CH: return qa_call_int32(-1);
    case ST_SHOWMANYC: return qa_call_int64(0);
    case ST_SYNC: return qa_call_int32(0);
    case ST_SETBUF: return qa_call_pointer(true, qa_guest_required_pointer(args, count, 0));
    case ST_FIELD: {
      qa_guest_address value = {0};
      bool has = field_ptr(s, qa_guest_required_pointer(args, count, 0), state->number, &value);
      return qa_call_pointer(has, value);
    }
    case ST_EGPTR: {
      bool input = state->number != 0;
      qa_guest_address object = qa_guest_required_pointer(args, count, 0);
      qa_guest_address next = {0};
      if (!field_ptr(s, object, input ? 56 : 64, &next)) return qa_call_pointer(false, none);
      return qa_call_pointer(
          true, at(s, next, qa_gmem_read_i32(memory, ref(s, at(s, object, input ? 80 : 88)))));
    }
    case ST_UFLOW: {
      qa_guest_address object = qa_guest_required_pointer(args, count, 0);
      if (result_integer(virtual_call(s, context, object, 6, NULL, 0, false,
                                      QA_STORAGE_INT32, NULL, 0)) == -1)
        return qa_call_int32(-1);
      return qa_call_int32(qa_memory_read_u8(memory, bump(s, object, 1, true)));
    }
    case ST_SPUTC:
      return qa_call_int32(put_char(s, context, qa_guest_required_pointer(args, count, 0),
                                    (int)qa_guest_integer(args, count, 1)));
    case ST_XS: {
      bool input = state->number != 0;
      qa_guest_address object = qa_guest_required_pointer(args, count, 0);
      qa_guest_address data = qa_guest_required_pointer(args, count, 1);
      int64_t requested = qa_guest_integer(args, count, 2);
      if (requested <= 0) return qa_call_int64(0);
      if (requested > 0x10000000) qa_range_error("MSVC stream transfer exceeds limit");
      size_t length = (size_t)requested;
      size_t copied = 0;
      while (copied < length) {
        int avail = available(s, object, input);
        size_t amount = avail > 0 && (size_t)avail < length - copied ? (size_t)avail
                        : avail > 0 ? length - copied : 0;
        if (amount > 0) {
          qa_guest_address buffer = bump(s, object, (int)amount, input);
          uint8_t *bytes = xmalloc(amount);
          if (input) {
            qa_memory_copy(memory, buffer, bytes, amount);
            qa_memory_write(memory, at(s, data, (int64_t)copied), bytes, amount);
          } else {
            qa_memory_copy(memory, at(s, data, (int64_t)copied), bytes, amount);
            qa_memory_write(memory, buffer, bytes, amount);
          }
          free(bytes);
          copied += amount;
        } else if (input) {
          int character = get_char(s, context, object, true);
          if (character == -1) break;
          qa_memory_write_u8(memory, at(s, data, (int64_t)copied++), (uint8_t)character);
        } else {
          if (put_char(s, context, object,
                       qa_memory_read_u8(memory, at(s, data, (int64_t)copied))) == -1)
            break;
          copied++;
        }
      }
      return qa_call_int64((int64_t)copied);
    }
    case ST_SPUTN: {
      qa_abi_storage params[] = {QA_STORAGE_POINTER, QA_STORAGE_INT64};
      qa_call_value extra[] = {qa_call_pointer(true, qa_guest_required_pointer(args, count, 1)),
                               qa_call_int64(qa_guest_integer(args, count, 2))};
      return virtual_call(s, context, qa_guest_required_pointer(args, count, 0), 9, params, 2,
                          false, QA_STORAGE_INT64, extra, 2);
    }
    case ST_SEEK: {
      qa_guest_address result = qa_guest_required_pointer(args, count, 1);
      uint8_t zeros[24] = {0};
      qa_memory_write(memory, result, zeros, sizeof zeros);
      qa_gmem_write_i64(memory, at(s, result, 8), -1);
      return qa_call_pointer(true, result);
    }
    case ST_BUFFER_CTOR: {
      qa_guest_address object = qa_guest_required_pointer(args, count, 0);
      uint8_t zeros[104] = {0};
      qa_memory_write(memory, object, zeros, sizeof zeros);
      write_ptr(s, object, true, s->buffer_table);
      const struct { int slot, target; } entries[] = {{24, 8}, {32, 16}, {56, 40},
                                                       {64, 48}, {80, 72}, {88, 76}};
      for (size_t i = 0; i < 6; i++)
        write_ptr(s, at(s, object, entries[i].slot), true, at(s, object, entries[i].target));
      write_ptr(s, at(s, object, 96), true, qa_msvc_locale_create(s->locale));
      return qa_call_pointer(true, object);
    }
    case ST_FLUSH: {
      qa_guest_address object = qa_guest_required_pointer(args, count, 0);
      flush_stream(s, context, object);
      return qa_call_pointer(true, object);
    }
    case ST_OSFX:
      suffix(s, context, qa_guest_required_pointer(args, count, 0));
      return qa_call_void();
    case ST_UNCAUGHT: return qa_call_uint32(0);
    case ST_TELLP: {
      qa_guest_address base = virtual_ios(s, qa_guest_required_pointer(args, count, 0));
      qa_guest_address result = qa_guest_required_pointer(args, count, 1);
      if ((qa_gmem_read_i32(memory, at(s, base, 16)) & 6) != 0) {
        uint8_t zeros[24] = {0};
        qa_memory_write(memory, result, zeros, sizeof zeros);
        qa_gmem_write_i64(memory, at(s, result, 8), -1);
        return qa_call_pointer(true, result);
      }
      qa_abi_storage params[] = {QA_STORAGE_POINTER, QA_STORAGE_INT64, QA_STORAGE_INT32,
                                 QA_STORAGE_INT32};
      qa_call_value extra[] = {qa_call_pointer(true, result), qa_call_int64(0),
                               qa_call_int32(1), qa_call_int32(2)};
      return virtual_call(s, context, ref(s, at(s, base, 72)), 10, params, 4, false,
                          QA_STORAGE_POINTER, extra, 4);
    }
    case ST_INSERT_INT: {
      int bits = state->number;
      qa_guest_address object = qa_guest_required_pointer(args, count, 0);
      qa_guest_address base = virtual_ios(s, object);
      qa_guest_address buffer = {0};
      bool has_buffer = read_ptr(s, at(s, base, 72), &buffer);
      if (has_buffer) {
        qa_call_result r = virtual_call(s, context, buffer, 1, NULL, 0, true,
                                        QA_STORAGE_INT32, NULL, 0);
        qa_call_value_free(&r);
      }
      if (qa_gmem_read_i32(memory, at(s, base, 16)) == 0 && has_buffer) {
        qa_guest_address tied = {0};
        if (read_ptr(s, at(s, base, 80), &tied) && tied.byte_offset != object.byte_offset)
          flush_stream(s, context, tied);
        if (qa_gmem_read_i32(memory, at(s, base, 16)) == 0) {
          qa_guest_address implementation = ref(s, ref(s, at(s, base, 64)));
          if (implementation.byte_offset != s->locale->global.byte_offset ||
              qa_memory_read_u64(memory, at(s, implementation, 24)) != 0)
            qa_unsupported_windows_import(
                library, "ostream integer insertion",
                "custom num_put locale requires facet dispatch");
          int32_t flags = qa_gmem_read_i32(memory, at(s, base, 24));
          int basefield = flags & 0xe00;
          int radix = basefield == 0x400 ? 8 : basefield == 0x800 ? 16 : 10;
          uint64_t raw = qa_guest_integer_u64(args, count, 1);
          bool negative = false;
          uint64_t magnitude;
          if (radix == 10) {
            int64_t signed_value = bits == 32 ? (int32_t)raw : (int64_t)raw;
            negative = signed_value < 0;
            magnitude = negative ? (uint64_t)(~((uint64_t)signed_value) + 1)
                                 : (uint64_t)signed_value;
          } else {
            magnitude = bits == 32 ? (uint32_t)raw : raw;
          }
          char digits_buffer[32];
          int digit_count = 0;
          uint64_t remaining = magnitude;
          do {
            int digit = (int)(remaining % (uint64_t)radix);
            digits_buffer[digit_count++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
            remaining /= (uint64_t)radix;
          } while (remaining != 0);
          char digits[32];
          for (int i = 0; i < digit_count; i++) digits[i] = digits_buffer[digit_count - 1 - i];
          digits[digit_count] = '\0';
          char prefix[4] = "";
          if (negative) strcpy(prefix, "-");
          else if (radix == 10 && (flags & 0x20) != 0) strcpy(prefix, "+");
          if (radix != 10 && magnitude != 0 && (flags & 8) != 0)
            strcpy(prefix, radix == 16 ? "0x" : "0");
          if ((flags & 4) != 0) {
            for (char *c = digits; *c != '\0'; c++)
              if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
            for (char *c = prefix; *c != '\0'; c++)
              if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
          }
          int64_t width = qa_gmem_read_i64(memory, at(s, base, 40));
          if (width > 0x1000000) qa_range_error("MSVC numeric field exceeds limit");
          int64_t fill_count = width - (int64_t)strlen(prefix) - digit_count;
          if (fill_count < 0) fill_count = 0;
          uint8_t fill_char = qa_memory_read_u8(memory, at(s, base, 88));
          int adjustment = flags & 0x1c0;
          /* text = left: prefix+digits+fill; internal: prefix+fill+digits;
           * else fill+prefix+digits. */
          bool failed = false;
          const char *parts[3];
          char *fill_text = xmalloc((size_t)fill_count + 1);
          memset(fill_text, fill_char, (size_t)fill_count);
          fill_text[fill_count] = '\0';
          if (adjustment == 0x40) { parts[0] = prefix; parts[1] = digits; parts[2] = fill_text; }
          else if (adjustment == 0x100) { parts[0] = prefix; parts[1] = fill_text; parts[2] = digits; }
          else { parts[0] = fill_text; parts[1] = prefix; parts[2] = digits; }
          for (size_t part = 0; part < 3 && !failed; part++)
            for (size_t i = 0; parts[part][i] != '\0'; i++)
              if (put_char(s, context, buffer, (uint8_t)parts[part][i]) == -1) {
                stream_setstate(s, context, base, 4);
                failed = true;
                break;
              }
          free(fill_text);
          qa_gmem_write_i64(memory, at(s, base, 40), 0);
        }
      }
      stream_setstate(s, context, base, 0);
      suffix(s, context, object);
      if (has_buffer) {
        qa_call_result r = virtual_call(s, context, buffer, 2, NULL, 0, true,
                                        QA_STORAGE_INT32, NULL, 0);
        qa_call_value_free(&r);
      }
      return qa_call_pointer(true, object);
    }
    case ST_INSERT_BUFFER: {
      qa_guest_address object = qa_guest_required_pointer(args, count, 0);
      qa_guest_address base = virtual_ios(s, object);
      qa_guest_address target = {0}, source = {0};
      bool has_target = read_ptr(s, at(s, base, 72), &target);
      bool has_source = qa_guest_pointer_argument(args, count, 1, &source);
      bool copied = false;
      int insert_state = 0;
      if (has_target) {
        qa_call_result r = virtual_call(s, context, target, 1, NULL, 0, true,
                                        QA_STORAGE_INT32, NULL, 0);
        qa_call_value_free(&r);
      }
      if (qa_gmem_read_i32(memory, at(s, base, 16)) == 0 && has_target && has_source) {
        qa_guest_address tied = {0};
        if (read_ptr(s, at(s, base, 80), &tied) && tied.byte_offset != object.byte_offset)
          flush_stream(s, context, tied);
        if (qa_gmem_read_i32(memory, at(s, base, 16)) == 0)
          for (;;) {
            int character = get_char(s, context, source, false);
            if (character == -1) break;
            if (put_char(s, context, target, character) == -1) { insert_state |= 4; break; }
            (void)get_char(s, context, source, true);
            copied = true;
          }
      }
      qa_gmem_write_i64(memory, at(s, base, 40), 0);
      stream_setstate(s, context, base, !has_source ? 4 : insert_state | (copied ? 0 : 2));
      suffix(s, context, object);
      if (has_target) {
        qa_call_result r = virtual_call(s, context, target, 2, NULL, 0, true,
                                        QA_STORAGE_INT32, NULL, 0);
        qa_call_value_free(&r);
      }
      return qa_call_pointer(true, object);
    }
    default: break;
  }
  (void)none;
  return qa_call_void();
}
static void bind(streams_ctx *s, int kind, int number, const char *name,
                 const qa_abi_storage *parameters, size_t parameter_count, bool result_is_void,
                 qa_abi_storage result) {
  stream_state *state = xmalloc(sizeof *state);
  state->s = s; state->kind = kind; state->number = number;
  qa_windows_import_implementation impl = {state, stream_impl};
  qa_windows_service(s->host, library, name, parameters, parameter_count, result_is_void,
                     result, impl);
}

void qa_install_msvc_streams(qa_windows_service_host *host) {
  if (host->memory->pointer_bytes != 8) return;
  streams_ctx *s = xmalloc(sizeof *s);
  memset(s, 0, sizeof *s);
  s->host = host;
  s->locale = xmalloc(sizeof *s->locale);
  qa_msvc_locale_init(s->locale, host);
  qa_mapped_guest_memory *memory = host->memory;
  qa_abi_storage ptr = QA_STORAGE_POINTER, i32 = QA_STORAGE_INT32, u32 = QA_STORAGE_UINT32,
                 i64 = QA_STORAGE_INT64;
  char *name;
  name = concat3("??1", ios_name, "UEAA@XZ");
  bind(s, ST_BASIC_DTOR, 0, name, &ptr, 1, true, i32);
  qa_abi_storage delete_params[] = {ptr, u32};
  bind(s, ST_BASIC_DELETE, 0, "runtime:basic-ios-delete", delete_params, 2, false, ptr);
  const char *ios_table_names[] = {"runtime:basic-ios-delete"};
  s->ios_table = make_table(s, ios_table_names, 1, "MSVC basic_ios vftable");
  name = concat3("??0", ios_name, "IEAA@XZ");
  bind(s, ST_IOS_CTOR, 0, name, &ptr, 1, false, ptr);
  name = concat3("?rdbuf@", ios_name, "QEBAPEAV?$basic_streambuf@DU?$char_traits@D@std@@@2@XZ");
  bind(s, ST_RDBUF, 0, name, &ptr, 1, false, ptr);
  qa_abi_storage setstate_params[] = {ptr, i32, u32};
  name = concat3("?setstate@", ios_name, "QEAAXH_N@Z");
  bind(s, ST_SETSTATE, 0, name, setstate_params, 3, true, i32);
  bind(s, ST_GOOD, 0, "?good@ios_base@std@@QEBA_NXZ", &ptr, 1, false, u32);
  s->ostream_vb = qa_gmem_allocate(host->memory_ext, 8, false, 0, false, QA_PERM_READ_WRITE,
                                   "MSVC ostream vbtable");
  qa_gmem_write_i32(memory, at(s, s->ostream_vb, 4), 16);
  qa_gmem_protect(host->memory_ext, s->ostream_vb, 8, QA_PERM_READ);
  s->iostream_vb = qa_gmem_allocate(host->memory_ext, 16, false, 0, false, QA_PERM_READ_WRITE,
                                    "MSVC iostream vbtables");
  qa_gmem_write_i32(memory, at(s, s->iostream_vb, 4), 32);
  qa_gmem_write_i32(memory, at(s, s->iostream_vb, 12), 16);
  qa_gmem_protect(host->memory_ext, s->iostream_vb, 16, QA_PERM_READ);
  qa_abi_storage ostream_ctor_params[] = {ptr, ptr, u32, i32};
  name = concat3("??0", ostream_name, "QEAA@PEAV?$basic_streambuf@DU?$char_traits@D@std@@@1@_N@Z");
  bind(s, ST_OSTREAM_CTOR, 0, name, ostream_ctor_params, 4, false, ptr);
  qa_abi_storage iostream_ctor_params[] = {ptr, ptr, i32};
  name = concat3("??0", iostream_name, "QEAA@PEAV?$basic_streambuf@DU?$char_traits@D@std@@@1@@Z");
  bind(s, ST_IOSTREAM_CTOR, 0, name, iostream_ctor_params, 3, false, ptr);
  name = concat3("??1", ostream_name, "UEAA@XZ");
  bind(s, ST_OSTREAM_DTOR, 0, name, &ptr, 1, true, i32);
  name = concat3("??1", iostream_name, "UEAA@XZ");
  bind(s, ST_IOSTREAM_DTOR, 0, name, &ptr, 1, true, i32);
  bind(s, ST_OSTREAM_DELETE, 0, "runtime:ostream-delete", delete_params, 2, false, ptr);
  const char *ostream_table_names[] = {"runtime:ostream-delete"};
  s->ostream_table = make_table(s, ostream_table_names, 1, "MSVC ostream vftable");
  s->iostream_table = make_table(s, ostream_table_names, 1, "MSVC iostream vftable");
  name = concat3("??1", streambuf_name, "UEAA@XZ");
  bind(s, ST_BUFFER_DTOR, 0, name, &ptr, 1, true, i32);
  bind(s, ST_BUFFER_DELETE, 0, "runtime:streambuf-delete", delete_params, 2, false, ptr);
  const char *lock_methods[] = {"_Lock", "_Unlock"};
  for (size_t i = 0; i < 2; i++) {
    char *lock_name = xmalloc(strlen(streambuf_name) + 32);
    (void)snprintf(lock_name, strlen(streambuf_name) + 32, "?%s@%sUEAAXXZ",
                   lock_methods[i], streambuf_name);
    bind(s, ST_VOID0, 0, lock_name, &ptr, 1, true, i32);
  }
  qa_abi_storage ch_params[] = {ptr, i32};
  bind(s, ST_MINUS1_CH, 0, "runtime:streambuf-overflow", ch_params, 2, false, i32);
  bind(s, ST_MINUS1_CH, 0, "runtime:streambuf-pbackfail", ch_params, 2, false, i32);
  bind(s, ST_MINUS1, 0, "runtime:streambuf-underflow", &ptr, 1, false, i32);
  name = concat3("?showmanyc@", streambuf_name, "MEAA_JXZ");
  bind(s, ST_SHOWMANYC, 0, name, &ptr, 1, false, i64);
  name = concat3("?sync@", streambuf_name, "MEAAHXZ");
  bind(s, ST_SYNC, 0, name, &ptr, 1, false, i32);
  qa_abi_storage setbuf_params[] = {ptr, ptr, i64};
  name = concat3("?setbuf@", streambuf_name, "MEAAPEAV12@PEAD_J@Z");
  bind(s, ST_SETBUF, 0, name, setbuf_params, 3, false, ptr);
  qa_abi_storage two_ptrs[] = {ptr, ptr};
  name = concat3("?imbue@", streambuf_name, "MEAAXAEBVlocale@2@@Z");
  bind(s, ST_VOID0, 0, name, two_ptrs, 2, true, i32);
  const struct { const char *name; int offset; } fields[] = {
      {"eback", 24}, {"pbase", 32}, {"gptr", 56}, {"pptr", 64}};
  for (size_t i = 0; i < 4; i++) {
    char *field_name = xmalloc(strlen(streambuf_name) + 24);
    (void)snprintf(field_name, strlen(streambuf_name) + 24, "?%s@%sIEBAPEADXZ",
                   fields[i].name, streambuf_name);
    bind(s, ST_FIELD, fields[i].offset, field_name, &ptr, 1, false, ptr);
  }
  for (int input = 1; input >= 0; input--) {
    char *field_name = xmalloc(strlen(streambuf_name) + 24);
    (void)snprintf(field_name, strlen(streambuf_name) + 24, "?%s@%sIEBAPEADXZ",
                   input ? "egptr" : "epptr", streambuf_name);
    bind(s, ST_EGPTR, input, field_name, &ptr, 1, false, ptr);
  }
  name = concat3("?uflow@", streambuf_name, "MEAAHXZ");
  bind(s, ST_UFLOW, 0, name, &ptr, 1, false, i32);
  name = concat3("?sputc@", streambuf_name, "QEAAHD@Z");
  bind(s, ST_SPUTC, 0, name, ch_params, 2, false, i32);
  for (int input = 1; input >= 0; input--) {
    char *xs_name = xmalloc(strlen(streambuf_name) + 32);
    (void)snprintf(xs_name, strlen(streambuf_name) + 32, "?%s@%sMEAA_J%s_J@Z",
                   input ? "xsgetn" : "xsputn", streambuf_name, input ? "PEAD" : "PEBD");
    qa_abi_storage xs_params[] = {ptr, ptr, i64};
    bind(s, ST_XS, input, xs_name, xs_params, 3, false, i64);
  }
  qa_abi_storage sputn_params[] = {ptr, ptr, i64};
  name = concat3("?sputn@", streambuf_name, "QEAA_JPEBD_J@Z");
  bind(s, ST_SPUTN, 0, name, sputn_params, 3, false, i64);
  qa_abi_storage seekoff_params[] = {ptr, ptr, i64, i32, i32};
  qa_abi_storage seekpos_params[] = {ptr, ptr, ptr, i32};
  bind(s, ST_SEEK, 0, "runtime:streambuf-seekoff", seekoff_params, 5, false, ptr);
  bind(s, ST_SEEK, 0, "runtime:streambuf-seekpos", seekpos_params, 4, false, ptr);
  char *lock_name = concat3("?_Lock@", streambuf_name, "UEAAXXZ");
  char *unlock_name = concat3("?_Unlock@", streambuf_name, "UEAAXXZ");
  char *showmanyc_name = concat3("?showmanyc@", streambuf_name, "MEAA_JXZ");
  char *uflow_name = concat3("?uflow@", streambuf_name, "MEAAHXZ");
  char *xsgetn_name = concat3("?xsgetn@", streambuf_name, "MEAA_JPEAD_J@Z");
  char *xsputn_name = concat3("?xsputn@", streambuf_name, "MEAA_JPEBD_J@Z");
  char *setbuf_name = concat3("?setbuf@", streambuf_name, "MEAAPEAV12@PEAD_J@Z");
  char *sync_name = concat3("?sync@", streambuf_name, "MEAAHXZ");
  char *imbue_name = concat3("?imbue@", streambuf_name, "MEAAXAEBVlocale@2@@Z");
  const char *buffer_table_names[] = {
      "runtime:streambuf-delete", lock_name, unlock_name, "runtime:streambuf-overflow",
      "runtime:streambuf-pbackfail", showmanyc_name, "runtime:streambuf-underflow",
      uflow_name, xsgetn_name, xsputn_name, "runtime:streambuf-seekoff",
      "runtime:streambuf-seekpos", setbuf_name, sync_name, imbue_name};
  s->buffer_table = make_table(s, buffer_table_names, 15, "MSVC streambuf vftable");
  name = concat3("??0", streambuf_name, "IEAA@XZ");
  bind(s, ST_BUFFER_CTOR, 0, name, &ptr, 1, false, ptr);
  name = concat3("?flush@", ostream_name, "QEAAAEAV12@XZ");
  bind(s, ST_FLUSH, 0, name, &ptr, 1, false, ptr);
  name = concat3("?_Osfx@", ostream_name, "QEAAXXZ");
  bind(s, ST_OSFX, 0, name, &ptr, 1, true, i32);
  /* C++ throw/unwind remains an explicit runtime stop (TS comment). */
  bind(s, ST_UNCAUGHT, 0, "?uncaught_exception@std@@YA_NXZ", NULL, 0, false, u32);
  qa_abi_storage tellp_params[] = {ptr, ptr};
  name = concat3("?tellp@", ostream_name, "QEAA?AV?$fpos@U_Mbstatet@@@2@XZ");
  bind(s, ST_TELLP, 0, name, tellp_params, 2, false, ptr);
  for (int b = 0; b < 2; b++) {
    int bits = b == 0 ? 32 : 64;
    char *insert_name = xmalloc(strlen(ostream_name) + 32);
    (void)snprintf(insert_name, strlen(ostream_name) + 32, "??6%sQEAAAEAV01@%s@Z",
                   ostream_name, bits == 32 ? "H" : "_J");
    qa_abi_storage insert_params[] = {ptr, bits == 32 ? i32 : i64};
    bind(s, ST_INSERT_INT, bits, insert_name, insert_params, 2, false, ptr);
  }
  name = concat3("??6", ostream_name,
                 "QEAAAEAV01@PEAV?$basic_streambuf@DU?$char_traits@D@std@@@1@@Z");
  bind(s, ST_INSERT_BUFFER, 0, name, two_ptrs, 2, false, ptr);
  (void)xstrdup; /* names above are intentionally retained like TS strings */
}
