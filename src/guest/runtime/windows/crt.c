/* Port of src/guest/runtime/windows/crt.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/guest/runtime/windows/crt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "qa/foundation/binary.h"
#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/format/services.h"
#include "qa/guest/runtime/common/memory.h"

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
static void plain_error(const char *message) { qa_range_error("%s", message); }
static void type_error(const char *message) { qa_range_error("%s", message); }
static qa_guest_core_address off(const qa_mapped_guest_memory *m, qa_guest_core_address a, int64_t d) {
  return qa_memory_offset(m, a, d);
}
static double now_milliseconds(qa_windows_service_host *host) {
  double value = 0;
  if (host->capabilities.now_milliseconds != NULL &&
      host->capabilities.now_milliseconds(host->capabilities.ctx, &value))
    return value;
  struct timespec ts;
  (void)timespec_get(&ts, TIME_UTC);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

typedef struct crt_ctx {
  qa_windows_service_host *host;
  qa_guest_core_address onexit;
  qa_guest_core_address errno_address;
  qa_guest_core_address time_buffer;
  qa_guest_core_address locale;
} crt_ctx;
typedef struct crt_state {
  crt_ctx *crt;
  int kind;
  char *name; /* owned */
} crt_state;
enum {
  C_XTIME, C_MALLOC, C_CALLOC, C_FREE, C_ZERO, C_MEMCOPY, C_MEMSET, C_MEMCMP, C_MEMCHR,
  C_STRLEN, C_STRCMP, C_STRCHR, C_STRSTR, C_CONFIGURE_ARGV, C_INIT_ENV, C_INIT_ONEXIT,
  C_REGISTER_ONEXIT, C_CRT_ATEXIT, C_EXECUTE_ONEXIT, C_CEXIT, C_INITTERM, C_TYPE_INFO_DESTROY,
  C_QSORT, C_MATH1, C_MATH2, C_POW, C_NEXTAFTERF, C_MODF, C_DCLASS, C_ERRNO, C_STRTOUL,
  C_ATOI, C_TIME64, C_LOCALTIME64, C_LOCALECONV
};
static qa_call_result crt_impl(void *ctx, const qa_guest_core_call_context *context,
                               const qa_call_value *args, size_t count);
static void service(crt_ctx *crt, int kind, const char *family, const char *name,
                    const qa_abi_storage *parameters, size_t parameter_count,
                    bool result_is_void, qa_abi_storage result) {
  char family_library[96];
  if (strcmp(family, "vcruntime") == 0) (void)snprintf(family_library, sizeof family_library, "%s", "vcruntime140.dll");
  else (void)snprintf(family_library, sizeof family_library, "api-ms-win-crt-%s-l1-1-0.dll", family);
  const char *libraries[] = {family_library, "ucrtbase.dll", "msvcrt.dll"};
  for (size_t i = 0; i < 3; i++) {
    crt_state *state = xmalloc(sizeof *state);
    state->crt = crt; state->kind = kind; state->name = xstrdup(name);
    qa_windows_import_implementation impl = {state, crt_impl};
    qa_windows_service(crt->host, libraries[i], name, parameters, parameter_count,
                       result_is_void, result, impl);
  }
}
static double real(const qa_call_value *args, size_t count, size_t index) {
  const qa_call_value *value = qa_guest_argument(args, count, index);
  if (value->kind != QA_CALL_FLOAT32 && value->kind != QA_CALL_FLOAT64)
    type_error("CRT floating argument required");
  return value->number;
}
static char *read_str(crt_ctx *crt, qa_guest_core_address address) {
  return qa_read_string(crt->host->memory, crt->host->memory_ext, address, 1048576);
}

