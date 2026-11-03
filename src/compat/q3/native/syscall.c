/* Port of src/compat/q3/native/syscall.ts. GPL-2.0-or-later. */
#include "qa/compat/q3/native/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/memory.h"

typedef qa_compat_q3_native_syscalls S;
static void depth_restore(void *arg) { ((S *)arg)->depth--; }
static qa_call_result syscall_invoke(void *ctx, const qa_guest_core_call_context *context, const qa_call_value *args, size_t count) {
  (void)context;
  S *syscalls = ctx;
  uint8_t words[QA_NATIVE_Q3_SYSCALL_WORDS * 4];
  syscalls->depth++;
  qa_cleanup scope;
  qa_cleanup_push(&scope, depth_restore, syscalls);
  for (size_t index = 0; index < QA_NATIVE_Q3_SYSCALL_WORDS; index++) {
    /* Number(BigInt.asIntN(32, integer(args, index))) */
    const uint32_t word = (uint32_t)qa_guest_integer_u64(args, count, index);
    words[index * 4] = (uint8_t)word;
    words[index * 4 + 1] = (uint8_t)(word >> 8);
    words[index * 4 + 2] = (uint8_t)(word >> 16);
    words[index * 4 + 3] = (uint8_t)(word >> 24);
  }
  const double result = syscalls->handler(syscalls->handler_ctx, words, sizeof words);
  qa_cleanup_pop(&scope);
  syscalls->depth--;
  return qa_call_int32(qa_js_to_int32(result));
}
bool qa_compat_q3_native_syscalls_init(S *syscalls, qa_guest_callback_table *callbacks,
  qa_guest_native_abi abi, qa_compat_q3_native_syscall_handler handler, void *handler_ctx) {
  memset(syscalls, 0, sizeof *syscalls);
  if (abi.pointer_bytes != 4) qa_range_error("Q3 1.32 syscalls require the i386 source ABI");
  syscalls->handler = handler;
  syscalls->handler_ctx = handler_ctx;
  for (size_t i = 0; i < QA_NATIVE_Q3_SYSCALL_WORDS - 1; i++) {
    syscalls->layouts[i].is_aggregate = false;
    syscalls->layouts[i].storage = QA_STORAGE_INT32;
  }
  const char *module_id = callbacks->memory->module.id ? callbacks->memory->module.id : "";
  size_t id_length = strlen("q3-native-syscall:") + strlen(module_id) + 1;
  syscalls->callback_id = malloc(id_length);
  if (!syscalls->callback_id) return false;
  snprintf(syscalls->callback_id, id_length, "q3-native-syscall:%s", module_id);
  static const qa_abi_storage fixed[1] = {QA_STORAGE_INT32};
  qa_guest_call_signature signature = qa_compat_q3_native_signature(abi, fixed, 1, true, QA_STORAGE_INT32, true,
    &syscalls->fixed_layout, &syscalls->result_layout);
  syscalls->callback = (qa_guest_host_callback){syscalls->callback_id, signature, syscalls, syscall_invoke};
  syscalls->address = qa_guest_callback_table_bind(callbacks, &syscalls->callback);
  return true;
}
void qa_compat_q3_native_syscalls_destroy(S *syscalls) {
  free(syscalls->callback_id);
  memset(syscalls, 0, sizeof *syscalls);
}
bool qa_compat_q3_native_syscalls_variadic_resolver(void *ctx, const qa_guest_host_callback *callback,
  const qa_call_value *fixed, size_t fixed_count, const qa_guest_core_call_context *context,
  qa_abi_value_layout **out, size_t *out_count) {
  (void)context; /* TS variadicLayouts reads only the callback. */
  *out = NULL; *out_count = 0;
  qa_compat_q3_native_syscalls_variadic_layouts(ctx, callback, fixed, fixed_count, out, out_count);
  return true;
}
bool qa_compat_q3_native_syscalls_variadic_layouts(S *syscalls, const qa_guest_host_callback *callback,
  const qa_call_value *fixed, size_t fixed_count, qa_abi_value_layout **out, size_t *out_count) {
  (void)fixed; (void)fixed_count;
  if (strcmp(callback->id, syscalls->callback_id) != 0) return false;
  qa_abi_value_layout *layouts = malloc(sizeof syscalls->layouts);
  if (!layouts) qa_range_error("Q3 syscall layout allocation failed");
  memcpy(layouts, syscalls->layouts, sizeof syscalls->layouts);
  *out = layouts; *out_count = QA_NATIVE_Q3_SYSCALL_WORDS - 1;
  return true;
}
