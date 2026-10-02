#ifndef QA_NETWORK_KEX_TRANSPORT_H
#define QA_NETWORK_KEX_TRANSPORT_H

#include "qa/network_kex_discovery.h"

typedef struct qa_kex_transport qa_kex_transport;
typedef struct qa_kex_transport_hooks {
    void *context;
    /* The actual raw receive owner invokes shared browser/admin services here.
     * A recognized packet never enters the lobby or Q2 game channel. */
    bool (*connectionless)(void *, const qa_net_datagram *, bool *recognized, qa_error *);
} qa_kex_transport_hooks;

/* Raw transport transfers only on success. The returned transport owns the
 * same LAN game stream and, for a native IP host, its mDNS advertisement. */
bool qa_kex_transport_open(qa_net_transport *, const qa_kex_lan_options *,
                           const qa_kex_transport_hooks *, qa_net_transport **,
                           qa_kex_transport **borrowed_control, qa_error *);
qa_kex_lan *qa_kex_transport_lobby(qa_kex_transport *);
bool qa_kex_transport_idle(const qa_kex_transport *);
bool qa_kex_transport_udp_policy(const qa_kex_transport *, qa_net_udp_policy *, bool *present, qa_error *);
/* Shared services send through the actual raw socket, including while the
 * receive callback is entered. Game sends use the returned LAN transport. */
bool qa_kex_transport_send_connectionless(qa_kex_transport *,
    const qa_net_address *, qa_bytes, qa_error *);

/* Restore creates only detached owners and generic dispatch containers.
 * Binding takes a genuine matching raw endpoint after successful native mDNS
 * activation. Publication is allocation-free and emits on the next pump. */
bool qa_kex_transport_checkpoint(const qa_kex_transport *, qa_buffer *, qa_error *);
bool qa_kex_transport_restore(qa_bytes, const qa_kex_transport_hooks *,
    qa_net_transport **, qa_kex_transport **borrowed_control, qa_error *);
bool qa_kex_transport_bind(qa_kex_transport *, qa_net_transport *, qa_error *);
bool qa_kex_transport_publish(qa_kex_transport *, qa_error *);
/* Preflight compares the complete saved owners. Handoff transfers the actual
 * live raw socket and mDNS publication; retired cleanup emits no disconnect. */
bool qa_kex_transport_handoff_ready(const qa_kex_transport *, const qa_kex_transport *, qa_error *);
bool qa_kex_transport_handoff(qa_kex_transport *active, qa_kex_transport *candidate, qa_error *);

#endif
