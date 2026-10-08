#ifndef QA_APPLICATION_EVENT_PAGES_H
#define QA_APPLICATION_EVENT_PAGES_H

#include "qa/common.h"

typedef struct application_event_pages application_event_pages;
typedef struct application_event_lease application_event_lease;

typedef struct application_event_pages_capacity {
    size_t total_bytes, available_bytes, free_pages;
    size_t total_records, available_records, retired_leased_records;
} application_event_pages_capacity;

typedef struct application_event_transaction {
    application_event_pages *owner;
    application_event_lease *lease;
    size_t first_page, last_page;
    size_t original_tail, original_used, original_next;
    bool active, blocked;
} application_event_transaction;

/* All payload, page metadata and record slots are allocated at load. One
 * writer owns begin through commit/abort; retire and lease release run outside
 * that interval. Records and their payloads stay at fixed addresses. */
application_event_pages *application_event_pages_create(size_t payload_bytes,
    size_t page_bytes, size_t record_capacity, qa_error *);
/* Retained leases keep their pages and owner alive after destruction. */
void application_event_pages_destroy(application_event_pages **);

/* False/zero is ordinary capacity backpressure and does not set an error.
 * A blocked allocation makes the whole transaction abort on commit. */
bool application_event_pages_begin(application_event_pages *, application_event_transaction *);
/* Matches qa_unified_clone_alloc_fn. Bytes are uninitialized. Requests may
 * span contiguous pages; size is positive and alignment is a power of two. */
void *application_event_pages_alloc(void *transaction, size_t size, size_t alignment, qa_error *);
uint64_t application_event_pages_commit(application_event_transaction *, void *record);
void application_event_pages_abort(application_event_transaction *);

uint64_t application_event_pages_first(const application_event_pages *);
uint64_t application_event_pages_next(const application_event_pages *);
const void *application_event_pages_at(const application_event_pages *, uint64_t id);
/* next is the minimum retirement cursor of subscribed consumers. Only the
 * contiguous prefix with id < next is retired. Submission is not an ACK. */
void application_event_pages_retire(application_event_pages *, uint64_t next);

/* A lease pins every payload page of this record without retaining its lookup
 * slot. Persistent/display owners release it when their own lifetime ends. */
application_event_lease *application_event_pages_retain(application_event_pages *, uint64_t id);
void application_event_lease_retain(application_event_lease *);
void application_event_lease_release(application_event_lease *);
uint64_t application_event_lease_id(const application_event_lease *);
const void *application_event_lease_record(const application_event_lease *);

/* Diagnostic occupancy, including alignment and pages pinned by leases. */
size_t application_event_pages_bytes_used(const application_event_pages *);
/* O(1). Available bytes include completely free pages and the append tail;
 * unused space in older pinned pages cannot admit a new allocation. */
void application_event_pages_capacity_read(const application_event_pages *,
    application_event_pages_capacity *);
/* A pressure-only diagnostic; scans the fixed page metadata. */
size_t application_event_pages_contiguous_bytes(const application_event_pages *);

#endif
