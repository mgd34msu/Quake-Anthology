/*
 * Port of src/compat/qvm/syscalls.ts. C accommodations:
 *  - QvmSystemCallResult (number | Promise<number>) becomes a plain
 *    synchronous int32, per docs/PORTING.md async handling.
 *  - QvmUnboundSyscallError has no stack to unwind; rejecting prints
 *    its exact TS message and aborts like other thrown errors.
 *  - createQvmSystemCall's returned closure becomes an init'd
 *    qa_qvm_system_call struct passed to qa_qvm_system_call_invoke.
 * Mirrored types (parallel slice): qa_qvmy_syscall mirrors QvmSyscall
 * (TS owner src/compat/qvm/interpreter.ts; the landed qa_qvm_syscall
 * carries that slice's own memory/view mirrors, so the two structs
 * stay separate until the slices are unified). The import decoders
 * are the landed qa_qvm_decode_* functions (qa/compat/qvm/abi.h and
 * legacy_bot_abi.h).
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_SYSCALLS_H
#define QA_COMPAT_QVM_SYSCALLS_H

#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

#include "qa/compat/qvm/interpreter.h"
#include "qa/compat/qvm/memory.h"
#include "qa/contracts/execution.h"

/* TS QvmRole: "qagame" | "cgame" | "ui" (exact strings). */
typedef const char *qa_qvm_role;

/* TS QvmHostResult. */
typedef int32_t qa_qvm_host_result;

/* TS QvmCancellationScope / QvmReadOnlyEvaluation are the landed
 * qa_qvm_cancellation_scope / qa_qvm_read_only_evaluation
 * (interpreter.h, TS owner src/compat/qvm/interpreter.ts). */

typedef int32_t (*qa_qvmy_invoke_fn)(void *ctx, const int32_t *args, size_t arg_count,
                                     int64_t instruction_index,
                                     const qa_qvm_read_only_evaluation *evaluation);
typedef void (*qa_qvmy_validate_fn)(void *ctx);
typedef int32_t (*qa_qvmy_invoke_async_fn)(void *ctx, const int32_t *args, size_t arg_count,
                                           int64_t instruction_index,
                                           qa_qvmy_validate_fn validate, void *validate_ctx);
/* The TS cancelFunction never returns; noreturn cannot ride a
 * function-pointer typedef portably, so implementations abort. */
typedef void (*qa_qvmy_cancel_fn)(void *ctx, qa_qvm_cancellation_scope scope);

/* Mirrored QvmSyscall. `words` are live little-endian syscall words;
 * `memory` is the raw allocation the guest view must borrow. */
typedef struct qa_qvmy_syscall {
  const uint8_t *words;
  size_t words_length;
  const uint8_t *memory;
  size_t memory_length;
  qa_qvm_memory *guest;
  void *call_ctx;
  qa_qvmy_invoke_fn invoke;
  qa_qvmy_invoke_async_fn invoke_async;
  qa_qvmy_cancel_fn cancel_function;
} qa_qvmy_syscall;

/* TS QvmHostCall: kind "engine" | "extension" (exact strings); code is
 * the decoded import value (engine) or raw word (extension).
 * command_arguments NULL is TS null. */
typedef struct qa_qvm_host_call {
  qa_qvmy_syscall base;
  qa_qvm_memory *guest;
  bool has_abi_profile;
  qa_qvm_abi_profile abi_profile;
  const char *const *command_arguments;
  size_t command_argument_count;
  bool has_command_arguments;
  const char *kind;
  qa_qvm_role role;
  int32_t code;
} qa_qvm_host_call;

#include <stdbool.h>

/* TS QvmHost. */
typedef qa_qvm_host_result (*qa_qvm_host_fn)(void *ctx, const qa_qvm_host_call *call);
typedef void (*qa_qvm_command_arguments_fn)(void *ctx, const char *const **out, size_t *out_count, bool *out_has);

noreturn void qa_qvm_reject_syscall(const qa_qvm_host_call *call);
/* Default QvmHost adapter for qa_qvm_reject_syscall. */
qa_qvm_host_result qa_qvm_reject_syscall_host(void *ctx, const qa_qvm_host_call *call);

/* TS QvmSystemCall closure state. */
typedef struct qa_qvm_system_call {
  qa_qvm_role role;
  qa_qvm_host_fn host; /* NULL rejects */
  void *host_ctx;
  qa_qvm_command_arguments_fn command_arguments; /* NULL gives null */
  void *command_arguments_ctx;
  qa_qvm_abi_profile abi_profile;
} qa_qvm_system_call;

void qa_qvm_create_system_call(qa_qvm_system_call *out, qa_qvm_role role, qa_qvm_host_fn host,
                               void *host_ctx, qa_qvm_command_arguments_fn command_arguments,
                               void *command_arguments_ctx, qa_qvm_abi_profile abi_profile);
qa_qvm_host_result qa_qvm_system_call_invoke(const qa_qvm_system_call *system_call, const qa_qvmy_syscall *call);

/* Word access over a host call's words, DataView bounds semantics. */
int32_t qa_qvm_host_call_word(const qa_qvm_host_call *call, size_t index);
float qa_qvm_host_call_word_f32(const qa_qvm_host_call *call, size_t offset);

#endif
