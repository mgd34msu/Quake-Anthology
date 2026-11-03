/* Port of src/app/bootstrap/server-browser.ts. See the header for the async and
 * error conventions. GPL-2.0-or-later. */
#include "qa/app/bootstrap/server_browser.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "qa/app/bootstrap/server_master_list.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/network/common/js_number.h"
#include "qa/network/common/transport.h"
#include "qa/network/q1/discovery.h"
#include "qa/network/q2/connectionless.h"
#include "qa/network/q2/handshake.h"
#include "qa/network/q2/kex/discovery.h"
#include "qa/network/q3/discovery.h"

const qa_app_browser_sort_choice qa_app_browser_sort_orders[8] = {
    {"ping-low", "Ping: lowest first"}, {"ping-high", "Ping: highest first"},
    {"name-az", "Name: A to Z"}, {"name-za", "Name: Z to A"},
    {"map-az", "Map: A to Z"}, {"map-za", "Map: Z to A"},
    {"players-most", "Players: most first"}, {"players-fewest", "Players: fewest first"}};
/* const protocols = ["q1", "qw", "q2", "q3"]; ports = { q1: 26000, qw: 27500, q2: 27910, q3: 27960 } */
static const char *const PROTOCOLS[4] = {"q1", "qw", "q2", "q3"};
static const int64_t PORTS[4] = {26000, 27500, 27910, 27960};
enum { Q1 = 0, QW = 1, Q2 = 2, Q3 = 3 };
static int protocol_index(const char *protocol) {
  for (int i = 0; i < 4; i++) if (strcmp(protocol, PROTOCOLS[i]) == 0) return i;
  return -1;
}
/* q3Sources: { 0: "lan", 1: "secondary-master", 2: "master", 3: "favorite" } */
static const char *q3_source_name(int source) {
  return source == 0 ? "lan" : source == 1 ? "secondary-master" : source == 2 ? "master" : "favorite";
}
static int q3_source(double source) {
  if (source != 0 && source != 1 && source != 2 && source != 3) qa_raise("RangeError", 0, 0, "Invalid Q3 browser source");
  return (int)source;
}
static double now_ms(void) { return qa_net_default_now(NULL); }
static noreturn void browser_error(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static char *xdup(const char *s) {
  size_t n = strlen(s != NULL ? s : "");
  char *r = malloc(n + 1);
  if (!r) qa_range_error("Out of memory");
  memcpy(r, s != NULL ? s : "", n + 1);
  return r;
}
static void set_str(char **slot, const char *value) {
  char *copy = xdup(value);
  free(*slot);
  *slot = copy;
}
static char *xkey(const qa_net_address *address) {
  char *key = qa_net_address_key(address, true);
  if (!key) qa_range_error("Out of memory");
  return key;
}
static bool same_key(const qa_net_address *a, const qa_net_address *b) {
  char *left = xkey(a), *right = xkey(b);
  bool same = strcmp(left, right) == 0;
  free(left); free(right);
  return same;
}
/* UTF-16 code units of UTF-8 text (a 4-byte sequence is a surrogate pair). */
static size_t utf16_length(const char *text) {
  size_t units = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++)
    if ((*p & 0xc0) != 0x80) units += *p >= 0xf0 ? 2 : 1;
  return units;
}
/* text.slice(0, units) as bytes; a surrogate pair that would be split is dropped whole. */
static size_t utf16_prefix_bytes(const char *text, size_t units) {
  size_t used = 0, at = 0;
  const unsigned char *p = (const unsigned char *)text;
  while (p[at] != '\0') {
    size_t width = p[at] >= 0xf0 ? 4 : p[at] >= 0xe0 ? 3 : p[at] >= 0xc0 ? 2 : 1;
    size_t cost = width == 4 ? 2 : 1;
    if (used + cost > units) break;
    for (size_t i = 1; i < width; i++) if (p[at + i] == '\0') { width = i; break; }
    used += cost; at += width;
  }
  return at;
}
/* value.replace(/[\x00-\x1f\x7f]/g, " ") */
static char *clean_text(const char *value) {
  char *copy = xdup(value);
  for (char *p = copy; *p; p++) if ((unsigned char)*p < 0x20 || (unsigned char)*p == 0x7f) *p = ' ';
  return copy;
}
static char *trimmed(const char *text) {
  char *out = qa_app_js_trim(text != NULL ? text : "");
  if (!out) qa_range_error("Out of memory");
  return out;
}
/* Cleanup adapters (qa_cleanup takes void (*)(void *)). */
static void release_address(void *arg) { qa_net_address_destroy(arg); }
static void release_event(void *arg) { qa_receive_event_destroy(arg); }
static void release_view(void *arg) { qa_q3_browser_cache_view_destroy(arg); free(arg); }
static void release_wire_state(void *arg) { qa_q3_discovery_wire_state_destroy(arg); }
static bool has_source(const qa_browser_entry *entry, const char *source) {
  for (size_t i = 0; i < entry->source_count; i++) if (strcmp(entry->sources[i], source) == 0) return true;
  return false;
}

char *qa_app_browser_address(const qa_net_address *address) {
  if (address->kind != QA_NET_IPV4 && address->kind != QA_NET_IPV6) browser_error("Server requires an IP address");
  char *host = qa_net_address_host(address);
  if (!host) qa_range_error("Out of memory");
  size_t n = strlen(host) + 32;
  char *out = malloc(n);
  if (!out) { free(host); qa_range_error("Out of memory"); }
  if (address->kind == QA_NET_IPV6) snprintf(out, n, "[%s]:%lld", host, (long long)address->port);
  else snprintf(out, n, "%s:%lld", host, (long long)address->port);
  free(host);
  return out;
}
void qa_app_browser_connection_destroy(qa_app_browser_connection *connection) {
  free(connection->remote);
  memset(connection, 0, sizeof *connection);
}

typedef struct pending_status { char *key; int protocol; qa_net_address address; } pending_status;
typedef struct kex_query { char *key; double at; } kex_query;
typedef struct q3_snapshot { bool live; double generation; qa_net_address *addresses; size_t count; } q3_snapshot;
struct qa_app_startup_server_browser {
  qa_udp_transport transport;
  const qa_config_store *config;
  /* The four ServerBrowser cores and their wire closure state. */
  qa_server_browser *browsers[4];
  qa_discovery_wire q2_inner_wire;
  qa_q2_discovery_wire_state q2_wire_state;
  qa_q3_discovery_wire_state q3_wire_state;
  qa_app_direct_server_address *direct[4]; size_t direct_count[4]; bool has_direct[4];
  char *addresses[4];
  char *master_addresses[4];
  bool closing, closed;
  double generation;
  double list_generations[4];
  q3_snapshot list_snapshots[4];
  double list_resets[4];
  double master_epoch;
  bool has_master; int master_source; bool master_has_address; qa_net_address master_address;
  double master_started; bool master_received;
  qa_q3_browser_cache_view *cached_view;
  bool has_q2_master; qa_net_address q2_master_address; double q2_master_started;
  qa_kex_mdns kex; bool kex_open, kex_opening;
  kex_query *kex_queries; size_t kex_query_count, kex_query_cap;
  pending_status *pending; size_t pending_count, pending_cap;
  int protocol;
  char *filter;
  bool favorites_only;
  const char *sort_order;
  bool hide_empty, hide_full;
  char *selected;
  char *status;
  const qa_browser_entry **row_scratch;
  char *address_scratch;
};

/* ---- construction ---- */
static bool sender_send(void *ctx, const qa_net_address *to, const uint8_t *bytes, size_t length) {
  qa_app_startup_server_browser *b = ctx;
  return (to->kind == QA_NET_IPV4 || to->kind == QA_NET_IPV6) ? qa_udp_transport_send(&b->transport, to, bytes, length) : false;
}
static qa_q2_status q2_browser_status(void *ctx) {
  (void)ctx; /* () => ({ serverInfo: "", players: [] }) */
  qa_q2_status status = {"", NULL, 0};
  return status;
}
/* { ...wire, query: (_kind, challenge) => wire.query("status", challenge) } */
static uint8_t *q2_status_query(void *ctx, const char *kind, const char *challenge, size_t *out_len) {
  qa_app_startup_server_browser *b = ctx;
  (void)kind;
  return b->q2_inner_wire.query(b->q2_inner_wire.ctx, "status", challenge, out_len);
}
static uint8_t *q2_master_query(void *ctx, size_t *out_len) {
  qa_app_startup_server_browser *b = ctx;
  return b->q2_inner_wire.master_query(b->q2_inner_wire.ctx, out_len);
}
static uint8_t *q2_heartbeat(void *ctx, bool active, size_t *out_len) {
  qa_app_startup_server_browser *b = ctx;
  return b->q2_inner_wire.heartbeat(b->q2_inner_wire.ctx, active, out_len);
}
static qa_server_browser *browser_of(qa_app_startup_server_browser *b, int protocol) {
  if (protocol < 0 || protocol > 3 || b->browsers[protocol] == NULL) browser_error("Unsupported browser protocol");
  return b->browsers[protocol];
}

static void free_snapshot(q3_snapshot *snapshot) {
  for (size_t i = 0; i < snapshot->count; i++) qa_net_address_destroy(&snapshot->addresses[i]);
  free(snapshot->addresses);
  memset(snapshot, 0, sizeof *snapshot);
}
static void clear_pending(qa_app_startup_server_browser *b) {
  for (size_t i = 0; i < b->pending_count; i++) { free(b->pending[i].key); qa_net_address_destroy(&b->pending[i].address); }
  b->pending_count = 0;
}
static void clear_kex_queries(qa_app_startup_server_browser *b) {
  for (size_t i = 0; i < b->kex_query_count; i++) free(b->kex_queries[i].key);
  b->kex_query_count = 0;
}
static void clear_master(qa_app_startup_server_browser *b) {
  if (b->has_master && b->master_has_address) qa_net_address_destroy(&b->master_address);
  memset(&b->master_address, 0, sizeof b->master_address);
  b->has_master = false; b->master_has_address = false; b->master_received = false;
}
static void clear_q2_master(qa_app_startup_server_browser *b) {
  if (b->has_q2_master) qa_net_address_destroy(&b->q2_master_address);
  memset(&b->q2_master_address, 0, sizeof b->q2_master_address);
  b->has_q2_master = false;
}
static void set_cached_view(qa_app_startup_server_browser *b, qa_q3_browser_cache_view *view) {
  if (b->cached_view) { qa_q3_browser_cache_view_destroy(b->cached_view); free(b->cached_view); }
  b->cached_view = view;
}
static void free_browser(qa_app_startup_server_browser *b) {
  for (int i = 0; i < 4; i++) {
    if (b->browsers[i] != NULL) qa_server_browser_free(b->browsers[i]);
    qa_app_direct_servers_free(b->direct[i], b->direct_count[i]);
    free(b->addresses[i]); free(b->master_addresses[i]);
    free_snapshot(&b->list_snapshots[i]);
  }
  qa_q3_discovery_wire_state_destroy(&b->q3_wire_state);
  clear_pending(b); free(b->pending);
  clear_kex_queries(b); free(b->kex_queries);
  set_cached_view(b, NULL);
  clear_master(b);
  clear_q2_master(b);
  qa_udp_transport_destroy(&b->transport);
  free(b->filter); free(b->selected); free(b->status); free(b->row_scratch); free(b->address_scratch);
  free(b);
}

