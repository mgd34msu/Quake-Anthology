/* Ports tests/network/common/network.test.ts (transport, loopback, IPX tunnel, reliability,
 * file downloads, filters and rcon), tests/network/common/ipx.test.ts (DOSBox relay with
 * a scripted UDP link), tests/network/hardening/limits.test.ts (gate, rcon guard), and the
 * malformed-input checks for the shared decoders: unified packets and channel, IPX headers,
 * SOCKS relay datagrams, each mutated with a fixed-seed PRNG under
 * qa_try. Temporary files stay under build/agents/network2-svc/tmp and are removed on every
 * outcome. GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <arpa/inet.h>
#include <ftw.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "qa/foundation/support.h"
#include "qa/network/common/diagnostics.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/common/ipx.h"
#include "qa/network/common/ipx_dosbox.h"
#include "qa/network/common/ipx_host.h"
#include "qa/network/common/loopback.h"
#include "qa/network/common/rcon_guard.h"
#include "qa/network/common/reliability.h"
#include "qa/network/common/scheduling.h"
#include "qa/network/common/session.h"
#include "qa/network/common/socks.h"
#include "qa/network/common/transport.h"
#include "qa/network/services/admin.h"
#include "qa/network/services/downloads.h"
#include "qa/network/services/http_downloads.h"
#include "qa/network/unified/channel.h"
#include "qa/network/unified/packet.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static int sentinel;
#define SENTINEL ((void *)&sentinel)

static double monotonic_ms(void) {
  struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1e6;
}
/* receive(): poll until a packet arrives or 2 s pass. */
static bool receive_udp(qa_udp_transport *t, qa_receive_event *out) {
  double deadline = monotonic_ms() + 2000;
  while (monotonic_ms() < deadline) {
    if (qa_udp_transport_poll(t, out)) return true;
    struct timespec pause_for = {0, 1000000}; nanosleep(&pause_for, NULL);
  }
  return false;
}
static void bind_local(qa_udp_transport *t) {
  qa_udp_bind_options options = {"127.0.0.1", 0, NULL, NULL, NULL, false};
  if (!qa_udp_transport_bind(t, &options)) abort();
}
static char *key_of(const qa_net_address *a) { return qa_net_address_key(a, true); }

/* "real Bun UDP exchange copies packets and retains source addresses" */
static void test_udp_exchange(void) {
  qa_udp_transport server, client;
  bind_local(&server); bind_local(&client);
  uint8_t bytes[8] = {255, 255, 255, 255, 112, 105, 110, 103};
  CHECK(qa_udp_transport_send(&client, &server.address, bytes, 8));
  memset(bytes, 0, sizeof bytes);
  qa_receive_event packet;
  CHECK(receive_udp(&server, &packet));
  CHECK(packet.kind == QA_RECV_PACKET);
  char *a = key_of(&packet.from), *b = key_of(&client.address);
  CHECK(strcmp(a, b) == 0); free(a); free(b);
  static const uint8_t expected[8] = {255, 255, 255, 255, 112, 105, 110, 103};
  CHECK(packet.length == 8 && memcmp(packet.payload, expected, 8) == 0);
  CHECK(qa_udp_transport_send(&server, &packet.from, (const uint8_t *)"ack", 3));
  qa_receive_event reply;
  CHECK(receive_udp(&client, &reply));
  CHECK(reply.kind == QA_RECV_PACKET && reply.length == 3 && memcmp(reply.payload, "ack", 3) == 0);
  qa_net_address resolved;
  CHECK(qa_net_resolve_address(&resolved, "localhost", server.address.port, 4));
  a = key_of(&resolved); b = key_of(&server.address);
  CHECK(strcmp(a, b) == 0); free(a); free(b);
  qa_net_address_destroy(&resolved);
  qa_receive_event_destroy(&packet); qa_receive_event_destroy(&reply);
  qa_udp_transport_destroy(&client); qa_udp_transport_destroy(&server);
}

/* A source datagram whose decode fails remotely never aborts: foreign SOCKS relay payloads
 * are skipped by poll (TS `continue`), so an empty queue still reads as empty. */
static void test_udp_socks_skip(void) {
  qa_udp_transport server, relay;
  bind_local(&server); bind_local(&relay);
  /* Pretend the relay was negotiated. */
  server.has_socks = true; server.socks.has_relay = true; server.socks.fd = -1;
  qa_net_address_clone(&relay.address, &server.socks.relay);
  uint8_t junk[3] = {1, 2, 3}, framed[12] = {0, 0, 0, 1, 10, 0, 0, 7, 0x69, 0x87, 42, 43};
  CHECK(qa_udp_transport_send(&relay, &server.address, junk, 3));
  CHECK(qa_udp_transport_send(&relay, &server.address, framed, 12));
  qa_receive_event event; memset(&event, 0, sizeof event);
  bool got = receive_udp(&server, &event);
  CHECK(got && event.kind == QA_RECV_PACKET);
  if (got) {
    CHECK(event.from.kind == QA_NET_IPV4 && event.from.ipv4[0] == 10 && event.from.ipv4[3] == 7 && event.from.port == 27015);
    CHECK(event.length == 2 && event.payload[0] == 42 && event.payload[1] == 43);
    qa_receive_event_destroy(&event);
  }
  CHECK(!qa_udp_transport_poll(&server, &event));
  qa_net_address_destroy(&server.socks.relay); server.socks.has_relay = false;
  qa_udp_transport_destroy(&relay); qa_udp_transport_destroy(&server);
}

/* "several local clients have separate return paths" */
static void test_loopback(void) {
  qa_loopback_hub hub; qa_loopback_hub_init(&hub, NULL, NULL, NULL);
  qa_loopback_transport *server = qa_loopback_hub_bind(&hub, "server"), *first = qa_loopback_hub_bind(&hub, "seat-0"),
    *second = qa_loopback_hub_bind(&hub, "seat-1");
  uint8_t one = 1, two = 2, nine = 9;
  qa_loopback_transport_send(first, &server->address, &one, 1);
  qa_loopback_transport_send(second, &server->address, &two, 1);
  qa_receive_event a, b, c;
  CHECK(qa_loopback_transport_poll(server, &a) && qa_loopback_transport_poll(server, &b));
  CHECK(a.kind == QA_RECV_PACKET && b.kind == QA_RECV_PACKET);
  qa_loopback_transport_send(server, &b.from, &nine, 1);
  CHECK(!qa_loopback_transport_poll(first, &c));
  CHECK(qa_loopback_transport_poll(second, &c) && c.kind == QA_RECV_PACKET && c.length == 1 && c.payload[0] == 9);
  qa_receive_event_destroy(&a); qa_receive_event_destroy(&b); qa_receive_event_destroy(&c);
  qa_loopback_hub_close(&hub);
  CHECK(qa_loopback_transport_closed(first)); /* closed endpoints stay valid objects */
  qa_loopback_hub_destroy(&hub);
}

/* "IPX tunnel exchanges Quake payloads on real UDP links" */
static bool receive_ipx(qa_ipx_udp_transport *t, qa_receive_event *out) {
  double deadline = monotonic_ms() + 2000;
  while (monotonic_ms() < deadline) {
    if (qa_ipx_udp_transport_poll(t, out)) return true;
    struct timespec pause_for = {0, 1000000}; nanosleep(&pause_for, NULL);
  }
  return false;
}
static void test_ipx_tunnel(void) {
  qa_udp_transport first_socket, second_socket;
  bind_local(&first_socket); bind_local(&second_socket);
  qa_net_address first_udp, second_udp;
  qa_net_address_clone(&first_socket.address, &first_udp); qa_net_address_clone(&second_socket.address, &second_udp);
  int64_t n1[6] = {0, 0, 0, 0, 0, 1}, n2[6] = {0, 0, 0, 0, 0, 2}, nb[6] = {255, 255, 255, 255, 255, 255};
  qa_net_address a1, a2, broadcast;
  qa_net_make_ipx(&a1, 0, n1, 26000); qa_net_make_ipx(&a2, 0, n2, 26000); qa_net_make_ipx(&broadcast, 0, nb, 26000);
  qa_ipx_udp_transport *first = calloc(1, sizeof *first), *second = calloc(1, sizeof *second);
  qa_ipx_profile quake1 = {QA_IPX_PROFILE_QUAKE1, 0};
  qa_ipx_udp_transport_init(first, &a1, &first_socket, quake1, NULL, NULL);
  qa_ipx_udp_transport_init(second, &a2, &second_socket, quake1, NULL, NULL);
  qa_ipx_udp_transport_add_peer(first, &a2, &second_udp);
  qa_ipx_udp_transport_add_peer(second, &a1, &first_udp);
  uint8_t payload[3] = {1, 2, 3}, eight = 8;
  CHECK(qa_ipx_udp_transport_send(first, &a2, payload, 3));
  qa_receive_event packet;
  CHECK(receive_ipx(second, &packet));
  CHECK(packet.kind == QA_RECV_PACKET && qa_net_same_address(&packet.from, &a1, true) && packet.length == 3 && memcmp(packet.payload, payload, 3) == 0);
  qa_receive_event_destroy(&packet);
  CHECK(qa_ipx_udp_transport_send(second, &broadcast, &eight, 1));
  CHECK(receive_ipx(first, &packet));
  CHECK(packet.kind == QA_RECV_PACKET && qa_net_same_address(&packet.from, &a2, true) && packet.length == 1 && packet.payload[0] == 8);
  qa_receive_event_destroy(&packet);
  qa_ipx_udp_transport_destroy(first); qa_ipx_udp_transport_destroy(second);
  free(first); free(second);
  qa_net_address_destroy(&first_udp); qa_net_address_destroy(&second_udp);
}

