/* Port of src/guest/runtime/system-v/libc.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/system_v/libc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static void type_error(const char *message) { qa_range_error("%s", message); }
static void plain_error(const char *message) { qa_range_error("%s", message); }
static qa_guest_address off(const qa_mapped_guest_memory *m, qa_guest_address a, int64_t d) {
  return qa_memory_offset(m, a, d);
}
static double floating(const qa_call_value *args, size_t count, size_t index) {
  const qa_call_value *value = qa_guest_argument(args, count, index);
  if (value->kind != QA_CALL_FLOAT32 && value->kind != QA_CALL_FLOAT64)
    type_error("System V floating argument required");
  return value->number;
}
static qa_call_result size_value(qa_system_v_service_host *host, size_t value) {
  return host->pointer_storage == QA_STORAGE_UINT32 ? qa_call_uint32((uint32_t)value)
                                                    : qa_call_uint64(value);
}
static qa_call_result signed_size_value(qa_system_v_service_host *host, int64_t value) {
  return host->signed_pointer_storage == QA_STORAGE_INT32 ? qa_call_int32((int32_t)value)
                                                          : qa_call_int64(value);
}
/* TS compare() over latin1 byte strings. */
static int compare_strings(const char *a, const char *b) {
  size_t la = strlen(a), lb = strlen(b);
  size_t length = la < lb ? la : lb;
  for (size_t i = 0; i < length; i++)
    if ((uint8_t)a[i] != (uint8_t)b[i]) return (uint8_t)a[i] - (uint8_t)b[i];
  if (la == lb) return 0;
  return la < lb ? -(uint8_t)b[length] : (uint8_t)a[length];
}

typedef struct libc_state {
  qa_system_v_service_host *host;
  int kind;
  char *name; /* owned loop-variant name */
  bool flag;  /* checked / wide-storage variant */
  qa_guest_address token_slot;
} libc_state;
enum {
  L_ERRNO_LOCATION, L_TLS_GET_ADDR, L_MALLOC, L_CALLOC, L_FREE, L_REALLOC, L_MEMCOPY, L_MEMSET,
  L_MEMCMP, L_STRLEN, L_STRCMP, L_STRCPY, L_STRNCPY, L_STRCHR, L_STRSTR, L_STRTOK, L_STRTOL,
  L_TIME, L_QSORT, L_MATH1, L_ATAN2, L_SINCOS, L_ISNANF, L_STACK_CHK
};
static qa_call_result libc_impl(void *ctx, const qa_guest_call_context *context,
                                const qa_call_value *args, size_t count);
static void reg(libc_state *proto, int kind, const char *library, const char *name,
                const char *const *versions, size_t version_count,
                const qa_abi_storage *parameters, size_t parameter_count, bool result_is_void,
                qa_abi_storage result) {
  libc_state *state = xmalloc(sizeof *state);
  *state = *proto;
  state->kind = kind;
  state->name = xstrdup(name);
  qa_system_v_implementation impl = {state, libc_impl};
  qa_system_v_service(state->host, library, name, versions, version_count, parameters,
                      parameter_count, result_is_void, result, impl);
}

static char *read_str(libc_state *state, qa_guest_address address) {
  return qa_read_string(state->host->memory, state->host->memory_ext, address, 1048576);
}

