/* Port of src/app/bootstrap/network/transport.ts. Async open ->
 * synchronous; AbortSignal becomes a borrowed aborted flag checked at the
 * TS throwIfAborted points (raises AbortError "The operation was
 * aborted."). Throws are qa_raise with the TS class and message.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_TRANSPORT_H
#define QA_APP_BOOTSTRAP_NETWORK_TRANSPORT_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/network/common/endpoint.h"
#include "qa/network/common/ipx_host.h"
#include "qa/network/common/transport.h"
typedef enum qa_app_transport_kind {
  QA_APP_TRANSPORT_UDP, QA_APP_TRANSPORT_IPX_DOSBOX, QA_APP_TRANSPORT_IPX_NATIVE
} qa_app_transport_kind;
typedef struct qa_app_network_transport_selection {
  qa_app_transport_kind kind; const char *relay; /* ipx-dosbox only */
} qa_app_network_transport_selection;
typedef enum qa_app_network_family {
  QA_APP_FAMILY_Q1, QA_APP_FAMILY_QW, QA_APP_FAMILY_Q2, QA_APP_FAMILY_Q3
} qa_app_network_family;
/* TS ApplicationUdpSocket: a datagram transport that can connect SOCKS. `release`
 * (may be NULL) frees the socket's storage when the application transport is destroyed;
 * closing goes through transport.close as in TS. */
typedef struct qa_app_udp_socket {
  qa_datagram_transport transport;
  qa_socks_connector socks;
  void (*release)(void *ctx);
} qa_app_udp_socket;
/* TS ApplicationTransportCapabilities. bind_udp fills *out and returns true, or raises
 * the bind error; false only on OOM. */
typedef struct qa_app_transport_capabilities {
  void *ctx;
  qa_native_ipx_capability native_ipx;
  bool (*bind_udp)(void *ctx, const qa_udp_bind_options *options, qa_app_udp_socket *out);
} qa_app_transport_capabilities;
qa_app_transport_capabilities qa_app_bun_application_transports(void);
/* Raises the TS Error on a malformed address. */
qa_net_address qa_app_parse_ipx_remote(const char *text, double default_port);
qa_net_address qa_app_resolve_application_address(const char *text, double default_port,
    qa_app_network_transport_selection selection, qa_app_network_family family);
typedef struct qa_app_application_transport qa_app_application_transport;
typedef struct qa_app_open_transport_options {
  qa_app_network_transport_selection selection; qa_app_network_family family;
  const char *host; double port; qa_datagram_limits limits;
  const bool *aborted; /* TS AbortSignal, may be NULL */
} qa_app_open_transport_options;
/* capabilities NULL uses qa_app_bun_application_transports(). NULL return only on OOM;
 * every raise releases what was opened, as the TS catch blocks close it. */
qa_app_application_transport *qa_app_open_application_transport(
    const qa_app_open_transport_options *options,
    const qa_app_transport_capabilities *capabilities);
void qa_app_application_transport_destroy(qa_app_application_transport *transport);
qa_datagram_transport qa_app_application_transport_as_transport(qa_app_application_transport *transport);
int64_t qa_app_application_transport_max_datagram_bytes(qa_app_application_transport *transport);
int qa_app_application_transport_address(qa_app_application_transport *transport, qa_net_address *out);
bool qa_app_application_transport_closed(qa_app_application_transport *transport);
/* Raises "The selected source requires a UDP transport" for IPX. */
qa_app_udp_socket *qa_app_application_transport_udp_socket(qa_app_application_transport *transport);
bool qa_app_application_transport_send(qa_app_application_transport *transport,
    const qa_net_address *to, const uint8_t *payload, size_t length);
int qa_app_application_transport_poll(qa_app_application_transport *transport, qa_receive_event *out);
long long qa_app_application_transport_subscribe(qa_app_application_transport *transport,
    qa_net_listener_fn fn, void *ctx);
void qa_app_application_transport_unsubscribe(qa_app_application_transport *transport, long long id);
void qa_app_application_transport_connect_socks(qa_app_application_transport *transport,
    const qa_socks_options *options);
void qa_app_application_transport_close(qa_app_application_transport *transport);
#endif
