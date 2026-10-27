/*
 * Port of src/network/q2/kex/lan.ts (KexLanTransport). The generic
 * transport becomes the landed common vtable over qa_net_address;
 * as_transport exposes the same vtable. Lobby ids are uint64;
 * attribute maps are ordered owned lists. Malformed datagrams are
 * dropped like the TS catch block (checked pre-dispatch), while
 * protocol errors inside admitted messages still abort.
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_KEX_LAN_H
#define QA_NETWORK_Q2_KEX_LAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/common/endpoint.h"
#include "qa/network/common/transport.h"
#include "qa/network/q2/kex/channel.h"
#include "qa/network/q2/kex/discovery.h"

#define QA_KEX_LAN_PORT 5069

/* One attribute map pair (owned). */
typedef struct qa_kex_lobby_attr {
  char *key;
  char *value;
} qa_kex_lobby_attr;

/* TS KexLobbyPlayer. */
typedef struct qa_kex_lobby_player {
  uint64_t id;
  qa_kex_lobby_attr *attributes; /* owned */
  size_t attribute_count, attribute_capacity;
} qa_kex_lobby_player;

/* TS KexLanOptions (host fields unused for clients). */
typedef struct qa_kex_lan_options {
  bool role_host;
  double max_players;
  double local_players;
  const char *name;          /* host */
  qa_net_address server;     /* client (borrowed; cloned) */
} qa_kex_lan_options;

/* One peer (channel stored by pointer for stable emit binding). */
typedef struct qa_kex_lan_peer {
  qa_net_address address; /* owned */
  qa_kex_channel *channel; /* owned */
  uint64_t *players; /* owned */
  size_t player_count, player_capacity;
  bool dropped; /* set when removed during iteration */
} qa_kex_lan_peer;

/* TS KexLanTransport. */
typedef struct qa_kex_lan_transport {
  qa_datagram_transport *transport; /* borrowed */
  qa_kex_lan_options options;
  qa_net_address server; /* owned client clone */
  qa_packet_queue queue;
  qa_kex_lan_peer *peers; /* owned */
  size_t peer_count, peer_capacity;
  qa_kex_lobby_player *players; /* owned */
  size_t player_count, player_capacity;
  qa_kex_lobby_attr *attributes; /* owned */
  size_t attribute_count, attribute_capacity;
  uint64_t next_id;
  double clock;
  double retry_at;
  bool joined;
  bool ended;
  long long unsubscribe_id;
  qa_kex_mdns discovery;
  bool has_discovery;
} qa_kex_lan_transport;

/* False only on allocation/transport failure. */
bool qa_kex_lan_transport_init(qa_kex_lan_transport *lan,
                               qa_datagram_transport *transport,
                               const qa_kex_lan_options *options);
void qa_kex_lan_transport_destroy(qa_kex_lan_transport *lan);
/* TS getters (get_ infix). */
int qa_kex_lan_get_address(const qa_kex_lan_transport *lan, qa_net_address *out);
bool qa_kex_lan_get_closed(const qa_kex_lan_transport *lan);
bool qa_kex_lan_get_ready(const qa_kex_lan_transport *lan);
const qa_kex_lobby_player *qa_kex_lan_get_lobby_players(
    const qa_kex_lan_transport *lan, size_t *out_count);
bool qa_kex_lan_admitted(const qa_kex_lan_transport *lan, const qa_net_address *address);
void qa_kex_lan_tick(qa_kex_lan_transport *lan, double now);
/* TS DatagramTransport surface. */
bool qa_kex_lan_send(qa_kex_lan_transport *lan, const qa_net_address *to,
                     const uint8_t *payload, size_t length);
int qa_kex_lan_poll(qa_kex_lan_transport *lan, qa_receive_event *out);
long long qa_kex_lan_subscribe(qa_kex_lan_transport *lan, qa_net_listener_fn fn,
                               void *ctx);
void qa_kex_lan_unsubscribe(qa_kex_lan_transport *lan, long long id);
void qa_kex_lan_close(qa_kex_lan_transport *lan);
void qa_kex_lan_set_attribute(qa_kex_lan_transport *lan, const char *key,
                              const char *value);
/* Vtable view of this transport (TS implements DatagramTransport). */
qa_datagram_transport qa_kex_lan_as_transport(qa_kex_lan_transport *lan);

#endif