static qa_call_result register_exit(crt_ctx *crt, qa_guest_core_address table,
                                    qa_guest_core_address callback) {
  qa_windows_service_host *host = crt->host;
  qa_mapped_guest_memory *memory = host->memory;
  int width = memory->pointer_bytes;
  qa_guest_core_address begin = {0}, end = {0}, capacity = {0};
  bool has_begin = qa_read_pointer(memory, table, &begin);
  bool has_end = qa_read_pointer(memory, off(memory, table, width), &end);
  bool has_capacity = qa_read_pointer(memory, off(memory, table, width * 2), &capacity);
  if (!has_begin || !has_end || !has_capacity || end.byte_offset == capacity.byte_offset) {
    size_t existing = !has_begin || !has_end ? 0 : (size_t)(end.byte_offset - begin.byte_offset);
    size_t next_size = (size_t)(32 * width) > existing * 2 ? (size_t)(32 * width) : existing * 2;
    qa_guest_core_address next = {0};
    if (!qa_windows_allocate(host, next_size, 0, &next)) return qa_call_int32(-1);
    if (has_begin) {
      qa_move_bytes(memory, host->memory_ext, next, begin, existing);
      (void)qa_windows_free(host, true, begin, 0);
    }
    begin = next;
    end = off(memory, next, (int64_t)existing);
    capacity = off(memory, next, (int64_t)next_size);
    qa_write_pointer(memory, table, true, begin);
    qa_write_pointer(memory, off(memory, table, width * 2), true, capacity);
  }
  qa_write_pointer(memory, end, true, callback);
  qa_write_pointer(memory, off(memory, table, width), true, off(memory, end, width));
  return qa_call_int32(0);
}
static qa_call_result execute_exit(crt_ctx *crt, const qa_guest_core_call_context *context,
                                   qa_guest_core_address table) {
  qa_windows_service_host *host = crt->host;
  qa_mapped_guest_memory *memory = host->memory;
  int width = memory->pointer_bytes;
  qa_guest_core_address begin = {0}, end = {0};
  bool has_begin = qa_read_pointer(memory, table, &begin);
  bool has_end = qa_read_pointer(memory, off(memory, table, width), &end);
  if (has_begin && has_end) {
    for (uint64_t at = end.byte_offset - (uint64_t)width; ; at -= (uint64_t)width) {
      qa_guest_core_address slot = {0};
      if (!qa_memory_pointer(memory, at, &slot)) plain_error("Null onexit slot");
      qa_guest_core_address callback = {0};
      bool has_callback = qa_read_pointer(memory, slot, &callback);
      qa_guest_core_address none = {0};
      qa_write_pointer(memory, slot, false, none);
      if (has_callback) {
        qa_call_result result = qa_windows_invoke(host, context, callback, NULL, 0, true,
                                                  QA_STORAGE_INT32, NULL, 0, false);
        qa_call_value_free(&result);
      }
      if (at <= begin.byte_offset) break;
    }
    (void)qa_windows_free(host, true, begin, 0);
  }
  uint8_t zeros[24] = {0};
  qa_memory_write(memory, table, zeros, (size_t)(width * 3));
  return qa_call_int32(0);
}

typedef struct qsort_ctx {
  crt_ctx *crt;
  const qa_guest_core_call_context *context;
  qa_guest_core_address base, comparator;
  size_t size;
} qsort_ctx;
static qa_guest_core_address qsort_at(qsort_ctx *q, size_t index) {
  return off(q->crt->host->memory, q->base, (int64_t)(index * q->size));
}
static int qsort_compare(qsort_ctx *q, size_t left, size_t right) {
  qa_abi_storage params[] = {QA_STORAGE_POINTER, QA_STORAGE_POINTER};
  qa_call_value invoke_args[] = {qa_call_pointer(true, qsort_at(q, left)),
                                 qa_call_pointer(true, qsort_at(q, right))};
  qa_call_result result = qa_windows_invoke(q->crt->host, q->context, q->comparator, params, 2,
                                            false, QA_STORAGE_INT32, invoke_args, 2, false);
  if (result.kind != QA_CALL_INT32) type_error("qsort comparator must return int");
  return (int)result.number;
}
static void qsort_swap(qsort_ctx *q, size_t left, size_t right) {
  qa_mapped_guest_memory *memory = q->crt->host->memory;
  uint8_t *value = xmalloc(q->size);
  uint8_t *other = xmalloc(q->size);
  qa_memory_copy(memory, qsort_at(q, left), value, q->size);
  qa_memory_copy(memory, qsort_at(q, right), other, q->size);
  qa_memory_write(memory, qsort_at(q, left), other, q->size);
  qa_memory_write(memory, qsort_at(q, right), value, q->size);
  free(value); free(other);
}
static void qsort_sift(qsort_ctx *q, size_t start, size_t end) {
  size_t root = start;
  while (root * 2 + 1 < end) {
    size_t child = root * 2 + 1;
    if (child + 1 < end && qsort_compare(q, child, child + 1) < 0) child++;
    if (qsort_compare(q, root, child) >= 0) return;
    qsort_swap(q, root, child);
    root = child;
  }
}

