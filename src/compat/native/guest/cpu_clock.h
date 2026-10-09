#ifndef QA_NATIVE_GUEST_CPU_CLOCK_H
#define QA_NATIVE_GUEST_CPU_CLOCK_H

#include "qa/native_guest.h"
#include "qa/source_save.h"

typedef struct guest_cpu_clock_value {
    uint64_t seconds;
    uint32_t nanoseconds;
    int64_t baseline_seconds;
    int32_t baseline_nanoseconds;
} guest_cpu_clock_value;
typedef struct guest_cpu_clock {
    guest_cpu_clock_value values[2];
    unsigned depth;
} guest_cpu_clock;

bool guest_cpu_clock_read(const guest_cpu_clock *, qa_native_guest *, int32_t,
    int64_t *, int32_t *, qa_error *);
bool guest_cpu_clock_physical_read(qa_native_guest *, int32_t,
    int64_t *, int32_t *, qa_error *);
bool guest_cpu_clock_enter(guest_cpu_clock *, qa_native_guest *, qa_error *);
bool guest_cpu_clock_leave(guest_cpu_clock *, qa_native_guest *, qa_error *);
bool guest_cpu_clock_fields(qa_source_save_io *, guest_cpu_clock *);

#endif
