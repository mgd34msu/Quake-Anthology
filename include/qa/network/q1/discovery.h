/* Port of src/network/q1/discovery.ts. Wire callbacks aborts on the TS
 * throw paths (masterQuery/heartbeat). GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_DISCOVERY_H
#define QA_NETWORK_Q1_DISCOVERY_H
#include <stddef.h>
#include <stdint.h>
#include "qa/network/services/discovery.h"
qa_discovery_wire qa_netquake_discovery_wire(void);
void qa_read_netquake_discovery(const uint8_t *bytes, size_t length, qa_server_status *out);
qa_discovery_wire qa_quakeworld_discovery_wire(void);
void qa_read_quakeworld_discovery(const uint8_t *bytes, size_t length, qa_server_status *out);
#endif
