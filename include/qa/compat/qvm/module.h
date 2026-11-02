/*
 * Port of src/compat/qvm/module.ts. C accommodations:
 *  - The TS constructor builds its interpreter and guest memory
 *    internally; both belong to the parallel slice, so the caller
 *    supplies them through mirrored tables (below) and routes the
 *    interpreter's system call through qa_qvm_module_system_call,
 *    which reproduces the TS current-entry wrapping.
 *  - Async entry points are synchronous (docs/PORTING.md); the
 *    deferred-UI-validation constructor token becomes init_deferred.
 *  - Hook/observer/resolver removals are id + unbind functions.
 * Mirrored types (parallel slice): qa_qvmy_artifact mirrors
 * ResolvedQvmArtifact bytecode + QvmImage extents (TS owners
 * src/compat/qvm/artifacts.ts, image.ts); qa_qvmy_interpreter_runtime
 * and qa_qvmy_function_call mirror QvmInterpreter/QvmFunctionCall and
 * friends (TS owner src/compat/qvm/interpreter.ts);
 * The allocation profile is the landed qa_qvm_allocation_profile
 * (TS owner allocation.ts, allocation.h).
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MODULE_H
#define QA_COMPAT_QVM_MODULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/allocation.h"
#include "qa/compat/qvm/regions.h"
#include "qa/compat/qvm/registry.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/numeric.h"

extern const qa_numeric_profile qa_qvm_numeric_profile;

qa_q3_api_identity qa_qvm_api(qa_qvm_role role, qa_qvm_abi_profile profile);

/* Mirrored resolved bytecode artifact + image extents. */
typedef struct qa_qvmy_artifact {
  qa_module_identity module;
  qa_qvm_role role;
  bool has_abi_profile;
  qa_qvm_abi_profile abi_profile;
  const qa_qvm_instruction *instructions;
  size_t instruction_count;
  const uint8_t *initialized_data;
  size_t initialized_data_length;
  int64_t data_length;
  int64_t literal_length;
  int64_t bss_length;
  int64_t allocated_data_length;
} qa_qvmy_artifact;

/* TS QvmEvaluationStack / QvmReadOnlyEvaluation are the landed
 * qa_qvm_evaluation_stack / qa_qvm_read_only_evaluation
 * (interpreter.h via syscalls.h). */

/* Mirrored QvmBranchBinding decide control + binding. */
typedef struct qa_qvmy_branch_binding {
  int64_t instruction_index;
  void *ctx;
  /* control_cancel aborts through the owning call's cancel. */
  bool (*decide)(void *ctx, bool original_taken, void *call_ctx, qa_qvmy_cancel_fn cancel);
} qa_qvmy_branch_binding;

/* Mirrored QvmRegionBinding (run result keeps TS strings). */
typedef struct qa_qvmy_region_binding {
  int64_t entry;
  int64_t join;
  void *ctx;
  const char *(*run)(void *ctx, void *control_ctx);
  void (*completed)(void *ctx, void *control_ctx); /* optional */
} qa_qvmy_region_binding;

/* Mirrored QvmFunctionCall. words_offset is the TS
 * words.byteOffset - memory.byteOffset within the allocation. */
typedef struct qa_qvmy_function_call {
  int64_t instruction_index;
  bool has_caller_instruction;
  int64_t caller_instruction;
  const char *execution; /* "synchronous" | "asynchronous" */
  const uint8_t *words;
  size_t words_length;
  int64_t words_offset;
  const uint8_t *memory;
  size_t memory_length;
  qa_qvm_memory *guest;
  void *call_ctx;
  qa_qvmy_invoke_fn invoke;
  qa_qvmy_invoke_async_fn invoke_async;
  qa_qvmy_cancel_fn cancel_function;
  qa_qvm_cancellation_scope (*cancellation_scope)(void *ctx);
  void (*branches)(void *ctx, const qa_qvmy_branch_binding *bindings, size_t count);
  void (*regions)(void *ctx, const qa_qvmy_region_binding *bindings, size_t count);
  int32_t (*evaluate_region)(void *ctx, const qa_qvm_region_evaluation *region,
                             const int64_t *inputs, size_t input_count);
  void (*effect)(void *ctx, qa_qvm_perform_fn perform, void *perform_ctx);
  int32_t (*proceed)(void *ctx);
  int32_t (*proceed_async)(void *ctx);
} qa_qvmy_function_call;

