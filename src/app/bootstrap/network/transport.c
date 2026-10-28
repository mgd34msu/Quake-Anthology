/* Port of src/app/bootstrap/network/transport.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/transport.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/platform/ipx.h"
struct qa_app_application_transport {
  bool is_ipx;
  qa_udp_transport udp; bool udp_live;
  qa_ipx_game_transport ipx; bool ipx_live;
  qa_dosbox_ipx_network network; bool network_live;
  qa_datagram_transport socket;
};
static bool bun_bind_udp(void *ctx, const qa_udp_bind_options *options, qa_udp_transport *out) {
  (void)ctx;
  return qa_udp_transport_bind(out, options) != 0;
}
qa_app_transport_capabilities qa_app_bun_application_transports(void) {
  qa_app_transport_capabilities caps = {0};
  qa_platform_native_ipx_capability native = qa_bun_native_ipx_capability();
  caps.native_ipx.available = native.kind == QA_NATIVE_IPX_AVAILABLE;
  caps.native_ipx.reason = caps.native_ipx.available ? NULL : "Native IPX is unavailable";
  caps.bind_udp = bun_bind_udp;
  return caps;
}
static void check_family(qa_app_network_transport_selection selection, qa_app_network_family family) {
  if (selection.kind != QA_APP_TRANSPORT_UDP && family == QA_APP_FAMILY_QW)
    qa_range_error("QuakeWorld uses UDP; IPX is not a QuakeWorld transport");
}
static void throw_if_aborted(const bool *aborted) {
  if (aborted && *aborted) qa_range_error("The operation was aborted");
}
static int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
qa_net_address qa_app_parse_ipx_remote(const char *text, double default_port) {
  const char *p = text;
  if (strncmp(p, "ipx:", 4) == 0) p += 4;
  bool ok = strlen(p) >= 21;
  int64_t network = 0; int64_t node[6] = {0};
  for (int i = 0; ok && i < 8; i++) { int v = hex_value(p[i]); if (v < 0) ok = false; else network = network * 16 + v; }
  if (ok && p[8] != '.' && p[8] != ':') ok = false;
  for (int i = 0; ok && i < 6; i++) {
    int hi = hex_value(p[9 + i * 2]), lo = hex_value(p[10 + i * 2]);
    if (hi < 0 || lo < 0) ok = false; else node[i] = hi * 16 + lo;
  }
  double port = default_port;
  const char *rest = p + 21;
  if (ok && *rest == ':') {
    rest++;
    if (!*rest) ok = false;
    double value = 0;
    for (const char *q = rest; ok && *q; q++) {
      if (!isdigit((unsigned char)*q)) ok = false; else value = value * 10 + (*q - '0');
    }
    port = value;
  } else if (ok && *rest != '\0') ok = false;
  if (!ok)
    qa_range_error("IPX address must be NETWORK.NODE[:SOCKET] with 8 and 12 hexadecimal digits");
  qa_net_address out;
  if (!qa_net_make_ipx(&out, network, node, (int64_t)port)) qa_range_error("out of memory");
  return out;
}
qa_net_address qa_app_resolve_application_address(const char *text, double default_port,
    qa_app_network_transport_selection selection, qa_app_network_family family) {
  check_family(selection, family);
  if (selection.kind != QA_APP_TRANSPORT_UDP) return qa_app_parse_ipx_remote(text, default_port);
  qa_net_address out;
  if (!qa_net_resolve_address(&out, text, (int64_t)default_port, family == QA_APP_FAMILY_Q3 ? 4 : 0))
    qa_range_error("out of memory");
  return out;
}
static const char *game_name(qa_app_network_family family) {
  return family == QA_APP_FAMILY_Q1 ? "quake1" : family == QA_APP_FAMILY_Q2 ? "quake2" : "quake3";
}
qa_app_application_transport *qa_app_open_application_transport(
    const qa_app_open_transport_options *options,
    const qa_app_transport_capabilities *capabilities) {
  qa_app_transport_capabilities fallback = qa_app_bun_application_transports();
  if (!capabilities) capabilities = &fallback;
  check_family(options->selection, options->family);
  throw_if_aborted(options->aborted);
  qa_app_application_transport *t = calloc(1, sizeof *t);
  if (!t) return NULL;
  if (options->selection.kind == QA_APP_TRANSPORT_UDP) {
    qa_udp_bind_options bind = {options->host, (int64_t)options->port, &options->limits, NULL, NULL, false};
    if (!capabilities->bind_udp(capabilities->ctx, &bind, &t->udp)) { free(t); return NULL; }
    t->udp_live = true;
    if (options->aborted && *options->aborted) {
      qa_udp_transport_close(&t->udp); /* TS catch closes, then throws */
      qa_range_error("The operation was aborted");
    }
    t->socket = qa_udp_transport_as_transport(&t->udp);
    return t;
  }
  const char *game = game_name(options->family);
  t->is_ipx = true;
  if (options->selection.kind == QA_APP_TRANSPORT_IPX_NATIVE) {
    qa_ipx_host host = {false, capabilities->native_ipx, NULL};
    qa_bind_ipx_transport(&host, game, options->port, &t->ipx);
    t->ipx_live = true;
    if (options->aborted && *options->aborted) {
      t->ipx.socket.close(t->ipx.socket.ctx);
      qa_range_error("The operation was aborted");
    }
    t->socket = qa_ipx_game_transport_as_transport(&t->ipx);
    return t;
  }
  qa_net_address relay;
  if (!qa_net_resolve_address(&relay, options->selection.relay, 213, 4)) { free(t); return NULL; }
  if (relay.kind != QA_NET_IPV4) {
    qa_net_address_destroy(&relay);
    qa_range_error("DOSBox IPX relay must resolve to IPv4");
  }
  throw_if_aborted(options->aborted);
  qa_datagram_limits limits = {1424, options->limits.queue_packets};
  qa_udp_bind_options bind = {options->host, 0, &limits, NULL, NULL, false};
  qa_udp_transport udp;
  if (!capabilities->bind_udp(capabilities->ctx, &bind, &udp)) {
    qa_net_address_destroy(&relay); free(t); return NULL;
  }
  qa_dosbox_connect_options connect = {0};
  connect.aborted = options->aborted;
  /* connect takes ownership of udp (moved), including on failure. */
  if (!qa_dosbox_ipx_network_connect(&t->network, &udp, &relay, &connect)) {
    qa_net_address_destroy(&relay); free(t); return NULL;
  }
  t->network_live = true;
  qa_net_address_destroy(&relay);
  if (options->aborted && *options->aborted) {
    qa_dosbox_ipx_network_close(&t->network);
    qa_range_error("The operation was aborted");
  }
  qa_ipx_host host = {true, capabilities->native_ipx, &t->network};
  qa_bind_ipx_transport(&host, game, options->port, &t->ipx);
  t->ipx_live = true;
  if (options->aborted && *options->aborted) {
    qa_dosbox_ipx_network_close(&t->network);
    qa_range_error("The operation was aborted");
  }
  t->socket = qa_ipx_game_transport_as_transport(&t->ipx);
  return t;
}
void qa_app_application_transport_destroy(qa_app_application_transport *transport) {
  if (!transport) return;
  qa_app_application_transport_close(transport);
  if (transport->ipx_live) qa_ipx_game_transport_destroy(&transport->ipx);
  if (transport->network_live) qa_dosbox_ipx_network_destroy(&transport->network);
  if (transport->udp_live) qa_udp_transport_destroy(&transport->udp);
  free(transport);
}
qa_datagram_transport qa_app_application_transport_as_transport(qa_app_application_transport *transport) {
  return transport->socket;
}
int64_t qa_app_application_transport_max_datagram_bytes(qa_app_application_transport *transport) {
  int64_t value = transport->socket.max_datagram_bytes
      ? transport->socket.max_datagram_bytes(transport->socket.ctx) : -1;
  return value < 0 ? 65507 : value;
}
int qa_app_application_transport_address(qa_app_application_transport *transport, qa_net_address *out) {
  return transport->socket.get_address(transport->socket.ctx, out);
}
bool qa_app_application_transport_closed(qa_app_application_transport *transport) {
  return transport->socket.is_closed(transport->socket.ctx);
}
qa_udp_transport *qa_app_application_transport_udp_socket(qa_app_application_transport *transport) {
  if (transport->is_ipx) qa_range_error("The selected source requires a UDP transport");
  return &transport->udp;
}
bool qa_app_application_transport_send(qa_app_application_transport *transport,
    const qa_net_address *to, const uint8_t *payload, size_t length) {
  if (transport->is_ipx) {
    if (to->kind != QA_NET_IPX) qa_range_error("An IPX transport requires an IPX destination");
  } else if (to->kind == QA_NET_IPX) {
    qa_range_error("An IPX destination requires an explicitly selected IPX transport");
  }
  return transport->socket.send(transport->socket.ctx, to, payload, length);
}
int qa_app_application_transport_poll(qa_app_application_transport *transport, qa_receive_event *out) {
  return transport->socket.poll(transport->socket.ctx, out);
}
long long qa_app_application_transport_subscribe(qa_app_application_transport *transport,
    qa_net_listener_fn fn, void *ctx) {
  return transport->socket.subscribe(transport->socket.ctx, fn, ctx);
}
void qa_app_application_transport_unsubscribe(qa_app_application_transport *transport, long long id) {
  transport->socket.unsubscribe(transport->socket.ctx, id);
}
void qa_app_application_transport_connect_socks(qa_app_application_transport *transport,
    const qa_socks_options *options) {
  if (transport->is_ipx)
    qa_range_error("SOCKS proxying is not available for the selected IPX transport");
  qa_udp_transport_connect_socks(&transport->udp, options);
}
void qa_app_application_transport_close(qa_app_application_transport *transport) {
  if (!transport->is_ipx) { transport->socket.close(transport->socket.ctx); return; }
  transport->socket.close(transport->socket.ctx);
  if (transport->network_live) qa_dosbox_ipx_network_close(&transport->network);
}
