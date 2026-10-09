#include "cpu_clock.h"
#include "internal.h"
#include "qa/platform_services.h"

bool guest_cpu_clock_physical_read(qa_native_guest *guest, int32_t clock_id,
    int64_t *seconds, int32_t *nanoseconds, qa_error *error)
{
    if (qa_native_guest_execution(guest) == QA_NATIVE_GUEST_HOST_X86_64)
        return guest_host_child_cpu_clock_read(guest->child, clock_id, seconds, nanoseconds, error);
    /* The emulated task executes synchronously on this exclusive strand.
     * Count its entered intervals, excluding other threads and stopped work. */
    qa_platform_timespec value;
    if (!qa_platform_clock_read(QA_PLATFORM_CLOCK_THREAD_CPU, &value, error)) return false;
    *seconds = value.seconds; *nanoseconds = value.nanoseconds;
    return true;
}
bool guest_cpu_clock_read(const guest_cpu_clock *clock, qa_native_guest *guest,
    int32_t clock_id, int64_t *seconds, int32_t *nanoseconds, qa_error *error)
{
    if (!clock || (clock_id != 2 && clock_id != 3) || !seconds || !nanoseconds)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Source CPU clock needs its actual task owner");
    const guest_cpu_clock_value *saved = clock->values + (clock_id - 2);
    uint64_t total = saved->seconds; int64_t part = saved->nanoseconds;
    if (clock->depth) {
        int64_t now; int32_t nanos;
        if (!guest_cpu_clock_physical_read(guest, clock_id, &now, &nanos, error)) return false;
        if (now < saved->baseline_seconds || nanos < 0 || nanos >= 1000000000 ||
            (now == saved->baseline_seconds && nanos < saved->baseline_nanoseconds))
            return guest_fail(error, QA_ERROR_FORMAT, 0, "Physical source CPU clock moved backwards");
        uint64_t elapsed = (uint64_t)(now - saved->baseline_seconds);
        if (elapsed > (uint64_t)INT64_MAX - total)
            return guest_fail(error, QA_ERROR_FORMAT, 0, "Source CPU accounting overflowed its timespec domain");
        total += elapsed; part += (int64_t)nanos - saved->baseline_nanoseconds;
        if (part < 0) { --total; part += 1000000000; }
        if (part >= 1000000000) {
            if (total == INT64_MAX) return guest_fail(error, QA_ERROR_FORMAT, 0, "Source CPU accounting seconds overflow");
            ++total; part -= 1000000000;
        }
    }
    *seconds = (int64_t)total; *nanoseconds = (int32_t)part;
    return true;
}
bool guest_cpu_clock_enter(guest_cpu_clock *clock, qa_native_guest *guest, qa_error *error)
{
    if (clock->depth) { ++clock->depth; return true; }
    for (int32_t id = 2; id <= 3; ++id) {
        guest_cpu_clock_value *saved = clock->values + (id - 2);
        if (!guest_cpu_clock_physical_read(guest, id, &saved->baseline_seconds, &saved->baseline_nanoseconds, error)) return false;
        if (saved->baseline_seconds < 0 || saved->baseline_nanoseconds < 0 || saved->baseline_nanoseconds >= 1000000000)
            return guest_fail(error, QA_ERROR_FORMAT, 0, "Source physical CPU clock has invalid fields");
    }
    clock->depth = 1;
    return true;
}
bool guest_cpu_clock_leave(guest_cpu_clock *clock, qa_native_guest *guest, qa_error *error)
{
    if (clock->depth > 1) { --clock->depth; return true; }
    bool okay = true;
    for (int32_t id = 2; id <= 3 && okay; ++id) {
        int64_t seconds; int32_t nanoseconds;
        okay = guest_cpu_clock_read(clock, guest, id, &seconds, &nanoseconds, error);
        if (okay) {
            clock->values[id - 2].seconds = (uint64_t)seconds;
            clock->values[id - 2].nanoseconds = (uint32_t)nanoseconds;
        }
    }
    clock->depth = 0;
    return okay;
}
bool guest_cpu_clock_fields(qa_source_save_io *io, guest_cpu_clock *clock)
{
    for (size_t i = 0; i < 2; ++i)
        if (!qa_source_save_u64(io, &clock->values[i].seconds) ||
            !qa_source_save_u32(io, &clock->values[i].nanoseconds) ||
            clock->values[i].seconds > INT64_MAX || clock->values[i].nanoseconds >= 1000000000)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "Saved source CPU accounting is invalid");
    return true;
}
