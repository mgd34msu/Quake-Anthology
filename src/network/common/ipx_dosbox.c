/* Port of src/network/common/ipx-dosbox.ts. GPL-2.0-or-later. */
#include "qa/network/common/ipx_dosbox.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define DOSBOX_PACKET_BYTES 1424
static bool node_broadcast(const qa_net_address *a) { for (int i = 0; i < 6; i++) if (a->node[i] != 255) return false; return true; }
static bool node_zero(const qa_net_address *a) { for (int i = 0; i < 6; i++) if (a->node[i] != 0) return false; return true; }
static void make_control(qa_net_address *out) {
  int64_t node[6] = {0,0,0,0,0,0};
  (void)qa_net_make_ipx(out, 0, node, 2);
}
static qa_dosbox_ipx_socket *socket_at(qa_dosbox_ipx_network *n, int64_t port) {
  for (size_t i = 0; i < n->socket_count; i++) if (n->ports[i] == port) return n->sockets[i];
  return NULL;
}
static void socket_accept(qa_dosbox_ipx_socket *s, const qa_ipx_packet *packet) {
  qa_packet_queue_accept(&s->queue, &packet->from, packet->payload, packet->length, false);
}
static void socket_error(qa_dosbox_ipx_socket *s, const char *message) {
  qa_packet_queue_push_error(&s->queue, message);
}
static void net_deliver(qa_dosbox_ipx_network *n, const qa_ipx_packet *packet);
static void net_receive(qa_dosbox_ipx_network *n);
static void udp_readable(void *ctx) { net_receive(ctx); }
int qa_dosbox_ipx_network_connect(qa_dosbox_ipx_network *n, qa_udp_transport *udp,
                                  const qa_net_address *server, const qa_dosbox_connect_options *options) {
  memset(n, 0, sizeof *n);
  bool ok = false;
  qa_net_address udp_addr;
  (void)qa_udp_transport_get_address(udp, &udp_addr);
  bool is_v4 = udp_addr.kind == QA_NET_IPV4;
  qa_net_address_destroy(&udp_addr);
  double timeout = options && options->has_timeout ? options->timeout_ms : 5000;
  qa_net_now_fn now_fn = options && options->now ? options->now : qa_net_default_now;
  void *now_ctx = options ? options->now_ctx : NULL;
  const bool *aborted = options ? options->aborted : NULL;
  if (!is_v4) { qa_udp_transport_close(udp); qa_range_error("DOSBox IPX requires an IPv4 UDP socket"); }
  if (!isfinite(timeout) || timeout <= 0) { qa_udp_transport_close(udp); qa_range_error("Invalid DOSBox registration timeout"); }
  if (aborted && *aborted) { qa_udp_transport_close(udp); qa_range_error("DOSBox IPX registration aborted"); }
  qa_net_address control; make_control(&control);
  qa_ipx_packet reg; memset(&reg, 0, sizeof reg);
  (void)qa_net_address_clone(&control, &reg.from);
  (void)qa_net_address_clone(&control, &reg.to);
  reg.hops = 0; reg.packet_type = 0; reg.payload = malloc(1); reg.length = 0;
  size_t wire_len = 0; uint8_t *wire = qa_encode_ipx_packet(&reg, &wire_len);
  qa_ipx_packet_destroy(&reg);
  if (!qa_udp_transport_send(udp, server, wire, wire_len)) {
    free(wire); qa_udp_transport_close(udp); qa_range_error("DOSBox IPX registration send failed");
  }
  free(wire);
  double start = now_fn(now_ctx);
  qa_net_address assigned; bool found = false;
  while (!found) {
    if (aborted && *aborted) { qa_udp_transport_close(udp); qa_range_error("DOSBox IPX registration aborted"); }
    if (qa_udp_transport_closed(udp)) { qa_udp_transport_close(udp); qa_range_error("DOSBox IPX registration socket closed"); }
    qa_receive_event event;
    bool any = false;
    while (qa_udp_transport_poll(udp, &event)) {
      any = true;
      if (event.kind == QA_RECV_ERROR) {
        char *msg = event.error ? event.error : (char *)"DOSBox IPX registration socket closed";
        (void)msg;
        qa_receive_event_destroy(&event);
        qa_udp_transport_close(udp);
        qa_range_error("DOSBox IPX registration socket closed");
      }
      if (event.kind != QA_RECV_PACKET || !qa_net_same_address(&event.from, server, true)) {
        qa_receive_event_destroy(&event); continue;
      }
      qa_ipx_packet packet;
      if (qa_decode_ipx_packet(event.payload, event.length, &packet)) {
        if (packet.length == 0 && packet.from.port == 2 && packet.to.port == 2
            && packet.from.network == 1 && !node_zero(&packet.to) && !node_broadcast(&packet.to)) {
          (void)qa_net_address_clone(&packet.to, &assigned);
          found = true;
        }
        qa_ipx_packet_destroy(&packet);
      }
      qa_receive_event_destroy(&event);
      if (found) break;
    }
    if (found) break;
    if (now_fn(now_ctx) - start >= timeout) { qa_udp_transport_close(udp); qa_range_error("DOSBox IPX registration timed out"); }
    (void)any;
    struct timespec pause_for = { 0, 1000000 };
    nanosleep(&pause_for, NULL);
  }
  n->udp = *udp; memset(udp, 0, sizeof *udp); udp->fd = -1;
  (void)qa_net_address_clone(server, &n->server);
  n->address = assigned;
  n->now = now_fn; n->now_ctx = now_ctx;
  n->subscription = qa_udp_transport_subscribe(&n->udp, udp_readable, n);
  ok = true;
  return ok ? 1 : 0;
}
void qa_dosbox_ipx_network_destroy(qa_dosbox_ipx_network *n) {
  qa_dosbox_ipx_network_close(n);
  qa_udp_transport_destroy(&n->udp);
  qa_net_address_destroy(&n->server);
  qa_net_address_destroy(&n->address);
  free(n->sockets); free(n->ports);
}
bool qa_dosbox_ipx_network_closed(const qa_dosbox_ipx_network *n) { return n->ended; }
int qa_dosbox_ipx_network_get_server(const qa_dosbox_ipx_network *n, qa_net_address *out) { return qa_net_address_clone(&n->server, out); }
int qa_dosbox_ipx_network_get_address(const qa_dosbox_ipx_network *n, qa_net_address *out) { return qa_net_address_clone(&n->address, out); }
qa_dosbox_ipx_socket *qa_dosbox_ipx_network_bind(qa_dosbox_ipx_network *n, double requested_port, double packet_type) {
  if (n->ended) qa_range_error("DOSBox IPX network is closed");
  (void)qa_net_port_number((int64_t)requested_port, true);
  if (packet_type != trunc(packet_type) || packet_type < 0 || packet_type > 255) qa_range_error("Invalid IPX packet type");
  if (n->socket_count >= 150) qa_range_error("DOSBox IPX socket table is full");
  int64_t port = (int64_t)requested_port;
  if (port == 0) {
    port = 0x4002;
    while (socket_at(n, port) && port < 0x7fff) port++;
  }
  if (port == 2 || socket_at(n, port)) qa_range_error("IPX socket is already bound or reserved");
  qa_dosbox_ipx_socket *s = calloc(1, sizeof *s);
  int64_t node[6]; for (int i = 0; i < 6; i++) node[i] = n->address.node[i];
  (void)qa_net_make_ipx(&s->address, n->address.network, node, port);
  s->network = n; s->packet_type = packet_type;
  qa_datagram_limits lim = { DOSBOX_PACKET_BYTES - 30, 256 };
  (void)qa_packet_queue_init(&s->queue, lim, n->now, n->now_ctx);
  if (n->socket_count == n->socket_cap) {
    size_t nc = n->socket_cap ? n->socket_cap * 2 : 8;
    n->sockets = realloc(n->sockets, nc * sizeof *n->sockets);
    n->ports = realloc(n->ports, nc * sizeof *n->ports);
    n->socket_cap = nc;
  }
  n->sockets[n->socket_count] = s; n->ports[n->socket_count] = port; n->socket_count++;
  net_receive(n);
  return s;
}
bool qa_dosbox_ipx_network_send(qa_dosbox_ipx_network *n, const qa_ipx_packet *packet) {
  if (n->ended) qa_range_error("DOSBox IPX network is closed");
  if ((long long)packet->length + 30 > DOSBOX_PACKET_BYTES) {
    qa_dosbox_ipx_socket *s = socket_at(n, packet->from.port);
    if (s) socket_error(s, "Packet exceeds DOSBox IPX capacity (1394 payload bytes)");
    return false;
  }
  bool local = qa_net_same_address(&packet->to, &n->address, false);
  size_t wire_len = 0; uint8_t *wire = qa_encode_ipx_packet(packet, &wire_len);
  bool sent = local || qa_udp_transport_send(&n->udp, &n->server, wire, wire_len);
  free(wire);
  if (local || node_broadcast(&packet->to)) net_deliver(n, packet);
  return sent;
}
static void net_deliver(qa_dosbox_ipx_network *n, const qa_ipx_packet *packet) {
  bool local = qa_net_same_address(&packet->to, &n->address, false);
  bool bcast_ok = node_broadcast(&packet->to) && (packet->to.network == 0 || packet->to.network == n->address.network);
  if (!local && !bcast_ok) return;
  if (packet->to.port == 2) {
    if (node_broadcast(&packet->to) && packet->from.port == 2 && packet->length == 0) {
      qa_ipx_packet reply; memset(&reply, 0, sizeof reply);
      (void)qa_net_address_clone(&n->address, &reply.from);
      (void)qa_net_address_clone(&packet->from, &reply.to);
      reply.hops = 0; reply.packet_type = 0; reply.payload = malloc(1); reply.length = 0;
      size_t wl = 0; uint8_t *wire = qa_encode_ipx_packet(&reply, &wl);
      qa_ipx_packet_destroy(&reply);
      (void)qa_udp_transport_send(&n->udp, &n->server, wire, wl);
      free(wire);
    }
    return;
  }
  qa_dosbox_ipx_socket *s = socket_at(n, packet->to.port);
  if (s) socket_accept(s, packet);
}
static void net_receive(qa_dosbox_ipx_network *n) {
  if (n->ended) return;
  if (qa_udp_transport_closed(&n->udp)) { qa_dosbox_ipx_network_close(n); return; }
  while (!n->ended) {
    qa_receive_event event;
    if (!qa_udp_transport_poll(&n->udp, &event)) return;
    if (event.kind == QA_RECV_ERROR) {
      for (size_t i = 0; i < n->socket_count; i++) socket_error(n->sockets[i], event.error ? event.error : "error");
      qa_receive_event_destroy(&event); continue;
    }
    if (event.kind != QA_RECV_PACKET || !qa_net_same_address(&event.from, &n->server, true) || event.length > DOSBOX_PACKET_BYTES) {
      qa_receive_event_destroy(&event); continue;
    }
    qa_ipx_packet packet;
    if (qa_decode_ipx_packet(event.payload, event.length, &packet)) {
      net_deliver(n, &packet);
      qa_ipx_packet_destroy(&packet);
    }
    qa_receive_event_destroy(&event);
  }
}
void qa_dosbox_ipx_network_receive(qa_dosbox_ipx_network *n) { net_receive(n); }
void qa_dosbox_ipx_network_close(qa_dosbox_ipx_network *n) {
  if (n->ended) return;
  n->ended = true;
  qa_udp_transport_unsubscribe(&n->udp, n->subscription);
  while (n->socket_count > 0) {
    qa_dosbox_ipx_socket *s = n->sockets[0];
    qa_dosbox_ipx_socket_close(s);
    qa_packet_queue_destroy(&s->queue);
    qa_net_address_destroy(&s->address);
    free(s);
  }
  qa_udp_transport_close(&n->udp);
}
bool qa_dosbox_ipx_socket_closed(const qa_dosbox_ipx_socket *s) { return s->ended; }
int qa_dosbox_ipx_socket_get_address(const qa_dosbox_ipx_socket *s, qa_net_address *out) { return qa_net_address_clone(&s->address, out); }
static void sock_opened(const qa_dosbox_ipx_socket *s) { if (s->ended) qa_range_error("DOSBox IPX socket is closed"); }
bool qa_dosbox_ipx_socket_send(qa_dosbox_ipx_socket *s, const qa_net_address *to, const uint8_t *payload, size_t length) {
  sock_opened(s);
  qa_ipx_packet packet; memset(&packet, 0, sizeof packet);
  (void)qa_net_address_clone(&s->address, &packet.from);
  (void)qa_net_address_clone(to, &packet.to);
  packet.packet_type = (int64_t)s->packet_type; packet.hops = 0;
  packet.payload = (uint8_t *)payload; packet.length = length;
  bool result = qa_dosbox_ipx_network_send(s->network, &packet);
  packet.payload = NULL;
  qa_ipx_packet_destroy(&packet);
  return result;
}
int qa_dosbox_ipx_socket_poll(qa_dosbox_ipx_socket *s, qa_receive_event *out) {
  sock_opened(s); net_receive(s->network);
  return qa_packet_queue_poll(&s->queue, out);
}
long long qa_dosbox_ipx_socket_subscribe(qa_dosbox_ipx_socket *s, qa_net_listener_fn fn, void *ctx) {
  sock_opened(s); return qa_packet_queue_subscribe(&s->queue, fn, ctx);
}
void qa_dosbox_ipx_socket_unsubscribe(qa_dosbox_ipx_socket *s, long long id) { qa_packet_queue_unsubscribe(&s->queue, id); }
void qa_dosbox_ipx_socket_close(qa_dosbox_ipx_socket *s) {
  if (s->ended) return;
  s->ended = true;
  qa_dosbox_ipx_network *n = s->network;
  for (size_t i = 0; i < n->socket_count; i++) if (n->sockets[i] == s) {
    memmove(&n->sockets[i], &n->sockets[i+1], (n->socket_count - i - 1) * sizeof *n->sockets);
    memmove(&n->ports[i], &n->ports[i+1], (n->socket_count - i - 1) * sizeof *n->ports);
    n->socket_count--; break;
  }
  qa_packet_queue_close(&s->queue);
}
static int vt_addr(void *ctx, qa_net_address *out) { return qa_dosbox_ipx_socket_get_address(ctx, out); }
static bool vt_closed(void *ctx) { return qa_dosbox_ipx_socket_closed(ctx); }
static int64_t vt_max(void *ctx) { (void)ctx; return DOSBOX_PACKET_BYTES - 30; }
static bool vt_send(void *ctx, const qa_net_address *to, const uint8_t *p, size_t n) { return qa_dosbox_ipx_socket_send(ctx, to, p, n); }
static int vt_poll(void *ctx, qa_receive_event *out) { return qa_dosbox_ipx_socket_poll(ctx, out); }
static long long vt_sub(void *ctx, qa_net_listener_fn fn, void *c) { return qa_dosbox_ipx_socket_subscribe(ctx, fn, c); }
static void vt_unsub(void *ctx, long long id) { qa_dosbox_ipx_socket_unsubscribe(ctx, id); }
static void vt_close(void *ctx) { qa_dosbox_ipx_socket_close(ctx); }
qa_datagram_transport qa_dosbox_ipx_socket_as_transport(qa_dosbox_ipx_socket *s) {
  qa_datagram_transport v; v.ctx = s; v.get_address = vt_addr; v.is_closed = vt_closed;
  v.max_datagram_bytes = vt_max; v.send = vt_send; v.poll = vt_poll;
  v.subscribe = vt_sub; v.unsubscribe = vt_unsub; v.close = vt_close; return v;
}
