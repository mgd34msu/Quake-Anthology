/*
 * Port of src/guest/core/cpuid.ts. CPUID register/feature encoding: Intel
 * SDM Vol. 2A; Microsoft __cpuid docs. GPL-2.0-or-later.
 */
#include "qa/guest/core/cpuid.h"

qa_cpuid_result qa_virtual_cpuid(uint64_t leaf, bool compare_exchange8) {
  qa_cpuid_result result = {0, 0, 0, 0};
  if (leaf == 0) {
    result.eax = 7;
    /* EBX, EDX, ECX spell "QuakeTSguest". */
    result.ebx = 0x6b617551ull;
    result.edx = 0x67535465ull;
    result.ecx = 0x74736575ull;
  } else if (leaf == 1) {
    result.eax = 0x600;
    result.ebx = 0x10000;
    /* x87, CMOV, SSE, SSE2, and CMPXCHG8B where implemented. No AVX, XSAVE, RDRAND or TSC. */
    result.edx = compare_exchange8 ? 0x06008101ull : 0x06008001ull;
  } else if (leaf == 0x80000000ull) {
    result.eax = 0x80000001ull;
  } else if (leaf == 0x80000001ull) {
    result.ecx = 1;
    result.edx = 0x20000000ull;
  }
  return result;
}
