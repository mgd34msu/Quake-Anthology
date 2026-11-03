/*
 * Port of src/guest/abi/runner.ts: GuestCallRunner with nested calls,
 * host-callback dispatch, inline regions and sliced loading calls.
 * GuestCallbackTable is the concrete table from core/callbacks.h.
 * C accommodations: generators become a loop with a yield hook;
 * invokeLoading's `await nextFrame()` becomes a synchronous callback run
 * between 16384-instruction slices; GuestCallStopped (plain Error) aborts
 * with the same message after stashing the stop for
 * qa_guest_call_runner_last_stop(). GPL-2.0-or-later.
 */
#ifndef QA_GUEST_ABI_RUNNER_H
#define QA_GUEST_ABI_RUNNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/guest/abi/adapter.h"
#include "qa/guest/core/callbacks.h" /* concrete qa_guest_callback_table */

/* AbiProcessorSnapshot. */
typedef struct qa_abi_processor_snapshot {
  qa_guest_architecture architecture;
  uint8_t registers[128];
  size_t registers_len;
  uint64_t instruction_pointer;
  uint64_t flags;
  qa_guest_segment segments[QA_SEG_COUNT];
  qa_guest_x87_state x87;
  qa_guest_simd_state simd;
} qa_abi_processor_snapshot;
void qa_capture_abi_processor_state(qa_abi_processor_snapshot *out, qa_guest_processor_state *state);
void qa_restore_abi_processor_state(qa_guest_processor_state *state, const qa_abi_processor_snapshot *snapshot);

typedef struct qa_guest_call_request {
  qa_guest_core_address target;
  qa_guest_call_signature signature;
  const qa_call_value *arguments;
  size_t argument_count;
  qa_guest_core_call_context context;
  int64_t instruction_budget;
} qa_guest_call_request;

typedef struct qa_guest_call_runner qa_guest_call_runner;

/* variadicLayouts: fills a malloc'd layouts array (runner frees); may be NULL. */
typedef struct qa_guest_call_runner_options {
  qa_guest_cpu *cpu;
  qa_guest_callback_table *callbacks;
  qa_guest_core_address return_address;
  void *variadic_ctx;
  bool (*variadic_layouts)(void *ctx, const qa_guest_host_callback *callback,
                           const qa_call_value *fixed_arguments, size_t fixed_count,
                           const qa_guest_core_call_context *context,
                           qa_abi_value_layout **out, size_t *out_count);
} qa_guest_call_runner_options;

typedef struct qa_guest_inline_continuation {
  void *ctx;
  void (*execute)(void *ctx);
  void (*skip)(void *ctx);
} qa_guest_inline_continuation;

/* Opaque unbind handle returned by qa_guest_call_runner_bind_inline_region. */
typedef struct qa_guest_inline_region_handle qa_guest_inline_region_handle;

/* Returns false only on OOM. */
bool qa_guest_call_runner_init(qa_guest_call_runner *runner, qa_guest_call_runner_options options);
void qa_guest_call_runner_destroy(qa_guest_call_runner *runner);
qa_guest_call_runner_options qa_guest_call_runner_options_of(const qa_guest_call_runner *runner);
int64_t qa_guest_call_runner_depth(const qa_guest_call_runner *runner);
const qa_guest_core_call_context *qa_guest_call_runner_current_context(const qa_guest_call_runner *runner);
double qa_guest_call_runner_maximum_loading_slice_ms(const qa_guest_call_runner *runner);
uint64_t qa_guest_call_runner_instructions_executed(const qa_guest_call_runner *runner);
const qa_guest_execution_stop *qa_guest_call_runner_last_stop(const qa_guest_call_runner *runner);

qa_call_result qa_guest_call_runner_invoke(qa_guest_call_runner *runner, const qa_guest_call_request *request);
/* next_frame runs synchronously between slices (TS await). */
qa_call_result qa_guest_call_runner_invoke_loading(qa_guest_call_runner *runner,
                                                   const qa_guest_call_request *request,
                                                   void (*next_frame)(void *ctx), void *next_frame_ctx);
/* accepts may be NULL (always true). Returned handle: unbind then free. */
qa_guest_inline_region_handle *qa_guest_call_runner_bind_inline_region(
    qa_guest_call_runner *runner, qa_guest_core_address entry, qa_guest_core_address join,
    qa_guest_native_abi abi,
    void (*intercept)(void *ctx, qa_guest_inline_continuation *continuation), void *intercept_ctx,
    bool (*accepts)(void *ctx), void *accepts_ctx);
void qa_guest_inline_region_unbind(qa_guest_inline_region_handle *handle);
void qa_guest_inline_region_handle_free(qa_guest_inline_region_handle *handle);
/* Storage size for callers that allocate the opaque runner. */
size_t qa_guest_call_runner_size(void);
/* Forget calls deeper than depth after a host-side unwind (the TS
 * runner's try/finally for a throw that crossed guest frames). */
void qa_guest_call_runner_unwind(qa_guest_call_runner *runner, int64_t depth);
/* TS recover(): after a stopped outermost call, returns the processor to its
 * state at entry so shutdown does not run on the faulted stack. */
void qa_guest_call_runner_recover(qa_guest_call_runner *runner);
/* TS chargeBytes: one budget unit per 64 bytes from every active frame. */
void qa_guest_call_runner_charge_bytes(qa_guest_call_runner *runner, size_t byte_length);

#endif