static bool js_space(uint8_t c) {
  return c == ' ' || (c >= 9 && c <= 13) || c == 0xa0;
}
static qa_call_result crt_impl(void *ctx, const qa_guest_core_call_context *context,
                               const qa_call_value *args, size_t count) {
  crt_state *state = ctx;
  crt_ctx *crt = state->crt;
  qa_windows_service_host *host = crt->host;
  qa_mapped_guest_memory *memory = host->memory;
  int width = memory->pointer_bytes;
  qa_guest_core_address none = {0};
  switch (state->kind) {
    case C_XTIME:
      return qa_call_int64((int64_t)trunc(now_milliseconds(host)) * 10000);
    case C_MALLOC: {
      qa_guest_core_address out = {0};
      bool has = qa_windows_allocate(host, qa_guest_count(args, count, 0), 0, &out);
      return qa_call_pointer(has, out);
    }
    case C_CALLOC: {
      int64_t a = qa_guest_integer(args, count, 0);
      int64_t b = qa_guest_integer(args, count, 1);
      bool too_big = a > 0 && b > 0 && (a > 0x10000000 / b || a * b > 0x10000000);
      if (too_big) return qa_call_pointer(false, none);
      qa_guest_core_address out = {0};
      bool has = qa_windows_allocate(host, (size_t)(a * b), 0, &out);
      return qa_call_pointer(has, out);
    }
    case C_FREE: {
      qa_guest_core_address address = {0};
      bool has = qa_guest_pointer_argument(args, count, 0, &address);
      if (!qa_windows_free(host, has, address, 0))
        plain_error("CRT free of invalid guest allocation");
      return qa_call_void();
    }
    case C_ZERO: return qa_call_int32(0);
    case C_MEMCOPY: {
      size_t size = qa_guest_count(args, count, 2);
      qa_guest_core_address destination = {0};
      bool has_destination = qa_guest_pointer_argument(args, count, 0, &destination);
      if (size > 0)
        qa_move_bytes(memory, host->memory_ext, qa_guest_required_pointer(args, count, 0),
                      qa_guest_required_pointer(args, count, 1), size);
      return qa_call_pointer(has_destination, destination);
    }
    case C_MEMSET: {
      size_t size = qa_guest_count(args, count, 2);
      qa_guest_core_address destination = {0};
      bool has_destination = qa_guest_pointer_argument(args, count, 0, &destination);
      if (size > 0)
        qa_fill_bytes(memory, host->memory_ext, qa_guest_required_pointer(args, count, 0), size,
                      (int)(qa_guest_integer(args, count, 1) & 255));
      return qa_call_pointer(has_destination, destination);
    }
    case C_MEMCMP: {
      size_t size = qa_guest_count(args, count, 2);
      if (size == 0) return qa_call_int32(0);
      uint8_t *left = xmalloc(size), *right = xmalloc(size);
      qa_memory_copy(memory, qa_guest_required_pointer(args, count, 0), left, size);
      qa_memory_copy(memory, qa_guest_required_pointer(args, count, 1), right, size);
      int result = 0;
      for (size_t i = 0; i < size; i++)
        if (left[i] != right[i]) { result = left[i] - right[i]; break; }
      free(left); free(right);
      return qa_call_int32(result);
    }
    case C_MEMCHR: {
      size_t size = qa_guest_count(args, count, 2);
      if (size == 0) return qa_call_pointer(false, none);
      qa_guest_core_address address = qa_guest_required_pointer(args, count, 0);
      uint8_t *bytes = xmalloc(size);
      qa_memory_copy(memory, address, bytes, size);
      uint8_t needle = (uint8_t)(qa_guest_integer(args, count, 1) & 255);
      int64_t index = -1;
      for (size_t i = 0; i < size; i++)
        if (bytes[i] == needle) { index = (int64_t)i; break; }
      free(bytes);
      return index < 0 ? qa_call_pointer(false, none)
                       : qa_call_pointer(true, off(memory, address, index));
    }
    case C_STRLEN: {
      size_t value = qa_string_length(memory, host->memory_ext,
                                      qa_guest_required_pointer(args, count, 0), 1048576);
      return host->pointer_storage == QA_STORAGE_UINT64 ? qa_call_uint64(value)
                                                        : qa_call_uint32((uint32_t)value);
    }
    case C_STRCMP: {
      qa_guest_core_address left = qa_guest_required_pointer(args, count, 0);
      qa_guest_core_address right = qa_guest_required_pointer(args, count, 1);
      size_t maximum = strcmp(state->name, "strcmp") == 0 ? 1048576
                                                           : qa_guest_count(args, count, 2);
      for (size_t i = 0; i < maximum; i++) {
        uint64_t a = qa_read_unsigned(memory, off(memory, left, (int64_t)i), 1);
        uint64_t b = qa_read_unsigned(memory, off(memory, right, (int64_t)i), 1);
        if (a != b) return qa_call_int32((int)a - (int)b);
        if (a == 0) return qa_call_int32(0);
      }
      return qa_call_int32(0);
    }
    case C_STRCHR: {
      qa_guest_core_address address = qa_guest_required_pointer(args, count, 0);
      char *text = read_str(crt, address);
      size_t length = strlen(text);
      uint8_t needle = (uint8_t)(qa_guest_integer(args, count, 1) & 255);
      int64_t index = -1;
      for (size_t i = 0; i <= length; i++)
        if ((uint8_t)text[i] == needle) { index = (int64_t)i; break; }
      free(text);
      return index < 0 ? qa_call_pointer(false, none)
                       : qa_call_pointer(true, off(memory, address, index));
    }
    case C_STRSTR: {
      qa_guest_core_address address = qa_guest_required_pointer(args, count, 0);
      char *text = read_str(crt, address);
      char *needle = read_str(crt, qa_guest_required_pointer(args, count, 1));
      const char *found = strstr(text, needle);
      int64_t index = found != NULL ? found - text : -1;
      free(text); free(needle);
      return index < 0 ? qa_call_pointer(false, none)
                       : qa_call_pointer(true, off(memory, address, index));
    }
    case C_CONFIGURE_ARGV: {
      int64_t mode = qa_guest_integer(args, count, 0);
      if (mode < 0 || mode > 2)
        qa_unsupported_windows_import("ucrtbase.dll", "_configure_narrow_argv",
                                      "invalid argument mode");
      return qa_call_int32(0);
    }
    case C_INIT_ENV: return qa_call_int32(0);
    case C_INIT_ONEXIT: {
      uint8_t zeros[24] = {0};
      qa_memory_write(memory, qa_guest_required_pointer(args, count, 0), zeros,
                      (size_t)(width * 3));
      return qa_call_int32(0);
    }
    case C_REGISTER_ONEXIT:
      return register_exit(crt, qa_guest_required_pointer(args, count, 0),
                           qa_guest_required_pointer(args, count, 1));
    case C_CRT_ATEXIT:
      return register_exit(crt, crt->onexit, qa_guest_required_pointer(args, count, 0));
    case C_EXECUTE_ONEXIT:
      return execute_exit(crt, context, qa_guest_required_pointer(args, count, 0));
    case C_CEXIT:
      (void)execute_exit(crt, context, crt->onexit);
      return qa_call_void();
    case C_INITTERM: {
      bool is_e = strcmp(state->name, "_initterm_e") == 0;
      qa_guest_core_address begin = qa_guest_required_pointer(args, count, 0);
      qa_guest_core_address end = qa_guest_required_pointer(args, count, 1);
      if (end.byte_offset < begin.byte_offset ||
          (end.byte_offset - begin.byte_offset) % (uint64_t)width != 0 ||
          end.byte_offset - begin.byte_offset > 1048576)
        plain_error("Invalid CRT initializer range");
      for (uint64_t at = begin.byte_offset; at < end.byte_offset; at += (uint64_t)width) {
        qa_guest_core_address slot = {0};
        if (!qa_memory_pointer(memory, at, &slot)) plain_error("Null CRT initializer slot");
        qa_guest_core_address callback = {0};
        if (!qa_read_pointer(memory, slot, &callback)) continue;
        qa_call_result result = qa_windows_invoke(host, context, callback, NULL, 0, !is_e,
                                                  QA_STORAGE_INT32, NULL, 0, false);
        if (result.kind == QA_CALL_INT32 && result.number != 0) return result;
        qa_call_value_free(&result);
      }
      return is_e ? qa_call_int32(0) : qa_call_void();
    }
    case C_TYPE_INFO_DESTROY: {
      size_t header_len = width == 4 ? 8 : 16;
      uint8_t header[16];
      qa_memory_copy(memory, qa_guest_required_pointer(args, count, 0), header, header_len);
      bool any = false;
      for (size_t i = 0; i < header_len; i++) if (header[i] != 0) any = true;
      if (any)
        qa_unsupported_windows_import(
            "vcruntime140.dll", "__std_type_info_destroy_list",
            "populated type-name SLIST destruction is not implemented");
      return qa_call_void();
    }
    case C_QSORT: {
      size_t length = qa_guest_count(args, count, 1);
      size_t size = qa_guest_count(args, count, 2);
      if (length < 2) return qa_call_void();
      if (size == 0 || length * size > 0x10000000)
        qa_range_error("Invalid guest qsort extent");
      qsort_ctx q;
      q.crt = crt; q.context = context;
      q.base = qa_guest_required_pointer(args, count, 0);
      q.comparator = qa_guest_required_pointer(args, count, 3);
      q.size = size;
      qa_memory_check(memory, q.base, length * size, QA_ACCESS_WRITE);
      for (int64_t start = (int64_t)(length / 2) - 1; start >= 0; start--)
        qsort_sift(&q, (size_t)start, length);
      for (size_t end = length - 1; end > 0; end--) {
        qsort_swap(&q, 0, end);
        qsort_sift(&q, 0, end);
      }
      return qa_call_void();
    }
    case C_MATH1: {
      double input = real(args, count, 0);
      double value;
      const char *name = state->name;
      if (strcmp(name, "acosf") == 0) value = acos(input);
      else if (strcmp(name, "sinf") == 0) value = sin(input);
      else if (strcmp(name, "ceilf") == 0) value = ceil(input);
      else if (strcmp(name, "cosf") == 0) value = cos(input);
      else if (strcmp(name, "truncf") == 0) value = trunc(input);
      else if (strcmp(name, "log2f") == 0) value = log2(input);
      else if (strcmp(name, "floorf") == 0) value = floor(input);
      else if (strcmp(name, "sqrtf") == 0) value = sqrt(input);
      else value = tan(input);
      return qa_call_float32(value);
    }
    case C_MATH2: {
      double a = real(args, count, 0), b = real(args, count, 1);
      return qa_call_float32(strcmp(state->name, "atan2f") == 0 ? atan2(a, b) : fmod(a, b));
    }
    case C_POW: return qa_call_float64(pow(real(args, count, 0), real(args, count, 1)));
    case C_NEXTAFTERF: {
      double a = (float)real(args, count, 0);
      double b = (float)real(args, count, 1);
      if (a != a || b != b) return qa_call_float32(a + b);
      if (a == b) return qa_call_float32(b);
      uint32_t bits = qa_float32_bits(a);
      if (a == 0) bits = b < 0 ? 0x80000001u : 1u;
      else bits = bits + ((b > a) == (a > 0) ? 1u : (uint32_t)-1);
      return qa_call_float32(qa_float32_from_bits(bits));
    }
    case C_MODF: {
      double a = real(args, count, 0);
      double whole = trunc(a);
      qa_gmem_write_f64(memory, qa_guest_required_pointer(args, count, 1), whole);
      double fraction = isfinite(a) ? a - whole : (a != a ? a : 0);
      /* Preserve -0 exactly where TS does. */
      if (fraction == 0 && signbit(a)) fraction = -0.0;
      return qa_call_float64(fraction);
    }
    case C_DCLASS: {
      double a = real(args, count, 0);
      int value;
      if (strcmp(state->name, "_dsign") == 0)
        value = signbit(a) ? 0x8000 : 0;
      else
        value = a != a ? 2 : !isfinite(a) ? 1 : a == 0 ? 0
            : fabs(a) < (strcmp(state->name, "_fdclass") == 0 ? 0x1p-126 : 0x1p-1022) ? -2 : -1;
      return qa_call_int32(value == 0x8000 ? -32768 : value);
    }
    case C_ERRNO: return qa_call_pointer(true, crt->errno_address);
    case C_STRTOUL: {
      qa_guest_core_address source = qa_guest_required_pointer(args, count, 0);
      qa_guest_core_address end = {0};
      bool has_end = qa_guest_pointer_argument(args, count, 1, &end);
      char *text = read_str(crt, source);
      size_t length = strlen(text);
      int base = (int)qa_guest_integer(args, count, 2);
      size_t index = 0;
      if (base != 0 && (base < 2 || base > 36)) {
        qa_write_unsigned(memory, crt->errno_address, 4, 22);
        if (has_end) qa_write_pointer(memory, end, true, source);
        free(text);
        return qa_call_uint32(0);
      }
      /* TS regex /[ \t\n\r\f\v]/ for this skip. */
      while (index < length && strchr(" \t\n\r\f\v", text[index]) != NULL) index++;
      bool negative = text[index] == '-';
      if (negative || text[index] == '+') index++;
      const char *digit_of = text;
      size_t digit_len = length;
      (void)digit_of; (void)digit_len;
      /* digit(at): 99 when out of range or not alphanumeric. */
      if ((base == 0 || base == 16) && text[index] == '0' &&
          (text[index + 1] == 'x' || text[index + 1] == 'X')) {
        char c2 = text[index + 2];
        int d2 = c2 >= '0' && c2 <= '9' ? c2 - '0' : c2 >= 'A' && c2 <= 'Z' ? c2 - 55
                : c2 >= 'a' && c2 <= 'z' ? c2 - 87 : 99;
        if (d2 < 16) { base = 16; index += 2; }
      }
      if (base == 0) base = text[index] == '0' ? 8 : 10;
      size_t first = index;
      uint64_t value = 0;
      bool overflowed = false;
      while (index < length) {
        char c = text[index];
        int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'Z' ? c - 55
                   : c >= 'a' && c <= 'z' ? c - 87 : 99;
        if (digit >= base) break;
        value = value * (uint64_t)base + (uint64_t)digit;
        if (value > 0xffffffffULL) { overflowed = true; value = 0xffffffffULL; }
        index++;
      }
      if (index == first) {
        if (has_end) qa_write_pointer(memory, end, true, source);
        free(text);
        return qa_call_uint32(0);
      }
      if (has_end) qa_write_pointer(memory, end, true, off(memory, source, (int64_t)index));
      free(text);
      if (overflowed) {
        qa_write_unsigned(memory, crt->errno_address, 4, 34);
        return qa_call_uint32(0xffffffffu);
      }
      return qa_call_uint32((uint32_t)(negative ? 0 - value : value));
    }
    case C_ATOI: {
      char *text = read_str(crt, qa_guest_required_pointer(args, count, 0));
      const char *name = state->name;
      if (strcmp(name, "atof") == 0) {
        size_t i = 0;
        while (js_space((uint8_t)text[i])) i++;
        size_t sign_at = i;
        bool neg = false;
        if (text[i] == '+' || text[i] == '-') { neg = text[i] == '-'; i++; }
        double value;
        if (strncmp(text + i, "Infinity", 8) == 0) value = neg ? -(1.0 / 0.0) : 1.0 / 0.0;
        else if (text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X'))
          value = neg ? -0.0 : 0.0;
        else if (!((text[i] >= '0' && text[i] <= '9') ||
                   (text[i] == '.' && text[i + 1] >= '0' && text[i + 1] <= '9')))
          value = 0; /* parseFloat NaN, mapped to 0 by TS */
        else value = strtod(text + sign_at, NULL);
        free(text);
        return qa_call_float64(value != value ? 0 : value);
      }
      size_t i = 0;
      while (js_space((uint8_t)text[i])) i++;
      bool neg = false;
      if (text[i] == '+' || text[i] == '-') { neg = text[i] == '-'; i++; }
      uint64_t magnitude = 0;
      bool any = false;
      while (text[i] >= '0' && text[i] <= '9') {
        magnitude = magnitude * 10 + (uint64_t)(text[i] - '0');
        i++; any = true;
      }
      if (!any) magnitude = 0;
      uint64_t bits = neg ? 0 - magnitude : magnitude;
      free(text);
      return strcmp(name, "atoi") == 0 ? qa_call_int32((int32_t)bits)
                                       : qa_call_int64((int64_t)bits);
    }
    case C_TIME64: {
      int64_t time_value = (int64_t)floor(now_milliseconds(host) / 1000.0);
      qa_guest_core_address out = {0};
      if (qa_guest_pointer_argument(args, count, 0, &out))
        qa_write_unsigned(memory, out, 8, (uint64_t)time_value);
      return qa_call_int64(time_value);
    }
    case C_LOCALTIME64: {
      int64_t value = (int64_t)qa_read_unsigned(
          memory, qa_guest_required_pointer(args, count, 0), 8);
      if (value < 0 || value > 32535215999LL) return qa_call_pointer(false, none);
      time_t t = (time_t)value;
      struct tm local_tm;
      (void)localtime_r(&t, &local_tm);
      int year = local_tm.tm_year + 1900;
      /* TS DST rule via getTimezoneOffset at Jan 1 / Jul 1 of that year. */
      struct tm jan = local_tm, jul = local_tm;
      jan.tm_mon = 0; jan.tm_mday = 1; jan.tm_hour = 0; jan.tm_min = 0; jan.tm_sec = 0;
      jan.tm_isdst = -1;
      jul.tm_mon = 6; jul.tm_mday = 1; jul.tm_hour = 0; jul.tm_min = 0; jul.tm_sec = 0;
      jul.tm_isdst = -1;
      time_t jan_t = mktime(&jan), jul_t = mktime(&jul);
      struct tm jan_g, jul_g, cur_g;
      (void)gmtime_r(&jan_t, &jan_g); jan_g.tm_isdst = 0;
      (void)gmtime_r(&jul_t, &jul_g); jul_g.tm_isdst = 0;
      (void)gmtime_r(&t, &cur_g); cur_g.tm_isdst = 0;
      /* getTimezoneOffset minutes = (mktime(gmtime fields as local) - t)/60. */
      long jan_off = (long)((mktime(&jan_g) - jan_t) / 60);
      long jul_off = (long)((mktime(&jul_g) - jul_t) / 60);
      long cur_off = (long)((mktime(&cur_g) - t) / 60);
      long max_off = jan_off > jul_off ? jan_off : jul_off;
      int isdst = cur_off < max_off ? 1 : 0;
      int fields[] = {local_tm.tm_sec, local_tm.tm_min, local_tm.tm_hour, local_tm.tm_mday,
                      local_tm.tm_mon, year - 1900, local_tm.tm_wday, local_tm.tm_yday, isdst};
      for (size_t i = 0; i < 9; i++)
        qa_write_unsigned(memory, off(memory, crt->time_buffer, (int64_t)i * 4), 4,
                          (uint64_t)(uint32_t)fields[i]);
      return qa_call_pointer(true, crt->time_buffer);
    }
    case C_LOCALECONV: return qa_call_pointer(true, crt->locale);
    default: break;
  }
  (void)none;
  return qa_call_void();
}

void qa_install_crt(qa_windows_service_host *host) {
  crt_ctx *crt = xmalloc(sizeof *crt);
  memset(crt, 0, sizeof *crt);
  crt->host = host;
  qa_mapped_guest_memory *memory = host->memory;
  int width = memory->pointer_bytes;
  qa_abi_storage ptr = QA_STORAGE_POINTER, i32 = QA_STORAGE_INT32, i64 = QA_STORAGE_INT64,
                 f32 = QA_STORAGE_FLOAT32, f64 = QA_STORAGE_FLOAT64;
  crt_state *xtime = xmalloc(sizeof *xtime);
  xtime->crt = crt; xtime->kind = C_XTIME; xtime->name = xstrdup("_Xtime_get_ticks");
  qa_windows_import_implementation xtime_impl = {xtime, crt_impl};
  qa_windows_service(host, "msvcp140.dll", "_Xtime_get_ticks", NULL, 0, false, i64, xtime_impl);
  qa_abi_storage size_param = host->pointer_storage;
  service(crt, C_MALLOC, "heap", "malloc", &size_param, 1, false, ptr);
  qa_abi_storage two_sizes[] = {size_param, size_param};
  service(crt, C_CALLOC, "heap", "calloc", two_sizes, 2, false, ptr);
  service(crt, C_FREE, "heap", "free", &ptr, 1, true, i32);
  service(crt, C_ZERO, "heap", "_callnewh", &size_param, 1, false, i32);
  qa_abi_storage copy_params[] = {ptr, ptr, size_param};
  service(crt, C_MEMCOPY, "vcruntime", "memcpy", copy_params, 3, false, ptr);
  service(crt, C_MEMCOPY, "vcruntime", "memmove", copy_params, 3, false, ptr);
  qa_abi_storage memset_params[] = {ptr, i32, size_param};
  service(crt, C_MEMSET, "vcruntime", "memset", memset_params, 3, false, ptr);
  service(crt, C_MEMCMP, "vcruntime", "memcmp", copy_params, 3, false, i32);
  service(crt, C_MEMCHR, "vcruntime", "memchr", memset_params, 3, false, ptr);
  service(crt, C_STRLEN, "string", "strlen", &ptr, 1, false, size_param);
  qa_abi_storage two_ptrs[] = {ptr, ptr};
  qa_abi_storage strncmp_params[] = {ptr, ptr, size_param};
  service(crt, C_STRCMP, "string", "strcmp", two_ptrs, 2, false, i32);
  service(crt, C_STRCMP, "string", "strncmp", strncmp_params, 3, false, i32);
  qa_abi_storage strchr_params[] = {ptr, i32};
  service(crt, C_STRCHR, "vcruntime", "strchr", strchr_params, 2, false, ptr);
  service(crt, C_STRSTR, "vcruntime", "strstr", two_ptrs, 2, false, ptr);
  service(crt, C_CONFIGURE_ARGV, "runtime", "_configure_narrow_argv", &i32, 1, false, i32);
  service(crt, C_INIT_ENV, "runtime", "_initialize_narrow_environment", NULL, 0, false, i32);
  crt->onexit = qa_gmem_allocate(host->memory_ext, (size_t)(width * 3), false, 0, false,
                                 QA_PERM_READ_WRITE, "UCRT process onexit table");
  service(crt, C_INIT_ONEXIT, "runtime", "_initialize_onexit_table", &ptr, 1, false, i32);
  service(crt, C_REGISTER_ONEXIT, "runtime", "_register_onexit_function", two_ptrs, 2, false,
          i32);
  service(crt, C_CRT_ATEXIT, "runtime", "_crt_atexit", &ptr, 1, false, i32);
  service(crt, C_EXECUTE_ONEXIT, "runtime", "_execute_onexit_table", &ptr, 1, false, i32);
  service(crt, C_CEXIT, "runtime", "_cexit", NULL, 0, true, i32);
  service(crt, C_INITTERM, "runtime", "_initterm", two_ptrs, 2, true, i32);
  service(crt, C_INITTERM, "runtime", "_initterm_e", two_ptrs, 2, false, i32);
  service(crt, C_TYPE_INFO_DESTROY, "vcruntime", "__std_type_info_destroy_list", &ptr, 1,
          true, i32);
  /* installMath */
  const char *unary[] = {"acosf", "sinf", "ceilf", "cosf", "truncf",
                         "log2f", "floorf", "sqrtf", "tanf"};
  for (size_t i = 0; i < 9; i++)
    service(crt, C_MATH1, "math", unary[i], &f32, 1, false, f32);
  qa_abi_storage two_f32[] = {f32, f32};
  service(crt, C_MATH2, "math", "atan2f", two_f32, 2, false, f32);
  service(crt, C_MATH2, "math", "fmodf", two_f32, 2, false, f32);
  qa_abi_storage two_f64[] = {f64, f64};
  service(crt, C_POW, "math", "pow", two_f64, 2, false, f64);
  service(crt, C_NEXTAFTERF, "math", "nextafterf", two_f32, 2, false, f32);
  qa_abi_storage modf_params[] = {f64, ptr};
  service(crt, C_MODF, "math", "modf", modf_params, 2, false, f64);
  const char *dclass_names[] = {"_dclass", "_fdclass", "_dsign"};
  for (size_t i = 0; i < 3; i++) {
    qa_abi_storage param = strcmp(dclass_names[i], "_fdclass") == 0 ? f32 : f64;
    service(crt, C_DCLASS, "math", dclass_names[i], &param, 1, false, QA_STORAGE_INT16);
  }
  /* installConversions */
  crt->errno_address = qa_gmem_allocate(host->memory_ext, 4, false, 0, false,
                                        QA_PERM_READ_WRITE, "CRT thread errno");
  service(crt, C_ERRNO, "runtime", "_errno", NULL, 0, false, ptr);
  qa_install_windows_format(host, crt->errno_address);
  qa_abi_storage strtoul_params[] = {ptr, ptr, i32};
  service(crt, C_STRTOUL, "convert", "strtoul", strtoul_params, 3, false, QA_STORAGE_UINT32);
  service(crt, C_ATOI, "convert", "atoi", &ptr, 1, false, i32);
  service(crt, C_ATOI, "convert", "atoll", &ptr, 1, false, i64);
  service(crt, C_ATOI, "convert", "atof", &ptr, 1, false, f64);
  crt->time_buffer = qa_gmem_allocate(host->memory_ext, 36, false, 0, false,
                                      QA_PERM_READ_WRITE, "CRT thread local tm");
  service(crt, C_TIME64, "time", "_time64", &ptr, 1, false, i64);
  service(crt, C_LOCALTIME64, "time", "_localtime64", &ptr, 1, false, ptr);
  crt->locale = qa_gmem_allocate(host->memory_ext, (size_t)(width * 10 + 16), false, 0, false,
                                 QA_PERM_READ_WRITE, "CRT C locale lconv");
  qa_guest_core_address empty = qa_gmem_allocate(host->memory_ext, 1, false, 0, false,
                                            QA_PERM_READ_WRITE, NULL);
  qa_guest_core_address decimal = qa_gmem_allocate(host->memory_ext, 2, false, 0, false,
                                              QA_PERM_READ_WRITE, NULL);
  size_t decimal_len = 0;
  uint8_t *decimal_bytes = qa_string_bytes(".", false, &decimal_len);
  qa_memory_write(memory, decimal, decimal_bytes, decimal_len);
  free(decimal_bytes);
  for (int i = 0; i < 10; i++)
    qa_write_pointer(memory, off(memory, crt->locale, (int64_t)i * width), true,
                     i == 0 ? decimal : empty);
  uint8_t fill[14];
  memset(fill, 127, sizeof fill);
  qa_memory_write(memory, off(memory, crt->locale, width * 10), fill, sizeof fill);
  service(crt, C_LOCALECONV, "locale", "localeconv", NULL, 0, false, ptr);
  qa_abi_storage qsort_params[] = {ptr, size_param, size_param, ptr};
  service(crt, C_QSORT, "utility", "qsort", qsort_params, 4, true, i32);
}
