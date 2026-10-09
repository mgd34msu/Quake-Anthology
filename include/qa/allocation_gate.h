#ifndef QA_ALLOCATION_GATE_H
#define QA_ALLOCATION_GATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { QA_ALLOCATION_GATE_WARMUP = 120, QA_ALLOCATION_GATE_RECORDS = 4096 };
typedef enum qa_allocation_gate_counter {
    QA_ALLOCATION_GATE_MALLOC,
    QA_ALLOCATION_GATE_CALLOC,
    QA_ALLOCATION_GATE_REALLOC,
    QA_ALLOCATION_GATE_FREE,
    QA_ALLOCATION_GATE_BYTES,
    QA_ALLOCATION_GATE_NULL_RESULTS,
    QA_ALLOCATION_GATE_SIZE_OVERFLOWS,
    QA_ALLOCATION_GATE_COUNTERS
} qa_allocation_gate_counter;
typedef struct qa_allocation_gate_counts {
    uint64_t values[QA_ALLOCATION_GATE_COUNTERS];
} qa_allocation_gate_counts;
typedef struct qa_allocation_gate_record {
    uint64_t frame;
    qa_allocation_gate_counts counts;
    bool succeeded;
} qa_allocation_gate_record;
typedef struct qa_allocation_gate_summary {
    uint64_t playing_frames, measured_frames, recorded_frames, discarded_records;
    qa_allocation_gate_counts total, peak;
} qa_allocation_gate_summary;

/* Enabled diagnostic links observe engine/static-object calls, including worker
 * threads. DSO/libc-internal calls and other allocation APIs are not wrapped.
 * Bytes are attempted requested sizes, not live/retained memory. */
void qa_allocation_gate_begin(void);
qa_allocation_gate_counts qa_allocation_gate_end(bool playing, bool succeeded);
const qa_allocation_gate_summary *qa_allocation_gate_read(void);
const qa_allocation_gate_record *qa_allocation_gate_records(void);
void qa_allocation_gate_report(void);

#endif