/* JSON.parse(text) !== undefined without throwing. */
static bool is_json_text(const char *text) {
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, strlen(text), true);
  if (value == NULL) return false;
  bool ok = value->kind != QA_SAVE_UNDEFINED;
  qa_save_value_free(value);
  return ok;
}
typedef struct skipped_list { char **items; size_t count; } skipped_list;
static void skipped_push(skipped_list *list, const char *name, const char *message) {
  char **grown = realloc(list->items, (list->count + 1) * sizeof *grown);
  if (!grown) qa_range_error("Out of memory");
  list->items = grown;
  size_t n = strlen(name) + strlen(message) + 3;
  char *line = malloc(n);
  if (!line) qa_range_error("Out of memory");
  snprintf(line, n, "%s: %s", name, message);
  list->items[list->count++] = line;
}
typedef struct favorites_load { qa_app_startup_server_browser *b; int protocol; const char *saved; } favorites_load;
/* for (const address of core.favoriteAddresses()) browserAddress(address); */
static void validate_favorites_body(void *ctx) {
  favorites_load *load = ctx;
  qa_server_browser *core = browser_of(load->b, load->protocol);
  size_t count = 0;
  qa_net_address *favorites = qa_server_browser_favorite_addresses(core, &count);
  bool usable = true;
  for (size_t i = 0; i < count; i++) {
    if (favorites[i].kind != QA_NET_IPV4 && favorites[i].kind != QA_NET_IPV6) usable = false;
    qa_net_address_destroy(&favorites[i]);
  }
  free(favorites);
  if (!usable) browser_error("Server requires an IP address");
}
static void load_favorites_body(void *ctx) {
  favorites_load *load = ctx;
  if (utf16_length(load->saved) > 65536) browser_error("Saved server list is too large");
  qa_server_browser *core = browser_of(load->b, load->protocol);
  qa_server_browser_restore_favorites(core, load->saved);
  qa_caught_error caught;
  if (!qa_try(validate_favorites_body, load, &caught)) {
    /* Do not keep a half-valid list: an address the browser cannot use would fail every later row read. */
    size_t count = 0;
    qa_net_address *favorites = qa_server_browser_favorite_addresses(core, &count);
    for (size_t i = 0; i < count; i++) { qa_server_browser_remove_favorite(core, &favorites[i]); qa_net_address_destroy(&favorites[i]); }
    free(favorites);
    qa_rethrow(&caught);
  }
}
static void load_direct_body(void *ctx) {
  favorites_load *load = ctx;
  qa_app_startup_server_browser *b = load->b;
  size_t count = 0;
  qa_app_direct_server_address *servers = qa_app_read_direct_servers(load->saved, &count);
  qa_app_direct_servers_free(b->direct[load->protocol], b->direct_count[load->protocol]);
  b->direct[load->protocol] = servers;
  b->direct_count[load->protocol] = count;
  b->has_direct[load->protocol] = true;
  for (size_t i = 0; i < count; i++) (void)qa_server_browser_add(browser_of(b, load->protocol), &servers[i].address, "direct", 0);
  if (count > 0) set_str(&b->addresses[load->protocol], servers[0].remote);
}
static void load_cache_body(void *ctx) {
  qa_q3_browser_cache_view *view = NULL;
  (void)qa_app_startup_server_browser_load_q3_cache(ctx, NULL, NULL, &view);
  if (view) { qa_q3_browser_cache_view_destroy(view); free(view); }
}
typedef struct open_scope { qa_app_startup_server_browser *b; skipped_list skipped; } open_scope;
static void open_scope_release(void *arg) {
  open_scope *scope = arg;
  for (size_t i = 0; i < scope->skipped.count; i++) free(scope->skipped.items[i]);
  free(scope->skipped.items);
  scope->skipped.items = NULL; scope->skipped.count = 0;
}
static void browser_release(void *arg) {
  qa_app_startup_server_browser **slot = arg;
  if (*slot != NULL) free_browser(*slot);
  *slot = NULL;
}
qa_app_startup_server_browser *qa_app_startup_server_browser_open(const qa_config_store *config) {
  qa_app_startup_server_browser *b = calloc(1, sizeof *b);
  if (!b) return NULL;
  b->transport.fd = -1;
  qa_cleanup browser_guard;
  qa_cleanup_push(&browser_guard, browser_release, &b);
  /* UdpTransport.bind({ host: "0.0.0.0", port: 0, broadcast: true }) */
  qa_udp_bind_options bind = {"0.0.0.0", 0, NULL, NULL, NULL, true};
  qa_udp_transport bound;
  if (!qa_udp_transport_bind(&bound, &bind)) qa_range_error("Out of memory");
  b->transport = bound;
  b->config = config;
  b->protocol = Q1;
  b->sort_order = "ping-low";
  b->filter = xdup("");
  b->status = xdup("");
  qa_packet_sender sender = {b, sender_send};
  qa_q2_protocol_identity q2_classic;
  qa_q2_get_protocol(34, 0, &q2_classic);
  qa_q2_discovery_wire_init(&b->q2_inner_wire, &b->q2_wire_state, q2_classic, q2_browser_status, b);
  qa_discovery_wire q2_wire = {b, q2_status_query, q2_master_query, q2_heartbeat};
  qa_discovery_wire q3_wire;
  if (!qa_q3_discovery_wire_init(&q3_wire, &b->q3_wire_state, NULL, 0, 68)) qa_range_error("Out of memory");
  qa_discovery_wire wires[4] = {qa_netquake_discovery_wire(), qa_quakeworld_discovery_wire(), q2_wire, q3_wire};
  for (int i = 0; i < 4; i++) {
    b->browsers[i] = qa_server_browser_create(wires[i], sender);
    if (!b->browsers[i]) qa_range_error("Out of memory");
  }
  /* Text that is not JSON at all behaves like the absent saved list; parsed lists
   * still face their validation. An optional list or cache that fails validation is
   * skipped and reported; it must not stop the launch. */
  open_scope scope = {b, {NULL, 0}};
  qa_cleanup scope_guard;
  qa_cleanup_push(&scope_guard, open_scope_release, &scope);
  char name[64];
  for (int i = 0; i < 4; i++) {
    snprintf(name, sizeof name, "servers-%s", PROTOCOLS[i]);
    size_t length = 0;
    char *saved = qa_config_store_load_text(config, name, &length);
    if (saved != NULL && is_json_text(saved)) {
      favorites_load load = {b, i, saved};
      qa_caught_error caught;
      if (!qa_try(load_favorites_body, &load, &caught)) skipped_push(&scope.skipped, name, caught.message);
    }
    free(saved);
  }
  for (int i = 0; i < 4; i++) {
    size_t length = 0;
    snprintf(name, sizeof name, "servers-master-%s", PROTOCOLS[i]);
    char *master = qa_config_store_load_text(config, name, &length);
    if (master != NULL) {
      char *text = trimmed(master);
      free(master);
      if (utf16_length(text) <= 2048) { free(b->master_addresses[i]); b->master_addresses[i] = text; }
      else free(text);
    }
    snprintf(name, sizeof name, "servers-direct-%s", PROTOCOLS[i]);
    char *saved = qa_config_store_load_text(config, name, &length);
    if (saved == NULL || !is_json_text(saved)) { free(saved); continue; }
    favorites_load load = {b, i, saved};
    qa_caught_error caught;
    if (!qa_try(load_direct_body, &load, &caught)) skipped_push(&scope.skipped, name, caught.message);
    free(saved);
  }
  qa_caught_error caught;
  if (!qa_try(load_cache_body, b, &caught)) skipped_push(&scope.skipped, "servers-cache-q3", caught.message);
  if (scope.skipped.count != 0) {
    size_t n = strlen("Ignored invalid saved server data ()") + 1;
    for (size_t i = 0; i < scope.skipped.count; i++) n += strlen(scope.skipped.items[i]) + 2;
    char *text = malloc(n);
    if (!text) qa_range_error("Out of memory");
    size_t at = (size_t)snprintf(text, n, "Ignored invalid saved server data (");
    for (size_t i = 0; i < scope.skipped.count; i++)
      at += (size_t)snprintf(text + at, n - at, "%s%s", i ? "; " : "", scope.skipped.items[i]);
    snprintf(text + at, n - at, ")");
    free(b->status);
    b->status = text;
  }
  qa_cleanup_pop(&scope_guard);
  open_scope_release(&scope);
  qa_cleanup_pop(&browser_guard);
  return b;
}
void qa_app_startup_server_browser_destroy(qa_app_startup_server_browser *b) {
  if (!b) return;
  qa_app_startup_server_browser_close(b);
  free_browser(b);
}

/* ---- simple state ---- */
const char *qa_app_startup_server_browser_protocol(const qa_app_startup_server_browser *b) { return PROTOCOLS[b->protocol]; }
const char *qa_app_startup_server_browser_master_address(const qa_app_startup_server_browser *b) {
  const char *value = b->master_addresses[b->protocol];
  return value ? value : "";
}
void qa_app_startup_server_browser_set_master_address(qa_app_startup_server_browser *b, const char *value) {
  set_str(&b->master_addresses[b->protocol], value);
}
const char *qa_app_startup_server_browser_address(qa_app_startup_server_browser *b) {
  const char *stored = b->addresses[b->protocol];
  if (stored) return stored;
  char buf[64];
  snprintf(buf, sizeof buf, "localhost:%lld", (long long)PORTS[b->protocol]);
  set_str(&b->address_scratch, buf);
  return b->address_scratch;
}
void qa_app_startup_server_browser_set_address(qa_app_startup_server_browser *b, const char *value) {
  set_str(&b->addresses[b->protocol], value);
}
const char *qa_app_startup_server_browser_filter(const qa_app_startup_server_browser *b) { return b->filter; }
void qa_app_startup_server_browser_set_filter(qa_app_startup_server_browser *b, const char *value) { set_str(&b->filter, value); }
bool qa_app_startup_server_browser_favorites_only(const qa_app_startup_server_browser *b) { return b->favorites_only; }
void qa_app_startup_server_browser_set_favorites_only(qa_app_startup_server_browser *b, bool value) { b->favorites_only = value; }
const char *qa_app_startup_server_browser_sort_order(const qa_app_startup_server_browser *b) { return b->sort_order; }
bool qa_app_startup_server_browser_hide_empty(const qa_app_startup_server_browser *b) { return b->hide_empty; }
void qa_app_startup_server_browser_set_hide_empty(qa_app_startup_server_browser *b, bool value) { b->hide_empty = value; }
bool qa_app_startup_server_browser_hide_full(const qa_app_startup_server_browser *b) { return b->hide_full; }
void qa_app_startup_server_browser_set_hide_full(qa_app_startup_server_browser *b, bool value) { b->hide_full = value; }
const char *qa_app_startup_server_browser_selected(const qa_app_startup_server_browser *b) { return b->selected; }
const char *qa_app_startup_server_browser_status(const qa_app_startup_server_browser *b) { return b->status; }
void qa_app_startup_server_browser_set_status(qa_app_startup_server_browser *b, const char *value) { set_str(&b->status, value); }
bool qa_app_startup_server_browser_transport_address(const qa_app_startup_server_browser *b, qa_net_address *out) {
  return qa_udp_transport_get_address(&b->transport, out) != 0;
}
void qa_app_startup_server_browser_assert_open(qa_app_startup_server_browser *b) {
  if (b->closing || b->closed) browser_error("Server browser is closed");
}
static void assert_generation(qa_app_startup_server_browser *b, double generation, bool draining) {
  if (b->closed || (!draining && b->closing) || generation != b->generation) browser_error("Server browser operation was retired");
}
qa_server_browser *qa_app_startup_server_browser_q3_core(qa_app_startup_server_browser *b) {
  qa_app_startup_server_browser_assert_open(b);
  return browser_of(b, Q3);
}

/* ---- Q3 list membership ---- */
qa_app_q3_list qa_app_startup_server_browser_q3_list(qa_app_startup_server_browser *b, double source_value) {
  qa_app_startup_server_browser_assert_open(b);
  int source = q3_source(source_value);
  double generation = b->list_generations[source];
  q3_snapshot *snapshot = &b->list_snapshots[source];
  if (!snapshot->live || snapshot->generation != generation) {
    qa_server_browser *core = browser_of(b, Q3);
    size_t count = qa_server_browser_size(core);
    qa_net_address *addresses = calloc(count ? count : 1, sizeof *addresses);
    if (!addresses) qa_range_error("Out of memory");
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
      const qa_browser_entry *entry = qa_server_browser_entry_at(core, i);
      if (!has_source(entry, q3_source_name(source))) continue;
      if (!qa_net_address_clone(&entry->address, &addresses[n])) {
        for (size_t j = 0; j < n; j++) qa_net_address_destroy(&addresses[j]);
        free(addresses);
        qa_range_error("Out of memory");
      }
      n++;
    }
    free_snapshot(snapshot);
    snapshot->live = true;
    snapshot->generation = generation;
    snapshot->addresses = addresses;
    snapshot->count = n;
  }
  qa_app_q3_list out;
  out.addresses = snapshot->addresses;
  out.address_count = snapshot->count;
  out.pending = b->has_master && b->master_source == source && !b->master_received;
  out.generation = generation;
  out.reset_generation = b->list_resets[source];
  return out;
}
/* The core's addresses as owned copies, for passes that change membership. */
static qa_net_address *core_addresses(qa_server_browser *core, size_t *out_count) {
  size_t count = qa_server_browser_size(core);
  qa_net_address *addresses = calloc(count ? count : 1, sizeof *addresses);
  if (!addresses) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++)
    if (!qa_net_address_clone(&qa_server_browser_entry_at(core, i)->address, &addresses[i])) {
      for (size_t j = 0; j < i; j++) qa_net_address_destroy(&addresses[j]);
      free(addresses);
      qa_range_error("Out of memory");
    }
  *out_count = count;
  return addresses;
}
static void free_addresses(qa_net_address *addresses, size_t count) {
  for (size_t i = 0; i < count; i++) qa_net_address_destroy(&addresses[i]);
  free(addresses);
}
static void clear_q3(qa_app_startup_server_browser *b, int source) {
  qa_server_browser *core = browser_of(b, Q3);
  size_t count = 0;
  qa_net_address *addresses = core_addresses(core, &count);
  for (size_t i = 0; i < count; i++) qa_server_browser_remove_source(core, &addresses[i], q3_source_name(source));
  free_addresses(addresses, count);
  b->list_generations[source]++;
  b->list_resets[source]++;
}

