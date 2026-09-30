#ifndef QA_NETWORK_RELIABILITY_SAVE_H
#define QA_NETWORK_RELIABILITY_SAVE_H
#include "qa/network.h"

/* Complete native reliability state at a caller-owned idle boundary. Packet
 * output/delivery scratch is excluded; no transport or replay callback runs.
 * Restored policy must agree with the actual enclosing channel admission. */
bool qa_net_toggle_checkpoint(const qa_net_toggle *, qa_buffer *, qa_error *);
bool qa_net_toggle_restore_checkpoint(qa_bytes, size_t capacity, qa_net_toggle **, qa_error *);
bool qa_net_stopwait_checkpoint(const qa_net_stopwait *, qa_buffer *, qa_error *);
bool qa_net_stopwait_restore_checkpoint(qa_bytes, size_t capacity, size_t fragment_bytes,
    uint64_t retry_ns, qa_net_stopwait **, qa_error *);
bool qa_net_stopwait_limits(const qa_net_stopwait *, size_t *capacity,
    size_t *fragment_bytes, uint64_t *retry_ns);
#endif
