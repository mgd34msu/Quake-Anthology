#ifndef QA_EVENT_RING_H
#define QA_EVENT_RING_H

#include "qa/common.h"

typedef struct qa_event_ring qa_event_ring;
typedef struct qa_event_lease qa_event_lease;

typedef struct qa_event_capacity {
    size_t total_bytes, available_bytes, free_pages;
    size_t total_records, available_records, retired_leased_records;
} qa_event_capacity;

typedef struct qa_event_transaction {
    qa_event_ring *owner;
    qa_event_lease *lease;
    size_t first_page, last_page;
    size_t original_tail, original_used, original_next;
    bool active, blocked;
} qa_event_transaction;

/* All payload, page metadata and record slots are allocated at load. One
 * writer owns begin through commit/abort; retire and lease release run outside
 * that interval. Records and their payloads stay at fixed addresses. */
qa_event_ring *qa_event_ring_create(size_t payload_bytes,
    size_t page_bytes, size_t record_capacity, qa_error *);
/* Retained leases keep their pages and owner alive after destruction. */
void qa_event_ring_destroy(qa_event_ring **);

/* False/zero is ordinary capacity backpressure and does not set an error.
 * A blocked allocation makes the whole transaction abort on commit. */
bool qa_event_ring_begin(qa_event_ring *, qa_event_transaction *);
/* Matches qa_unified_clone_alloc_fn. Bytes are uninitialized. Requests may
 * span contiguous pages; size is positive and alignment is a power of two. */
void *qa_event_ring_alloc(void *transaction, size_t size, size_t alignment, qa_error *);
uint64_t qa_event_ring_commit(qa_event_transaction *, void *record);
void qa_event_ring_abort(qa_event_transaction *);

uint64_t qa_event_ring_first(const qa_event_ring *);
uint64_t qa_event_ring_next(const qa_event_ring *);
const void *qa_event_ring_at(const qa_event_ring *, uint64_t id);
/* next is the minimum retirement cursor of subscribed consumers. Only the
 * contiguous prefix with id < next is retired. Submission is not an ACK. */
void qa_event_ring_retire(qa_event_ring *, uint64_t next);

/* A lease pins every payload page of this record without retaining its lookup
 * slot. Persistent/display owners release it when their own lifetime ends. */
qa_event_lease *qa_event_ring_retain(qa_event_ring *, uint64_t id);
void qa_event_lease_retain(qa_event_lease *);
void qa_event_lease_release(qa_event_lease *);
uint64_t qa_event_lease_id(const qa_event_lease *);
const void *qa_event_lease_record(const qa_event_lease *);

/* Diagnostic occupancy, including alignment and pages pinned by leases. */
size_t qa_event_ring_bytes_used(const qa_event_ring *);
/* O(1). Available bytes include completely free pages and the append tail;
 * unused space in older pinned pages cannot admit a new allocation. */
void qa_event_capacity_read(const qa_event_ring *,
    qa_event_capacity *);
/* A pressure-only diagnostic; scans the fixed page metadata. */
size_t qa_event_ring_contiguous_bytes(const qa_event_ring *);

#endif
