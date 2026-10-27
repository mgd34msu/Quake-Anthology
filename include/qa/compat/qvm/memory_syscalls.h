/*
 * Port of src/compat/qvm/memory-syscalls.ts. False means another
 * service owns the trap (TS null). Role is unused, as in the TS.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MEMORY_SYSCALLS_H
#define QA_COMPAT_QVM_MEMORY_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/memory.h"

bool qa_qvm_memory_syscall(const char *role, const uint8_t *words, size_t words_length,
                           qa_qvm_memory *memory, int32_t *out);

#endif
