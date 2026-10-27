/*
 * Port of src/compat/qvm/guest-memory.ts (QvmGuestMemory implements
 * GuestMemory). The address space symbol is the instance pointer.
 * `borrow` returns a live view; `copy` returns a malloc'd copy in
 * `copy_storage` owned by the instance until the next copy/destroy
 * (the TS returns a fresh Uint8Array). GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_GUEST_MEMORY_H
#define QA_COMPAT_QVM_GUEST_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/interpreter.h"
#include "qa/contracts/execution.h"

typedef struct qa_qvm_guest_memory {
  qa_module_identity module;
  qa_qvmx_memory *memory; /* borrowed */
  uint8_t *copy_storage;  /* owned */
} qa_qvm_guest_memory;

void qa_qvm_guest_memory_init(qa_qvm_guest_memory *guest, qa_module_identity module,
                              qa_qvmx_memory *memory);
void qa_qvm_guest_memory_destroy(qa_qvm_guest_memory *guest);
/* Fills the GuestMemory function table (contracts/execution.h) for this instance. */
qa_guest_memory qa_qvm_guest_memory_as_guest_memory(qa_qvm_guest_memory *guest);

qa_guest_address_result qa_qvm_guest_memory_pointer(qa_qvm_guest_memory *guest, int64_t raw_value);
qa_guest_address qa_qvm_guest_memory_offset(qa_qvm_guest_memory *guest, qa_guest_address address,
                                            int64_t displacement);
qa_qvmx_view qa_qvm_guest_memory_borrow(qa_qvm_guest_memory *guest, qa_guest_address address,
                                        double byte_length);
/* Borrowed copy storage; valid until the next copy or destroy. */
qa_qvmx_view qa_qvm_guest_memory_copy(qa_qvm_guest_memory *guest, qa_guest_address address,
                                      double byte_length);
void qa_qvm_guest_memory_write(qa_qvm_guest_memory *guest, qa_guest_address address,
                               const uint8_t *bytes, size_t length);

#endif
