/*
 * Port of src/guest/x64/cpu.ts: X64Cpu user-mode interpreter.
 * The decoded-instruction cache, semantic blocks and managed integer
 * blocks follow TS #runGeneric/#runManaged. Plan execution is mirrored
 * locally over the throw channel so LOCK/numeric faults become exception
 * stops exactly as in TS (plan.c's qa_execute_x64_plan aborts for those by
 * design); the integer kernel routes its passthrough plans to that mirror.
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_X64_CPU_H
#define QA_GUEST_X64_CPU_H

#include "qa/guest/core/callbacks.h"
#include "qa/guest/core/contracts.h"
#include "qa/guest/core/memory.h"
#include "qa/guest/x64/decoder.h"
#include "qa/guest/x64/integer_kernel.h"

typedef struct qa_x64_instruction_cache qa_x64_instruction_cache;

typedef struct qa_x64_cpu {
  qa_guest_processor_state *state;
  qa_sparse_guest_memory *memory;
  bool (*is_host_call)(void *ctx, qa_guest_core_address address);
  void *is_host_call_ctx;
  qa_guest_callback_table *callbacks; /* nullable */
  bool blocks_allowed;                /* TS #blocksAllowed */
  bool managed;                       /* TS #managedMemory !== null */
  qa_x64_instruction_cache *cache;    /* owned; TS #instructions */
  int64_t decode_cache_clears;        /* TS decodeCacheClears */
  /* Borrowed storage behind the last stop's detail/bytes/mnemonic pointers. */
  char stop_detail[512];
  char stop_mnemonic[32];
  uint8_t stop_bytes[15];
} qa_x64_cpu;

/* callbacks NULL and is_host_call NULL: no host stops. Aborts with
 * the TS RangeErrors on mismatched state/memory/callback ownership. */
void qa_x64_cpu_init(qa_x64_cpu *cpu, qa_guest_processor_state *state, qa_sparse_guest_memory *memory,
                     bool (*is_host_call)(void *ctx, qa_guest_core_address address), void *is_host_call_ctx,
                     qa_guest_callback_table *callbacks);
/* Releases the instruction cache. Call before destroying the memory. */
void qa_x64_cpu_destroy(qa_x64_cpu *cpu);
qa_guest_execution_stop qa_x64_cpu_run(qa_x64_cpu *cpu, int64_t instruction_budget,
                                       bool has_return_address, qa_guest_core_address return_address);

#endif