/* "reliable source protocols and exact-multiple fragmentation" */
static void test_reliability(void) {
  qa_toggle_channel sender, receiver;
  qa_toggle_channel_init(&sender, 1400, 1); qa_toggle_channel_init(&receiver, 1400, 1);
  uint8_t reliable[2] = {11, 12}, unreliable = 13;
  qa_toggle_channel_queue(&sender, reliable, 2);
  qa_toggle_packet first; qa_toggle_channel_transmit(&sender, &unreliable, 1, -1, &first);
  qa_toggle_receive_result r = qa_toggle_channel_receive(&receiver, &first);
  CHECK(r.accepted && r.length == 3 && r.payload[0] == 11 && r.payload[1] == 12 && r.payload[2] == 13);
  free(r.payload);
  r = qa_toggle_channel_receive(&receiver, &first);
  CHECK(!r.accepted && strcmp(r.reason, "sequence") == 0);
  qa_toggle_packet back; qa_toggle_channel_transmit(&receiver, NULL, 0, -1, &back);
  r = qa_toggle_channel_receive(&sender, &back); free(r.payload);
  CHECK(!qa_toggle_channel_has_pending_reliable(&sender));
  free(first.payload); free(back.payload);
  qa_toggle_channel_destroy(&sender); qa_toggle_channel_destroy(&receiver);

  qa_stop_and_wait nq; qa_stop_and_wait_init(&nq, 100, 4, 1000);
  uint8_t five[5] = {1, 2, 3, 4, 5};
  qa_stop_and_wait_begin(&nq, five, 5);
  qa_reliable_fragment fragment, again;
  CHECK(qa_stop_and_wait_next(&nq, 0, &fragment) && fragment.sequence == 0 && !fragment.final);
  CHECK(!qa_stop_and_wait_next(&nq, 1000, &again));
  CHECK(qa_stop_and_wait_next(&nq, 1001, &again) && again.sequence == 0 && again.length == fragment.length
    && memcmp(again.payload, fragment.payload, again.length) == 0);
  free(fragment.payload); free(again.payload);
  CHECK(qa_stop_and_wait_acknowledge(&nq, 0));
  CHECK(qa_stop_and_wait_next(&nq, 1002, &fragment) && fragment.sequence == 1 && fragment.final && fragment.length == 1 && fragment.payload[0] == 5);
  free(fragment.payload);
  qa_stop_and_wait_destroy(&nq);

  qa_packet_rate rate; qa_packet_rate_init(&rate, 2500, 200);
  qa_packet_rate_sent(&rate, 1000, 0, false);
  CHECK(!qa_packet_rate_can_send(&rate, 319, false)); CHECK(qa_packet_rate_can_send(&rate, 321, false));
}

/* Build-local temporary tree, removed on every outcome. */
static char root[160];
static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
  (void)st; (void)flag; (void)ftw; return remove(path);
}
static void make_root(void) {
  mkdir("build", 0755); mkdir("build/agents", 0755); mkdir("build/agents/network2-svc", 0755);
  mkdir("build/agents/network2-svc/tmp", 0755);
  snprintf(root, sizeof root, "build/agents/network2-svc/tmp/qa-netc-XXXXXX");
  if (!mkdtemp(root)) abort();
}
static void remove_root(void) { if (root[0]) nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS); root[0] = '\0'; }

/* One-shot HTTP server thread serving `body` with status 200. */
static struct { int listener, port; const uint8_t *body; size_t length; int status; pthread_t thread; } http;
static void *http_serve(void *arg) {
  (void)arg;
  int fd = accept(http.listener, NULL, NULL);
  if (fd < 0) return NULL;
  char request[4096]; size_t used = 0;
  while (used + 1 < sizeof request) {
    ssize_t n = recv(fd, request + used, sizeof request - 1 - used, 0);
    if (n <= 0) break;
    used += (size_t)n; request[used] = '\0';
    if (strstr(request, "\r\n\r\n")) break;
  }
  char header[256];
  int n = http.status == 302
    ? snprintf(header, sizeof header, "HTTP/1.1 302 Found\r\nLocation: /elsewhere\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
    : snprintf(header, sizeof header, "HTTP/1.1 %d X\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", http.status, http.length);
  send(fd, header, (size_t)n, MSG_NOSIGNAL);
  if (http.status != 302) send(fd, http.body, http.length, MSG_NOSIGNAL);
  close(fd);
  return NULL;
}
static void http_start(const uint8_t *body, size_t length, int status) {
  http.body = body; http.length = length; http.status = status;
  http.listener = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in sin; memset(&sin, 0, sizeof sin);
  sin.sin_family = AF_INET; sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(http.listener, (struct sockaddr *)&sin, sizeof sin);
  socklen_t sl = sizeof sin; getsockname(http.listener, (struct sockaddr *)&sin, &sl);
  http.port = ntohs(sin.sin_port);
  listen(http.listener, 4);
  pthread_create(&http.thread, NULL, http_serve, NULL);
}
static void http_stop(void) { pthread_join(http.thread, NULL); close(http.listener); }

struct sink_scope { qa_download_sink *sink; const char *name; qa_download_expectation expected; char *digest; const char *url; };
static void create_sink_body(void *ctx) {
  struct sink_scope *scope = ctx;
  qa_download_sink_create(scope->sink, root, scope->name, scope->expected);
}
/* "file downloads verify bytes before installation" */
static void test_downloads(void) {
  make_root();
  const char *text = "authored download fixture";
  size_t length = strlen(text);
  char hex[65]; qa_net_sha256_hex((const uint8_t *)text, length, hex);
  char digest[80]; snprintf(digest, sizeof digest, "sha256:%s", hex);
  char *expected_digest = qa_create_content_digest(hex);
  CHECK(expected_digest && strcmp(expected_digest, digest) == 0);
  char path[256]; snprintf(path, sizeof path, "%s/source.dat", root);
  FILE *f = fopen(path, "wb"); fwrite(text, 1, length, f); fclose(f);
  qa_download_file source; CHECK(qa_download_file_open(&source, root, "source.dat"));
  qa_download_expectation expectation = {false, expected_digest, (double)length, 0};
  qa_download_sink sink; CHECK(qa_download_sink_create(&sink, root, "mods/udp.dat", expectation));
  for (double offset = 0; offset < source.byte_length; offset += 8) {
    size_t got_bytes = 0;
    uint8_t *chunk = qa_download_file_read(&source, offset, 8, &got_bytes);
    qa_download_sink_append(&sink, chunk, got_bytes);
    free(chunk);
  }
  char *finished = qa_download_sink_finish(&sink);
  CHECK(finished && strcmp(finished, expected_digest) == 0); free(finished);
  snprintf(path, sizeof path, "%s/mods/udp.dat", root);
  char back[64] = {0}; f = fopen(path, "rb"); size_t got = f ? fread(back, 1, sizeof back, f) : 0; if (f) fclose(f);
  CHECK(got == length && memcmp(back, text, length) == 0);

  qa_caught_error caught;
  char url[96];

  /* "shared metadata reads enforce bounded optional filelists and HTTP response status" */
  const char *list = "maps/test.bsp\n";
  double limits_for[3] = {100, 4, 100};
  int statuses[3] = {200, 200, 404};
  for (int i = 0; i < 4; i++) {
    http_start((const uint8_t *)list, strlen(list), i < 3 ? statuses[i] : 200);
    snprintf(url, sizeof url, "http://127.0.0.1:%d/list", http.port);
    bool aborted = i == 3;
    size_t got_length = 99;
    uint8_t *meta = qa_fetch_http_download_metadata(url, i < 3 ? limits_for[i] : 100, &aborted, &got_length);
    if (i == 0) { CHECK(meta && got_length == strlen(list) && memcmp(meta, list, got_length) == 0); }
    else CHECK(meta == NULL);
    free(meta);
    if (i == 3) { /* the aborted request never reached the server: connect once to release it */
      int fd = socket(AF_INET, SOCK_STREAM, 0); struct sockaddr_in sin; memset(&sin, 0, sizeof sin);
      sin.sin_family = AF_INET; sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK); sin.sin_port = htons((uint16_t)http.port);
      connect(fd, (struct sockaddr *)&sin, sizeof sin); send(fd, "\r\n\r\n", 4, MSG_NOSIGNAL); close(fd);
    }
    http_stop();
  }

  /* Escaping the root through a symlink is refused. */
  char link_path[256]; snprintf(link_path, sizeof link_path, "%s/outside", root);
  CHECK(symlink("/tmp", link_path) == 0);
  qa_download_sink outside; memset(&outside, 0, sizeof outside);
  struct sink_scope create = {&outside, "outside/nope.dat", expectation, NULL, NULL};
  /* TS throws a catchable Error; no staging is created. */
  CHECK(!qa_try(create_sink_body, &create, &caught) && strcmp(caught.name, "Error") == 0);

  /* A sequential sink hashes what it appended: a wrong digest refuses publication and
   * removes the staged file (finish closes in finally). */
  qa_download_expectation wrong = {false, "sha256:0000000000000000000000000000000000000000000000000000000000000000", (double)length, 0};
  qa_download_sink bad; CHECK(qa_download_sink_create(&bad, root, "mods/bad.dat", wrong));
  qa_download_sink_append(&bad, (const uint8_t *)text, length);
  const char *error = NULL;
  CHECK(qa_download_sink_try_finish(&bad, &error) == NULL);
  CHECK(error && strcmp(error, "Download checksum differs from content identity") == 0 && bad.ended);
  qa_download_sink_destroy(&bad);
  snprintf(path, sizeof path, "%s/mods/bad.dat", root);
  CHECK(access(path, F_OK) != 0);

  qa_download_sink_destroy(&sink);
  qa_download_file_close(&source);
  free(expected_digest);
  remove_root();
}

