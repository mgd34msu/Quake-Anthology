#include "qa/allocation_gate.h"

#include <stdatomic.h>
#include <stdio.h>

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
void *__wrap_malloc(size_t);
void *__wrap_calloc(size_t, size_t);
void *__wrap_realloc(void *, size_t);
void __wrap_free(void *);

static _Atomic bool observing;
static _Atomic unsigned in_flight;
static _Atomic uint64_t counters[QA_ALLOCATION_GATE_COUNTERS];
static qa_allocation_gate_record records[QA_ALLOCATION_GATE_RECORDS];
static qa_allocation_gate_summary summary;
static uint64_t frame;

static uint64_t sum(uint64_t a, uint64_t b)
{ return b > UINT64_MAX - a ? UINT64_MAX : a + b; }

static void count(qa_allocation_gate_counter counter, uint64_t amount)
{
    uint64_t previous = atomic_load_explicit(&counters[counter], memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(&counters[counter], &previous,
        sum(previous, amount), memory_order_relaxed, memory_order_relaxed)) {}
}

static bool enter(void)
{
    if (!atomic_load_explicit(&observing, memory_order_acquire)) return false;
    atomic_fetch_add_explicit(&in_flight, 1, memory_order_acquire);
    if (atomic_load_explicit(&observing, memory_order_acquire)) return true;
    atomic_fetch_sub_explicit(&in_flight, 1, memory_order_release);
    return false;
}

static void leave(void)
{ atomic_fetch_sub_explicit(&in_flight, 1, memory_order_release); }

void qa_allocation_gate_capacity_exhausted(void)
{
    if (!enter()) return;
    count(QA_ALLOCATION_GATE_CAPACITY_EXHAUSTIONS, 1);
    leave();
}

void *__wrap_malloc(size_t size)
{
    bool watched = enter();
    if (watched) {
        count(QA_ALLOCATION_GATE_MALLOC, 1);
        count(QA_ALLOCATION_GATE_BYTES, (uint64_t)size);
    }
    void *result = __real_malloc(size);
    if (watched) {
        if (!result) count(QA_ALLOCATION_GATE_NULL_RESULTS, 1);
        leave();
    }
    return result;
}

void *__wrap_calloc(size_t length, size_t size)
{
    bool watched = enter();
    if (watched) {
        uint64_t n = (uint64_t)length, s = (uint64_t)size;
        bool overflow = n && s > UINT64_MAX / n;
        bool size_overflow = length && size > SIZE_MAX / length;
        count(QA_ALLOCATION_GATE_CALLOC, 1);
        count(QA_ALLOCATION_GATE_BYTES, overflow ? UINT64_MAX : n * s);
        if (size_overflow) count(QA_ALLOCATION_GATE_SIZE_OVERFLOWS, 1);
    }
    void *result = __real_calloc(length, size);
    if (watched) {
        if (!result) count(QA_ALLOCATION_GATE_NULL_RESULTS, 1);
        leave();
    }
    return result;
}

void *__wrap_realloc(void *pointer, size_t size)
{
    bool watched = enter();
    if (watched) {
        count(QA_ALLOCATION_GATE_REALLOC, 1);
        count(QA_ALLOCATION_GATE_BYTES, (uint64_t)size);
    }
    void *result = __real_realloc(pointer, size);
    if (watched) {
        if (!result) count(QA_ALLOCATION_GATE_NULL_RESULTS, 1);
        leave();
    }
    return result;
}

void __wrap_free(void *pointer)
{
    bool watched = pointer && enter();
    if (watched) count(QA_ALLOCATION_GATE_FREE, 1);
    __real_free(pointer);
    if (watched) leave();
}

void qa_allocation_gate_begin(void)
{
    ++frame;
    for (size_t i = 0; i < QA_ALLOCATION_GATE_COUNTERS; ++i)
        atomic_store_explicit(&counters[i], 0, memory_order_relaxed);
    atomic_store_explicit(&observing, true, memory_order_release);
}

