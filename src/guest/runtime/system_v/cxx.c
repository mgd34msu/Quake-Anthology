/* Port of src/guest/runtime/system-v/cxx.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/system_v/cxx.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/memory.h"

typedef struct cxx_state {
  qa_system_v_service_host *host;
  char *name; /* owned, operator new/delete loop variants */
} cxx_state;

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
static cxx_state *new_state(qa_system_v_service_host *host) {
  cxx_state *state = xmalloc(sizeof *state);
  state->host = host; state->name = NULL;
  return state;
}
static void reg(qa_system_v_service_host *host, const char *library, const char *name,
                const char *const *versions, size_t version_count,
                const qa_abi_storage *parameters, size_t parameter_count, bool result_is_void,
                qa_abi_storage result,
                qa_call_result (*invoke)(void *, const qa_guest_call_context *,
                                         const qa_call_value *, size_t)) {
  qa_system_v_implementation impl = {new_state(host), invoke};
  qa_system_v_service(host, library, name, versions, version_count, parameters, parameter_count,
                      result_is_void, result, impl);
}

static qa_call_result cxa_atexit_impl(void *ctx, const qa_guest_call_context *context,
                                      const qa_call_value *args, size_t count) {
  cxx_state *state = ctx; (void)context;
  qa_system_v_service_host *host = state->host;
  qa_guest_address argument = {0}, dso = {0};
  bool has_argument = qa_guest_pointer_argument(args, count, 1, &argument);
  bool has_dso = qa_guest_pointer_argument(args, count, 2, &dso);
  host->register_destructor(host->ctx, qa_guest_required_pointer(args, count, 0),
                            has_argument, argument, has_dso, dso);
  return qa_call_int32(0);
}
static qa_call_result cxa_finalize_impl(void *ctx, const qa_guest_call_context *context,
                                        const qa_call_value *args, size_t count) {
  cxx_state *state = ctx;
  qa_system_v_service_host *host = state->host;
  qa_guest_address dso = {0};
  bool has_dso = qa_guest_pointer_argument(args, count, 0, &dso);
  host->finalize_destructors(host->ctx, context, has_dso, dso);
  return qa_call_void();
}
static qa_call_result guard_acquire_impl(void *ctx, const qa_guest_call_context *context,
                                         const qa_call_value *args, size_t count) {
  cxx_state *state = ctx; (void)context;
  qa_mapped_guest_memory *m = state->host->memory;
  qa_guest_address guard = qa_guest_required_pointer(args, count, 0);
  if (qa_memory_read_u8(m, guard) != 0) return qa_call_int32(0);
  if (qa_memory_read_u8(m, qa_memory_offset(m, guard, 1)) != 0)
    qa_unsupported_system_v_service("libstdc++.so.6", "__cxa_guard_acquire", "CXXABI_1.3",
                                    "recursive local static initialization");
  qa_memory_write_u8(m, qa_memory_offset(m, guard, 1), 1);
  return qa_call_int32(1);
}
static qa_call_result guard_release_impl(void *ctx, const qa_guest_call_context *context,
                                         const qa_call_value *args, size_t count) {
  cxx_state *state = ctx; (void)context;
  qa_mapped_guest_memory *m = state->host->memory;
  qa_guest_address guard = qa_guest_required_pointer(args, count, 0);
  qa_memory_write_u8(m, guard, 1);
  qa_memory_write_u8(m, qa_memory_offset(m, guard, 1), 0);
  return qa_call_void();
}
static qa_call_result guard_abort_impl(void *ctx, const qa_guest_call_context *context,
                                       const qa_call_value *args, size_t count) {
  cxx_state *state = ctx; (void)context;
  qa_mapped_guest_memory *m = state->host->memory;
  qa_memory_write_u8(m, qa_memory_offset(m, qa_guest_required_pointer(args, count, 0), 1), 0);
  return qa_call_void();
}
static qa_call_result operator_new_impl(void *ctx, const qa_guest_call_context *context,
                                        const qa_call_value *args, size_t count) {
  cxx_state *state = ctx; (void)context;
  return qa_call_pointer(true, qa_system_v_allocate(state->host, qa_guest_count(args, count, 0)));
}
static qa_call_result operator_delete_impl(void *ctx, const qa_guest_call_context *context,
                                           const qa_call_value *args, size_t count) {
  cxx_state *state = ctx; (void)context;
  qa_guest_address address = {0};
  bool has = qa_guest_pointer_argument(args, count, 0, &address);
  qa_system_v_free(state->host, has, address);
  return qa_call_void();
}
static size_t exception_header(const qa_system_v_service_host *host) {
  return host->memory->pointer_bytes == 4 ? 96 : 128;
}
static qa_call_result allocate_exception_impl(void *ctx, const qa_guest_call_context *context,
                                              const qa_call_value *args, size_t count) {
  cxx_state *state = ctx; (void)context;
  qa_system_v_service_host *host = state->host;
  qa_guest_address allocation = qa_system_v_allocate(
      host, qa_guest_count(args, count, 0) + exception_header(host));
  return qa_call_pointer(true, qa_memory_offset(host->memory, allocation,
                                                (int64_t)exception_header(host)));
}
static qa_call_result free_exception_impl(void *ctx, const qa_guest_call_context *context,
                                          const qa_call_value *args, size_t count) {
  cxx_state *state = ctx; (void)context;
  qa_system_v_service_host *host = state->host;
  qa_system_v_free(host, true,
                   qa_memory_offset(host->memory, qa_guest_required_pointer(args, count, 0),
                                    -(int64_t)exception_header(host)));
  return qa_call_void();
}

