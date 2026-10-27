/* Port of src/compat/qvm/allocation.ts (types only). GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_ALLOCATION_H
#define QA_COMPAT_QVM_ALLOCATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* TS QvmAllocation: reading `bytes` must reject a released allocation;
 * the C form borrows the allocator's storage (bytes/length pair). */
typedef struct qa_qvm_allocation {
  const uint8_t *bytes;
  size_t length;
} qa_qvm_allocation;

typedef struct qa_qvm_allocation_request {
  const char *purpose;
  const char *resource;
  double byte_length;
} qa_qvm_allocation_request;

/* TS QvmAllocationProfile union on kind: "unaccounted" | "accounted". */
typedef struct qa_qvm_allocation_profile {
  bool accounted;
  void *ctx;
  qa_qvm_allocation (*allocate)(void *ctx, qa_qvm_allocation_request request);
} qa_qvm_allocation_profile;

#endif
