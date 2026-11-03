/* TS planGuestCall(...).stackBytes (guest/abi/classify.ts) for scalar native
 * calls, in plain types: guest/abi headers conflict with contracts/execution.h
 * in one TU. result_storage NULL is "void". GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_CALL_PLAN_H
#define QA_COMPAT_Q2_NATIVE_MOD_CALL_PLAN_H
#include <stddef.h>
#include <stdint.h>
int64_t qa_compat_q2_native_plan_stack_bytes(const char *abi_kind, const char *abi_image, int64_t pointer_bytes, const char *abi_call,
    const char *const *parameter_storages, size_t parameter_count, const char *result_storage);
#endif
