/* Port of src/network/q3/discovery.ts. GPL-2.0-or-later.
 * TS-owner mirrors: services/discovery.ts DiscoveryWire/ServerStatus and
 * common/endpoint.ts Ipv4Address (see admission.h) until those land. */
#ifndef QA_NETWORK_Q3_DISCOVERY_H
#define QA_NETWORK_Q3_DISCOVERY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/protocol.h"
#include "qa/network/q3/admission.h"
typedef enum qa_q3_discovery_request_kind { QA_Q3_DISCOVERY_INFO, QA_Q3_DISCOVERY_STATUS } qa_q3_discovery_request_kind;
/* Mirror of DiscoveryWire (closures capture keywords/protocol). */
typedef struct qa_q3_mirror_discovery_wire qa_q3_mirror_discovery_wire;
typedef qa_q3_mirror_discovery_wire qa_q3_discovery_wire;
bool qa_q3_discovery_wire_create(const char *const *keywords, size_t keyword_count, int64_t protocol, qa_q3_discovery_wire **out);
bool qa_q3_discovery_wire_create_default(qa_q3_discovery_wire **out);
void qa_q3_discovery_wire_destroy(qa_q3_discovery_wire *w);
bool qa_q3_discovery_wire_query(const qa_q3_discovery_wire *w, qa_q3_discovery_request_kind kind, const char *challenge, uint8_t **out, size_t *out_len);
bool qa_q3_discovery_wire_master_query(const qa_q3_discovery_wire *w, uint8_t **out, size_t *out_len);
bool qa_q3_discovery_wire_heartbeat(const qa_q3_discovery_wire *w, bool active, uint8_t **out, size_t *out_len);
bool qa_q3_decode_master_response(const uint8_t *bytes, size_t len, qa_q3_mirror_ipv4_address **out, size_t *count);
bool qa_q3_decode_master_packet(const uint8_t *bytes, size_t len, qa_q3_mirror_ipv4_address **out, size_t *count, bool *complete);
/* Mirror of ServerStatus. rules is an insertion-ordered key/value list. */
typedef struct qa_q3_mirror_rule { char *key, *value; } qa_q3_mirror_rule;
typedef struct qa_q3_mirror_player_detail { char *name; int64_t score, ping; } qa_q3_mirror_player_detail;
typedef struct qa_q3_mirror_server_status {
  char *name, *map; int64_t players, max_players;
  qa_q3_mirror_rule *rules; size_t rule_count;
  qa_q3_mirror_player_detail *player_details; size_t player_detail_count;
  const char *wire_kind; /* "source" */
  qa_q3_protocol_identity wire_protocol;
} qa_q3_mirror_server_status;
typedef qa_q3_mirror_server_status qa_q3_server_status;
void qa_q3_mirror_server_status_destroy(qa_q3_mirror_server_status *s);
bool qa_q3_decode_server_status(const uint8_t *bytes, size_t len, char **challenge_out, qa_q3_mirror_server_status *status_out);
typedef struct qa_q3_status_player { int64_t score, ping; const char *name; } qa_q3_status_player;
bool qa_q3_encode_status(const char *info, const qa_q3_status_player *players, size_t count, uint8_t **out, size_t *out_len);
bool qa_q3_encode_info(const char *info, uint8_t **out, size_t *out_len);
#endif
