/* Port of src/guest/runtime/common/format/services.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/common/format/services.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/floating_point/binary.h"
#include "qa/guest/runtime/common/format.h"
#include "qa/guest/runtime/common/format/scan.h"
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
static qa_format_rounding to_format_rounding(qa_fp_rounding mode) {
  switch (mode) {
    case QA_FP_NEAREST: return QA_FORMAT_NEAREST;
    case QA_FP_DOWN: return QA_FORMAT_DOWN;
    case QA_FP_UP: return QA_FORMAT_UP;
    case QA_FP_ZERO: return QA_FORMAT_ZERO;
  }
  return QA_FORMAT_NEAREST;
}

typedef struct windows_format_state {
  qa_windows_service_host *host;
  qa_guest_core_address errno_address;
  char *library; /* owned */
} windows_format_state;

static qa_call_result vsscanf_impl(void *ctx, const qa_guest_core_call_context *context,
                                   const qa_call_value *args, size_t count) {
  windows_format_state *state = ctx;
  (void)context;
  qa_guest_core_address ignored = {0};
  bool has_locale = qa_guest_pointer_argument(args, count, 4, &ignored);
  if (has_locale || (qa_guest_integer_u64(args, count, 0) & ~2ULL) != 0)
    qa_unsupported_windows_import(state->library, "__stdio_common_vsscanf",
                                  "scanf locale or options are not implemented");
  qa_guest_core_address arguments = {0};
  bool has_arguments = qa_guest_pointer_argument(args, count, 5, &arguments);
  qa_scan_windows_buffer_options options;
  options.memory = state->host->memory;
  options.memory_ext = state->host->memory_ext;
  options.input = qa_guest_required_pointer(args, count, 1);
  options.capacity = qa_guest_integer(args, count, 2);
  options.format = qa_guest_required_pointer(args, count, 3);
  options.has_arguments = has_arguments;
  options.arguments = arguments;
  qa_guest_scan_catch catch_;
  int result = qa_scan_windows_buffer(&options, &catch_);
  if (result == -2)
    qa_unsupported_windows_import(state->library, "__stdio_common_vsscanf", catch_.message);
  return qa_call_int32(result);
}

static qa_call_result vsprintf_impl(void *ctx, const qa_guest_core_call_context *context,
                                    const qa_call_value *args, size_t count) {
  windows_format_state *state = ctx;
  (void)context;
  qa_windows_service_host *host = state->host;
  uint64_t options = qa_guest_integer_u64(args, count, 0);
  qa_guest_core_address ignored = {0};
  if (qa_guest_pointer_argument(args, count, 4, &ignored))
    qa_unsupported_windows_import(state->library, "__stdio_common_vsprintf",
                                  "explicit printf locale is not the installed C locale");
  if ((options & ~0x3fULL) != 0 || (options & 8) != 0)
    qa_unsupported_windows_import(state->library, "__stdio_common_vsprintf",
                                  "legacy MSVCRT compatibility formatting is not implemented");
  qa_guest_format_request request; memset(&request, 0, sizeof request);
  request.memory = host->memory;
  request.memory_ext = host->memory_ext;
  request.dialect = QA_FORMAT_WINDOWS;
  request.has_buffer = qa_guest_pointer_argument(args, count, 1, &request.buffer);
  request.capacity = qa_guest_integer(args, count, 2);
  request.has_format = qa_guest_pointer_argument(args, count, 3, &request.format);
  request.has_arguments = qa_guest_pointer_argument(args, count, 5, &request.arguments);
  request.termination = (options & 1) != 0 ? QA_TERMINATION_UCRT_LEGACY
      : (options & 2) != 0 ? QA_TERMINATION_C99 : QA_TERMINATION_UCRT;
  request.continue_count = (options & 2) != 0;
  qa_guest_call_runner_options runner_options = qa_guest_call_runner_options_of(host->runner);
  request.rounding = (options & 32) != 0
      ? to_format_rounding(qa_fp_rounding_from_bits((int)(runner_options.cpu->state->simd.mxcsr >> 13) & 3))
      : QA_FORMAT_LEGACY_NEAREST;
  request.exponent_digits = (options & 16) != 0 ? 3 : 2;
  qa_guest_format_result result = qa_format_guest_buffer(&request);
  if (result.has_errno) qa_gmem_write_i32(host->memory, state->errno_address, result.errno_value);
  return qa_call_int32(result.result);
}

