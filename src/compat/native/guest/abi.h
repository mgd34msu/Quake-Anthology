#ifndef QA_NATIVE_GUEST_ABI_H
#define QA_NATIVE_GUEST_ABI_H

#include "qa/native_guest.h"

typedef enum guest_abi_convention {
    GUEST_ABI_DEFAULT, GUEST_ABI_STDCALL, GUEST_ABI_FASTCALL, GUEST_ABI_THISCALL
} guest_abi_convention;
typedef struct guest_abi_field {
    qa_native_value_type kind;
    size_t offset, count;
} guest_abi_field;
/* Explicit POD scalar fields include actual repeated/overlapping fields.
 * Scalar layouts carry their actual width/alignment and no fields. */
typedef struct guest_abi_layout {
    qa_native_value_type kind;
    size_t bytes, alignment;
    const guest_abi_field *fields;
    size_t field_count;
    /* Actual stack-class argument, including System V x87 long double.
     * This describes argument passing, never an inferred result class. */
    bool stack_only;
} guest_abi_layout;
typedef struct guest_abi_signature {
    qa_native_abi abi;
    guest_abi_convention convention;
    const guest_abi_layout *parameters;
    size_t parameter_count;
    guest_abi_layout result;
    bool variadic;
} guest_abi_signature;
typedef struct guest_abi_plan guest_abi_plan;

/* Plans own every layout/field/location. Extra variadic layouts are supplied
 * by the actual API descriptor; this layer never guesses stack argument types.
 * Natural stock qa_native_type layouts are converted using the target ABI. */
bool guest_abi_plan_create(const guest_abi_signature *, const guest_abi_layout *,
    size_t, guest_abi_plan **, qa_error *);
bool guest_abi_plan_native(const qa_native_signature *, const guest_abi_layout *,
    size_t, guest_abi_plan **, qa_error *);
void guest_abi_plan_destroy(guest_abi_plan *);
size_t guest_abi_argument_count(const guest_abi_plan *);
size_t guest_abi_result_bytes(const guest_abi_plan *);

/* CPU and stack belong to the same lower guest. The caller supplies its actual
 * executable return trap and instruction budget. Successful nested calls
 * restore the enclosing CPU; faults retain the terminal processor and RAM. */
bool guest_abi_invoke(const guest_abi_plan *, qa_native_guest *, uint64_t, uint64_t,
    const qa_native_value *, size_t, qa_native_value *, size_t, qa_error *);
bool guest_abi_invoke_original(const guest_abi_plan *, qa_native_guest *, uint64_t,
    uint64_t, uint64_t, const qa_native_value *, size_t, qa_native_value *, size_t, qa_error *);
/* Same actual ABI stack/result contract, with explicit unbudgeted hardware
 * execution. Nested success restores every owned hardware component. */
bool guest_abi_invoke_native(const guest_abi_plan *, qa_native_guest *, uint64_t, uint64_t,
    const qa_native_value *, size_t, qa_native_value *, qa_error *);
/* Decode a real import entry before its prologue. Aggregate values borrow the
 * returned owned storage until it is released. Outputs and storage start empty. */
bool guest_abi_decode(const guest_abi_plan *, const qa_native_guest *,
    qa_native_value *, size_t, qa_buffer *, qa_error *);
/* Read one actual argument location at the reached source conversion. Earlier
 * %n effects can change later stack arguments; no later location is read. */
bool guest_abi_decode_argument(const guest_abi_plan *, const qa_native_guest *, size_t,
    qa_native_value *, qa_buffer *, qa_error *);
/* Emit the actual result and return from that same stopped import frame. */
bool guest_abi_return(const guest_abi_plan *, qa_native_guest *,
    const qa_native_value *, qa_error *);

#endif
