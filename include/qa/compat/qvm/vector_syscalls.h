/* Port of src/compat/qvm/vector-syscalls.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_VECTOR_SYSCALLS_H
#define QA_COMPAT_QVM_VECTOR_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/memory.h"

bool qa_qvm_vector_syscall(const char *role, const uint8_t *words, size_t words_length,
                           qa_qvm_memory *memory, int32_t *out);

#endif
