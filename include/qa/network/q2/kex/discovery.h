/*
 * Port of src/network/q2/kex/discovery.ts: KEX lobby query/status
 * plus mDNS DNS-SD (KexMdns). The Node dgram socket becomes a POSIX
 * UDP multicast socket pumped by qa_kex_mdns_poll (PORTING async
 * rule); hostname/interfaces come from gethostname/getifaddrs.
 * ServerStatus is the landed qa_server_status. found/failed are
 * fn + ctx (failed gets owned message text
 * copied as a borrowed C string). One divergence: a malformed
 * multicast packet aborts with its TS message instead of being
 * swallowed by the TS socket handler's catch (noted like the GTV
 * broadcast port). GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_KEX_DISCOVERY_H
#define QA_NETWORK_Q2_KEX_DISCOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/common/endpoint.h"
#include "qa/network/q2/connectionless.h"

/* TS kexDiscoveryQuery / readKexDiscovery. */
uint8_t *qa_kex_discovery_query(size_t *out_length);
bool qa_kex_read_discovery(const uint8_t *bytes, size_t length,
                           qa_server_status *out);

typedef void (*qa_kex_mdns_found_fn)(void *ctx, const qa_net_address *address);
typedef void (*qa_kex_mdns_failed_fn)(void *ctx, const char *message);

/* Endpoint/address map entries. */
typedef struct qa_kex_mdns_endpoint {
  char *name;  /* owned */
  char *target; /* owned */
  double port;
} qa_kex_mdns_endpoint;
typedef struct qa_kex_mdns_address {
  char *name; /* owned, lower-case */
  qa_net_address address;
} qa_kex_mdns_address;

/* TS KexMdns (socket owned; pump with qa_kex_mdns_poll). */
typedef struct qa_kex_mdns {
  int socket_fd;
  bool closed;
  bool has_advertised_port;
  double advertised_port;
  qa_kex_mdns_found_fn found;
  void *found_ctx;
  qa_kex_mdns_failed_fn failed;
  void *failed_ctx;
  qa_kex_mdns_endpoint *endpoints; /* owned */
  size_t endpoint_count, endpoint_capacity;
  qa_kex_mdns_address *addresses; /* owned */
  size_t address_count, address_capacity;
} qa_kex_mdns;

/* TS KexMdns.open (synchronous). False only on bind/socket failure. */
bool qa_kex_mdns_open(qa_kex_mdns *mdns, bool has_advertised_port, double advertised_port,
                      qa_kex_mdns_found_fn found, void *found_ctx,
                      qa_kex_mdns_failed_fn failed, void *failed_ctx);
/* Drain pending datagrams (non-blocking). Returns packets handled. */
size_t qa_kex_mdns_poll(qa_kex_mdns *mdns);
/* TS socket.on('message', bytes => { try { owner.receive(bytes) } catch {} }): one received
 * datagram; false when it was malformed and ignored. Usable on a zeroed, unopened owner. */
bool qa_kex_mdns_message(qa_kex_mdns *mdns, const uint8_t *bytes, size_t length);
void qa_kex_mdns_query(qa_kex_mdns *mdns);
void qa_kex_mdns_close(qa_kex_mdns *mdns);

#endif
