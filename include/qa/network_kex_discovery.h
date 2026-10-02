#ifndef QA_NETWORK_KEX_DISCOVERY_H
#define QA_NETWORK_KEX_DISCOVERY_H

#include "qa/network_q2_kex.h"

typedef struct qa_kex_mdns_owner qa_kex_mdns_owner;
typedef struct qa_kex_mdns_hooks {
    void *context;
    bool (*found)(void *, const qa_net_address *, uint64_t now_ns, qa_error *);
} qa_kex_mdns_hooks;

/* Zero advertised_port creates a browser. A host advertises its real game
 * socket port. Hooks borrow their context until the owner is destroyed. */
bool qa_kex_mdns_owner_open(uint16_t advertised_port, const qa_kex_mdns_hooks *,
                            qa_kex_mdns_owner **, qa_error *);
bool qa_kex_mdns_owner_query(qa_kex_mdns_owner *, qa_error *);
bool qa_kex_mdns_owner_pump(qa_kex_mdns_owner *, uint64_t now_ns, qa_error *);
bool qa_kex_mdns_owner_shutdown(qa_kex_mdns_owner *, qa_error *);
void qa_kex_mdns_owner_destroy(qa_kex_mdns_owner *);
bool qa_kex_mdns_owner_idle(const qa_kex_mdns_owner *);

/* Decode is detached: no socket, interface enumeration, callbacks, queries or
 * announcements run. Activate opens only the socket. Publish marks the real
 * activated owner for an initial announcement on its next pump; it allocates
 * nothing and sends nothing during enclosing candidate publication. */
bool qa_kex_mdns_owner_checkpoint(const qa_kex_mdns_owner *, qa_buffer *, qa_error *);
bool qa_kex_mdns_owner_restore(qa_bytes, const qa_kex_mdns_hooks *,
                               qa_kex_mdns_owner **, qa_error *);
bool qa_kex_mdns_owner_activate(qa_kex_mdns_owner *, qa_error *);
bool qa_kex_mdns_owner_publish(qa_kex_mdns_owner *, qa_error *);
/* A complete saved logical cut admits the real socket/publication transfer.
 * The retired owner closes quietly; no multicast query or announcement runs. */
bool qa_kex_mdns_owner_handoff_ready(const qa_kex_mdns_owner *active,
    const qa_kex_mdns_owner *candidate, qa_error *);
bool qa_kex_mdns_owner_handoff(qa_kex_mdns_owner *active,
    qa_kex_mdns_owner *candidate, qa_error *);

#endif
