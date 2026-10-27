/*
 * Port of src/compat/qvm/cvar-syscalls.ts. QvmCvarServices is a Pick
 * of CvarRegistry, which is qa_cvar_registry in C.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CVAR_SYSCALLS_H
#define QA_COMPAT_QVM_CVAR_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/core/cvars/index.h"

typedef qa_cvar_registry qa_qvm_cvar_services;

/* TS qvmCvarSyscall(role, words, memory, cvars): number|null.
 * `words` is the live syscall word area (DataView). */
bool qa_qvm_cvar_syscall(qa_qvm_role role, const uint8_t *words, size_t words_length,
                         qa_qvm_memory *memory, qa_qvm_cvar_services *cvars,
                         qa_qvm_host_result *out);

#endif
