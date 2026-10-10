#ifndef QA_NETWORK_UNIFIED_FRAME_POOL_H
#define QA_NETWORK_UNIFIED_FRAME_POOL_H

#include "qa/common.h"

typedef struct qa_unified_frame_pool qa_unified_frame_pool;
typedef struct qa_unified_frame_lease qa_unified_frame_lease;

/* Load-sized shared pages and lease slots; custody stays on the owning
 * network/session thread. Zero selects 96 MiB and 128 leases. Pages are reused
 * across leases, without reserving the maximum frame size for each slot. */
qa_unified_frame_pool *qa_unified_frame_pool_create(size_t bytes, size_t leases, qa_error *);
/* Retires the owner; retained leases remain valid until their final release. */
void qa_unified_frame_pool_destroy(qa_unified_frame_pool **);
qa_unified_frame_lease *qa_unified_frame_lease_acquire(qa_unified_frame_pool *, qa_error *);
bool qa_unified_frame_lease_retain(qa_unified_frame_lease *, qa_error *);
void qa_unified_frame_lease_release(qa_unified_frame_lease *);
/* Successful requested bytes, excluding alignment padding and retained blocks. */
size_t qa_unified_frame_lease_used(const qa_unified_frame_lease *);
/* Zeroes only count * stride requested bytes. A zero count needs no storage.
 * Addresses stay fixed until the final lease release. */
void *qa_unified_frame_lease_alloc(qa_unified_frame_lease *, size_t count,
    size_t stride, size_t alignment, qa_error *);
void *qa_unified_frame_lease_alloc_callback(void *, size_t, size_t, qa_error *);

#endif
