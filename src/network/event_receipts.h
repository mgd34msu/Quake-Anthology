#ifndef QA_NETWORK_EVENT_RECEIPTS_H
#define QA_NETWORK_EVENT_RECEIPTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { QA_EVENT_RECEIPTS_CAPACITY = 64 };
typedef struct qa_event_receipt {
    uint64_t first, after, token;
} qa_event_receipt;
typedef struct qa_event_receipts {
    qa_event_receipt spans[QA_EVENT_RECEIPTS_CAPACITY];
    size_t head, count;
    uint64_t through;
} qa_event_receipts;

/* Reset only with a new native channel or restored/reconfigured owner. */
void qa_event_receipts_reset(qa_event_receipts *, uint64_t first);
size_t qa_event_receipts_count(const qa_event_receipts *);
bool qa_event_receipts_full(const qa_event_receipts *);
/* Record an accepted submission. Token zero needs no reliable ACK. Repeated
 * tokens share a span. False is normal receipt capacity pressure. */
bool qa_event_receipts_submit(qa_event_receipts *, uint64_t first,
    uint64_t after, uint64_t token);
/* Poll the actual transport ACK, then return the first unretired event ID. */
uint64_t qa_event_receipts_retired(qa_event_receipts *, uint64_t acknowledged);

#endif
