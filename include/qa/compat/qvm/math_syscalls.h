/*
 * Port of src/compat/qvm/math-syscalls.ts. The TS DataView of syscall
 * words becomes a byte span; out-of-span word reads abort like the
 * DataView RangeError. False means the trap belongs to another
 * service (TS null), not a successful zero result. Role is the exact
 * TS string "game" | "cgame" | "ui".
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MATH_SYSCALLS_H
#define QA_COMPAT_QVM_MATH_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool qa_qvm_math_syscall(const char *role, const uint8_t *words, size_t words_length, int32_t *out);

#endif
