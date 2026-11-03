/*
 * StartupServerBrowser (src/app/bootstrap/server-browser.ts) and its collaborators,
 * ported from ../quake-anthology-ts/tests/first-playable/server-browser-masters.test.ts,
 * server-browser-persistence.test.ts and server-browser-sort-filter.test.ts with the
 * same captured packets, over real UDP sockets on 127.0.0.1. The TS menu-driven cases
 * drive the browser through the same calls the StartupMenu makes (choose, address,
 * select, favorite, connection, favorites-only, sort and filter setters); the menu's
 * click geometry belongs to the startup-menu tests. TS awaits that interleave with a
 * pending write or DNS lookup cannot interleave in the synchronous C port; those steps
 * are reproduced at the points the C call reaches them (an assertCurrent that retires
 * on its second call is the TS "flip during await").
 *
 * Also: the browser.test of startup-selection.test.ts against the C Q1 (NetQuake
 * control answerer), Q2 (Q2ConnectionlessServer) and Q3 (Q3 server network) servers on
 * loopback, the Q3BrowserView over the browser's Q3BrowserHost adapter with sentinel
 * contexts, and a mutation pass that sends truncated and corrupted copies of every
 * packet kind the browser parses to its socket: no remote datagram may abort.
 * GPL-2.0-or-later.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <ftw.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "qa/app/bootstrap/network/q3.h"
#include "qa/app/bootstrap/server_browser.h"
#include "qa/app/bootstrap/server_browser_addresses.h"
#include "qa/app/bootstrap/server_browser_cache.h"
#include "qa/app/bootstrap/server_master_list.h"
#include "qa/foundation/support.h"
#include "qa/network/common/transport.h"
#include "qa/network/q1/discovery.h"
#include "qa/network/q1/handshake.h"
#include "qa/network/q2/connectionless.h"
#include "qa/network/q2/handshake.h"
#include "qa/network/q2/kex/discovery.h"
#include "qa/network/q3/browser_view.h"
#include "qa/network/q3/connectionless.h"
#include "qa/network/q3/discovery.h"
#include "qa/network/q3/state/product.h"
#include "qa/settings/config.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define MAGIC 0x5e57b0a1u
typedef struct sentinel { unsigned magic; int calls; int fail_on; char message[64]; } sentinel;
static sentinel *fix(void *ctx) {
  CHECK(ctx != NULL && ((sentinel *)ctx)->magic == MAGIC);
  return ctx;
}

/* ---------------- fixtures ---------------- */
static void sleep_ms(int ms) {
  struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}
static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
  (void)st; (void)flag; (void)ftw;
  return remove(path);
}
static void remove_tree(const char *path) { (void)nftw(path, remove_entry, 16, FTW_DEPTH | FTW_PHYS); }
static char *temp_root(const char *prefix) {
  const char *base = getenv("TMPDIR");
  char *path = malloc(512);
  snprintf(path, 512, "%s/%sXXXXXX", base != NULL && base[0] ? base : "/tmp", prefix);
  CHECK(mkdtemp(path) != NULL);
  return path;
}
static qa_config_store config_at(const char *root) {
  qa_config_store store;
  CHECK(qa_config_store_init(&store, root));
  return store;
}
static char *load_text(const qa_config_store *config, const char *name) {
  size_t length = 0;
  return qa_config_store_load_text(config, name, &length);
}
static qa_net_address ipv4(int a, int b, int c, int d, int64_t port) {
  qa_net_address out;
  int64_t host[4] = {a, b, c, d};
  CHECK(qa_net_make_ipv4(&out, host, port, true));
  return out;
}
static bool udp_bind_local(qa_udp_transport *t) {
  qa_udp_bind_options options = {"127.0.0.1", 0, NULL, NULL, NULL, false};
  return qa_udp_transport_bind(t, &options) != 0;
}
static int64_t udp_port(const qa_udp_transport *t) { return t->address.port; }
/* Waits up to `attempts` ms for one packet. */
static bool peer_receive(qa_udp_transport *peer, qa_receive_event *out, int attempts) {
  for (int i = 0; i < attempts; i++) {
    memset(out, 0, sizeof *out);
    if (qa_udp_transport_poll(peer, out)) {
      if (out->kind == QA_RECV_PACKET) return true;
      qa_receive_event_destroy(out);
      continue;
    }
    sleep_ms(1);
  }
  return false;
}
static qa_net_address browser_endpoint(qa_app_startup_server_browser *b) {
  qa_net_address bound;
  CHECK(qa_app_startup_server_browser_transport_address(b, &bound));
  qa_net_address out = ipv4(127, 0, 0, 1, bound.port);
  qa_net_address_destroy(&bound);
  return out;
}
static char *address_text(const qa_net_address *a) { return qa_app_browser_address(a); }
static char *key_of(const qa_net_address *a) { return qa_net_address_key(a, true); }
static bool lines_contain(char **lines, size_t count, const char *text) {
  for (size_t i = 0; i < count; i++) if (strcmp(lines[i], text) == 0) return true;
  return false;
}
static void lines_free(char **lines, size_t count) {
  for (size_t i = 0; i < count; i++) free(lines[i]);
  free(lines);
}
static size_t row_count(qa_app_startup_server_browser *b) {
  size_t count = 0;
  (void)qa_app_startup_server_browser_rows(b, &count);
  return count;
}
static const qa_browser_entry *row_at(qa_app_startup_server_browser *b, size_t index) {
  size_t count = 0;
  const qa_browser_entry **rows = qa_app_startup_server_browser_rows(b, &count);
  return index < count ? rows[index] : NULL;
}
static bool sources_are(const qa_browser_entry *entry, const char *const *expected, size_t count) {
  if (entry == NULL || entry->source_count != count) return false;
  for (size_t i = 0; i < count; i++) if (strcmp(entry->sources[i], expected[i]) != 0) return false;
  return true;
}
static bool same_list(qa_app_q3_list list, const qa_net_address *expected, size_t count) {
  if (list.address_count != count) return false;
  for (size_t i = 0; i < count; i++) if (!qa_net_same_address(&list.addresses[i], &expected[i], true)) return false;
  return true;
}
static uint8_t *master_packet(const qa_net_address *addresses, size_t count, bool complete, size_t *out_length) {
  size_t n = 4 + 18 + count * 7 + 1 + (complete ? 3 : 0);
  uint8_t *bytes = malloc(n), *p = bytes;
  memset(p, 255, 4); p += 4;
  memcpy(p, "getserversResponse", 18); p += 18;
  for (size_t i = 0; i < count; i++) {
    *p++ = 92;
    memcpy(p, addresses[i].ipv4, 4); p += 4;
    *p++ = (uint8_t)(addresses[i].port >> 8);
    *p++ = (uint8_t)(addresses[i].port & 255);
  }
  *p++ = 92;
  if (complete) { *p++ = 69; *p++ = 79; *p++ = 84; }
  *out_length = n;
  return bytes;
}
/* decodeConnectionless(payload, "server").arguments[0] */
static char *q3_challenge_of(const uint8_t *payload, size_t length) {
  qa_q3_connectionless_packet packet;
  if (!qa_q3_decode_connectionless(payload, length, QA_Q3_CONNLESS_SERVER, NULL, &packet)) return NULL;
  size_t n = packet.argument_count ? strlen(packet.arguments[0]) : 0;
  char *challenge = malloc(n + 1);
  memcpy(challenge, packet.argument_count ? packet.arguments[0] : "", n + 1);
  qa_q3_connectionless_packet_destroy(&packet);
  return challenge;
}
static char *q3_line_of(const uint8_t *payload, size_t length) {
  qa_q3_connectionless_packet packet;
  if (!qa_q3_decode_connectionless(payload, length, QA_Q3_CONNLESS_SERVER, NULL, &packet)) return NULL;
  size_t n = strlen(packet.line);
  char *line = malloc(n + 1);
  memcpy(line, packet.line, n + 1);
  qa_q3_connectionless_packet_destroy(&packet);
  return line;
}
static uint8_t *q3_status_packet(const char *info, const qa_q3_status_player *players, size_t count, size_t *length) {
  uint8_t *bytes = NULL;
  CHECK(qa_q3_encode_status(info, players, count, &bytes, length));
  return bytes;
}

/* ---------------- raising calls ---------------- */
enum op_kind {
  OP_QUERY, OP_FAVORITE, OP_CONNECTION, OP_DISCOVER, OP_LOAD_CACHE, OP_SAVE_CACHE, OP_MASTER, OP_RESOLVE_Q3, OP_ADD_Q3,
  OP_WRITE_CACHE, OP_READ_CACHE, OP_OPEN, OP_POLL, OP_Q3_LIST, OP_SCAN
};
typedef struct op {
  enum op_kind kind;
  qa_app_startup_server_browser *b;
  /* arguments */
  double source; const char *remote; double protocol; const char *const *keywords; size_t keyword_count;
  void (*assert_current)(void *ctx); void *assert_ctx;
  const qa_net_address *address;
  const qa_browser_entry *entries; size_t entry_count; const qa_q3_browser_cache_view *view;
  const char *text; const qa_config_store *config;
  /* results */
  qa_app_browser_connection connection; qa_q3_browser_cache_view *loaded; bool loaded_view;
  qa_net_address resolved; bool has_resolved;
  char *written; qa_app_q3_browser_cache cache; qa_app_startup_server_browser *opened;
} op;
static void op_body(void *ctx) {
  op *o = ctx;
  switch (o->kind) {
    case OP_QUERY: qa_app_startup_server_browser_query(o->b); break;
    case OP_FAVORITE: qa_app_startup_server_browser_favorite(o->b); break;
    case OP_CONNECTION: o->connection = qa_app_startup_server_browser_connection(o->b); break;
    case OP_DISCOVER: qa_app_startup_server_browser_discover(o->b); break;
    case OP_LOAD_CACHE: o->loaded_view = qa_app_startup_server_browser_load_q3_cache(o->b, o->assert_current, o->assert_ctx, &o->loaded); break;
    case OP_SAVE_CACHE: qa_app_startup_server_browser_save_q3_cache(o->b, o->view); break;
    case OP_MASTER:
      qa_app_startup_server_browser_request_q3_master(o->b, o->source, o->remote, o->protocol, o->keywords, o->keyword_count,
                                                      o->assert_current, o->assert_ctx);
      break;
    case OP_RESOLVE_Q3: o->has_resolved = qa_app_startup_server_browser_resolve_q3(o->b, o->remote, &o->resolved); break;
    case OP_ADD_Q3: qa_app_startup_server_browser_add_q3(o->b, o->source, o->address); break;
    case OP_WRITE_CACHE: o->written = qa_app_write_q3_browser_cache(o->entries, o->entry_count, o->view); break;
    case OP_READ_CACHE: o->cache = qa_app_read_q3_browser_cache(o->text); break;
    case OP_OPEN: o->opened = qa_app_startup_server_browser_open(o->config); break;
    case OP_POLL: qa_app_startup_server_browser_poll(o->b); break;
    case OP_Q3_LIST: (void)qa_app_startup_server_browser_q3_list(o->b, o->source); break;
    case OP_SCAN: qa_app_startup_server_browser_scan(o->b); break;
  }
}
/* Runs the op; true when it completed. A raise is reported unless `expect` names a substring it must contain. */
static bool run(op *o, const char *expect) {
  qa_caught_error caught;
  bool ok = qa_try(op_body, o, &caught);
  if (expect == NULL && !ok) { failures++; fprintf(stderr, "FAIL op %d raised %s: %s\n", (int)o->kind, caught.name, caught.message); }
  if (expect != NULL) {
    checks++;
    if (ok || strstr(caught.message, expect) == NULL) {
      failures++;
      fprintf(stderr, "FAIL op %d: expected raise containing \"%s\", got %s\n", (int)o->kind, expect, ok ? "success" : caught.message);
    }
  }
  return ok;
}
static void call(qa_app_startup_server_browser *b, enum op_kind kind) { op o; memset(&o, 0, sizeof o); o.kind = kind; o.b = b; (void)run(&o, NULL); }
static void call_raises(qa_app_startup_server_browser *b, enum op_kind kind, const char *expect) {
  op o; memset(&o, 0, sizeof o); o.kind = kind; o.b = b; (void)run(&o, expect);
}
static qa_app_browser_connection connect_now(qa_app_startup_server_browser *b) {
  op o; memset(&o, 0, sizeof o); o.kind = OP_CONNECTION; o.b = b;
  (void)run(&o, NULL);
  return o.connection;
}
static qa_app_startup_server_browser *open_browser(const qa_config_store *config) {
  op o; memset(&o, 0, sizeof o); o.kind = OP_OPEN; o.config = config;
  CHECK(run(&o, NULL) && o.opened != NULL);
  return o.opened;
}
static void add_q3(qa_app_startup_server_browser *b, double source, const qa_net_address *address) {
  op o; memset(&o, 0, sizeof o); o.kind = OP_ADD_Q3; o.b = b; o.source = source; o.address = address;
  (void)run(&o, NULL);
}
static void request_master(qa_app_startup_server_browser *b, double source, const char *remote, double protocol,
                           const char *const *keywords, size_t count) {
  op o; memset(&o, 0, sizeof o); o.kind = OP_MASTER; o.b = b; o.source = source; o.remote = remote; o.protocol = protocol;
  o.keywords = keywords; o.keyword_count = count;
  (void)run(&o, NULL);
}

