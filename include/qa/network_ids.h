#ifndef QA_NETWORK_IDS_H
#define QA_NETWORK_IDS_H

#include "qa/common.h"

typedef struct qa_net_client_id { uint64_t owner, generation; uint32_t slot; } qa_net_client_id;
typedef struct qa_net_seat_id { uint64_t owner; uint32_t index; } qa_net_seat_id;

#endif
