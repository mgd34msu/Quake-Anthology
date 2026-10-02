#ifndef QA_NATIVE_GUEST_PROFILE_INSTRUCTION_H
#define QA_NATIVE_GUEST_PROFILE_INSTRUCTION_H

#include "qa/native_guest.h"

typedef enum guest_profile_instruction_kind {
    GUEST_PROFILE_HARDWARE, GUEST_PROFILE_CPUID,
    GUEST_PROFILE_UNSUPPORTED, GUEST_PROFILE_PROCESSOR_FAULT,
    GUEST_PROFILE_SYSCALL
} guest_profile_instruction_kind;
typedef struct guest_profile_instruction {
    guest_profile_instruction_kind kind;
    uint32_t vector;
    bool repeated_string;
    const char *detail;
} guest_profile_instruction;

/* Classify one actual decoder-qualified instruction, never arbitrary file
 * bytes or a declaration's claimed safe region. Fetch/operand authority and
 * full CPU preservation remain mandatory duties of the installed guard. */
guest_profile_instruction guest_profile_x64_instruction(qa_bytes);
/* The source CPU owns virtual feature enumeration, independent of host CPUID.
 * Register outputs are 32-bit writes, therefore zero-extended by the caller. */
void guest_profile_x64_cpuid(uint32_t, uint32_t, uint32_t[4]);

#endif
