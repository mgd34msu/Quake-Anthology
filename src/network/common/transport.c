/* Port of src/network/common/transport.ts. GPL-2.0-or-later. */
#define _DEFAULT_SOURCE
#include "qa/network/common/transport.h"
#include "qa/foundation/support.h"
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netdb.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>

const qa_datagram_limits QA_Q3_DATAGRAM_LIMITS_NET = { 16383, 256 };
const qa_datagram_limits QA_Q2_DATAGRAM_LIMITS_NET = { 4096, 256 };
const qa_datagram_limits QA_UNIFIED_DATAGRAM_LIMITS_NET = { 65507, 256 };

void qa_receive_event_destroy(qa_receive_event *e) {
  qa_net_address_destroy(&e->from); free(e->payload); free(e->error);
  e->payload = NULL; e->error = NULL;
}
double qa_net_default_now(void *ctx) {
  (void)ctx; struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}
static int is_safe(long long v) { return v >= -QA_MAX_SAFE_INTEGER && v <= QA_MAX_SAFE_INTEGER; }
static char *dupstr(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }

int qa_packet_queue_init(qa_packet_queue *q, qa_datagram_limits limits, qa_net_now_fn now, void *now_ctx) {
  if (!is_safe(limits.max_bytes) || limits.max_bytes < 1 || !is_safe(limits.queue_packets) || limits.queue_packets < 1)
    qa_range_error("Invalid datagram limits");
  memset(q, 0, sizeof *q); q->limits = limits;
  q->now = now ? now : qa_net_default_now; q->now_ctx = now_ctx;
  q->capacity = (size_t)limits.queue_packets;
  q->events = calloc(q->capacity, sizeof *q->events);
  return q->events != NULL;
}
void qa_packet_queue_destroy(qa_packet_queue *q) {
  for (size_t i = 0; i < q->count; i++) qa_receive_event_destroy(&q->events[(q->head + i) % q->capacity]);
  free(q->events); free(q->listeners); memset(q, 0, sizeof *q);
}
qa_datagram_limits qa_packet_queue_get_limits(const qa_packet_queue *q) { return q->limits; }
long long qa_packet_queue_dropped(const qa_packet_queue *q) { return q->dropped; }
bool qa_packet_queue_closed(const qa_packet_queue *q) { return q->ended; }
void qa_packet_queue_push_event(qa_packet_queue *q, qa_receive_event *event) {
  if (q->ended) { qa_receive_event_destroy(event); return; }
  if ((long long)q->count == q->limits.queue_packets) q->dropped++;
  /* BoundedQueue push on full replaces oldest (foundation semantics mirror:
   * TS pushes onto a full BoundedQueue; dropped counter already bumped). */
  size_t idx;
  if (q->count == q->capacity) { idx = q->head; qa_receive_event_destroy(&q->events[idx]); q->head = (q->head + 1) % q->capacity; }
  else { idx = (q->head + q->count) % q->capacity; q->count++; }
  q->events[idx] = *event; memset(event, 0, sizeof *event);
  /* Notify a snapshot, skipping listeners removed during notification. */
  size_t n = q->listener_count;
  struct qa_net_listener *snap = malloc((n ? n : 1) * sizeof *snap);
  if (!snap) return;
  if (n) memcpy(snap, q->listeners, n * sizeof *snap);
  for (size_t i = 0; i < n; i++) {
    bool alive = false;
    for (size_t j = 0; j < q->listener_count; j++) if (q->listeners[j].id == snap[i].id) alive = true;
    if (alive) snap[i].fn(snap[i].ctx);
  }
  free(snap);
}
void qa_packet_queue_accept(qa_packet_queue *q, const qa_net_address *from,
                            const uint8_t *payload, size_t length, bool truncated) {
  if (q->ended) return;
  qa_receive_event e; memset(&e, 0, sizeof e);
  (void)qa_net_address_clone(from, &e.from);
  if (truncated || (long long)length > q->limits.max_bytes) {
    q->dropped++; e.kind = QA_RECV_DROPPED; e.reason = "oversize";
    qa_packet_queue_push_event(q, &e); return;
  }
  e.kind = QA_RECV_PACKET; e.length = length; e.received_at = q->now(q->now_ctx);
  e.payload = malloc(length ? length : 1); if (length) memcpy(e.payload, payload, length);
  qa_packet_queue_push_event(q, &e);
}
void qa_packet_queue_push_error(qa_packet_queue *q, const char *message) {
  qa_receive_event e; memset(&e, 0, sizeof e);
  e.kind = QA_RECV_ERROR; e.error = dupstr(message);
  qa_packet_queue_push_event(q, &e);
}
int qa_packet_queue_poll(qa_packet_queue *q, qa_receive_event *out) {
  if (q->count == 0) return 0;
  *out = q->events[q->head]; memset(&q->events[q->head], 0, sizeof *out);
  q->head = (q->head + 1) % q->capacity; q->count--; return 1;
}
long long qa_packet_queue_subscribe(qa_packet_queue *q, qa_net_listener_fn fn, void *ctx) {
  if (q->ended) qa_raise("Error", 0, 0, "Packet queue is closed");
  if (q->listener_count == q->listener_cap) {
    size_t nc = q->listener_cap ? q->listener_cap * 2 : 4;
    struct qa_net_listener *nl = realloc(q->listeners, nc * sizeof *nl); if (!nl) return -1;
    q->listeners = nl; q->listener_cap = nc;
  }
  long long id = q->next_id++;
  q->listeners[q->listener_count++] = (struct qa_net_listener){ id, fn, ctx };
  return id;
}
void qa_packet_queue_unsubscribe(qa_packet_queue *q, long long id) {
  for (size_t i = 0; i < q->listener_count; i++) if (q->listeners[i].id == id) {
    memmove(&q->listeners[i], &q->listeners[i + 1], (q->listener_count - i - 1) * sizeof *q->listeners);
    q->listener_count--; return;
  }
}
void qa_packet_queue_close(qa_packet_queue *q) {
  if (q->ended) return;
  q->ended = true;
  for (size_t i = 0; i < q->count; i++) qa_receive_event_destroy(&q->events[(q->head + i) % q->capacity]);
  q->count = 0; q->head = 0;
  size_t n = q->listener_count;
  struct qa_net_listener *snap = malloc((n ? n : 1) * sizeof *snap);
  if (snap && n) memcpy(snap, q->listeners, n * sizeof *snap);
  q->listener_count = 0;
  if (snap) { for (size_t i = 0; i < n; i++) snap[i].fn(snap[i].ctx); free(snap); }
}

