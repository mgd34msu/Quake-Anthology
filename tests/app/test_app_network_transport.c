/* Ports tests/app/network-transport.test.ts (IPX remote parsing, DOSBox application transport
 * over an in-memory UDP socket, UDP SOCKS ownership, native IPX refusal, cancellation) and the
 * unit layer of tests/first-playable/remote-socks.test.ts: a threaded SOCKS5 proxy with a UDP
 * relay authenticates, relays both directions, a stalled proxy fails the negotiation, and a
 * dropped control connection never falls back to direct UDP. The Application/RemoteApplication
 * cases need the application layer and are not run here. Also checks the saved SOCKS settings
 * connector and the client download permission policy. GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "qa/app/bootstrap/network/transport.h"
#include "qa/foundation/support.h"
#include "qa/network/common/ipx.h"
#include "qa/network/common/socks.h"
#include "qa/network/common/transport.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static int sentinel;
#define SENTINEL ((void *)&sentinel)

/* MemorySocket / MemoryUdp */
typedef struct memory_udp {
  void *sentinel;
  qa_packet_queue queue; qa_net_address address;
  qa_net_address sent_to[8]; uint8_t sent[8][64]; size_t sent_length[8]; int sent_count;
  bool closed, registration; int socks, released;
} memory_udp;
static const uint8_t ack_bytes[30] = {255, 255, 0, 30, 0, 0, 0, 0, 0, 0, 127, 0, 0, 1, 160, 1, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 213, 0, 2};
static double clock_one(void *ctx) { CHECK(ctx == SENTINEL); return 1; }
static int m_address(void *ctx, qa_net_address *out) { memory_udp *m = ctx; CHECK(m->sentinel == SENTINEL); return qa_net_address_clone(&m->address, out); }
static bool m_closed(void *ctx) { return ((memory_udp *)ctx)->closed; }
static int64_t m_max(void *ctx) { (void)ctx; return -1; }
static bool m_send(void *ctx, const qa_net_address *to, const uint8_t *payload, size_t length) {
  memory_udp *m = ctx; CHECK(m->sentinel == SENTINEL);
  int i = m->sent_count++ % 8;
  qa_net_address_destroy(&m->sent_to[i]); qa_net_address_clone(to, &m->sent_to[i]);
  memcpy(m->sent[i], payload, length < 64 ? length : 64); m->sent_length[i] = length;
  if (m->registration && m->sent_count == 1) qa_packet_queue_accept(&m->queue, to, ack_bytes, 30, false);
  return true;
}
static int m_poll(void *ctx, qa_receive_event *out) { return qa_packet_queue_poll(&((memory_udp *)ctx)->queue, out); }
static long long m_sub(void *ctx, qa_net_listener_fn fn, void *c) { return qa_packet_queue_subscribe(&((memory_udp *)ctx)->queue, fn, c); }
static void m_unsub(void *ctx, long long id) { qa_packet_queue_unsubscribe(&((memory_udp *)ctx)->queue, id); }
static void m_close(void *ctx) { memory_udp *m = ctx; if (m->closed) return; m->closed = true; qa_packet_queue_close(&m->queue); }
static void m_socks(void *ctx, const qa_socks_options *options) { memory_udp *m = ctx; CHECK(m->sentinel == SENTINEL); (void)options; m->socks++; }
static void m_release(void *ctx) { ((memory_udp *)ctx)->released++; }
static void memory_init(memory_udp *m, int64_t port) {
  memset(m, 0, sizeof *m); m->sentinel = SENTINEL;
  qa_datagram_limits limits = {65507, 256};
  qa_packet_queue_init(&m->queue, limits, clock_one, SENTINEL);
  int64_t host[4] = {127, 0, 0, 1};
  qa_net_make_ipv4(&m->address, host, port, false);
}
static void memory_destroy(memory_udp *m) {
  qa_packet_queue_destroy(&m->queue); qa_net_address_destroy(&m->address);
  for (int i = 0; i < 8; i++) qa_net_address_destroy(&m->sent_to[i]);
}
static qa_app_udp_socket memory_socket(memory_udp *m) {
  qa_app_udp_socket s;
  qa_datagram_transport t = {m, m_address, m_closed, m_max, m_send, m_poll, m_sub, m_unsub, m_close};
  s.transport = t; s.socks.ctx = m; s.socks.connect_socks = m_socks; s.release = m_release;
  return s;
}
typedef struct caps_ctx { void *sentinel; memory_udp *udp; int binds; bool expect_relay; bool abort_on_bind; bool *aborted; } caps_ctx;
static bool caps_bind(void *ctx, const qa_udp_bind_options *options, qa_app_udp_socket *out) {
  caps_ctx *c = ctx; CHECK(c->sentinel == SENTINEL);
  c->binds++;
  if (c->expect_relay) { CHECK(options->port == 0); CHECK(options->limits && options->limits->max_bytes == 1424); }
  if (c->abort_on_bind) *c->aborted = true;
  *out = memory_socket(c->udp);
  return true;
}
static qa_datagram_limits limits = {4096, 256};
static const qa_socks_options proxy = {"localhost", 1080, "", ""};