static qa_call_result libc_impl(void *ctx, const qa_guest_call_context *context,
                                const qa_call_value *args, size_t count) {
  libc_state *state = ctx;
  qa_system_v_service_host *host = state->host;
  qa_mapped_guest_memory *m = host->memory;
  switch (state->kind) {
    case L_ERRNO_LOCATION: return qa_call_pointer(true, host->errno_address);
    case L_TLS_GET_ADDR: {
      qa_guest_address index = qa_guest_required_pointer(args, count, 0);
      return qa_call_pointer(true, host->tls_address(host->ctx, qa_memory_read_u64(m, index),
                                                     (int64_t)qa_memory_read_u64(m, off(m, index, 8))));
    }
    case L_MALLOC:
      return qa_call_pointer(true, qa_system_v_allocate(host, qa_guest_count(args, count, 0)));
    case L_CALLOC: {
      int64_t a = qa_guest_integer(args, count, 0);
      int64_t b = qa_guest_integer(args, count, 1);
      /* TS BigInt product compared against 0x10000000. */
      bool too_big = a > 0 && b > 0 && a > 0x10000000 / b;
      if (a > 0 && b > 0 && !too_big && a * b > 0x10000000) too_big = true;
      if (too_big) {
        qa_system_v_errno_set(host, 12);
        qa_guest_address none = {0};
        return qa_call_pointer(false, none);
      }
      /* A negative total reaches allocate() and RangeErrors there, as in TS. */
      return qa_call_pointer(true, qa_system_v_allocate(host, (size_t)(a * b)));
    }
    case L_FREE: {
      qa_guest_address address = {0};
      bool has = qa_guest_pointer_argument(args, count, 0, &address);
      qa_system_v_free(host, has, address);
      return qa_call_void();
    }
    case L_REALLOC: {
      qa_guest_address old = {0};
      bool has_old = qa_guest_pointer_argument(args, count, 0, &old);
      size_t size = qa_guest_count(args, count, 1);
      if (size == 0 && has_old) {
        qa_system_v_free(host, true, old);
        qa_guest_address none = {0};
        return qa_call_pointer(false, none);
      }
      size_t old_size = 0;
      bool has_size = true;
      if (has_old) has_size = qa_system_v_allocation_size(host, old, &old_size);
      if (!has_size) plain_error("System V realloc of non-live allocation");
      qa_guest_address next = qa_system_v_allocate(host, size);
      if (has_old) {
        qa_move_bytes(m, host->memory_ext, next, old, size < old_size ? size : old_size);
        qa_system_v_free(host, true, old);
      }
      return qa_call_pointer(true, next);
    }
    case L_MEMCOPY: {
      qa_guest_address destination = {0}, source = {0};
      bool has_destination = qa_guest_pointer_argument(args, count, 0, &destination);
      bool has_source = qa_guest_pointer_argument(args, count, 1, &source);
      size_t length = qa_guest_count(args, count, 2);
      bool checked = state->name[0] == '_' && state->name[1] == '_';
      if (checked && (int64_t)length > qa_guest_integer(args, count, 3))
        qa_range_error("%s detected guest buffer overflow", state->name);
      if (length != 0) {
        if (!has_destination || !has_source) type_error("Nonnull guest memory arguments required");
        qa_move_bytes(m, host->memory_ext, destination, source, length);
      }
      return qa_call_pointer(has_destination, destination);
    }
    case L_MEMSET: {
      qa_guest_address destination = {0};
      bool has_destination = qa_guest_pointer_argument(args, count, 0, &destination);
      size_t length = qa_guest_count(args, count, 2);
      if (length != 0) {
        if (!has_destination) type_error("Nonnull memset destination required");
        qa_fill_bytes(m, host->memory_ext, destination, length,
                      (int)(qa_guest_integer_u64(args, count, 1) & 255u));
      }
      return qa_call_pointer(has_destination, destination);
    }
    case L_MEMCMP: {
      size_t length = qa_guest_count(args, count, 2);
      if (length == 0) return qa_call_int32(0);
      uint8_t *a = xmalloc(length), *b = xmalloc(length);
      qa_memory_copy(m, qa_guest_required_pointer(args, count, 0), a, length);
      qa_memory_copy(m, qa_guest_required_pointer(args, count, 1), b, length);
      int result = 0;
      for (size_t i = 0; i < length; i++)
        if (a[i] != b[i]) { result = (int)a[i] - (int)b[i]; break; }
      free(a); free(b);
      return qa_call_int32(result);
    }
    case L_STRLEN:
      return size_value(host, qa_string_length(m, host->memory_ext,
                                               qa_guest_required_pointer(args, count, 0), 1048576));
    case L_STRCMP: {
      char *a = read_str(state, qa_guest_required_pointer(args, count, 0));
      char *b = read_str(state, qa_guest_required_pointer(args, count, 1));
      int result = compare_strings(a, b);
      free(a); free(b);
      return qa_call_int32(result);
    }
    case L_STRCPY: {
      bool checked = strncmp(state->name, "__", 2) == 0;
      bool concatenate = strstr(state->name, "strcat") != NULL;
      bool end = strstr(state->name, "stpcpy") != NULL;
      qa_guest_address destination = qa_guest_required_pointer(args, count, 0);
      char *text = read_str(state, qa_guest_required_pointer(args, count, 1));
      size_t prefix = concatenate ? qa_string_length(m, host->memory_ext, destination, 1048576) : 0;
      if (checked && (int64_t)(prefix + strlen(text) + 1) > qa_guest_integer(args, count, 2))
        qa_range_error("%s detected guest buffer overflow", state->name);
      size_t len = 0;
      uint8_t *bytes = qa_string_bytes(text, false, &len);
      qa_memory_write(m, off(m, destination, (int64_t)prefix), bytes, len);
      free(bytes);
      qa_guest_address result = end ? off(m, destination, (int64_t)strlen(text)) : destination;
      free(text);
      return qa_call_pointer(true, result);
    }
    case L_STRNCPY: {
      size_t length = qa_guest_count(args, count, 2);
      qa_guest_address destination = {0};
      bool has_destination = qa_guest_pointer_argument(args, count, 0, &destination);
      if (length == 0) return qa_call_pointer(has_destination, destination);
      if (!has_destination) type_error("Nonnull strncpy destination required");
      qa_guest_address source = qa_guest_required_pointer(args, count, 1);
      uint8_t *bytes = xmalloc(length);
      memset(bytes, 0, length);
      for (size_t i = 0; i < length; i++) {
        uint8_t byte = qa_memory_read_u8(m, off(m, source, (int64_t)i));
        if (byte == 0) break;
        bytes[i] = byte;
      }
      qa_memory_write(m, destination, bytes, length);
      free(bytes);
      return qa_call_pointer(true, destination);
    }
    case L_STRCHR: {
      qa_guest_address source = qa_guest_required_pointer(args, count, 0);
      char *text = read_str(state, source);
      size_t length = strlen(text);
      uint8_t needle = (uint8_t)(qa_guest_integer_u64(args, count, 1) & 255u);
      /* TS searches readString()+"\0", so index length (the terminator) can match. */
      int64_t index = -1;
      if (strcmp(state->name, "strchr") == 0) {
        for (size_t i = 0; i <= length; i++)
          if ((uint8_t)text[i] == needle) { index = (int64_t)i; break; }
      } else {
        for (size_t i = length + 1; i-- > 0;)
          if ((uint8_t)text[i] == needle) { index = (int64_t)i; break; }
      }
      free(text);
      qa_guest_address none = {0};
      return index < 0 ? qa_call_pointer(false, none)
                       : qa_call_pointer(true, off(m, source, index));
    }
    case L_STRSTR: {
      qa_guest_address source = qa_guest_required_pointer(args, count, 0);
      char *text = read_str(state, source);
      char *needle = read_str(state, qa_guest_required_pointer(args, count, 1));
      int64_t index = -1;
      if (strcmp(state->name, "strstr") == 0) {
        const char *found = strstr(text, needle);
        if (found != NULL) index = found - text;
      } else {
        for (size_t i = 0; text[i] != '\0'; i++)
          if (strchr(needle, text[i]) != NULL) { index = (int64_t)i; break; }
      }
      free(text); free(needle);
      qa_guest_address none = {0};
      return index < 0 ? qa_call_pointer(false, none)
                       : qa_call_pointer(true, off(m, source, index));
    }
    case L_STRTOK: {
      qa_guest_address source = {0};
      bool has_source = qa_guest_pointer_argument(args, count, 0, &source);
      if (!has_source) has_source = qa_gmem_read_pointer(m, state->token_slot, &source);
      char *delimiters = read_str(state, qa_guest_required_pointer(args, count, 1));
      qa_guest_address none = {0};
      if (!has_source) { free(delimiters); return qa_call_pointer(false, none); }
      char *text = read_str(state, source);
      size_t length = strlen(text), start = 0;
      while (start < length && strchr(delimiters, text[start]) != NULL) start++;
      if (start == length) {
        qa_gmem_write_pointer(m, state->token_slot, false, none);
        free(text); free(delimiters);
        return qa_call_pointer(false, none);
      }
      size_t end = start;
      while (end < length && strchr(delimiters, text[end]) == NULL) end++;
      if (end < length) {
        qa_memory_write_u8(m, off(m, source, (int64_t)end), 0);
        qa_gmem_write_pointer(m, state->token_slot, true, off(m, source, (int64_t)(end + 1)));
      } else {
        qa_gmem_write_pointer(m, state->token_slot, false, none);
      }
      free(text); free(delimiters);
      return qa_call_pointer(true, off(m, source, (int64_t)start));
    }
    case L_STRTOL: {
      qa_guest_address source = qa_guest_required_pointer(args, count, 0);
      qa_guest_address end_pointer = {0};
      bool has_end = qa_guest_pointer_argument(args, count, 1, &end_pointer);
      char *text = read_str(state, source);
      size_t length = strlen(text);
      int radix = (int)qa_guest_integer(args, count, 2);
      size_t index = 0;
      int64_t sign = 1;
      if (radix != 0 && (radix < 2 || radix > 36)) {
        qa_system_v_errno_set(host, 22);
        if (has_end) qa_gmem_write_pointer(m, end_pointer, true, source);
        free(text);
        return signed_size_value(host, 0);
      }
      while (index < length && strchr(" \t\n\r\v\f", text[index]) != NULL) index++;
      if (text[index] == '-' || text[index] == '+') { if (text[index] == '-') sign = -1; index++; }
      bool hex_prefix = (radix == 0 || radix == 16) && index + 2 < length + 1 && text[index] == '0' &&
          (text[index + 1] == 'x' || text[index + 1] == 'X') &&
          ((text[index + 2] >= '0' && text[index + 2] <= '9') ||
           (text[index + 2] >= 'a' && text[index + 2] <= 'f') ||
           (text[index + 2] >= 'A' && text[index + 2] <= 'F'));
      if (hex_prefix) { radix = 16; index += 2; }
      if (radix == 0) radix = text[index] == '0' ? 8 : 10;
      size_t first = index;
      /* TS accumulates in BigInt while value <= 2^(bits-1), then stops.
       * Saturating one past the limit preserves every later comparison. */
      uint64_t limit = 1ULL << (m->pointer_bytes * 8 - 1);
      uint64_t value = 0;
      for (; index < length; index++) {
        char c = text[index];
        int digit = c >= '0' && c <= '9' ? c - '0'
            : c >= 'a' && c <= 'z' ? c - 'a' + 10
            : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : -1;
        if (digit < 0 || digit >= radix) break;
        if (value <= limit) {
          if (value > (UINT64_MAX - (uint64_t)digit) / (uint64_t)radix) value = limit + 1;
          else {
            value = value * (uint64_t)radix + (uint64_t)digit;
            if (value > limit) value = limit + 1;
          }
        }
      }
      if (has_end)
        qa_gmem_write_pointer(m, end_pointer, true,
                              off(m, source, (int64_t)(index == first ? 0 : index)));
      free(text);
      if (sign > 0 && value >= limit) {
        qa_system_v_errno_set(host, 34);
        return signed_size_value(host, (int64_t)(limit - 1));
      }
      if (sign < 0 && value > limit) {
        qa_system_v_errno_set(host, 34);
        return signed_size_value(host, (int64_t)(~limit + 1));
      }
      return signed_size_value(host, sign > 0 ? (int64_t)value : (int64_t)(~value + 1));
    }
    case L_TIME: {
      if (host->capabilities.now_seconds == NULL)
        qa_unsupported_system_v_service(
            "libc.so.6", "time", m->pointer_bytes == 4 ? "GLIBC_2.0" : "GLIBC_2.2.5",
            "no deterministic clock capability supplied");
      int64_t value = 0;
      (void)host->capabilities.now_seconds(host->capabilities.ctx, &value);
      qa_guest_address destination = {0};
      if (qa_guest_pointer_argument(args, count, 0, &destination)) {
        if (m->pointer_bytes == 4) qa_gmem_write_i32(m, destination, (int32_t)value);
        else qa_gmem_write_i64(m, destination, value);
      }
      return signed_size_value(host, value);
    }
    case L_QSORT: {
      qa_guest_address base = {0};
      bool has_base = qa_guest_pointer_argument(args, count, 0, &base);
      size_t length = qa_guest_count(args, count, 1);
      size_t size = qa_guest_count(args, count, 2);
      qa_guest_address comparator = qa_guest_required_pointer(args, count, 3);
      if (length < 2 || size == 0) return qa_call_void();
      if (!has_base || length * size > 0x10000000)
        qa_range_error("qsort range outside supported guest memory");
      qa_memory_check(m, base, length * size, QA_ACCESS_WRITE);
      /* Heap sort exactly as TS: less() invokes the guest comparator. */
      for (size_t pass = 0; pass < 2; pass++) { (void)pass; break; }
      {
        /* sift/swap inlined as loops below via helper macros would hide the
         * comparator calls; implement with explicit loops. */
        size_t n = length;
        for (int64_t start = (int64_t)(n / 2) - 1; start >= 0; start--) {
          size_t root = (size_t)start;
          for (;;) {
            size_t child = root * 2 + 1;
            if (child >= n) break;
            if (child + 1 < n) {
              qa_abi_storage params[] = {QA_STORAGE_POINTER, QA_STORAGE_POINTER};
              qa_call_value cargs[] = {qa_call_pointer(true, off(m, base, (int64_t)(child * size))),
                                       qa_call_pointer(true, off(m, base, (int64_t)((child + 1) * size)))};
              qa_call_result r = qa_system_v_invoke(host, context, comparator, params, 2, false,
                                                    QA_STORAGE_INT32, cargs, 2);
              if (r.kind != QA_CALL_INT32) type_error("qsort comparator returned wrong type");
              if (r.number < 0) child++;
            }
            {
              qa_abi_storage params[] = {QA_STORAGE_POINTER, QA_STORAGE_POINTER};
              qa_call_value cargs[] = {qa_call_pointer(true, off(m, base, (int64_t)(root * size))),
                                       qa_call_pointer(true, off(m, base, (int64_t)(child * size)))};
              qa_call_result r = qa_system_v_invoke(host, context, comparator, params, 2, false,
                                                    QA_STORAGE_INT32, cargs, 2);
              if (r.kind != QA_CALL_INT32) type_error("qsort comparator returned wrong type");
              if (!(r.number < 0)) break;
            }
            uint8_t *tmp = xmalloc(size);
            qa_memory_copy(m, off(m, base, (int64_t)(root * size)), tmp, size);
            uint8_t *other = xmalloc(size);
            qa_memory_copy(m, off(m, base, (int64_t)(child * size)), other, size);
            qa_memory_write(m, off(m, base, (int64_t)(root * size)), other, size);
            qa_memory_write(m, off(m, base, (int64_t)(child * size)), tmp, size);
            free(tmp); free(other);
            root = child;
          }
        }
        for (size_t end = n - 1; end > 0; end--) {
          uint8_t *tmp = xmalloc(size);
          qa_memory_copy(m, base, tmp, size);
          uint8_t *other = xmalloc(size);
          qa_memory_copy(m, off(m, base, (int64_t)(end * size)), other, size);
          qa_memory_write(m, base, other, size);
          qa_memory_write(m, off(m, base, (int64_t)(end * size)), tmp, size);
          free(tmp); free(other);
          size_t root = 0;
          for (;;) {
            size_t child = root * 2 + 1;
            if (child >= end) break;
            if (child + 1 < end) {
              qa_abi_storage params[] = {QA_STORAGE_POINTER, QA_STORAGE_POINTER};
              qa_call_value cargs[] = {qa_call_pointer(true, off(m, base, (int64_t)(child * size))),
                                       qa_call_pointer(true, off(m, base, (int64_t)((child + 1) * size)))};
              qa_call_result r = qa_system_v_invoke(host, context, comparator, params, 2, false,
                                                    QA_STORAGE_INT32, cargs, 2);
              if (r.kind != QA_CALL_INT32) type_error("qsort comparator returned wrong type");
              if (r.number < 0) child++;
            }
            {
              qa_abi_storage params[] = {QA_STORAGE_POINTER, QA_STORAGE_POINTER};
              qa_call_value cargs[] = {qa_call_pointer(true, off(m, base, (int64_t)(root * size))),
                                       qa_call_pointer(true, off(m, base, (int64_t)(child * size)))};
              qa_call_result r = qa_system_v_invoke(host, context, comparator, params, 2, false,
                                                    QA_STORAGE_INT32, cargs, 2);
              if (r.kind != QA_CALL_INT32) type_error("qsort comparator returned wrong type");
              if (!(r.number < 0)) break;
            }
            uint8_t *tmp2 = xmalloc(size);
            qa_memory_copy(m, off(m, base, (int64_t)(root * size)), tmp2, size);
            uint8_t *other2 = xmalloc(size);
            qa_memory_copy(m, off(m, base, (int64_t)(child * size)), other2, size);
            qa_memory_write(m, off(m, base, (int64_t)(root * size)), other2, size);
            qa_memory_write(m, off(m, base, (int64_t)(child * size)), tmp2, size);
            free(tmp2); free(other2);
            root = child;
          }
        }
      }
      return qa_call_void();
    }
    case L_MATH1: {
      double input = floating(args, count, 0);
      double value;
      const char *name = state->name;
      size_t name_len = strlen(name);
      bool is_float = name_len > 0 && name[name_len - 1] == 'f';
      char base_name[16];
      size_t base_len = is_float ? name_len - 1 : name_len;
      memcpy(base_name, name, base_len); base_name[base_len] = '\0';
      if (strcmp(base_name, "sin") == 0) value = sin(input);
      else if (strcmp(base_name, "cos") == 0) value = cos(input);
      else if (strcmp(base_name, "ceil") == 0) value = ceil(input);
      else if (strcmp(base_name, "floor") == 0) value = floor(input);
      else if (strcmp(base_name, "sqrt") == 0) value = sqrt(input);
      else value = fabs(input);
      return is_float ? qa_call_float32(value) : qa_call_float64(value);
    }
    case L_ATAN2:
      return qa_call_float64(atan2(floating(args, count, 0), floating(args, count, 1)));
    case L_SINCOS: {
      double input = floating(args, count, 0);
      qa_guest_address sine = qa_guest_required_pointer(args, count, 1);
      qa_guest_address cosine = qa_guest_required_pointer(args, count, 2);
      if (state->flag) {
        qa_gmem_write_f32(m, sine, sin(input));
        qa_gmem_write_f32(m, cosine, cos(input));
      } else {
        qa_gmem_write_f64(m, sine, sin(input));
        qa_gmem_write_f64(m, cosine, cos(input));
      }
      return qa_call_void();
    }
    case L_ISNANF: {
      double input = floating(args, count, 0);
      return qa_call_int32(input != input ? 1 : 0);
    }
    case L_STACK_CHK:
      qa_unsupported_system_v_service("libc.so.6", "__stack_chk_fail", "GLIBC_2.4",
                                      "guest stack protection failure");
      break;
    default: break;
  }
  return qa_call_void();
}

