#ifndef QA_NETWORK_Q2_UNICAST_H
#define QA_NETWORK_Q2_UNICAST_H
#include "qa/network_runtime.h"
#include "qa/source_save.h"

typedef struct qa_q2_unicast_claim {
    qa_net_client_id client;
    uint64_t connection_epoch;
    qa_actor_owner source;
    qa_sha256_digest map;
    uint64_t source_frame, source_time_ns;
    uint32_t key;
} qa_q2_unicast_claim;
typedef struct qa_q2_unicast_cache qa_q2_unicast_cache;
bool qa_q2_unicast_cache_create(qa_q2_unicast_cache **, qa_error *);
void qa_q2_unicast_cache_destroy(qa_q2_unicast_cache *);
/* Check is pure. Remember follows successful Source packet publication. */
bool qa_q2_unicast_check(const qa_q2_unicast_cache *, const qa_q2_unicast_claim *, bool *, qa_error *);
/* Reserve before publication so remembering its accepted key cannot allocate. */
bool qa_q2_unicast_reserve(qa_q2_unicast_cache *, qa_error *);
bool qa_q2_unicast_remember(qa_q2_unicast_cache *, const qa_q2_unicast_claim *, qa_error *);
void qa_q2_unicast_remove_client(qa_q2_unicast_cache *, qa_net_client_id);
void qa_q2_unicast_remove_source(qa_q2_unicast_cache *, qa_actor_owner);

typedef struct qa_q2_unicast_refs {
    void *context;
    bool (*client_encode)(void *, qa_net_client_id, uint64_t *saved, qa_error *);
    bool (*client_decode)(void *, uint64_t saved, qa_net_client_id *, qa_error *);
    bool (*source_encode)(void *, qa_actor_owner, uint64_t *saved, qa_error *);
    bool (*source_decode)(void *, uint64_t saved, qa_actor_owner *, qa_error *);
    /* Qualifies actual current connection epoch and Source map/frame domain.
     * A retired connection is absent; no admission or Source call occurs. */
    bool (*current)(void *, const qa_q2_unicast_claim *, bool *present, qa_error *);
} qa_q2_unicast_refs;
bool qa_q2_unicast_capture(const qa_q2_unicast_cache *, const qa_q2_unicast_refs *, qa_buffer *, qa_error *);
bool qa_q2_unicast_restore(qa_bytes, const qa_q2_unicast_refs *, qa_q2_unicast_cache **empty, qa_error *);
#endif
