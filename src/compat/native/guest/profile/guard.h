#ifndef QA_NATIVE_GUEST_PROFILE_GUARD_H
#define QA_NATIVE_GUEST_PROFILE_GUARD_H

#include "instruction.h"

typedef struct guest_profile_guard_launch {
    const char *runner, *client; /* Actual configured SDK runner/client paths. */
} guest_profile_guard_launch;
typedef struct guest_profile_guard_receipt {
    uint32_t policy, version; /* Written by the actual installed client. */
} guest_profile_guard_receipt;
#define GUEST_PROFILE_GUARD_SOURCE_X64 1u
#define GUEST_PROFILE_GUARD_SOURCE_VERSION 2u
#define GUEST_PROFILE_GUARD_CONTROL_VERSION 2u
typedef struct guest_profile_guard_mapping {
    qa_native_guest_mapping mapping;
    bool file;
    uint64_t accessible_bytes; /* Actual backing's accessible EOF prefix. */
} guest_profile_guard_mapping;
typedef struct guest_profile_guard_callback { uint64_t id, address; } guest_profile_guard_callback;
typedef enum guest_profile_guard_operation {
    GUEST_PROFILE_GUARD_PROBE, GUEST_PROFILE_GUARD_ENTER,
    GUEST_PROFILE_GUARD_RESUME, GUEST_PROFILE_GUARD_LEAVE,
    GUEST_PROFILE_GUARD_CAPTURE
} guest_profile_guard_operation;
typedef enum guest_profile_guard_fault_kind {
    GUEST_PROFILE_GUARD_NO_FAULT, GUEST_PROFILE_GUARD_FETCH,
    GUEST_PROFILE_GUARD_OPERAND, GUEST_PROFILE_GUARD_INSTRUCTION,
    GUEST_PROFILE_GUARD_PROCESSOR, GUEST_PROFILE_GUARD_ENTRY,
    GUEST_PROFILE_GUARD_SYSCALL
} guest_profile_guard_fault_kind;
typedef struct guest_profile_guard_fault {
    guest_profile_guard_fault_kind kind;
    uint32_t access, vector;
    uint64_t scope, instruction, address, bytes;
} guest_profile_guard_fault;
typedef struct guest_profile_guard_control {
    uint64_t magic;
    uint32_t version, operation, status, reserved;
    guest_profile_guard_receipt installed;
    uint64_t scope, entry, stop, fs_base, gs_base;
    bool syscalls; /* Actual invocation owns a stopped Linux syscall service. */
    const guest_profile_guard_mapping *mappings;
    size_t mapping_count;
    const guest_profile_guard_callback *callbacks;
    size_t callback_count;
    guest_profile_guard_fault *fault;
    uint8_t *xsave;
    size_t xsave_bytes; /* Borrowed actual stopped signal/capture transfer. */
} guest_profile_guard_control;
#define GUEST_PROFILE_GUARD_MAGIC UINT64_C(0x5141475541524431)

/* Actual child/controller control entry intercepted by the separately loaded
 * DR client. Without that genuine client the status stays zero and installation
 * fails. Its receipt identifies the monitor, not arbitrary CPU-state admission.
 * Calls use real stopped child rows; ENTER/RESUME borrow them only for
 * this synchronous control transfer. No guest source can call this entry: the
 * active instruction guard excludes controller fetch/data addresses. CAPTURE
 * translates the actual captured x87 cache FIP through DR's translation API;
 * the transfer remains private and is never an opaque cold wire format. */
bool guest_profile_guard_control_call(guest_profile_guard_control *, qa_error *);
void qa_guest_profile_control(guest_profile_guard_control *);
void qa_guest_profile_fault(void);
void qa_guest_profile_import(void);

#endif
