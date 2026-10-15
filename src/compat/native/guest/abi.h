#ifndef QA_NATIVE_GUEST_ABI_H
#define QA_NATIVE_GUEST_ABI_H

#include "qa/native_guest.h"
#include "qa/arena.h"

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
    qa_native_call_convention convention;
    const guest_abi_layout *parameters;
    size_t parameter_count;
    guest_abi_layout result;
    bool variadic;
} guest_abi_signature;
typedef struct guest_abi_plan guest_abi_plan;
typedef struct guest_abi_call guest_abi_call;

typedef enum guest_readonly_kind {
    GUEST_READONLY_NONE, GUEST_READONLY_COMPARE, GUEST_READONLY_LENGTH
} guest_readonly_kind;
typedef struct guest_abi_scalar_call {
    uint8_t count, arguments[3], result, result_bytes;
} guest_abi_scalar_call;
/* Lower only fixed Win64 integer-register calls; unsupported plans stay brokered. */
bool guest_abi_scalar_plan(const guest_abi_plan *, guest_abi_scalar_call *);
void guest_abi_scalar_arguments(const guest_abi_scalar_call *, const uint64_t[16], uint64_t[3]);
void guest_abi_integer_register(uint64_t[16], unsigned, size_t, uint64_t);
void guest_abi_scalar_result(const guest_abi_scalar_call *, uint64_t[16], uint64_t);
bool guest_readonly_apply(guest_readonly_kind, qa_bytes, qa_bytes, uint64_t, uint64_t *);
typedef bool (*guest_abi_read_fn)(void *, uint64_t, void *, size_t, qa_error *);
typedef bool (*guest_abi_access_fn)(void *, uint64_t, size_t, uint32_t, qa_error *);
bool guest_abi_return_address(unsigned, size_t, uint64_t, guest_abi_read_fn,
    guest_abi_access_fn, void *, uint64_t *, uint64_t *, qa_error *);

/* NULL arena owns load-time layout/field/location storage; a supplied arena
 * owns call-scoped plans until reset. Extra variadic layouts are supplied
 * by the actual API descriptor; this layer never guesses stack argument types.
 * Natural stock qa_native_type layouts are converted using the target ABI. */
bool guest_abi_plan_create(const guest_abi_signature *, const guest_abi_layout *,
    size_t, qa_arena *, guest_abi_plan **, qa_error *);
bool guest_abi_plan_native(const qa_native_signature *, const guest_abi_layout *,
    size_t, qa_arena *, guest_abi_plan **, qa_error *);
void guest_abi_plan_destroy(guest_abi_plan *);
size_t guest_abi_argument_count(const guest_abi_plan *);
size_t guest_abi_argument_bytes(const guest_abi_plan *);
size_t guest_abi_result_bytes(const guest_abi_plan *);
bool guest_abi_scratch_bytes(size_t, size_t, size_t *, qa_error *);
size_t guest_abi_format_plan_bytes(size_t);

/* The caller retains storage and plan until close. ADDRESS arguments name
 * admitted guest memory, not copied host pointees. Result storage is borrowed.
 * A soft step retains the actual CPU and never decodes a return value. */
bool guest_abi_call_begin(const guest_abi_plan *, qa_native_guest *, uint64_t,
    uint64_t, const qa_native_value *, size_t, qa_native_value *, size_t, bool,
    uint64_t, qa_arena *, guest_abi_call **, qa_error *);
bool guest_abi_call_step(guest_abi_call *, size_t, bool *, qa_error *);
bool guest_abi_call_finish(guest_abi_call *, qa_error *);
void guest_abi_call_close(guest_abi_call **);

typedef struct guest_process_call_options {
    uint64_t return_trap;
    size_t budget;
    void *context;
    bool previous_busy;
    bool (*enter)(void *, qa_error *);
    bool (*leave)(void *, bool, qa_error *);
    void (*close)(void *, bool, bool);
} guest_process_call_options;

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
 * returned caller-arena storage until that arena resets. Outputs and storage start empty. */
bool guest_abi_decode(const guest_abi_plan *, const qa_native_guest *,
    qa_native_value *, size_t, qa_arena *, qa_buffer *, qa_error *);
/* Read one actual argument location at the reached source conversion. Earlier
 * %n effects can change later stack arguments; no later location is read. */
bool guest_abi_decode_argument(const guest_abi_plan *, const qa_native_guest *, size_t,
    qa_native_value *, qa_arena *, qa_buffer *, qa_error *);
/* Emit the actual result and return from that same stopped import frame. */
bool guest_abi_return(const guest_abi_plan *, qa_native_guest *,
    const qa_native_value *, qa_error *);

#endif
