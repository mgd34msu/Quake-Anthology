/*
 * Port of src/guest/x86/cpu.ts: user-mode i386 interpreter (I386Cpu).
 * Faults become GuestExecutionStop exceptions through the contracts.h
 * throw channel installed on the sparse memory for the run; nested
 * runs save/restore it. The numeric hook defaults to
 * qa_execute_numeric_instruction (floating-point/index.ts).
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_X86_CPU_H
#define QA_GUEST_X86_CPU_H

#include "qa/guest/core/contracts.h"
#include "qa/guest/core/memory.h"
#include "qa/guest/floating_point/binary.h" /* qa_numeric_result/context */
#include "qa/guest/x86/decoder.h"

typedef struct qa_i386_cpu {
  qa_guest_processor_state *state;
  qa_sparse_guest_memory *memory;
  bool (*host_call)(void *ctx, qa_guest_core_address address);
  void *host_call_ctx;
  void (*numeric)(void *ctx, qa_numeric_result *out, qa_numeric_context *context);
  void *numeric_ctx;
  /* Borrowed storage behind the last stop's detail/bytes pointers. */
  char stop_detail[512];
  uint8_t stop_bytes[15];
} qa_i386_cpu;

/* Aborts with the TS RangeError when state/memory widths mismatch. */
void qa_i386_cpu_init(qa_i386_cpu *cpu, qa_guest_processor_state *state, qa_sparse_guest_memory *memory,
                      bool (*host_call)(void *ctx, qa_guest_core_address address), void *host_call_ctx,
                      void (*numeric)(void *ctx, qa_numeric_result *out, qa_numeric_context *context), void *numeric_ctx);
qa_guest_execution_stop qa_i386_cpu_run(qa_i386_cpu *cpu, int64_t instruction_budget,
                                        bool has_return_address, qa_guest_core_address return_address);

#endif
