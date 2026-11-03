/* Port of src/app/bootstrap/network/transport.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/platform/ipx.h"
#include <math.h>
struct qa_app_application_transport {
  bool is_ipx;
  qa_app_udp_socket udp; bool udp_live;          /* kind "udp" owner */
  qa_ipx_game_transport ipx; bool ipx_live;      /* kind "ipx" owner */
  qa_dosbox_ipx_network network; bool network_live;
  qa_app_udp_socket relay_udp; bool relay_udp_live; /* the DOSBox registration socket */
  qa_datagram_transport socket;
  void *native; /* platform AF_IPX socket storage from the default capability */
};
static void release_udp(void *ctx) { qa_udp_transport_destroy(ctx); free(ctx); }
static bool bun_bind_udp(void *ctx, const qa_udp_bind_options *options, qa_app_udp_socket *out) {
  (void)ctx;
  qa_udp_transport *socket = calloc(1, sizeof *socket);
  if (!socket) return false;
  qa_cleanup guard; qa_cleanup_push(&guard, free, socket);
  int bound = qa_udp_transport_bind(socket, options);
  qa_cleanup_pop(&guard);
  if (!bound) { free(socket); return false; }
  out->transport = qa_udp_transport_as_transport(socket);
  out->socks = qa_udp_transport_as_socks_connector(socket);
  out->release = release_udp;
  return true;
}
/* bunNativeIpxCapability(): the platform AF_IPX transport behind DatagramTransport<IpxAddress>. */
typedef struct native_socket {
  qa_platform_native_ipx_capability capability;
  qa_native_ipx_transport transport;
  qa_net_address address;
} native_socket;
static qa_platform_ipx_address platform_address(const qa_net_address *a) {
  qa_platform_ipx_address out; out.network = a->network; memcpy(out.node, a->node, 6); out.port = (uint16_t)a->port;
  return out;
}
static void net_address(const qa_platform_ipx_address *a, qa_net_address *out) {
  int64_t node[6]; for (int i = 0; i < 6; i++) node[i] = a->node[i];
  (void)qa_net_make_ipx(out, a->network, node, a->port);
}
static int native_address(void *ctx, qa_net_address *out) { return qa_net_address_clone(&((native_socket *)ctx)->address, out); }
static bool native_closed(void *ctx) { return qa_native_ipx_transport_closed(&((native_socket *)ctx)->transport); }
static int64_t native_max(void *ctx) { return qa_native_ipx_transport_max_datagram_bytes(&((native_socket *)ctx)->transport); }
static bool native_send(void *ctx, const qa_net_address *to, const uint8_t *payload, size_t length) {
  qa_platform_ipx_address destination = platform_address(to);
  return qa_native_ipx_transport_send(&((native_socket *)ctx)->transport, &destination, payload, length);
}
static int native_poll(void *ctx, qa_receive_event *out) {
  qa_platform_ipx_receive_event event; memset(&event, 0, sizeof event);
  qa_native_ipx_transport_poll(&((native_socket *)ctx)->transport, &event);
  memset(out, 0, sizeof *out);
  if (event.kind == QA_PLATFORM_IPX_RECEIVE_NONE) return 0;
  if (event.kind == QA_PLATFORM_IPX_RECEIVE_ERROR) {
    out->kind = QA_RECV_ERROR;
    size_t n = strlen(event.error.message) + 1;
    out->error = malloc(n); if (out->error) memcpy(out->error, event.error.message, n);
  } else {
    /* An address the platform read off the wire with port 0 cannot be represented; drop it. */
    if (event.from.port == 0) { qa_platform_ipx_receive_event_dispose(&event); return native_poll(ctx, out); }
    net_address(&event.from, &out->from);
    out->received_at = event.received_at;
    if (event.kind == QA_PLATFORM_IPX_RECEIVE_DROPPED) { out->kind = QA_RECV_DROPPED; out->reason = "oversize"; }
    else { out->kind = QA_RECV_PACKET; out->payload = event.payload; out->length = event.payload_length; event.payload = NULL; }
  }
  qa_platform_ipx_receive_event_dispose(&event);
  return 1;
}
static long long native_subscribe(void *ctx, qa_net_listener_fn fn, void *listener_ctx) {
  return (long long)qa_native_ipx_transport_subscribe_readable(&((native_socket *)ctx)->transport, fn, listener_ctx);
}
static void native_unsubscribe(void *ctx, long long id) { qa_native_ipx_transport_unsubscribe(&((native_socket *)ctx)->transport, (size_t)id); }
static void native_close(void *ctx) { qa_native_ipx_transport_close(&((native_socket *)ctx)->transport); }
static int native_bind(void *ctx, double port, double packet_type, qa_datagram_transport *out) {
  native_socket *s = calloc(1, sizeof *s);
  if (!s) return 0;
  s->capability = *(const qa_platform_native_ipx_capability *)ctx;
  qa_platform_ipx_bind_options options = { port, packet_type, true };
  qa_platform_ipx_error error; memset(&error, 0, sizeof error);
  if (!qa_native_ipx_capability_bind(&s->capability, &options, &s->transport, &error)) {
    free(s);
    if (error.unsupported) qa_raise("UnsupportedTransportError", 0, 0, "%s", error.message);
    qa_raise("Error", 0, 0, "%s", error.message);
  }
  net_address(qa_native_ipx_transport_address(&s->transport), &s->address);
  qa_datagram_transport v = { s, native_address, native_closed, native_max, native_send, native_poll,
    native_subscribe, native_unsubscribe, native_close };
  *out = v; /* the owning application transport frees the storage */
  return 1;
}
qa_app_transport_capabilities qa_app_bun_application_transports(void) {
  static char reason[256];
  static qa_platform_native_ipx_capability native;
  qa_app_transport_capabilities caps = {0};
  native = qa_bun_native_ipx_capability();
  caps.native_ipx.available = native.kind == QA_NATIVE_IPX_AVAILABLE;
  snprintf(reason, sizeof reason, "%s", native.reason);
  caps.native_ipx.reason = caps.native_ipx.available ? NULL : reason;
  caps.native_ipx.ctx = &native;
  caps.native_ipx.bind = native_bind;
  caps.bind_udp = bun_bind_udp;
  return caps;
}
static void check_family(qa_app_network_transport_selection selection, qa_app_network_family family) {
  if (selection.kind != QA_APP_TRANSPORT_UDP && family == QA_APP_FAMILY_QW)
    qa_raise("Error", 0, 0, "QuakeWorld uses UDP; IPX is not a QuakeWorld transport");
}
/* AbortSignal.throwIfAborted() */
static void throw_if_aborted(const bool *aborted) {
  if (aborted && *aborted) qa_raise("AbortError", 0, 0, "The operation was aborted.");
}
static int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
/* /^(?:ipx:)?([0-9a-fA-F]{8})[.:]([0-9a-fA-F]{12})(?::([0-9]+))?$/ */
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
  int64_t port = -1;
  const char *rest = ok ? p + 21 : p;
  if (ok && *rest == ':') {
    rest++;
    if (!*rest) ok = false;
    port = 0;
    for (const char *q = rest; ok && *q; q++) {
      if (*q < '0' || *q > '9') ok = false;
      else if (port <= 65535) port = port * 10 + (*q - '0'); /* Number(port): only the range check sees it */
    }
  } else if (ok && *rest != '\0') ok = false;
  if (!ok)
    qa_raise("Error", 0, 0, "IPX address must be NETWORK.NODE[:SOCKET] with 8 and 12 hexadecimal digits");
  if (port < 0) {
    if (!(default_port >= 0 && default_port <= 65535) || !(isfinite(default_port) && default_port == trunc(default_port))) port = -1;
    else port = (int64_t)default_port;
  }
  qa_net_address out;
  if (!qa_net_make_ipx(&out, network, node, port)) qa_range_error("Out of memory");
  return out;
}
qa_net_address qa_app_resolve_application_address(const char *text, double default_port,
    qa_app_network_transport_selection selection, qa_app_network_family family) {
  check_family(selection, family);
  if (selection.kind != QA_APP_TRANSPORT_UDP) return qa_app_parse_ipx_remote(text, default_port);
  qa_net_address out;
  int64_t port = default_port >= 0 && default_port <= 65535 && (isfinite(default_port) && default_port == trunc(default_port)) ? (int64_t)default_port : -1;
  if (!qa_net_resolve_address(&out, text, port, family == QA_APP_FAMILY_Q3 ? 4 : 0))
    qa_range_error("Out of memory");
  return out;
}
static const char *game_name(qa_app_network_family family) {
  return family == QA_APP_FAMILY_Q1 ? "quake1" : family == QA_APP_FAMILY_Q2 ? "quake2" : "quake3";
}
/* Frees the storage an application transport holds (TS leaves this to the GC). */
static void release_transport(qa_app_application_transport *t) {
  if (t->native) { native_socket *s = t->native; qa_net_address_destroy(&s->address); free(s); }
  if (t->network_live) qa_dosbox_ipx_network_destroy(&t->network);
  if (t->relay_udp_live && t->relay_udp.release) t->relay_udp.release(t->relay_udp.transport.ctx);
  if (t->udp_live && t->udp.release) t->udp.release(t->udp.transport.ctx);
  free(t);
}
struct open_scope { const qa_app_open_transport_options *options; const qa_app_transport_capabilities *capabilities; qa_app_application_transport *t; };
static void open_body(void *ctx) {
  struct open_scope *scope = ctx;
  const qa_app_open_transport_options *options = scope->options;
  const qa_app_transport_capabilities *capabilities = scope->capabilities;
  qa_app_application_transport *t = scope->t;
  if (options->selection.kind == QA_APP_TRANSPORT_UDP) {
    qa_udp_bind_options bind = {options->host, (int64_t)options->port, &options->limits, NULL, NULL, false};
    if (!capabilities->bind_udp(capabilities->ctx, &bind, &t->udp)) qa_range_error("Out of memory");
    t->udp_live = true;
    /* try { throwIfAborted() } catch (error) { socket.close(); throw error } */
    if (options->aborted && *options->aborted) {
      t->udp.transport.close(t->udp.transport.ctx);
      throw_if_aborted(options->aborted);
    }
    t->socket = t->udp.transport;
    return;
  }
  const char *game = game_name(options->family);
  t->is_ipx = true;
  if (options->selection.kind == QA_APP_TRANSPORT_IPX_NATIVE) {
    qa_ipx_host host = {false, capabilities->native_ipx, NULL};
    qa_bind_ipx_transport(&host, game, options->port, &t->ipx);
    if (capabilities->native_ipx.bind == native_bind) t->native = t->ipx.socket.ctx;
    if (options->aborted && *options->aborted) {
      t->ipx.socket.close(t->ipx.socket.ctx);
      throw_if_aborted(options->aborted);
    }
    t->ipx_live = true;
    t->socket = qa_ipx_game_transport_as_transport(&t->ipx);
    return;
  }
  qa_net_address relay;
  if (!qa_net_resolve_address(&relay, options->selection.relay, 213, 4)) qa_range_error("Out of memory");
  if (relay.kind != QA_NET_IPV4) { qa_net_address_destroy(&relay); qa_raise("Error", 0, 0, "DOSBox IPX relay must resolve to IPv4"); }
  throw_if_aborted(options->aborted);
  qa_datagram_limits limits = {1424, options->limits.queue_packets};
  qa_udp_bind_options bind = {options->host, 0, &limits, NULL, NULL, false};
  if (!capabilities->bind_udp(capabilities->ctx, &bind, &t->relay_udp)) qa_range_error("Out of memory");
  t->relay_udp_live = true;
  qa_dosbox_connect_options connect = {0};
  connect.aborted = options->aborted;
  /* connect owns (closes) the socket, including when it raises. */
  (void)qa_dosbox_ipx_network_connect(&t->network, t->relay_udp.transport, &relay, &connect);
  t->network_live = true;
  /* try { throwIfAborted(); bind; throwIfAborted() } catch (error) { network.close(); throw error }:
   * the caller's catch closes a live network. */
  throw_if_aborted(options->aborted);
  qa_ipx_host host = {true, capabilities->native_ipx, &t->network};
  qa_bind_ipx_transport(&host, game, options->port, &t->ipx);
  t->ipx_live = true;
  throw_if_aborted(options->aborted);
  t->socket = qa_ipx_game_transport_as_transport(&t->ipx);
}
qa_app_application_transport *qa_app_open_application_transport(
    const qa_app_open_transport_options *options,
    const qa_app_transport_capabilities *capabilities) {
  qa_app_transport_capabilities fallback;
  if (!capabilities) { fallback = qa_app_bun_application_transports(); capabilities = &fallback; }
  check_family(options->selection, options->family);
  throw_if_aborted(options->aborted);
  qa_app_application_transport *t = calloc(1, sizeof *t);
  if (!t) return NULL;
  struct open_scope scope = { options, capabilities, t };
  qa_caught_error caught;
  if (!qa_try(open_body, &scope, &caught)) {
    if (t->network_live) qa_dosbox_ipx_network_close(&t->network);
    release_transport(t);
    qa_rethrow(&caught);
  }
  return t;
}
void qa_app_application_transport_destroy(qa_app_application_transport *transport) {
  if (!transport) return;
  qa_app_application_transport_close(transport);
  release_transport(transport);
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
qa_app_udp_socket *qa_app_application_transport_udp_socket(qa_app_application_transport *transport) {
  if (transport->is_ipx) qa_raise("Error", 0, 0, "The selected source requires a UDP transport");
  return &transport->udp;
}
bool qa_app_application_transport_send(qa_app_application_transport *transport,
    const qa_net_address *to, const uint8_t *payload, size_t length) {
  if (transport->is_ipx) {
    if (to->kind != QA_NET_IPX) qa_raise("Error", 0, 0, "An IPX transport requires an IPX destination");
  } else if (to->kind == QA_NET_IPX) {
    qa_raise("Error", 0, 0, "An IPX destination requires an explicitly selected IPX transport");
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
    qa_raise("Error", 0, 0, "SOCKS proxying is not available for the selected IPX transport");
  transport->udp.socks.connect_socks(transport->udp.socks.ctx, options);
}
static void close_socket_body(void *ctx) {
  qa_app_application_transport *transport = ctx;
  transport->socket.close(transport->socket.ctx);
}
void qa_app_application_transport_close(qa_app_application_transport *transport) {
  if (!transport->is_ipx) { transport->socket.close(transport->socket.ctx); return; }
  /* try { socket.close() } finally { closeNetwork() } */
  qa_caught_error caught;
  bool ok = qa_try(close_socket_body, transport, &caught);
  if (transport->network_live) qa_dosbox_ipx_network_close(&transport->network);
  if (!ok) qa_rethrow(&caught);
}