/* resolveAddress(text, port, 4) under qa_try. */
typedef struct resolve_scope { const char *text; int64_t port; bool direct_text; qa_net_address address; bool resolved; } resolve_scope;
static void resolve_body(void *ctx) {
  resolve_scope *scope = ctx;
  const char *text = scope->direct_text ? qa_app_direct_server_text(scope->text) : scope->text;
  if (!qa_net_resolve_address(&scope->address, text, scope->port, 4)) qa_range_error("Out of memory");
  scope->resolved = true;
}
bool qa_app_startup_server_browser_resolve_q3(qa_app_startup_server_browser *b, const char *text, qa_net_address *out) {
  qa_app_startup_server_browser_assert_open(b);
  double generation = b->generation;
  resolve_scope scope = {text, 27960, true, {0}, false};
  qa_caught_error caught;
  if (!qa_try(resolve_body, &scope, &caught)) {
    assert_generation(b, generation, false);
    return false;
  }
  qa_net_address address = scope.address;
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_address, &address);
  assert_generation(b, generation, false);
  qa_cleanup_pop(&guard);
  bool broadcast = address.kind == QA_NET_IPV4 && address.ipv4[0] == 255 && address.ipv4[1] == 255 && address.ipv4[2] == 255 && address.ipv4[3] == 255;
  if (address.kind != QA_NET_IPV4 || address.port == 0 || broadcast) { qa_net_address_destroy(&address); return false; }
  *out = address;
  return true;
}
void qa_app_startup_server_browser_add_q3(qa_app_startup_server_browser *b, double source_value, const qa_net_address *address) {
  qa_app_startup_server_browser_assert_open(b);
  int source = q3_source(source_value);
  if (address->kind != QA_NET_IPV4 || address->port == 0) browser_error("Q3 browser requires an IPv4 endpoint");
  qa_server_browser *core = browser_of(b, Q3);
  char *key = xkey(address);
  size_t entries = 0;
  bool known = false;
  for (size_t i = 0; i < qa_server_browser_size(core); i++) {
    const qa_browser_entry *entry = qa_server_browser_entry_at(core, i);
    if (!has_source(entry, q3_source_name(source))) continue;
    entries++;
    char *entry_key = xkey(&entry->address);
    if (strcmp(key, entry_key) == 0) known = true;
    free(entry_key);
  }
  free(key);
  if (known) return;
  if (entries >= (source == 2 ? 8192u : 128u)) browser_error("Q3 browser list is full");
  (void)qa_server_browser_add(core, address, q3_source_name(source), 0);
  b->list_generations[source]++;
}
/* addQ3Many: a master packet's addresses in one pass, up to the source's capacity;
 * returns those newly added (owned copies). */
static qa_net_address *add_q3_many(qa_app_startup_server_browser *b, int source, const qa_net_address *addresses, size_t count,
                                   size_t *out_count) {
  qa_app_startup_server_browser_assert_open(b);
  qa_server_browser *core = browser_of(b, Q3);
  size_t known_count = 0, known_cap = qa_server_browser_size(core) + count + 1;
  char **known = calloc(known_cap, sizeof *known);
  qa_net_address *added = calloc(count ? count : 1, sizeof *added);
  if (!known || !added) { free(known); free(added); qa_range_error("Out of memory"); }
  for (size_t i = 0; i < qa_server_browser_size(core); i++) {
    const qa_browser_entry *entry = qa_server_browser_entry_at(core, i);
    if (has_source(entry, q3_source_name(source))) known[known_count++] = xkey(&entry->address);
  }
  size_t limit = source == 2 ? 8192 : 128, n = 0;
  for (size_t i = 0; i < count; i++) {
    if (known_count >= limit) break;
    if (addresses[i].kind != QA_NET_IPV4 || addresses[i].port == 0) continue;
    char *key = xkey(&addresses[i]);
    bool seen = false;
    for (size_t j = 0; j < known_count && !seen; j++) seen = strcmp(known[j], key) == 0;
    if (seen) { free(key); continue; }
    (void)qa_server_browser_add(core, &addresses[i], q3_source_name(source), 0);
    known[known_count++] = key;
    if (!qa_net_address_clone(&addresses[i], &added[n])) qa_range_error("Out of memory");
    n++;
  }
  for (size_t i = 0; i < known_count; i++) free(known[i]);
  free(known);
  if (n > 0) b->list_generations[source]++;
  *out_count = n;
  return added;
}
void qa_app_startup_server_browser_remove_q3(qa_app_startup_server_browser *b, double source_value, const qa_net_address *address) {
  qa_app_startup_server_browser_assert_open(b);
  int source = q3_source(source_value);
  qa_server_browser_remove_source(browser_of(b, Q3), address, q3_source_name(source));
  b->list_generations[source]++;
}
static void broadcast_to(qa_app_startup_server_browser *b, int protocol, const int64_t *ports, size_t count) {
  qa_net_address targets[4];
  int64_t host[4] = {255, 255, 255, 255};
  for (size_t i = 0; i < count; i++) (void)qa_net_make_ipv4(&targets[i], host, ports[i], false);
  qa_server_browser *core = browser_of(b, protocol);
  double now = now_ms();
  (void)qa_server_browser_broadcast(core, targets, count, now);
  for (size_t i = 0; i < count; i++) qa_net_address_destroy(&targets[i]);
}
void qa_app_startup_server_browser_scan_q3(qa_app_startup_server_browser *b) {
  qa_app_startup_server_browser_assert_open(b);
  clear_q3(b, 0);
  int64_t ports[4] = {27960, 27961, 27962, 27963};
  broadcast_to(b, Q3, ports, 4);
}

/* ---- Q3 master ---- */
typedef struct master_scope {
  qa_app_startup_server_browser *b;
  int source; const char *remote;
  void (*assert_current)(void *ctx); void *assert_ctx;
  const uint8_t *bytes; size_t length;
  double generation, epoch;
} master_scope;
static void request_master_body(void *ctx) {
  master_scope *s = ctx;
  qa_app_startup_server_browser *b = s->b;
  resolve_scope resolve = {s->remote, 27950, true, {0}, false};
  resolve_body(&resolve);
  qa_net_address address = resolve.address;
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_address, &address);
  assert_generation(b, s->generation, false);
  if (s->assert_current) s->assert_current(s->assert_ctx);
  qa_cleanup_pop(&guard);
  if (s->epoch != b->master_epoch) { qa_net_address_destroy(&address); return; }
  clear_master(b);
  b->has_master = true; b->master_source = s->source;
  b->master_address = address; b->master_has_address = true;
  b->master_started = now_ms(); b->master_received = false;
  if (!qa_udp_transport_send(&b->transport, &b->master_address, s->bytes, s->length)) browser_error("Could not send Q3 master query");
}
static void free_bytes(void *bytes) { free(bytes); }
void qa_app_startup_server_browser_request_q3_master(qa_app_startup_server_browser *b, double source_value, const char *remote, double protocol,
                                                     const char *const *keywords, size_t keyword_count,
                                                     void (*assert_current)(void *ctx), void *assert_ctx) {
  qa_app_startup_server_browser_assert_open(b);
  if (assert_current) assert_current(assert_ctx);
  int source = (int)source_value;
  if (source_value != 1 && source_value != 2) qa_raise("RangeError", 0, 0, "Invalid Q3 master source");
  /* q3DiscoveryWire(keywords, protocol).masterQuery() */
  qa_q3_discovery_wire_state state;
  qa_discovery_wire wire;
  if (!qa_q3_discovery_wire_init(&wire, &state, keywords, keyword_count, protocol)) qa_range_error("Out of memory");
  qa_cleanup state_guard;
  qa_cleanup_push(&state_guard, release_wire_state, &state);
  size_t length = 0;
  uint8_t *bytes = wire.master_query(wire.ctx, &length);
  if (!bytes) qa_range_error("Out of memory");
  qa_cleanup_pop(&state_guard);
  qa_q3_discovery_wire_state_destroy(&state);
  qa_cleanup bytes_guard;
  qa_cleanup_push(&bytes_guard, free_bytes, bytes);
  double generation = b->generation, epoch = ++b->master_epoch;
  clear_q3(b, source);
  clear_master(b);
  b->has_master = true; b->master_source = source; b->master_has_address = false;
  b->master_started = now_ms(); b->master_received = false;
  master_scope scope = {b, source, remote, assert_current, assert_ctx, bytes, length, generation, epoch};
  qa_caught_error caught;
  bool ok = qa_try(request_master_body, &scope, &caught);
  qa_cleanup_pop(&bytes_guard);
  free(bytes);
  if (!ok) {
    if (epoch == b->master_epoch) clear_master(b);
    qa_rethrow(&caught);
  }
}