qa_allocation_gate_counts qa_allocation_gate_end(bool playing, bool succeeded)
{
    atomic_store_explicit(&observing, false, memory_order_release);
    while (atomic_load_explicit(&in_flight, memory_order_acquire)) {}
    qa_allocation_gate_counts sample = {0};
    for (size_t i = 0; i < QA_ALLOCATION_GATE_COUNTERS; ++i)
        sample.values[i] = atomic_load_explicit(&counters[i], memory_order_relaxed);
    if (!playing || ++summary.playing_frames <= QA_ALLOCATION_GATE_WARMUP) return sample;
    ++summary.measured_frames;
    for (size_t i = 0; i < QA_ALLOCATION_GATE_COUNTERS; ++i) {
        summary.total.values[i] = sum(summary.total.values[i], sample.values[i]);
        if (sample.values[i] > summary.peak.values[i]) summary.peak.values[i] = sample.values[i];
    }
    if (summary.recorded_frames < QA_ALLOCATION_GATE_RECORDS) {
        records[summary.recorded_frames++] = (qa_allocation_gate_record){frame, sample, succeeded};
    } else ++summary.discarded_records;
    return sample;
}

const qa_allocation_gate_summary *qa_allocation_gate_read(void)
{ return &summary; }

const qa_allocation_gate_record *qa_allocation_gate_records(void)
{ return records; }

static void print_counts(const qa_allocation_gate_counts *counts)
{
    (void)fprintf(stderr, " malloc=%llu calloc=%llu realloc=%llu frees=%llu requested_bytes=%llu null_results=%llu size_overflows=%llu capacity_exhaustions=%llu",
        (unsigned long long)counts->values[QA_ALLOCATION_GATE_MALLOC],
        (unsigned long long)counts->values[QA_ALLOCATION_GATE_CALLOC],
        (unsigned long long)counts->values[QA_ALLOCATION_GATE_REALLOC],
        (unsigned long long)counts->values[QA_ALLOCATION_GATE_FREE],
        (unsigned long long)counts->values[QA_ALLOCATION_GATE_BYTES],
        (unsigned long long)counts->values[QA_ALLOCATION_GATE_NULL_RESULTS],
        (unsigned long long)counts->values[QA_ALLOCATION_GATE_SIZE_OVERFLOWS],
        (unsigned long long)counts->values[QA_ALLOCATION_GATE_CAPACITY_EXHAUSTIONS]);
}

void qa_allocation_gate_report(void)
{
    (void)fprintf(stderr, "allocation_gate scope=engine_static_calls threads=all exclusions=dso_libc_internal,other_allocation_apis,save_drain,pacing,shutdown warmup=%u\n",
        QA_ALLOCATION_GATE_WARMUP);
    for (uint64_t i = 0; i < summary.recorded_frames; ++i) {
        (void)fprintf(stderr, "allocation_gate frame=%llu succeeded=%u",
            (unsigned long long)records[i].frame, records[i].succeeded ? 1u : 0u);
        print_counts(&records[i].counts);
        (void)fputc('\n', stderr);
    }
    bool failed = summary.total.values[QA_ALLOCATION_GATE_MALLOC] ||
        summary.total.values[QA_ALLOCATION_GATE_CALLOC] ||
        summary.total.values[QA_ALLOCATION_GATE_REALLOC] ||
        summary.total.values[QA_ALLOCATION_GATE_CAPACITY_EXHAUSTIONS];
    (void)fprintf(stderr, "allocation_gate summary status=%s playing_frames=%llu measured_frames=%llu recorded_frames=%llu discarded_records=%llu",
        failed ? "fail" : "pass",
        (unsigned long long)summary.playing_frames, (unsigned long long)summary.measured_frames,
        (unsigned long long)summary.recorded_frames, (unsigned long long)summary.discarded_records);
    print_counts(&summary.total);
    (void)fputc('\n', stderr);
    (void)fputs("allocation_gate peak", stderr);
    print_counts(&summary.peak);
    (void)fputc('\n', stderr);
}