static void udp_opened(const qa_udp_transport *t) { if (t->ended) qa_raise("Error", 0, 0, "UDP transport is closed"); }
static int64_t bound_address(int fd, char num[INET6_ADDRSTRLEN]) {
  struct sockaddr_storage ss; socklen_t sl = sizeof ss;
  memset(&ss, 0, sizeof ss);
  getsockname(fd, (struct sockaddr *)&ss, &sl);
  if (ss.ss_family == AF_INET) {
    inet_ntop(AF_INET, &((struct sockaddr_in *)&ss)->sin_addr, num, INET6_ADDRSTRLEN);
    return ntohs(((struct sockaddr_in *)&ss)->sin_port);
  }
  inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&ss)->sin6_addr, num, INET6_ADDRSTRLEN);
  return ntohs(((struct sockaddr_in6 *)&ss)->sin6_port);
}
/* Numeric host (with an optional IPv6 %scope) to a socket address. */
static bool numeric_sockaddr(const char *host, int64_t port, struct sockaddr_storage *ss, socklen_t *sl) {
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof hints);
  hints.ai_flags = AI_NUMERICHOST; hints.ai_socktype = SOCK_DGRAM;
  if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return false;
  memcpy(ss, res->ai_addr, res->ai_addrlen); *sl = (socklen_t)res->ai_addrlen;
  freeaddrinfo(res);
  if (ss->ss_family == AF_INET) ((struct sockaddr_in *)ss)->sin_port = htons((uint16_t)port);
  else ((struct sockaddr_in6 *)ss)->sin6_port = htons((uint16_t)port);
  return true;
}
int qa_udp_transport_bind(qa_udp_transport *t, const qa_udp_bind_options *opts) {
  qa_net_address check; (void)qa_net_make_ip(&check, opts->host, opts->port, true); qa_net_address_destroy(&check);
  memset(t, 0, sizeof *t); t->fd = -1;
  qa_datagram_limits limits = opts->limits ? *opts->limits : QA_UNIFIED_DATAGRAM_LIMITS_NET;
  if (!qa_packet_queue_init(&t->queue, limits, opts->now, opts->now_ctx)) return 0;
  (void)qa_net_port_number(opts->port, true);
  /* Bun.udpSocket: a failed socket or bind rejects with the system error; the queue
   * built for it is released first. */
  struct sockaddr_storage ss; socklen_t sl = 0;
  if (!numeric_sockaddr(opts->host, opts->port, &ss, &sl)) {
    qa_packet_queue_destroy(&t->queue);
    qa_raise("Error", EINVAL, 0, "bind EINVAL %s", opts->host);
  }
  int fd = socket(ss.ss_family, SOCK_DGRAM, 0);
  if (fd < 0 || bind(fd, (struct sockaddr *)&ss, sl) != 0) {
    int code = errno;
    if (fd >= 0) close(fd);
    qa_packet_queue_destroy(&t->queue);
    qa_raise("Error", code, 0, "bind %s", strerror(code));
  }
  int flags = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  if (opts->broadcast) {
    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one) != 0) {
      close(fd); qa_packet_queue_destroy(&t->queue);
      qa_raise("Error", 0, 0, "Could not enable UDP broadcast");
    }
  }
  t->fd = fd;
  char num[INET6_ADDRSTRLEN]; int64_t bound_port = bound_address(fd, num);
  (void)qa_net_make_ip(&t->address, num, bound_port, false);
  (void)qa_socks_association_init(&t->socks);
  return 1;
}
void qa_udp_transport_destroy(qa_udp_transport *t) {
  qa_udp_transport_close(t); qa_packet_queue_destroy(&t->queue); qa_net_address_destroy(&t->address);
}
bool qa_udp_transport_closed(const qa_udp_transport *t) { return t->ended; }
long long qa_udp_transport_dropped_packets(const qa_udp_transport *t) { return qa_packet_queue_dropped(&t->queue); }
int qa_udp_transport_get_address(const qa_udp_transport *t, qa_net_address *out) { return qa_net_address_clone(&t->address, out); }
struct socks_scope { qa_udp_transport *t; const qa_socks_options *opts; };
static void socks_open_body(void *ctx) {
  struct socks_scope *scope = ctx;
  qa_socks_association_open(&scope->t->socks, scope->opts, scope->t->address.port);
}
void qa_udp_transport_connect_socks(qa_udp_transport *t, const qa_socks_options *opts) {
  udp_opened(t);
  if (t->address.kind != QA_NET_IPV4) qa_raise("Error", 0, 0, "SOCKS UDP association requires IPv4");
  if (t->has_socks) qa_socks_association_close(&t->socks);
  (void)qa_socks_association_init(&t->socks); t->has_socks = true;
  struct socks_scope scope = { t, opts };
  qa_caught_error caught;
  if (!qa_try(socks_open_body, &scope, &caught)) { qa_socks_association_close(&t->socks); qa_rethrow(&caught); }
  udp_opened(t);
}
struct source_scope { const char *host; int64_t port; qa_net_address *from; };
static void source_address_body(void *ctx) {
  struct source_scope *scope = ctx;
  (void)qa_net_make_ip(scope->from, scope->host, scope->port, false);
}
/* The Bun data callback per datagram. Nested polls from readable listeners only drain
 * the queue: TS delivers datagrams from the event loop, never recursively. */
