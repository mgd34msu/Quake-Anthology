/*
 * Port of src/network/q2/connectionless.ts. Host callbacks are
 * fn + ctx (executeRcon is synchronous per PORTING; output flushes
 * like the TS finally block even on abort-free return).
 * ServerStatus/DiscoveryWire use the landed services/discovery
 * types (qa_server_status / qa_discovery_wire).
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CONNECTIONLESS_H
#define QA_NETWORK_Q2_CONNECTIONLESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/protocol.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/common/rcon_guard.h"
#include "qa/network/q2/handshake.h"
#include "qa/network/services/discovery.h"

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

/* TS readQ2Status: false is TS null (also false on OOM). The result
 * is the landed qa_server_status (free with qa_server_status_destroy;
 * wire is {kind:"source", protocol}). */
bool qa_q2_read_status(const qa_q2_connectionless_message *message,
                       qa_q2_protocol_identity protocol,
                       qa_server_status *out);

typedef qa_q2_status (*qa_q2_status_fn)(void *ctx);
/* Closure state for the landed qa_discovery_wire (caller-owned;
 * wire.ctx points at it). */
typedef struct qa_q2_discovery_wire_state {
  qa_q2_protocol_identity protocol;
  qa_q2_status_fn status;
  void *status_ctx;
} qa_q2_discovery_wire_state;

/* TS q2DiscoveryWire(protocol, status): fills a landed
 * qa_discovery_wire whose callbacks malloc their byte results. */
void qa_q2_discovery_wire_init(qa_discovery_wire *wire,
                               qa_q2_discovery_wire_state *state,
                               qa_q2_protocol_identity protocol,
                               qa_q2_status_fn status, void *ctx);

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
  /* TS rconGuards.get(host): the password-failure backoff of this host object. NULL is a host
   * object seen for the first time (a fresh guard for this call only, as when the TS caller
   * spreads a new host object per packet). */
  qa_rcon_guard *rcon_guard;
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