/* ---- Q3 cache ---- */
static qa_q3_browser_cache_view *clone_view(const qa_q3_browser_cache_view *view) {
  if (view == NULL) return NULL;
  qa_q3_browser_cache_view *copy = calloc(1, sizeof *copy);
  if (!copy) qa_range_error("Out of memory");
  copy->lists = calloc(view->list_count ? view->list_count : 1, sizeof *copy->lists);
  if (!copy->lists) { free(copy); qa_range_error("Out of memory"); }
  copy->list_count = view->list_count;
  for (size_t i = 0; i < view->list_count; i++) {
    copy->lists[i].source = view->lists[i].source;
    copy->lists[i].rows = calloc(view->lists[i].row_count ? view->lists[i].row_count : 1, sizeof *copy->lists[i].rows);
    if (!copy->lists[i].rows) qa_range_error("Out of memory");
    for (size_t j = 0; j < view->lists[i].row_count; j++) {
      qa_q3_browser_cache_row *row = &copy->lists[i].rows[j];
      *row = view->lists[i].rows[j];
      row->address.loopback_id = NULL;
      row->address.ipv6_host = NULL;
      row->name = xdup(view->lists[i].rows[j].name ? view->lists[i].rows[j].name : "");
      copy->lists[i].row_count++;
    }
  }
  return copy;
}
typedef struct live_status { char *key; qa_server_status status; bool has_ping; double ping_ms, updated_at; } live_status;
typedef struct load_scope {
  char *saved;
  qa_app_q3_browser_cache cache;
  live_status *live; size_t live_count;
} load_scope;
static void load_scope_release(void *arg) {
  load_scope *scope = arg;
  free(scope->saved); scope->saved = NULL;
  qa_app_q3_browser_cache_free(&scope->cache);
  for (size_t i = 0; i < scope->live_count; i++) { free(scope->live[i].key); qa_server_status_destroy(&scope->live[i].status); }
  free(scope->live); scope->live = NULL; scope->live_count = 0;
}
bool qa_app_startup_server_browser_load_q3_cache(qa_app_startup_server_browser *b, void (*assert_current)(void *ctx), void *assert_ctx,
                                                 qa_q3_browser_cache_view **out_view) {
  *out_view = NULL;
  if (assert_current) assert_current(assert_ctx);
  qa_app_startup_server_browser_assert_open(b); /* persist() */
  double generation = b->generation;
  load_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, load_scope_release, &scope);
  size_t length = 0;
  scope.saved = qa_config_store_load_text(b->config, "servers-cache-q3", &length);
  assert_generation(b, generation, true);
  if (assert_current) assert_current(assert_ctx);
  /* A cache that is not JSON at all means no cache; parsed caches still face version and shape validation. */
  if (scope.saved == NULL || !is_json_text(scope.saved)) {
    qa_cleanup_pop(&guard);
    load_scope_release(&scope);
    return false;
  }
  scope.cache = qa_app_read_q3_browser_cache(scope.saved);
  qa_server_browser *core = browser_of(b, Q3);
  /* live: the entries with a status, read before the lists are cleared. */
  size_t count = qa_server_browser_size(core);
  scope.live = calloc(count ? count : 1, sizeof *scope.live);
  if (!scope.live) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    const qa_browser_entry *entry = qa_server_browser_entry_at(core, i);
    if (!entry->has_status) continue;
    live_status *live = &scope.live[scope.live_count];
    live->key = xkey(&entry->address);
    qa_server_status_copy(&entry->status, &live->status);
    live->has_ping = entry->has_ping; live->ping_ms = entry->ping_ms; live->updated_at = entry->updated_at;
    scope.live_count++;
  }
  for (int source = 1; source <= 3; source++) clear_q3(b, source);
  b->master_epoch++;
  clear_master(b);
  for (size_t i = 0; i < scope.cache.entry_count; i++) {
    qa_browser_entry entry = scope.cache.entries[i];
    char *key = xkey(&entry.address);
    const live_status *current = NULL;
    for (size_t j = 0; j < scope.live_count && current == NULL; j++) if (strcmp(scope.live[j].key, key) == 0) current = &scope.live[j];
    free(key);
    if (current != NULL) {
      /* { ...entry, status: current.status, pingMilliseconds: current.pingMilliseconds, updatedAt: current.updatedAt } */
      entry.has_status = true;
      entry.status = current->status;
      entry.has_ping = current->has_ping; entry.ping_ms = current->ping_ms; entry.updated_at = current->updated_at;
    }
    qa_server_browser_restore_entry(core, &entry);
  }
  /* view = cache.view; this.cachedView = cache.view (null when the cache has none) */
  qa_q3_browser_cache_view *view = scope.cache.has_view ? clone_view(&scope.cache.view) : NULL;
  qa_q3_browser_cache_view *kept = NULL;
  if (view != NULL) {
    qa_cleanup view_guard;
    qa_cleanup_push(&view_guard, release_view, view);
    kept = clone_view(view);
    qa_cleanup_pop(&view_guard);
  }
  set_cached_view(b, kept);
  qa_cleanup_pop(&guard);
  load_scope_release(&scope);
  *out_view = view;
  return view != NULL;
}
static qa_browser_entry *copy_browser_list(qa_server_browser *core, size_t *out_count) {
  size_t count = qa_server_browser_size(core);
  qa_browser_entry *entries = calloc(count ? count : 1, sizeof *entries);
  if (!entries) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    const qa_browser_entry *source = qa_server_browser_entry_at(core, i);
    entries[i] = *source;
    (void)qa_net_address_clone(&source->address, &entries[i].address);
    entries[i].sources = calloc(source->source_count ? source->source_count : 1, sizeof *entries[i].sources);
    if (!entries[i].sources) qa_range_error("Out of memory");
    memcpy(entries[i].sources, source->sources, source->source_count * sizeof *entries[i].sources);
    if (source->has_status) qa_server_status_copy(&source->status, &entries[i].status);
  }
  *out_count = count;
  return entries;
}
static void free_entry_copies(qa_browser_entry *entries, size_t count) {
  for (size_t i = 0; i < count; i++) qa_app_browser_entry_dispose(&entries[i]);
  free(entries);
}
/* writeQ3BrowserCache(core.list(), view) over a copy of the list. */
typedef struct entry_copies { qa_browser_entry *entries; size_t count; } entry_copies;
static void entry_copies_release(void *arg) {
  entry_copies *copies = arg;
  free_entry_copies(copies->entries, copies->count);
  copies->entries = NULL; copies->count = 0;
}
static char *write_q3_cache(qa_app_startup_server_browser *b, const qa_q3_browser_cache_view *view) {
  entry_copies copies = {NULL, 0};
  copies.entries = copy_browser_list(browser_of(b, Q3), &copies.count);
  qa_cleanup guard;
  qa_cleanup_push(&guard, entry_copies_release, &copies);
  char *text = qa_app_write_q3_browser_cache(copies.entries, copies.count, view);
  qa_cleanup_pop(&guard);
  entry_copies_release(&copies);
  return text;
}
/* ConfigStore.dump: the C store reports a failed write as false; the TS store rejects. */
static void config_dump(qa_app_startup_server_browser *b, const char *name, const char *text) {
  errno = 0;
  if (!qa_config_store_dump(b->config, name, text))
    qa_raise("Error", errno, 0, "%s: could not write %s", errno != 0 ? strerror(errno) : "write failed", name);
}
typedef struct text_scope { char *text; } text_scope;
static void text_scope_release(void *arg) { text_scope *scope = arg; free(scope->text); scope->text = NULL; }
void qa_app_startup_server_browser_save_q3_cache(qa_app_startup_server_browser *b, const qa_q3_browser_cache_view *view) {
  qa_app_startup_server_browser_assert_open(b); /* persist() */
  double generation = b->generation;
  text_scope scope = {write_q3_cache(b, view)};
  qa_cleanup guard;
  qa_cleanup_push(&guard, text_scope_release, &scope);
  config_dump(b, "servers-cache-q3", scope.text);
  assert_generation(b, generation, true);
  qa_cleanup_pop(&guard);
  text_scope_release(&scope);
  set_cached_view(b, clone_view(view));
}
static qa_net_address row_address(const qa_q3_browser_cache_row *row) {
  qa_net_address address;
  memset(&address, 0, sizeof address);
  address.kind = QA_NET_IPV4;
  memcpy(address.ipv4, row->address.host, 4);
  address.port = row->address.port;
  return address;
}
static qa_q3_browser_cache_view *current_cache_view(qa_app_startup_server_browser *b) {
  if (b->cached_view == NULL) return NULL;
  qa_q3_browser_cache_view *view = clone_view(b->cached_view);
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_view, view);
  qa_server_browser *core = browser_of(b, Q3);
  for (size_t li = 0; li < view->list_count; li++) {
    qa_q3_browser_cache_list *list = &view->lists[li];
    const char *membership = q3_source_name((int)list->source);
    /* rows = list.rows.filter(row => keys.has(addressKey(row.address))) */
    size_t kept = 0;
    for (size_t i = 0; i < list->row_count; i++) {
      qa_net_address address = row_address(&list->rows[i]);
      const qa_browser_entry *entry = qa_server_browser_get_entry(core, &address);
      if (entry != NULL && has_source(entry, membership)) list->rows[kept++] = list->rows[i];
      else free(list->rows[i].name);
    }
    list->row_count = kept;
    size_t capacity = list->source == 2 ? 4096 : 128, retained = kept;
    for (size_t i = 0; i < qa_server_browser_size(core); i++) {
      if (list->row_count >= capacity) break;
      const qa_browser_entry *entry = qa_server_browser_entry_at(core, i);
      if (!has_source(entry, membership)) continue;
      bool known = false;
      for (size_t j = 0; j < retained && !known; j++) {
        qa_net_address address = row_address(&list->rows[j]);
        known = qa_net_same_address(&address, &entry->address, true);
      }
      if (known) continue;
      qa_q3_browser_cache_row *grown = realloc(list->rows, (list->row_count + 1) * sizeof *grown);
      if (!grown) qa_range_error("Out of memory");
      list->rows = grown;
      qa_q3_browser_cache_row *row = &list->rows[list->row_count];
      memset(row, 0, sizeof *row);
      row->address.kind = QA_Q3_ADDR_IPV4;
      memcpy(row->address.host, entry->address.ipv4, 4);
      row->address.port = entry->address.port;
      const char *name = entry->has_status && entry->status.name ? entry->status.name : "";
      size_t bytes = utf16_prefix_bytes(name, 31);
      row->name = malloc(bytes + 1);
      if (!row->name) qa_range_error("Out of memory");
      memcpy(row->name, name, bytes);
      row->name[bytes] = '\0';
      row->visible = 1;
      row->ping = -1;
      list->row_count++;
    }
  }
  qa_cleanup_pop(&guard);
  return view;
}

/* ---- protocol, sort, details ---- */
void qa_app_startup_server_browser_choose(qa_app_startup_server_browser *b, const char *protocol) {
  int index = protocol_index(protocol);
  if (index < 0) browser_error("Unsupported server protocol");
  b->protocol = index;
  free(b->selected); b->selected = NULL;
}
void qa_app_startup_server_browser_choose_sort(qa_app_startup_server_browser *b, const char *order) {
  for (int i = 0; i < 8; i++) if (strcmp(qa_app_browser_sort_orders[i].id, order) == 0) { b->sort_order = qa_app_browser_sort_orders[i].id; return; }
  browser_error("Unsupported server sort order");
}
const qa_browser_entry *qa_app_startup_server_browser_selected_entry(qa_app_startup_server_browser *b) {
  qa_server_browser *core = browser_of(b, b->protocol);
  if (b->selected == NULL) return NULL;
  for (size_t i = 0; i < qa_server_browser_size(core); i++) {
    const qa_browser_entry *entry = qa_server_browser_entry_at(core, i);
    char *key = xkey(&entry->address);
    bool same = strcmp(key, b->selected) == 0;
    free(key);
    if (same) return entry;
  }
  return NULL;
}
typedef struct lines { char **items; size_t count, capacity; } lines;
static void lines_release(void *arg) {
  lines *l = arg;
  for (size_t i = 0; i < l->count; i++) free(l->items[i]);
  free(l->items);
  l->items = NULL; l->count = 0;
}
static void lines_push(lines *l, char *line) {
  if (l->count == l->capacity) {
    size_t capacity = l->capacity ? l->capacity * 2 : 16;
    char **grown = realloc(l->items, capacity * sizeof *grown);
    if (!grown) { free(line); qa_range_error("Out of memory"); }
    l->items = grown; l->capacity = capacity;
  }
  l->items[l->count++] = line;
}
static char *join3(const char *a, const char *b, const char *c) {
  size_t n = strlen(a) + strlen(b) + strlen(c) + 1;
  char *out = malloc(n);
  if (!out) qa_range_error("Out of memory");
  snprintf(out, n, "%s%s%s", a, b, c);
  return out;
}
typedef struct rule_order { const qa_qw_info_entry *entry; } rule_order;
static int rule_compare(const void *a, const void *b, void *ctx) {
  (void)ctx;
  return qa_js_locale_compare(((const rule_order *)a)->entry->key, ((const rule_order *)b)->entry->key);
}
char **qa_app_startup_server_browser_details(qa_app_startup_server_browser *b, size_t *out_count) {
  const qa_browser_entry *entry = qa_app_startup_server_browser_selected_entry(b);
  lines l = {NULL, 0, 0};
  qa_cleanup guard;
  qa_cleanup_push(&guard, lines_release, &l);
  if (entry == NULL) lines_push(&l, xdup("Select a server first."));
  else if (!entry->has_status) {
    lines_push(&l, qa_app_browser_address(&entry->address));
    lines_push(&l, xdup("No status response yet."));
  } else {
    const qa_server_status *status = &entry->status;
    lines_push(&l, clean_text(status->name));
    lines_push(&l, qa_app_browser_address(&entry->address));
    char *map = clean_text(status->map);
    size_t n = strlen(map) + 96;
    char *line = malloc(n);
    if (!line) { free(map); qa_range_error("Out of memory"); }
    snprintf(line, n, "Map: %s  Players: %s/%s", map, QA_JS_NUMBER(status->players), QA_JS_NUMBER(status->max_players));
    free(map);
    lines_push(&l, line);
    for (size_t i = 0; i < status->player_count; i++) {
      char *name = clean_text(status->player_details[i].name);
      size_t m = strlen(name) + 96;
      char *player = malloc(m);
      if (!player) { free(name); qa_range_error("Out of memory"); }
      snprintf(player, m, "%s  score %s  ping %s", name, QA_JS_NUMBER(status->player_details[i].score), QA_JS_NUMBER(status->player_details[i].ping));
      free(name);
      lines_push(&l, player);
    }
    /* [...status.rules].sort(([a], [b]) => a.localeCompare(b)) */
    rule_order *order = calloc(status->rules.count ? status->rules.count : 1, sizeof *order);
    if (!order) qa_range_error("Out of memory");
    for (size_t i = 0; i < status->rules.count; i++) order[i].entry = &status->rules.entries[i];
    qa_js_stable_sort(order, status->rules.count, sizeof *order, rule_compare, NULL);
    for (size_t i = 0; i < status->rules.count; i++) {
      char *key = clean_text(order[i].entry->key), *value = clean_text(order[i].entry->value);
      char *rule = join3(key, ": ", value);
      free(key); free(value);
      lines_push(&l, rule);
    }
    free(order);
  }
  qa_cleanup_pop(&guard);
  *out_count = l.count;
  return l.items;
}
static void queue_status(qa_app_startup_server_browser *b, int protocol, const qa_net_address *address) {
  char *address_key = xkey(address);
  char *key = join3(PROTOCOLS[protocol], ":", address_key);
  free(address_key);
  /* Map.set: an existing key keeps its position and takes the new value. */
  for (size_t i = 0; i < b->pending_count; i++) if (strcmp(b->pending[i].key, key) == 0) {
    free(key);
    qa_net_address copy;
    if (!qa_net_address_clone(address, &copy)) qa_range_error("Out of memory");
    qa_net_address_destroy(&b->pending[i].address);
    b->pending[i].address = copy;
    b->pending[i].protocol = protocol;
    return;
  }
  if (b->pending_count == b->pending_cap) {
    size_t capacity = b->pending_cap ? b->pending_cap * 2 : 16;
    pending_status *grown = realloc(b->pending, capacity * sizeof *grown);
    if (!grown) { free(key); qa_range_error("Out of memory"); }
    b->pending = grown; b->pending_cap = capacity;
  }
  pending_status *entry = &b->pending[b->pending_count];
  if (!qa_net_address_clone(address, &entry->address)) { free(key); qa_range_error("Out of memory"); }
  entry->key = key;
  entry->protocol = protocol;
  b->pending_count++;
}

