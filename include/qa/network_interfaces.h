#ifndef QA_NETWORK_INTERFACES_H
#define QA_NETWORK_INTERFACES_H

#include "qa/network.h"

typedef struct qa_net_interfaces qa_net_interfaces;
typedef struct qa_net_interface {
    const char *name;
    uint32_t index;
    bool up, running, loopback, has_netmask;
    qa_net_address address, netmask;
} qa_net_interface;

/* Native enumeration owns the exact OS-ordered unicast address records and
 * masks. A replacement snapshot is published only after full enumeration. */
bool qa_net_interfaces_capture(qa_net_interfaces **, qa_error *);
void qa_net_interfaces_destroy(qa_net_interfaces *);
size_t qa_net_interfaces_count(const qa_net_interfaces *);
const qa_net_interface *qa_net_interfaces_at(const qa_net_interfaces *, size_t);
/* Supply the actual socket's IPV6_V6ONLY policy. Qualification requires a
 * native enumeration, and uses its masks and scope. Restored records describe
 * historical state; capture a fresh native snapshot before qualification. */
bool qa_net_interfaces_local(const qa_net_interfaces *, const qa_net_address *bound,
                              bool ipv6_only, const qa_net_address *remote);
bool qa_net_interfaces_checkpoint(const qa_net_interfaces *, qa_buffer *, qa_error *);
bool qa_net_interfaces_restore(qa_bytes, qa_net_interfaces **, qa_error *);

#endif
