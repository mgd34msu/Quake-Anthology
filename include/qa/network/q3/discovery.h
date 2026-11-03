/* Port of src/network/q3/discovery.ts over the shared discovery types:
 * DiscoveryWire is qa_discovery_wire (services/discovery.h), ServerStatus is
 * qa_server_status and master records are qa_net_address (common/endpoint.h).
 * The TS wire closure captures keywords/protocol; in C they live in a
 * caller-owned qa_q3_discovery_wire_state that wire.ctx points at. Throws are
 * qa_raise with the TS class name, so a catching caller recovers.
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_DISCOVERY_H
#define QA_NETWORK_Q3_DISCOVERY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/protocol.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/q3/admission.h"
#include "qa/network/services/discovery.h"
/* TS DiscoveryRequestKind, for the Q3 browser view's request surface. */
typedef enum qa_q3_discovery_request_kind { QA_Q3_DISCOVERY_INFO, QA_Q3_DISCOVERY_STATUS } qa_q3_discovery_request_kind;
/* Closure state of q3DiscoveryWire(keywords, protocol). */
typedef struct qa_q3_discovery_wire_state {
  char **keywords; size_t keyword_count;
  double protocol;
} qa_q3_discovery_wire_state;
/* TS q3DiscoveryWire(keywords = [], protocol = 68). Raises RangeError "Invalid Q3
 * master protocol" / "Q3 master keywords must be source words"; false only on OOM.
 * The wire's callbacks return malloc'd packets and raise what encodeConnectionlessText
 * raises. Release the state with qa_q3_discovery_wire_state_destroy. */
bool qa_q3_discovery_wire_init(qa_discovery_wire *wire, qa_q3_discovery_wire_state *state,
                               const char *const *keywords, size_t keyword_count, double protocol);
void qa_q3_discovery_wire_state_destroy(qa_q3_discovery_wire_state *state);
/* TS decodeQ3MasterPacket: raises RangeError "Not a Q3 master response"; the
 * malloc'd address list (possibly empty, *out may be NULL) is freed with
 * qa_q3_master_addresses_free. false only on OOM. */
bool qa_q3_decode_master_packet(const uint8_t *bytes, size_t len, qa_net_address **out, size_t *count, bool *complete);
/* TS decodeQ3MasterResponse. */
bool qa_q3_decode_master_response(const uint8_t *bytes, size_t len, qa_net_address **out, size_t *count);
void qa_q3_master_addresses_free(qa_net_address *addresses, size_t count);
/* TS decodeQ3ServerStatus: *challenge_out malloc'd, *status_out owned (free with
 * qa_server_status_destroy; wire {kind:"source", protocol: Q3_PROTOCOL}). Raises
 * RangeError for packets that are not Q3 browser responses or use another wire
 * version, and what q3InfoValue/tokenizeCommand raise; nothing leaks on a raise.
 * false only on OOM. */
bool qa_q3_decode_server_status(const uint8_t *bytes, size_t len, char **challenge_out, qa_server_status *status_out);
typedef struct qa_q3_status_player { int64_t score, ping; const char *name; } qa_q3_status_player;
bool qa_q3_encode_status(const char *info, const qa_q3_status_player *players, size_t count, uint8_t **out, size_t *out_len);
bool qa_q3_encode_info(const char *info, uint8_t **out, size_t *out_len);
#endif