/* ---- discover ---- */
typedef struct discover_scope {
  char *remote;
  char **names; size_t name_count;
  qa_net_address *addresses; size_t address_count;
} discover_scope;
static void discover_scope_release(void *arg) {
  discover_scope *s = arg;
  free(s->remote); s->remote = NULL;
  qa_app_server_master_list_free(s->names, s->name_count); s->names = NULL; s->name_count = 0;
  free_addresses(s->addresses, s->address_count); s->addresses = NULL; s->address_count = 0;
}
static void count_skipped(void *ctx, double count) { *(double *)ctx += count; }
static bool http_url(const char *remote) {
  return strncasecmp(remote, "http://", 7) == 0 || strncasecmp(remote, "https://", 8) == 0;
}
/* remote.replace(/^udp:\/\//, "") */
static const char *without_udp(const char *remote) { return strncmp(remote, "udp://", 6) == 0 ? remote + 6 : remote; }
void qa_app_startup_server_browser_discover(qa_app_startup_server_browser *b) {
  qa_app_startup_server_browser_assert_open(b);
  int protocol = b->protocol;
  discover_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, discover_scope_release, &scope);
  scope.remote = trimmed(qa_app_startup_server_browser_master_address(b));
  double generation = b->generation;
  if (scope.remote[0] == '\0') browser_error("Enter a master address or HTTP list URL");
  /* const timeout = setTimeout(() => controller.abort(), 4000) */
  qa_app_master_list_signal signal = {now_ms() + 4000, false};
  if (http_url(scope.remote)) {
    double skipped = 0;
    scope.names = qa_app_fetch_server_master_list(scope.remote, &signal, count_skipped, &skipped, &scope.name_count);
    scope.addresses = calloc(scope.name_count ? scope.name_count : 1, sizeof *scope.addresses);
    if (!scope.addresses) qa_range_error("Out of memory");
    /* Batches of 16 resolutions (Promise.allSettled); a rejected one is skipped. */
    for (size_t offset = 0; offset < scope.name_count; offset += 16) {
      size_t end = offset + 16 < scope.name_count ? offset + 16 : scope.name_count;
      bool fulfilled[16];
      qa_net_address resolved[16];
      for (size_t i = offset; i < end; i++) {
        resolve_scope resolve = {scope.names[i], PORTS[protocol], false, {0}, false};
        qa_caught_error caught;
        fulfilled[i - offset] = qa_try(resolve_body, &resolve, &caught);
        resolved[i - offset] = resolve.address;
      }
      for (size_t i = offset; i < end; i++) {
        if (fulfilled[i - offset]) scope.addresses[scope.address_count++] = resolved[i - offset];
        else skipped++;
      }
      assert_generation(b, generation, false);
      if (signal.aborted || now_ms() >= signal.deadline_ms) qa_raise("AbortError", 0, 0, "The operation was aborted.");
    }
    qa_server_browser *core = browser_of(b, protocol);
    for (size_t i = 0; i < scope.address_count; i++) {
      if (qa_server_browser_size(core) >= 4096 && qa_server_browser_get_entry(core, &scope.addresses[i]) == NULL) break;
      if (protocol == Q3) qa_app_startup_server_browser_add_q3(b, 2, &scope.addresses[i]);
      else (void)qa_server_browser_add(core, &scope.addresses[i], "master", 0);
      queue_status(b, protocol, &scope.addresses[i]);
    }
    char text[160];
    if (skipped == 0) snprintf(text, sizeof text, "Found %zu servers; querying status", scope.address_count);
    else snprintf(text, sizeof text, "Found %zu servers; querying status (%s skipped)", scope.address_count, QA_JS_NUMBER(skipped));
    set_str(&b->status, text);
  } else if (protocol == Q2) {
    resolve_scope resolve = {without_udp(scope.remote), 27900, true, {0}, false};
    resolve_body(&resolve);
    scope.addresses = calloc(1, sizeof *scope.addresses);
    if (!scope.addresses) { qa_net_address_destroy(&resolve.address); qa_range_error("Out of memory"); }
    scope.addresses[0] = resolve.address;
    scope.address_count = 1;
    assert_generation(b, generation, false);
    if (signal.aborted || now_ms() >= signal.deadline_ms) qa_raise("AbortError", 0, 0, "The operation was aborted.");
    clear_q2_master(b);
    if (!qa_net_address_clone(&scope.addresses[0], &b->q2_master_address)) qa_range_error("Out of memory");
    b->has_q2_master = true;
    b->q2_master_started = now_ms();
    if (!qa_server_browser_query_master(browser_of(b, protocol), &scope.addresses[0])) browser_error("Could not send master query");
    set_str(&b->status, "Querying master...");
  } else if (protocol == Q3) {
    static const char *const keywords[] = {"empty", "full"};
    qa_app_startup_server_browser_request_q3_master(b, 2, without_udp(scope.remote), 68, keywords, 2, NULL, NULL);
    set_str(&b->status, "Querying master...");
  } else browser_error("Use an HTTP master list for Quake or QuakeWorld");
  /* persist(async () => { assertGeneration(generation); await config.dump(`servers-master-${protocol}`, remote) }) */
  qa_app_startup_server_browser_assert_open(b);
  assert_generation(b, generation, false);
  char name[64];
  snprintf(name, sizeof name, "servers-master-%s", PROTOCOLS[protocol]);
  config_dump(b, name, scope.remote);
  assert_generation(b, generation, false);
  qa_cleanup_pop(&guard);
  discover_scope_release(&scope);
}
const char *qa_app_startup_server_browser_empty_message(qa_app_startup_server_browser *b) {
  return qa_server_browser_size(browser_of(b, b->protocol)) == 0
             ? "No servers yet. Enter an address or find LAN."
             : "No servers match these filters.";
}

/* ---- rows ---- */
typedef struct row_key { const qa_browser_entry *entry; char *address; } row_key;
typedef struct rows_scope { row_key *rows; size_t count; char *search; char *haystack; } rows_scope;
static void rows_scope_release(void *arg) {
  rows_scope *s = arg;
  for (size_t i = 0; i < s->count; i++) free(s->rows[i].address);
  free(s->rows); s->rows = NULL; s->count = 0;
  free(s->search); s->search = NULL;
  free(s->haystack); s->haystack = NULL;
}
static int sign_of(double value) { return value < 0 ? -1 : value > 0 ? 1 : 0; }
static int entry_compare(const void *left, const void *right, void *ctx) {
  const qa_app_startup_server_browser *b = ctx;
  const row_key *ra = left, *rb = right;
  const qa_browser_entry *a = ra->entry, *c = rb->entry;
  /* Q2's source keeps unanswered rows last in either direction and treats 0/0 as unknown capacity. */
  if (a->has_status != c->has_status) return a->has_status ? -1 : 1;
  double compared = 0;
  const char *order = b->sort_order;
  if (strcmp(order, "ping-low") == 0 || strcmp(order, "ping-high") == 0) {
    if (a->has_ping != c->has_ping) return a->has_ping ? -1 : 1;
    compared = ((a->has_ping ? a->ping_ms : 0) - (c->has_ping ? c->ping_ms : 0)) * (strcmp(order, "ping-low") == 0 ? 1 : -1);
  } else if (strcmp(order, "name-az") == 0 || strcmp(order, "name-za") == 0) {
    compared = (double)qa_js_locale_compare_base(a->has_status && a->status.name ? a->status.name : "", c->has_status && c->status.name ? c->status.name : "")
               * (strcmp(order, "name-az") == 0 ? 1 : -1);
  } else if (strcmp(order, "map-az") == 0 || strcmp(order, "map-za") == 0) {
    compared = (double)qa_js_locale_compare_base(a->has_status && a->status.map ? a->status.map : "", c->has_status && c->status.map ? c->status.map : "")
               * (strcmp(order, "map-az") == 0 ? 1 : -1);
  } else if (strcmp(order, "players-most") == 0 || strcmp(order, "players-fewest") == 0) {
    compared = ((a->has_status ? a->status.players : 0) - (c->has_status ? c->status.players : 0)) * (strcmp(order, "players-fewest") == 0 ? 1 : -1);
  }
  /* compared || browserAddress(a).localeCompare(browserAddress(b)): NaN and 0 fall through. */
  if (compared != 0 && !isnan(compared)) return sign_of(compared);
  return qa_js_locale_compare(ra->address, rb->address);
}
static char *row_haystack(const qa_browser_entry *entry, const char *address) {
  const char *name = entry->has_status && entry->status.name ? entry->status.name : "";
  const char *map = entry->has_status && entry->status.map ? entry->status.map : "";
  const char *game = NULL;
  if (entry->has_status) {
    game = qa_qw_info_get(&entry->status.rules, "gamedir");
    if (game == NULL) game = qa_qw_info_get(&entry->status.rules, "game");
  }
  size_t n = strlen(name) + strlen(map) + (game ? strlen(game) : 0) + strlen(address) + 8;
  if (entry->has_status)
    for (size_t i = 0; i < entry->status.player_count; i++) n += strlen(entry->status.player_details[i].name ? entry->status.player_details[i].name : "") + 1;
  char *text = malloc(n);
  if (!text) qa_range_error("Out of memory");
  size_t at = (size_t)snprintf(text, n, "%s %s %s ", name, map, game ? game : "");
  /* playerDetails.map(player => player.name.replace(/\^[0-9]/g, "")).join(" ") */
  if (entry->has_status)
    for (size_t i = 0; i < entry->status.player_count; i++) {
      if (i) text[at++] = ' ';
      for (const char *p = entry->status.player_details[i].name ? entry->status.player_details[i].name : ""; *p; p++) {
        if (p[0] == '^' && p[1] >= '0' && p[1] <= '9') { p++; continue; }
        text[at++] = *p;
      }
    }
  snprintf(text + at, n - at, " %s", address);
  return text;
}
/* computeRows(entries): filter by favorites, capacity and search text, then the selected order. */
static const qa_browser_entry **compute_rows(qa_app_startup_server_browser *b, qa_server_browser *core, size_t *out_count) {
  size_t total = qa_server_browser_size(core);
  rows_scope scope = {NULL, 0, NULL, NULL};
  qa_cleanup guard;
  qa_cleanup_push(&guard, rows_scope_release, &scope);
  char *search_text = trimmed(b->filter);
  scope.search = qa_js_to_lower_case(search_text);
  free(search_text);
  if (!scope.search) qa_range_error("Out of memory");
  scope.rows = calloc(total ? total : 1, sizeof *scope.rows);
  if (!scope.rows) qa_range_error("Out of memory");
  for (size_t i = 0; i < total; i++) {
    const qa_browser_entry *entry = qa_server_browser_entry_at(core, i);
    if (b->favorites_only && !has_source(entry, "favorite")) continue;
    if (entry->has_status && !(entry->status.max_players <= 0) &&
        !((!b->hide_empty || entry->status.players > 0) && (!b->hide_full || entry->status.players < entry->status.max_players)))
      continue;
    char *address = qa_app_browser_address(&entry->address);
    scope.haystack = row_haystack(entry, address);
    char *lower = qa_js_to_lower_case(scope.haystack);
    free(scope.haystack); scope.haystack = NULL;
    if (!lower) { free(address); qa_range_error("Out of memory"); }
    bool match = strstr(lower, scope.search) != NULL;
    free(lower);
    if (!match) { free(address); continue; }
    scope.rows[scope.count].entry = entry;
    scope.rows[scope.count].address = address;
    scope.count++;
  }
  qa_js_stable_sort(scope.rows, scope.count, sizeof *scope.rows, entry_compare, b);
  const qa_browser_entry **out = calloc(scope.count ? scope.count : 1, sizeof *out);
  if (!out) qa_range_error("Out of memory");
  for (size_t i = 0; i < scope.count; i++) out[i] = scope.rows[i].entry;
  size_t count = scope.count;
  qa_cleanup_pop(&guard);
  rows_scope_release(&scope);
  *out_count = count;
  return out;
}
/* rows(): TS caches the result by entry identity; C entries are edited in place, so the rows are
 * recomputed (the same result) and the array is owned until the next call. */
const qa_browser_entry **qa_app_startup_server_browser_rows(qa_app_startup_server_browser *b, size_t *out_count) {
  const qa_browser_entry **out = compute_rows(b, browser_of(b, b->protocol), out_count);
  free(b->row_scratch);
  b->row_scratch = out;
  return out;
}
void qa_app_startup_server_browser_select(qa_app_startup_server_browser *b, const char *key) {
  size_t count = 0;
  const qa_browser_entry **rows = qa_app_startup_server_browser_rows(b, &count);
  const qa_browser_entry *entry = NULL;
  for (size_t i = 0; i < count && entry == NULL; i++) {
    char *entry_key = xkey(&rows[i]->address);
    if (strcmp(entry_key, key) == 0) entry = rows[i];
    free(entry_key);
  }
  if (entry == NULL) return;
  const char *direct_remote = NULL;
  if (has_source(entry, "direct") && b->has_direct[b->protocol]) {
    for (size_t i = 0; i < b->direct_count[b->protocol] && direct_remote == NULL; i++) {
      char *server_key = xkey(&b->direct[b->protocol][i].address);
      if (strcmp(server_key, key) == 0) direct_remote = b->direct[b->protocol][i].remote;
      free(server_key);
    }
  }
  char *address = direct_remote != NULL ? xdup(direct_remote) : qa_app_browser_address(&entry->address);
  set_str(&b->selected, key);
  free(b->addresses[b->protocol]);
  b->addresses[b->protocol] = address;
}

