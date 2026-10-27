/* Port of src/guest/runtime/windows/kernel.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/guest/runtime/windows/kernel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "qa/foundation/support.h"
#include "qa/guest/pe/format.h"
#include "qa/guest/pe/image.h"
#include "qa/guest/runtime/common/memory.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Guest runtime out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_range_error("Guest runtime out of memory");
  return next;
}
static void plain_error(const char *message) { qa_range_error("%s", message); }
static qa_guest_address off(const qa_mapped_guest_memory *m, qa_guest_address a, int64_t d) {
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

typedef struct reservation { qa_guest_address address; size_t size; } reservation;
typedef struct fls_slot {
  bool used;
  bool has_callback; qa_guest_address callback;
  bool has_value; qa_guest_address value;
} fls_slot;
typedef struct file_handle {
  uint64_t key;
  int stream_kind; /* 0 file, 1 stdin, 2 stdout, 3 stderr */
  qa_windows_file *file;
  size_t offset;
} file_handle;
typedef struct std_handle { int64_t id; qa_guest_address address; } std_handle;

typedef struct kernel_ctx {
  qa_windows_service_host *host;
  qa_guest_address command_line, command_line_w, environment_a, environment_w;
  bool has_invalid; qa_guest_address invalid;
  uint64_t pointer_secret;
  qa_guest_address process_heap;
  uint64_t *heaps; size_t heap_count, heap_cap;
  reservation *reservations; size_t reservation_count, reservation_cap;
  bool tls_used[1088];
  qa_guest_address dynamic_tls;
  fls_slot fls[128];
  uint64_t *locks; size_t lock_count, lock_cap;
  bool has_exception_filter; qa_guest_address exception_filter;
  file_handle *handles; size_t handle_count, handle_cap;
  std_handle *standards; size_t standard_count, standard_cap;
} kernel_ctx;

static void grow(void **array, size_t *cap, size_t count, size_t element) {
  if (count < *cap) return;
  *cap = *cap == 0 ? 8 : *cap * 2;
  *array = xrealloc(*array, *cap * element);
}
static bool u64_set_has(const uint64_t *set, size_t count, uint64_t value) {
  for (size_t i = 0; i < count; i++) if (set[i] == value) return true;
  return false;
}
static void u64_set_add(uint64_t **set, size_t *count, size_t *cap, uint64_t value) {
  if (u64_set_has(*set, *count, value)) return;
  grow((void **)set, cap, *count, sizeof(uint64_t));
  (*set)[(*count)++] = value;
}
static bool u64_set_delete(uint64_t *set, size_t *count, uint64_t value) {
  for (size_t i = 0; i < *count; i++)
    if (set[i] == value) {
      memmove(&set[i], &set[i + 1], (*count - i - 1) * sizeof(uint64_t));
      (*count)--;
      return true;
    }
  return false;
}

static qa_guest_address stored_string(kernel_ctx *k, const char *value, bool wide) {
  size_t len = 0;
  uint8_t *bytes = qa_string_bytes(value, wide, &len);
  qa_guest_address address = qa_allocate_native_memory(k->host->memory_ext, len,
                                                       "Windows process string");
  qa_memory_write(k->host->memory, address, bytes, len);
  free(bytes);
  return address;
}
static qa_guest_address stored_environment(kernel_ctx *k, bool wide) {
  /* TS: entries joined by NUL plus a trailing NUL, then stringBytes adds
   * its own terminator unit. */
  size_t units = 1; /* trailing join NUL */
  for (size_t i = 0; i < k->host->capabilities.environment_count; i++)
    units += strlen(k->host->capabilities.environment_keys[i]) + 1 +
             strlen(k->host->capabilities.environment_values[i]) + 1;
  if (k->host->capabilities.environment_count > 0) units -= 1; /* join, not suffix */
  uint16_t *codes = xmalloc((units + 1) * sizeof *codes);
  size_t at = 0;
  for (size_t i = 0; i < k->host->capabilities.environment_count; i++) {
    if (i > 0) codes[at++] = 0;
    const char *key = k->host->capabilities.environment_keys[i];
    const char *value = k->host->capabilities.environment_values[i];
    for (size_t j = 0; key[j] != '\0'; j++) codes[at++] = (uint8_t)key[j];
    codes[at++] = '=';
    for (size_t j = 0; value[j] != '\0'; j++) codes[at++] = (uint8_t)value[j];
  }
  codes[at++] = 0;
  size_t len = 0;
  uint8_t *bytes = qa_string_bytes_wide(codes, at, wide, &len);
  free(codes);
  qa_guest_address address = qa_allocate_native_memory(k->host->memory_ext, len,
                                                       "Windows process string");
  qa_memory_write(k->host->memory, address, bytes, len);
  free(bytes);
  return address;
}
static char *read_narrow(kernel_ctx *k, qa_guest_address address) {
  return qa_read_string(k->host->memory, k->host->memory_ext, address, 1048576);
}
/* Wide guest strings become narrow byte strings of low bytes where TS
 * readString(wide) feeds a narrow consumer (library names); units above
 * 255 truncate like charCodeAt consumers in those paths. */
static char *read_maybe_wide(kernel_ctx *k, qa_guest_address address, bool wide) {
  if (!wide) return read_narrow(k, address);
  size_t len = 0;
  uint16_t *units = qa_read_string_wide(k->host->memory, address, 1048576, &len);
  char *text = xmalloc(len + 1);
  for (size_t i = 0; i < len; i++) text[i] = (char)(units[i] & 255);
  text[len] = '\0';
  free(units);
  return text;
}

typedef struct kernel_state {
  kernel_ctx *k;
  int kind;
  int number; /* wide flag / fixed value / change */
  char *name; /* owned */
} kernel_state;
enum {
  K_ENCODE, K_GET_PROCESS_HEAP, K_GET_LAST_ERROR, K_SET_LAST_ERROR, K_GET_THREAD_ID,
  K_GET_PROCESS_ID, K_GET_CURRENT_PROCESS, K_GET_VERSION, K_GET_COMMAND_LINE_A,
  K_GET_COMMAND_LINE_W, K_GET_ACP, K_GET_OEMCP, K_IS_VALID_CP, K_GET_CP_INFO,
  K_GET_ENV_STRINGS, K_FREE_ENV_STRINGS, K_GET_MODULE_HANDLE, K_LOAD_LIBRARY,
  K_LOAD_LIBRARY_EX, K_FREE_LIBRARY, K_GET_PROC_ADDRESS, K_DISABLE_THREAD_CALLS,
  K_GET_MODULE_FILE_NAME, K_HEAP_CREATE, K_HEAP_ALLOC, K_HEAP_FREE, K_HEAP_REALLOC,
  K_HEAP_DESTROY, K_VIRTUAL_ALLOC, K_VIRTUAL_FREE, K_TLS_ALLOC, K_TLS_FREE, K_TLS_GET,
  K_TLS_SET, K_FLS_ALLOC, K_FLS_GET, K_FLS_SET, K_FLS_FREE, K_INIT_CS, K_INIT_CS_SPIN,
  K_INIT_CS_EX, K_SET_SPIN, K_DELETE_CS, K_ENTER_CS, K_LEAVE_CS, K_INTERLOCKED,
  K_ACQUIRE_SRW, K_RELEASE_SRW, K_INIT_SLIST, K_FLUSH_SLIST, K_WAKE_ALL, K_IS_DEBUGGER,
  K_IS_PROCESSOR_FEATURE, K_GET_SYSTEM_TIME_FILETIME, K_QPC, K_QPF, K_GET_TIME,
  K_GET_TIMEZONE, K_SET_UNHANDLED_FILTER, K_RTL_LOOKUP, K_GET_STD_HANDLE, K_SET_STD_HANDLE,
  K_SET_HANDLE_COUNT, K_GET_FILE_TYPE, K_GET_STARTUP_INFO, K_CREATE_FILE_A, K_READ_FILE,
  K_WRITE_FILE, K_CLOSE_HANDLE, K_FLUSH_FILE_BUFFERS, K_SET_FILE_POINTER, K_SET_END_OF_FILE,
  K_MULTIBYTE_TO_WIDE, K_WIDE_TO_MULTIBYTE, K_GET_STRING_TYPE, K_LCMAP
};
static qa_call_result kernel_impl(void *ctx, const qa_guest_call_context *context,
                                  const qa_call_value *args, size_t count);
static void reg(kernel_ctx *k, int kind, int number, const char *name,
                const qa_abi_storage *parameters, size_t parameter_count, bool result_is_void,
                qa_abi_storage result) {
  kernel_state *state = xmalloc(sizeof *state);
  state->k = k; state->kind = kind; state->number = number;
  state->name = xmalloc(strlen(name) + 1);
  strcpy(state->name, name);
  qa_windows_import_implementation impl = {state, kernel_impl};
  qa_windows_service(k->host, "kernel32.dll", name, parameters, parameter_count,
                     result_is_void, result, impl);
}
static qa_call_result u32_result(uint32_t value) { return qa_call_uint32(value); }
static qa_call_result bool_result(bool value) { return qa_call_int32(value ? 1 : 0); }
static file_handle *handle_at(kernel_ctx *k, bool has, qa_guest_address address) {
  if (!has) return NULL;
  for (size_t i = 0; i < k->handle_count; i++)
    if (k->handles[i].key == address.byte_offset) return &k->handles[i];
  return NULL;
}
static bool standard_get(kernel_ctx *k, int64_t id, qa_guest_address *out) {
  for (size_t i = 0; i < k->standard_count; i++)
    if (k->standards[i].id == id) { *out = k->standards[i].address; return true; }
  return false;
}
static void standard_set(kernel_ctx *k, int64_t id, qa_guest_address address) {
  for (size_t i = 0; i < k->standard_count; i++)
    if (k->standards[i].id == id) { k->standards[i].address = address; return; }
  grow((void **)&k->standards, &k->standard_cap, k->standard_count, sizeof(std_handle));
  k->standards[k->standard_count].id = id;
  k->standards[k->standard_count].address = address;
  k->standard_count++;
}
static qa_guest_address tls_slot(kernel_ctx *k, int index) {
  qa_mapped_guest_memory *memory = k->host->memory;
  int width = memory->pointer_bytes;
  return index < 64
      ? off(memory, k->host->teb, (width == 4 ? 0xe10 : 0x1480) + (int64_t)index * width)
      : off(memory, k->dynamic_tls, (int64_t)(index - 64) * width);
}
static void initialize_lock(kernel_ctx *k, qa_guest_address address, uint64_t spin) {
  qa_mapped_guest_memory *memory = k->host->memory;
  int width = memory->pointer_bytes;
  uint8_t zeros[40] = {0};
  qa_memory_write(memory, address, zeros, width == 4 ? 24 : 40);
  qa_write_unsigned(memory, off(memory, address, width), 4, 0xffffffffu);
  qa_write_unsigned(memory, off(memory, address, width == 4 ? 20 : 32), width,
                    spin & 0x7fffffffu);
  u64_set_add(&k->locks, &k->lock_count, &k->lock_cap, address.byte_offset);
}

