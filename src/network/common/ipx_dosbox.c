/* Port of src/network/common/ipx-dosbox.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/network/common/ipx_dosbox.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define DOSBOX_PACKET_BYTES 1424
static bool node_broadcast(const qa_net_address *a) { for (int i = 0; i < 6; i++) if (a->node[i] != 255) return false; return true; }
static bool node_zero(const qa_net_address *a) { for (int i = 0; i < 6; i++) if (a->node[i] != 0) return false; return true; }
static void make_control(qa_net_address *out) {
  int64_t node[6] = {0, 0, 0, 0, 0, 0};
  (void)qa_net_make_ipx(out, 0, node, 2);
}
static qa_dosbox_ipx_socket *socket_at(qa_dosbox_ipx_network *n, int64_t port) {
  for (size_t i = 0; i < n->socket_count; i++) if (n->ports[i] == port) return n->sockets[i];
  return NULL;
}
static void socket_accept(qa_dosbox_ipx_socket *s, const qa_ipx_packet *packet) {
  qa_packet_queue_accept(&s->queue, &packet->from, packet->payload, packet->length, false);
}
static void socket_error(qa_dosbox_ipx_socket *s, const char *message) { qa_packet_queue_push_error(&s->queue, message); }
static void net_deliver(qa_dosbox_ipx_network *n, const qa_ipx_packet *packet);
static void net_receive(qa_dosbox_ipx_network *n);
static void udp_readable(void *ctx) { net_receive(ctx); }
static double monotonic_ms(void) {
  struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1e6;
}
static uint8_t *empty_control_packet(const qa_net_address *from, const qa_net_address *to, size_t *length) {
  qa_ipx_packet packet; memset(&packet, 0, sizeof packet);
  packet.from = *from; packet.to = *to; packet.payload = NULL; packet.length = 0;
  return qa_encode_ipx_packet(&packet, length);
}

/* The registration promise: settles with the assigned node or an error message. */
struct registration {
  qa_datagram_transport udp; const qa_net_address *server; const bool *aborted;
  bool done; bool failed; char failure[QA_CAUGHT_MESSAGE_CAPACITY]; qa_net_address assigned;
};
static void registration_finish_error(struct registration *r, const char *message) {
  if (r->done) return;
  r->done = true; r->failed = true;
  snprintf(r->failure, sizeof r->failure, "%s", message);
}
static void registration_read(void *ctx) {
  struct registration *r = ctx;
  if (r->done) return;
  if (r->udp.is_closed(r->udp.ctx)) { registration_finish_error(r, "DOSBox IPX registration socket closed"); return; }
  while (!r->done) {
    qa_receive_event event;
    if (!r->udp.poll(r->udp.ctx, &event)) return;
    if (event.kind == QA_RECV_ERROR) {
      registration_finish_error(r, event.error ? event.error : "Error");
      qa_receive_event_destroy(&event); return;
    }
    if (event.kind != QA_RECV_PACKET || !qa_net_same_address(&event.from, r->server, true)) { qa_receive_event_destroy(&event); continue; }
    qa_ipx_packet packet;
    if (qa_decode_ipx_packet(event.payload, event.length, &packet)) {
      if (packet.length == 0 && packet.from.port == 2 && packet.to.port == 2 && packet.from.network == 1
          && !node_zero(&packet.to) && !node_broadcast(&packet.to)) {
        r->done = true; r->assigned = packet.to; memset(&packet.to, 0, sizeof packet.to);
      }
      qa_ipx_packet_destroy(&packet);
    }
    qa_receive_event_destroy(&event);
  }
}
static void registration_send_body(void *ctx) {
  struct registration *r = ctx;
  qa_net_address control; make_control(&control);
  size_t wire_length = 0;
  uint8_t *wire = empty_control_packet(&control, &control, &wire_length);
  qa_cleanup wire_guard; qa_cleanup_push(&wire_guard, free, wire);
  bool sent = r->udp.send(r->udp.ctx, r->server, wire, wire_length);
  qa_cleanup_pop(&wire_guard); free(wire);
  if (!sent) registration_finish_error(r, "DOSBox IPX registration send failed");
  else registration_read(r);
}
struct connect_scope {
  qa_dosbox_ipx_network *n; qa_datagram_transport udp; const qa_net_address *server;
  const qa_dosbox_connect_options *options;
};
static void connect_body(void *ctx) {
  struct connect_scope *scope = ctx;
  const qa_dosbox_connect_options *options = scope->options;
  qa_net_address udp_address;
  if (!scope->udp.get_address(scope->udp.ctx, &udp_address)) qa_range_error("Out of memory");
  bool is_v4 = udp_address.kind == QA_NET_IPV4;
  qa_net_address_destroy(&udp_address);
  if (!is_v4) qa_raise("Error", 0, 0, "DOSBox IPX requires an IPv4 UDP socket");
  double timeout = options && options->has_timeout ? options->timeout_ms : 5000;
  if (!isfinite(timeout) || timeout <= 0) qa_range_error("Invalid DOSBox registration timeout");
  const bool *aborted = options ? options->aborted : NULL;
  if (aborted && *aborted) qa_raise("AbortError", 0, 0, "The operation was aborted.");
  struct registration r; memset(&r, 0, sizeof r);
  r.udp = scope->udp; r.server = scope->server; r.aborted = aborted;
  double deadline = monotonic_ms() + timeout;
  long long subscription = scope->udp.subscribe(scope->udp.ctx, registration_read, &r);
  /* try { send the registration; read() } catch (error) { finish(error) } */
  qa_caught_error caught;
  if (!qa_try(registration_send_body, &r, &caught)) registration_finish_error(&r, caught.message);
  /* The timer and abort listener, with the transport pumped in between. */
  while (!r.done) {
    if (aborted && *aborted) { registration_finish_error(&r, "DOSBox IPX registration aborted"); break; }
    if (monotonic_ms() >= deadline) { registration_finish_error(&r, "DOSBox IPX registration timed out"); break; }
    struct timespec pause_for = { 0, 1000000 };
    nanosleep(&pause_for, NULL);
    registration_read(&r);
  }
  scope->udp.unsubscribe(scope->udp.ctx, subscription);
  if (r.failed) qa_raise("Error", 0, 0, "%s", r.failure);
  qa_dosbox_ipx_network *n = scope->n;
  n->udp = scope->udp;
  (void)qa_net_address_clone(scope->server, &n->server);
  n->address = r.assigned;
  n->now = options && options->now ? options->now : qa_net_default_now;
  n->now_ctx = options ? options->now_ctx : NULL;
  n->subscription = scope->udp.subscribe(scope->udp.ctx, udp_readable, n);
}
int qa_dosbox_ipx_network_connect(qa_dosbox_ipx_network *n, qa_datagram_transport udp,
                                  const qa_net_address *server, const qa_dosbox_connect_options *options) {
  memset(n, 0, sizeof *n);
  struct connect_scope scope = { n, udp, server, options };
  qa_caught_error caught;
  /* Takes ownership of the UDP socket, including on cancellation or registration failure. */
  if (!qa_try(connect_body, &scope, &caught)) {
    qa_net_address_destroy(&n->server); qa_net_address_destroy(&n->address);
    memset(n, 0, sizeof *n);
    udp.close(udp.ctx);
    qa_rethrow(&caught);
  }
  return 1;
}
void qa_dosbox_ipx_network_destroy(qa_dosbox_ipx_network *n) {
  qa_dosbox_ipx_network_close(n);
  for (size_t i = 0; i < n->bound_count; i++) {
    qa_packet_queue_destroy(&n->bound[i]->queue); qa_net_address_destroy(&n->bound[i]->address); free(n->bound[i]);
  }
  qa_net_address_destroy(&n->server);
  qa_net_address_destroy(&n->address);
  free(n->sockets); free(n->ports); free(n->bound);
  memset(n, 0, sizeof *n);
}
bool qa_dosbox_ipx_network_closed(const qa_dosbox_ipx_network *n) { return n->ended; }
int qa_dosbox_ipx_network_get_server(const qa_dosbox_ipx_network *n, qa_net_address *out) { return qa_net_address_clone(&n->server, out); }
int qa_dosbox_ipx_network_get_address(const qa_dosbox_ipx_network *n, qa_net_address *out) { return qa_net_address_clone(&n->address, out); }
static void *grow(void *items, size_t *capacity, size_t count, size_t size) {
  if (count < *capacity) return items;
  size_t next = *capacity ? *capacity * 2 : 8;
  void *grown = realloc(items, next * size);
  if (!grown) qa_range_error("Out of memory");
  *capacity = next; return grown;
}
qa_dosbox_ipx_socket *qa_dosbox_ipx_network_bind(qa_dosbox_ipx_network *n, double requested_port, double packet_type) {
  if (n->ended) qa_raise("Error", 0, 0, "DOSBox IPX network is closed");
  if (!(requested_port >= 0 && requested_port <= 65535) || requested_port != trunc(requested_port)) qa_range_error("Invalid network port");
  if (packet_type != trunc(packet_type) || packet_type < 0 || packet_type > 255) qa_range_error("Invalid IPX packet type");
  if (n->socket_count >= 150) qa_raise("Error", 0, 0, "DOSBox IPX socket table is full");
  int64_t port = (int64_t)requested_port;
  if (port == 0) {
    port = 0x4002;
    while (socket_at(n, port) && port < 0x7fff) port++;
  }
  if (port == 2 || socket_at(n, port)) qa_raise("Error", 0, 0, "IPX socket is already bound or reserved");
  qa_dosbox_ipx_socket *s = calloc(1, sizeof *s);
  if (!s) qa_range_error("Out of memory");
  int64_t node[6]; for (int i = 0; i < 6; i++) node[i] = n->address.node[i];
  (void)qa_net_make_ipx(&s->address, n->address.network, node, port);
  s->network = n; s->packet_type = packet_type;
  qa_datagram_limits lim = { DOSBOX_PACKET_BYTES - 30, 256 };
  (void)qa_packet_queue_init(&s->queue, lim, n->now, n->now_ctx);
  /* The network owns every socket it bound until destroy, so a socket closed with its
   * network stays a valid closed object, as the TS object does. */
  n->bound = grow(n->bound, &n->bound_cap, n->bound_count, sizeof *n->bound);
  n->bound[n->bound_count++] = s;
  size_t cap = n->socket_cap;
  n->sockets = grow(n->sockets, &cap, n->socket_count, sizeof *n->sockets);
  n->ports = grow(n->ports, &n->socket_cap, n->socket_count, sizeof *n->ports);
  n->sockets[n->socket_count] = s; n->ports[n->socket_count] = port; n->socket_count++;
  net_receive(n);
  return s;
}
bool qa_dosbox_ipx_network_send(qa_dosbox_ipx_network *n, const qa_ipx_packet *packet) {
  if (n->ended) qa_raise("Error", 0, 0, "DOSBox IPX network is closed");
  if ((long long)packet->length + 30 > DOSBOX_PACKET_BYTES) {
    qa_dosbox_ipx_socket *s = socket_at(n, packet->from.port);
    if (s) socket_error(s, "Packet exceeds DOSBox IPX capacity (1394 payload bytes)");
    return false;
  }
  bool local = qa_net_same_address(&packet->to, &n->address, false);
  size_t wire_len = 0; uint8_t *wire = qa_encode_ipx_packet(packet, &wire_len);
  qa_cleanup guard; qa_cleanup_push(&guard, free, wire);
  bool sent = local || n->udp.send(n->udp.ctx, &n->server, wire, wire_len);
  qa_cleanup_pop(&guard); free(wire);
  if (local || node_broadcast(&packet->to)) net_deliver(n, packet);
  return sent;
}
static void net_deliver(qa_dosbox_ipx_network *n, const qa_ipx_packet *packet) {
  bool local = qa_net_same_address(&packet->to, &n->address, false);
  bool bcast_ok = node_broadcast(&packet->to) && (packet->to.network == 0 || packet->to.network == n->address.network);
  if (!local && !bcast_ok) return;
  if (packet->to.port == 2) {
    if (node_broadcast(&packet->to) && packet->from.port == 2 && packet->length == 0) {
      size_t wl = 0; uint8_t *wire = empty_control_packet(&n->address, &packet->from, &wl);
      qa_cleanup guard; qa_cleanup_push(&guard, free, wire);
      (void)n->udp.send(n->udp.ctx, &n->server, wire, wl);
      qa_cleanup_pop(&guard); free(wire);
    }
    return;
  }
  qa_dosbox_ipx_socket *s = socket_at(n, packet->to.port);
  if (s) socket_accept(s, packet);
}
static void net_receive(qa_dosbox_ipx_network *n) {
  if (n->ended) return;
  if (n->udp.is_closed(n->udp.ctx)) { qa_dosbox_ipx_network_close(n); return; }
  while (!n->ended) {
    qa_receive_event event;
    if (!n->udp.poll(n->udp.ctx, &event)) return;
    if (event.kind == QA_RECV_ERROR) {
      for (size_t i = 0; i < n->socket_count; i++) socket_error(n->sockets[i], event.error ? event.error : "Error");
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
struct close_sockets_scope { qa_dosbox_ipx_network *n; };
static void close_sockets_body(void *ctx) {
  qa_dosbox_ipx_network *n = ctx;
  while (n->socket_count > 0) qa_dosbox_ipx_socket_close(n->sockets[0]);
}
void qa_dosbox_ipx_network_close(qa_dosbox_ipx_network *n) {
  if (n->ended) return;
  n->ended = true;
  if (n->udp.unsubscribe) n->udp.unsubscribe(n->udp.ctx, n->subscription);
  /* try { close every socket } finally { udp.close() } */
  qa_caught_error caught;
  bool ok = qa_try(close_sockets_body, n, &caught);
  if (n->udp.close) n->udp.close(n->udp.ctx);
  if (!ok) qa_rethrow(&caught);
}
bool qa_dosbox_ipx_socket_closed(const qa_dosbox_ipx_socket *s) { return s->ended; }
int qa_dosbox_ipx_socket_get_address(const qa_dosbox_ipx_socket *s, qa_net_address *out) { return qa_net_address_clone(&s->address, out); }
static void sock_opened(const qa_dosbox_ipx_socket *s) { if (s->ended) qa_raise("Error", 0, 0, "DOSBox IPX socket is closed"); }
bool qa_dosbox_ipx_socket_send(qa_dosbox_ipx_socket *s, const qa_net_address *to, const uint8_t *payload, size_t length) {
  sock_opened(s);
  qa_ipx_packet packet; memset(&packet, 0, sizeof packet);
  packet.from = s->address; packet.to = *to;
  packet.packet_type = (int64_t)s->packet_type; packet.hops = 0;
  packet.payload = (uint8_t *)payload; packet.length = length;
  return qa_dosbox_ipx_network_send(s->network, &packet);
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
    memmove(&n->sockets[i], &n->sockets[i + 1], (n->socket_count - i - 1) * sizeof *n->sockets);
    memmove(&n->ports[i], &n->ports[i + 1], (n->socket_count - i - 1) * sizeof *n->ports);
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