static void udp_pump(qa_udp_transport *t) {
  if (t->pumping || t->fd < 0) return;
  t->pumping = true;
  uint8_t *buf = malloc(65507);
  if (!buf) { t->pumping = false; return; }
  for (;;) {
    struct sockaddr_storage ss; socklen_t sl = sizeof ss;
    memset(&ss, 0, sizeof ss);
    ssize_t n = recvfrom(t->fd, buf, 65507, MSG_TRUNC, (struct sockaddr *)&ss, &sl);
    if (n < 0) {
      if (errno == EINTR) continue;
      /* A pending ICMP error is reported by the socket error callback. */
      if (errno != EAGAIN && errno != EWOULDBLOCK) qa_packet_queue_push_error(&t->queue, strerror(errno));
      if (errno == EAGAIN || errno == EWOULDBLOCK || t->ended) break;
      continue;
    }
    bool truncated = (size_t)n > 65507;
    size_t len = truncated ? 65507 : (size_t)n;
    char num[INET6_ADDRSTRLEN + IF_NAMESIZE + 2]; int64_t port;
    if (ss.ss_family == AF_INET) {
      inet_ntop(AF_INET, &((struct sockaddr_in *)&ss)->sin_addr, num, sizeof num);
      port = ntohs(((struct sockaddr_in *)&ss)->sin_port);
    } else if (ss.ss_family == AF_INET6) {
      struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
      inet_ntop(AF_INET6, &sin6->sin6_addr, num, sizeof num);
      if (sin6->sin6_scope_id != 0) {
        char name[IF_NAMESIZE];
        size_t used = strlen(num);
        if (if_indextoname(sin6->sin6_scope_id, name)) snprintf(num + used, sizeof num - used, "%%%s", name);
        else snprintf(num + used, sizeof num - used, "%%%u", (unsigned)sin6->sin6_scope_id);
      }
      port = ntohs(sin6->sin6_port);
    } else continue;
    /* A source port of 0 cannot be answered or attributed; it is dropped rather than reported. */
    if (port == 0) continue;
    /* try { from = ipAddress(host, port) } catch (error) { queue.push(error event) } */
    qa_net_address from; memset(&from, 0, sizeof from);
    struct source_scope scope = { num, port, &from };
    qa_caught_error caught;
    if (!qa_try(source_address_body, &scope, &caught)) qa_packet_queue_push_error(&t->queue, caught.message);
    else { qa_packet_queue_accept(&t->queue, &from, buf, len, truncated); qa_net_address_destroy(&from); }
    if (t->ended) break;
  }
  free(buf);
  t->pumping = false;
}
bool qa_udp_transport_send(qa_udp_transport *t, const qa_net_address *to, const uint8_t *payload, size_t length) {
  udp_opened(t);
  if (to->kind == QA_NET_IPV4 || to->kind == QA_NET_IPV6) (void)qa_net_port_number(to->port, false);
  if ((long long)length > qa_packet_queue_get_limits(&t->queue).max_bytes)
    qa_range_error("Datagram exceeds selected transport limit");
  qa_net_address relay; bool has_relay = t->has_socks && qa_socks_association_get_relay(&t->socks, &relay);
  if (t->has_socks && (!has_relay || to->kind != QA_NET_IPV4)) {
    if (has_relay) qa_net_address_destroy(&relay);
    qa_raise("Error", 0, 0, "SOCKS association unavailable or destination unsupported");
  }
  bool broadcast_all = to->kind == QA_NET_IPV4 && to->ipv4[0] == 255 && to->ipv4[1] == 255 && to->ipv4[2] == 255 && to->ipv4[3] == 255;
  bool proxied = to->kind == QA_NET_IPV4 && has_relay && !broadcast_all;
  const qa_net_address *dest = proxied ? &relay : to;
  uint8_t *owned = NULL; const uint8_t *bytes = payload; size_t blen = length;
  if (proxied) { owned = qa_socks_datagram(to, payload, length, &blen); bytes = owned; }
  char *host = qa_net_address_host(dest);
  struct sockaddr_storage ss; socklen_t sl = 0;
  bool addressable = host && numeric_sockaddr(host, dest->port, &ss, &sl);
  bool result = false;
  for (int attempt = 0; addressable; attempt++) {
    ssize_t sent = sendto(t->fd, bytes, blen, 0, (struct sockaddr *)&ss, sl);
    if (sent >= 0) { result = (size_t)sent == blen; break; }
    if (errno == EINTR) continue;
    /* Bun's send reports backpressure as false. */
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS) { result = false; break; }
    /* Linux delivers pending ICMP errors (e.g. port-unreachable from an earlier datagram
     * to a dead endpoint) by failing the next sendmsg without transmitting. The failed
     * call consumes one queued error, so retry a bounded number of times before
     * reporting the send as lost. */
    char message[160]; snprintf(message, sizeof message, "send %s", strerror(errno));
    qa_packet_queue_push_error(&t->queue, message);
    if (attempt >= 8) { result = false; break; }
  }
  free(host); free(owned);
  if (has_relay) qa_net_address_destroy(&relay);
  return result;
}
int qa_udp_transport_poll(qa_udp_transport *t, qa_receive_event *out) {
  udp_opened(t); udp_pump(t);
  for (;;) {
    if (!qa_packet_queue_poll(&t->queue, out)) return 0;
    qa_net_address relay; bool has_relay = t->has_socks && qa_socks_association_get_relay(&t->socks, &relay);
    if (!has_relay) return 1;
    bool from_relay = out->kind == QA_RECV_PACKET && out->from.kind == QA_NET_IPV4
        && out->from.port == relay.port && memcmp(out->from.ipv4, relay.ipv4, 4) == 0;
    qa_net_address_destroy(&relay);
    if (!from_relay) return 1;
    qa_net_address from; uint8_t *payload = NULL; size_t plen = 0;
    int ok = qa_socks_read_datagram(out->payload, out->length, &from, &payload, &plen);
    double at = out->received_at;
    qa_receive_event_destroy(out); memset(out, 0, sizeof *out);
    /* A malformed relay datagram is skipped; 0 must keep meaning an empty queue. */
    if (!ok) continue;
    out->kind = QA_RECV_PACKET; out->from = from; out->payload = payload; out->length = plen; out->received_at = at;
    return 1;
  }
}
long long qa_udp_transport_subscribe(qa_udp_transport *t, qa_net_listener_fn fn, void *ctx) {
  udp_opened(t); return qa_packet_queue_subscribe(&t->queue, fn, ctx);
}
void qa_udp_transport_unsubscribe(qa_udp_transport *t, long long id) { qa_packet_queue_unsubscribe(&t->queue, id); }
static void close_queue_body(void *ctx) { qa_packet_queue_close(ctx); }
void qa_udp_transport_close(qa_udp_transport *t) {
  if (t->ended) return;
  t->ended = true;
  if (t->has_socks) { qa_socks_association_close(&t->socks); t->has_socks = false; }
  /* try { queue.close() } finally { socket.close() } */
  qa_caught_error caught;
  bool ok = qa_try(close_queue_body, &t->queue, &caught);
  if (t->fd >= 0) { close(t->fd); t->fd = -1; }
  if (!ok) qa_rethrow(&caught);
}
static int udp_vt_address(void *ctx, qa_net_address *out) { return qa_udp_transport_get_address(ctx, out); }
static bool udp_vt_closed(void *ctx) { return qa_udp_transport_closed(ctx); }
static int64_t udp_vt_max(void *ctx) { (void)ctx; return -1; }
static bool udp_vt_send(void *ctx, const qa_net_address *to, const uint8_t *p, size_t n) { return qa_udp_transport_send(ctx, to, p, n); }
static int udp_vt_poll(void *ctx, qa_receive_event *out) { return qa_udp_transport_poll(ctx, out); }
static long long udp_vt_sub(void *ctx, qa_net_listener_fn fn, void *c) { return qa_udp_transport_subscribe(ctx, fn, c); }
static void udp_vt_unsub(void *ctx, long long id) { qa_udp_transport_unsubscribe(ctx, id); }
static void udp_vt_close(void *ctx) { qa_udp_transport_close(ctx); }
qa_datagram_transport qa_udp_transport_as_transport(qa_udp_transport *t) {
  qa_datagram_transport v; v.ctx = t; v.get_address = udp_vt_address; v.is_closed = udp_vt_closed;
  v.max_datagram_bytes = udp_vt_max; v.send = udp_vt_send; v.poll = udp_vt_poll;
  v.subscribe = udp_vt_sub; v.unsubscribe = udp_vt_unsub; v.close = udp_vt_close; return v;
}
void qa_unsupported_transport_error(const char *transport, const char *detail) {
  qa_raise("UnsupportedTransportError", 0, 0, "%s: %s", transport, detail);
}
static void udp_connect_socks(void *ctx, const qa_socks_options *options) { qa_udp_transport_connect_socks(ctx, options); }
qa_socks_connector qa_udp_transport_as_socks_connector(qa_udp_transport *t) {
  qa_socks_connector connector = { t, udp_connect_socks };
  return connector;
}
