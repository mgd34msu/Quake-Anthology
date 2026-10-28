/* Port of src/compat/q3/native/syscall.ts. GPL-2.0-or-later.
 * Binding invoke returns a double (TS number) truncated to the
 * int32 syscall result by the runner encoding, matching TS. */
#ifndef QA_COMPAT_Q3_NATIVE_SYSCALL_H
#define QA_COMPAT_Q3_NATIVE_SYSCALL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q3/native/module.h"
#include "qa/guest/core/callbacks.h"

typedef struct qa_compat_q3_native_syscall_binding {
  int64_t code;
  const char *name;
  const qa_abi_storage *parameters; size_t parameter_count;
  void *ctx;
  double (*invoke)(void *ctx, const qa_guest_core_call_context *context,
    const qa_call_value *arguments, size_t argument_count);
} qa_compat_q3_native_syscall_binding;

typedef struct qa_compat_q3_native_syscalls {
  qa_guest_core_address address;
  qa_compat_q3_native_syscall_binding *services; size_t service_count; /* owned copy */
  qa_guest_host_callback callback;
  char *callback_id; /* owned */
  qa_abi_value_layout fixed_layout, result_layout;
  qa_abi_value_layout *layouts; size_t layout_count, layout_capacity; /* variadic scratch, owned */
} qa_compat_q3_native_syscalls;
bool qa_compat_q3_native_syscalls_init(qa_compat_q3_native_syscalls *syscalls,
  qa_guest_callback_table *callbacks, qa_guest_native_abi abi,
  const qa_compat_q3_native_syscall_binding *bindings, size_t binding_count);
void qa_compat_q3_native_syscalls_destroy(qa_compat_q3_native_syscalls *syscalls);
/* Runner variadicLayouts resolver shape: malloc'd layouts (caller
 * frees) or false for a foreign callback (TS null). */
bool qa_compat_q3_native_syscalls_variadic_layouts(qa_compat_q3_native_syscalls *syscalls,
  const qa_guest_host_callback *callback, const qa_call_value *fixed, size_t fixed_count,
  qa_abi_value_layout **out, size_t *out_count);
#endif