#include <dirent.h>
static int list_root(char names[][256], int max) {
  DIR *d = opendir(root); int n = 0;
  if (!d) return -1;
  for (struct dirent *e; (e = readdir(d));) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    if (n < max) snprintf(names[n], 256, "%s", e->d_name);
    n++;
  }
  closedir(d); return n;
}
static qa_download_expectation bounded_expectation(double maximum) { qa_download_expectation e = {true, NULL, 0, maximum}; return e; }
struct ranged_scope { qa_download_sink *sink; const char *name; qa_download_expectation expected; double total; const qa_download_span *spans; size_t count; };
static void ranged_body(void *ctx) {
  struct ranged_scope *s = ctx;
  qa_download_sink_create_ranged(s->sink, root, s->name, s->expected, s->total, s->spans, s->count);
}
struct append_scope { qa_download_sink *sink; double start; const uint8_t *bytes; size_t length; bool sequential; };
static void append_body(void *ctx) {
  struct append_scope *s = ctx;
  if (s->sequential) qa_download_sink_append(s->sink, s->bytes, s->length);
  else qa_download_sink_append_range(s->sink, s->start, s->bytes, s->length);
}
static const char *append_error(qa_download_sink *sink, double start, const uint8_t *bytes, size_t length, bool sequential) {
  static qa_caught_error caught;
  struct append_scope scope = {sink, start, bytes, length, sequential};
  return qa_try(append_body, &scope, &caught) ? NULL : caught.message;
}
struct staged_ctx { void *sentinel; const uint8_t *expected; size_t length; qa_download_sink *sink; bool raise; bool ran; };
static void staged_inspect(void *ctx, const char *path) {
  struct staged_ctx *c = ctx; CHECK(c->sentinel == SENTINEL);
  c->ran = true;
  if (c->raise) qa_raise("Error", 0, 0, "Must not inspect incomplete ranges");
  FILE *f = fopen(path, "rb"); uint8_t *back = malloc(c->length + 1); size_t got = f ? fread(back, 1, c->length + 1, f) : 0; if (f) fclose(f);
  CHECK(got == c->length && (c->length == 0 || memcmp(back, c->expected, c->length) == 0));
  free(back);
  if (c->sink) { const char *e = append_error(c->sink, 0, NULL, 0, false); CHECK(e && strstr(e, "inspection")); }
}
struct inspect_scope { qa_download_sink *sink; struct staged_ctx *ctx; };
static void inspect_body(void *ctx) { struct inspect_scope *s = ctx; qa_download_sink_inspect_staged(s->sink, staged_inspect, s->ctx); }
struct finish_scope { qa_download_sink *sink; char *digest; };
static void finish_body(void *ctx) { struct finish_scope *s = ctx; s->digest = qa_download_sink_finish(s->sink); }
static void test_download_ranges(void) {
  make_root();
  size_t total = 200003;
  uint8_t *bytes = malloc(total);
  for (size_t i = 0; i < total; i++) bytes[i] = (uint8_t)(i * 37 % 251);
  char hex[65]; qa_net_sha256_hex(bytes, total, hex);
  char *digest = qa_create_content_digest(hex);
  qa_download_expectation identity = {false, digest, (double)total, 0};
  qa_download_span spans[3] = {{120000, (double)total - 1}, {0, 49999}, {50000, 119999}};
  qa_download_sink sink; CHECK(qa_download_sink_create_ranged(&sink, root, "archive.pk3", identity, (double)total, spans, 3));
  qa_download_sink_append_range(&sink, 120000, bytes + 120000, total - 120000);
  qa_download_sink_append_range(&sink, 50000, bytes + 50000, 20000);
  CHECK(qa_download_sink_byte_length(&sink) == 100003);
  struct staged_ctx refuse = {SENTINEL, NULL, 0, NULL, true, false};
  struct inspect_scope inspect = {&sink, &refuse};
  qa_caught_error caught;
  CHECK(!qa_try(inspect_body, &inspect, &caught) && strstr(caught.message, "incomplete") && !refuse.ran);
  qa_download_sink_append_range(&sink, 0, bytes, 49999);
  qa_download_sink_append_range(&sink, 50000, bytes + 70000, 50000);
  qa_download_sink_append_range(&sink, 0, bytes + 49999, 1);
  CHECK(qa_download_sink_byte_length(&sink) == (double)total);
  char names[8][256];
  CHECK(list_root(names, 8) == 1);
  struct staged_ctx check = {SENTINEL, bytes, total, &sink, false, false};
  inspect.ctx = &check;
  CHECK(qa_try(inspect_body, &inspect, &caught) && check.ran);
  struct finish_scope finish = {&sink, NULL};
  CHECK(qa_try(finish_body, &finish, &caught) && finish.digest && strcmp(finish.digest, digest) == 0);
  free(finish.digest);
  CHECK(list_root(names, 8) == 1 && strcmp(names[0], "archive.pk3") == 0);
  qa_download_sink_destroy(&sink);
  /* An inspection that raises closes the sink and removes staging. */
  qa_download_span whole = {0, 0};
  qa_download_sink raising; CHECK(qa_download_sink_create_ranged(&raising, root, "raising", bounded_expectation(1024 * 1024), 1, &whole, 1));
  uint8_t one = 1;
  qa_download_sink_append_range(&raising, 0, &one, 1);
  inspect.sink = &raising; refuse.ran = false; inspect.ctx = &refuse;
  CHECK(!qa_try(inspect_body, &inspect, &caught) && refuse.ran && raising.ended && !raising.inspecting);
  qa_download_sink_destroy(&raising);
  CHECK(list_root(names, 8) == 1);
  free(bytes); free(digest);
  remove_root();

  make_root();
  static const qa_download_span p1[1] = {{1, 3}}, p2[2] = {{0, 1}, {3, 3}}, p3[2] = {{0, 2}, {2, 3}}, p4[2] = {{0, 3}, {0, 3}},
    p5[1] = {{0, 4}}, p6[1] = {{0, -1}}, p7[1] = {{0, 9007199254740991.0}}, p8[2] = {{0, 1.5}, {2.5, 3}};
  const qa_download_span *partitions[] = {NULL, p1, p2, p3, p4, p5, p6, p7, p8};
  size_t counts[] = {0, 1, 2, 2, 2, 1, 1, 1, 2};
  qa_download_sink bad;
  for (size_t i = 0; i < 9; i++) {
    struct ranged_scope r = {&bad, "bad", bounded_expectation(1024 * 1024), 4, partitions[i], counts[i]};
    CHECK(!qa_try(ranged_body, &r, &caught));
  }
  double totals[] = {-1, 0.5, 9007199254740992.0, NAN, INFINITY};
  for (size_t i = 0; i < 5; i++) {
    struct ranged_scope r = {&bad, "bad", bounded_expectation(1024 * 1024), totals[i], NULL, 0};
    CHECK(!qa_try(ranged_body, &r, &caught));
  }
  static const qa_download_span four[1] = {{0, 3}};
  struct ranged_scope r = {&bad, "bad", bounded_expectation(3), 4, four, 1};
  CHECK(!qa_try(ranged_body, &r, &caught));
  r.expected = bounded_expectation(NAN); r.total = 0; r.spans = NULL; r.count = 0;
  CHECK(!qa_try(ranged_body, &r, &caught));
  qa_download_expectation five = {false, "sha256:0000000000000000000000000000000000000000000000000000000000000000", 5, 0};
  r.expected = five; r.total = 4; r.spans = four; r.count = 1;
  CHECK(!qa_try(ranged_body, &r, &caught));
  CHECK(list_root(names, 8) == 0);

  static const qa_download_span halves[2] = {{0, 1}, {2, 3}};
  qa_download_sink ranged, ordinary;
  CHECK(qa_download_sink_create_ranged(&ranged, root, "ranged", bounded_expectation(1024 * 1024), 4, halves, 2));
  CHECK(qa_download_sink_create(&ordinary, root, "ordinary", bounded_expectation(1024 * 1024)));
  uint8_t data[3] = {1, 2, 3};
  const char *e = append_error(&ranged, 0, data, 1, true); CHECK(e && strstr(e, "appendRange"));
  e = append_error(&ordinary, 0, data, 1, false); CHECK(e && strstr(e, "append"));
  e = append_error(&ranged, 1, data, 1, false); CHECK(e && strstr(e, "Unknown"));
  e = append_error(&ranged, 0, data, 3, false); CHECK(e && strstr(e, "span size"));
  CHECK(qa_download_sink_byte_length(&ranged) == 0);
  qa_download_sink_append_range(&ranged, 0, data, 2);
  e = append_error(&ranged, 0, data + 2, 1, false); CHECK(e && strstr(e, "span size"));
  CHECK(qa_download_sink_byte_length(&ranged) == 2);
  finish.sink = &ranged;
  CHECK(!qa_try(finish_body, &finish, &caught) && strstr(caught.message, "incomplete") && ranged.ended);
  qa_download_sink_close(&ordinary);
  CHECK(list_root(names, 8) == 0);
  qa_download_sink_destroy(&ranged); qa_download_sink_destroy(&ordinary);
  remove_root();

  make_root();
  char path[256]; snprintf(path, sizeof path, "%s/installed", root);
  FILE *f = fopen(path, "wb"); fputs("keep", f); fclose(f);
  static const qa_download_span pair[1] = {{0, 1}};
  qa_download_sink conflict; CHECK(qa_download_sink_create_ranged(&conflict, root, "installed", bounded_expectation(1024 * 1024), 2, pair, 1));
  qa_download_sink_append_range(&conflict, 0, data, 2);
  finish.sink = &conflict;
  CHECK(!qa_try(finish_body, &finish, &caught));
  char keep[8] = {0}; f = fopen(path, "rb"); size_t kept = fread(keep, 1, 7, f); fclose(f);
  CHECK(kept == 4 && strcmp(keep, "keep") == 0);
  qa_download_sink cancelled; CHECK(qa_download_sink_create_ranged(&cancelled, root, "cancelled", bounded_expectation(1024 * 1024), 4, halves, 2));
  qa_download_sink_append_range(&cancelled, 2, data + 2, 1);
  qa_download_sink_close(&cancelled); qa_download_sink_close(&cancelled);
  e = append_error(&cancelled, 0, data, 1, false); CHECK(e && strstr(e, "closed"));
  static const qa_download_span single[1] = {{0, 0}};
  qa_download_expectation zero = {false, "sha256:0000000000000000000000000000000000000000000000000000000000000000", 1, 0};
  qa_download_sink mismatch; CHECK(qa_download_sink_create_ranged(&mismatch, root, "mismatch", zero, 1, single, 1));
  qa_download_sink_append_range(&mismatch, 0, data, 1);
  finish.sink = &mismatch;
  CHECK(!qa_try(finish_body, &finish, &caught) && strstr(caught.message, "checksum"));
  CHECK(list_root(names, 8) == 1 && strcmp(names[0], "installed") == 0);
  qa_download_sink_destroy(&conflict); qa_download_sink_destroy(&cancelled); qa_download_sink_destroy(&mismatch);
  remove_root();

  make_root();
  qa_download_sink empty; CHECK(qa_download_sink_create_ranged(&empty, root, "empty", bounded_expectation(1024 * 1024), 0, NULL, 0));
  struct staged_ctx nothing = {SENTINEL, NULL, 0, NULL, false, false};
  inspect.sink = &empty; inspect.ctx = &nothing;
  CHECK(qa_try(inspect_body, &inspect, &caught) && nothing.ran);
  finish.sink = &empty;
  CHECK(qa_try(finish_body, &finish, &caught) && strcmp(finish.digest, "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
  free(finish.digest);
  snprintf(path, sizeof path, "%s/empty", root);
  struct stat st; CHECK(stat(path, &st) == 0 && st.st_size == 0);
  qa_download_sink_destroy(&empty);
  remove_root();
}

/* "source address filters and private rcon replies" */
struct rcon_host { void *sentinel; char replies[4][128]; int reply_count; char executed[4][64]; int executed_count; bool raise_in_execute; };
static const char *rcon_password(void *ctx) { struct rcon_host *h = ctx; CHECK(h->sentinel == SENTINEL); return "secret"; }
static void rcon_execute(void *ctx, const char *command, qa_rcon_output_fn output, void *output_ctx) {
  struct rcon_host *h = ctx; CHECK(h->sentinel == SENTINEL);
  snprintf(h->executed[h->executed_count++ & 3], 64, "%s", command);
  output(output_ctx, "map: start\n", 11);
  if (h->raise_in_execute) qa_raise("Error", 0, 0, "execute failed");
}
static void rcon_reply(void *ctx, const qa_net_address *to, const char *text) {
  struct rcon_host *h = ctx; CHECK(h->sentinel == SENTINEL);
  char *key = qa_net_address_key(to, true);
  snprintf(h->replies[h->reply_count++ & 3], 128, "%s %s", key, text); free(key);
}
struct handle_scope { qa_rcon_service *svc; const qa_net_address *from; const char *result; };
static void handle_body(void *ctx) {
  struct handle_scope *scope = ctx;
  scope->result = qa_rcon_service_handle(scope->svc, scope->from, "secret", "status", 5000);
}
static void test_filters_and_rcon(void) {
  qa_ip_filter_list filters; qa_ip_filter_list_init(&filters, true);
  qa_ipv4_filter filter; CHECK(qa_source_ipv4_filter("192.168.0", &filter));
  qa_ip_filter_list_add(&filters, filter);
  qa_net_address inside, outside, address;
  qa_net_make_ip(&inside, "192.168.1.4", 27960, false); qa_net_make_ip(&outside, "10.0.0.1", 27960, false);
  CHECK(qa_ip_filter_list_rejects(&filters, &inside)); CHECK(!qa_ip_filter_list_rejects(&filters, &outside));
  qa_ip_filter_list_destroy(&filters);
  /* parseInt(...) & 255 keeps the low byte of long digit runs. */
  CHECK(qa_source_ipv4_filter("300.99999999999999999999.1", &filter));
  CHECK((filter.compare & 255) == 44 && ((filter.compare >> 8) & 255) == 0 && ((filter.compare >> 16) & 255) == 1);

  struct rcon_host host; memset(&host, 0, sizeof host); host.sentinel = SENTINEL;
  qa_rcon_host rh = {&host, rcon_password, rcon_execute, rcon_reply};
  qa_rcon_service admin; qa_rcon_service_init(&admin, rh, true, 1008);
  qa_net_make_ip(&address, "127.0.0.1", 3000, false);
  CHECK(strcmp(qa_rcon_service_handle(&admin, &address, "bad", "status", 500), "denied") == 0);
  CHECK(strcmp(qa_rcon_service_handle(&admin, &address, "secret", "status", 1000), "executed") == 0);
  CHECK(host.executed_count == 1 && strcmp(host.executed[0], "status") == 0);
  CHECK(host.reply_count == 2 && strcmp(host.replies[1], "127.0.0.1:3000 map: start\n") == 0);
  /* Another source keeps its own 500 ms slot. */
  CHECK(strcmp(qa_rcon_service_handle(&admin, &outside, "secret", "status", 1001), "executed") == 0);
  CHECK(strcmp(qa_rcon_service_handle(&admin, &address, "secret", "status", 1200), "throttled") == 0);
  /* A raise from execute flushes buffered output, then propagates. */
  host.raise_in_execute = true; host.reply_count = 0;
  struct handle_scope scope = {&admin, &address, NULL};
  qa_caught_error caught;
  CHECK(!qa_try(handle_body, &scope, &caught) && strcmp(caught.message, "execute failed") == 0);
  CHECK(host.reply_count == 1 && strcmp(host.replies[0], "127.0.0.1:3000 map: start\n") == 0);
  qa_rcon_service_destroy(&admin);
  /* Short lines slice to empty instead of reading past the text. */
  char *command = qa_q3_rcon_command("rc");
  CHECK(command && command[0] == '\0'); free(command);
  command = qa_q3_rcon_command("rcon secret  map q3dm1");
  CHECK(strcmp(command, "map q3dm1") == 0); free(command);
  qa_net_address_destroy(&inside); qa_net_address_destroy(&outside); qa_net_address_destroy(&address);
}

/* tests/network/hardening/limits.test.ts: gate and rcon guard. */
static void test_limits(void) {
  qa_connectionless_gate gate; qa_connectionless_gate_init(&gate);
  int allowed = 0;
  for (int index = 0; index < 100; index++) {
    int64_t host[4] = {203, 0, 113, 9};
    qa_net_address remote; qa_net_make_ipv4(&remote, host, 1000 + index, false);
    if (qa_connectionless_gate_allow(&gate, &remote, 0)) allowed++;
  }
  CHECK(allowed == 10);
  int64_t host[4] = {203, 0, 113, 9};
  qa_net_address remote, lan, loop; qa_net_make_ipv4(&remote, host, 5, false);
  CHECK(qa_connectionless_gate_allow(&gate, &remote, 10000));
  qa_net_make_loopback(&loop, "a");
  bool always = true;
  for (int index = 0; index < 1000; index++) if (!qa_connectionless_gate_allow(&gate, &loop, 0)) always = false;
  CHECK(always);
  qa_connectionless_gate_destroy(&gate);
  /* The per-source table is bounded: 8192 spoofed sources, then refusal until idle. */
  qa_flood_limiter limiter; qa_flood_limiter_init_max(&limiter, 2, 100, 4);
  for (int index = 0; index < 4; index++) {
    int64_t h[4] = {10, 0, 0, index};
    qa_net_address a; qa_net_make_ipv4(&a, h, 1, false);
    CHECK(qa_flood_limiter_allow(&limiter, &a, 0));
  }
  int64_t fifth[4] = {10, 0, 0, 9};
  qa_net_address f5; qa_net_make_ipv4(&f5, fifth, 1, false);
  CHECK(!qa_flood_limiter_allow(&limiter, &f5, 100)); CHECK(qa_peer_table_count(&limiter.peers) == 4);
  CHECK(qa_flood_limiter_allow(&limiter, &f5, 201)); CHECK(qa_peer_table_count(&limiter.peers) == 1);
  qa_flood_limiter_destroy(&limiter);

  qa_rcon_guard guard; qa_rcon_guard_init(&guard, 3, 1024);
  int64_t lan_host[4] = {192, 168, 1, 5};
  qa_net_make_ipv4(&lan, lan_host, 27910, false);
  remote.port = 4000;
  for (int index = 0; index < 3; index++) { CHECK(qa_rcon_guard_permitted(&guard, &remote, 0)); qa_rcon_guard_failed(&guard, &remote, 0); }
  CHECK(!qa_rcon_guard_permitted(&guard, &remote, 500));
  CHECK(qa_rcon_guard_permitted(&guard, &remote, 1500));
  CHECK(qa_rcon_guard_permitted(&guard, &lan, 0));
  qa_rcon_guard_destroy(&guard);
  CHECK(qa_password_matches("secret", "secret")); CHECK(!qa_password_matches("secret", "secreT"));
  CHECK(!qa_password_matches("", ""));
  qa_net_address_destroy(&loop);
}

/* printablePeerText counts UTF-16 units: a four-byte character takes two, and a cut through
 * it leaves a lone surrogate (U+FFFD once encoded). */
static void test_peer_text(void) {
  char *t = qa_printable_peer_text("a\x1b[2Jb\n", 256);
  CHECK(strcmp(t, "a?[2Jb\n") == 0); free(t);
  char many[1001]; memset(many, 'x', 1000); many[1000] = '\0';
  t = qa_printable_peer_text(many, 256); CHECK(strlen(t) == 256); free(t);
  t = qa_printable_peer_text("\xc3\xa9\xc3\xa9\xc3\xa9", 2); CHECK(strcmp(t, "\xc3\xa9\xc3\xa9") == 0); free(t);
  t = qa_printable_peer_text("ab\xf0\x9f\x98\x80", 3); CHECK(strcmp(t, "ab\xef\xbf\xbd") == 0); free(t);
  t = qa_printable_peer_text("ab\xf0\x9f\x98\x80z", 4); CHECK(strcmp(t, "ab\xf0\x9f\x98\x80") == 0); free(t);
}

/* Endpoint parsing: WHATWG IPv6 canonical text, Node isIP scope rules, integer records. */
struct ip_scope { const char *host; char *out; };
static void make_ip_body(void *ctx) {
  struct ip_scope *scope = ctx;
  qa_net_address a; qa_net_make_ip(&a, scope->host, 1, false);
  scope->out = qa_net_address_host(&a); qa_net_address_destroy(&a);
}
static void test_endpoints(void) {
  static const struct { const char *in, *out; } cases[] = {
    {"fe80::1%eth0", "fe80::1%eth0"}, {"::ffff:1.2.3.4", "::ffff:102:304"}, {"::1.2.3.4", "::102:304"},
    {"1:0:0:2:0:0:0:3", "1:0:0:2::3"}, {"1:0:0:0:2:0:0:0", "1::2:0:0:0"}, {"::", "::"},
    {"0:0:0:0:0:0:0:1", "::1"}, {"ABCD::EF", "abcd::ef"}, {"1::2:3:4:5:6:7", "1:0:2:3:4:5:6:7"},
    {"fe80::1%", NULL}, {"fe80::1%e_t", NULL}, {"fe80::1%a%b", NULL}, {"1.2.3.4%x", NULL}, {"01.2.3.4", NULL}, {"1.2.3.04", NULL},
  };
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
    struct ip_scope scope = {cases[i].in, NULL};
    qa_caught_error caught;
    bool ok = qa_try(make_ip_body, &scope, &caught);
    if (cases[i].out) { CHECK(ok && scope.out && strcmp(scope.out, cases[i].out) == 0); }
    else { CHECK(!ok && strncmp(caught.message, "Expected an IP literal: ", 24) == 0); }
    if (ok && !cases[i].out) fprintf(stderr, "  accepted %s\n", cases[i].in);
    free(scope.out);
  }
}

/* --- DOSBox relay (ipx.test.ts) over a scripted UDP link. --- */
typedef struct relay_link {
  void *sentinel;
  qa_packet_queue queue; qa_net_address address;
  struct { qa_net_address to; uint8_t bytes[1500]; size_t length; } sent[16]; int sent_count;
  bool closed, acknowledge;
  int injections; /* queued registration traffic released on first poll */
  bool abort_on_poll; bool *aborted;
} relay_link;
static qa_net_address relay;
static const uint8_t ack_bytes[30] = {255, 255, 0, 30, 0, 0, 0, 0, 0, 0, 127, 0, 0, 1, 160, 1, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 213, 0, 2};
static int link_address(void *ctx, qa_net_address *out) { relay_link *l = ctx; CHECK(l->sentinel == SENTINEL); return qa_net_address_clone(&l->address, out); }
static bool link_closed(void *ctx) { return ((relay_link *)ctx)->closed; }
static int64_t link_max(void *ctx) { (void)ctx; return -1; }
static bool link_send(void *ctx, const qa_net_address *to, const uint8_t *payload, size_t length) {
  relay_link *l = ctx; CHECK(l->sentinel == SENTINEL);
  int i = l->sent_count++ % 16;
  qa_net_address_destroy(&l->sent[i].to);
  qa_net_address_clone(to, &l->sent[i].to); memcpy(l->sent[i].bytes, payload, length); l->sent[i].length = length;
  if (l->acknowledge && l->sent_count == 1) qa_packet_queue_accept(&l->queue, &relay, ack_bytes, 30, false);
  return true;
}
static void link_inject(relay_link *l, const qa_net_address *from, const qa_net_address *to, const uint8_t *payload, size_t length,
                        const qa_net_address *sender) {
  qa_ipx_packet p; memset(&p, 0, sizeof p);
  p.from = *from; p.to = *to; p.payload = (uint8_t *)payload; p.length = length; p.packet_type = 4;
  size_t n; uint8_t *wire = qa_encode_ipx_packet(&p, &n);
  qa_packet_queue_accept(&l->queue, sender ? sender : &relay, wire, n, false); free(wire);
}
static qa_net_address node, peer;
static int link_poll(void *ctx, qa_receive_event *out) {
  relay_link *l = ctx;
  if (l->abort_on_poll) *l->aborted = true;
  if (l->injections == 1) {
    /* "registration ignores another relay and a game packet" */
    l->injections = 0;
    int64_t other_relay_host[4] = {127, 0, 0, 2};
    qa_net_address other_relay; qa_net_make_ipv4(&other_relay, other_relay_host, 213, false);
    int64_t ack_node[6] = {0, 0, 0, 0, 0, 213};
    qa_net_address ack_from; qa_net_make_ipx(&ack_from, 1, ack_node, 2);
    qa_ipx_packet p; memset(&p, 0, sizeof p); p.from = ack_from; p.to = node;
    size_t n; uint8_t *wire = qa_encode_ipx_packet(&p, &n);
    qa_packet_queue_accept(&l->queue, &other_relay, wire, n, false);
    qa_net_address game_to; int64_t peer_node[6]; for (int i = 0; i < 6; i++) peer_node[i] = peer.node[i];
    qa_net_make_ipx(&game_to, 0, peer_node, 26000);
    link_inject(l, &peer, &game_to, NULL, 0, NULL);
    qa_packet_queue_accept(&l->queue, &relay, wire, n, false);
    free(wire);
  }
  return qa_packet_queue_poll(&l->queue, out);
}
static long long link_subscribe(void *ctx, qa_net_listener_fn fn, void *c) { return qa_packet_queue_subscribe(&((relay_link *)ctx)->queue, fn, c); }
static void link_unsubscribe(void *ctx, long long id) { qa_packet_queue_unsubscribe(&((relay_link *)ctx)->queue, id); }
static void link_close(void *ctx) { relay_link *l = ctx; if (l->closed) return; l->closed = true; qa_packet_queue_close(&l->queue); }
static qa_datagram_transport link_transport(relay_link *l) {
  qa_datagram_transport v = {l, link_address, link_closed, link_max, link_send, link_poll, link_subscribe, link_unsubscribe, link_close};
  return v;
}
static double fixed_now(void *ctx) { CHECK(ctx == SENTINEL); return 100; }
static void link_init(relay_link *l) {
  memset(l, 0, sizeof *l); l->sentinel = SENTINEL; l->acknowledge = true;
  qa_datagram_limits limits = {65507, 256};
  qa_packet_queue_init(&l->queue, limits, fixed_now, SENTINEL);
  int64_t host[4] = {127, 0, 0, 1};
  qa_net_make_ipv4(&l->address, host, 40961, false);
}
static void link_destroy(relay_link *l) {
  qa_packet_queue_destroy(&l->queue); qa_net_address_destroy(&l->address);
  for (int i = 0; i < 16; i++) qa_net_address_destroy(&l->sent[i].to);
}
struct connect_scope { qa_dosbox_ipx_network *n; relay_link *l; qa_dosbox_connect_options *options; };
static void connect_body(void *ctx) {
  struct connect_scope *scope = ctx;
  qa_dosbox_ipx_network_connect(scope->n, link_transport(scope->l), &relay, scope->options);
}
static void connect_ok(qa_dosbox_ipx_network *n, relay_link *l) {
  qa_dosbox_connect_options options; memset(&options, 0, sizeof options);
  options.now = fixed_now; options.now_ctx = SENTINEL;
  struct connect_scope scope = {n, l, &options};
  qa_caught_error caught;
  if (!qa_try(connect_body, &scope, &caught)) { fprintf(stderr, "connect: %s\n", caught.message); abort(); }
}
struct bind_scope { qa_dosbox_ipx_network *n; double port; };
static void bind_body(void *ctx) { struct bind_scope *s = ctx; qa_dosbox_ipx_network_bind(s->n, s->port, 0); }
static void test_dosbox_registration(void) {
  relay_link l; link_init(&l);
  qa_dosbox_ipx_network network; connect_ok(&network, &l);
  static const uint8_t registration[30] = {255, 255, 0, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
  CHECK(l.sent[0].length == 30 && memcmp(l.sent[0].bytes, registration, 30) == 0);
  CHECK(qa_net_same_address(&network.address, &node, true));
  qa_dosbox_ipx_socket *first = qa_dosbox_ipx_network_bind(&network, 0, 4), *second = qa_dosbox_ipx_network_bind(&network, 0, 0);
  CHECK(first->address.port == 0x4002 && second->address.port == 0x4003);
  CHECK(memcmp(first->address.node, second->address.node, 6) == 0);
  struct bind_scope scope = {&network, (double)first->address.port};
  qa_caught_error caught;
  CHECK(!qa_try(bind_body, &scope, &caught) && strstr(caught.message, "already bound"));
  qa_dosbox_ipx_socket_close(first);
  CHECK(qa_dosbox_ipx_network_bind(&network, 0, 0)->address.port == 0x4002);
  CHECK(!l.closed);
  qa_dosbox_ipx_network_close(&network);
  CHECK(l.closed);
  CHECK(qa_dosbox_ipx_socket_closed(second)); /* still a valid closed object */
  qa_dosbox_ipx_network_destroy(&network);
  link_destroy(&l);
}
static const qa_receive_event *poll_game(qa_ipx_game_transport *t, qa_receive_event *out) {
  return qa_ipx_game_transport_poll(t, out) ? out : NULL;
}
static void test_dosbox_games(void) {
  relay_link l; link_init(&l);
  qa_dosbox_ipx_network network; connect_ok(&network, &l);
  qa_ipx_host host = {true, {0}, &network};
  qa_ipx_game_transport q1, q2, q3;
  qa_bind_ipx_transport(&host, "quake1", 26000, &q1);
  qa_bind_ipx_transport(&host, "quake2", 27910, &q2);
  qa_bind_ipx_transport(&host, "quake3", 27960, &q3);
  uint8_t seven = 7, eight = 8;
  qa_ipx_game_transport_send(&q1, &peer, &seven, 1); qa_ipx_game_transport_send(&q1, &peer, &eight, 1);
  qa_ipx_packet decoded;
  CHECK(qa_decode_ipx_packet(l.sent[1].bytes, l.sent[1].length, &decoded));
  qa_net_address q1_address; q1.socket.get_address(q1.socket.ctx, &q1_address);
  CHECK(qa_net_same_address(&l.sent[1].to, &relay, true));
  static const uint8_t first_payload[5] = {0, 0, 0, 0, 7}, second_payload[5] = {1, 0, 0, 0, 8};
  CHECK(qa_net_same_address(&decoded.from, &q1_address, true) && qa_net_same_address(&decoded.to, &peer, true)
    && decoded.packet_type == 4 && decoded.length == 5 && memcmp(decoded.payload, first_payload, 5) == 0);
  qa_ipx_packet_destroy(&decoded);
  CHECK(qa_decode_ipx_packet(l.sent[2].bytes, l.sent[2].length, &decoded) && decoded.length == 5 && memcmp(decoded.payload, second_payload, 5) == 0);
  qa_ipx_packet_destroy(&decoded);
  static const uint8_t inbound[5] = {6, 0, 0, 0, 91};
  link_inject(&l, &peer, &q1_address, inbound, 5, NULL);
  qa_receive_event event;
  CHECK(poll_game(&q1, &event) && event.kind == QA_RECV_PACKET && qa_net_same_address(&event.from, &peer, true)
    && event.length == 1 && event.payload[0] == 91);
  qa_receive_event_destroy(&event);
  CHECK(!poll_game(&q2, &event));
  qa_ipx_game_transport *raw[2] = {&q2, &q3};
  for (int i = 0; i < 2; i++) {
    uint8_t data[2] = {9, 10}, back[2] = {11, 12};
    qa_ipx_game_transport_send(raw[i], &peer, data, 2);
    int last = (l.sent_count - 1) % 16;
    CHECK(qa_decode_ipx_packet(l.sent[last].bytes, l.sent[last].length, &decoded) && decoded.packet_type == 0
      && decoded.length == 2 && memcmp(decoded.payload, data, 2) == 0);
    qa_ipx_packet_destroy(&decoded);
    qa_net_address address; raw[i]->socket.get_address(raw[i]->socket.ctx, &address);
    link_inject(&l, &peer, &address, back, 2, NULL);
    CHECK(poll_game(raw[i], &event) && event.length == 2 && memcmp(event.payload, back, 2) == 0);
    qa_receive_event_destroy(&event); qa_net_address_destroy(&address);
  }
  uint8_t one = 1;
  link_inject(&l, &peer, &q1_address, &one, 1, NULL);
  CHECK(!poll_game(&q1, &event));
  uint8_t *big = calloc(1391, 1);
  CHECK(!qa_ipx_game_transport_send(&q1, &peer, big, 1391));
  CHECK(poll_game(&q1, &event) && event.kind == QA_RECV_ERROR && strstr(event.error, "capacity"));
  qa_receive_event_destroy(&event);
  CHECK(qa_ipx_game_transport_send(&q1, &peer, big, 1390));
  free(big);
  qa_dosbox_ipx_network_close(&network);
  CHECK(q1.socket.is_closed(q1.socket.ctx) && q2.socket.is_closed(q2.socket.ctx) && q3.socket.is_closed(q3.socket.ctx));
  qa_net_address_destroy(&q1_address);
  qa_dosbox_ipx_network_destroy(&network);
  link_destroy(&l);
}
static void test_dosbox_filters(void) {
  relay_link l; link_init(&l);
  qa_dosbox_ipx_network network; connect_ok(&network, &l);
  qa_dosbox_ipx_socket *socket = qa_dosbox_ipx_network_bind(&network, 26000, 4);
  int64_t foreign_host[4] = {127, 0, 0, 2};
  qa_net_address foreign; qa_net_make_ipv4(&foreign, foreign_host, 213, false);
  uint8_t one = 1, two = 2, three = 3, four = 4, five = 5;
  link_inject(&l, &peer, &socket->address, &one, 1, &foreign);
  int64_t peer_node[6]; for (int i = 0; i < 6; i++) peer_node[i] = peer.node[i];
  qa_net_address to_peer; qa_net_make_ipx(&to_peer, 0, peer_node, 26000);
  link_inject(&l, &peer, &to_peer, &two, 1, NULL);
  uint8_t short_bytes[2] = {255, 255};
  qa_packet_queue_accept(&l.queue, &relay, short_bytes, 2, false);
  qa_receive_event event;
  CHECK(!qa_dosbox_ipx_socket_poll(socket, &event));
  int64_t all[6] = {255, 255, 255, 255, 255, 255};
  qa_net_address broadcast; qa_net_make_ipx(&broadcast, 0, all, 26000);
  qa_dosbox_ipx_socket_send(socket, &broadcast, &three, 1);
  CHECK(qa_dosbox_ipx_socket_poll(socket, &event) && qa_net_same_address(&event.from, &socket->address, true) && event.payload[0] == 3);
  qa_receive_event_destroy(&event);
  link_inject(&l, &peer, &broadcast, &four, 1, NULL);
  CHECK(qa_dosbox_ipx_socket_poll(socket, &event) && qa_net_same_address(&event.from, &peer, true) && event.payload[0] == 4);
  qa_receive_event_destroy(&event);
  qa_net_address ping_from, ping_to; qa_net_make_ipx(&ping_from, 0, peer_node, 2); qa_net_make_ipx(&ping_to, 0, all, 2);
  link_inject(&l, &ping_from, &ping_to, NULL, 0, NULL);
  qa_dosbox_ipx_network_receive(&network);
  int last = (l.sent_count - 1) % 16;
  qa_ipx_packet decoded;
  CHECK(qa_decode_ipx_packet(l.sent[last].bytes, l.sent[last].length, &decoded) && qa_net_same_address(&decoded.from, &node, true)
    && qa_net_same_address(&decoded.to, &ping_from, true) && decoded.packet_type == 0 && decoded.length == 0);
  qa_ipx_packet_destroy(&decoded);
  CHECK(!qa_dosbox_ipx_socket_poll(socket, &event));
  int count = l.sent_count;
  qa_dosbox_ipx_socket_send(socket, &socket->address, &five, 1);
  CHECK(l.sent_count == count);
  CHECK(qa_dosbox_ipx_socket_poll(socket, &event) && event.payload[0] == 5);
  qa_receive_event_destroy(&event);
  qa_dosbox_ipx_network_destroy(&network);
  qa_net_address_destroy(&foreign);
  link_destroy(&l);
}
static void test_dosbox_failures(void) {
  qa_caught_error caught;
  relay_link timed; link_init(&timed); timed.acknowledge = false;
  qa_dosbox_ipx_network network;
  qa_dosbox_connect_options options; memset(&options, 0, sizeof options);
  options.has_timeout = true; options.timeout_ms = 2;
  struct connect_scope scope = {&network, &timed, &options};
  CHECK(!qa_try(connect_body, &scope, &caught) && strstr(caught.message, "timed out"));
  CHECK(timed.closed);
  link_destroy(&timed);
  relay_link cancelled; link_init(&cancelled); cancelled.acknowledge = false;
  bool aborted = false; cancelled.abort_on_poll = true; cancelled.aborted = &aborted;
  memset(&options, 0, sizeof options); options.aborted = &aborted;
  scope.l = &cancelled;
  CHECK(!qa_try(connect_body, &scope, &caught) && strstr(caught.message, "aborted"));
  CHECK(cancelled.closed);
  link_destroy(&cancelled);
  /* A transport error event rejects registration with that error. */
  relay_link erroring; link_init(&erroring); erroring.acknowledge = false;
  qa_packet_queue_push_error(&erroring.queue, "ECONNREFUSED");
  memset(&options, 0, sizeof options);
  scope.l = &erroring;
  CHECK(!qa_try(connect_body, &scope, &caught) && strcmp(caught.message, "ECONNREFUSED") == 0 && erroring.closed);
  link_destroy(&erroring);
  relay_link udp; link_init(&udp);
  connect_ok(&network, &udp);
  qa_dosbox_ipx_socket *socket = qa_dosbox_ipx_network_bind(&network, 26000, 4);
  link_close(&udp);
  qa_dosbox_ipx_network_receive(&network);
  CHECK(qa_dosbox_ipx_network_closed(&network) && qa_dosbox_ipx_socket_closed(socket));
  qa_dosbox_ipx_network_destroy(&network);
  link_destroy(&udp);
  /* Registration ignores another relay and a game packet. */
  relay_link ignoring; link_init(&ignoring); ignoring.acknowledge = false; ignoring.injections = 1;
  connect_ok(&network, &ignoring);
  CHECK(qa_net_same_address(&network.address, &node, true));
  qa_dosbox_ipx_network_destroy(&network);
  link_destroy(&ignoring);
}
/* "native IPX is an explicit host capability and never silently becomes UDP" */
struct raw_ipx { void *sentinel; qa_packet_queue queue; bool closed; uint8_t sent[16]; size_t sent_length; };
static int raw_address(void *ctx, qa_net_address *out) { (void)ctx; return qa_net_address_clone(&peer, out); }
static bool raw_closed(void *ctx) { return ((struct raw_ipx *)ctx)->closed; }
static int64_t raw_max(void *ctx) { (void)ctx; return -1; }
static bool raw_send(void *ctx, const qa_net_address *to, const uint8_t *p, size_t n) { (void)to; struct raw_ipx *r = ctx; memcpy(r->sent, p, n); r->sent_length = n; return true; }
static int raw_poll(void *ctx, qa_receive_event *out) { return qa_packet_queue_poll(&((struct raw_ipx *)ctx)->queue, out); }
static long long raw_sub(void *ctx, qa_net_listener_fn fn, void *c) { return qa_packet_queue_subscribe(&((struct raw_ipx *)ctx)->queue, fn, c); }
static void raw_unsub(void *ctx, long long id) { qa_packet_queue_unsubscribe(&((struct raw_ipx *)ctx)->queue, id); }
static void raw_close(void *ctx) { struct raw_ipx *r = ctx; r->closed = true; qa_packet_queue_close(&r->queue); }
static int raw_bind(void *ctx, double port, double packet_type, qa_datagram_transport *out) {
  struct raw_ipx *r = ctx; CHECK(r->sentinel == SENTINEL);
  CHECK(port == 26000 && packet_type == 4);
  qa_datagram_transport v = {r, raw_address, raw_closed, raw_max, raw_send, raw_poll, raw_sub, raw_unsub, raw_close};
  *out = v; return 1;
}
struct native_scope { qa_ipx_host host; qa_ipx_game_transport out; };
static void native_bind_body(void *ctx) { struct native_scope *s = ctx; qa_bind_ipx_transport(&s->host, "quake3", 27960, &s->out); }
static void test_native_ipx(void) {
  struct native_scope scope; memset(&scope, 0, sizeof scope);
  scope.host.capability.available = false; scope.host.capability.reason = "AF_IPX is not provided by this Bun host";
  qa_caught_error caught;
  CHECK(!qa_try(native_bind_body, &scope, &caught) && strncmp(caught.message, "ipx-native: AF_IPX", 18) == 0);
  CHECK(strcmp(caught.name, "UnsupportedTransportError") == 0);
  struct raw_ipx raw; memset(&raw, 0, sizeof raw); raw.sentinel = SENTINEL;
  qa_datagram_limits limits = {4096, 4};
  qa_packet_queue_init(&raw.queue, limits, fixed_now, SENTINEL);
  qa_ipx_host host = {false, {&raw, true, NULL, raw_bind}, NULL};
  qa_ipx_game_transport transport;
  qa_bind_ipx_transport(&host, "quake1", 26000, &transport);
  uint8_t ninety_nine = 99;
  qa_ipx_game_transport_send(&transport, &peer, &ninety_nine, 1);
  static const uint8_t expected[5] = {0, 0, 0, 0, 99};
  CHECK(raw.sent_length == 5 && memcmp(raw.sent, expected, 5) == 0);
  static const uint8_t inbound[5] = {12, 0, 0, 0, 100};
  qa_packet_queue_accept(&raw.queue, &peer, inbound, 5, false);
  qa_receive_event event;
  CHECK(qa_ipx_game_transport_poll(&transport, &event) && event.length == 1 && event.payload[0] == 100);
  qa_receive_event_destroy(&event);
  transport.socket.close(transport.socket.ctx);
  CHECK(raw.closed);
  qa_packet_queue_destroy(&raw.queue);
}

/* --- Malformed remote input: fixed-seed mutation under qa_try. --- */
static uint32_t prng = 0x9e3779b9u;
static uint32_t next_random(void) { prng ^= prng << 13; prng ^= prng >> 17; prng ^= prng << 5; return prng; }
static void mutate(uint8_t *bytes, size_t *length, size_t capacity) {
  switch (next_random() % 4) {
    case 0: if (*length) *length = next_random() % *length; break;
    case 1: for (int i = 0; i < 4 && *length; i++) bytes[next_random() % *length] = (uint8_t)next_random(); break;
    case 2: if (*length) bytes[next_random() % *length] ^= (uint8_t)(1u << (next_random() % 8)); break;
    default: { size_t extra = next_random() % 16; while (extra-- && *length < capacity) bytes[(*length)++] = (uint8_t)next_random(); }
  }
}
struct channel_scope { qa_unified_channel *c; const uint8_t *bytes; size_t length; double now; };
static void channel_receive_body(void *ctx) {
  struct channel_scope *s = ctx; size_t count = 0;
  qa_unified_delivery *d = qa_unified_channel_receive(s->c, s->bytes, s->length, s->now, &count);
  qa_unified_deliveries_destroy(d, count);
}
static void test_unified_mutation(void) {
  const char *token = "0123456789abcdef0123456789abcdef";
  qa_unified_channel_limits limits = qa_unified_channel_default_limits();
  limits.datagram_bytes = 80; limits.message_bytes = 4096; limits.queued_reliable_bytes = 8192; limits.retry_milliseconds = 10;
  qa_unified_channel a, b;
  qa_unified_channel_init(&a, token, &limits); qa_unified_channel_init(&b, token, &limits);
  uint8_t payload[300]; for (int i = 0; i < 300; i++) payload[i] = (uint8_t)i;
  qa_unified_channel_queue_reliable(&a, payload, 200); qa_unified_channel_queue_frame(&a, payload, 100, 1);
  size_t *lengths = NULL, count = 0;
  uint8_t **packets = qa_unified_channel_flush(&a, 0, &lengths, &count);
  CHECK(count > 2);
  int raised = 0, decoded = 0;
  for (int round = 0; round < 20000; round++) {
    size_t pick = next_random() % count;
    uint8_t bytes[128]; size_t length = lengths[pick] < sizeof bytes ? lengths[pick] : sizeof bytes;
    memcpy(bytes, packets[pick], length);
    mutate(bytes, &length, sizeof bytes);
    qa_unified_packet packet;
    if (qa_decode_unified_packet(bytes, length, &packet)) {
      decoded++;
      CHECK(packet.kind == QA_UNIFIED_ACK || packet.length <= (size_t)packet.fragment_bytes);
    }
    struct channel_scope scope = {&b, bytes, length, 1};
    qa_caught_error caught;
    if (!qa_try(channel_receive_body, &scope, &caught)) raised++;
  }
  CHECK(raised == 0); /* malformed or foreign datagrams are dropped, never thrown */
  CHECK(decoded > 0);
  CHECK(!b.ended);
  /* inboundMessageBytes: a peer may not announce more than the receiver accepts. */
  qa_unified_channel_limits narrow = limits; narrow.inbound_message_bytes = 100;
  qa_unified_channel c; qa_unified_channel_init(&c, token, &narrow);
  CHECK(c.limits.inbound_message_bytes == 100);
  size_t got = 0;
  qa_unified_delivery *none = NULL;
  for (size_t i = 0; i < count; i++) { none = qa_unified_channel_receive(&c, packets[i], lengths[i], 1, &got); qa_unified_deliveries_destroy(none, got); CHECK(got == 0); }
  CHECK(c.reliable_received == 0);
  qa_unified_channel_destroy(&c);
  qa_unified_channel_limits defaults = qa_unified_channel_default_limits();
  qa_unified_channel d; qa_unified_channel_init(&d, token, &defaults);
  CHECK(d.limits.inbound_message_bytes == 4 * 1024 * 1024);
  qa_unified_channel_destroy(&d);
  for (size_t i = 0; i < count; i++) free(packets[i]);
  free(packets); free(lengths);
  qa_unified_channel_destroy(&a); qa_unified_channel_destroy(&b);
}
static void test_ipx_socks_mutation(void) {
  int64_t n1[6] = {1, 2, 3, 4, 5, 6};
  qa_net_address from, to; qa_net_make_ipx(&from, 7, n1, 26000); qa_net_make_ipx(&to, 0xffffffff, n1, 1);
  qa_ipx_packet p; memset(&p, 0, sizeof p); p.from = from; p.to = to; p.packet_type = 4;
  uint8_t payload[20] = "quake ipx payload";
  p.payload = payload; p.length = sizeof payload;
  size_t n; uint8_t *wire = qa_encode_ipx_packet(&p, &n);
  int valid = 0;
  for (int round = 0; round < 20000; round++) {
    uint8_t bytes[96]; size_t length = n; memcpy(bytes, wire, n);
    mutate(bytes, &length, sizeof bytes);
    qa_ipx_packet out;
    if (qa_decode_ipx_packet(bytes, length, &out)) { valid++; CHECK(out.length + 30 == length); qa_ipx_packet_destroy(&out); }
    qa_net_address source; uint8_t *data = NULL; size_t data_length = 0;
    if (qa_socks_read_datagram(bytes, length, &source, &data, &data_length)) { CHECK(data_length + 10 == length); free(data); }
  }
  CHECK(valid > 0);
  free(wire);
}
int main(void) {
  int64_t node_bytes[6] = {127, 0, 0, 1, 160, 1}, peer_bytes[6] = {127, 0, 0, 1, 160, 2}, relay_host[4] = {127, 0, 0, 1};
  qa_net_make_ipx(&node, 0, node_bytes, 2); qa_net_make_ipx(&peer, 0, peer_bytes, 26000);
  qa_net_make_ipv4(&relay, relay_host, 213, false);
  test_udp_exchange();
  test_udp_socks_skip();
  test_loopback();
  test_ipx_tunnel();
  test_reliability();
  test_downloads();
  test_download_ranges();
  test_filters_and_rcon();
  test_limits();
  test_endpoints();
  test_peer_text();
  test_dosbox_registration();
  test_dosbox_games();
  test_dosbox_filters();
  test_dosbox_failures();
  test_native_ipx();
  test_unified_mutation();
  test_ipx_socks_mutation();
  remove_root();
  printf("test_network_common: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
