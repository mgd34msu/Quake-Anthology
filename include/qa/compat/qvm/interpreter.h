/*
 * Port of src/compat/qvm/interpreter.ts. GPL-2.0-or-later.
 * C accommodations:
 *  - Promise<number> results collapse to synchronous double returns
 *    (invokeAsync/proceedAsync run the synchronous path; the TS async
 *    interleavings have no C counterpart, see docs/PORTING.md).
 *  - qa_qvmx_* types/functions mirror qvm-b-owned TS files
 *    (memory.ts, syscalls.ts, operations.ts, regions.ts) with a unique
 *    prefix; symbols.ts/registry.ts use their landed headers.
 */
#ifndef QA_COMPAT_QVM_INTERPRETER_H
#define QA_COMPAT_QVM_INTERPRETER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

#include "qa/compat/qvm/allocation.h"
#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/regions.h"
#include "qa/compat/qvm/registry.h"
#include "qa/compat/qvm/symbols.h"
#include "qa/contracts/execution.h"
#include "qa/foundation/support.h"

/* TS QvmArguments: a 10-tuple of numbers. */
typedef struct qa_qvm_interpreter_arguments { double values[10]; } qa_qvm_interpreter_arguments;

/* TS QvmCancellationScope: an opaque capability issued by an interpreter. */
typedef struct qa_qvm_cancellation_scope { const void *token; } qa_qvm_cancellation_scope;

typedef struct qa_qvm_evaluation_stack { double start, end; } qa_qvm_evaluation_stack;

/* TS QvmRegionEvaluation is the landed qa_qvm_region_evaluation (regions.h). */
typedef qa_qvm_region_evaluation qa_qvmx_region_evaluation;

typedef struct qa_qvm_read_only_evaluation {
  bool has_stack;
  qa_qvm_evaluation_stack stack;
  qa_qvmx_region_evaluation region;
  const double *inputs;
  size_t input_count;
} qa_qvm_read_only_evaluation;

/* ---- Mirrors of src/compat/qvm/memory.ts (qvm-b slice) ---- */
/* A live DataView over guest bytes. Accessors abort like DataView OOB. */
typedef struct qa_qvmx_view {
  uint8_t *bytes; /* borrowed start of the view */
  size_t length;
} qa_qvmx_view;

