/* Port of src/compat/qvm/snap-vector-syscalls.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_SNAP_VECTOR_SYSCALLS_H
#define QA_COMPAT_QVM_SNAP_VECTOR_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/memory.h"

bool qa_qvm_snap_vector_syscall(const char *role, const uint8_t *words, size_t words_length,
                                const qa_qvm_game_memory *memory, int32_t *out);

#endif