/* Windows-1252 (WHATWG) for bytes 0x80..0x9f; others decode to themselves. */
static const uint16_t cp1252_high[32] = {
    0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
    0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
    0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178};
static uint16_t cp1252_decode(uint8_t byte) {
  return byte >= 0x80 && byte < 0xa0 ? cp1252_high[byte - 0x80] : byte;
}
/* Input() from kernel.ts installLocale: units of the guest string,
 * including the terminator unit when length is -1. Malloc'd. */
static uint16_t *input_units(kernel_ctx *k, qa_guest_address address, int64_t length, bool wide,
                             size_t *out_len) {
  qa_mapped_guest_memory *memory = k->host->memory;
  if (length == -1) {
    if (wide) {
      size_t len = 0;
      uint16_t *units = qa_read_string_wide(memory, address, 1048576, &len);
      uint16_t *out = xmalloc((len + 1) * sizeof *out);
      memcpy(out, units, len * sizeof *out);
      out[len] = 0;
      free(units);
      *out_len = len + 1;
      return out;
    }
    char *text = read_narrow(k, address);
    size_t len = strlen(text);
    uint16_t *out = xmalloc((len + 1) * sizeof *out);
    for (size_t i = 0; i < len; i++) out[i] = (uint8_t)text[i];
    out[len] = 0;
    free(text);
    *out_len = len + 1;
    return out;
  }
  if (length < 0 || length > 1048576) qa_range_error("Invalid Windows string length");
  uint16_t *out = xmalloc((size_t)(length == 0 ? 1 : length) * sizeof *out);
  for (int64_t i = 0; i < length; i++)
    out[i] = (uint16_t)qa_read_unsigned(memory, off(memory, address, i * (wide ? 2 : 1)),
                                        wide ? 2 : 1);
  *out_len = (size_t)length;
  return out;
}
/* UTF-8 decode (WHATWG replacement semantics simplified to U+FFFD per
 * invalid sequence maximal subpart) into UTF-16 units. Malloc'd. */
