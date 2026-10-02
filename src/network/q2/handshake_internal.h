#ifndef QA_Q2_HANDSHAKE_INTERNAL_H
#define QA_Q2_HANDSHAKE_INTERNAL_H
#include "qa/network_q2.h"
struct challenge_entry {
    qa_net_address address;
    int32_t value;
    uint64_t time;
};
struct qa_q2_challenges {
    struct challenge_entry *entries;
    size_t count, capacity;
    qa_q2_random_fn random;
    void *user;
};
#endif