static inline void qa_qvmx_view_check(const qa_qvmx_view *view, int64_t offset, int64_t width) {
  if (offset < 0 || width < 0 || offset > (int64_t)view->length - width)
    qa_range_error("Offset is outside the bounds of the DataView");
}
static inline int32_t qa_qvmx_view_i32(const qa_qvmx_view *view, int64_t offset) {
  qa_qvmx_view_check(view, offset, 4);
  const uint8_t *p = view->bytes + offset;
  uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  int32_t out;
  __builtin_memcpy(&out, &v, sizeof out);
  return out;
}
static inline void qa_qvmx_view_set_i32(qa_qvmx_view *view, int64_t offset, int32_t value) {
  qa_qvmx_view_check(view, offset, 4);
  uint8_t *p = view->bytes + offset;
  uint32_t v;
  __builtin_memcpy(&v, &value, sizeof v);
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline float qa_qvmx_view_f32(const qa_qvmx_view *view, int64_t offset) {
  int32_t bits = qa_qvmx_view_i32(view, offset);
  float out;
  __builtin_memcpy(&out, &bits, sizeof out);
  return out;
}
static inline void qa_qvmx_view_set_f32(qa_qvmx_view *view, int64_t offset, float value) {
  int32_t bits;
  __builtin_memcpy(&bits, &value, sizeof bits);
  qa_qvmx_view_set_i32(view, offset, bits);
}
static inline uint8_t qa_qvmx_view_u8(const qa_qvmx_view *view, int64_t offset) {
  qa_qvmx_view_check(view, offset, 1);
  return view->bytes[offset];
}
static inline void qa_qvmx_view_set_u8(qa_qvmx_view *view, int64_t offset, uint8_t value) {
  qa_qvmx_view_check(view, offset, 1);
  view->bytes[offset] = value;
}
static inline uint16_t qa_qvmx_view_u16(const qa_qvmx_view *view, int64_t offset) {
  qa_qvmx_view_check(view, offset, 2);
  const uint8_t *p = view->bytes + offset;
  return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}
static inline void qa_qvmx_view_set_u16(qa_qvmx_view *view, int64_t offset, uint16_t value) {
  qa_qvmx_view_check(view, offset, 2);
  view->bytes[offset] = (uint8_t)value;
  view->bytes[offset + 1] = (uint8_t)(value >> 8);
}

typedef struct qa_qvmx_memory {
  uint8_t *bytes; /* borrowed allocation */
  size_t length;
} qa_qvmx_memory;

/* Pointer word -> byte offset. *is_null for word 0. Aborts like memory.ts. */
size_t qa_qvmx_memory_pointer_offset(const qa_qvmx_memory *memory, double word, bool *is_null);
qa_qvmx_view qa_qvmx_memory_span(const qa_qvmx_memory *memory, double word, double length,
                                 double relative_offset);
qa_qvmx_view qa_qvmx_memory_view(const qa_qvmx_memory *memory, double word, double length,
                                 double relative_offset);
qa_qvmx_view qa_qvmx_memory_data_view(const qa_qvmx_memory *memory, double byte_offset,
                                      double byte_length);
/* Malloc'd NUL-terminated copy; NULL only on OOM. */
char *qa_qvmx_memory_read_string(const qa_qvmx_memory *memory, double word);
void qa_qvmx_memory_write_string(qa_qvmx_memory *memory, double word, const char *text,
                                 double capacity);
void qa_qvmx_memory_write_bounded_string(qa_qvmx_memory *memory, double word, const char *text,
                                         double capacity);
void qa_qvmx_memory_write_bytes(qa_qvmx_memory *memory, double byte_offset,
                                const uint8_t *bytes, size_t length);
void qa_qvmx_memory_fill_bytes(qa_qvmx_memory *memory, double byte_offset, double byte_length,
                               double value);
void qa_qvmx_memory_copy_bytes(qa_qvmx_memory *memory, double destination, double source,
                               double byte_length);

/* Mirrors of src/compat/qvm/operations.ts (qvm-b slice). */
int32_t qa_qvmx_evaluate_unary(qa_qvm_opcode opcode, int32_t word);
int32_t qa_qvmx_evaluate_binary(qa_qvm_opcode opcode, int32_t left, int32_t right);
bool qa_qvmx_evaluate_branch(qa_qvm_opcode opcode, int32_t left, int32_t right);

/* TS QvmRole is the landed qa_qvm_role (syscalls.h). */

/* ---- interpreter.ts proper ---- */
typedef struct qa_qvm_syscall qa_qvm_syscall;
typedef double qa_qvm_interpreter_system_call_result;

struct qa_qvm_syscall {
  qa_qvmx_view words;
  uint8_t *memory; /* borrowed */
  size_t memory_length;
  qa_qvmx_memory *guest;
  void *ctx;
  double (*invoke)(void *ctx, const double *args, size_t arg_count, bool has_index,
                   double instruction_index, const qa_qvm_read_only_evaluation *evaluation);
  double (*invoke_async)(void *ctx, const double *args, size_t arg_count, bool has_index,
                         double instruction_index);
  void (*cancel_function)(void *ctx, qa_qvm_cancellation_scope scope);
};

typedef struct qa_qvm_interpreter_system_call {
  void *ctx;
  qa_qvm_interpreter_system_call_result (*call)(void *ctx, qa_qvm_syscall *call);
} qa_qvm_interpreter_system_call;

typedef struct qa_qvm_branch_binding {
  double instruction_index;
  void *ctx;
  bool (*decide)(void *ctx, bool original_taken, qa_qvm_syscall *control);
} qa_qvm_branch_binding;

typedef struct qa_qvm_region_binding qa_qvm_region_binding;
typedef struct qa_qvm_function_call qa_qvm_function_call;

typedef struct qa_qvm_region_control {
  void *ctx;
  double (*invoke)(void *ctx, const double *args, size_t arg_count, bool has_index,
                   double instruction_index, const qa_qvm_read_only_evaluation *evaluation);
  void (*cancel_function)(void *ctx, qa_qvm_cancellation_scope scope);
  double (*local_word)(void *ctx, double offset);
} qa_qvm_region_control;

struct qa_qvm_region_binding {
  double entry, join;
  void *ctx;
  const char *(*run)(void *ctx, qa_qvm_region_control *control); /* "execute" | "skip" */
  void (*completed)(void *ctx, qa_qvm_region_control *control);  /* optional */
};

struct qa_qvm_function_call {
  double instruction_index;
  bool has_caller_instruction;
  double caller_instruction;
  const char *execution; /* "synchronous" | "asynchronous" */
  qa_qvmx_view words;
  uint8_t *memory;
  size_t memory_length;
  qa_qvmx_memory *guest;
  void *ctx;
  double (*invoke)(void *ctx, const double *args, size_t arg_count, bool has_index,
                   double instruction_index, const qa_qvm_read_only_evaluation *evaluation);
  double (*invoke_async)(void *ctx, const double *args, size_t arg_count, bool has_index,
                         double instruction_index);
  void (*cancel_function)(void *ctx, qa_qvm_cancellation_scope scope);
  qa_qvm_cancellation_scope (*cancellation_scope)(void *ctx);
  void (*branches)(void *ctx, const qa_qvm_branch_binding *bindings, size_t count);
  void (*regions)(void *ctx, const qa_qvm_region_binding *bindings, size_t count);
  double (*evaluate_region)(void *ctx, qa_qvmx_region_evaluation region,
                            const double *inputs, size_t input_count);
  void (*effect)(void *ctx, void (*perform)(void *perform_ctx), void *perform_ctx);
  double (*proceed)(void *ctx);
  double (*proceed_async)(void *ctx);
};

typedef struct qa_qvm_function_hook {
  void *ctx;
  qa_qvm_interpreter_system_call_result (*call)(void *ctx, qa_qvm_function_call *call);
} qa_qvm_function_hook;

typedef struct qa_qvm_function_resolver {
  void *ctx;
  bool (*resolve)(void *ctx, double instruction_index, double first_argument,
                  const qa_qvmx_view *words, qa_qvm_function_hook *out_hook);
} qa_qvm_function_resolver;

typedef struct qa_qvm_function_observation {
  double instruction_index;
  void *ctx;
  double (*invoke)(void *ctx, const double *args, size_t arg_count, bool has_index,
                   double instruction_index, const qa_qvm_read_only_evaluation *evaluation);
  double (*invoke_async)(void *ctx, const double *args, size_t arg_count, bool has_index,
                         double instruction_index);
  void (*cancel_function)(void *ctx, qa_qvm_cancellation_scope scope);
  double (*argument)(void *ctx, double index);
} qa_qvm_function_observation;

typedef struct qa_qvm_function_observer {
  void *ctx;
  void (*observe)(void *ctx, qa_qvm_function_observation *call);
} qa_qvm_function_observer;

typedef const char *qa_qvm_semantics; /* "interpreted" | "compiled" */

/* TS QvmSymbolLoadOptions is the landed qa_qvm_symbol_load_options (symbols.h). */

/* TS VmRegistration is the landed qa_qvm_vm_registration (registry.h);
 * create() takes it by pointer, NULL for the TS null default. */

typedef struct qa_qvm_interpreter qa_qvm_interpreter;

typedef struct qa_qvm_store_effect {
  void *ctx;
  void (*call)(void *ctx, qa_qvm_syscall *scope, void (*perform)(void *perform_ctx),
               void *perform_ctx);
} qa_qvm_store_effect;

/* NULL only on OOM. `profile`/`registration`/`semantics`/`store_effect` may be NULL
 * for the TS defaults ({kind:"unaccounted"}, null, "interpreted", identity). */
qa_qvm_interpreter *qa_qvm_interpreter_create(const qa_qvm_image *image,
                                              qa_qvm_interpreter_system_call system_call,
                                              const qa_qvm_allocation_profile *profile,
                                              const qa_qvm_vm_registration *registration,
                                              qa_qvm_semantics semantics,
                                              const qa_qvm_store_effect *store_effect);
void qa_qvm_interpreter_destroy(qa_qvm_interpreter *interpreter);

/* Borrowed views into interpreter-owned storage. */
uint8_t *qa_qvm_interpreter_get_memory(const qa_qvm_interpreter *interpreter, size_t *out_length);
qa_qvmx_memory *qa_qvm_interpreter_get_address_space(qa_qvm_interpreter *interpreter);
double qa_qvm_interpreter_get_call_level(const qa_qvm_interpreter *interpreter);
bool qa_qvm_interpreter_get_is_active(const qa_qvm_interpreter *interpreter);
double qa_qvm_interpreter_get_stack_pointer(const qa_qvm_interpreter *interpreter);
double qa_qvm_interpreter_get_break_count(const qa_qvm_interpreter *interpreter);
bool qa_qvm_interpreter_get_debug_enabled(const qa_qvm_interpreter *interpreter);
double qa_qvm_interpreter_get_code_length(qa_qvm_interpreter *interpreter);
double qa_qvm_interpreter_get_instruction_pointers_length(qa_qvm_interpreter *interpreter);

/* Bindings return an unbind closure as fn+ctx. */
typedef struct qa_qvm_unbind {
  void *ctx;
  void (*call)(void *ctx);
} qa_qvm_unbind;
qa_qvm_unbind qa_qvm_interpreter_bind_function(qa_qvm_interpreter *interpreter,
                                               double instruction_index, qa_qvm_function_hook hook);
qa_qvm_unbind qa_qvm_interpreter_bind_invocation(qa_qvm_interpreter *interpreter,
                                                 double instruction_index, qa_qvm_function_hook hook);
qa_qvm_unbind qa_qvm_interpreter_bind_function_resolver(qa_qvm_interpreter *interpreter,
                                                        qa_qvm_function_resolver resolver);
qa_qvm_unbind qa_qvm_interpreter_observe_function(qa_qvm_interpreter *interpreter,
                                                  double instruction_index,
                                                  qa_qvm_function_observer observer);
void qa_qvm_interpreter_restore_data(qa_qvm_interpreter *interpreter, const uint8_t *data,
                                     size_t length);
/* Malloc'd strings; caller frees, NULL on OOM. */
char *qa_qvm_interpreter_indent(const qa_qvm_interpreter *interpreter);
char *qa_qvm_interpreter_stack_trace(qa_qvm_interpreter *interpreter, double program_counter,
                                     double program_stack);
void qa_qvm_interpreter_load_symbols(qa_qvm_interpreter *interpreter,
                                     const qa_qvm_symbol_load_options *options);
/* TS readonly symbols: borrowed pointer to interpreter-owned storage. */
qa_qvm_symbols *qa_qvm_interpreter_get_symbols(qa_qvm_interpreter *interpreter);
/* TS pointer(word): Uint8Array|null -> view with bytes NULL for the null word. */
qa_qvmx_view qa_qvm_interpreter_pointer(qa_qvm_interpreter *interpreter, double word);
void qa_qvm_interpreter_restart(qa_qvm_interpreter *interpreter, const qa_qvm_data_image *image);
double qa_qvm_interpreter_invoke(qa_qvm_interpreter *interpreter, const double *args,
                                 size_t arg_count, double instruction_index,
                                 const qa_qvm_read_only_evaluation *evaluation);
double qa_qvm_interpreter_invoke_async(qa_qvm_interpreter *interpreter, const double *args,
                                       size_t arg_count, double instruction_index);
double qa_qvm_interpreter_evaluate_counter(qa_qvm_interpreter *interpreter, double address,
                                           double initial, const double *functions,
                                           size_t function_count,
                                           double (*execute)(void *ctx), void *execute_ctx,
                                           const qa_qvm_evaluation_stack *stack);

#endif
