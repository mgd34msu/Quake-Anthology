#ifndef QA_NETWORK_Q2_SESSION_SAVE_H
#define QA_NETWORK_Q2_SESSION_SAVE_H
#include "qa/network_q2_session.h"
#include "qa/source_save.h"

/* Pure recipe fields. Optional admits only a literal allzero pending policy;
 * selected policies have genuine CLIENT channel and command extents. This
 * nominal boundary rejects process reader/context pointers. */
bool qa_network_q2_save_client_policy(qa_source_save_io *, qa_network_q2_client_policy *, bool optional);

/* Actual graph identities, independent of host pointers and socket owners.
 * Decode borrows the isolated candidate graph; the session retains only its
 * immutable resource, while the actual Source owns the VFS view lifetime. */
typedef struct qa_network_q2_checkpoint_refs {
    void *context;
    bool (*source_server)(void *, qa_network_runtime *, const qa_net_client *, qa_network_q2_server_policy *,
        qa_network_q2_server_hooks *, qa_error *);
    bool (*source_client)(void *, qa_network_runtime *, const qa_net_client *, qa_network_q2_client_policy *,
        qa_network_q2_client_hooks *, qa_error *);
    bool (*view_encode)(void *, const qa_vfs *, uint64_t *, qa_error *);
    bool (*view_decode)(void *, uint64_t, qa_vfs **, qa_error *);
    bool (*resource_encode)(void *, const qa_resource *, uint64_t *pool, uint64_t *resource, qa_error *);
    bool (*resource_decode)(void *, uint64_t pool, uint64_t resource, const qa_resource **, qa_error *);
} qa_network_q2_checkpoint_refs;

/* These wire owners reconstruct no gameplay, admission, Source startup or
 * publication. Source callbacks are rebound by the enclosing candidate owner.
 * Peer installation belongs to the sole generic runtime save writer. */
struct qa_network_peer;
bool qa_network_q2_checkpoint_peer(const struct qa_network_peer *,
    const qa_network_q2_checkpoint_refs *, bool *server, qa_buffer *, qa_error *);
bool qa_network_q2_restore_peer(qa_network_runtime *, const qa_net_client *,
    bool server, qa_bytes, const qa_network_q2_checkpoint_refs *,
    struct qa_network_peer *, qa_error *);
#endif
