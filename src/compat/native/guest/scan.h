#ifndef QA_NATIVE_GUEST_SCAN_H
#define QA_NATIVE_GUEST_SCAN_H

#include "qa/native_guest.h"

typedef struct guest_scan_program guest_scan_program;
typedef bool (*guest_scan_destination_fn)(void *, uint64_t *, qa_error *);
/* Format and input come from the actual guest; destination pointers are read
 * as their conversions are reached, preserving aliased argument storage. */
bool guest_scan_prepare(const qa_native_guest *, uint64_t format, bool c23,
    guest_scan_program **, qa_error *);
size_t guest_scan_argument_count(const guest_scan_program *);
bool guest_scan_execute(const guest_scan_program *, qa_native_guest *, uint64_t input,
    uint64_t capacity, guest_scan_destination_fn, void *, int32_t *, qa_error *);
void guest_scan_destroy(guest_scan_program *);

#endif