struct parse_scope { const char *text; qa_net_address out; };
static void parse_body(void *ctx) { struct parse_scope *s = ctx; s->out = qa_app_parse_ipx_remote(s->text, 26000); }
struct resolve_scope { const char *text; double port; qa_app_network_transport_selection selection; qa_app_network_family family; qa_net_address out; };
static void resolve_body(void *ctx) { struct resolve_scope *s = ctx; s->out = qa_app_resolve_application_address(s->text, s->port, s->selection, s->family); }
static void test_ipx_remotes(void) {
  int64_t node[6] = {0, 17, 34, 51, 68, 255};
  qa_net_address expected; qa_net_make_ipx(&expected, 0x12345678, node, 26000);
  qa_net_address parsed = qa_app_parse_ipx_remote("12345678:0011223344ff", 26000);
  CHECK(qa_net_same_address(&parsed, &expected, true));
  qa_app_network_transport_selection native = {QA_APP_TRANSPORT_IPX_NATIVE, NULL};
  qa_net_address resolved = qa_app_resolve_application_address("ipx:12345678:0011223344FF:26000", 27960, native, QA_APP_FAMILY_Q3);
  CHECK(qa_net_same_address(&resolved, &expected, true));
  static const char *invalid[] = {"localhost:27960", "0000:001122334455", "00000000:001122334455:0", "00000000:001122334455:65536",
    "00000000:0011223344zz:1", "00000000:001122334455:999999999999999999999999"};
  qa_caught_error caught;
  for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
    struct parse_scope s = {invalid[i], {0}};
    CHECK(!qa_try(parse_body, &s, &caught));
  }
  struct resolve_scope r = {"12345678:0011223344ff", 27500, native, QA_APP_FAMILY_QW, {0}};
  CHECK(!qa_try(resolve_body, &r, &caught) && strstr(caught.message, "QuakeWorld"));
}
struct open_scope { qa_app_open_transport_options options; const qa_app_transport_capabilities *caps; qa_app_application_transport *out; };
static void open_body(void *ctx) { struct open_scope *s = ctx; s->out = qa_app_open_application_transport(&s->options, s->caps); }
static void socks_body(void *ctx) { qa_app_application_transport_connect_socks(ctx, &proxy); }
static void udp_socket_body(void *ctx) { (void)qa_app_application_transport_udp_socket(ctx); }
struct send_scope { qa_app_application_transport *t; const qa_net_address *to; };
static void send_body(void *ctx) { struct send_scope *s = ctx; (void)qa_app_application_transport_send(s->t, s->to, NULL, 0); }
static void test_dosbox_application(void) {
  const qa_app_network_family families[3] = {QA_APP_FAMILY_Q1, QA_APP_FAMILY_Q2, QA_APP_FAMILY_Q3};
  for (int i = 0; i < 3; i++) {
    memory_udp udp; memory_init(&udp, 40961); udp.registration = true;
    caps_ctx c = {SENTINEL, &udp, 0, true, false, NULL};
    qa_app_transport_capabilities caps = {&c, {NULL, false, "test host", NULL}, caps_bind};
    qa_app_open_transport_options options = {{QA_APP_TRANSPORT_IPX_DOSBOX, "127.0.0.1:213"}, families[i], "0.0.0.0", 26000, limits, NULL};
    qa_app_application_transport *transport = qa_app_open_application_transport(&options, &caps);
    qa_net_address remote = qa_app_parse_ipx_remote("00000000:7f000001a002:26000", 26000);
    uint8_t data[2] = {1, 2};
    qa_app_application_transport_send(transport, &remote, data, 2);
    int last = (udp.sent_count - 1) % 8;
    int64_t relay_host[4] = {127, 0, 0, 1};
    qa_net_address relay; qa_net_make_ipv4(&relay, relay_host, 213, false);
    CHECK(qa_net_same_address(&udp.sent_to[last], &relay, true));
    qa_ipx_packet decoded;
    CHECK(qa_decode_ipx_packet(udp.sent[last], udp.sent_length[last], &decoded));
    if (families[i] == QA_APP_FAMILY_Q1) { static const uint8_t q1[6] = {0, 0, 0, 0, 1, 2}; CHECK(decoded.length == 6 && memcmp(decoded.payload, q1, 6) == 0); }
    else CHECK(decoded.length == 2 && memcmp(decoded.payload, data, 2) == 0);
    qa_ipx_packet_destroy(&decoded);
    qa_caught_error caught;
    CHECK(!qa_try(socks_body, transport, &caught) && strstr(caught.message, "SOCKS"));
    CHECK(!qa_try(udp_socket_body, transport, &caught) && strstr(caught.message, "requires a UDP"));
    int64_t loop[4] = {127, 0, 0, 1};
    qa_net_address ip; qa_net_make_ipv4(&ip, loop, 26000, false);
    struct send_scope ss = {transport, &ip};
    CHECK(!qa_try(send_body, &ss, &caught) && strstr(caught.message, "IPX destination"));
    qa_app_application_transport_close(transport); qa_app_application_transport_close(transport);
    CHECK(udp.closed);
    qa_app_application_transport_destroy(transport);
    CHECK(udp.released == 1);
    memory_destroy(&udp);
  }
}
static void test_udp_application(void) {
  memory_udp udp; memory_init(&udp, 27000);
  caps_ctx c = {SENTINEL, &udp, 0, false, false, NULL};
  qa_app_transport_capabilities caps = {&c, {NULL, false, "no AF_IPX", NULL}, caps_bind};
  struct open_scope s = {{{QA_APP_TRANSPORT_IPX_NATIVE, NULL}, QA_APP_FAMILY_Q2, "0.0.0.0", 27910, limits, NULL}, &caps, NULL};
  qa_caught_error caught;
  CHECK(!qa_try(open_body, &s, &caught) && strstr(caught.message, "no AF_IPX"));
  CHECK(c.binds == 0);
  qa_app_open_transport_options options = {{QA_APP_TRANSPORT_UDP, NULL}, QA_APP_FAMILY_Q2, "0.0.0.0", 27910, limits, NULL};
  qa_app_application_transport *transport = qa_app_open_application_transport(&options, &caps);
  qa_app_application_transport_connect_socks(transport, &proxy);
  CHECK(udp.socks == 1);
  CHECK(qa_app_application_transport_udp_socket(transport)->transport.ctx == &udp);
  qa_net_address ipx = qa_app_parse_ipx_remote("00000000:001122334455", 27910);
  struct send_scope ss = {transport, &ipx};
  CHECK(!qa_try(send_body, &ss, &caught) && strstr(caught.message, "explicitly selected"));
  qa_app_application_transport_close(transport);
  CHECK(udp.closed);
  qa_app_application_transport_destroy(transport);
  memory_destroy(&udp);
  /* Cancellation closes a socket allocated while cancellation arrived. */
  memory_udp late; memory_init(&late, 27000);
  bool aborted = false;
  caps_ctx lc = {SENTINEL, &late, 0, false, true, &aborted};
  qa_app_transport_capabilities lcaps = {&lc, {NULL, false, "no AF_IPX", NULL}, caps_bind};
  struct open_scope ls = {{{QA_APP_TRANSPORT_UDP, NULL}, QA_APP_FAMILY_Q1, "0.0.0.0", 26000, limits, &aborted}, &lcaps, NULL};
  CHECK(!qa_try(open_body, &ls, &caught) && strcmp(caught.name, "AbortError") == 0);
  CHECK(late.closed && late.released == 1);
  memory_destroy(&late);
}

