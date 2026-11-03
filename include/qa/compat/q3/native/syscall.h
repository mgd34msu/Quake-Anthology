/* Port of src/compat/q3/native/syscall.ts. GPL-2.0-or-later.
 * The variadic `syscall(int arg, ...)` entry dllEntry receives. Like id's
 * VM_DllSyscall it reads a fixed sixteen words from the caller's stack for
 * every trap and hands them to the shared trap table as the same 16
 * little-endian words an interpreted module's trap carries. The TS handler
 * may return a promise (asynchronous guest calls); the C port is synchronous.
 * Each invocation keeps its words in its own frame, as the TS keeps one
 * buffer per nesting level. */
#ifndef QA_COMPAT_Q3_NATIVE_SYSCALL_H
#define QA_COMPAT_Q3_NATIVE_SYSCALL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q3/native/module.h"
#include "qa/guest/core/callbacks.h"

/* TS NATIVE_Q3_SYSCALL_WORDS: the trap number and fifteen arguments. */
#define QA_NATIVE_Q3_SYSCALL_WORDS 16

/* TS NativeQ3SyscallHandler: words is 16 * 4 little-endian bytes; the
 * result is a JS number, completed as `value | 0`. */
typedef double (*qa_compat_q3_native_syscall_handler)(void *ctx, const uint8_t *words, size_t words_length);

typedef struct qa_compat_q3_native_syscalls {
  qa_guest_core_address address;
  qa_compat_q3_native_syscall_handler handler;
  void *handler_ctx;
  qa_guest_host_callback callback;
  char *callback_id; /* owned */
  qa_abi_value_layout fixed_layout, result_layout;
  qa_abi_value_layout layouts[QA_NATIVE_Q3_SYSCALL_WORDS - 1];
  int64_t depth;
} qa_compat_q3_native_syscalls;
bool qa_compat_q3_native_syscalls_init(qa_compat_q3_native_syscalls *syscalls,
  qa_guest_callback_table *callbacks, qa_guest_native_abi abi,
  qa_compat_q3_native_syscall_handler handler, void *handler_ctx);
void qa_compat_q3_native_syscalls_destroy(qa_compat_q3_native_syscalls *syscalls);
/* Runner variadicLayouts resolver shape: malloc'd fifteen int32 layouts
 * (caller frees) or false for a foreign callback (TS null). */
bool qa_compat_q3_native_syscalls_variadic_layouts(qa_compat_q3_native_syscalls *syscalls,
  const qa_guest_host_callback *callback, const qa_call_value *fixed, size_t fixed_count,
  qa_abi_value_layout **out, size_t *out_count);
/* The same, shaped for qa_guest_call_runner_options.variadic_layouts with
 * variadic_ctx = syscalls: a foreign callback resolves to no layouts (TS null ?? []). */
bool qa_compat_q3_native_syscalls_variadic_resolver(void *ctx, const qa_guest_host_callback *callback,
  const qa_call_value *fixed, size_t fixed_count, const qa_guest_core_call_context *context,
  qa_abi_value_layout **out, size_t *out_count);
#endif
