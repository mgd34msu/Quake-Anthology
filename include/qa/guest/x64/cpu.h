/*
 * Port of src/guest/x64/cpu.ts: X64Cpu user-mode interpreter.
 * C accommodations: the TS decoded-instruction cache, semantic
 * blocks and managed integer-block fast path are dropped (they are
 * performance structure; the integer kernel remains available as
 * its own module). Every instruction decodes fresh, which preserves
 * the observable live-bytes/entry-hook/rollback semantics the TS
 * tests pin. Plan execution is mirrored locally over the throw
 * channel so LOCK/numeric faults become exception stops exactly as
 * in TS (plan.c's qa_execute_x64_plan aborts for those by design).
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_X64_CPU_H
#define QA_GUEST_X64_CPU_H

#include "qa/guest/core/callbacks.h"
#include "qa/guest/core/contracts.h"
#include "qa/guest/core/memory.h"
#include "qa/guest/x64/decoder.h"

typedef struct qa_x64_cpu {
  qa_guest_processor_state *state;
  qa_sparse_guest_memory *memory;
  bool (*is_host_call)(void *ctx, qa_guest_address address);
  void *is_host_call_ctx;
  qa_guest_callback_table *callbacks; /* nullable */
  /* Borrowed storage behind the last stop's detail/bytes/mnemonic pointers. */
  char stop_detail[512];
  char stop_mnemonic[32];
  uint8_t stop_bytes[15];
} qa_x64_cpu;

/* callbacks NULL and is_host_call NULL: no host stops. Aborts with
 * the TS RangeErrors on mismatched state/memory/callback ownership. */
void qa_x64_cpu_init(qa_x64_cpu *cpu, qa_guest_processor_state *state, qa_sparse_guest_memory *memory,
                     bool (*is_host_call)(void *ctx, qa_guest_address address), void *is_host_call_ctx,
                     qa_guest_callback_table *callbacks);
qa_guest_execution_stop qa_x64_cpu_run(qa_x64_cpu *cpu, int64_t instruction_budget,
                                       bool has_return_address, qa_guest_address return_address);

#endif
