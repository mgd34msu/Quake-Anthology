#ifndef QA_POOL_H
#define QA_POOL_H

#include "qa/arena.h"

/* One owner; fixed capacity. The caller's arena owns backing storage and must
 * outlive all slots. Payload survives release/reuse; callers initialize it.
 * Slot generations and domain identity belong to callers, not this pool. */
typedef struct qa_pool {
    size_t stride, capacity;
    uint8_t *values;
    size_t *next;
    size_t head, active, peak, overflow;
} qa_pool;

/* Cold preparation; stride rounds up to alignment. Zero capacity is valid and
 * uses no arena storage. Failure leaves the destination pool unchanged. */
bool qa_pool_prepare(qa_pool *, qa_arena *, size_t count, size_t stride,
    size_t alignment, qa_error *);
/* O(1), no allocation. Exhaustion returns NULL, sets slot=SIZE_MAX and counts
 * overflow (saturating). Existing leases remain valid. */
void *qa_pool_take(qa_pool *, size_t *slot);
/* Runtime callers supply their already-owned slot; no separate admission or
 * generation checks. Releasing a slot never changes another leased payload. */
void qa_pool_release(qa_pool *, size_t slot);
/* At a drained owner boundary, invalidate all leases and retain payloads. */
void qa_pool_reset(qa_pool *);
/* Contiguous pages for the common arena. Slots keep the same lifetime and
 * exhaustion accounting as single-slot leases. */
void *qa_pool_take_run(qa_pool *, size_t count, size_t *slot);
void qa_pool_release_run(qa_pool *, size_t slot, size_t count);
void *qa_pool_at(const qa_pool *, size_t slot);

#endif