/* ---------------- tiny HTTP server for master lists ---------------- */
typedef struct http_fixture {
  int fd; int64_t port; pthread_t thread; volatile bool stop;
  char body[4096];
} http_fixture;
static void *http_main(void *arg) {
  http_fixture *h = arg;
  while (!h->stop) {
    int client = accept(h->fd, NULL, NULL);
    if (client < 0) { if (h->stop) break; continue; }
    char request[4096]; size_t used = 0;
    while (used + 1 < sizeof request) {
      ssize_t n = recv(client, request + used, sizeof request - 1 - used, 0);
      if (n <= 0) break;
      used += (size_t)n;
      request[used] = '\0';
      if (strstr(request, "\r\n\r\n") != NULL) break;
    }
    char header[256];
    int hn = snprintf(header, sizeof header, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                      strlen(h->body));
    (void)!send(client, header, (size_t)hn, MSG_NOSIGNAL);
    (void)!send(client, h->body, strlen(h->body), MSG_NOSIGNAL);
    close(client);
  }
  return NULL;
}
static void http_start(http_fixture *h, const char *body) {
  memset(h, 0, sizeof *h);
  snprintf(h->body, sizeof h->body, "%s", body);
  h->fd = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(h->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  CHECK(bind(h->fd, (struct sockaddr *)&addr, sizeof addr) == 0);
  socklen_t len = sizeof addr;
  getsockname(h->fd, (struct sockaddr *)&addr, &len);
  h->port = ntohs(addr.sin_port);
  CHECK(listen(h->fd, 8) == 0);
  CHECK(pthread_create(&h->thread, NULL, http_main, h) == 0);
}
static void http_stop(http_fixture *h) {
  h->stop = true;
  shutdown(h->fd, SHUT_RDWR);
  close(h->fd);
  pthread_join(h->thread, NULL);
}

/* ================= server-browser-masters.test.ts ================= */
static void test_q2_udp_master(void) {
  char *root = temp_root("server-master-");
  qa_config_store config = config_at(root);
  qa_app_startup_server_browser *browser = open_browser(&config);
  qa_udp_transport master, server;
  CHECK(udp_bind_local(&master) && udp_bind_local(&server));
  qa_app_startup_server_browser_choose(browser, "q2");
  char *master_text = address_text(&master.address);
  qa_app_startup_server_browser_set_master_address(browser, master_text);
  call(browser, OP_DISCOVER);
  for (int frame = 0; frame < 150 && !(row_at(browser, 0) != NULL && row_at(browser, 0)->has_status); frame++) {
    qa_receive_event request;
    memset(&request, 0, sizeof request);
    if (qa_udp_transport_poll(&master, &request)) {
      if (request.kind == QA_RECV_PACKET) {
        CHECK(request.length == 5 && memcmp(request.payload, "query", 5) == 0);
        uint8_t reply[14];
        memcpy(reply, "servers ", 8);
        memcpy(reply + 8, server.address.ipv4, 4);
        reply[12] = (uint8_t)(server.address.port >> 8);
        reply[13] = (uint8_t)(server.address.port & 255);
        CHECK(qa_udp_transport_send(&master, &request.from, reply, sizeof reply));
      }
      qa_receive_event_destroy(&request);
    }
    qa_receive_event status;
    memset(&status, 0, sizeof status);
    if (qa_udp_transport_poll(&server, &status)) {
      if (status.kind == QA_RECV_PACKET) {
        size_t length = 0;
        uint8_t *bytes = qa_q2_out_of_band("print\n\\hostname\\Arena\\mapname\\q2dm1\\maxclients\\8\\gamedir\\ctf\\timelimit\\20\n7 40 \"Ranger\"\n",
                                           false, &length);
        CHECK(qa_udp_transport_send(&server, &status.from, bytes, length));
        free(bytes);
      }
      qa_receive_event_destroy(&status);
    }
    sleep_ms(1);
    call(browser, OP_POLL);
  }
  const qa_browser_entry *entry = row_at(browser, 0);
  CHECK(entry != NULL);
  if (entry != NULL) {
    char *key = key_of(&entry->address);
    qa_app_startup_server_browser_select(browser, key);
    free(key);
    entry = row_at(browser, 0);
    CHECK(entry->has_status && strcmp(entry->status.name, "Arena") == 0);
    bool master_source = false;
    for (size_t i = 0; i < entry->source_count; i++) if (strcmp(entry->sources[i], "master") == 0) master_source = true;
    CHECK(master_source);
    size_t count = 0;
    char **details = qa_app_startup_server_browser_details(browser, &count);
    CHECK(lines_contain(details, count, "Ranger  score 7  ping 40"));
    CHECK(lines_contain(details, count, "gamedir: ctf"));
    lines_free(details, count);
    qa_app_startup_server_browser_set_filter(browser, "ctf");
    CHECK(row_count(browser) == 1);
    qa_app_startup_server_browser_set_filter(browser, "rAnGeR");
    CHECK(row_count(browser) == 1);
    qa_app_startup_server_browser_set_filter(browser, "missing player");
    CHECK(row_count(browser) == 0);
  }
  qa_app_startup_server_browser_close(browser);
  qa_app_startup_server_browser *restored = open_browser(&config);
  qa_app_startup_server_browser_choose(restored, "q2");
  CHECK(strcmp(qa_app_startup_server_browser_master_address(restored), master_text) == 0);
  qa_app_startup_server_browser_destroy(restored);
  qa_app_startup_server_browser_destroy(browser);
  qa_udp_transport_destroy(&master);
  qa_udp_transport_destroy(&server);
  free(master_text);
  qa_config_store_destroy(&config);
  remove_tree(root);
  free(root);
}

static void test_http_quakeworld_list(void) {
  char *root = temp_root("qw-master-");
  qa_config_store config = config_at(root);
  qa_app_startup_server_browser *browser = open_browser(&config);
  qa_udp_transport peer;
  CHECK(udp_bind_local(&peer));
  char *peer_text = address_text(&peer.address);
  char body[512];
  snprintf(body, sizeof body, "# public servers\n%s\n%s\n", peer_text, peer_text);
  http_fixture http;
  http_start(&http, body);
  size_t packet_length = 0;
  uint8_t *packet = qa_quakeworld_out_of_band("n\\hostname\\QW Arena\\map\\dm2\\maxclients\\16\\*gamedir\\qw\n1 12 3 45 \"Player one\" \"base\" 4 13\n",
                                              false, &packet_length);
  qa_server_status parsed;
  qa_read_quakeworld_discovery(packet, packet_length, &parsed);
  CHECK(parsed.player_count == 1 && strcmp(parsed.player_details[0].name, "Player one") == 0 &&
        parsed.player_details[0].score == 12 && parsed.player_details[0].ping == 45);
  qa_server_status_destroy(&parsed);
  qa_app_startup_server_browser_choose(browser, "qw");
  char url[96];
  snprintf(url, sizeof url, "http://127.0.0.1:%lld/servers", (long long)http.port);
  qa_app_startup_server_browser_set_master_address(browser, url);
  call(browser, OP_DISCOVER);
  for (int frame = 0; frame < 150 && !(row_at(browser, 0) != NULL && row_at(browser, 0)->has_status); frame++) {
    call(browser, OP_POLL);
    qa_receive_event request;
    memset(&request, 0, sizeof request);
    if (qa_udp_transport_poll(&peer, &request)) {
      if (request.kind == QA_RECV_PACKET) CHECK(qa_udp_transport_send(&peer, &request.from, packet, packet_length));
      qa_receive_event_destroy(&request);
    }
    sleep_ms(1);
  }
  CHECK(row_count(browser) == 1);
  CHECK(row_at(browser, 0) != NULL && row_at(browser, 0)->has_status && strcmp(row_at(browser, 0)->status.map, "dm2") == 0);
  qa_app_startup_server_browser_set_filter(browser, "Player one");
  CHECK(row_count(browser) == 1);
  qa_app_startup_server_browser_set_filter(browser, "");
  qa_app_startup_server_browser_set_address(browser, peer_text);
  qa_app_browser_connection connection = connect_now(browser);
  CHECK(connection.protocol != NULL && strcmp(connection.protocol, "qw") == 0);
  qa_app_browser_connection_destroy(&connection);
  http_stop(&http);
  free(packet);
  free(peer_text);
  qa_udp_transport_destroy(&peer);
  qa_app_startup_server_browser_destroy(browser);
  qa_config_store_destroy(&config);
  remove_tree(root);
  free(root);
}

static void test_http_list_skips_bad_lines(void) {
  char *root = temp_root("qw-master-bad-");
  qa_config_store config = config_at(root);
  qa_app_startup_server_browser *browser = open_browser(&config);
  qa_udp_transport peer;
  CHECK(udp_bind_local(&peer));
  char *peer_text = address_text(&peer.address);
  char body[512];
  snprintf(body, sizeof body, "%s\n1.2.3.5:27500 my favourite server\n1.2.3.6:notaport\n", peer_text);
  http_fixture http;
  http_start(&http, body);
  qa_app_startup_server_browser_choose(browser, "qw");
  char url[96];
  snprintf(url, sizeof url, "http://127.0.0.1:%lld/servers", (long long)http.port);
  qa_app_startup_server_browser_set_master_address(browser, url);
  call(browser, OP_DISCOVER);
  CHECK(row_count(browser) == 1);
  CHECK(strstr(qa_app_startup_server_browser_status(browser), "skipped") != NULL);
  CHECK(strcmp(qa_app_startup_server_browser_status(browser), "Found 1 servers; querying status (2 skipped)") == 0);
  http_stop(&http);
  free(peer_text);
  qa_udp_transport_destroy(&peer);
  qa_app_startup_server_browser_destroy(browser);
  qa_config_store_destroy(&config);
  remove_tree(root);
  free(root);
}

static void test_invalid_saved_direct_list(void) {
  char *root = temp_root("browser-bad-list-");
  qa_config_store config = config_at(root);
  CHECK(qa_config_store_dump(&config, "servers-direct-q1", "{}"));
  qa_app_startup_server_browser *browser = open_browser(&config);
  CHECK(strstr(qa_app_startup_server_browser_status(browser), "servers-direct-q1") != NULL);
  CHECK(strcmp(qa_app_startup_server_browser_status(browser),
               "Ignored invalid saved server data (servers-direct-q1: Invalid direct server list)") == 0);
  qa_app_startup_server_browser_destroy(browser);
  qa_config_store_destroy(&config);
  remove_tree(root);
  free(root);
}

/* ================= server-browser-persistence.test.ts ================= */
static void test_q3_list_reuses_scan(void) {
  char *root = temp_root("quake-browser-read-reuse-");
  qa_config_store config = config_at(root);
  qa_app_startup_server_browser *browser = open_browser(&config);
  qa_udp_transport peer;
  CHECK(udp_bind_local(&peer));
  qa_server_browser *core = qa_app_startup_server_browser_q3_core(browser);
  qa_net_address endpoint = ipv4(127, 0, 0, 1, udp_port(&peer)), extra = ipv4(127, 0, 0, 1, udp_port(&peer) == 65535 ? 65534 : udp_port(&peer) + 1);
  add_q3(browser, 2, &endpoint);
  qa_app_q3_list first = qa_app_startup_server_browser_q3_list(browser, 2);
  bool shared = true;
  for (int i = 0; i < 1024; i++) shared = qa_app_startup_server_browser_q3_list(browser, 2).addresses == first.addresses && shared;
  CHECK(shared);
  long long handle = qa_server_browser_request(core, &endpoint, 0, "status", false, false, 0);
  CHECK(handle != 0);
  qa_receive_event query;
  CHECK(peer_receive(&peer, &query, 200));
  char *challenge = q3_challenge_of(query.payload, query.length);
  CHECK(challenge != NULL);
  qa_receive_event_destroy(&query);
  char info[256];
  snprintf(info, sizeof info, "\\challenge\\%s\\sv_hostname\\Updated status\\mapname\\q3dm1\\sv_maxclients\\8", challenge ? challenge : "");
  size_t length = 0;
  uint8_t *bytes = q3_status_packet(info, NULL, 0, &length);
  char *reply_challenge = NULL;
  qa_server_status reply;
  CHECK(qa_q3_decode_server_status(bytes, length, &reply_challenge, &reply));
  CHECK(qa_server_browser_receive(core, &endpoint, &reply, reply_challenge, 10, NULL));
  qa_server_browser_release_request(core, handle);
  CHECK(qa_app_startup_server_browser_q3_list(browser, 2).addresses == first.addresses);
  CHECK(qa_server_browser_get_entry(core, &endpoint) != NULL && strcmp(qa_server_browser_get_entry(core, &endpoint)->status.name, "Updated status") == 0);
  add_q3(browser, 2, &extra);
  qa_app_q3_list second = qa_app_startup_server_browser_q3_list(browser, 2);
  qa_net_address both[2] = {endpoint, extra};
  CHECK(same_list(second, both, 2));
  CHECK(second.generation != first.generation);
  for (int i = 0; i < 1024; i++) (void)qa_app_startup_server_browser_q3_list(browser, 2);
  CHECK(qa_app_startup_server_browser_q3_list(browser, 2).addresses == second.addresses);
  add_q3(browser, 3, &endpoint);
  CHECK(qa_app_startup_server_browser_q3_list(browser, 2).addresses == second.addresses);
  qa_app_startup_server_browser_remove_q3(browser, 2, &extra);
  qa_app_q3_list third = qa_app_startup_server_browser_q3_list(browser, 2);
  CHECK(same_list(third, &endpoint, 1) && third.generation != second.generation);
  free(challenge); free(reply_challenge); free(bytes);
  qa_server_status_destroy(&reply);
  qa_net_address_destroy(&endpoint); qa_net_address_destroy(&extra);
  qa_udp_transport_destroy(&peer);
  qa_app_startup_server_browser_destroy(browser);
  qa_config_store_destroy(&config);
  remove_tree(root);
  free(root);
}

/* The TS write gate exposes the optimistic favorite while the write is pending; the C write
 * completes in place, so the check is the rolled-back state and its fresh snapshot. */
static void test_favorite_rollback_invalidates_snapshot(void) {
  char *root = temp_root("quake-browser-read-rollback-");
  qa_config_store config = config_at(root);
  qa_app_startup_server_browser *browser = open_browser(&config);
  qa_app_startup_server_browser_choose(browser, "q3");
  qa_app_startup_server_browser_set_address(browser, "127.0.0.1:27960");
  qa_app_q3_list before = qa_app_startup_server_browser_q3_list(browser, 3);
  CHECK(before.address_count == 0);
  remove_tree(root);
  FILE *blocker = fopen(root, "w");
  CHECK(blocker != NULL);
  if (blocker) { fputs("not a settings directory", blocker); fclose(blocker); }
  call_raises(browser, OP_FAVORITE, "could not write servers-cache-q3");
  qa_app_q3_list rolled = qa_app_startup_server_browser_q3_list(browser, 3);
  CHECK(rolled.address_count == 0);
  CHECK(rolled.generation == before.generation + 2);
  CHECK(qa_app_startup_server_browser_q3_list(browser, 3).addresses == rolled.addresses);
  qa_app_startup_server_browser_destroy(browser);
  qa_config_store_destroy(&config);
  remove(root);
  free(root);
}

static void test_master_packet_eot_boundary(void) {
  qa_net_address addresses[256];
  for (int i = 0; i < 256; i++) addresses[i] = ipv4(127, 0, 0, 1, 20000 + i);
  for (int complete = 1; complete >= 0; complete--) {
    size_t length = 0;
    uint8_t *packet = master_packet(addresses, 256, complete, &length);
    qa_net_address *out = NULL; size_t count = 0; bool done = !complete;
    CHECK(qa_q3_decode_master_packet(packet, length, &out, &count, &done));
    CHECK(count == 256 && done == (bool)complete);
    bool same = count == 256;
    for (size_t i = 0; same && i < count; i++) same = qa_net_same_address(&out[i], &addresses[i], true);
    CHECK(same);
    qa_q3_master_addresses_free(out, count);
    free(packet);
  }
}

static void test_cache_master_capacity(void) {
  size_t count = 8193;
  qa_browser_entry *entries = calloc(count, sizeof *entries);
  static const char *master[] = {"master"};
  for (size_t i = 0; i < count; i++) {
    entries[i].address = i < 8192 ? ipv4(127, 0, 0, 1, 20000 + (int64_t)i) : ipv4(127, 0, 0, 2, 27960);
    entries[i].sources = master;
    entries[i].source_count = 1;
  }
  op o; memset(&o, 0, sizeof o);
  o.kind = OP_WRITE_CACHE; o.entries = entries; o.entry_count = 8192;
  CHECK(run(&o, NULL));
  op r; memset(&r, 0, sizeof r);
  r.kind = OP_READ_CACHE; r.text = o.written;
  CHECK(run(&r, NULL) && r.cache.entry_count == 8192);
  qa_app_q3_browser_cache_free(&r.cache);
  free(o.written);
  memset(&o, 0, sizeof o);
  o.kind = OP_WRITE_CACHE; o.entries = entries; o.entry_count = count;
  (void)run(&o, "master entries");
  for (size_t i = 0; i < count; i++) qa_net_address_destroy(&entries[i].address);
  free(entries);
}

typedef struct retire_ctx { sentinel s; } retire_ctx;
/* assertCurrent that holds on its first call and retires on the second (the TS flag flips during the await). */
static void retire_second(void *ctx) {
  sentinel *s = fix(ctx);
  if (++s->calls >= s->fail_on) qa_raise("Error", 0, 0, "%s", s->message);
}
static bool poll_until(qa_app_startup_server_browser *b) {
  sleep_ms(3);
  op o; memset(&o, 0, sizeof o); o.kind = OP_POLL; o.b = b;
  return run(&o, NULL);
}
static void q3_send(qa_udp_transport *from, const qa_net_address *to, const qa_net_address *addresses, size_t count, bool complete) {
  size_t length = 0;
  uint8_t *packet = master_packet(addresses, count, complete, &length);
  CHECK(qa_udp_transport_send(from, to, packet, length));
  free(packet);
}
static void q3_answer_status(qa_udp_transport *peer, const char *name) {
  qa_receive_event query;
  CHECK(peer_receive(peer, &query, 200));
  char *challenge = q3_challenge_of(query.payload, query.length);
  CHECK(challenge != NULL);
  char info[256];
  snprintf(info, sizeof info, "\\challenge\\%s\\sv_hostname\\%s\\mapname\\q3dm1\\sv_maxclients\\8", challenge ? challenge : "", name);
  qa_q3_status_player player = {4, 12, "Player"};
  size_t length = 0;
  uint8_t *bytes = q3_status_packet(info, &player, 1, &length);
  CHECK(qa_udp_transport_send(peer, &query.from, bytes, length));
  free(bytes); free(challenge);
  qa_receive_event_destroy(&query);
}
static void test_q3_facade_masters_and_cache(void) {
  char *root = temp_root("quake-browser-facade-");
  qa_config_store config = config_at(root);
  qa_udp_transport peer, stranger;
  CHECK(udp_bind_local(&peer) && udp_bind_local(&stranger));
  qa_app_startup_server_browser *browser = open_browser(&config);
  qa_net_address first = ipv4(127, 0, 0, 1, 20001), second = ipv4(127, 0, 0, 1, 20002);
  qa_net_address to_browser = browser_endpoint(browser);
  double initial = qa_app_startup_server_browser_q3_list(browser, 2).generation;
  char peer_remote[64];
  snprintf(peer_remote, sizeof peer_remote, "127.0.0.1:%lld", (long long)udp_port(&peer));
  static const char *const keywords[] = {"empty", "full"};
  request_master(browser, 2, peer_remote, 71, keywords, 2);
  qa_receive_event request;
  CHECK(peer_receive(&peer, &request, 200));
  char *line = q3_line_of(request.payload, request.length);
  CHECK(line != NULL && strcmp(line, "getservers 71 empty full") == 0);
  free(line);
  qa_receive_event_destroy(&request);
  CHECK(qa_app_startup_server_browser_q3_list(browser, 2).pending);
  q3_send(&stranger, &to_browser, &first, 1, true);
  CHECK(poll_until(browser));
  CHECK(qa_app_startup_server_browser_q3_list(browser, 2).address_count == 0);
  q3_send(&peer, &to_browser, &first, 1, false);
  CHECK(poll_until(browser));
  CHECK(same_list(qa_app_startup_server_browser_q3_list(browser, 2), &first, 1));
  CHECK(!qa_app_startup_server_browser_q3_list(browser, 2).pending);
  q3_send(&peer, &to_browser, &second, 1, true);
  CHECK(poll_until(browser));
  qa_net_address both[2] = {first, second};
  CHECK(same_list(qa_app_startup_server_browser_q3_list(browser, 2), both, 2));
  CHECK(qa_app_startup_server_browser_q3_list(browser, 2).generation > initial);
  request_master(browser, 1, peer_remote, 68, NULL, 0);
  qa_receive_event secondary;
  CHECK(peer_receive(&peer, &secondary, 200));
  qa_receive_event_destroy(&secondary);
  q3_send(&peer, &to_browser, &peer.address, 1, true);
  CHECK(poll_until(browser));
  CHECK(same_list(qa_app_startup_server_browser_q3_list(browser, 1), &peer.address, 1));
  CHECK(same_list(qa_app_startup_server_browser_q3_list(browser, 2), both, 2));
  add_q3(browser, 3, &peer.address);
  qa_server_browser *core = qa_app_startup_server_browser_q3_core(browser);
  long long status = qa_server_browser_request(core, &peer.address, qa_net_default_now(NULL), "status", false, false, 0);
  CHECK(status != 0);
  q3_answer_status(&peer, "Actual status");
  CHECK(poll_until(browser));
  qa_discovery_result result;
  CHECK(qa_server_browser_request_result(core, status, &result) && strcmp(result.kind, "completed") == 0);
  qa_net_address_destroy(&result.address);
  if (result.has_status) qa_server_status_destroy(&result.status);
  qa_server_browser_release_request(core, status);
  /* view: lists 1 (Authored name), 2 ([second, first] Global), 3 (Favorite) */
  qa_q3_browser_cache_row rows1[1], rows2[2], rows3[1];
  memset(rows1, 0, sizeof rows1); memset(rows2, 0, sizeof rows2); memset(rows3, 0, sizeof rows3);
  rows1[0].address.kind = QA_Q3_ADDR_IPV4; memcpy(rows1[0].address.host, peer.address.ipv4, 4); rows1[0].address.port = peer.address.port;
  rows1[0].name = (char *)"Authored name"; rows1[0].visible = 7; rows1[0].ping = 23;
  const qa_net_address *order[2] = {&second, &first};
  for (int i = 0; i < 2; i++) {
    rows2[i].address.kind = QA_Q3_ADDR_IPV4; memcpy(rows2[i].address.host, order[i]->ipv4, 4); rows2[i].address.port = order[i]->port;
    rows2[i].name = (char *)"Global"; rows2[i].visible = 1; rows2[i].ping = -1;
  }
  rows3[0] = rows1[0]; rows3[0].name = (char *)"Favorite"; rows3[0].visible = 3; rows3[0].ping = 24;
  qa_q3_browser_cache_list lists[3] = {{1, rows1, 1}, {2, rows2, 2}, {3, rows3, 1}};
  qa_q3_browser_cache_view view = {lists, 3};
  op save; memset(&save, 0, sizeof save); save.kind = OP_SAVE_CACHE; save.b = browser; save.view = &view;
  CHECK(run(&save, NULL));
  long long newer = qa_server_browser_request(core, &peer.address, qa_net_default_now(NULL), "status", false, false, 0);
  CHECK(newer != 0);
  q3_answer_status(&peer, "New live status");
  CHECK(poll_until(browser));
  op load; memset(&load, 0, sizeof load); load.kind = OP_LOAD_CACHE; load.b = browser;
  CHECK(run(&load, NULL) && load.loaded_view);
  if (load.loaded) { qa_q3_browser_cache_view_destroy(load.loaded); free(load.loaded); }
  const qa_browser_entry *live = qa_server_browser_get_entry(core, &peer.address);
  CHECK(live != NULL && live->has_status && strcmp(live->status.name, "New live status") == 0);
  qa_server_browser_release_request(core, newer);
  qa_app_startup_server_browser_choose(browser, "q3");
  qa_app_startup_server_browser_set_address(browser, "127.0.0.1:20003");
  call(browser, OP_FAVORITE);
  char *saved = load_text(&config, "servers-cache-q3");
  CHECK(saved != NULL);
  if (saved != NULL) {
    op read; memset(&read, 0, sizeof read); read.kind = OP_READ_CACHE; read.text = saved;
    CHECK(run(&read, NULL) && read.cache.has_view);
    if (read.cache.has_view) {
      CHECK(read.cache.view.lists[0].row_count > 0 && strcmp(read.cache.view.lists[0].rows[0].name, "Authored name") == 0);
      CHECK(read.cache.view.lists[1].row_count == 2 && read.cache.view.lists[1].rows[0].address.port == 20002 &&
            read.cache.view.lists[1].rows[1].address.port == 20001);
    }
    qa_app_q3_browser_cache_free(&read.cache);
  }
  free(saved);
  /* writeQ3BrowserCache(entries, mismatched view) */
  size_t entry_count = qa_server_browser_size(core);
  qa_browser_entry *entries = calloc(entry_count ? entry_count : 1, sizeof *entries);
  for (size_t i = 0; i < entry_count; i++) entries[i] = *qa_server_browser_entry_at(core, i);
  qa_q3_browser_cache_row wrong[2];
  memset(wrong, 0, sizeof wrong);
  for (int i = 0; i < 2; i++) {
    wrong[i].address.kind = QA_Q3_ADDR_IPV4; memcpy(wrong[i].address.host, first.ipv4, 4); wrong[i].address.port = first.port;
    wrong[i].visible = 1;
  }
  wrong[0].name = (char *)"Wrong source"; wrong[1].name = (char *)"B";
  qa_q3_browser_cache_list mismatched[3] = {{1, wrong, 1}, {2, NULL, 0}, {3, NULL, 0}};
  qa_q3_browser_cache_view mismatched_view = {mismatched, 3};
  op write; memset(&write, 0, sizeof write); write.kind = OP_WRITE_CACHE; write.entries = entries; write.entry_count = entry_count;
  write.view = &mismatched_view;
  (void)run(&write, "membership");
  wrong[0].name = (char *)"A";
  qa_q3_browser_cache_list duplicate[3] = {{1, NULL, 0}, {2, wrong, 2}, {3, NULL, 0}};
  qa_q3_browser_cache_view duplicate_view = {duplicate, 3};
  write.view = &duplicate_view;
  (void)run(&write, "membership");
  free(entries);
  qa_app_startup_server_browser_destroy(browser);
  browser = open_browser(&config);
  core = qa_app_startup_server_browser_q3_core(browser);
  CHECK(same_list(qa_app_startup_server_browser_q3_list(browser, 2), both, 2));
  live = qa_server_browser_get_entry(core, &peer.address);
  CHECK(live != NULL && live->has_status && live->status.player_count == 1 &&
        strcmp(live->status.player_details[0].name, "Player") == 0 && live->status.player_details[0].score == 4 &&
        live->status.player_details[0].ping == 12);
  CHECK(live != NULL && live->has_status && qa_qw_info_get(&live->status.rules, "mapname") != NULL &&
        strcmp(qa_qw_info_get(&live->status.rules, "mapname"), "q3dm1") == 0);
  qa_app_q3_list favorites = qa_app_startup_server_browser_q3_list(browser, 3);
  bool has_20003 = false;
  for (size_t i = 0; i < favorites.address_count; i++) {
    char *key = key_of(&favorites.addresses[i]);
    if (strcmp(key, "127.0.0.1:20003") == 0) has_20003 = true;
    free(key);
  }
  CHECK(has_20003);
  /* loadQ3Cache whose UI retires while the cache read is pending. */
  qa_app_q3_list before = qa_app_startup_server_browser_q3_list(browser, 2);
  sentinel cache_current = {MAGIC, 0, 2, "Retired cache view"};
  op retired_load; memset(&retired_load, 0, sizeof retired_load);
  retired_load.kind = OP_LOAD_CACHE; retired_load.b = browser; retired_load.assert_current = retire_second; retired_load.assert_ctx = &cache_current;
  (void)run(&retired_load, "Retired cache view");
  qa_app_q3_list after = qa_app_startup_server_browser_q3_list(browser, 2);
  CHECK(after.addresses == before.addresses && after.generation == before.generation && after.pending == before.pending &&
        after.reset_generation == before.reset_generation);
  /* requestQ3Master whose UI retires during the resolve. */
  sentinel ui_current = {MAGIC, 0, 2, "Retired UI"};
  op retired_master; memset(&retired_master, 0, sizeof retired_master);
  retired_master.kind = OP_MASTER; retired_master.b = browser; retired_master.source = 2; retired_master.remote = "localhost:27950";
  retired_master.protocol = 68; retired_master.assert_current = retire_second; retired_master.assert_ctx = &ui_current;
  (void)run(&retired_master, "Retired UI");
  CHECK(!qa_app_startup_server_browser_q3_list(browser, 2).pending);
  /* close() is idempotent; a later operation finds the browser closed. */
  qa_app_startup_server_browser_close(browser);
  qa_app_startup_server_browser_close(browser);
  op closed_resolve; memset(&closed_resolve, 0, sizeof closed_resolve);
  closed_resolve.kind = OP_RESOLVE_Q3; closed_resolve.b = browser; closed_resolve.remote = "localhost";
  (void)run(&closed_resolve, "Server browser is closed");
  qa_app_startup_server_browser_destroy(browser);
  qa_net_address_destroy(&first); qa_net_address_destroy(&second); qa_net_address_destroy(&to_browser);
  qa_udp_transport_destroy(&peer); qa_udp_transport_destroy(&stranger);
  qa_config_store_destroy(&config);
  remove_tree(root);
  free(root);
}

/* normal browser menu persists favorites, removal, selected addresses and direct connect */
static void test_menu_persistence(void) {
  char *root = temp_root("quake-browser-menu-");
  qa_config_store config = config_at(root);
  static const char *const direct_only[] = {"direct"};
  static const char *const favorite_direct[] = {"favorite", "direct"};
  /* first: address 127.0.0.1:27911, favorite */
  qa_app_startup_server_browser *b = open_browser(&config);
  qa_app_startup_server_browser_choose(b, "q2");
  qa_app_startup_server_browser_set_address(b, "127.0.0.1:27911");
  CHECK(strcmp(qa_app_startup_server_browser_address(b), "127.0.0.1:27911") == 0);
  call(b, OP_FAVORITE);
  qa_app_startup_server_browser_destroy(b);
  char *saved = load_text(&config, "servers-q2");
  CHECK(saved != NULL && strcmp(saved, "[{\"kind\":\"ipv4\",\"host\":[127,0,0,1],\"port\":27911}]") == 0);
  free(saved);
  /* second: select the row and remove the favorite */
  b = open_browser(&config);
  qa_app_startup_server_browser_choose(b, "q2");
  CHECK(row_count(b) == 1);
  qa_app_startup_server_browser_select(b, "127.0.0.1:27911");
  CHECK(qa_app_startup_server_browser_selected(b) != NULL && strcmp(qa_app_startup_server_browser_selected(b), "127.0.0.1:27911") == 0);
  CHECK(strcmp(qa_app_startup_server_browser_address(b), "127.0.0.1:27911") == 0);
  call(b, OP_FAVORITE);
  qa_app_startup_server_browser_destroy(b);
  saved = load_text(&config, "servers-q2");
  CHECK(saved != NULL && strcmp(saved, "[]") == 0);
  free(saved);
  /* third: direct connect by hostname */
  b = open_browser(&config);
  qa_app_startup_server_browser_choose(b, "q2");
  CHECK(row_count(b) == 0);
  qa_app_startup_server_browser_set_address(b, "localhost:27912");
  qa_app_browser_connection connection = connect_now(b);
  CHECK(connection.remote != NULL && strcmp(connection.protocol, "q2") == 0 && strcmp(connection.remote, "localhost:27912") == 0 &&
        !connection.has_q2_protocol);
  qa_app_browser_connection_destroy(&connection);
  qa_app_startup_server_browser_destroy(b);
  saved = load_text(&config, "servers-direct-q2");
  size_t count = 0;
  qa_app_direct_server_address *direct = saved ? qa_app_read_direct_servers(saved, &count) : NULL;
  CHECK(count == 1 && strcmp(direct[0].remote, "localhost:27912") == 0);
  qa_app_direct_servers_free(direct, count);
  free(saved);
  /* fourth: the remembered hostname survives selection, reconnect and favorite */
  b = open_browser(&config);
  qa_app_startup_server_browser_choose(b, "q2");
  CHECK(strcmp(qa_app_startup_server_browser_address(b), "localhost:27912") == 0);
  CHECK(sources_are(row_at(b, 0), direct_only, 1));
  qa_app_startup_server_browser_select(b, "127.0.0.1:27912");
  CHECK(qa_app_startup_server_browser_selected(b) != NULL && strcmp(qa_app_startup_server_browser_selected(b), "127.0.0.1:27912") == 0);
  CHECK(strcmp(qa_app_startup_server_browser_address(b), "localhost:27912") == 0);
  connection = connect_now(b);
  CHECK(connection.remote != NULL && strcmp(connection.remote, "localhost:27912") == 0);
  qa_app_browser_connection_destroy(&connection);
  call(b, OP_FAVORITE);
  qa_app_startup_server_browser_destroy(b);
  saved = load_text(&config, "servers-direct-q2");
  direct = saved ? qa_app_read_direct_servers(saved, &count) : NULL;
  CHECK(count == 1 && strcmp(direct[0].remote, "localhost:27912") == 0);
  qa_app_direct_servers_free(direct, count);
  free(saved);
  /* fifth: favorite then direct membership; remove the favorite */
  b = open_browser(&config);
  qa_app_startup_server_browser_choose(b, "q2");
  CHECK(sources_are(row_at(b, 0), favorite_direct, 2));
  qa_app_startup_server_browser_select(b, "127.0.0.1:27912");
  call(b, OP_FAVORITE);
  qa_app_startup_server_browser_destroy(b);
  /* sixth: favorites-only hides the direct row */
  b = open_browser(&config);
  qa_app_startup_server_browser_choose(b, "q2");
  CHECK(sources_are(row_at(b, 0), direct_only, 1));
  qa_app_startup_server_browser_set_favorites_only(b, !qa_app_startup_server_browser_favorites_only(b));
  CHECK(row_count(b) == 0);
  qa_app_startup_server_browser_set_favorites_only(b, !qa_app_startup_server_browser_favorites_only(b));
  CHECK(row_count(b) == 1);
  qa_app_startup_server_browser_destroy(b);
  qa_config_store_destroy(&config);
  remove_tree(root);
  free(root);
}

static void test_direct_recall_and_failed_favorite(void) {
  char *root = temp_root("quake-browser-addresses-");
  qa_config_store config = config_at(root);
  qa_app_startup_server_browser *b = open_browser(&config);
  qa_app_browser_connection connection;
  qa_app_startup_server_browser_choose(b, "q2");
  qa_app_startup_server_browser_set_address(b, "127.0.0.1:27911");
  connection = connect_now(b); qa_app_browser_connection_destroy(&connection);
  qa_app_startup_server_browser_choose(b, "q3");
  qa_app_startup_server_browser_set_address(b, "127.0.0.1:27961");
  connection = connect_now(b); qa_app_browser_connection_destroy(&connection);
  qa_app_startup_server_browser_choose(b, "q2");
  CHECK(strcmp(qa_app_startup_server_browser_address(b), "127.0.0.1:27911") == 0);
  qa_app_startup_server_browser_set_address(b, "127.0.0.1:27912");
  connection = connect_now(b); qa_app_browser_connection_destroy(&connection);
  qa_app_startup_server_browser_destroy(b);
  b = open_browser(&config);
  qa_app_startup_server_browser_choose(b, "q2");
  CHECK(strcmp(qa_app_startup_server_browser_address(b), "127.0.0.1:27912") == 0);
  CHECK(row_count(b) == 2);
  qa_app_startup_server_browser_choose(b, "q3");
  CHECK(strcmp(qa_app_startup_server_browser_address(b), "127.0.0.1:27961") == 0);
  CHECK(row_count(b) == 1);
  qa_app_startup_server_browser_choose(b, "q1");
  CHECK(strcmp(qa_app_startup_server_browser_address(b), "localhost:26000") == 0);
  CHECK(row_count(b) == 0);
  qa_app_startup_server_browser_set_address(b, "");
  call_raises(b, OP_CONNECTION, "Invalid direct server address");
  remove_tree(root);
  FILE *blocker = fopen(root, "w");
  CHECK(blocker != NULL);
  if (blocker) { fputs("not a settings directory", blocker); fclose(blocker); }
  qa_app_startup_server_browser_set_address(b, "127.0.0.1:26001");
  call_raises(b, OP_FAVORITE, "");
  CHECK(row_count(b) == 0);
  qa_app_startup_server_browser_destroy(b);
  qa_config_store_destroy(&config);
  remove(root);
  free(root);
}

/* ================= server-browser-sort-filter.test.ts ================= */
typedef struct server_row { const char *name, *map; int players, max_players; } server_row;
static const server_row SERVERS[4] = {
  {"Alpha Arena", "zbase", 0, 8}, {"Bravo Base", "abase", 8, 8}, {"Charlie Coop", "mhub", 3, 8}, {"Unknown capacity", "unknown", 0, 0}};
static uint8_t *status_reply(const char *protocol, const server_row *server, const qa_net_address *peer, const uint8_t *query,
                             size_t query_length, size_t *length) {
  char names[8][32];
  if (strcmp(protocol, "q1") == 0) {
    qa_netquake_control m;
    memset(&m, 0, sizeof m);
    m.kind = QA_NQ_SERVER_INFO; m.version = 3;
    char *address = address_text(peer);
    m.address = address; m.name = (char *)server->name; m.map = (char *)server->map;
    m.players = server->players; m.max_players = server->max_players;
    uint8_t *bytes = qa_encode_netquake_control(&m, length);
    free(address);
    return bytes;
  }
  if (strcmp(protocol, "q2") == 0) {
    qa_q2_status_player players[8];
    for (int i = 0; i < server->players; i++) {
      snprintf(names[i], sizeof names[i], "Player %d", i);
      players[i].name = names[i]; players[i].score = i; players[i].ping = 20;
    }
    char info[256];
    snprintf(info, sizeof info, "\\hostname\\%s\\mapname\\%s\\maxclients\\%d", server->name, server->map, server->max_players);
    qa_q2_status status = {info, players, (size_t)server->players};
    char *text = qa_q2_status_text(&status, false, 0);
    size_t n = strlen(text) + 8;
    char *full = malloc(n);
    snprintf(full, n, "print\n%s", text);
    uint8_t *bytes = qa_q2_out_of_band(full, false, length);
    free(text); free(full);
    return bytes;
  }
  qa_q3_status_player players[8];
  for (int i = 0; i < server->players; i++) {
    snprintf(names[i], sizeof names[i], "Player %d", i);
    players[i].name = names[i]; players[i].score = i; players[i].ping = 20;
  }
  char *challenge = q3_challenge_of(query, query_length);
  char info[256];
  snprintf(info, sizeof info, "\\sv_hostname\\%s\\mapname\\%s\\sv_maxclients\\%d\\challenge\\%s", server->name, server->map,
           server->max_players, challenge ? challenge : "");
  free(challenge);
  return q3_status_packet(info, players, (size_t)server->players, length);
}
static bool rows_have_name(qa_app_startup_server_browser *b, const char *name) {
  size_t count = 0;
  const qa_browser_entry **rows = qa_app_startup_server_browser_rows(b, &count);
  for (size_t i = 0; i < count; i++) if (rows[i]->has_status && strcmp(rows[i]->status.name, name) == 0) return true;
  return false;
}
static void populate(qa_app_startup_server_browser *b, qa_udp_transport *peers, const char *protocol) {
  for (int index = 0; index < 5; index++) {
    char *peer_text = address_text(&peers[index].address);
    qa_app_startup_server_browser_set_address(b, peer_text);
    free(peer_text);
    if (index == 4) {
      qa_app_browser_connection connection = connect_now(b);
      qa_app_browser_connection_destroy(&connection);
      continue;
    }
    call(b, OP_QUERY);
    qa_receive_event event;
    bool answered = peer_receive(&peers[index], &event, 100);
    CHECK(answered);
    if (answered) {
      size_t length = 0;
      uint8_t *bytes = status_reply(protocol, &SERVERS[index], &peers[index].address, event.payload, event.length, &length);
      CHECK(qa_udp_transport_send(&peers[index], &event.from, bytes, length));
      free(bytes);
      qa_receive_event_destroy(&event);
    }
    for (int attempt = 0; attempt < 100 && !rows_have_name(b, SERVERS[index].name); attempt++) {
      sleep_ms(1);
      call(b, OP_POLL);
    }
    CHECK(rows_have_name(b, SERVERS[index].name));
  }
}
static size_t names(qa_app_startup_server_browser *b, const char **out) {
  size_t count = 0;
  const qa_browser_entry **rows = qa_app_startup_server_browser_rows(b, &count);
  for (size_t i = 0; i < count && i < 8; i++) out[i] = rows[i]->has_status ? rows[i]->status.name : "Unanswered";
  return count;
}
static bool names_are(const char **got, size_t from, const char *const *expected, size_t count) {
  for (size_t i = 0; i < count; i++) if (strcmp(got[from + i], expected[i]) != 0) return false;
  return true;
}
static bool names_contain(const char **got, size_t count, const char *name) {
  for (size_t i = 0; i < count; i++) if (strcmp(got[i], name) == 0) return true;
  return false;
}
static void test_sort_and_filter(void) {
  char *root = temp_root("quake-browser-sorting-");
  qa_config_store config = config_at(root);
  qa_app_startup_server_browser *b = open_browser(&config);
  qa_udp_transport peers[5];
  for (int i = 0; i < 5; i++) CHECK(udp_bind_local(&peers[i]));
  static const char *const protocols[3] = {"q1", "q2", "q3"};
  static const char *const orders[8] = {"ping-low", "ping-high", "name-az", "name-za", "map-az", "map-za", "players-most", "players-fewest"};
  for (int p = 0; p < 3; p++) {
    qa_app_startup_server_browser_choose(b, protocols[p]);
    CHECK(strcmp(qa_app_startup_server_browser_protocol(b), protocols[p]) == 0);
    populate(b, peers, protocols[p]);
    CHECK(row_count(b) == 5);
    const char *got[8], *ascending[8];
    size_t count = names(b, ascending);
    CHECK(count == 5);
    for (int o = 0; o < 8; o++) {
      qa_app_startup_server_browser_choose_sort(b, orders[o]);
      CHECK(strcmp(qa_app_startup_server_browser_sort_order(b), orders[o]) == 0);
      count = names(b, got);
      CHECK(count == 5 && strcmp(got[4], "Unanswered") == 0);
      if (o == 1) {
        const char *reversed[4] = {ascending[3], ascending[2], ascending[1], ascending[0]};
        CHECK(names_are(got, 0, reversed, 4));
      }
      static const char *const name_az[4] = {"Alpha Arena", "Bravo Base", "Charlie Coop", "Unknown capacity"};
      static const char *const name_za[4] = {"Unknown capacity", "Charlie Coop", "Bravo Base", "Alpha Arena"};
      static const char *const map_az[4] = {"Bravo Base", "Charlie Coop", "Unknown capacity", "Alpha Arena"};
      static const char *const map_za[4] = {"Alpha Arena", "Unknown capacity", "Charlie Coop", "Bravo Base"};
      static const char *const most[2] = {"Bravo Base", "Charlie Coop"};
      static const char *const fewest[2] = {"Charlie Coop", "Bravo Base"};
      if (o == 2) CHECK(names_are(got, 0, name_az, 4));
      if (o == 3) CHECK(names_are(got, 0, name_za, 4));
      if (o == 4) CHECK(names_are(got, 0, map_az, 4));
      if (o == 5) CHECK(names_are(got, 0, map_za, 4));
      if (o == 6) CHECK(names_are(got, 0, most, 2));
      if (o == 7) CHECK(names_are(got, 2, fewest, 2));
    }
    qa_app_startup_server_browser_set_hide_empty(b, true);
    count = names(b, got);
    CHECK(!names_contain(got, count, "Alpha Arena"));
    qa_app_startup_server_browser_set_hide_full(b, true);
    count = names(b, got);
    CHECK(names_contain(got, count, "Unknown capacity") && names_contain(got, count, "Unanswered"));
    CHECK(!names_contain(got, count, "Bravo Base") && names_contain(got, count, "Charlie Coop"));
    qa_app_startup_server_browser_set_filter(b, "mhub");
    count = names(b, got);
    CHECK(count == 1 && strcmp(got[0], "Charlie Coop") == 0);
    qa_app_startup_server_browser_set_filter(b, "absent-server");
    CHECK(row_count(b) == 0);
    CHECK(strcmp(qa_app_startup_server_browser_empty_message(b), "No servers match these filters.") == 0);
    qa_app_startup_server_browser_set_filter(b, "");
    qa_app_startup_server_browser_set_hide_empty(b, false);
    qa_app_startup_server_browser_set_hide_full(b, false);
    qa_app_startup_server_browser_choose_sort(b, "ping-low");
    CHECK(row_count(b) == 5);
  }
  for (int i = 0; i < 5; i++) qa_udp_transport_destroy(&peers[i]);
  qa_app_startup_server_browser_destroy(b);
  qa_config_store_destroy(&config);
  remove_tree(root);
  free(root);
}

/* ================= startup-selection.test.ts: real local C hosts ================= */
typedef struct local_host {
  unsigned magic;
  qa_udp_transport transport;
  /* Q1 */
  qa_netquake_host q1;
  /* Q2 */
  qa_q2_connectionless_host q2_host; qa_q2_challenge_table q2_challenges; qa_q2_connectionless_server q2;
  /* Q3 */
  qa_app_q3_server_network *q3; qa_q3_product q3_product; char q3_status[512];
  int status_calls;
} local_host;
static local_host *host_of(void *ctx) { CHECK(ctx != NULL && ((local_host *)ctx)->magic == MAGIC); return ctx; }
static char *heap_text(const char *text) { size_t n = strlen(text) + 1; char *out = malloc(n); memcpy(out, text, n); return out; }
static void q1_server_info(void *ctx, qa_netquake_control *out) {
  local_host *h = host_of(ctx);
  h->status_calls++;
  out->kind = QA_NQ_SERVER_INFO; out->version = 3;
  out->address = address_text(&h->transport.address);
  out->name = heap_text("C NetQuake host"); out->map = heap_text("e1m1");
  out->players = 0; out->max_players = 8;
}
static int q1_player_info(void *ctx, double index, qa_netquake_control *out) { (void)host_of(ctx); (void)index; (void)out; return 0; }
static int q1_next_rule(void *ctx, const char *previous, char **name, char **value) { (void)host_of(ctx); (void)previous; (void)name; (void)value; return 0; }
static qa_nq_connect_result q1_connect(void *ctx, const qa_net_address *from, double now) {
  (void)host_of(ctx); (void)from; (void)now;
  qa_nq_connect_result result = {1, 0, "Server is full.\n"};
  return result;
}
static qa_q2_status q2_status(void *ctx) {
  host_of(ctx)->status_calls++;
  qa_q2_status status = {"\\hostname\\C Q2 host\\mapname\\base1\\maxclients\\8", NULL, 0};
  return status;
}
static qa_q2_host_info q2_info(void *ctx) { (void)host_of(ctx); qa_q2_host_info info = {"C Q2 host", "base1", 0, 8}; return info; }
static qa_q2_connect_admission q2_connect(void *ctx, const qa_net_address *from, const qa_q2_connect_request *request) {
  (void)host_of(ctx); (void)from; (void)request;
  qa_q2_connect_admission admission = {false, NULL, heap_text("Server is full.")};
  return admission;
}
static void q2_reply(void *ctx, const qa_net_address *to, const uint8_t *bytes, size_t length) {
  local_host *h = host_of(ctx);
  (void)qa_udp_transport_send(&h->transport, to, bytes, length);
}
static double q2_random(void *ctx) { (void)host_of(ctx); return 0.5; }
static const char *q3_status_text(void *ctx, const char *challenge, bool detailed) {
  local_host *h = host_of(ctx);
  h->status_calls++;
  snprintf(h->q3_status, sizeof h->q3_status, detailed ? "statusResponse\n\\sv_hostname\\C Q3 host\\mapname\\q3dm1\\sv_maxclients\\8\\challenge\\%s\n"
                                                      : "infoResponse\n\\protocol\\68\\hostname\\C Q3 host\\mapname\\q3dm1\\clients\\0\\sv_maxclients\\8\\challenge\\%s",
           challenge);
  return h->q3_status;
}
static void q3_print(void *ctx, const char *text) { (void)host_of(ctx); (void)text; }
static double q3_time(void *ctx) { (void)host_of(ctx); return 0; }
static const double *q3_occupied(void *ctx, size_t *count) { (void)host_of(ctx); *count = 0; return NULL; }
static double q3_random(void *ctx) { (void)host_of(ctx); return 0.25; }
static void q3_prepare(void *ctx, double feed, double server_id, void (*configstring)(void *, double, const char *), void *configstring_ctx) {
  (void)host_of(ctx); (void)feed; (void)server_id; (void)configstring; (void)configstring_ctx;
}
static bool q3_downloads(void *ctx) { (void)host_of(ctx); return false; }
static qa_datagram_transport q3_transport_view;
static void host_start(local_host *h, const char *protocol) {
  memset(h, 0, sizeof *h);
  h->magic = MAGIC;
  CHECK(udp_bind_local(&h->transport));
  if (strcmp(protocol, "q1") == 0) {
    qa_netquake_host q1 = {h, q1_server_info, q1_player_info, q1_next_rule, q1_connect};
    h->q1 = q1;
  } else if (strcmp(protocol, "q2") == 0) {
    static qa_q2_protocol_identity classic;
    qa_q2_get_protocol(34, 0, &classic);
    h->q2_host.ctx = h; h->q2_host.protocols = &classic; h->q2_host.protocol_count = 1;
    h->q2_host.status = q2_status; h->q2_host.info = q2_info; h->q2_host.connect = q2_connect; h->q2_host.reply = q2_reply;
    CHECK(qa_q2_challenge_table_init(&h->q2_challenges, q2_random, h, 0));
    h->q2.host = &h->q2_host; h->q2.challenges = &h->q2_challenges;
  } else {
    q3_transport_view = qa_udp_transport_as_transport(&h->transport);
    h->q3_product = QA_Q3_PRODUCT_BASEQ3;
    qa_app_q3_server_network_options options;
    memset(&options, 0, sizeof options);
    options.transport = &q3_transport_view;
    options.host.ctx = h; options.host.product = &h->q3_product; options.host.max_clients = 8;
    options.host.status = q3_status_text; options.host.print = q3_print; options.host.time = q3_time;
    options.host.occupied_slots = q3_occupied;
    options.host.prepare = q3_prepare; options.host.downloads_enabled = q3_downloads;
    options.random = q3_random; options.random_ctx = h;
    h->q3 = qa_app_q3_server_network_create(options);
    CHECK(h->q3 != NULL);
  }
}
static void host_pump(local_host *h, const char *protocol, double now) {
  if (strcmp(protocol, "q3") == 0) {
    size_t count = 0;
    (void)qa_app_q3_server_network_poll(h->q3, now, &count);
    return;
  }
  qa_receive_event event;
  memset(&event, 0, sizeof event);
  while (qa_udp_transport_poll(&h->transport, &event)) {
    if (event.kind == QA_RECV_PACKET) {
      if (strcmp(protocol, "q1") == 0) {
        size_t length = 0;
        uint8_t *reply = qa_answer_netquake_control(event.payload, event.length, &event.from, now, &h->q1, &length);
        if (reply != NULL) { (void)qa_udp_transport_send(&h->transport, &event.from, reply, length); free(reply); }
      } else (void)qa_q2_connectionless_server_receive(&h->q2, &event.from, event.payload, event.length, now);
    }
    qa_receive_event_destroy(&event);
    memset(&event, 0, sizeof event);
  }
}
static void host_stop(local_host *h, const char *protocol) {
  if (strcmp(protocol, "q3") == 0) { qa_app_q3_server_network_close(h->q3); qa_app_q3_server_network_destroy(h->q3); }
  if (strcmp(protocol, "q2") == 0) qa_q2_challenge_table_destroy(&h->q2_challenges);
  qa_udp_transport_destroy(&h->transport);
}
static void test_real_local_hosts(void) {
  static const char *const families[3] = {"q1", "q2", "q3"};
  static const char *const maps[3] = {"e1m1", "base1", "q3dm1"};
  static const char *const kinds[3] = {"q1-netquake", "q2-classic", "q3"};
  static const int64_t versions[3] = {15, 34, 68};
  for (int f = 0; f < 3; f++) {
    char *root = temp_root("quake-browser-");
    qa_config_store config = config_at(root);
    local_host *host = calloc(1, sizeof *host);
    host_start(host, families[f]);
    qa_app_startup_server_browser *browser = open_browser(&config);
    qa_app_startup_server_browser_choose(browser, families[f]);
    char address[64];
    snprintf(address, sizeof address, "localhost:%lld", (long long)udp_port(&host->transport));
    qa_app_startup_server_browser_set_address(browser, address);
    call(browser, OP_QUERY);
    for (int i = 0; i < 30 && !(row_at(browser, 0) != NULL && row_at(browser, 0)->has_status); i++) {
      host_pump(host, families[f], qa_net_default_now(NULL));
      sleep_ms(2);
      call(browser, OP_POLL);
    }
    const qa_browser_entry *row = row_at(browser, 0);
    CHECK(row != NULL && row->has_status && strcmp(row->status.map, maps[f]) == 0);
    CHECK(row != NULL && row->has_ping);
    CHECK(row != NULL && row->has_status && !row->status.wire.is_unified && strcmp(row->status.wire.protocol.kind, kinds[f]) == 0 &&
          row->status.wire.protocol.version == versions[f]);
    CHECK(host->status_calls >= 1);
    qa_app_startup_server_browser_set_filter(browser, "absent-name");
    CHECK(row_count(browser) == 0);
    qa_app_startup_server_browser_set_filter(browser, "");
    call(browser, OP_FAVORITE);
    qa_app_startup_server_browser_set_address(browser, "[::1]:26000");
    call_raises(browser, OP_QUERY, "Address family");
    qa_app_startup_server_browser *restored = open_browser(&config);
    qa_app_startup_server_browser_choose(restored, families[f]);
    const qa_browser_entry *favorite = row_at(restored, 0);
    bool has_favorite = false;
    for (size_t i = 0; favorite != NULL && i < favorite->source_count; i++) if (strcmp(favorite->sources[i], "favorite") == 0) has_favorite = true;
    CHECK(has_favorite);
    qa_net_address expected = ipv4(127, 0, 0, 1, udp_port(&host->transport));
    CHECK(favorite != NULL && qa_net_same_address(&favorite->address, &expected, true));
    qa_net_address_destroy(&expected);
    qa_app_startup_server_browser_destroy(restored);
    if (f == 0) {
      CHECK(qa_config_store_dump(&config, "servers-q1", "[{\"kind\":\"loopback\",\"id\":\"invalid-favorite\"}]"));
      qa_app_startup_server_browser *tolerant = open_browser(&config);
      qa_app_startup_server_browser_choose(tolerant, "q1");
      size_t count = 0;
      const qa_browser_entry **rows = qa_app_startup_server_browser_rows(tolerant, &count);
      size_t favorites = 0;
      for (size_t i = 0; i < count; i++) for (size_t j = 0; j < rows[i]->source_count; j++) if (strcmp(rows[i]->sources[j], "favorite") == 0) favorites++;
      CHECK(favorites == 0);
      CHECK(strstr(qa_app_startup_server_browser_status(tolerant), "Ignored invalid saved server data") != NULL);
      CHECK(strstr(qa_app_startup_server_browser_status(tolerant), "requires an IP address") != NULL);
      qa_app_startup_server_browser_destroy(tolerant);
    }
    qa_app_startup_server_browser_destroy(browser);
    host_stop(host, families[f]);
    free(host);
    qa_config_store_destroy(&config);
    remove_tree(root);
    free(root);
  }
}

/* ================= Q3BrowserView over the browser's Q3BrowserHost ================= */
typedef struct view_fixture { sentinel s; int now_calls, print_calls, write_calls; char printed[256]; } view_fixture;
static double vf_now(void *ctx) { ((view_fixture *)fix(ctx))->now_calls++; return qa_net_default_now(NULL); }
static double vf_max_ping(void *ctx) { (void)fix(ctx); return 800; }
static double vf_resend(void *ctx) { (void)fix(ctx); return 750; }
static void vf_print(void *ctx, const char *text) {
  view_fixture *f = (view_fixture *)fix(ctx);
  f->print_calls++;
  snprintf(f->printed, sizeof f->printed, "%s", text);
}
static void vf_write(void *ctx, const char *text) { ((view_fixture *)fix(ctx))->write_calls++; (void)text; }
static const char *vf_name(void *ctx) { (void)fix(ctx); return "Added row"; }
typedef struct vf_address { sentinel s; char text[64]; } vf_address;
static const char *vf_address_text(void *ctx) { return ((vf_address *)fix(ctx))->text; }
typedef struct view_op { qa_q3_browser_view *v; int kind; const char *text; vf_address *address; view_fixture *f; } view_op;
static void view_body(void *ctx) {
  view_op *o = ctx;
  switch (o->kind) {
    case 0: qa_q3_browser_view_ping(o->v, o->text); break;
    case 1: (void)qa_q3_browser_view_add_server(o->v, 3, vf_name, o->f, vf_address_text, o->address); break;
    case 2: qa_q3_browser_view_poll(o->v); break;
    case 3: qa_q3_browser_view_local_servers(o->v); break;
    case 4: qa_q3_browser_view_save_servers_to_cache(o->v); break;
    case 5: qa_q3_browser_view_load_cached_servers(o->v); break;
    default: break;
  }
}
static void view_run(qa_q3_browser_view *v, int kind, const char *text, vf_address *address, view_fixture *f) {
  view_op o = {v, kind, text, address, f};
  qa_caught_error caught;
  if (!qa_try(view_body, &o, &caught)) { failures++; fprintf(stderr, "FAIL view op %d raised %s\n", kind, caught.message); }
  checks++;
}
static void test_q3_view_adapter(void) {
  char *root = temp_root("quake-browser-view-");
  qa_config_store config = config_at(root);
  qa_app_startup_server_browser *browser = open_browser(&config);
  qa_app_startup_q3_browser_host *adapter = qa_app_startup_server_browser_q3_host_create(browser);
  view_fixture f;
  memset(&f, 0, sizeof f);
  f.s.magic = MAGIC;
  qa_q3_browser_view_options options;
  memset(&options, 0, sizeof options);
  options.browser = qa_app_startup_q3_browser_host_view(adapter);
  options.now = vf_now; options.max_ping = vf_max_ping; options.status_resend_time = vf_resend;
  options.print = vf_print; options.print_ctx = &f; options.ctx = &f;
  qa_q3_browser_view *view = NULL;
  CHECK(qa_q3_browser_view_init(&view, &options));
  qa_udp_transport peer;
  CHECK(udp_bind_local(&peer));
  char remote[64];
  snprintf(remote, sizeof remote, "127.0.0.1:%lld", (long long)udp_port(&peer));
  view_run(view, 0, remote, NULL, &f);
  qa_receive_event query;
  CHECK(peer_receive(&peer, &query, 200));
  char *challenge = q3_challenge_of(query.payload, query.length);
  char info[256];
  snprintf(info, sizeof info, "\\protocol\\68\\hostname\\View host\\mapname\\q3dm2\\clients\\1\\sv_maxclients\\4\\challenge\\%s", challenge ? challenge : "");
  uint8_t *bytes = NULL; size_t length = 0;
  CHECK(qa_q3_encode_info(info, &bytes, &length));
  CHECK(qa_udp_transport_send(&peer, &query.from, bytes, length));
  free(bytes); free(challenge);
  qa_receive_event_destroy(&query);
  int64_t time = 0;
  for (int i = 0; i < 100 && time == 0; i++) {
    sleep_ms(2);
    call(browser, OP_POLL);
    view_run(view, 2, NULL, NULL, &f);
    char *address = NULL;
    if (qa_q3_browser_view_get_ping(view, 0, 64, vf_write, &f, &address, &time)) free(address);
  }
  CHECK(time > 0);
  vf_address added = {{MAGIC, 0, 0, ""}, ""};
  snprintf(added.text, sizeof added.text, "127.0.0.1:27965");
  view_run(view, 1, NULL, &added, &f);
  CHECK(qa_q3_browser_view_get_server_count(view, 3) == 1);
  view_run(view, 3, NULL, NULL, &f);
  view_run(view, 4, NULL, NULL, &f);
  view_run(view, 5, NULL, NULL, &f);
  CHECK(f.now_calls > 0 && f.write_calls > 0);
  qa_q3_browser_view_close(view);
  qa_q3_browser_view_destroy(view);
  qa_app_startup_server_browser_q3_host_destroy(adapter);
  qa_app_startup_server_browser_destroy(browser);
  qa_udp_transport_destroy(&peer);
  qa_config_store_destroy(&config);
  remove_tree(root);
  free(root);
}

/* ================= remote input: mutation pass over every parsed packet kind ================= */
static uint32_t rng_state = 0x5eedb10cu;
static uint32_t rng(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }
static uint8_t *mutate(const uint8_t *base, size_t length, size_t *out_length) {
  uint8_t *work = malloc(length + 96);
  memcpy(work, base, length);
  size_t n = length;
  uint32_t mode = rng() % 4;
  if (mode == 0) n = rng() % (length + 1);
  else if (mode < 3) {
    int edits = 1 + (int)(rng() % 6);
    static const uint8_t picks[] = {0, '\\', '\n', '"', ' ', 255, '%', 0x80, 0xc3, 92, 69, 0, 0xff, '9'};
    for (int e = 0; e < edits && length > 0; e++) {
      size_t at = rng() % length;
      work[at] = (rng() & 1) ? (uint8_t)rng() : picks[rng() % sizeof picks];
    }
    if (mode == 2) { size_t extra = rng() % 96; for (size_t i = 0; i < extra; i++) work[length + i] = (uint8_t)rng(); n = length + extra; }
  } else {
    /* splice a run of 0xff / backslashes over the middle */
    size_t at = length ? rng() % length : 0;
    for (size_t i = at; i < length && i < at + 8; i++) work[i] = (rng() & 1) ? 255 : 92;
  }
  *out_length = n;
  return work;
}
static uint8_t *mutate_header(const uint8_t *base, size_t length, size_t header, size_t *out_length) {
  uint8_t *work = malloc(length + 1);
  memcpy(work, base, length);
  size_t n = length;
  if (rng() & 1) n = rng() % (length + 1);
  else for (int e = 0; e < 1 + (int)(rng() % 3); e++) work[rng() % header] = (rng() & 1) ? (uint8_t)rng() : 92;
  *out_length = n;
  return work;
}
typedef struct seed { uint8_t *bytes; size_t length; } seed;
static void q2_master_decode_body(void *ctx) {
  seed *s = ctx;
  size_t count = 0;
  qa_net_address *addresses = qa_q2_read_master_reply(s->bytes, s->length, &count);
  if (addresses != NULL) qa_q2_master_reply_free(addresses, count);
}
static void q3_master_decode_body(void *ctx) {
  seed *s = ctx;
  qa_net_address *addresses = NULL; size_t count = 0; bool complete = false;
  if (qa_q3_decode_master_packet(s->bytes, s->length, &addresses, &count, &complete)) {
    CHECK(count <= 256);
    qa_q3_master_addresses_free(addresses, count);
  }
}
static void kex_decode_body(void *ctx) {
  seed *s = ctx;
  qa_server_status status;
  if (qa_kex_read_discovery(s->bytes, s->length, &status)) qa_server_status_destroy(&status);
}
static void test_remote_mutations(void) {
  char *root = temp_root("quake-browser-fuzz-");
  qa_config_store config = config_at(root);
  qa_app_startup_server_browser *b = open_browser(&config);
  qa_udp_transport peer;
  CHECK(udp_bind_local(&peer));
  qa_net_address to_browser = browser_endpoint(b);
  char *peer_text = address_text(&peer.address);
  seed seeds[9];
  size_t seed_count = 0, master_q2 = 99, master_q3 = 99;
  server_row row = SERVERS[2];
  /* q1 server-info, qw status, q2 print status, q2 master reply, q3 status/info, q3 master packet */
  seeds[seed_count].bytes = status_reply("q1", &row, &peer.address, NULL, 0, &seeds[seed_count].length); seed_count++;
  seeds[seed_count].bytes = qa_quakeworld_out_of_band("n\\hostname\\QW Arena\\map\\dm2\\maxclients\\16\\*gamedir\\qw\n1 12 3 45 \"Player one\" \"base\" 4 13\n",
                                                      false, &seeds[seed_count].length); seed_count++;
  seeds[seed_count].bytes = status_reply("q2", &row, &peer.address, NULL, 0, &seeds[seed_count].length); seed_count++;
  {
    uint8_t *reply = malloc(4 + 8 + 12);
    memset(reply, 255, 4); memcpy(reply + 4, "servers ", 8);
    uint8_t records[12] = {127, 0, 0, 1, 0x6d, 0x38, 10, 0, 0, 2, 0, 0};
    memcpy(reply + 12, records, 12);
    master_q2 = seed_count;
    seeds[seed_count].bytes = reply; seeds[seed_count].length = 24; seed_count++;
  }
  {
    qa_q3_status_player players[2] = {{5, 40, "Player One"}, {-2, 999, "x"}};
    seeds[seed_count].bytes = q3_status_packet("\\sv_hostname\\Test Host\\mapname\\q3dm17\\sv_maxclients\\16\\challenge\\1", players, 2,
                                               &seeds[seed_count].length);
    seed_count++;
    CHECK(qa_q3_encode_info("\\protocol\\68\\hostname\\Box\\mapname\\q3tourney2\\clients\\3\\sv_maxclients\\8\\challenge\\2",
                            &seeds[seed_count].bytes, &seeds[seed_count].length));
    seed_count++;
  }
  {
    qa_net_address list[3] = {ipv4(127, 0, 0, 1, 20001), ipv4(127, 0, 0, 1, 0), ipv4(127, 0, 0, 2, 27960)};
    master_q3 = seed_count;
    seeds[seed_count].bytes = master_packet(list, 3, false, &seeds[seed_count].length);
    seed_count++;
    for (int i = 0; i < 3; i++) qa_net_address_destroy(&list[i]);
  }
  /* KEX lobby status (decoder only: the browser reaches it for mDNS-found endpoints). */
  {
    seed kex;
    static const uint8_t lobby[] = {4, 'K', 'e', 'x', '1', 2, 8, 3, 'm', 'a', 'p', 5, 'b', 'a', 's', 'e', '1'};
    kex.bytes = malloc(sizeof lobby); memcpy(kex.bytes, lobby, sizeof lobby); kex.length = sizeof lobby;
    qa_caught_error caught;
    for (int round = 0; round < 3000; round++) {
      seed mutated;
      mutated.bytes = mutate(kex.bytes, kex.length, &mutated.length);
      (void)qa_try(kex_decode_body, &mutated, &caught);
      free(mutated.bytes);
    }
    free(kex.bytes);
  }
  for (int round = 0; round < 3000; round++) {
    size_t which = (round & 1) ? master_q3 : master_q2;
    seed mutated;
    mutated.bytes = mutate(seeds[which].bytes, seeds[which].length, &mutated.length);
    qa_caught_error caught;
    (void)qa_try(which == master_q2 ? q2_master_decode_body : q3_master_decode_body, &mutated, &caught);
    free(mutated.bytes);
  }
  /* Both masters point at the peer so its packets reach the Q2 and Q3 master decoders. */
  qa_app_startup_server_browser_choose(b, "q2");
  qa_app_startup_server_browser_set_master_address(b, peer_text);
  int rounds = 0, raised = 0;
  double started = qa_net_default_now(NULL);
  for (int batch = 0; batch < 120; batch++) {
    if (batch % 20 == 0) {
      call(b, OP_DISCOVER);
      request_master(b, 2, peer_text, 68, NULL, 0);
      /* Direct queries to the peer let well-formed replies reach core.receive as well (no LAN broadcast). */
      static const char *const query_protocols[] = {"q1", "qw", "q3", "q2"};
      for (int i = 0; i < 4; i++) {
        qa_app_startup_server_browser_choose(b, query_protocols[i]);
        qa_app_startup_server_browser_set_address(b, peer_text);
        call(b, OP_QUERY);
      }
      qa_receive_event drained;
      while (peer_receive(&peer, &drained, 2)) qa_receive_event_destroy(&drained);
    }
    for (int i = 0; i < 24; i++) {
      size_t which = rng() % seed_count;
      seed *base = &seeds[which];
      size_t length = 0;
      /* Master replies sent to the browser keep their records (truncation or header edits only), so the
       * browser never queues status queries to arbitrary hosts; their full mutation runs on the decoders below. */
      uint8_t *bytes = which == master_q2 || which == master_q3 ? mutate_header(base->bytes, base->length, which == master_q2 ? 12 : 22, &length)
                                                                 : mutate(base->bytes, base->length, &length);
      (void)qa_udp_transport_send(&peer, &to_browser, bytes, length);
      free(bytes);
      rounds++;
    }
    sleep_ms(1);
    op o; memset(&o, 0, sizeof o); o.kind = OP_POLL; o.b = b;
    qa_caught_error caught;
    if (!qa_try(op_body, &o, &caught)) { raised++; fprintf(stderr, "poll raised: %s\n", caught.message); }
    /* The browser's status queries to mutated endpoints go nowhere; keep the peer drained. */
    qa_receive_event drained;
    while (peer_receive(&peer, &drained, 1)) qa_receive_event_destroy(&drained);
  }
  CHECK(raised == 0);
  CHECK(rounds == 120 * 24);
  /* The unmodified master packet with a port-0 record: TS addQ3Many skips it; nothing raises. */
  request_master(b, 2, peer_text, 68, NULL, 0);
  qa_receive_event drained;
  while (peer_receive(&peer, &drained, 2)) qa_receive_event_destroy(&drained);
  CHECK(qa_udp_transport_send(&peer, &to_browser, seeds[master_q3].bytes, seeds[master_q3].length));
  sleep_ms(3);
  call(b, OP_POLL);
  qa_app_q3_list masters = qa_app_startup_server_browser_q3_list(b, 2);
  bool zero = false, listed = false;
  for (size_t i = 0; i < masters.address_count; i++) {
    if (masters.addresses[i].port == 0) zero = true;
    if (masters.addresses[i].port == 20001) listed = true;
  }
  CHECK(listed && !zero && masters.address_count == 2);
  /* The browser is still usable after the hostile traffic. */
  size_t count = 0;
  (void)qa_app_startup_server_browser_rows(b, &count);
  qa_app_startup_server_browser_choose(b, "q3");
  (void)qa_app_startup_server_browser_q3_list(b, 2);
  fprintf(stderr, "mutation pass: %d datagrams in %.0f ms\n", rounds, qa_net_default_now(NULL) - started);
  for (size_t i = 0; i < seed_count; i++) free(seeds[i].bytes);
  free(peer_text);
  qa_net_address_destroy(&to_browser);
  qa_udp_transport_destroy(&peer);
  qa_app_startup_server_browser_destroy(b);
  qa_config_store_destroy(&config);
  remove_tree(root);
  free(root);
}

/* ================= master list body (server-master-list.ts) ================= */
static void count_skipped(void *ctx, double count) { fix(ctx)->calls += (int)count; }
typedef struct parse_op { const char *text; size_t length; sentinel *s; char **out; size_t count; } parse_op;
static void parse_body(void *ctx) {
  parse_op *o = ctx;
  o->out = qa_app_parse_server_master_list((const uint8_t *)o->text, o->length, count_skipped, o->s, &o->count);
}
static void test_master_list_body(void) {
  sentinel s = {MAGIC, 0, 0, ""};
  const char *text = "\xef\xbb\xbf# list\r\n  1.2.3.4:27500 \r\n1.2.3.4:27500\n\n#x\nbad host\n\xc2\xa0" "5.6.7.8\xc2\xa0\n";
  parse_op o = {text, strlen(text), &s, NULL, 0};
  qa_caught_error caught;
  CHECK(qa_try(parse_body, &o, &caught));
  CHECK(o.count == 2 && strcmp(o.out[0], "1.2.3.4:27500") == 0 && strcmp(o.out[1], "5.6.7.8") == 0);
  CHECK(s.calls == 1);
  qa_app_server_master_list_free(o.out, o.count);
  parse_op bad = {"\xff", 1, &s, NULL, 0};
  CHECK(!qa_try(parse_body, &bad, &caught) && strcmp(caught.name, "TypeError") == 0);
  parse_op overlong = {"\xc0\x80", 2, &s, NULL, 0};
  CHECK(!qa_try(parse_body, &overlong, &caught));
}

int main(void) {
  test_master_list_body();
  test_master_packet_eot_boundary();
  test_cache_master_capacity();
  test_invalid_saved_direct_list();
  test_q2_udp_master();
  test_http_quakeworld_list();
  test_http_list_skips_bad_lines();
  test_q3_list_reuses_scan();
  test_favorite_rollback_invalidates_snapshot();
  test_q3_facade_masters_and_cache();
  test_menu_persistence();
  test_direct_recall_and_failed_favorite();
  test_sort_and_filter();
  test_real_local_hosts();
  test_q3_view_adapter();
  test_remote_mutations();
  printf("test_server_browser: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