static uint16_t *utf8_decode(const uint8_t *bytes, size_t len, size_t *out_len) {
  uint16_t *out = xmalloc((len + 1) * 2 * sizeof *out);
  size_t count = 0, i = 0;
  while (i < len) {
    uint8_t b = bytes[i];
    uint32_t cp;
    size_t need;
    if (b < 0x80) { cp = b; need = 1; }
    else if (b >= 0xc2 && b < 0xe0) { cp = b & 0x1f; need = 2; }
    else if (b >= 0xe0 && b < 0xf0) { cp = b & 0x0f; need = 3; }
    else if (b >= 0xf0 && b < 0xf5) { cp = b & 0x07; need = 4; }
    else { out[count++] = 0xfffd; i++; continue; }
    bool ok = i + need <= len;
    if (ok)
      for (size_t j = 1; j < need; j++)
        if ((bytes[i + j] & 0xc0) != 0x80) ok = false;
    if (!ok) { out[count++] = 0xfffd; i++; continue; }
    for (size_t j = 1; j < need; j++) cp = (cp << 6) | (bytes[i + j] & 0x3f);
    uint32_t min = need == 2 ? 0x80 : need == 3 ? 0x800 : need == 4 ? 0x10000 : 0;
    if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp < 0xe000)) {
      out[count++] = 0xfffd; i++; continue;
    }
    i += need;
    if (cp < 0x10000) out[count++] = (uint16_t)cp;
    else {
      cp -= 0x10000;
      out[count++] = (uint16_t)(0xd800 + (cp >> 10));
      out[count++] = (uint16_t)(0xdc00 + (cp & 0x3ff));
    }
  }
  *out_len = count;
  return out;
}
/* TextEncoder UTF-8 encode of UTF-16 units (lone surrogates -> U+FFFD). */
static uint8_t *utf8_encode(const uint16_t *units, size_t len, size_t *out_len) {
  uint8_t *out = xmalloc(len * 3 + 3);
  size_t count = 0;
  for (size_t i = 0; i < len; i++) {
    uint32_t cp = units[i];
    if (cp >= 0xd800 && cp < 0xdc00 && i + 1 < len && units[i + 1] >= 0xdc00 &&
        units[i + 1] < 0xe000) {
      cp = 0x10000 + ((cp - 0xd800) << 10) + (units[i + 1] - 0xdc00);
      i++;
    } else if (cp >= 0xd800 && cp < 0xe000) cp = 0xfffd;
    if (cp < 0x80) out[count++] = (uint8_t)cp;
    else if (cp < 0x800) {
      out[count++] = (uint8_t)(0xc0 | (cp >> 6));
      out[count++] = (uint8_t)(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
      out[count++] = (uint8_t)(0xe0 | (cp >> 12));
      out[count++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
      out[count++] = (uint8_t)(0x80 | (cp & 0x3f));
    } else {
      out[count++] = (uint8_t)(0xf0 | (cp >> 18));
      out[count++] = (uint8_t)(0x80 | ((cp >> 12) & 0x3f));
      out[count++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
      out[count++] = (uint8_t)(0x80 | (cp & 0x3f));
    }
  }
  *out_len = count;
  return out;
}
static bool js_space_u16(uint16_t c) {
  return (c >= 9 && c <= 13) || c == 0x20 || c == 0xa0 || c == 0x1680 ||
         (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f ||
         c == 0x205f || c == 0x3000 || c == 0xfeff;
}

static qa_call_result kernel_impl(void *ctx, const qa_guest_call_context *context,
                                  const qa_call_value *args, size_t count) {
  kernel_state *state = ctx;
  kernel_ctx *k = state->k;
  qa_windows_service_host *host = k->host;
  qa_mapped_guest_memory *memory = host->memory;
  int width = memory->pointer_bytes;
  qa_guest_address none = {0};
  bool wide = state->number != 0;
  switch (state->kind) {
    case K_ENCODE: {
      qa_guest_address value = {0};
      bool has = qa_guest_pointer_argument(args, count, 0, &value);
      uint64_t raw = (has ? value.byte_offset : 0) ^ k->pointer_secret;
      qa_guest_address out = {0};
      bool has_out = qa_memory_pointer(memory, raw, &out);
      return qa_call_pointer(has_out, out);
    }
    case K_GET_PROCESS_HEAP: return qa_call_pointer(true, k->process_heap);
    case K_GET_LAST_ERROR: return u32_result(qa_windows_last_error_get(host));
    case K_SET_LAST_ERROR:
      qa_windows_last_error_set(host, (uint32_t)qa_guest_integer_u64(args, count, 0));
      return qa_call_void();
    case K_GET_THREAD_ID: return u32_result((uint32_t)host->thread_id);
    case K_GET_PROCESS_ID: return u32_result((uint32_t)host->process_id);
    case K_GET_CURRENT_PROCESS: return qa_call_pointer(k->has_invalid, k->invalid);
    case K_GET_VERSION: return u32_result(0x05650004u);
    case K_GET_COMMAND_LINE_A: return qa_call_pointer(true, k->command_line);
    case K_GET_COMMAND_LINE_W: return qa_call_pointer(true, k->command_line_w);
    case K_GET_ACP: return u32_result(1252);
    case K_GET_OEMCP: return u32_result(437);
    case K_IS_VALID_CP: {
      int64_t cp = qa_guest_integer(args, count, 0);
      return bool_result(cp == 1252 || cp == 437 || cp == 65001);
    }
    case K_GET_CP_INFO: {
      int64_t cp = qa_guest_integer(args, count, 0);
      if (!(cp == 0 || cp == 1 || cp == 1252 || cp == 437 || cp == 65001)) {
        qa_windows_last_error_set(host, 87);
        return bool_result(false);
      }
      qa_guest_address address = qa_guest_required_pointer(args, count, 1);
      uint8_t zeros[20] = {0};
      qa_memory_write(memory, address, zeros, sizeof zeros);
      qa_write_unsigned(memory, address, 4, cp == 65001 ? 4 : 1);
      qa_write_unsigned(memory, off(memory, address, 4), 2, 63);
      return bool_result(true);
    }
    case K_GET_ENV_STRINGS:
      return qa_call_pointer(true, wide ? k->environment_w : k->environment_a);
    case K_FREE_ENV_STRINGS: return bool_result(true);
    case K_GET_MODULE_HANDLE: {
      qa_guest_address name = {0};
      qa_guest_address handle = {0};
      bool has_handle;
      if (!qa_guest_pointer_argument(args, count, 0, &name)) {
        size_t image_count = 0;
        const qa_pe_image *const *images = qa_windows_images(host, &image_count);
        has_handle = image_count > 0;
        if (has_handle) handle = images[0]->image.base;
      } else {
        char *text = read_maybe_wide(k, name, wide);
        has_handle = qa_windows_library_handle(host, text, &handle);
        free(text);
      }
      if (!has_handle) qa_windows_last_error_set(host, 126);
      return qa_call_pointer(has_handle, handle);
    }
    case K_LOAD_LIBRARY: {
      char *text = read_maybe_wide(k, qa_guest_required_pointer(args, count, 0), wide);
      qa_guest_address handle = {0};
      bool has = qa_windows_load_library(host, text, &handle);
      free(text);
      return qa_call_pointer(has, handle);
    }
    case K_LOAD_LIBRARY_EX: {
      qa_guest_address name = {0}, reserved = {0};
      bool has_name = qa_guest_pointer_argument(args, count, 0, &name);
      bool has_reserved = qa_guest_pointer_argument(args, count, 1, &reserved);
      uint32_t flags = (uint32_t)qa_guest_integer_u64(args, count, 2);
      if (!has_name || has_reserved || (flags & ~0x3fffu) != 0) {
        qa_windows_last_error_set(host, 87);
        return qa_call_pointer(false, none);
      }
      if ((flags & ~0x1f00u) != 0) {
        qa_windows_last_error_set(host, 50);
        return qa_call_pointer(false, none);
      }
      char *library_text = read_maybe_wide(k, name, wide);
      bool absolute = (library_text[0] >= 'A' && library_text[0] <= 'Z') ||
                      (library_text[0] >= 'a' && library_text[0] <= 'z');
      absolute = absolute && library_text[1] == ':' &&
                 (library_text[2] == '\\' || library_text[2] == '/');
      bool unc = (library_text[0] == '\\' || library_text[0] == '/') &&
                 (library_text[1] == '\\' || library_text[1] == '/');
      if (library_text[0] == '\0' || ((flags & 0x100u) != 0 && !absolute && !unc)) {
        free(library_text);
        qa_windows_last_error_set(host, 87);
        return qa_call_pointer(false, none);
      }
      qa_guest_address handle = {0};
      bool has = qa_windows_load_library(host, library_text, &handle);
      free(library_text);
      return qa_call_pointer(has, handle);
    }
    case K_FREE_LIBRARY: {
      qa_guest_address handle = {0};
      if (!qa_guest_pointer_argument(args, count, 0, &handle)) {
        qa_windows_last_error_set(host, 6);
        return bool_result(false);
      }
      return bool_result(qa_windows_free_library(host, handle));
    }
    case K_GET_PROC_ADDRESS: {
      qa_guest_address handle = {0}, name = {0};
      bool has_handle = qa_guest_pointer_argument(args, count, 0, &handle);
      bool has_name = qa_guest_pointer_argument(args, count, 1, &name);
      if (!has_handle || !has_name) {
        qa_windows_last_error_set(host, 127);
        return qa_call_pointer(false, none);
      }
      const char *library_name = qa_windows_library_name(host, handle);
      qa_guest_address address = {0};
      bool has_address = false;
      if (library_name != NULL) {
        if (name.byte_offset <= 65535) {
          char ordinal[16];
          (void)snprintf(ordinal, sizeof ordinal, "#%llu", (unsigned long long)name.byte_offset);
          has_address = qa_windows_resolve_address(host, library_name, ordinal, &address);
        } else {
          char *text = read_narrow(k, name);
          has_address = qa_windows_resolve_address(host, library_name, text, &address);
          free(text);
        }
      }
      if (!has_address) qa_windows_last_error_set(host, 127);
      return qa_call_pointer(has_address, address);
    }
    case K_DISABLE_THREAD_CALLS: {
      qa_guest_address base = {0};
      bool has_base = qa_guest_pointer_argument(args, count, 0, &base);
      size_t image_count = 0;
      const qa_pe_image *const *images = qa_windows_images(host, &image_count);
      const qa_pe_image *found = NULL;
      if (has_base)
        for (size_t i = 0; i < image_count; i++)
          if (images[i]->image.base.byte_offset == base.byte_offset) found = images[i];
      if (found == NULL || found->image.has_tls) {
        qa_windows_last_error_set(host, 87);
        return bool_result(false);
      }
      return bool_result(true);
    }
    case K_GET_MODULE_FILE_NAME: {
      qa_guest_address handle = {0};
      bool has_handle = qa_guest_pointer_argument(args, count, 0, &handle);
      const char *owned_name = NULL;
      const char *name_text = "quake-typescript.exe";
      if (has_handle) {
        owned_name = qa_windows_library_name(host, handle);
        name_text = owned_name;
      }
      if (name_text == NULL) {
        qa_windows_last_error_set(host, 126);
        return u32_result(0);
      }
      size_t size = qa_guest_count(args, count, 2);
      if (size == 0) {
        qa_windows_last_error_set(host, 122);
        return u32_result(0);
      }
      size_t bytes_len = 0;
      uint8_t *bytes = qa_string_bytes(name_text, wide, &bytes_len);
      size_t unit = wide ? 2 : 1;
      size_t length = bytes_len / unit - 1;
      size_t copied = length < size - 1 ? length : size - 1;
      qa_guest_address destination = qa_guest_required_pointer(args, count, 1);
      qa_memory_write(memory, destination, bytes, copied * unit);
      free(bytes);
      qa_write_unsigned(memory, off(memory, destination, (int64_t)(copied * unit)),
                        (int)unit, 0);
      if (length >= size) {
        qa_windows_last_error_set(host, 122);
        return u32_result((uint32_t)size);
      }
      return u32_result((uint32_t)length);
    }
    case K_HEAP_CREATE: {
      qa_guest_address address = qa_gmem_allocate(host->memory_ext, 16, false, 0, false,
                                                  QA_PERM_READ_WRITE, "Windows heap handle");
      u64_set_add(&k->heaps, &k->heap_count, &k->heap_cap, address.byte_offset);
      return qa_call_pointer(true, address);
    }
    case K_HEAP_ALLOC: {
      qa_guest_address heap = {0};
      if (!qa_guest_pointer_argument(args, count, 0, &heap) ||
          !u64_set_has(k->heaps, k->heap_count, heap.byte_offset)) {
        qa_windows_last_error_set(host, 6);
        return qa_call_pointer(false, none);
      }
      qa_guest_address out = {0};
      bool has = qa_windows_allocate(host, qa_guest_count(args, count, 2), heap.byte_offset,
                                     &out);
      return qa_call_pointer(has, out);
    }
    case K_HEAP_FREE: {
      qa_guest_address heap = {0}, address = {0};
      bool has_heap = qa_guest_pointer_argument(args, count, 0, &heap);
      bool has_address = qa_guest_pointer_argument(args, count, 2, &address);
      return bool_result(qa_windows_free(host, has_address, address,
                                         has_heap ? heap.byte_offset : 0));
    }
    case K_HEAP_REALLOC: {
      qa_guest_address heap = {0}, old = {0};
      bool has_heap = qa_guest_pointer_argument(args, count, 0, &heap);
      bool has_old = qa_guest_pointer_argument(args, count, 2, &old);
      size_t size = qa_guest_count(args, count, 3);
      uint32_t flags = (uint32_t)qa_guest_integer_u64(args, count, 1);
      if (!has_heap || !has_old) {
        qa_windows_last_error_set(host, 87);
        return qa_call_pointer(false, none);
      }
      size_t previous = 0;
      if (!qa_windows_allocation_size(host, old, heap.byte_offset, &previous)) {
        qa_windows_last_error_set(host, 87);
        return qa_call_pointer(false, none);
      }
      if (size <= previous) return qa_call_pointer(true, old);
      if ((flags & 16u) != 0) return qa_call_pointer(false, none);
      qa_guest_address address = {0};
      bool has = qa_windows_allocate(host, size, heap.byte_offset, &address);
      if (has) {
        uint8_t *bytes = xmalloc(previous);
        qa_memory_copy(memory, old, bytes, previous);
        qa_memory_write(memory, address, bytes, previous);
        free(bytes);
        (void)qa_windows_free(host, true, old, heap.byte_offset);
      }
      return qa_call_pointer(has, address);
    }
    case K_HEAP_DESTROY: {
      qa_guest_address heap = {0};
      if (!qa_guest_pointer_argument(args, count, 0, &heap) ||
          heap.byte_offset == k->process_heap.byte_offset ||
          !u64_set_delete(k->heaps, &k->heap_count, heap.byte_offset))
        return bool_result(false);
      qa_windows_destroy_heap(host, heap.byte_offset);
      qa_gmem_unmap(host->memory_ext, heap, 16);
      return bool_result(true);
    }
    case K_VIRTUAL_ALLOC: {
      qa_guest_address requested = {0};
      bool has_requested = qa_guest_pointer_argument(args, count, 0, &requested);
      size_t size = qa_guest_count(args, count, 1);
      uint32_t flags = (uint32_t)qa_guest_integer_u64(args, count, 2);
      uint32_t protection = (uint32_t)qa_guest_integer_u64(args, count, 3);
      bool protection_ok = protection == 1 || protection == 2 || protection == 4 ||
                           protection == 0x10 || protection == 0x20 || protection == 0x40;
      if (size == 0 || (flags & ~(0x1000u | 0x2000u | 0x100000u)) != 0 || !protection_ok)
        qa_unsupported_windows_import("kernel32.dll", "VirtualAlloc",
                                      "unsupported allocation flags/protection");
      qa_guest_permissions permissions = protection == 1 ? QA_PERM_NONE
          : protection == 2 ? QA_PERM_READ
          : protection == 4 ? QA_PERM_READ_WRITE
          : protection == 0x10 ? QA_PERM_EXECUTE
          : protection == 0x20 ? QA_PERM_READ_EXECUTE : QA_PERM_READ_WRITE_EXECUTE;
      qa_guest_address address = requested;
      size_t bytes = (size + 4095) / 4096 * 4096;
      if ((flags & 0x2000u) != 0 || !has_requested) {
        if (!has_requested) {
          address = qa_gmem_allocate(host->memory_ext, bytes, true, 65536, true, QA_PERM_NONE,
                                     "Windows VirtualAlloc");
        } else {
          uint64_t base = address.byte_offset & ~65535ULL;
          bytes = (size_t)(((address.byte_offset - base) + size + 4095) / 4096 * 4096);
          /* TS catches a failed map as ERROR_INVALID_ADDRESS; the C memory
           * map aborts on failure, so that path is unobservable here. */
          address = qa_gmem_map(host->memory_ext, base, bytes, QA_PERM_NONE,
                                "Windows VirtualAlloc", NULL, 0);
        }
        grow((void **)&k->reservations, &k->reservation_cap, k->reservation_count,
             sizeof(reservation));
        k->reservations[k->reservation_count].address = address;
        k->reservations[k->reservation_count].size = bytes;
        k->reservation_count++;
      } else {
        uint64_t base = address.byte_offset & ~4095ULL;
        bytes = (size_t)(((address.byte_offset - base) + size + 4095) / 4096 * 4096);
        bool has_address = qa_memory_pointer(memory, base, &address);
        bool inside = false;
        if (has_address)
          for (size_t i = 0; i < k->reservation_count; i++)
            if (base >= k->reservations[i].address.byte_offset &&
                base + bytes <= k->reservations[i].address.byte_offset + k->reservations[i].size)
              inside = true;
        if (!has_address || !inside) {
          qa_windows_last_error_set(host, 487);
          return qa_call_pointer(false, none);
        }
      }
      if ((flags & 0x1000u) != 0)
        qa_gmem_protect(host->memory_ext, address, bytes, permissions);
      return qa_call_pointer(true, address);
    }
    case K_VIRTUAL_FREE: {
      qa_guest_address address = qa_guest_required_pointer(args, count, 0);
      size_t size = qa_guest_count(args, count, 1);
      uint32_t flags = (uint32_t)qa_guest_integer_u64(args, count, 2);
      if (flags == 0x8000u && size == 0) {
        for (size_t i = 0; i < k->reservation_count; i++)
          if (k->reservations[i].address.byte_offset == address.byte_offset) {
            qa_gmem_unmap(host->memory_ext, address, k->reservations[i].size);
            memmove(&k->reservations[i], &k->reservations[i + 1],
                    (k->reservation_count - i - 1) * sizeof(reservation));
            k->reservation_count--;
            return bool_result(true);
          }
        return bool_result(false);
      }
      if (flags == 0x4000u && size > 0) {
        bool inside = false;
        for (size_t i = 0; i < k->reservation_count; i++)
          if (address.byte_offset >= k->reservations[i].address.byte_offset &&
              address.byte_offset + size <=
                  k->reservations[i].address.byte_offset + k->reservations[i].size)
            inside = true;
        if (!inside) return bool_result(false);
        qa_gmem_protect(host->memory_ext, address, size, QA_PERM_READ_WRITE);
        uint8_t *zeros = xmalloc(size);
        memset(zeros, 0, size);
        qa_memory_write(memory, address, zeros, size);
        free(zeros);
        qa_gmem_protect(host->memory_ext, address, size, QA_PERM_NONE);
        return bool_result(true);
      }
      qa_windows_last_error_set(host, 87);
      return bool_result(false);
    }
    case K_TLS_ALLOC: {
      for (int index = 0; index < 1088; index++)
        if (!k->tls_used[index]) {
          k->tls_used[index] = true;
          qa_write_unsigned(memory, tls_slot(k, index), width, 0);
          return u32_result((uint32_t)index);
        }
      return u32_result(0xffffffffu);
    }
    case K_TLS_FREE: {
      int64_t index = qa_guest_integer(args, count, 0);
      if (index < 0 || index >= 1088 || !k->tls_used[index]) {
        qa_windows_last_error_set(host, 87);
        return bool_result(false);
      }
      k->tls_used[index] = false;
      return bool_result(true);
    }
    case K_TLS_GET: {
      int64_t index = qa_guest_integer(args, count, 0);
      if (index < 0 || index >= 1088 || !k->tls_used[index]) {
        qa_windows_last_error_set(host, 87);
        return qa_call_pointer(false, none);
      }
      qa_windows_last_error_set(host, 0);
      qa_guest_address value = {0};
      bool has = qa_read_pointer(memory, tls_slot(k, (int)index), &value);
      return qa_call_pointer(has, value);
    }
    case K_TLS_SET: {
      int64_t index = qa_guest_integer(args, count, 0);
      if (index < 0 || index >= 1088 || !k->tls_used[index]) {
        qa_windows_last_error_set(host, 87);
        return bool_result(false);
      }
      qa_guest_address value = {0};
      bool has = qa_guest_pointer_argument(args, count, 1, &value);
      qa_write_pointer(memory, tls_slot(k, (int)index), has, value);
      return bool_result(true);
    }
    case K_FLS_ALLOC: {
      for (int index = 0; index < 128; index++)
        if (!k->fls[index].used) {
          k->fls[index].used = true;
          k->fls[index].has_callback =
              qa_guest_pointer_argument(args, count, 0, &k->fls[index].callback);
          k->fls[index].has_value = false;
          return u32_result((uint32_t)index);
        }
      qa_windows_last_error_set(host, 8);
      return u32_result(0xffffffffu);
    }
    case K_FLS_GET: {
      int64_t index = qa_guest_integer(args, count, 0);
      if (index < 0 || index >= 128 || !k->fls[index].used) {
        qa_windows_last_error_set(host, 87);
        return qa_call_pointer(false, none);
      }
      qa_windows_last_error_set(host, 0);
      return qa_call_pointer(k->fls[index].has_value, k->fls[index].value);
    }
    case K_FLS_SET: {
      int64_t index = qa_guest_integer(args, count, 0);
      if (index < 0 || index >= 128 || !k->fls[index].used) {
        qa_windows_last_error_set(host, 87);
        return bool_result(false);
      }
      k->fls[index].has_value = qa_guest_pointer_argument(args, count, 1, &k->fls[index].value);
      return bool_result(true);
    }
    case K_FLS_FREE: {
      int64_t index = qa_guest_integer(args, count, 0);
      if (index < 0 || index >= 128 || !k->fls[index].used) {
        qa_windows_last_error_set(host, 87);
        return bool_result(false);
      }
      fls_slot slot = k->fls[index];
      k->fls[index].used = false;
      if (slot.has_callback && slot.has_value) {
        qa_abi_storage params[] = {QA_STORAGE_POINTER};
        qa_call_value invoke_args[] = {qa_call_pointer(true, slot.value)};
        qa_call_result result = qa_windows_invoke(host, context, slot.callback, params, 1,
                                                  true, QA_STORAGE_INT32, invoke_args, 1,
                                                  true);
        qa_call_value_free(&result);
      }
      return bool_result(true);
    }
    case K_INIT_CS:
      initialize_lock(k, qa_guest_required_pointer(args, count, 0), 0);
      return qa_call_void();
    case K_INIT_CS_SPIN:
      initialize_lock(k, qa_guest_required_pointer(args, count, 0),
                      qa_guest_integer_u64(args, count, 1));
      return bool_result(true);
    case K_INIT_CS_EX:
      if ((qa_guest_integer_u64(args, count, 2) & ~0x01000000ULL) != 0) {
        qa_windows_last_error_set(host, 87);
        return bool_result(false);
      }
      initialize_lock(k, qa_guest_required_pointer(args, count, 0),
                      qa_guest_integer_u64(args, count, 1));
      return bool_result(true);
    case K_SET_SPIN: {
      qa_guest_address address = qa_guest_required_pointer(args, count, 0);
      if (!u64_set_has(k->locks, k->lock_count, address.byte_offset))
        plain_error("Uninitialized guest critical section");
      qa_guest_address at = off(memory, address, width == 4 ? 20 : 32);
      uint64_t previous = qa_read_unsigned(memory, at, width);
      qa_write_unsigned(memory, at, width, qa_guest_integer_u64(args, count, 1) & 0x7fffffffu);
      return u32_result((uint32_t)previous);
    }
    case K_DELETE_CS:
      (void)u64_set_delete(k->locks, &k->lock_count,
                           qa_guest_required_pointer(args, count, 0).byte_offset);
      return qa_call_void();
    case K_ENTER_CS: {
      qa_guest_address address = qa_guest_required_pointer(args, count, 0);
      if (!u64_set_has(k->locks, k->lock_count, address.byte_offset))
        plain_error("Uninitialized guest critical section");
      uint64_t depth = qa_read_unsigned(memory, off(memory, address, width + 4), 4);
      uint64_t owner = qa_read_unsigned(memory, off(memory, address, width + 8), width);
      if (depth != 0 && owner != (uint64_t)host->thread_id)
        qa_unsupported_windows_import(
            "kernel32.dll", "EnterCriticalSection",
            "contended lock requires a guest thread scheduler");
      qa_write_unsigned(memory, off(memory, address, width), 4, depth);
      qa_write_unsigned(memory, off(memory, address, width + 4), 4, depth + 1);
      qa_write_unsigned(memory, off(memory, address, width + 8), width,
                        (uint64_t)host->thread_id);
      return qa_call_void();
    }
    case K_LEAVE_CS: {
      qa_guest_address address = qa_guest_required_pointer(args, count, 0);
      uint64_t depth = qa_read_unsigned(memory, off(memory, address, width + 4), 4);
      if (!u64_set_has(k->locks, k->lock_count, address.byte_offset) || depth == 0 ||
          qa_read_unsigned(memory, off(memory, address, width + 8), width) !=
              (uint64_t)host->thread_id)
        plain_error("Unowned guest critical section");
      qa_write_unsigned(memory, off(memory, address, width), 4, depth - 2);
      qa_write_unsigned(memory, off(memory, address, width + 4), 4, depth - 1);
      if (depth == 1) qa_write_unsigned(memory, off(memory, address, width + 8), width, 0);
      return qa_call_void();
    }
    case K_INTERLOCKED: {
      qa_guest_address address = qa_guest_required_pointer(args, count, 0);
      int64_t change = state->number;
      uint32_t value = (uint32_t)(qa_read_unsigned(memory, address, 4) + (uint64_t)change);
      qa_write_unsigned(memory, address, 4, value);
      return qa_call_int32((int32_t)value);
    }
    case K_ACQUIRE_SRW: {
      qa_guest_address address = qa_guest_required_pointer(args, count, 0);
      if (qa_read_unsigned(memory, address, width) != 0)
        qa_unsupported_windows_import(
            "kernel32.dll", "AcquireSRWLockExclusive",
            "contended lock requires a guest thread scheduler");
      qa_write_unsigned(memory, address, width, 1);
      return qa_call_void();
    }
    case K_RELEASE_SRW: {
      qa_guest_address address = qa_guest_required_pointer(args, count, 0);
      if (qa_read_unsigned(memory, address, width) == 0)
        plain_error("Unowned guest SRW lock");
      qa_write_unsigned(memory, address, width, 0);
      return qa_call_void();
    }
    case K_INIT_SLIST: {
      uint8_t zeros[16] = {0};
      qa_memory_write(memory, qa_guest_required_pointer(args, count, 0), zeros,
                      width == 4 ? 8 : 16);
      return qa_call_void();
    }
    case K_FLUSH_SLIST: {
      qa_guest_address address = qa_guest_required_pointer(args, count, 0);
      if (address.byte_offset % (uint64_t)(width == 4 ? 8 : 16) != 0)
        qa_range_error("Unaligned Windows SLIST_HEADER");
      if (width == 4) {
        qa_guest_address next = {0};
        if (!qa_read_pointer(memory, address, &next)) return qa_call_pointer(false, none);
        uint64_t sequence = qa_read_unsigned(memory, off(memory, address, 6), 2);
        uint8_t zeros[8] = {0};
        qa_memory_write(memory, address, zeros, sizeof zeros);
        qa_write_unsigned(memory, off(memory, address, 6), 2, sequence + 1);
        return qa_call_pointer(true, next);
      }
      uint64_t lower = qa_read_unsigned(memory, address, 8);
      qa_guest_address upper_at = off(memory, address, 8);
      uint64_t upper = qa_read_unsigned(memory, upper_at, 8);
      qa_guest_address next = {0};
      if (!qa_memory_pointer(memory, upper & ~15ULL, &next))
        return qa_call_pointer(false, none);
      qa_write_unsigned(memory, address, 8, (lower & ~65535ULL) + 65536);
      qa_write_unsigned(memory, upper_at, 8, upper & 15);
      return qa_call_pointer(true, next);
    }
    case K_WAKE_ALL: return qa_call_void();
    case K_IS_DEBUGGER: return bool_result(false);
    case K_IS_PROCESSOR_FEATURE: {
      int64_t feature = qa_guest_integer(args, count, 0);
      return bool_result(feature == 6 || feature == 10);
    }
    case K_GET_SYSTEM_TIME_FILETIME: {
      uint64_t value = (uint64_t)((int64_t)trunc(now_milliseconds(host)) * 10000) +
                       116444736000000000ULL;
      qa_write_unsigned(memory, qa_guest_required_pointer(args, count, 0), 8, value);
      return qa_call_void();
    }
    case K_QPC: {
      int64_t value;
      if (host->capabilities.performance_counter != NULL &&
          host->capabilities.performance_counter(host->capabilities.ctx, &value)) {
        /* supplied */
      } else {
        /* TS performance.now()*1000 (microseconds on a monotonic clock);
         * TIME_UTC serves as the C clock here. */
        struct timespec ts;
        (void)timespec_get(&ts, TIME_UTC);
        value = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
      }
      qa_write_unsigned(memory, qa_guest_required_pointer(args, count, 0), 8, (uint64_t)value);
      return bool_result(true);
    }
    case K_QPF: {
      uint64_t value = host->capabilities.has_performance_frequency
          ? (uint64_t)host->capabilities.performance_frequency : 1000000;
      qa_write_unsigned(memory, qa_guest_required_pointer(args, count, 0), 8, value);
      return bool_result(true);
    }
    case K_GET_TIME: {
      bool local = state->number != 0;
      time_t t = (time_t)(now_milliseconds(host) / 1000.0);
      struct tm tm_value;
      if (local) (void)localtime_r(&t, &tm_value);
      else (void)gmtime_r(&t, &tm_value);
      double ms = now_milliseconds(host);
      int millis = (int)(ms - floor(ms / 1000.0) * 1000.0);
      int fields[] = {tm_value.tm_year + 1900, tm_value.tm_mon + 1, tm_value.tm_wday,
                      tm_value.tm_mday, tm_value.tm_hour, tm_value.tm_min, tm_value.tm_sec,
                      millis};
      qa_guest_address address = qa_guest_required_pointer(args, count, 0);
      for (size_t i = 0; i < 8; i++)
        qa_write_unsigned(memory, off(memory, address, (int64_t)i * 2), 2,
                          (uint64_t)(uint16_t)fields[i]);
      return qa_call_void();
    }
    case K_GET_TIMEZONE: {
      qa_guest_address address = qa_guest_required_pointer(args, count, 0);
      uint8_t zeros[172] = {0};
      qa_memory_write(memory, address, zeros, sizeof zeros);
      time_t t = (time_t)(now_milliseconds(host) / 1000.0);
      struct tm g;
      (void)gmtime_r(&t, &g);
      g.tm_isdst = 0;
      long offset_minutes = (long)((mktime(&g) - t) / 60);
      qa_write_unsigned(memory, address, 4, (uint64_t)(uint32_t)offset_minutes);
      return u32_result(0);
    }
    case K_SET_UNHANDLED_FILTER: {
      qa_guest_address previous = k->exception_filter;
      bool has_previous = k->has_exception_filter;
      k->has_exception_filter = qa_guest_pointer_argument(args, count, 0,
                                                          &k->exception_filter);
      return qa_call_pointer(has_previous, previous);
    }
    case K_RTL_LOOKUP: {
      uint64_t pc = qa_guest_integer_u64(args, count, 0);
      size_t image_count = 0;
      const qa_pe_image *const *images = qa_windows_images(host, &image_count);
      const qa_pe_image *found = NULL;
      for (size_t i = 0; i < image_count; i++)
        if (pc >= images[i]->image.base.byte_offset &&
            pc < images[i]->image.base.byte_offset + images[i]->image.byte_length)
          found = images[i];
      if (found == NULL) return qa_call_pointer(false, none);
      int64_t index = -1;
      for (size_t i = 0; i < found->unwind_record_count; i++)
        if (pc >= found->image.base.byte_offset + found->unwind_records[i].begin_rva &&
            pc < found->image.base.byte_offset + found->unwind_records[i].end_rva)
          index = (int64_t)i;
      if (index < 0) return qa_call_pointer(false, none);
      qa_write_unsigned(memory, qa_guest_required_pointer(args, count, 1), 8,
                        found->image.base.byte_offset);
      return qa_call_pointer(
          true, off(memory, found->image.base,
                    (int64_t)(found->pe.directories[3].rva + (uint32_t)index * 12)));
    }
    case K_GET_STD_HANDLE: {
      qa_guest_address address = {0};
      if (!standard_get(k, qa_guest_integer(args, count, 0), &address))
        return qa_call_pointer(k->has_invalid, k->invalid);
      return qa_call_pointer(true, address);
    }
    case K_SET_STD_HANDLE: {
      qa_guest_address address = {0};
      if (!qa_guest_pointer_argument(args, count, 1, &address)) return bool_result(false);
      standard_set(k, qa_guest_integer(args, count, 0), address);
      return bool_result(true);
    }
    case K_SET_HANDLE_COUNT: return u32_result((uint32_t)qa_guest_integer_u64(args, count, 0));
    case K_GET_FILE_TYPE: {
      qa_guest_address address = {0};
      bool has = qa_guest_pointer_argument(args, count, 0, &address);
      file_handle *entry = handle_at(k, has, address);
      return u32_result(entry == NULL ? 0 : entry->stream_kind != 0 ? 2 : 1);
    }
    case K_GET_STARTUP_INFO: {
      qa_guest_address address = qa_guest_required_pointer(args, count, 0);
      int size = width == 4 ? 68 : 104;
      uint8_t zeros[104] = {0};
      qa_memory_write(memory, address, zeros, (size_t)size);
      qa_write_unsigned(memory, address, 4, (uint64_t)size);
      const int64_t ids[] = {-10, -11, -12};
      for (size_t i = 0; i < 3; i++) {
        qa_guest_address standard = {0};
        bool has = standard_get(k, ids[i], &standard);
        qa_write_pointer(
            memory,
            off(memory, address, (int64_t)((width == 4 ? 56 : 80) + (int)i * width)), has,
            standard);
      }
      return qa_call_void();
    }
    case K_CREATE_FILE_A: {
      uint32_t access = (uint32_t)qa_guest_integer_u64(args, count, 1);
      int creation = (int)qa_guest_integer(args, count, 4);
      qa_windows_file *file = NULL;
      if (host->capabilities.open_file != NULL) {
        char *path = read_narrow(k, qa_guest_required_pointer(args, count, 0));
        file = host->capabilities.open_file(host->capabilities.ctx, path,
                                            (access & 0x80000000u) != 0,
                                            (access & 0x40000000u) != 0, creation);
        free(path);
      }
      if (file == NULL) {
        qa_windows_last_error_set(host, 2);
        return qa_call_pointer(k->has_invalid, k->invalid);
      }
      qa_guest_address address = qa_gmem_allocate(host->memory_ext, 16, false, 0, false,
                                                  QA_PERM_READ_WRITE,
                                                  "Windows file handle");
      grow((void **)&k->handles, &k->handle_cap, k->handle_count, sizeof(file_handle));
      k->handles[k->handle_count].key = address.byte_offset;
      k->handles[k->handle_count].stream_kind = 0;
      k->handles[k->handle_count].file = file;
      k->handles[k->handle_count].offset = 0;
      k->handle_count++;
      return qa_call_pointer(true, address);
    }
    case K_READ_FILE: {
      qa_guest_address handle = {0};
      bool has_handle = qa_guest_pointer_argument(args, count, 0, &handle);
      file_handle *entry = handle_at(k, has_handle, handle);
      if (entry == NULL) {
        qa_windows_last_error_set(host, 6);
        return bool_result(false);
      }
      qa_guest_address overlapped = {0};
      if (qa_guest_pointer_argument(args, count, 4, &overlapped))
        qa_unsupported_windows_import("kernel32.dll", "ReadFile",
                                      "overlapped I/O is not implemented");
      size_t length = qa_guest_count(args, count, 2);
      uint8_t *bytes = xmalloc(length);
      size_t read_count;
      if (entry->stream_kind == 1) {
        read_count = host->capabilities.standard_input != NULL
            ? host->capabilities.standard_input(host->capabilities.ctx, length, bytes) : 0;
      } else if (entry->stream_kind != 0) {
        free(bytes);
        qa_windows_last_error_set(host, 5);
        return bool_result(false);
      } else {
        read_count = entry->file->read(entry->file->ctx, entry->offset, bytes, length);
      }
      if (read_count > length) plain_error("File capability exceeded requested read length");
      qa_memory_write(memory, qa_guest_required_pointer(args, count, 1), bytes, read_count);
      free(bytes);
      entry->offset += read_count;
      qa_guest_address read_out = {0};
      if (qa_guest_pointer_argument(args, count, 3, &read_out))
        qa_write_unsigned(memory, read_out, 4, read_count);
      return bool_result(true);
    }
    case K_WRITE_FILE: {
      qa_guest_address handle = {0};
      bool has_handle = qa_guest_pointer_argument(args, count, 0, &handle);
      file_handle *entry = handle_at(k, has_handle, handle);
      if (entry == NULL) {
        qa_windows_last_error_set(host, 6);
        return bool_result(false);
      }
      qa_guest_address overlapped = {0};
      if (qa_guest_pointer_argument(args, count, 4, &overlapped))
        qa_unsupported_windows_import("kernel32.dll", "WriteFile",
                                      "overlapped I/O is not implemented");
      size_t length = qa_guest_count(args, count, 2);
      uint8_t *bytes = xmalloc(length);
      qa_memory_copy(memory, qa_guest_required_pointer(args, count, 1), bytes, length);
      size_t written;
      if (entry->stream_kind == 2 || entry->stream_kind == 3) {
        if (host->capabilities.standard_output == NULL) {
          free(bytes);
          qa_windows_last_error_set(host, 6);
          return bool_result(false);
        }
        host->capabilities.standard_output(
            host->capabilities.ctx,
            entry->stream_kind == 2 ? QA_WINDOWS_STDOUT : QA_WINDOWS_STDERR, bytes, length);
        written = length;
      } else if (entry->stream_kind == 1) {
        free(bytes);
        qa_windows_last_error_set(host, 5);
        return bool_result(false);
      } else {
        written = (size_t)entry->file->write(entry->file->ctx, entry->offset, bytes, length);
      }
      free(bytes);
      entry->offset += written;
      qa_guest_address out = {0};
      if (qa_guest_pointer_argument(args, count, 3, &out))
        qa_write_unsigned(memory, out, 4, written);
      return bool_result(true);
    }
    case K_CLOSE_HANDLE: {
      qa_guest_address address = {0};
      bool has = qa_guest_pointer_argument(args, count, 0, &address);
      file_handle *entry = handle_at(k, has, address);
      if (entry == NULL || !has) {
        qa_windows_last_error_set(host, 6);
        return bool_result(false);
      }
      if (entry->stream_kind == 0 && entry->file->close != NULL)
        entry->file->close(entry->file->ctx);
      size_t index = (size_t)(entry - k->handles);
      memmove(&k->handles[index], &k->handles[index + 1],
              (k->handle_count - index - 1) * sizeof(file_handle));
      k->handle_count--;
      return bool_result(true);
    }
    case K_FLUSH_FILE_BUFFERS: {
      qa_guest_address address = {0};
      bool has = qa_guest_pointer_argument(args, count, 0, &address);
      file_handle *entry = handle_at(k, has, address);
      if (entry == NULL) return bool_result(false);
      if (entry->stream_kind == 0 && entry->file->flush != NULL)
        entry->file->flush(entry->file->ctx);
      return bool_result(true);
    }
    case K_SET_FILE_POINTER: {
      qa_guest_address address = {0};
      bool has = qa_guest_pointer_argument(args, count, 0, &address);
      file_handle *entry = handle_at(k, has, address);
      if (entry == NULL || entry->stream_kind != 0) {
        qa_windows_last_error_set(host, 6);
        return u32_result(0xffffffffu);
      }
      qa_guest_address high = {0};
      bool has_high = qa_guest_pointer_argument(args, count, 2, &high);
      int64_t low = qa_guest_integer(args, count, 1);
      int64_t distance = !has_high ? low
          : (int64_t)(((uint64_t)(int64_t)(int32_t)qa_read_unsigned(memory, high, 4) << 32) |
                      (uint32_t)low);
      int64_t origin = qa_guest_integer(args, count, 3);
      int64_t origin_base = origin == 0 ? 0 : origin == 1 ? (int64_t)entry->offset
                                                           : entry->file->size(entry->file->ctx);
      bool overflow = (distance > 0 && origin_base > INT64_MAX - distance) ||
                      (distance < 0 && origin_base < INT64_MIN - distance);
      int64_t position = overflow ? INT64_MAX : origin_base + distance;
      if (origin > 2 || position < 0 || position > QA_MAX_SAFE_INTEGER) {
        qa_windows_last_error_set(host, 87);
        return u32_result(0xffffffffu);
      }
      entry->offset = (size_t)position;
      if (has_high) qa_write_unsigned(memory, high, 4, (uint64_t)position >> 32);
      return u32_result((uint32_t)position);
    }
    case K_SET_END_OF_FILE: {
      qa_guest_address address = {0};
      bool has = qa_guest_pointer_argument(args, count, 0, &address);
      file_handle *entry = handle_at(k, has, address);
      if (entry == NULL || entry->stream_kind != 0) return bool_result(false);
      entry->file->truncate(entry->file->ctx, entry->offset);
      return bool_result(true);
    }
    case K_MULTIBYTE_TO_WIDE: {
      int64_t cp = qa_guest_integer(args, count, 0);
      if (!(cp == 0 || cp == 1252 || cp == 65001)) {
        char detail[32];
        (void)snprintf(detail, sizeof detail, "code page %lld", (long long)cp);
        qa_unsupported_windows_import("kernel32.dll", "MultiByteToWideChar", detail);
      }
      size_t source_len = 0;
      uint16_t *source = input_units(k, qa_guest_required_pointer(args, count, 2),
                                     qa_guest_integer(args, count, 3), false, &source_len);
      uint8_t *source_bytes = xmalloc(source_len);
      for (size_t i = 0; i < source_len; i++) source_bytes[i] = (uint8_t)source[i];
      free(source);
      size_t text_len = 0;
      uint16_t *text;
      if (cp == 65001) {
        text = utf8_decode(source_bytes, source_len, &text_len);
      } else {
        text = xmalloc((source_len == 0 ? 1 : source_len) * sizeof *text);
        for (size_t i = 0; i < source_len; i++) text[i] = cp1252_decode(source_bytes[i]);
        text_len = source_len;
      }
      free(source_bytes);
      int64_t capacity = qa_guest_integer(args, count, 5);
      qa_guest_address output = {0};
      bool has_output = qa_guest_pointer_argument(args, count, 4, &output);
      if (capacity != 0) {
        if (!has_output || capacity < (int64_t)text_len) {
          free(text);
          qa_windows_last_error_set(host, 122);
          return qa_call_int32(0);
        }
        size_t bytes_len = 0;
        uint8_t *bytes = qa_string_bytes_wide(text, text_len, true, &bytes_len);
        qa_memory_write(memory, output, bytes, text_len * 2);
        free(bytes);
      }
      free(text);
      return qa_call_int32((int32_t)text_len);
    }
    case K_WIDE_TO_MULTIBYTE: {
      int64_t cp = qa_guest_integer(args, count, 0);
      if (!(cp == 0 || cp == 1252 || cp == 65001)) {
        char detail[32];
        (void)snprintf(detail, sizeof detail, "code page %lld", (long long)cp);
        qa_unsupported_windows_import("kernel32.dll", "WideCharToMultiByte", detail);
      }
      size_t source_len = 0;
      uint16_t *source = input_units(k, qa_guest_required_pointer(args, count, 2),
                                     qa_guest_integer(args, count, 3), true, &source_len);
      bool used_default = false;
      size_t bytes_len = 0;
      uint8_t *bytes;
      if (cp == 65001) {
        bytes = utf8_encode(source, source_len, &bytes_len);
      } else {
        bytes = xmalloc(source_len == 0 ? 1 : source_len);
        for (size_t i = 0; i < source_len; i++) {
          int found = -1;
          for (int byte = 0; byte < 256; byte++)
            if (cp1252_decode((uint8_t)byte) == source[i]) found = byte;
          if (found >= 0) bytes[i] = (uint8_t)found;
          else { used_default = true; bytes[i] = 63; }
        }
        bytes_len = source_len;
      }
      free(source);
      int64_t capacity = qa_guest_integer(args, count, 5);
      qa_guest_address output = {0}, used = {0};
      bool has_output = qa_guest_pointer_argument(args, count, 4, &output);
      if (qa_guest_pointer_argument(args, count, 7, &used))
        qa_write_unsigned(memory, used, 4, used_default ? 1 : 0);
      if (capacity != 0) {
        if (!has_output || capacity < (int64_t)bytes_len) {
          free(bytes);
          qa_windows_last_error_set(host, 122);
          return qa_call_int32(0);
        }
        qa_memory_write(memory, output, bytes, bytes_len);
      }
      free(bytes);
      return qa_call_int32((int32_t)bytes_len);
    }
    case K_GET_STRING_TYPE: {
      int shift = wide ? 0 : 1;
      if (qa_guest_integer(args, count, (size_t)shift) != 1)
        qa_unsupported_windows_import("kernel32.dll", "GetStringType",
                                      "only CT_CTYPE1 is implemented");
      size_t source_len = 0;
      uint16_t *source = input_units(
          k, qa_guest_required_pointer(args, count, (size_t)(shift + 1)),
          qa_guest_integer(args, count, (size_t)(shift + 2)), wide, &source_len);
      qa_guest_address output = qa_guest_required_pointer(args, count, (size_t)(shift + 3));
      for (size_t i = 0; i < source_len; i++) {
        uint16_t c = source[i];
        bool is_upper = c >= 'A' && c <= 'Z', is_lower = c >= 'a' && c <= 'z';
        bool is_digit = c >= '0' && c <= '9';
        bool is_punct = (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
                        (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
        bool is_hex = is_digit || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        int flags = (is_upper ? 1 : 0) | (is_lower ? 2 : 0) | (is_digit ? 4 : 0) |
                    (js_space_u16(c) ? 8 : 0) | (is_punct ? 16 : 0) |
                    (c < 32 || c == 127 ? 32 : 0) | (c == ' ' || c == '\t' ? 64 : 0) |
                    (is_hex ? 128 : 0) | (is_upper || is_lower ? 256 : 0);
        qa_write_unsigned(memory, off(memory, output, (int64_t)i * 2), 2, (uint64_t)flags);
      }
      free(source);
      return qa_call_int32(1);
    }
    case K_LCMAP: {
      int64_t flags = qa_guest_integer(args, count, 1);
      if (flags != 0x100 && flags != 0x200) {
        char detail[32];
        (void)snprintf(detail, sizeof detail, "flags %lld", (long long)flags);
        qa_unsupported_windows_import("kernel32.dll", "LCMapString", detail);
      }
      size_t source_len = 0;
      uint16_t *source = input_units(k, qa_guest_required_pointer(args, count, 2),
                                     qa_guest_integer(args, count, 3), wide, &source_len);
      /* JS toLowerCase/toUpperCase reduced to ASCII + Latin-1 simple
       * mappings (with ß -> SS); full Unicode casing is not modeled. */
      uint16_t *mapped = xmalloc((source_len * 2 + 1) * sizeof *mapped);
      size_t mapped_len = 0;
      for (size_t i = 0; i < source_len; i++) {
        uint16_t c = source[i];
        if (flags == 0x100) { /* lower */
          if (c >= 'A' && c <= 'Z') c = (uint16_t)(c + 32);
          else if (c >= 0xc0 && c <= 0xde && c != 0xd7) c = (uint16_t)(c + 32);
          else if (c == 0x178) c = 0xff;
          mapped[mapped_len++] = c;
        } else {
          if (c == 0xdf) { mapped[mapped_len++] = 'S'; mapped[mapped_len++] = 'S'; continue; }
          if (c >= 'a' && c <= 'z') c = (uint16_t)(c - 32);
          else if (c >= 0xe0 && c <= 0xfe && c != 0xf7) c = (uint16_t)(c - 32);
          else if (c == 0xff) c = 0x178;
          mapped[mapped_len++] = c;
        }
      }
      free(source);
      int64_t capacity = qa_guest_integer(args, count, 5);
      qa_guest_address output = {0};
      bool has_output = qa_guest_pointer_argument(args, count, 4, &output);
      if (capacity != 0) {
        if (!has_output || capacity < (int64_t)mapped_len) {
          free(mapped);
          qa_windows_last_error_set(host, 122);
          return qa_call_int32(0);
        }
        size_t bytes_len = 0;
        uint8_t *bytes = qa_string_bytes_wide(mapped, mapped_len, wide, &bytes_len);
        qa_memory_write(memory, output, bytes, mapped_len * (wide ? 2 : 1));
        free(bytes);
      }
      free(mapped);
      return qa_call_int32((int32_t)mapped_len);
    }
    default: break;
  }
  (void)none;
  return qa_call_void();
}

void qa_install_kernel(qa_windows_service_host *host) {
  kernel_ctx *k = xmalloc(sizeof *k);
  memset(k, 0, sizeof *k);
  k->host = host;
  qa_mapped_guest_memory *memory = host->memory;
  int width = memory->pointer_bytes;
  const char *command_line_text = host->capabilities.command_line != NULL
      ? host->capabilities.command_line : "\"quake-typescript.exe\"";
  k->command_line = stored_string(k, command_line_text, false);
  k->command_line_w = stored_string(k, command_line_text, true);
  k->environment_a = stored_environment(k, false);
  k->environment_w = stored_environment(k, true);
  k->has_invalid = qa_memory_pointer(memory, width == 4 ? 0xffffffffULL : UINT64_MAX,
                                      &k->invalid);
  /* crypto.getRandomValues secret, forced odd. */
  {
    uint8_t secret_bytes[8] = {0};
    FILE *random = fopen("/dev/urandom", "rb");
    if (random != NULL) {
      if (fread(secret_bytes, 1, (size_t)width, random) != (size_t)width)
        memset(secret_bytes, 0xa5, sizeof secret_bytes);
      (void)fclose(random);
    }
    uint64_t secret = 0;
    for (int i = width - 1; i >= 0; i--) secret = (secret << 8) | secret_bytes[i];
    k->pointer_secret = secret | 1;
  }
  qa_abi_storage ptr = QA_STORAGE_POINTER, i32 = QA_STORAGE_INT32, u32 = QA_STORAGE_UINT32,
                 u64 = QA_STORAGE_UINT64, size_param = host->pointer_storage;
  reg(k, K_ENCODE, 0, "EncodePointer", &ptr, 1, false, ptr);
  reg(k, K_ENCODE, 0, "DecodePointer", &ptr, 1, false, ptr);
  k->process_heap = qa_gmem_allocate(host->memory_ext, 16, false, 0, false,
                                     QA_PERM_READ_WRITE, "Windows process heap");
  u64_set_add(&k->heaps, &k->heap_count, &k->heap_cap, k->process_heap.byte_offset);
  reg(k, K_GET_PROCESS_HEAP, 0, "GetProcessHeap", NULL, 0, false, ptr);
  reg(k, K_GET_LAST_ERROR, 0, "GetLastError", NULL, 0, false, u32);
  reg(k, K_SET_LAST_ERROR, 0, "SetLastError", &u32, 1, true, i32);
  reg(k, K_GET_THREAD_ID, 0, "GetCurrentThreadId", NULL, 0, false, u32);
  reg(k, K_GET_PROCESS_ID, 0, "GetCurrentProcessId", NULL, 0, false, u32);
  reg(k, K_GET_CURRENT_PROCESS, 0, "GetCurrentProcess", NULL, 0, false, ptr);
  reg(k, K_GET_VERSION, 0, "GetVersion", NULL, 0, false, u32);
  reg(k, K_GET_COMMAND_LINE_A, 0, "GetCommandLineA", NULL, 0, false, ptr);
  reg(k, K_GET_COMMAND_LINE_W, 0, "GetCommandLineW", NULL, 0, false, ptr);
  reg(k, K_GET_ACP, 0, "GetACP", NULL, 0, false, u32);
  reg(k, K_GET_OEMCP, 0, "GetOEMCP", NULL, 0, false, u32);
  reg(k, K_IS_VALID_CP, 0, "IsValidCodePage", &u32, 1, false, i32);
  qa_abi_storage cp_info_params[] = {u32, ptr};
  reg(k, K_GET_CP_INFO, 0, "GetCPInfo", cp_info_params, 2, false, i32);
  reg(k, K_GET_ENV_STRINGS, 0, "GetEnvironmentStrings", NULL, 0, false, ptr);
  reg(k, K_GET_ENV_STRINGS, 0, "GetEnvironmentStringsA", NULL, 0, false, ptr);
  reg(k, K_GET_ENV_STRINGS, 1, "GetEnvironmentStringsW", NULL, 0, false, ptr);
  reg(k, K_FREE_ENV_STRINGS, 0, "FreeEnvironmentStringsA", &ptr, 1, false, i32);
  reg(k, K_FREE_ENV_STRINGS, 0, "FreeEnvironmentStringsW", &ptr, 1, false, i32);
  for (int w = 0; w < 2; w++) {
    char suffix = w ? 'W' : 'A';
    char name[48];
    (void)snprintf(name, sizeof name, "GetModuleHandle%c", suffix);
    reg(k, K_GET_MODULE_HANDLE, w, name, &ptr, 1, false, ptr);
    (void)snprintf(name, sizeof name, "LoadLibrary%c", suffix);
    reg(k, K_LOAD_LIBRARY, w, name, &ptr, 1, false, ptr);
    qa_abi_storage ex_params[] = {ptr, ptr, u32};
    (void)snprintf(name, sizeof name, "LoadLibraryEx%c", suffix);
    reg(k, K_LOAD_LIBRARY_EX, w, name, ex_params, 3, false, ptr);
  }
  reg(k, K_FREE_LIBRARY, 0, "FreeLibrary", &ptr, 1, false, i32);
  qa_abi_storage two_ptrs[] = {ptr, ptr};
  reg(k, K_GET_PROC_ADDRESS, 0, "GetProcAddress", two_ptrs, 2, false, ptr);
  reg(k, K_DISABLE_THREAD_CALLS, 0, "DisableThreadLibraryCalls", &ptr, 1, false, i32);
  qa_abi_storage file_name_params[] = {ptr, ptr, u32};
  reg(k, K_GET_MODULE_FILE_NAME, 0, "GetModuleFileNameA", file_name_params, 3, false, u32);
  reg(k, K_GET_MODULE_FILE_NAME, 1, "GetModuleFileNameW", file_name_params, 3, false, u32);
  qa_abi_storage heap_create_params[] = {u32, size_param, size_param};
  reg(k, K_HEAP_CREATE, 0, "HeapCreate", heap_create_params, 3, false, ptr);
  qa_abi_storage heap_alloc_params[] = {ptr, u32, size_param};
  reg(k, K_HEAP_ALLOC, 0, "HeapAlloc", heap_alloc_params, 3, false, ptr);
  qa_abi_storage heap_free_params[] = {ptr, u32, ptr};
  reg(k, K_HEAP_FREE, 0, "HeapFree", heap_free_params, 3, false, i32);
  qa_abi_storage heap_realloc_params[] = {ptr, u32, ptr, size_param};
  reg(k, K_HEAP_REALLOC, 0, "HeapReAlloc", heap_realloc_params, 4, false, ptr);
  reg(k, K_HEAP_DESTROY, 0, "HeapDestroy", &ptr, 1, false, i32);
  qa_abi_storage virtual_alloc_params[] = {ptr, size_param, u32, u32};
  reg(k, K_VIRTUAL_ALLOC, 0, "VirtualAlloc", virtual_alloc_params, 4, false, ptr);
  qa_abi_storage virtual_free_params[] = {ptr, size_param, u32};
  reg(k, K_VIRTUAL_FREE, 0, "VirtualFree", virtual_free_params, 3, false, i32);
  k->dynamic_tls = qa_gmem_allocate(host->memory_ext, (size_t)(1024 * width), true, 16,
                                    false, QA_PERM_READ_WRITE,
                                    "Windows expansion TLS slots");
  qa_write_pointer(memory, off(memory, host->teb, width == 4 ? 0xf94 : 0x1780), true,
                   k->dynamic_tls);
  reg(k, K_TLS_ALLOC, 0, "TlsAlloc", NULL, 0, false, u32);
  reg(k, K_TLS_FREE, 0, "TlsFree", &u32, 1, false, i32);
  reg(k, K_TLS_GET, 0, "TlsGetValue", &u32, 1, false, ptr);
  qa_abi_storage tls_set_params[] = {u32, ptr};
  reg(k, K_TLS_SET, 0, "TlsSetValue", tls_set_params, 2, false, i32);
  reg(k, K_FLS_ALLOC, 0, "FlsAlloc", &ptr, 1, false, u32);
  reg(k, K_FLS_GET, 0, "FlsGetValue", &u32, 1, false, ptr);
  reg(k, K_FLS_SET, 0, "FlsSetValue", tls_set_params, 2, false, i32);
  reg(k, K_FLS_FREE, 0, "FlsFree", &u32, 1, false, i32);
  reg(k, K_INIT_CS, 0, "InitializeCriticalSection", &ptr, 1, true, i32);
  qa_abi_storage spin_params[] = {ptr, u32};
  reg(k, K_INIT_CS_SPIN, 0, "InitializeCriticalSectionAndSpinCount", spin_params, 2, false,
      i32);
  qa_abi_storage cs_ex_params[] = {ptr, u32, u32};
  reg(k, K_INIT_CS_EX, 0, "InitializeCriticalSectionEx", cs_ex_params, 3, false, i32);
  reg(k, K_SET_SPIN, 0, "SetCriticalSectionSpinCount", spin_params, 2, false, u32);
  reg(k, K_DELETE_CS, 0, "DeleteCriticalSection", &ptr, 1, true, i32);
  reg(k, K_ENTER_CS, 0, "EnterCriticalSection", &ptr, 1, true, i32);
  reg(k, K_LEAVE_CS, 0, "LeaveCriticalSection", &ptr, 1, true, i32);
  reg(k, K_INTERLOCKED, 1, "InterlockedIncrement", &ptr, 1, false, i32);
  reg(k, K_INTERLOCKED, -1, "InterlockedDecrement", &ptr, 1, false, i32);
  reg(k, K_ACQUIRE_SRW, 0, "AcquireSRWLockExclusive", &ptr, 1, true, i32);
  reg(k, K_RELEASE_SRW, 0, "ReleaseSRWLockExclusive", &ptr, 1, true, i32);
  reg(k, K_INIT_SLIST, 0, "InitializeSListHead", &ptr, 1, true, i32);
  reg(k, K_FLUSH_SLIST, 0, "InterlockedFlushSList", &ptr, 1, false, ptr);
  reg(k, K_WAKE_ALL, 0, "WakeAllConditionVariable", &ptr, 1, true, i32);
  reg(k, K_IS_DEBUGGER, 0, "IsDebuggerPresent", NULL, 0, false, i32);
  reg(k, K_IS_PROCESSOR_FEATURE, 0, "IsProcessorFeaturePresent", &u32, 1, false, i32);
  reg(k, K_GET_SYSTEM_TIME_FILETIME, 0, "GetSystemTimeAsFileTime", &ptr, 1, true, i32);
  reg(k, K_QPC, 0, "QueryPerformanceCounter", &ptr, 1, false, i32);
  reg(k, K_QPF, 0, "QueryPerformanceFrequency", &ptr, 1, false, i32);
  reg(k, K_GET_TIME, 0, "GetSystemTime", &ptr, 1, true, i32);
  reg(k, K_GET_TIME, 1, "GetLocalTime", &ptr, 1, true, i32);
  reg(k, K_GET_TIMEZONE, 0, "GetTimeZoneInformation", &ptr, 1, false, u32);
  reg(k, K_SET_UNHANDLED_FILTER, 0, "SetUnhandledExceptionFilter", &ptr, 1, false, ptr);
  qa_abi_storage rtl_params[] = {u64, ptr, ptr};
  reg(k, K_RTL_LOOKUP, 0, "RtlLookupFunctionEntry", rtl_params, 3, false, ptr);
  /* installFiles */
  {
    const struct { int64_t id; int kind; const char *name; } std_streams[] = {
        {-10, 1, "stdin"}, {-11, 2, "stdout"}, {-12, 3, "stderr"}};
    for (size_t i = 0; i < 3; i++) {
      qa_guest_address address = stored_string(k, std_streams[i].name, false);
      standard_set(k, std_streams[i].id, address);
      grow((void **)&k->handles, &k->handle_cap, k->handle_count, sizeof(file_handle));
      k->handles[k->handle_count].key = address.byte_offset;
      k->handles[k->handle_count].stream_kind = std_streams[i].kind;
      k->handles[k->handle_count].file = NULL;
      k->handles[k->handle_count].offset = 0;
      k->handle_count++;
    }
  }
  reg(k, K_GET_STD_HANDLE, 0, "GetStdHandle", &i32, 1, false, ptr);
  qa_abi_storage set_std_params[] = {i32, ptr};
  reg(k, K_SET_STD_HANDLE, 0, "SetStdHandle", set_std_params, 2, false, i32);
  reg(k, K_SET_HANDLE_COUNT, 0, "SetHandleCount", &u32, 1, false, u32);
  reg(k, K_GET_FILE_TYPE, 0, "GetFileType", &ptr, 1, false, u32);
  reg(k, K_GET_STARTUP_INFO, 0, "GetStartupInfoA", &ptr, 1, true, i32);
  reg(k, K_GET_STARTUP_INFO, 0, "GetStartupInfoW", &ptr, 1, true, i32);
  qa_abi_storage create_params[] = {ptr, u32, u32, ptr, u32, u32, ptr};
  reg(k, K_CREATE_FILE_A, 0, "CreateFileA", create_params, 7, false, ptr);
  qa_abi_storage io_params[] = {ptr, ptr, u32, ptr, ptr};
  reg(k, K_READ_FILE, 0, "ReadFile", io_params, 5, false, i32);
  reg(k, K_WRITE_FILE, 0, "WriteFile", io_params, 5, false, i32);
  reg(k, K_CLOSE_HANDLE, 0, "CloseHandle", &ptr, 1, false, i32);
  reg(k, K_FLUSH_FILE_BUFFERS, 0, "FlushFileBuffers", &ptr, 1, false, i32);
  qa_abi_storage set_ptr_params[] = {ptr, i32, ptr, u32};
  reg(k, K_SET_FILE_POINTER, 0, "SetFilePointer", set_ptr_params, 4, false, u32);
  reg(k, K_SET_END_OF_FILE, 0, "SetEndOfFile", &ptr, 1, false, i32);
  /* installLocale */
  qa_abi_storage mb_params[] = {u32, u32, ptr, i32, ptr, i32};
  reg(k, K_MULTIBYTE_TO_WIDE, 0, "MultiByteToWideChar", mb_params, 6, false, i32);
  qa_abi_storage wc_params[] = {u32, u32, ptr, i32, ptr, i32, ptr, ptr};
  reg(k, K_WIDE_TO_MULTIBYTE, 0, "WideCharToMultiByte", wc_params, 8, false, i32);
  qa_abi_storage type_w_params[] = {u32, ptr, i32, ptr};
  qa_abi_storage type_a_params[] = {u32, u32, ptr, i32, ptr};
  reg(k, K_GET_STRING_TYPE, 1, "GetStringTypeW", type_w_params, 4, false, i32);
  reg(k, K_GET_STRING_TYPE, 0, "GetStringTypeA", type_a_params, 5, false, i32);
  qa_abi_storage lcmap_params[] = {u32, u32, ptr, i32, ptr, i32};
  reg(k, K_LCMAP, 1, "LCMapStringW", lcmap_params, 6, false, i32);
  reg(k, K_LCMAP, 0, "LCMapStringA", lcmap_params, 6, false, i32);
}