void qa_install_cxx(qa_system_v_service_host *host) {
  qa_mapped_guest_memory *m = host->memory;
  const char *cxx = "libstdc++.so.6";
  const char *cxa[] = {"CXXABI_1.3", NULL};
  const char *old[] = {"GLIBCXX_3.4", NULL};
  const char *atexit_versions[] = {m->pointer_bytes == 4 ? "GLIBC_2.1.3" : "GLIBC_2.2.5", NULL};
  qa_abi_storage ptr = QA_STORAGE_POINTER;
  qa_abi_storage three_ptrs[] = {ptr, ptr, ptr};
  reg(host, "libc.so.6", "__cxa_atexit", atexit_versions, 2, three_ptrs, 3, false,
      QA_STORAGE_INT32, cxa_atexit_impl);
  reg(host, "libc.so.6", "__cxa_finalize", atexit_versions, 2, &ptr, 1, true,
      QA_STORAGE_INT32, cxa_finalize_impl);
  reg(host, cxx, "__cxa_guard_acquire", cxa, 2, &ptr, 1, false, QA_STORAGE_INT32,
      guard_acquire_impl);
  reg(host, cxx, "__cxa_guard_release", cxa, 2, &ptr, 1, true, QA_STORAGE_INT32,
      guard_release_impl);
  reg(host, cxx, "__cxa_guard_abort", cxa, 2, &ptr, 1, true, QA_STORAGE_INT32, guard_abort_impl);
  const char *new_names[] = {m->pointer_bytes == 4 ? "_Znwj" : "_Znwm",
                             m->pointer_bytes == 4 ? "_Znaj" : "_Znam"};
  for (size_t i = 0; i < 2; i++) {
    cxx_state *state = new_state(host); state->name = xstrdup(new_names[i]);
    qa_system_v_implementation impl = {state, operator_new_impl};
    qa_abi_storage parameter = host->pointer_storage;
    qa_system_v_service(host, cxx, new_names[i], old, 2, &parameter, 1, false,
                        QA_STORAGE_POINTER, impl);
  }
  const char *delete_names[] = {"_ZdlPv", "_ZdaPv"};
  for (size_t i = 0; i < 2; i++) {
    cxx_state *state = new_state(host); state->name = xstrdup(delete_names[i]);
    qa_system_v_implementation impl = {state, operator_delete_impl};
    qa_system_v_service(host, cxx, delete_names[i], old, 2, &ptr, 1, true,
                        QA_STORAGE_INT32, impl);
  }
  /* An exception allocation includes raw Itanium/GNU bookkeeping before the user object. */
  qa_abi_storage size_parameter = host->pointer_storage;
  reg(host, cxx, "__cxa_allocate_exception", cxa, 2, &size_parameter, 1, false,
      QA_STORAGE_POINTER, allocate_exception_impl);
  reg(host, cxx, "__cxa_free_exception", cxa, 2, &ptr, 1, true, QA_STORAGE_INT32,
      free_exception_impl);
}