/* --- remote-socks.test.ts unit layer: a SOCKS5 proxy with a UDP relay. --- */
static struct proxy_server {
  int tcp, udp, tcp_port, udp_port, peer_port;
  bool stall, stopping, drop; int authenticated, associations, outbound, inbound;
  pthread_t thread; pthread_mutex_t lock;
  struct sockaddr_in client; bool has_client;
} px;
static void *proxy_thread(void *arg) {
  (void)arg;
  int control = -1; uint8_t pending[512]; size_t used = 0; int stage = 0;
  while (!px.stopping) {
    struct pollfd fds[3] = {{px.tcp, POLLIN, 0}, {px.udp, POLLIN, 0}, {control, POLLIN, 0}};
    if (poll(fds, control >= 0 ? 3 : 2, 20) < 0) continue;
    pthread_mutex_lock(&px.lock);
    if (px.drop && control >= 0) { close(control); control = -1; px.drop = false; }
    pthread_mutex_unlock(&px.lock);
    /* The newest control connection replaces an earlier one (a refused client may not have
     * been seen closing yet). */
    if (fds[0].revents & POLLIN) { int fd = accept(px.tcp, NULL, NULL); if (control >= 0) close(control); control = fd; used = 0; stage = 0; }
    if (fds[1].revents & POLLIN) {
      uint8_t bytes[2048]; struct sockaddr_in from; socklen_t sl = sizeof from;
      ssize_t n = recvfrom(px.udp, bytes, sizeof bytes, 0, (struct sockaddr *)&from, &sl);
      if (n > 0 && ntohs(from.sin_port) == px.peer_port) {
        if (px.has_client) {
          uint8_t framed[2100] = {0, 0, 0, 1, 127, 0, 0, 1, (uint8_t)(px.peer_port >> 8), (uint8_t)px.peer_port};
          memcpy(framed + 10, bytes, (size_t)n); px.inbound++;
          sendto(px.udp, framed, (size_t)n + 10, 0, (struct sockaddr *)&px.client, sizeof px.client);
        }
      } else if (n >= 10 && bytes[3] == 1 && ((bytes[8] << 8) | bytes[9]) == px.peer_port) {
        px.client = from; px.has_client = true; px.outbound++;
        struct sockaddr_in peer; memset(&peer, 0, sizeof peer);
        peer.sin_family = AF_INET; peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK); peer.sin_port = htons((uint16_t)px.peer_port);
        sendto(px.udp, bytes + 10, (size_t)n - 10, 0, (struct sockaddr *)&peer, sizeof peer);
      }
    }
    if (control >= 0 && (fds[2].revents & (POLLIN | POLLHUP))) {
      ssize_t n = recv(control, pending + used, sizeof pending - used, 0);
      if (n <= 0) { close(control); control = -1; continue; }
      used += (size_t)n;
      if (px.stall) continue;
      for (;;) {
        if (stage == 0) {
          if (used < 2 || used < (size_t)pending[1] + 2) break;
          bool has_auth = memchr(pending + 2, 2, pending[1]) != NULL;
          if (pending[0] != 5 || !has_auth) { close(control); control = -1; break; }
          size_t length = (size_t)pending[1] + 2; memmove(pending, pending + length, used - length); used -= length;
          send(control, "\x05\x02", 2, MSG_NOSIGNAL); stage++;
        } else if (stage == 1) {
          if (used < 2) break;
          size_t user = pending[1]; if (used < 3 + user) break;
          size_t pass = pending[2 + user]; if (used < 3 + user + pass) break;
          bool valid = pending[0] == 1 && user == 12 && memcmp(pending + 2, "fixture-user", 12) == 0
            && pass == 14 && memcmp(pending + 3 + user, "fixture-secret", 14) == 0;
          uint8_t reply[2] = {1, valid ? 0 : 1}; send(control, reply, 2, MSG_NOSIGNAL);
          size_t length = 3 + user + pass; memmove(pending, pending + length, used - length); used -= length;
          if (!valid) break;
          px.authenticated++; stage++;
        } else if (stage == 2) {
          if (used < 10) break;
          if (pending[0] != 5 || pending[1] != 3 || pending[3] != 1) { close(control); control = -1; break; }
          px.associations++;
          uint8_t reply[10] = {5, 0, 0, 1, 127, 0, 0, 1, (uint8_t)(px.udp_port >> 8), (uint8_t)px.udp_port};
          send(control, reply, 10, MSG_NOSIGNAL);
          memmove(pending, pending + 10, used - 10); used -= 10; stage++;
          break;
        } else break;
      }
    }
  }
  if (control >= 0) close(control);
  return NULL;
}
static int bound_socket(int type, int *port) {
  int fd = socket(AF_INET, type, 0);
  struct sockaddr_in sin; memset(&sin, 0, sizeof sin);
  sin.sin_family = AF_INET; sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(fd, (struct sockaddr *)&sin, sizeof sin);
  socklen_t sl = sizeof sin; getsockname(fd, (struct sockaddr *)&sin, &sl);
  *port = ntohs(sin.sin_port);
  return fd;
}
static void proxy_start(int peer_port, bool stall) {
  memset(&px, 0, sizeof px);
  px.peer_port = peer_port; px.stall = stall;
  pthread_mutex_init(&px.lock, NULL);
  px.tcp = bound_socket(SOCK_STREAM, &px.tcp_port); listen(px.tcp, 4);
  px.udp = bound_socket(SOCK_DGRAM, &px.udp_port);
  pthread_create(&px.thread, NULL, proxy_thread, NULL);
}
static void proxy_stop(void) {
  px.stopping = true; pthread_join(px.thread, NULL);
  close(px.tcp); close(px.udp); pthread_mutex_destroy(&px.lock);
}
struct connect_scope { qa_udp_transport *t; qa_socks_options options; };
static void connect_socks_body(void *ctx) { struct connect_scope *s = ctx; qa_udp_transport_connect_socks(s->t, &s->options); }
struct udp_send_scope { qa_udp_transport *t; const qa_net_address *to; };
static void udp_send_body(void *ctx) { struct udp_send_scope *s = ctx; uint8_t one = 1; (void)qa_udp_transport_send(s->t, s->to, &one, 1); }
static double monotonic_ms(void) { struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now); return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1e6; }
static void test_socks_relay(void) {
  int peer_port; int peer = bound_socket(SOCK_DGRAM, &peer_port);
  int64_t loop[4] = {127, 0, 0, 1};
  qa_net_address destination; qa_net_make_ipv4(&destination, loop, peer_port, false);
  qa_udp_bind_options bind = {"127.0.0.1", 0, NULL, NULL, NULL, false};
  qa_caught_error caught;
  /* A stalled proxy fails the negotiation at the TS 5 s timer; the association closes. */
  proxy_start(peer_port, true);
  qa_udp_transport candidate; qa_udp_transport_bind(&candidate, &bind);
  struct connect_scope cs = {&candidate, {"127.0.0.1", px.tcp_port, "fixture-user", "fixture-secret"}};
  double start = monotonic_ms();
  CHECK(!qa_try(connect_socks_body, &cs, &caught) && strcmp(caught.message, "SOCKS negotiation timed out") == 0);
  CHECK(monotonic_ms() - start < 6000);
  CHECK(candidate.socks.failed && candidate.socks.fd < 0);
  qa_udp_transport_destroy(&candidate);
  proxy_stop();
  /* Wrong credentials are refused by the proxy. */
  proxy_start(peer_port, false);
  qa_udp_transport refused; qa_udp_transport_bind(&refused, &bind);
  struct connect_scope bad = {&refused, {"127.0.0.1", px.tcp_port, "fixture-user", "wrong"}};
  CHECK(!qa_try(connect_socks_body, &bad, &caught) && strcmp(caught.message, "SOCKS authentication failed") == 0);
  qa_udp_transport_destroy(&refused);
  /* Credential text: more than 255 characters, or characters past U+00FF, are refused. */
  char long_name[300]; memset(long_name, 'a', 299); long_name[299] = '\0';
  qa_udp_transport invalid; qa_udp_transport_bind(&invalid, &bind);
  struct connect_scope too_long = {&invalid, {"127.0.0.1", px.tcp_port, long_name, ""}};
  CHECK(!qa_try(connect_socks_body, &too_long, &caught) && strcmp(caught.message, "SOCKS credential exceeds 255 bytes") == 0);
  qa_udp_transport_destroy(&invalid);
  qa_udp_transport_bind(&invalid, &bind);
  struct connect_scope wide = {&invalid, {"127.0.0.1", px.tcp_port, "\xc4\x80", ""}};
  CHECK(!qa_try(connect_socks_body, &wide, &caught) && strcmp(caught.message, "SOCKS credentials require non-NUL source bytes") == 0);
  qa_udp_transport_destroy(&invalid);
  /* An authenticated association relays both directions. */
  qa_udp_transport connected; qa_udp_transport_bind(&connected, &bind);
  struct connect_scope good = {&connected, {"127.0.0.1", px.tcp_port, "fixture-user", "fixture-secret"}};
  bool associated = qa_try(connect_socks_body, &good, &caught);
  CHECK(associated);
  if (!associated) { fprintf(stderr, "  socks: %s\n", caught.message); qa_udp_transport_destroy(&connected); proxy_stop(); close(peer); return; }
  CHECK(px.authenticated == 1 && px.associations == 1);
  uint8_t hello[3] = {'h', 'i', '!'};
  CHECK(qa_udp_transport_send(&connected, &destination, hello, 3));
  uint8_t got[64]; struct sockaddr_in from; socklen_t sl = sizeof from;
  struct pollfd pfd = {peer, POLLIN, 0}; poll(&pfd, 1, 2000);
  ssize_t n = recvfrom(peer, got, sizeof got, MSG_DONTWAIT, (struct sockaddr *)&from, &sl);
  CHECK(n == 3 && memcmp(got, hello, 3) == 0 && ntohs(from.sin_port) == px.udp_port);
  sendto(peer, "ok", 2, 0, (struct sockaddr *)&from, sl);
  qa_receive_event event; memset(&event, 0, sizeof event);
  bool received = false;
  for (double deadline = monotonic_ms() + 2000; !received && monotonic_ms() < deadline;) {
    received = qa_udp_transport_poll(&connected, &event);
    if (!received) { struct timespec pause_for = {0, 1000000}; nanosleep(&pause_for, NULL); }
  }
  CHECK(received && event.kind == QA_RECV_PACKET && qa_net_same_address(&event.from, &destination, true)
    && event.length == 2 && memcmp(event.payload, "ok", 2) == 0);
  if (received) qa_receive_event_destroy(&event);
  /* Proxy loss drops the relay: sends throw instead of falling back to direct UDP. */
  int outbound = px.outbound;
  pthread_mutex_lock(&px.lock); px.drop = true; pthread_mutex_unlock(&px.lock);
  struct timespec settle = {0, 50000000}; nanosleep(&settle, NULL);
  struct udp_send_scope us = {&connected, &destination};
  CHECK(!qa_try(udp_send_body, &us, &caught) && strstr(caught.message, "SOCKS"));
  nanosleep(&settle, NULL);
  CHECK(px.outbound == outbound);
  CHECK(recv(peer, got, sizeof got, MSG_DONTWAIT) < 0); /* nothing arrived directly */
  qa_udp_transport_destroy(&connected);
  proxy_stop();
  close(peer);
}

int main(void) {
  test_ipx_remotes();
  test_dosbox_application();
  test_udp_application();
  test_socks_relay();
  printf("test_app_network_transport: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
