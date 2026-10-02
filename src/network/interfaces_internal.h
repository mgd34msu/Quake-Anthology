#ifndef QA_NETWORK_INTERFACES_INTERNAL_H
#define QA_NETWORK_INTERFACES_INTERNAL_H

#include "qa/network_interfaces.h"

struct qa_net_interfaces {
    qa_net_interface *entries;
    size_t count;
    bool native_enumerated;
};

bool qa_net_interfaces_append(qa_net_interfaces *, const qa_net_interface *, qa_error *);
bool qa_net_interfaces_valid(const qa_net_interfaces *);

#endif