/* ---- persistence of direct servers ---- */
static void append_json_string(char **text, size_t *length, size_t *capacity, const char *value) {
  for (int pass = 0; pass < 1; pass++) {
    size_t need = *length + strlen(value) * 6 + 3;
    if (need > *capacity) {
      size_t grown_capacity = *capacity ? *capacity : 256;
      while (grown_capacity < need) grown_capacity *= 2;
      char *grown = realloc(*text, grown_capacity);
      if (!grown) qa_range_error("Out of memory");
      *text = grown; *capacity = grown_capacity;
    }
  }
  char *out = *text + *length;
  *out++ = '"';
  for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
    switch (*p) {
      case '"': *out++ = '\\'; *out++ = '"'; break;
      case '\\': *out++ = '\\'; *out++ = '\\'; break;
      case '\b': *out++ = '\\'; *out++ = 'b'; break;
      case '\f': *out++ = '\\'; *out++ = 'f'; break;
      case '\n': *out++ = '\\'; *out++ = 'n'; break;
      case '\r': *out++ = '\\'; *out++ = 'r'; break;
      case '\t': *out++ = '\\'; *out++ = 't'; break;
      default:
        if (*p < 0x20) out += sprintf(out, "\\u%04x", *p);
        else *out++ = (char)*p;
    }
  }
  *out++ = '"';
  *out = '\0';
  *length = (size_t)(out - *text);
}
static void append_text(char **text, size_t *length, size_t *capacity, const char *value) {
  size_t n = strlen(value);
  if (*length + n + 1 > *capacity) {
    size_t grown_capacity = *capacity ? *capacity : 256;
    while (grown_capacity < *length + n + 1) grown_capacity *= 2;
    char *grown = realloc(*text, grown_capacity);
    if (!grown) qa_range_error("Out of memory");
    *text = grown; *capacity = grown_capacity;
  }
  memcpy(*text + *length, value, n + 1);
  *length += n;
}
/* `${JSON.stringify({ version: 1, servers })}\n` */
static char *direct_servers_json(const qa_app_direct_server_address *servers, size_t count) {
  char *text = NULL; size_t length = 0, capacity = 0;
  text_scope scope = {NULL};
  qa_cleanup guard;
  qa_cleanup_push(&guard, text_scope_release, &scope);
  append_text(&text, &length, &capacity, "{\"version\":1,\"servers\":[");
  scope.text = text;
  for (size_t i = 0; i < count; i++) {
    const qa_net_address *a = &servers[i].address;
    append_text(&text, &length, &capacity, i ? ",{\"remote\":" : "{\"remote\":"); scope.text = text;
    append_json_string(&text, &length, &capacity, servers[i].remote); scope.text = text;
    char record[160];
    if (a->kind == QA_NET_IPV4) {
      snprintf(record, sizeof record, ",\"address\":{\"kind\":\"ipv4\",\"host\":[%u,%u,%u,%u],\"port\":%lld}}",
               a->ipv4[0], a->ipv4[1], a->ipv4[2], a->ipv4[3], (long long)a->port);
      append_text(&text, &length, &capacity, record); scope.text = text;
    } else {
      append_text(&text, &length, &capacity, ",\"address\":{\"kind\":\"ipv6\",\"host\":"); scope.text = text;
      append_json_string(&text, &length, &capacity, a->host ? a->host : ""); scope.text = text;
      snprintf(record, sizeof record, ",\"port\":%lld}}", (long long)a->port);
      append_text(&text, &length, &capacity, record); scope.text = text;
    }
  }
  append_text(&text, &length, &capacity, "]}\n");
  qa_cleanup_pop(&guard);
  return text;
}
typedef struct direct_list { qa_app_direct_server_address *servers; size_t count; char *text; } direct_list;
static void direct_list_release(void *arg) {
  direct_list *list = arg;
  qa_app_direct_servers_free(list->servers, list->count);
  list->servers = NULL; list->count = 0;
  free(list->text); list->text = NULL;
}
static void remember_direct(qa_app_startup_server_browser *b, int protocol, const char *remote, const qa_net_address *address) {
  double generation = b->generation;
  direct_list list = {NULL, 0, NULL};
  qa_cleanup guard;
  qa_cleanup_push(&guard, direct_list_release, &list);
  /* [server, ...previous].filter(first by addressKey).slice(0, maximumDirectServers) */
  size_t previous = b->direct_count[protocol];
  list.servers = calloc(previous + 1, sizeof *list.servers);
  if (!list.servers) qa_range_error("Out of memory");
  char **keys = calloc(previous + 1, sizeof *keys);
  if (!keys) qa_range_error("Out of memory");
  size_t key_count = 0;
  for (size_t i = 0; i <= previous && list.count < QA_APP_MAXIMUM_DIRECT_SERVERS; i++) {
    const char *candidate_remote = i == 0 ? remote : b->direct[protocol][i - 1].remote;
    const qa_net_address *candidate = i == 0 ? address : &b->direct[protocol][i - 1].address;
    char *key = qa_net_address_key(candidate, true);
    bool seen = key == NULL;
    for (size_t j = 0; j < key_count && !seen; j++) seen = strcmp(keys[j], key) == 0;
    if (seen) { free(key); continue; }
    keys[key_count++] = key;
    qa_app_direct_server_address *server = &list.servers[list.count];
    if (!qa_net_address_clone(candidate, &server->address)) break;
    server->remote = malloc(strlen(candidate_remote) + 1);
    list.count++;
    if (server->remote == NULL) break;
    memcpy(server->remote, candidate_remote, strlen(candidate_remote) + 1);
  }
  bool complete = key_count == 0 || list.servers[list.count - 1].remote != NULL;
  for (size_t i = 0; i < key_count; i++) free(keys[i]);
  free(keys);
  if (!complete) qa_range_error("Out of memory");
  list.text = direct_servers_json(list.servers, list.count);
  char name[64];
  snprintf(name, sizeof name, "servers-direct-%s", PROTOCOLS[protocol]);
  config_dump(b, name, list.text);
  assert_generation(b, generation, true);
  qa_cleanup_pop(&guard);
  free(list.text);
  qa_app_direct_servers_free(b->direct[protocol], b->direct_count[protocol]);
  b->direct[protocol] = list.servers;
  b->direct_count[protocol] = list.count;
  b->has_direct[protocol] = true;
  (void)qa_server_browser_add(browser_of(b, protocol), address, "direct", 0);
}
/* persist(async () => { generation; address = await resolveAddress(remote, ports[protocol], 4); ... }) */
typedef struct resolved_remote { char *remote; qa_net_address address; bool has_address; } resolved_remote;
static void resolved_remote_release(void *arg) {
  resolved_remote *r = arg;
  free(r->remote); r->remote = NULL;
  if (r->has_address) qa_net_address_destroy(&r->address);
  r->has_address = false;
}
static void resolve_remote(qa_app_startup_server_browser *b, resolved_remote *r) {
  resolve_scope resolve = {r->remote, PORTS[b->protocol], false, {0}, false};
  resolve_body(&resolve);
  r->address = resolve.address;
  r->has_address = true;
}
/* remote = directServerText(this.address.trim()) */
static char *direct_remote(qa_app_startup_server_browser *b) {
  char *remote = trimmed(qa_app_startup_server_browser_address(b));
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, remote);
  (void)qa_app_direct_server_text(remote);
  qa_cleanup_pop(&guard);
  return remote;
}
void qa_app_startup_server_browser_query(qa_app_startup_server_browser *b) {
  int protocol = b->protocol;
  resolved_remote r = {direct_remote(b), {0}, false};
  qa_cleanup guard;
  qa_cleanup_push(&guard, resolved_remote_release, &r);
  qa_app_startup_server_browser_assert_open(b);
  double generation = b->generation;
  resolve_remote(b, &r);
  assert_generation(b, generation, true);
  remember_direct(b, protocol, r.remote, &r.address);
  assert_generation(b, generation, true);
  qa_server_browser *core = browser_of(b, protocol);
  double now = now_ms();
  if (!qa_server_browser_query(core, &r.address, now, "status")) browser_error("Could not send server query");
  set_str(&b->status, "Query sent");
  qa_cleanup_pop(&guard);
  resolved_remote_release(&r);
}

/* ---- LAN scan and KEX mDNS ---- */
static void kex_found(void *ctx, const qa_net_address *address) {
  qa_app_startup_server_browser *b = ctx;
  if (b->closing || b->closed) return;
  char *key = xkey(address);
  double now = now_ms(), last = -INFINITY;
  size_t at = b->kex_query_count;
  for (size_t i = 0; i < b->kex_query_count; i++) if (strcmp(b->kex_queries[i].key, key) == 0) { last = b->kex_queries[i].at; at = i; }
  if (now - last < 1000 || b->kex_query_count >= 256) { free(key); return; }
  if (at < b->kex_query_count) { b->kex_queries[at].at = now; free(key); }
  else {
    if (b->kex_query_count == b->kex_query_cap) {
      size_t capacity = b->kex_query_cap ? b->kex_query_cap * 2 : 16;
      kex_query *grown = realloc(b->kex_queries, capacity * sizeof *grown);
      if (!grown) { free(key); qa_range_error("Out of memory"); }
      b->kex_queries = grown; b->kex_query_cap = capacity;
    }
    b->kex_queries[b->kex_query_count].key = key;
    b->kex_queries[b->kex_query_count].at = now;
    b->kex_query_count++;
  }
  size_t length = 0;
  uint8_t *query = qa_kex_discovery_query(&length);
  if (!query) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, free_bytes, query);
  (void)qa_udp_transport_send(&b->transport, address, query, length);
  qa_cleanup_pop(&guard);
  free(query);
}
static void kex_failed(void *ctx, const char *message) {
  qa_app_startup_server_browser *b = ctx;
  if (!b->closing && !b->closed) set_str(&b->status, message);
}
void qa_app_startup_server_browser_scan(qa_app_startup_server_browser *b) {
  qa_app_startup_server_browser_assert_open(b);
  if (b->protocol == Q3) {
    qa_app_startup_server_browser_scan_q3(b);
    set_str(&b->status, "Searching local network...");
    return;
  }
  char failure[160] = "";
  if (b->protocol == Q2) {
    if (b->kex_open) qa_kex_mdns_query(&b->kex);
    else if (!b->kex_opening) {
      /* KexMdns.open resolves in place; a failed bind rejects with the socket error. */
      b->kex_opening = true;
      errno = 0;
      bool opened = qa_kex_mdns_open(&b->kex, false, 0, kex_found, b, kex_failed, b);
      int code = errno;
      b->kex_opening = false;
      if (!opened) snprintf(failure, sizeof failure, "bind %s 0.0.0.0:5353", code != 0 ? strerror(code) : "failed");
      else if (b->closing || b->closed) qa_kex_mdns_close(&b->kex);
      else {
        b->kex_open = true;
        qa_kex_mdns_query(&b->kex);
      }
    }
  }
  int64_t port = PORTS[b->protocol];
  broadcast_to(b, b->protocol, &port, 1);
  set_str(&b->status, "Searching local network...");
  /* The TS rejection handler runs after this synchronous status. */
  if (failure[0] != '\0' && !b->closing && !b->closed) set_str(&b->status, failure);
}

