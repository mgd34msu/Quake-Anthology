#ifndef QA_NATIVE_GUEST_MEMORY_STRING_H
#define QA_NATIVE_GUEST_MEMORY_STRING_H

#include "qa/native_guest.h"
#include "qa/arena.h"

/* Raw narrow or little-endian UTF-16 units, excluding the terminator in size.
 * A terminated result includes its terminator in storage. */
bool guest_string_read(const qa_native_guest *, uint64_t, size_t unit_bytes,
    size_t maximum_units, qa_arena *, qa_buffer *, bool *terminated, qa_error *);

#endif
