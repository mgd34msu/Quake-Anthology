/* Port of src/compat/q3/native/syscall.ts. GPL-2.0-or-later. */
#include "qa/compat/q3/native/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/memory.h"

typedef qa_compat_q3_native_syscalls S;
static const qa_compat_q3_native_syscall_binding *service_of(S *syscalls, const qa_call_value *args, size_t count) {
  int64_t code = qa_guest_integer(args, count, 0);
  for (size_t i = 0; i < syscalls->service_count; i++)
    if (syscalls->services[i].code == code) return &syscalls->services[i];
  qa_range_error("Unbound Q3 native syscall %lld", (long long)code);
}
static qa_call_result syscall_invoke(void *ctx, const qa_guest_core_call_context *context, const qa_call_value *args, size_t count) {
  S *syscalls = ctx;
  const qa_compat_q3_native_syscall_binding *service = service_of(syscalls, args, count);
  double value = service->invoke(service->ctx, context, count ? args + 1 : args, count ? count - 1 : 0);
  return qa_call_int32((int32_t)value);
}
bool qa_compat_q3_native_syscalls_init(S *syscalls, qa_guest_callback_table *callbacks,
  qa_guest_native_abi abi, const qa_compat_q3_native_syscall_binding *bindings, size_t binding_count) {
  memset(syscalls, 0, sizeof *syscalls);
  if (abi.pointer_bytes != 4) qa_range_error("Q3 1.32 syscalls require the i386 source ABI");
  syscalls->services = calloc(binding_count ? binding_count : 1, sizeof *syscalls->services);
  if (!syscalls->services) return false;
  for (size_t i = 0; i < binding_count; i++) {
    for (size_t j = 0; j < syscalls->service_count; j++)
      if (syscalls->services[j].code == bindings[i].code)
        qa_range_error("Duplicate native Q3 syscall %lld", (long long)bindings[i].code);
    syscalls->services[syscalls->service_count++] = bindings[i];
  }
  const char *module_id = callbacks->memory->module.id ? callbacks->memory->module.id : "";
  size_t id_length = strlen("q3-native-syscall:") + strlen(module_id) + 1;
  syscalls->callback_id = malloc(id_length);
  if (!syscalls->callback_id) { free(syscalls->services); return false; }
  snprintf(syscalls->callback_id, id_length, "q3-native-syscall:%s", module_id);
  static const qa_abi_storage fixed[1] = {QA_STORAGE_INT32};
  qa_guest_call_signature signature = qa_compat_q3_native_signature(abi, fixed, 1, true, QA_STORAGE_INT32, true,
    &syscalls->fixed_layout, &syscalls->result_layout);
  syscalls->callback = (qa_guest_host_callback){syscalls->callback_id, signature, syscalls, syscall_invoke};
  syscalls->address = qa_guest_callback_table_bind(callbacks, &syscalls->callback);
  return true;
}
void qa_compat_q3_native_syscalls_destroy(S *syscalls) {
  free(syscalls->services); free(syscalls->callback_id); free(syscalls->layouts);
  memset(syscalls, 0, sizeof *syscalls);
}
bool qa_compat_q3_native_syscalls_variadic_layouts(S *syscalls, const qa_guest_host_callback *callback,
  const qa_call_value *fixed, size_t fixed_count, qa_abi_value_layout **out, size_t *out_count) {
  if (strcmp(callback->id, syscalls->callback_id) != 0) return false;
  const qa_compat_q3_native_syscall_binding *service = service_of(syscalls, fixed, fixed_count);
  qa_abi_value_layout *layouts = calloc(service->parameter_count ? service->parameter_count : 1, sizeof *layouts);
  if (!layouts) qa_range_error("Q3 syscall layout allocation failed");
  for (size_t i = 0; i < service->parameter_count; i++) {
    layouts[i].is_aggregate = false; layouts[i].storage = service->parameters[i];
  }
  *out = layouts; *out_count = service->parameter_count;
  return true;
}