/* ---- favorites and connection ---- */
typedef struct favorite_scope {
  qa_app_startup_server_browser *b; int protocol; qa_server_browser *core;
  qa_q3_browser_cache_view *view; char *text; double generation;
} favorite_scope;
static void favorite_write_body(void *ctx) {
  favorite_scope *s = ctx;
  char name[64];
  if (s->protocol == Q3) {
    snprintf(name, sizeof name, "servers-cache-q3");
    s->text = write_q3_cache(s->b, s->view);
  } else {
    snprintf(name, sizeof name, "servers-%s", PROTOCOLS[s->protocol]);
    s->text = qa_server_browser_save_favorites(s->core);
    if (!s->text) qa_range_error("Out of memory");
  }
  config_dump(s->b, name, s->text);
  assert_generation(s->b, s->generation, true);
}
void qa_app_startup_server_browser_favorite(qa_app_startup_server_browser *b) {
  int protocol = b->protocol;
  resolved_remote r = {direct_remote(b), {0}, false};
  qa_cleanup guard;
  qa_cleanup_push(&guard, resolved_remote_release, &r);
  qa_app_startup_server_browser_assert_open(b);
  double generation = b->generation;
  resolve_remote(b, &r);
  qa_server_browser *core = browser_of(b, protocol);
  assert_generation(b, generation, true);
  char *before = qa_server_browser_save_favorites(core);
  if (!before) qa_range_error("Out of memory");
  qa_cleanup before_guard;
  qa_cleanup_push(&before_guard, free, before);
  const qa_browser_entry *existing = qa_server_browser_get_entry(core, &r.address);
  bool removing = existing != NULL && has_source(existing, "favorite");
  if (removing) qa_server_browser_remove_favorite(core, &r.address);
  else (void)qa_server_browser_add(core, &r.address, "favorite", 0);
  if (protocol == Q3) b->list_generations[3]++;
  favorite_scope scope = {b, protocol, core, protocol == Q3 ? current_cache_view(b) : NULL, NULL, generation};
  qa_caught_error caught;
  bool ok = qa_try(favorite_write_body, &scope, &caught);
  free(scope.text);
  if (!ok) {
    if (scope.view) { qa_q3_browser_cache_view_destroy(scope.view); free(scope.view); }
    qa_server_browser_restore_favorites(core, before);
    if (protocol == Q3) b->list_generations[3]++;
    qa_rethrow(&caught);
  }
  qa_cleanup_pop(&before_guard);
  free(before);
  if (protocol == Q3) set_cached_view(b, scope.view);
  set_str(&b->status, removing ? "Favorite removed" : "Favorite added");
  qa_cleanup_pop(&guard);
  resolved_remote_release(&r);
}
qa_app_browser_connection qa_app_startup_server_browser_connection(qa_app_startup_server_browser *b) {
  int protocol = b->protocol;
  resolved_remote r = {direct_remote(b), {0}, false};
  qa_cleanup guard;
  qa_cleanup_push(&guard, resolved_remote_release, &r);
  qa_app_startup_server_browser_assert_open(b);
  double generation = b->generation;
  resolve_remote(b, &r);
  assert_generation(b, generation, true);
  remember_direct(b, protocol, r.remote, &r.address);
  assert_generation(b, generation, true);
  /* The entry is found by the resolved address, since `remote` may be a hostname. */
  const qa_browser_entry *selected = qa_server_browser_get_entry(browser_of(b, protocol), &r.address);
  qa_app_browser_connection connection;
  memset(&connection, 0, sizeof connection);
  connection.protocol = PROTOCOLS[protocol];
  if (protocol == Q2 && selected != NULL && selected->has_status && !selected->status.wire.is_unified &&
      selected->status.wire.protocol.kind != NULL && strcmp(selected->status.wire.protocol.kind, "q2-kex") == 0) {
    connection.has_q2_protocol = true;
    connection.q2_protocol_kind = "q2-kex";
    connection.q2_protocol_version = (double)selected->status.wire.protocol.version;
  }
  qa_cleanup_pop(&guard);
  connection.remote = r.remote;
  r.remote = NULL;
  resolved_remote_release(&r);
  return connection;
}

/* ---- poll ---- */
typedef struct packet_scope {
  qa_app_startup_server_browser *b;
  const qa_net_address *from; const uint8_t *payload; size_t length;
  int protocol;
  bool handled;
  /* owned decode results */
  qa_net_address *addresses; size_t address_count; bool complete;
  qa_server_status status; bool has_status;
  char *challenge;
  qa_q2_connectionless_message message; bool has_message;
  double kex_sent;
} packet_scope;
static void packet_scope_release(void *arg) {
  packet_scope *s = arg;
  free_addresses(s->addresses, s->address_count); s->addresses = NULL; s->address_count = 0;
  if (s->has_status) qa_server_status_destroy(&s->status);
  s->has_status = false;
  free(s->challenge); s->challenge = NULL;
  if (s->has_message) qa_q2_connectionless_message_free(&s->message);
  s->has_message = false;
}
static void q2_master_body(void *ctx) {
  packet_scope *s = ctx;
  qa_app_startup_server_browser *b = s->b;
  size_t count = 0;
  qa_net_address *addresses = qa_q2_read_master_reply(s->payload, s->length, &count);
  if (addresses == NULL) return;
  s->addresses = addresses; s->address_count = count;
  qa_server_browser *core = browser_of(b, Q2);
  for (size_t i = 0; i < count; i++) {
    if (qa_server_browser_size(core) >= 4096 && qa_server_browser_get_entry(core, &addresses[i]) == NULL) break;
    (void)qa_server_browser_add(core, &addresses[i], "master", 0);
    queue_status(b, Q2, &addresses[i]);
  }
  char text[96];
  snprintf(text, sizeof text, "Found %zu servers; querying status", count);
  set_str(&b->status, text);
  s->handled = true;
}
static void q3_master_body(void *ctx) {
  packet_scope *s = ctx;
  qa_app_startup_server_browser *b = s->b;
  if (!qa_q3_decode_master_packet(s->payload, s->length, &s->addresses, &s->address_count, &s->complete)) qa_range_error("Out of memory");
  size_t added_count = 0;
  qa_net_address *added = add_q3_many(b, b->master_source, s->addresses, s->address_count, &added_count);
  free_addresses(s->addresses, s->address_count);
  s->addresses = added; s->address_count = added_count;
  for (size_t i = 0; i < added_count; i++) queue_status(b, Q3, &added[i]);
  if (s->complete) clear_master(b);
  else b->master_received = true;
  s->handled = true;
}
static void kex_status_body(void *ctx) {
  packet_scope *s = ctx;
  qa_app_startup_server_browser *b = s->b;
  if (!qa_kex_read_discovery(s->payload, s->length, &s->status)) qa_range_error("Out of memory");
  s->has_status = true;
  qa_server_browser *core = browser_of(b, Q2);
  double now = now_ms();
  const qa_browser_entry *entry = qa_server_browser_add(core, s->from, "lan", now);
  /* core.restoreEntry({ ...entry, status, pingMilliseconds: Math.max(0, now - kexSent), updatedAt: now }) */
  qa_browser_entry restored = *entry;
  restored.has_status = true;
  restored.status = s->status;
  restored.has_ping = true;
  restored.ping_ms = qa_js_max(0, now - s->kex_sent);
  restored.updated_at = now;
  qa_server_browser_restore_entry(core, &restored);
  s->handled = true;
}
/* One protocol's decode: { status, challenge } with status possibly null. */
static void decode_status_body(void *ctx) {
  packet_scope *s = ctx;
  switch (s->protocol) {
    case Q1:
      qa_read_netquake_discovery(s->payload, s->length, &s->status);
      s->has_status = true;
      break;
    case QW:
      qa_read_quakeworld_discovery(s->payload, s->length, &s->status);
      s->has_status = true;
      break;
    case Q2: {
      if (!qa_q2_read_out_of_band(s->payload, s->length, false, &s->message)) break;
      s->has_message = true;
      qa_q2_protocol_identity protocol;
      qa_q2_get_protocol(34, 0, &protocol);
      s->has_status = qa_q2_read_status(&s->message, protocol, &s->status);
      break;
    }
    default:
      if (!qa_q3_decode_server_status(s->payload, s->length, &s->challenge, &s->status)) qa_range_error("Out of memory");
      s->has_status = true;
      break;
  }
}
/* The bounded copy poll() hands to core.receive: clean name and map, rules cut at
 * 1000 info bytes (the UI rebuilds them as one info string that cannot exceed 1023
 * bytes), at most 1024 players with names of at most 1024 units. */
static void bounded_status(const qa_server_status *status, qa_server_status *out) {
  memset(out, 0, sizeof *out);
  qa_qw_info_init(&out->rules);
  out->name = clean_text(status->name);
  out->map = clean_text(status->map);
  out->players = status->players;
  out->max_players = status->max_players;
  size_t rule_bytes = 0;
  for (size_t i = 0; i < status->rules.count; i++) {
    rule_bytes += utf16_length(status->rules.entries[i].key) + utf16_length(status->rules.entries[i].value) + 2;
    if (rule_bytes > 1000) break;
    qa_qw_info_set(&out->rules, status->rules.entries[i].key, status->rules.entries[i].value);
  }
  size_t players = status->player_count < 1024 ? status->player_count : 1024;
  out->player_details = calloc(players ? players : 1, sizeof *out->player_details);
  if (!out->player_details) qa_range_error("Out of memory");
  for (size_t i = 0; i < players; i++) {
    const char *name = status->player_details[i].name ? status->player_details[i].name : "";
    size_t bytes = utf16_prefix_bytes(name, 1024);
    out->player_details[i].name = malloc(bytes + 1);
    if (!out->player_details[i].name) qa_range_error("Out of memory");
    memcpy(out->player_details[i].name, name, bytes);
    out->player_details[i].name[bytes] = '\0';
    out->player_details[i].score = status->player_details[i].score;
    out->player_details[i].ping = status->player_details[i].ping;
    out->player_count++;
  }
  out->wire = status->wire;
}
static bool safe_integer(double value) { return value == trunc(value) && fabs(value) <= 9007199254740991.0; }
typedef struct receive_scope { qa_app_startup_server_browser *b; packet_scope *packet; qa_server_status bounded; bool has_bounded; } receive_scope;
static void receive_status_body(void *ctx) {
  receive_scope *r = ctx;
  packet_scope *s = r->packet;
  qa_app_startup_server_browser *b = r->b;
  const qa_server_status *status = &s->status;
  if (!safe_integer(status->players) || !safe_integer(status->max_players) || status->players < 0 || status->max_players < 0 ||
      status->players > 1024 || status->max_players > 1024 || utf16_length(status->name ? status->name : "") > 1024 ||
      utf16_length(status->map ? status->map : "") > 1024)
    return;
  r->has_bounded = true;
  bounded_status(status, &r->bounded);
  qa_server_browser *core = browser_of(b, s->protocol);
  const qa_browser_entry *before = qa_server_browser_get_entry(core, s->from);
  bool was_lan = before != NULL && has_source(before, "lan");
  if (qa_server_browser_receive(core, s->from, &r->bounded, s->challenge, now_ms(), NULL)) {
    set_str(&b->status, "Server updated");
    const qa_browser_entry *after = qa_server_browser_get_entry(core, s->from);
    if (s->protocol == Q3 && !was_lan && after != NULL && has_source(after, "lan")) b->list_generations[0]++;
  }
}
static void receive_scope_release(void *arg) {
  receive_scope *r = arg;
  if (r->has_bounded) qa_server_status_destroy(&r->bounded);
  r->has_bounded = false;
}
static void receive_body(void *ctx) {
  receive_scope *r = ctx;
  qa_cleanup guard;
  qa_cleanup_push(&guard, receive_scope_release, r);
  receive_status_body(r);
  qa_cleanup_pop(&guard);
  receive_scope_release(r);
}
/* `try { ... } catch { ... }` around one packet stage: false when it raised. */
static bool packet_try(void (*body)(void *ctx), packet_scope *s) {
  qa_caught_error caught;
  bool ok = qa_try(body, s, &caught);
  packet_scope_release(s);
  return ok;
}
static void handle_packet(qa_app_startup_server_browser *b, const qa_net_address *from, const uint8_t *payload, size_t length) {
  packet_scope s;
  if (b->has_q2_master && same_key(&b->q2_master_address, from)) {
    memset(&s, 0, sizeof s);
    s.b = b; s.from = from; s.payload = payload; s.length = length;
    if (!packet_try(q2_master_body, &s)) { set_str(&b->status, "Malformed master reply"); return; }
    if (s.handled) return;
  }
  if (b->has_master && b->master_has_address && same_key(&b->master_address, from)) {
    memset(&s, 0, sizeof s);
    s.b = b; s.from = from; s.payload = payload; s.length = length;
    /* A queried master may also answer a direct server query. */
    if (packet_try(q3_master_body, &s) && s.handled) return;
  }
  char *from_key = xkey(from);
  size_t kex_at = b->kex_query_count;
  for (size_t i = 0; i < b->kex_query_count; i++) if (strcmp(b->kex_queries[i].key, from_key) == 0) kex_at = i;
  free(from_key);
  if (kex_at < b->kex_query_count) {
    memset(&s, 0, sizeof s);
    s.b = b; s.from = from; s.payload = payload; s.length = length;
    s.kex_sent = b->kex_queries[kex_at].at;
    /* A discovered endpoint may also answer a legacy query. */
    if (packet_try(kex_status_body, &s) && s.handled) {
      free(b->kex_queries[kex_at].key);
      memmove(&b->kex_queries[kex_at], &b->kex_queries[kex_at + 1], (b->kex_query_count - kex_at - 1) * sizeof *b->kex_queries);
      b->kex_query_count--;
      set_str(&b->status, "Server updated");
      return;
    }
  }
  /* Other source packets and malformed datagrams are not browser entries. */
  for (int protocol = 0; protocol < 4; protocol++) {
    memset(&s, 0, sizeof s);
    s.b = b; s.from = from; s.payload = payload; s.length = length; s.protocol = protocol;
    qa_caught_error caught;
    if (qa_try(decode_status_body, &s, &caught) && s.has_status) {
      receive_scope r = {b, &s, {0}, false};
      (void)qa_try(receive_body, &r, &caught);
    }
    packet_scope_release(&s);
  }
}
void qa_app_startup_server_browser_poll(qa_app_startup_server_browser *b) {
  qa_app_startup_server_browser_assert_open(b);
  /* KexMdns socket messages arrive through the event loop in TS; pump them here. */
  if (b->kex_open) (void)qa_kex_mdns_poll(&b->kex);
  for (;;) {
    qa_receive_event event;
    memset(&event, 0, sizeof event);
    if (!qa_udp_transport_poll(&b->transport, &event)) break;
    qa_cleanup guard;
    qa_cleanup_push(&guard, release_event, &event);
    if (event.kind == QA_RECV_ERROR) set_str(&b->status, event.error ? event.error : "");
    else if (event.kind == QA_RECV_PACKET) handle_packet(b, &event.from, event.payload, event.length);
    qa_cleanup_pop(&guard);
    qa_receive_event_destroy(&event);
  }
  for (int protocol = 0; protocol < 4; protocol++) {
    size_t expired_count = 0;
    qa_net_address *expired = qa_server_browser_expire_queries(b->browsers[protocol], now_ms(), 3000, &expired_count);
    free_addresses(expired, expired_count);
    if (expired_count > 0 && protocol == b->protocol) set_str(&b->status, "No response from server");
  }
  size_t sent = 0;
  for (size_t i = 0; i < b->pending_count;) {
    qa_server_browser *core = browser_of(b, b->pending[i].protocol);
    if (qa_server_browser_pending_requests(core) >= 16) { i++; continue; }
    pending_status query = b->pending[i];
    memmove(&b->pending[i], &b->pending[i + 1], (b->pending_count - i - 1) * sizeof *b->pending);
    b->pending_count--;
    free(query.key);
    qa_cleanup guard;
    qa_cleanup_push(&guard, release_address, &query.address);
    (void)qa_server_browser_query(core, &query.address, now_ms(), "status");
    qa_cleanup_pop(&guard);
    qa_net_address_destroy(&query.address);
    if (++sent == 4) break;
  }
  if (b->has_q2_master && now_ms() - b->q2_master_started >= 3000) clear_q2_master(b);
  if (b->has_master && now_ms() - b->master_started >= 3000) {
    clear_master(b);
    b->master_epoch++;
  }
}
void qa_app_startup_server_browser_close(qa_app_startup_server_browser *b) {
  if (b->closing || b->closed) return;
  b->closing = true;
  if (b->kex_open) qa_kex_mdns_close(&b->kex);
  b->kex_open = false;
  clear_kex_queries(b);
  clear_q2_master(b);
  clear_pending(b);
  b->master_epoch++;
  clear_master(b);
  /* this.writes.finally(...): writes complete in place, so the drain is immediate. */
  b->closed = true;
  b->generation++;
  qa_udp_transport_close(&b->transport);
}