void qa_install_windows_format(qa_windows_service_host *host, qa_guest_core_address errno_address) {
  static const char *const libraries[] = {"api-ms-win-crt-stdio-l1-1-0.dll", "ucrtbase.dll", "msvcrt.dll"};
  qa_abi_storage parameters[] = {QA_STORAGE_UINT64, QA_STORAGE_POINTER, QA_STORAGE_POINTER,
                                 QA_STORAGE_POINTER, QA_STORAGE_POINTER, QA_STORAGE_POINTER};
  parameters[2] = host->pointer_storage;
  for (size_t i = 0; i < 3; i++) {
    windows_format_state *scan_state = xmalloc(sizeof *scan_state);
    scan_state->host = host; scan_state->errno_address = errno_address;
    scan_state->library = xstrdup(libraries[i]);
    qa_windows_import_implementation scan_impl = {scan_state, vsscanf_impl};
    qa_windows_service(host, libraries[i], "__stdio_common_vsscanf", parameters, 6,
                       false, QA_STORAGE_INT32, scan_impl);
    windows_format_state *printf_state = xmalloc(sizeof *printf_state);
    printf_state->host = host; printf_state->errno_address = errno_address;
    printf_state->library = xstrdup(libraries[i]);
    qa_windows_import_implementation printf_impl = {printf_state, vsprintf_impl};
    qa_windows_service(host, libraries[i], "__stdio_common_vsprintf", parameters, 6,
                       false, QA_STORAGE_INT32, printf_impl);
  }
}

typedef struct system_v_format_state {
  qa_system_v_service_host *host;
  bool checked;
} system_v_format_state;

static qa_call_result vsnprintf_impl(void *ctx, const qa_guest_core_call_context *context,
                                     const qa_call_value *args, size_t count) {
  system_v_format_state *state = ctx;
  (void)context;
  qa_system_v_service_host *host = state->host;
  bool checked = state->checked;
  int64_t capacity = qa_guest_integer(args, count, 1);
  if (checked && qa_guest_integer(args, count, 3) < capacity)
    qa_guest_format_fortify_failure("glibc __vsnprintf_chk destination size is smaller than maxlen");
  if (capacity > 0) qa_memory_write_u8(host->memory, qa_guest_required_pointer(args, count, 0), 0);
  qa_guest_format_request request; memset(&request, 0, sizeof request);
  request.memory = host->memory;
  request.memory_ext = host->memory_ext;
  request.dialect = QA_FORMAT_SYSTEM_V;
  request.has_buffer = qa_guest_pointer_argument(args, count, 0, &request.buffer);
  request.capacity = capacity;
  request.has_format = qa_guest_pointer_argument(args, count, checked ? 4 : 2, &request.format);
  request.has_arguments = qa_guest_pointer_argument(args, count, checked ? 5 : 3, &request.arguments);
  request.termination = QA_TERMINATION_C99;
  request.fortify = checked && qa_guest_integer(args, count, 2) > 0;
  qa_guest_call_runner_options runner_options = qa_guest_call_runner_options_of(host->runner);
  request.rounding = to_format_rounding(qa_fp_rounding_from_bits(
      (int)(runner_options.cpu->state->x87.control_word >> 10) & 3));
  request.exponent_digits = 2;
  qa_guest_format_result result = qa_format_guest_buffer(&request);
  if (result.has_errno) qa_system_v_errno_set(host, result.errno_value);
  return qa_call_int32(result.result);
}

void qa_install_system_v_format(qa_system_v_service_host *host) {
  const char *base = host->memory->pointer_bytes == 4 ? "GLIBC_2.0" : "GLIBC_2.2.5";
  for (int pass = 0; pass < 2; pass++) {
    bool checked = pass == 1;
    const char *name = checked ? "__vsnprintf_chk" : "vsnprintf";
    const char *versions[] = {checked ? "GLIBC_2.3.4" : base, NULL};
    qa_abi_storage plain[] = {QA_STORAGE_POINTER, QA_STORAGE_POINTER, QA_STORAGE_POINTER, QA_STORAGE_POINTER};
    qa_abi_storage chk[] = {QA_STORAGE_POINTER, QA_STORAGE_POINTER, QA_STORAGE_INT32,
                            QA_STORAGE_POINTER, QA_STORAGE_POINTER, QA_STORAGE_POINTER};
    plain[1] = host->pointer_storage;
    chk[1] = host->pointer_storage;
    chk[3] = host->pointer_storage;
    system_v_format_state *state = xmalloc(sizeof *state);
    state->host = host; state->checked = checked;
    qa_system_v_implementation impl = {state, vsnprintf_impl};
    qa_system_v_service(host, "libc.so.6", name, versions, 2,
                        checked ? chk : plain, checked ? 6 : 4, false, QA_STORAGE_INT32, impl);
  }
}
