#ifndef QA_NETWORK_KEX_STATUS_H
#define QA_NETWORK_KEX_STATUS_H

#include "qa/network_q2_kex.h"

typedef struct qa_kex_status_attribute {
    qa_bytes key, value;
} qa_kex_status_attribute;
typedef struct qa_kex_status_view {
    qa_bytes name;
    uint8_t players, max_players;
    qa_kex_status_attribute attributes[256];
    size_t attribute_count;
} qa_kex_status_view;

/* Counted text borrows the genuine unframed retail datagram. Map replacement
 * preserves insertion order; strings retain embedded NUL and full extents. */
bool qa_kex_status_read(qa_bytes, qa_kex_status_view *, qa_error *);
qa_bytes qa_kex_status_value(const qa_kex_status_view *, const char *key);

#endif