/* ---- StartupServerBrowser as Q3BrowserHost ---- */
#define Q3_HOST_STATUS_SLOTS 4
struct qa_app_startup_q3_browser_host {
  qa_app_startup_server_browser *browser;
  qa_server_status statuses[Q3_HOST_STATUS_SLOTS]; bool status_live[Q3_HOST_STATUS_SLOTS]; size_t next_status;
  qa_q3_mirror_network_address *list; size_t list_count;
};
static char *q3_host_strdup(const char *text) { return xdup(text != NULL ? text : ""); }
static void q3_net_to_mirror(const qa_net_address *in, qa_q3_mirror_network_address *out) {
  memset(out, 0, sizeof *out);
  out->port = in->port;
  switch (in->kind) {
    case QA_NET_IPV4: out->kind = QA_Q3_ADDR_IPV4; memcpy(out->host, in->ipv4, 4); break;
    case QA_NET_IPV6: out->kind = QA_Q3_ADDR_IPV6; out->ipv6_host = q3_host_strdup(in->host); break;
    case QA_NET_LOOPBACK: out->kind = QA_Q3_ADDR_LOOPBACK; out->loopback_id = q3_host_strdup(in->host); break;
    case QA_NET_IPX: out->kind = QA_Q3_ADDR_IPX; out->network = in->network; memcpy(out->node, in->node, 6); break;
  }
}
static void q3_mirror_to_net(const qa_q3_mirror_network_address *in, qa_net_address *out) {
  memset(out, 0, sizeof *out);
  out->port = in->port;
  switch (in->kind) {
    case QA_Q3_ADDR_IPV4: out->kind = QA_NET_IPV4; memcpy(out->ipv4, in->host, 4); break;
    case QA_Q3_ADDR_IPV6: out->kind = QA_NET_IPV6; out->host = q3_host_strdup(in->ipv6_host); break;
    case QA_Q3_ADDR_LOOPBACK: out->kind = QA_NET_LOOPBACK; out->host = q3_host_strdup(in->loopback_id); break;
    case QA_Q3_ADDR_IPX: out->kind = QA_NET_IPX; out->network = (uint32_t)in->network; memcpy(out->node, in->node, 6); break;
  }
}
/* A borrowed copy of a status, valid for the next few calls (the view reads it at once). */
static const qa_server_status *q3_host_status(qa_app_startup_q3_browser_host *host, const qa_server_status *in) {
  size_t slot = host->next_status;
  host->next_status = (host->next_status + 1) % Q3_HOST_STATUS_SLOTS;
  if (host->status_live[slot]) qa_server_status_destroy(&host->statuses[slot]);
  qa_server_status_copy(in, &host->statuses[slot]);
  host->status_live[slot] = true;
  return &host->statuses[slot];
}
static bool q3_host_entry_status(void *ctx, const qa_q3_mirror_network_address *address, const qa_server_status **status_out) {
  qa_app_startup_q3_browser_host *host = ctx;
  qa_net_address net;
  q3_mirror_to_net(address, &net);
  const qa_browser_entry *entry = qa_server_browser_get_entry(qa_app_startup_server_browser_q3_core(host->browser), &net);
  qa_net_address_destroy(&net);
  if (entry == NULL || !entry->has_status) return false;
  *status_out = q3_host_status(host, &entry->status);
  return true;
}
static void q3_host_release(void *ctx, qa_q3_mirror_discovery_handle handle) {
  qa_app_startup_q3_browser_host *host = ctx;
  qa_server_browser_release_request(qa_app_startup_server_browser_q3_core(host->browser), handle);
}
static bool q3_host_result(void *ctx, qa_q3_mirror_discovery_handle handle, qa_q3_mirror_request_result *out) {
  qa_app_startup_q3_browser_host *host = ctx;
  qa_discovery_result result;
  if (!qa_server_browser_request_result(qa_app_startup_server_browser_q3_core(host->browser), handle, &result)) return false;
  memset(out, 0, sizeof *out);
  out->kind = strcmp(result.kind, "completed") == 0 ? QA_Q3_REQ_COMPLETED
      : strcmp(result.kind, "expired") == 0 ? QA_Q3_REQ_EXPIRED
      : strcmp(result.kind, "cancelled") == 0 ? QA_Q3_REQ_CANCELLED : QA_Q3_REQ_PENDING;
  if (result.has_status) out->status = q3_host_status(host, &result.status);
  out->ping_milliseconds = result.ping_ms;
  qa_net_address_destroy(&result.address);
  if (result.has_status) qa_server_status_destroy(&result.status);
  return true;
}
static qa_q3_mirror_discovery_handle q3_host_request(void *ctx, const qa_q3_mirror_network_address *address, double now,
                                                     qa_q3_discovery_request_kind kind) {
  qa_app_startup_q3_browser_host *host = ctx;
  qa_server_browser *core = qa_app_startup_server_browser_q3_core(host->browser);
  qa_net_address net;
  q3_mirror_to_net(address, &net);
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_address, &net);
  long long handle = qa_server_browser_request(core, &net, now, kind == QA_Q3_DISCOVERY_STATUS ? "status" : "info", false, false, 0);
  qa_cleanup_pop(&guard);
  qa_net_address_destroy(&net);
  return handle;
}
static void q3_host_clear_list(qa_app_startup_q3_browser_host *host) {
  for (size_t i = 0; i < host->list_count; i++) qa_q3_mirror_address_destroy(&host->list[i]);
  free(host->list);
  host->list = NULL;
  host->list_count = 0;
}
static qa_q3_browser_list_state q3_host_list(void *ctx, int64_t source) {
  qa_app_startup_q3_browser_host *host = ctx;
  qa_app_q3_list list = qa_app_startup_server_browser_q3_list(host->browser, (double)source);
  q3_host_clear_list(host);
  host->list = calloc(list.address_count == 0 ? 1 : list.address_count, sizeof *host->list);
  if (host->list == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < list.address_count; i++) {
    q3_net_to_mirror(&list.addresses[i], &host->list[i]);
    host->list_count++;
  }
  qa_q3_browser_list_state state = {host->list, host->list_count, list.pending, (int64_t)list.generation,
                                    (int64_t)list.reset_generation};
  return state;
}
static bool q3_host_resolve(void *ctx, const char *text, qa_q3_mirror_network_address *out) {
  qa_app_startup_q3_browser_host *host = ctx;
  qa_net_address net;
  if (!qa_app_startup_server_browser_resolve_q3(host->browser, text, &net)) return false;
  q3_net_to_mirror(&net, out);
  qa_net_address_destroy(&net);
  return true;
}
static void q3_host_add(void *ctx, int64_t source, const qa_q3_mirror_network_address *address) {
  qa_app_startup_q3_browser_host *host = ctx;
  qa_net_address net;
  q3_mirror_to_net(address, &net);
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_address, &net);
  qa_app_startup_server_browser_add_q3(host->browser, (double)source, &net);
  qa_cleanup_pop(&guard);
  qa_net_address_destroy(&net);
}
static void q3_host_remove(void *ctx, int64_t source, const qa_q3_mirror_network_address *address) {
  qa_app_startup_q3_browser_host *host = ctx;
  qa_net_address net;
  q3_mirror_to_net(address, &net);
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_address, &net);
  qa_app_startup_server_browser_remove_q3(host->browser, (double)source, &net);
  qa_cleanup_pop(&guard);
  qa_net_address_destroy(&net);
}
static void q3_host_scan(void *ctx) {
  qa_app_startup_server_browser_scan_q3(((qa_app_startup_q3_browser_host *)ctx)->browser);
}
static void q3_host_master(void *ctx, int64_t source, const char *remote, int64_t protocol, const char *const *keywords,
                           size_t keyword_count, qa_q3_browser_assert_fn assert_current, void *assert_ctx) {
  qa_app_startup_server_browser_request_q3_master(((qa_app_startup_q3_browser_host *)ctx)->browser, (double)source, remote,
                                                  (double)protocol, keywords, keyword_count, assert_current, assert_ctx);
}
static bool q3_host_load_cache(void *ctx, qa_q3_browser_assert_fn assert_current, void *assert_ctx,
                               qa_q3_browser_cache_view *out) {
  qa_q3_browser_cache_view *view = NULL;
  if (!qa_app_startup_server_browser_load_q3_cache(((qa_app_startup_q3_browser_host *)ctx)->browser, assert_current,
                                                   assert_ctx, &view))
    return false;
  *out = *view;
  free(view);
  return true;
}
static void q3_host_save_cache(void *ctx, const qa_q3_browser_cache_view *view) {
  qa_app_startup_server_browser_save_q3_cache(((qa_app_startup_q3_browser_host *)ctx)->browser, view);
}
static void q3_host_assert_open(void *ctx) {
  qa_app_startup_server_browser_assert_open(((qa_app_startup_q3_browser_host *)ctx)->browser);
}
qa_app_startup_q3_browser_host *qa_app_startup_server_browser_q3_host_create(qa_app_startup_server_browser *b) {
  qa_app_startup_q3_browser_host *host = calloc(1, sizeof *host);
  if (host == NULL) qa_range_error("Out of memory");
  host->browser = b;
  return host;
}
qa_q3_browser_host qa_app_startup_q3_browser_host_view(qa_app_startup_q3_browser_host *host) {
  qa_q3_browser_host view;
  memset(&view, 0, sizeof view);
  view.q3_core.entry_status = q3_host_entry_status;
  view.q3_core.release_request = q3_host_release;
  view.q3_core.request_result = q3_host_result;
  view.q3_core.request = q3_host_request;
  view.q3_core.ctx = host;
  view.q3_list = q3_host_list;
  view.resolve_q3 = q3_host_resolve;
  view.add_q3 = q3_host_add;
  view.remove_q3 = q3_host_remove;
  view.scan_q3 = q3_host_scan;
  view.request_q3_master = q3_host_master;
  view.load_q3_cache = q3_host_load_cache;
  view.save_q3_cache = q3_host_save_cache;
  view.assert_open = q3_host_assert_open;
  view.ctx = host;
  return view;
}
void qa_app_startup_server_browser_q3_host_destroy(qa_app_startup_q3_browser_host *host) {
  if (host == NULL) return;
  for (size_t i = 0; i < Q3_HOST_STATUS_SLOTS; i++) if (host->status_live[i]) qa_server_status_destroy(&host->statuses[i]);
  q3_host_clear_list(host);
  free(host);
}
