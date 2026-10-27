/*
 * Port of src/guest/abi/classify.ts: System V eightbyte classification and
 * per-ABI call planning. C accommodation: the TS WeakMap plan cache (with
 * frozen-layout invalidation) has no observable effect beyond identity, so
 * every qa_plan_guest_call() builds a fresh plan the caller releases with
 * qa_abi_call_plan_free(); signature edits are therefore always followed.
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_ABI_CLASSIFY_H
#define QA_GUEST_ABI_CLASSIFY_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/guest/abi/values.h"

typedef enum qa_abi_location_kind { QA_LOCATION_INTEGER = 0, QA_LOCATION_SSE, QA_LOCATION_STACK } qa_abi_location_kind;
typedef struct qa_abi_location {
  qa_abi_location_kind kind;
  qa_guest_register reg; /* integer */
  int sse_register;      /* sse */
  int64_t stack_offset;  /* stack */
  int64_t offset;
  int64_t bytes;
} qa_abi_location;

typedef struct qa_abi_argument {
  qa_abi_value_layout layout;
  bool indirect;
  qa_abi_location *locations; /* owned */
  size_t location_count;
} qa_abi_argument;

typedef enum qa_abi_result_kind {
  QA_ABI_RESULT_VOID = 0, QA_ABI_RESULT_X87, QA_ABI_RESULT_REGISTERS, QA_ABI_RESULT_MEMORY
} qa_abi_result_kind;
typedef struct qa_abi_result {
  qa_abi_result_kind kind;
  qa_abi_storage storage; /* x87: float32|float64 */
  qa_abi_value_layout layout; /* registers/memory */
  qa_abi_location *locations; /* registers, owned */
  size_t location_count;
  qa_abi_argument pointer; /* memory */
} qa_abi_result;

typedef struct qa_abi_call_plan {
  qa_guest_native_abi abi;
  qa_abi_argument *arguments; /* owned */
  size_t argument_count;
  qa_abi_result result;
  int64_t stack_bytes;
  int64_t stack_alignment;
  int64_t callee_pop_bytes;
  int64_t vector_registers;
} qa_abi_call_plan;

typedef enum qa_eightbyte_class { QA_EIGHTBYTE_NONE = 0, QA_EIGHTBYTE_INTEGER, QA_EIGHTBYTE_SSE } qa_eightbyte_class;
const char *qa_eightbyte_class_name(qa_eightbyte_class kind);

/* Returns false for TS "memory"; otherwise fills classes (at most 2) and *count. */
bool qa_classify_system_v_aggregate(const qa_abi_value_layout *layout, qa_eightbyte_class *classes, size_t *count);

/* layouts==NULL uses signature->parameters. Returns false only on OOM. */
bool qa_plan_guest_call(qa_abi_call_plan *out, const qa_guest_call_signature *signature,
                        const qa_abi_value_layout *layouts, size_t layout_count);
void qa_abi_call_plan_free(qa_abi_call_plan *plan);

#endif