/* Mirrored QvmFunctionHook / QvmFunctionObserver / QvmFunctionResolver. */
typedef int32_t (*qa_qvmy_function_hook_fn)(void *ctx, qa_qvmy_function_call *call);
typedef void (*qa_qvmy_function_observer_fn)(void *ctx, qa_qvmy_function_call *call, int64_t argument_index_base);
typedef bool (*qa_qvmy_function_resolver_fn)(void *ctx, int64_t instruction_index, int32_t first_argument,
                                             const uint8_t *words, size_t words_length,
                                             qa_qvmy_function_hook_fn *out_hook, void **out_hook_ctx);

/* Mirrored QvmInterpreter operations used by QvmModule. Binding
 * functions return an id removed through unbind. restart_bytes
 * mirrors parseQvmRestart + interpreter.restart in the owner slice. */
typedef struct qa_qvmy_interpreter_runtime {
  void *ctx;
  qa_qvm_memory *address_space;
  bool (*is_active)(void *ctx);
  int64_t (*stack_pointer)(void *ctx);
  int32_t (*invoke)(void *ctx, const int32_t *args, size_t arg_count, int64_t instruction_index,
                    const qa_qvm_read_only_evaluation *evaluation);
  int32_t (*invoke_async)(void *ctx, const int32_t *args, size_t arg_count, int64_t instruction_index,
                          qa_qvmy_validate_fn validate, void *validate_ctx);
  int32_t (*evaluate_counter)(void *ctx, int64_t address, const int64_t *functions, size_t function_count,
                              int32_t (*run)(void *run_ctx), void *run_ctx,
                              const qa_qvm_evaluation_stack *stack);
  size_t (*bind_function)(void *ctx, int64_t instruction_index, qa_qvmy_function_hook_fn hook, void *hook_ctx);
  size_t (*bind_invocation)(void *ctx, int64_t instruction_index, qa_qvmy_function_hook_fn hook, void *hook_ctx);
  size_t (*bind_function_resolver)(void *ctx, qa_qvmy_function_resolver_fn resolve, void *resolve_ctx);
  size_t (*observe_function)(void *ctx, int64_t instruction_index, qa_qvmy_function_observer_fn observe, void *observe_ctx);
  void (*unbind)(void *ctx, size_t id);
  void (*restart_bytes)(void *ctx, const uint8_t *bytes, size_t length, const char *artifact_path);
  void (*restore_data)(void *ctx, const uint8_t *data, size_t length);
} qa_qvmy_interpreter_runtime;

/* TS QvmHostCheckpoint / QvmHostState. */
typedef struct qa_qvm_host_checkpoint {
  qa_guest_private_state state;
  const qa_random_state *random;
  size_t random_count;
  const qa_saved_guest_callback_binding *callbacks;
  size_t callback_count;
} qa_qvm_host_checkpoint;

typedef struct qa_qvm_host_state {
  void *ctx;
  qa_qvm_host_checkpoint (*checkpoint)(void *ctx);
  void (*restore)(void *ctx, const qa_qvm_host_checkpoint *state);
} qa_qvm_host_state;

/* TS QvmModuleOptions plus the caller-supplied runtime (see header). */
typedef struct qa_qvm_module_options {
  const qa_qvmy_artifact *artifact;
  qa_qvm_host_fn host;
  void *host_ctx;
  const qa_qvm_allocation_profile *allocation; /* TS QvmAllocationProfile */
  const qa_qvm_vm_registration *registration; /* optional */
  const qa_qvm_host_state *host_state;         /* optional */
  qa_qvmy_interpreter_runtime *interpreter;
  const qa_guest_memory *guest_memory;
} qa_qvm_module_options;

/* TS QvmArguments: exactly ten words. */
typedef struct qa_qvm_arguments {
  int32_t words[10];
} qa_qvm_arguments;

qa_qvm_arguments qa_qvm_make_arguments(const int32_t *words, size_t count);