void qa_install_libc(qa_system_v_service_host *host) {
  qa_install_system_v_format(host);
  qa_mapped_guest_memory *m = host->memory;
  qa_abi_storage p = host->pointer_storage;
  const char *base = m->pointer_bytes == 4 ? "GLIBC_2.0" : "GLIBC_2.2.5";
  const char *version[] = {base, NULL};
  const char *lib = "libc.so.6";
  libc_state proto;
  memset(&proto, 0, sizeof proto);
  proto.host = host;
  qa_abi_storage ptr = QA_STORAGE_POINTER, i32 = QA_STORAGE_INT32;
  reg(&proto, L_ERRNO_LOCATION, lib, "__errno_location", version, 2, NULL, 0, false, ptr);
  if (m->pointer_bytes == 8) {
    const char *tls_version[] = {"GLIBC_2.3", NULL};
    reg(&proto, L_TLS_GET_ADDR, "ld-linux-x86-64.so.2", "__tls_get_addr", tls_version, 2,
        &ptr, 1, false, ptr);
  }
  reg(&proto, L_MALLOC, lib, "malloc", version, 2, &p, 1, false, ptr);
  qa_abi_storage two_sizes[] = {p, p};
  reg(&proto, L_CALLOC, lib, "calloc", version, 2, two_sizes, 2, false, ptr);
  reg(&proto, L_FREE, lib, "free", version, 2, &ptr, 1, true, i32);
  qa_abi_storage realloc_params[] = {ptr, p};
  reg(&proto, L_REALLOC, lib, "realloc", version, 2, realloc_params, 2, false, ptr);
  const char *copy_names[] = {"memcpy", "memmove", "__memcpy_chk", "__memmove_chk"};
  for (size_t i = 0; i < 4; i++) {
    bool checked = copy_names[i][0] == '_';
    const char *chk_version[] = {"GLIBC_2.3.4", NULL};
    const char *memcpy_versions[] = {base, NULL, "GLIBC_2.14"};
    qa_abi_storage plain[] = {ptr, ptr, p};
    qa_abi_storage chk[] = {ptr, ptr, p, p};
    if (checked)
      reg(&proto, L_MEMCOPY, lib, copy_names[i], chk_version, 2, chk, 4, false, ptr);
    else if (strcmp(copy_names[i], "memcpy") == 0 && m->pointer_bytes == 8)
      reg(&proto, L_MEMCOPY, lib, copy_names[i], memcpy_versions, 3, plain, 3, false, ptr);
    else
      reg(&proto, L_MEMCOPY, lib, copy_names[i], version, 2, plain, 3, false, ptr);
  }
  qa_abi_storage memset_params[] = {ptr, i32, p};
  reg(&proto, L_MEMSET, lib, "memset", version, 2, memset_params, 3, false, ptr);
  qa_abi_storage memcmp_params[] = {ptr, ptr, p};
  reg(&proto, L_MEMCMP, lib, "memcmp", version, 2, memcmp_params, 3, false, i32);
  reg(&proto, L_STRLEN, lib, "strlen", version, 2, &ptr, 1, false, p);
  qa_abi_storage two_ptrs[] = {ptr, ptr};
  reg(&proto, L_STRCMP, lib, "strcmp", version, 2, two_ptrs, 2, false, i32);
  const char *strcpy_names[] = {"strcpy", "stpcpy", "strcat", "__strcpy_chk", "__stpcpy_chk",
                                "__strcat_chk"};
  for (size_t i = 0; i < 6; i++) {
    bool checked = strcpy_names[i][0] == '_';
    const char *chk_version[] = {"GLIBC_2.3.4", NULL};
    qa_abi_storage chk[] = {ptr, ptr, p};
    reg(&proto, L_STRCPY, lib, strcpy_names[i], checked ? chk_version : version, 2,
        checked ? chk : two_ptrs, checked ? 3 : 2, false, ptr);
  }
  qa_abi_storage strncpy_params[] = {ptr, ptr, p};
  reg(&proto, L_STRNCPY, lib, "strncpy", version, 2, strncpy_params, 3, false, ptr);
  qa_abi_storage strchr_params[] = {ptr, i32};
  reg(&proto, L_STRCHR, lib, "strchr", version, 2, strchr_params, 2, false, ptr);
  reg(&proto, L_STRCHR, lib, "strrchr", version, 2, strchr_params, 2, false, ptr);
  reg(&proto, L_STRSTR, lib, "strstr", version, 2, two_ptrs, 2, false, ptr);
  reg(&proto, L_STRSTR, lib, "strpbrk", version, 2, two_ptrs, 2, false, ptr);
  proto.token_slot = qa_gmem_allocate(host->memory_ext, (size_t)m->pointer_bytes, false, 0,
                                      false, QA_PERM_READ_WRITE, "System V strtok cursor");
  reg(&proto, L_STRTOK, lib, "strtok", version, 2, two_ptrs, 2, false, ptr);
  qa_abi_storage strtol_params[] = {ptr, ptr, i32};
  reg(&proto, L_STRTOL, lib, "strtol", version, 2, strtol_params, 3, false,
      host->signed_pointer_storage);
  reg(&proto, L_TIME, lib, "time", version, 2, &ptr, 1, false, host->signed_pointer_storage);
  qa_abi_storage qsort_params[] = {ptr, p, p, ptr};
  reg(&proto, L_QSORT, lib, "qsort", version, 2, qsort_params, 4, true, i32);
  const char *math_names[] = {"sin", "cos", "ceil", "floor", "sqrt", "fabs"};
  for (size_t i = 0; i < 6; i++) {
    for (int s = 0; s < 2; s++) {
      qa_abi_storage storage = s == 0 ? QA_STORAGE_FLOAT32 : QA_STORAGE_FLOAT64;
      char name_buffer[16];
      (void)snprintf(name_buffer, sizeof name_buffer, "%s%s", math_names[i], s == 0 ? "f" : "");
      reg(&proto, L_MATH1, "libm.so.6", name_buffer, version, 2, &storage, 1, false, storage);
    }
  }
  qa_abi_storage two_f64[] = {QA_STORAGE_FLOAT64, QA_STORAGE_FLOAT64};
  const char *atan2_version[] = {"GLIBC_2.15", NULL};
  reg(&proto, L_ATAN2, "libm.so.6", "__atan2_finite", atan2_version, 2, two_f64, 2, false,
      QA_STORAGE_FLOAT64);
  for (int s = 0; s < 2; s++) {
    qa_abi_storage storage = s == 0 ? QA_STORAGE_FLOAT32 : QA_STORAGE_FLOAT64;
    qa_abi_storage params[] = {storage, ptr, ptr};
    const char *sincos_version[] = {m->pointer_bytes == 4 ? "GLIBC_2.1" : "GLIBC_2.2.5", NULL};
    libc_state sincos_proto = proto;
    sincos_proto.flag = s == 0;
    reg(&sincos_proto, L_SINCOS, "libm.so.6", s == 0 ? "sincosf" : "sincos",
        sincos_version, 2, params, 3, true, i32);
  }
  qa_abi_storage f32 = QA_STORAGE_FLOAT32;
  reg(&proto, L_ISNANF, lib, "__isnanf", version, 2, &f32, 1, false, i32);
  const char *chk_fail_version[] = {"GLIBC_2.4", NULL};
  reg(&proto, L_STACK_CHK, lib, "__stack_chk_fail", chk_fail_version, 2, NULL, 0, true, i32);
}
