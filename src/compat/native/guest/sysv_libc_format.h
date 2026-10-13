#ifndef QA_NATIVE_GUEST_FORMAT_H
#define QA_NATIVE_GUEST_FORMAT_H

#include "abi.h"

size_t guest_format_scratch_bytes(void);

/* Reads only the actual stopped guest format. Layouts borrow the caller arena
 * and describe promoted arguments after the genuine fixed API prefix. */
bool guest_format_select(const qa_native_guest *, uint64_t,
    qa_arena *, guest_abi_layout **, size_t *, qa_error *);
/* Values come from that complete ABI decode. Output includes a terminating
 * NUL and borrows the caller arena; size excludes it. %n writes the real guest destination in source order.
 * This never consumes a host va_list or guesses the fixed API prefix. */
bool guest_format_render(qa_native_guest *, uint64_t, const qa_native_value *,
    size_t, size_t, qa_arena *, qa_buffer *, qa_error *);
/* Sequential arguments are read from the real entry frame as each conversion
 * reaches them, so %n writes can affect later aliased stack arguments.
 * Positional arguments are gathered before conversion, as in the source. */
bool guest_format_render_entry(qa_native_guest *, const qa_native_signature *,
    uint64_t, size_t, qa_arena *, qa_buffer *, qa_error *);
/* UCRT's actual va_list and destination use the same parser/conversions as
 * entry rendering. Options select its counting/termination and float policy;
 * format errors are returned as guest errno values, not host failures. */
bool guest_format_render_va(qa_native_guest *, uint64_t format, uint64_t arguments,
    uint64_t destination, uint64_t capacity, uint64_t options, qa_arena *,
    int32_t *result, uint32_t *format_errno, qa_error *);

#endif
