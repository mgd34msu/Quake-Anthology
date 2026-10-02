#ifndef QA_NATIVE_GUEST_FORMAT_H
#define QA_NATIVE_GUEST_FORMAT_H

#include "abi.h"

/* Reads only the actual stopped guest format. Layouts are owned by the caller
 * and describe promoted arguments after the genuine fixed API prefix. */
bool guest_format_select(const qa_native_guest *, uint64_t,
    guest_abi_layout **, size_t *, qa_error *);
/* Values come from that complete ABI decode. Output includes a terminating
 * NUL; size excludes it. %n writes the real guest destination in source order.
 * This never consumes a host va_list or guesses the fixed API prefix. */
bool guest_format_render(qa_native_guest *, uint64_t, const qa_native_value *,
    size_t, size_t, qa_buffer *, qa_error *);
/* Sequential arguments are read from the real entry frame as each conversion
 * reaches them, so %n writes can affect later aliased stack arguments.
 * Positional arguments are gathered before conversion, as in the source. */
bool guest_format_render_entry(qa_native_guest *, const qa_native_signature *,
    uint64_t, size_t, qa_buffer *, qa_error *);

#endif
