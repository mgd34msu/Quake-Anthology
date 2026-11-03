/*
 * Port of src/guest/core/cpuid.ts: virtual CPU feature enumeration,
 * independent of the host machine. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_CORE_CPUID_H
#define QA_GUEST_CORE_CPUID_H

#include <stdbool.h>
#include <stdint.h>

typedef struct qa_cpuid_result {
  uint64_t eax, ebx, ecx, edx;
} qa_cpuid_result;

/* compare_exchange8 advertises CMPXCHG8B only for CPUs that implement it. */
qa_cpuid_result qa_virtual_cpuid(uint64_t leaf, bool compare_exchange8);

#endif
