/*
 * Port of src/guest/abi/adapter.ts: X86AbiAdapter enter/arguments/return/
 * leave over a GuestCpu. GuestCpu/GuestExecutionStop/GuestException come
 * from src/guest/core/contracts.ts (another agent's core module); the
 * shapes are modeled here from that TS with qa_ names, CPU run as a
 * function pointer + ctx. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_ABI_ADAPTER_H
#define QA_GUEST_ABI_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/guest/abi/classify.h"
#include "qa/guest/abi/values.h"

/* GuestException union. */
typedef enum qa_guest_exception_kind {
  QA_EXCEPTION_PROCESSOR = 0, QA_EXCEPTION_MEMORY, QA_EXCEPTION_WINDOWS, QA_EXCEPTION_CXX
} qa_guest_exception_kind;
typedef struct qa_guest_exception {
  qa_guest_exception_kind kind;
  int vector; /* processor */
  bool has_error_code;
  int64_t error_code;
  qa_guest_core_address instruction; /* processor; windows address; cxx object */
  const char *detail; /* processor/memory */
  qa_guest_access access; /* memory */
  qa_guest_core_address address; /* memory */
  int64_t byte_length; /* memory */
  uint32_t code; /* windows */
  uint32_t flags; /* windows */
  const uint64_t *arguments; /* windows */
  size_t argument_count;
  bool has_type_info, has_destructor; /* cxx */
  qa_guest_core_address type_info, destructor;
  qa_guest_native_abi abi; /* cxx */
} qa_guest_exception;

/* GuestExecutionStop union. */
typedef enum qa_guest_execution_stop_kind {
  QA_STOP_BUDGET = 0, QA_STOP_RETURN, QA_STOP_HOST_CALL, QA_STOP_HALT,
  QA_STOP_EXCEPTION, QA_STOP_UNSUPPORTED
} qa_guest_execution_stop_kind;
const char *qa_guest_execution_stop_kind_name(qa_guest_execution_stop_kind kind);
typedef struct qa_guest_execution_stop {
  qa_guest_execution_stop_kind kind;
  int64_t instructions;
  qa_guest_core_address address; /* return/host-call/halt */
  qa_guest_exception exception; /* exception */
  qa_guest_core_address instruction_address; /* unsupported */
  const uint8_t *instruction_bytes;
  size_t instruction_bytes_len;
  const char *mnemonic; /* unsupported */
  const char *detail; /* unsupported */
} qa_guest_execution_stop;

/* GuestCpu: state + memory + run(instructionBudget, returnAddress|null). */
typedef struct qa_guest_cpu {
  qa_guest_processor_state *state;
  qa_mapped_guest_memory *memory;
  qa_mapped_memory_ext *memory_ext;
  qa_guest_module_identity module; /* memory.module in TS */
  void *run_ctx;
  qa_guest_execution_stop (*run)(void *ctx, int64_t instruction_budget,
                                 bool has_return_address, qa_guest_core_address return_address);
} qa_guest_cpu;

typedef struct qa_x86_abi_adapter { qa_guest_native_abi abi; } qa_x86_abi_adapter;

qa_guest_core_address qa_guest_pointer(const qa_mapped_guest_memory *memory, uint64_t raw);
void qa_x86_abi_adapter_init(qa_x86_abi_adapter *adapter, qa_guest_native_abi abi);
void qa_x86_abi_adapter_enter(qa_x86_abi_adapter *adapter, qa_guest_cpu *cpu, qa_guest_core_address target,
                              const qa_guest_call_signature *signature,
                              const qa_call_value *arguments, size_t argument_count,
                              qa_guest_core_address return_address);
/* Results are malloc'd arrays of call values; free each value then the array.
 * Returns false only on OOM. */
bool qa_x86_abi_adapter_arguments(qa_x86_abi_adapter *adapter, qa_guest_cpu *cpu,
                                  const qa_guest_call_signature *signature,
                                  const qa_abi_value_layout *variadic_layouts, size_t variadic_count,
                                  qa_call_value **out, size_t *out_count);
qa_call_value qa_x86_abi_adapter_argument(qa_x86_abi_adapter *adapter, qa_guest_cpu *cpu,
                                          const qa_guest_call_signature *signature, int64_t index);
qa_call_result qa_x86_abi_adapter_return_value(qa_x86_abi_adapter *adapter, qa_guest_cpu *cpu,
                                               const qa_guest_call_signature *signature);
void qa_x86_abi_adapter_leave(qa_x86_abi_adapter *adapter, qa_guest_cpu *cpu,
                              const qa_guest_call_signature *signature, qa_call_result *result);

#endif
