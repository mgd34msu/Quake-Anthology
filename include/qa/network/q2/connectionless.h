/*
 * Port of src/network/q2/connectionless.ts. Host callbacks are
 * fn + ctx (executeRcon is synchronous per PORTING; output flushes
 * like the TS finally block even on abort-free return).
 * Mirrors (TS-owner: src/network/services/discovery.ts, not yet
 * landed in C): qa_q2_mirror_server_status and
 * qa_q2_mirror_discovery_wire; remove when services/discovery lands.
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CONNECTIONLESS_H
#define QA_NETWORK_Q2_CONNECTIONLESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/protocol.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/q2/handshake.h"

/* TS Q2Status player row (borrowed name on input). */
typedef struct qa_q2_status_player {
  double score;
  double ping;
  const char *name;
} qa_q2_status_player;

/* TS Q2Status (borrowed strings on input). */
typedef struct qa_q2_status {
  const char *server_info;
  const qa_q2_status_player *players;
  size_t player_count;
} qa_q2_status;

/* TS q2StatusText(status, maximumBytes=1384): malloc'd or NULL. */
char *qa_q2_status_text(const qa_q2_status *status, bool has_maximum, double maximum);

/* --- Mirror of ServerStatus (services/discovery.ts owner). --- */
typedef struct qa_q2_mirror_rule {
  char *key;
  char *value;
} qa_q2_mirror_rule;
typedef struct qa_q2_mirror_player_detail {
  char *name;
  double score;
  double ping;
} qa_q2_mirror_player_detail;
typedef struct qa_q2_mirror_server_status {
  char *name;
  char *map;
  double players;
  double max_players;
  qa_q2_mirror_rule *rules;
  size_t rule_count;
  qa_q2_mirror_player_detail *player_details;
  size_t player_detail_count;
  qa_q2_protocol_identity protocol; /* TS wire {kind:"source"} */
} qa_q2_mirror_server_status;

void qa_q2_mirror_server_status_free(qa_q2_mirror_server_status *status);
/* TS readQ2Status: false is TS null (also false on OOM). */
bool qa_q2_read_status(const qa_q2_connectionless_message *message,
                       qa_q2_protocol_identity protocol,
                       qa_q2_mirror_server_status *out);

/* --- Mirror of DiscoveryWire (services/discovery.ts owner). --- */
typedef enum qa_q2_discovery_query_kind {
  QA_Q2_DISCOVERY_INFO, QA_Q2_DISCOVERY_STATUS
} qa_q2_discovery_query_kind;
typedef qa_q2_status (*qa_q2_status_fn)(void *ctx);
typedef struct qa_q2_mirror_discovery_wire {
  qa_q2_protocol_identity protocol;
  qa_q2_status_fn status;
  void *status_ctx;
} qa_q2_mirror_discovery_wire;

/* TS q2DiscoveryWire + its three closures (malloc'd or NULL). */
void qa_q2_discovery_wire_init(qa_q2_mirror_discovery_wire *wire,
                               qa_q2_protocol_identity protocol, qa_q2_status_fn status,
                               void *ctx);
uint8_t *qa_q2_discovery_wire_query(const qa_q2_mirror_discovery_wire *wire,
                                    qa_q2_discovery_query_kind kind, size_t *out_length);
uint8_t *qa_q2_discovery_wire_master_query(const qa_q2_mirror_discovery_wire *wire,
                                           size_t *out_length);
uint8_t *qa_q2_discovery_wire_heartbeat(const qa_q2_mirror_discovery_wire *wire,
                                        bool active, size_t *out_length);

/* TS readQ2MasterReply: NULL is TS null; otherwise malloc'd list. */
qa_net_address *qa_q2_read_master_reply(const uint8_t *bytes, size_t length,
                                        size_t *out_count);
void qa_q2_master_reply_free(qa_net_address *addresses, size_t count);

/* TS Q2ConnectAdmission. */
typedef struct qa_q2_connect_admission {
  bool accepted;
  char *response_arguments; /* accepted; owned, may be NULL */
  char *reason;             /* rejected; owned */
} qa_q2_connect_admission;

void qa_q2_connect_admission_free(qa_q2_connect_admission *admission);

/* TS host info() result. */
typedef struct qa_q2_host_info {
  const char *name;
  const char *map;
  double players;
  double max_players;
} qa_q2_host_info;

/* TS Q2ConnectionlessHost. */
typedef struct qa_q2_connectionless_host {
  void *ctx;
  bool profile_rerelease; /* false is "classic" */
  const qa_q2_protocol_identity *protocols;
  size_t protocol_count;
  qa_q2_status (*status)(void *ctx);
  qa_q2_host_info (*info)(void *ctx);
  qa_q2_connect_admission (*connect)(void *ctx, const qa_net_address *from,
                                     const qa_q2_connect_request *request);
  void (*reply)(void *ctx, const qa_net_address *to, const uint8_t *bytes,
                size_t length);
  const char *(*rcon_password)(void *ctx);
  /* limited_rcon returns false for TS null; prefixes borrowed. */
  bool (*limited_rcon)(void *ctx, const char **password, const char ***prefixes,
                       size_t *prefix_count);
  bool (*rcon_rate_allowed)(void *ctx, double now);
  void (*recharge_rcon_rate)(void *ctx);
  void (*execute_rcon)(void *ctx, const char *command, bool limited,
                       void (*output)(void *octx, const char *text), void *output_ctx);
} qa_q2_connectionless_host;

/* TS q2InfoText: malloc'd or NULL. */
char *qa_q2_info_text(qa_q2_host_info info, const qa_q2_protocol_identity *protocols,
                      size_t protocol_count, double version);

/* TS Q2ConnectionlessServer. */
typedef struct qa_q2_connectionless_server {
  const qa_q2_connectionless_host *host;
  qa_q2_challenge_table *challenges;
} qa_q2_connectionless_server;

bool qa_q2_connectionless_server_receive(qa_q2_connectionless_server *server,
                                         const qa_net_address *from,
                                         const uint8_t *bytes, size_t length,
                                         double now);

/* TS handleQ2Rcon (host is the rcon subset; same struct). */
void qa_q2_handle_rcon(const qa_q2_connectionless_host *host,
                       const qa_net_address *from,
                       const qa_q2_connectionless_message *message, double now);

#endif