typedef struct qa_qvm_module {
  qa_qvm_module_options options;
  qa_execution_profile profile;
  qa_qvmy_interpreter_runtime *interpreter;
  qa_qvm_memory *memory;
  const qa_guest_memory *guest_memory;
  qa_qvm_system_call source_system_call;
  bool has_current_entry;
  qa_qvmy_syscall current_entry;
  bool has_command_arguments;
  const char *const *command_arguments;
  size_t command_argument_count;
  bool retired;
  void *wrappers; /* opaque hook wrapper registry */
  size_t wrapper_count;
  size_t wrapper_capacity;
} qa_qvm_module;

void qa_qvm_module_destroy(qa_qvm_module *module);

qa_qvm_abi_profile qa_qvm_module_get_abi_profile(const qa_qvm_module *module);
const qa_execution_profile *qa_qvm_module_get_profile(const qa_qvm_module *module);
void qa_qvm_module_init(qa_qvm_module *module, const qa_qvm_module_options *options);
void qa_qvm_module_init_deferred(qa_qvm_module *module, const qa_qvm_module_options *options);
void qa_qvm_module_create(qa_qvm_module *module, const qa_qvm_module_options *options,
                          qa_qvmy_validate_fn validate, void *validate_ctx);
/* The system call the owning interpreter must route traps through. */
int32_t qa_qvm_module_system_call(qa_qvm_module *module, const qa_qvmy_syscall *call);
void qa_qvm_module_cancel_function(qa_qvm_module *module, qa_qvm_cancellation_scope scope);
size_t qa_qvm_module_bind_function(qa_qvm_module *module, const qa_guest_callback_reference *reference,
                                   qa_qvmy_function_hook_fn hook, void *hook_ctx);
size_t qa_qvm_module_bind_invocation(qa_qvm_module *module, const qa_guest_callback_reference *reference,
                                     qa_qvmy_function_hook_fn hook, void *hook_ctx);
size_t qa_qvm_module_bind_function_resolver(qa_qvm_module *module, qa_qvmy_function_resolver_fn resolve, void *resolve_ctx);
size_t qa_qvm_module_observe_function(qa_qvm_module *module, const qa_guest_callback_reference *reference,
                                      qa_qvmy_function_observer_fn observe, void *observe_ctx);
void qa_qvm_module_unbind(qa_qvm_module *module, size_t id);
int32_t qa_qvm_module_call(qa_qvm_module *module, const int32_t *words, size_t count, int64_t instruction_index);
int32_t qa_qvm_module_call_async(qa_qvm_module *module, const int32_t *words, size_t count,
                                 int64_t instruction_index, qa_qvmy_validate_fn validate, void *validate_ctx);
int32_t qa_qvm_module_command(qa_qvm_module *module, const int32_t *words, size_t count,
                              const char *const *arguments, size_t argument_count);
int32_t qa_qvm_module_command_async(qa_qvm_module *module, const int32_t *words, size_t count,
                                    const char *const *arguments, size_t argument_count,
                                    qa_qvmy_validate_fn validate, void *validate_ctx);
int32_t qa_qvm_module_invoke_source_callback(qa_qvm_module *module, qa_qvmy_function_call *call,
                                             int32_t pointer, const int32_t *args, size_t arg_count);
int32_t qa_qvm_module_evaluate_region(qa_qvm_module *module, const int32_t *words, size_t count,
                                      int64_t instruction_index, const qa_qvm_region_evaluation *region,
                                      const int64_t *inputs, size_t input_count,
                                      const qa_qvm_evaluation_stack *stack);
int32_t qa_qvm_module_evaluate_counter(qa_qvm_module *module, const int32_t *words, size_t count,
                                       int64_t instruction_index, int64_t address,
                                       const int64_t *functions, size_t function_count,
                                       const qa_qvm_evaluation_stack *stack);
qa_guest_call_result qa_qvm_module_invoke(qa_qvm_module *module, const qa_guest_call_context *context,
                                          const qa_guest_call_value *arguments, size_t argument_count);
void qa_qvm_module_restart(qa_qvm_module *module, const uint8_t *bytes, size_t length);
/* out->data.bytes and out->host_state.bytes.bytes are malloc'd copies. */
void qa_qvm_module_checkpoint(qa_qvm_module *module, qa_guest_checkpoint *out);
void qa_qvm_module_restore(qa_qvm_module *module, const qa_guest_checkpoint *checkpoint);
void qa_qvm_module_retire(qa_qvm_module *module);

#endif
