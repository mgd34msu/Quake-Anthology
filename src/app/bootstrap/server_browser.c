/* Port of src/app/bootstrap/server-browser.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/server_browser.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/support.h"
const qa_app_browser_sort_choice qa_app_browser_sort_orders[8] = {
    {"ping-low", "Ping: lowest first"}, {"ping-high", "Ping: highest first"},
    {"name-az", "Name: A to Z"}, {"name-za", "Name: Z to A"},
    {"map-az", "Map: A to Z"}, {"map-za", "Map: Z to A"},
    {"players-most", "Players: most first"}, {"players-fewest", "Players: fewest first"}};
static const char *const PROTOCOLS[4] = {"q1", "qw", "q2", "q3"};
static double protocol_port(const char *protocol) {
  return strcmp(protocol, "q1") == 0 ? 26000 : strcmp(protocol, "qw") == 0 ? 27500 : strcmp(protocol, "q2") == 0 ? 27910 : 27960;
}
static int protocol_index(const char *protocol) {
  for (int i = 0; i < 4; i++) if (strcmp(protocol, PROTOCOLS[i]) == 0) return i;
  return -1;
}
static const char *q3_source_name(double source) {
  return source == 0 ? "lan" : source == 1 ? "secondary-master" : source == 2 ? "master" : "favorite";
}
static char *xdup(const char *s) { char *r = qa_input_dup(s ? s : ""); if (!r) qa_range_error("Out of memory"); return r; }
static void set_str(char **slot, const char *value) { free(*slot); *slot = xdup(value); }
char *qa_app_browser_address(const qa_net_address *address) {
  if (address->kind != QA_NET_IPV4 && address->kind != QA_NET_IPV6) qa_range_error("Server requires an IP address");
  char *host = qa_net_address_host(address);
  if (!host) qa_range_error("Out of memory");
  char buf[1024];
  if (address->kind == QA_NET_IPV6) snprintf(buf, sizeof buf, "[%s]:%lld", host, (long long)address->port);
  else snprintf(buf, sizeof buf, "%s:%lld", host, (long long)address->port);
  free(host);
  return xdup(buf);
}
typedef struct pending_status { char *key; const char *protocol; qa_net_address address; } pending_status;
struct qa_app_startup_server_browser {
  qa_app_server_browser_host host;
  const qa_config_store *config;
  /* Landed discovery.h declares qa_server_browser opaque with no size or
   * create() (reconciliation item): allocate aligned storage generously. */
  qa_server_browser *browsers[4];
  bool browsers_live[4];
  qa_app_direct_server_address *direct[4]; size_t direct_count[4];
  char *addresses[4];
  char *master_addresses[4];
  const char *protocol;
  char *filter;
  bool favorites_only, hide_empty, hide_full;
  const char *sort_order;
  char *selected;
  char *status;
  bool closing, closed;
  double generation;
  double list_generations[4];
  double list_resets[4];
  struct { double generation; qa_net_address *addresses; size_t count; bool live; } list_snapshots[4];
  double master_epoch;
  bool has_master; double master_source; bool master_has_address; qa_net_address master_address; double master_started; bool master_received;
  bool has_q2_master; qa_net_address q2_master_address; double q2_master_started;
  bool kex_open, kex_opening;
  struct { char *key; double at; } *kex_queries; size_t kex_query_count, kex_query_cap;
  pending_status *pending; size_t pending_count, pending_cap;
  qa_q3_browser_cache_view *cached_view;
  const qa_browser_entry **row_scratch;
  char *address_scratch;
};
static bool sender_send(void *ctx, const qa_net_address *to, const uint8_t *bytes, size_t length) {
  qa_app_startup_server_browser *b = ctx;
  return (to->kind == QA_NET_IPV4 || to->kind == QA_NET_IPV6) ? b->host.send(b->host.ctx, to, bytes, length) : false;
}
static qa_server_browser *browser_of(qa_app_startup_server_browser *b, const char *protocol) {
  int index = protocol_index(protocol);
  if (index < 0) qa_range_error("Unsupported browser protocol");
  return b->browsers[index];
}
static const qa_browser_entry *entry_at(qa_server_browser *browser, size_t index) {
  return qa_server_browser_entry_at(browser, index);
}
static bool has_source(const qa_browser_entry *entry, const char *source) {
  for (size_t i = 0; i < entry->source_count; i++) if (strcmp(entry->sources[i], source) == 0) return true;
  return false;
}
static bool is_json_text(const char *text) {
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, strlen(text), true);
  if (value == NULL) return false;
  bool ok = value->kind != QA_SAVE_UNDEFINED;
  qa_save_value_free(value);
  return ok;
}
qa_app_startup_server_browser *qa_app_startup_server_browser_open(qa_app_server_browser_host host, const qa_config_store *config) {
  qa_app_startup_server_browser *b = calloc(1, sizeof *b);
  if (!b) return NULL;
  b->host = host; b->config = config;
  b->protocol = "q1";
  b->sort_order = "ping-low";
  b->filter = xdup("");
  b->status = xdup("");
  qa_packet_sender sender = {b, sender_send};
  for (int i = 0; i < 4; i++) {
    b->browsers[i] = calloc(1, 1024);
    if (!b->browsers[i]) qa_range_error("Out of memory");
    qa_server_browser_init(b->browsers[i], host.wire(host.ctx, PROTOCOLS[i]), sender);
    b->browsers_live[i] = true;
  }
  for (int i = 0; i < 4; i++) {
    char name[64];
    snprintf(name, sizeof name, "servers-%s", PROTOCOLS[i]);
    size_t len = 0;
    char *saved = qa_config_store_load_text(config, name, &len);
    if (saved != NULL && is_json_text(saved)) {
      if (len > 65536) { free(saved); qa_app_startup_server_browser_destroy(b); qa_range_error("Saved server list is too large"); }
      qa_server_browser_restore_favorites(b->browsers[i], saved);
      size_t fav_count = 0;
      qa_net_address *favs = qa_server_browser_favorite_addresses(b->browsers[i], &fav_count);
      for (size_t j = 0; j < fav_count; j++) { char *text = qa_app_browser_address(&favs[j]); free(text); qa_net_address_destroy(&favs[j]); }
      free(favs);
    }
    free(saved);
  }
  for (int i = 0; i < 4; i++) {
    char name[64];
    size_t len = 0;
    snprintf(name, sizeof name, "servers-master-%s", PROTOCOLS[i]);
    char *master = qa_config_store_load_text(config, name, &len);
    if (master != NULL) {
      char *trimmed = qa_input_js_trim(master);
      if (trimmed && strlen(trimmed) <= 2048) b->master_addresses[i] = trimmed;
      else free(trimmed);
      free(master);
    }
    snprintf(name, sizeof name, "servers-direct-%s", PROTOCOLS[i]);
    char *saved = qa_config_store_load_text(config, name, &len);
    if (saved != NULL && is_json_text(saved)) {
      size_t count = 0;
      qa_app_direct_server_address *servers = qa_app_read_direct_servers(saved, &count);
      b->direct[i] = servers;
      b->direct_count[i] = count;
      for (size_t j = 0; j < count; j++) qa_server_browser_add(b->browsers[i], &servers[j].address, "direct", 0);
      if (count > 0) b->addresses[i] = xdup(servers[0].remote);
    }
    free(saved);
  }
  qa_q3_browser_cache_view *view = NULL;
  qa_app_startup_server_browser_load_q3_cache(b, NULL, NULL, &view);
  if (view) { qa_q3_browser_cache_view_destroy(view); free(view); }
  return b;
}
void qa_app_startup_server_browser_destroy(qa_app_startup_server_browser *b) {
  if (!b) return;
  qa_app_startup_server_browser_close(b);
  for (int i = 0; i < 4; i++) {
    if (b->browsers_live[i]) { qa_server_browser_destroy(b->browsers[i]); free(b->browsers[i]); }
    qa_app_direct_servers_free(b->direct[i], b->direct_count[i]);
    free(b->addresses[i]); free(b->master_addresses[i]);
    for (size_t j = 0; j < b->list_snapshots[i].count; j++) qa_net_address_destroy(&b->list_snapshots[i].addresses[j]);
    free(b->list_snapshots[i].addresses);
  }
  for (size_t i = 0; i < b->pending_count; i++) { free(b->pending[i].key); qa_net_address_destroy(&b->pending[i].address); }
  free(b->pending);
  for (size_t i = 0; i < b->kex_query_count; i++) free(b->kex_queries[i].key);
  free(b->kex_queries);
  if (b->cached_view) { qa_q3_browser_cache_view_destroy(b->cached_view); free(b->cached_view); }
  if (b->has_master && b->master_has_address) qa_net_address_destroy(&b->master_address);
  if (b->has_q2_master) qa_net_address_destroy(&b->q2_master_address);
  free(b->filter); free(b->selected); free(b->status); free(b->row_scratch); free(b->address_scratch);
  free(b);
}
const char *qa_app_startup_server_browser_protocol(const qa_app_startup_server_browser *b) { return b->protocol; }
const char *qa_app_startup_server_browser_master_address(const qa_app_startup_server_browser *b) {
  const char *value = b->master_addresses[protocol_index(b->protocol)];
  return value ? value : "";
}
void qa_app_startup_server_browser_set_master_address(qa_app_startup_server_browser *b, const char *value) {
  set_str(&b->master_addresses[protocol_index(b->protocol)], value);
}
const char *qa_app_startup_server_browser_address(qa_app_startup_server_browser *b) {
  char *stored = b->addresses[protocol_index(b->protocol)];
  if (stored) return stored;
  free(b->address_scratch);
  char buf[128];
  snprintf(buf, sizeof buf, "localhost:%.0f", protocol_port(b->protocol));
  b->address_scratch = xdup(buf);
  return b->address_scratch;
}
void qa_app_startup_server_browser_set_address(qa_app_startup_server_browser *b, const char *value) {
  set_str(&b->addresses[protocol_index(b->protocol)], value);
}
const char *qa_app_startup_server_browser_filter(const qa_app_startup_server_browser *b) { return b->filter; }
void qa_app_startup_server_browser_set_filter(qa_app_startup_server_browser *b, const char *value) { set_str(&b->filter, value); }
bool qa_app_startup_server_browser_favorites_only(const qa_app_startup_server_browser *b) { return b->favorites_only; }
void qa_app_startup_server_browser_set_favorites_only(qa_app_startup_server_browser *b, bool value) { b->favorites_only = value; }
bool qa_app_startup_server_browser_hide_empty(const qa_app_startup_server_browser *b) { return b->hide_empty; }
void qa_app_startup_server_browser_set_hide_empty(qa_app_startup_server_browser *b, bool value) { b->hide_empty = value; }
bool qa_app_startup_server_browser_hide_full(const qa_app_startup_server_browser *b) { return b->hide_full; }
void qa_app_startup_server_browser_set_hide_full(qa_app_startup_server_browser *b, bool value) { b->hide_full = value; }
const char *qa_app_startup_server_browser_selected(const qa_app_startup_server_browser *b) { return b->selected; }
const char *qa_app_startup_server_browser_status(const qa_app_startup_server_browser *b) { return b->status; }
void qa_app_startup_server_browser_assert_open(qa_app_startup_server_browser *b) {
  if (b->closing || b->closed) qa_range_error("Server browser is closed");
}
static void assert_generation(qa_app_startup_server_browser *b, double generation, bool draining) {
  if (b->closed || (!draining && b->closing) || generation != b->generation)
    qa_range_error("Server browser operation was retired");
}
qa_server_browser *qa_app_startup_server_browser_q3_core(qa_app_startup_server_browser *b) {
  qa_app_startup_server_browser_assert_open(b);
  return browser_of(b, "q3");
}
static void clear_q3(qa_app_startup_server_browser *b, double source) {
  qa_server_browser *core = browser_of(b, "q3");
  size_t count = qa_server_browser_size(core);
  qa_net_address *addrs = calloc(count ? count : 1, sizeof *addrs);
  if (!addrs) qa_range_error("Out of memory");
  size_t n = 0;
  for (size_t i = 0; i < count; i++) {
    const qa_browser_entry *entry = entry_at(core, i);
    if (!qa_net_address_clone(&entry->address, &addrs[n])) qa_range_error("Out of memory");
    n++;
  }
  for (size_t i = 0; i < n; i++) { qa_server_browser_remove_source(core, &addrs[i], q3_source_name(source)); qa_net_address_destroy(&addrs[i]); }
  free(addrs);
  b->list_generations[(int)source]++;
  b->list_resets[(int)source]++;
}
qa_app_q3_list qa_app_startup_server_browser_q3_list(qa_app_startup_server_browser *b, double source) {
  qa_app_startup_server_browser_assert_open(b);
  int at = (int)source;
  double generation = b->list_generations[at];
  if (!b->list_snapshots[at].live || b->list_snapshots[at].generation != generation) {
    for (size_t i = 0; i < b->list_snapshots[at].count; i++) qa_net_address_destroy(&b->list_snapshots[at].addresses[i]);
    free(b->list_snapshots[at].addresses);
    qa_server_browser *core = browser_of(b, "q3");
    size_t count = qa_server_browser_size(core);
    qa_net_address *addresses = calloc(count ? count : 1, sizeof *addresses);
    if (!addresses) qa_range_error("Out of memory");
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
      const qa_browser_entry *entry = entry_at(core, i);
      if (!has_source(entry, q3_source_name(source))) continue;
      if (!qa_net_address_clone(&entry->address, &addresses[n])) qa_range_error("Out of memory");
      n++;
    }
    b->list_snapshots[at].generation = generation;
    b->list_snapshots[at].addresses = addresses;
    b->list_snapshots[at].count = n;
    b->list_snapshots[at].live = true;
  }
  qa_app_q3_list out; memset(&out, 0, sizeof out);
  out.address_count = b->list_snapshots[at].count;
  out.addresses = calloc(out.address_count ? out.address_count : 1, sizeof *out.addresses);
  if (!out.addresses) qa_range_error("Out of memory");
  for (size_t i = 0; i < out.address_count; i++)
    if (!qa_net_address_clone(&b->list_snapshots[at].addresses[i], &out.addresses[i])) qa_range_error("Out of memory");
  out.pending = b->has_master && b->master_source == source && !b->master_received;
  out.generation = generation;
  out.reset_generation = b->list_resets[at];
  return out;
}
bool qa_app_startup_server_browser_resolve_q3(qa_app_startup_server_browser *b, const char *text, qa_net_address *out) {
  qa_app_startup_server_browser_assert_open(b);
  double generation = b->generation;
  qa_net_address address; memset(&address, 0, sizeof address);
  const char *error = b->host.resolve(b->host.ctx, qa_app_direct_server_text(text), 27960, &address);
  if (error != NULL) { assert_generation(b, generation, false); return false; }
  assert_generation(b, generation, false);
  bool broadcast = address.kind == QA_NET_IPV4 && address.ipv4[0] == 255 && address.ipv4[1] == 255 && address.ipv4[2] == 255 && address.ipv4[3] == 255;
  if (address.kind != QA_NET_IPV4 || address.port == 0 || broadcast) { qa_net_address_destroy(&address); return false; }
  *out = address;
  return true;
}
void qa_app_startup_server_browser_add_q3(qa_app_startup_server_browser *b, double source, const qa_net_address *address) {
  qa_app_startup_server_browser_assert_open(b);
  if (address->kind != QA_NET_IPV4 || address->port == 0) qa_range_error("Q3 browser requires an IPv4 endpoint");
  qa_server_browser *core = browser_of(b, "q3");
  char *key = qa_net_address_key(address, true);
  size_t entries = 0;
  for (size_t i = 0; i < qa_server_browser_size(core); i++) {
    const qa_browser_entry *entry = entry_at(core, i);
    if (!has_source(entry, q3_source_name(source))) continue;
    entries++;
    char *entry_key = qa_net_address_key(&entry->address, true);
    bool same = key && entry_key && strcmp(key, entry_key) == 0;
    free(entry_key);
    if (same) { free(key); return; }
  }
  free(key);
  if (entries >= (source == 2 ? 8192u : 128u)) qa_range_error("Q3 browser list is full");
  qa_server_browser_add(core, address, q3_source_name(source), 0);
  b->list_generations[(int)source]++;
}
void qa_app_startup_server_browser_remove_q3(qa_app_startup_server_browser *b, double source, const qa_net_address *address) {
  qa_app_startup_server_browser_assert_open(b);
  qa_server_browser_remove_source(browser_of(b, "q3"), address, q3_source_name(source));
  b->list_generations[(int)source]++;
}
void qa_app_startup_server_browser_scan_q3(qa_app_startup_server_browser *b) {
  qa_app_startup_server_browser_assert_open(b);
  clear_q3(b, 0);
  qa_net_address targets[4];
  for (int i = 0; i < 4; i++) {
    int64_t host[4] = {255, 255, 255, 255};
    if (!qa_net_make_ipv4(&targets[i], host, 27960 + i, true)) qa_range_error("Out of memory");
  }
  qa_server_browser_broadcast(browser_of(b, "q3"), targets, 4, b->host.now(b->host.ctx));
  for (int i = 0; i < 4; i++) qa_net_address_destroy(&targets[i]);
}
void qa_app_startup_server_browser_request_q3_master(qa_app_startup_server_browser *b, double source, const char *remote, double protocol,
                                                     const char *const *keywords, size_t keyword_count,
                                                     void (*assert_current)(void *ctx), void *assert_ctx) {
  qa_app_startup_server_browser_assert_open(b);
  if (assert_current) assert_current(assert_ctx);
  qa_discovery_wire wire = b->host.q3_wire(b->host.ctx, keywords, keyword_count, protocol);
  size_t byte_length = 0;
  uint8_t *bytes = wire.master_query(wire.ctx, &byte_length);
  double generation = b->generation;
  double epoch = ++b->master_epoch;
  clear_q3(b, source);
  if (b->has_master && b->master_has_address) qa_net_address_destroy(&b->master_address);
  b->has_master = true; b->master_source = source; b->master_has_address = false;
  b->master_started = b->host.now(b->host.ctx); b->master_received = false;
  qa_net_address address; memset(&address, 0, sizeof address);
  const char *error = b->host.resolve(b->host.ctx, qa_app_direct_server_text(remote), 27950, &address);
  if (error != NULL) {
    if (epoch == b->master_epoch) b->has_master = false;
    free(bytes);
    qa_range_error("%s", error);
  }
  assert_generation(b, generation, false);
  if (assert_current) assert_current(assert_ctx);
  if (epoch != b->master_epoch) { qa_net_address_destroy(&address); free(bytes); return; }
  b->master_address = address; b->master_has_address = true;
  b->master_started = b->host.now(b->host.ctx);
  bool sent = b->host.send(b->host.ctx, &address, bytes, byte_length);
  free(bytes);
  if (!sent) {
    if (epoch == b->master_epoch) { b->has_master = false; qa_net_address_destroy(&b->master_address); b->master_has_address = false; }
    qa_range_error("Could not send Q3 master query");
  }
}
static qa_q3_browser_cache_view *clone_view(const qa_q3_browser_cache_view *view) {
  if (view == NULL) return NULL;
  qa_q3_browser_cache_view *copy = calloc(1, sizeof *copy);
  if (!copy) qa_range_error("Out of memory");
  copy->lists = calloc(view->list_count ? view->list_count : 1, sizeof *copy->lists);
  if (!copy->lists) qa_range_error("Out of memory");
  copy->list_count = view->list_count;
  for (size_t i = 0; i < view->list_count; i++) {
    copy->lists[i].source = view->lists[i].source;
    copy->lists[i].rows = calloc(view->lists[i].row_count ? view->lists[i].row_count : 1, sizeof *copy->lists[i].rows);
    if (!copy->lists[i].rows) qa_range_error("Out of memory");
    copy->lists[i].row_count = view->lists[i].row_count;
    for (size_t j = 0; j < view->lists[i].row_count; j++) {
      copy->lists[i].rows[j] = view->lists[i].rows[j];
      copy->lists[i].rows[j].name = xdup(view->lists[i].rows[j].name ? view->lists[i].rows[j].name : "");
      copy->lists[i].rows[j].address.loopback_id = NULL;
      copy->lists[i].rows[j].address.ipv6_host = NULL;
    }
  }
  return copy;
}
bool qa_app_startup_server_browser_load_q3_cache(qa_app_startup_server_browser *b, void (*assert_current)(void *ctx), void *assert_ctx, qa_q3_browser_cache_view **out_view) {
  *out_view = NULL;
  if (assert_current) assert_current(assert_ctx);
  double generation = b->generation;
  size_t len = 0;
  char *saved = qa_config_store_load_text(b->config, "servers-cache-q3", &len);
  assert_generation(b, generation, true);
  if (assert_current) assert_current(assert_ctx);
  if (saved == NULL) return false;
  if (!is_json_text(saved)) { free(saved); return false; }
  qa_app_q3_browser_cache cache = qa_app_read_q3_browser_cache(saved);
  free(saved);
  qa_server_browser *core = browser_of(b, "q3");
  for (double source = 1; source <= 3; source++) clear_q3(b, source);
  b->master_epoch++;
  if (b->has_master && b->master_has_address) qa_net_address_destroy(&b->master_address);
  b->has_master = false; b->master_has_address = false;
  for (size_t i = 0; i < cache.entry_count; i++) {
    qa_browser_entry entry = cache.entries[i];
    const qa_browser_entry *current = qa_server_browser_get_entry(core, &entry.address);
    if (current != NULL && current->has_status) {
      if (entry.has_status) qa_server_status_destroy(&entry.status);
      qa_server_status_copy(&current->status, &entry.status);
      entry.has_status = true;
      entry.has_ping = current->has_ping;
      entry.ping_ms = current->ping_ms;
      entry.updated_at = current->updated_at;
      qa_server_browser_restore_entry(core, &entry);
      qa_server_status_destroy(&entry.status);
      entry.has_status = false;
      /* Prevent double destroy in cache free for the copied status. */
      cache.entries[i].has_status = false;
      memset(&cache.entries[i].status, 0, sizeof cache.entries[i].status);
    } else {
      qa_server_browser_restore_entry(core, &entry);
    }
  }
  if (cache.has_view) {
    if (b->cached_view) { qa_q3_browser_cache_view_destroy(b->cached_view); free(b->cached_view); }
    b->cached_view = clone_view(&cache.view);
    *out_view = clone_view(&cache.view);
  }
  qa_app_q3_browser_cache_free(&cache);
  return *out_view != NULL;
}
static qa_browser_entry *copy_browser_list(qa_server_browser *core, size_t *out_count) {
  size_t count = qa_server_browser_size(core);
  qa_browser_entry *entries = calloc(count ? count : 1, sizeof *entries);
  if (!entries) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    const qa_browser_entry *source = entry_at(core, i);
    entries[i] = *source;
    if (!qa_net_address_clone(&source->address, &entries[i].address)) qa_range_error("Out of memory");
    entries[i].sources = calloc(source->source_count ? source->source_count : 1, sizeof *entries[i].sources);
    if (!entries[i].sources) qa_range_error("Out of memory");
    for (size_t j = 0; j < source->source_count; j++) entries[i].sources[j] = source->sources[j];
    if (source->has_status) qa_server_status_copy(&source->status, &entries[i].status);
  }
  *out_count = count;
  return entries;
}
static void free_entry_copies(qa_browser_entry *entries, size_t count) {
  for (size_t i = 0; i < count; i++) qa_app_browser_entry_dispose(&entries[i]);
  free(entries);
}
void qa_app_startup_server_browser_save_q3_cache(qa_app_startup_server_browser *b, const qa_q3_browser_cache_view *view) {
  double generation = b->generation;
  size_t count = 0;
  qa_browser_entry *entries = copy_browser_list(browser_of(b, "q3"), &count);
  char *text = qa_app_write_q3_browser_cache(entries, count, view);
  free_entry_copies(entries, count);
  qa_config_store_dump(b->config, "servers-cache-q3", text);
  free(text);
  assert_generation(b, generation, true);
  if (b->cached_view) { qa_q3_browser_cache_view_destroy(b->cached_view); free(b->cached_view); }
  b->cached_view = clone_view(view);
}
static qa_q3_browser_cache_view *current_cache_view(qa_app_startup_server_browser *b) {
  if (b->cached_view == NULL) return NULL;
  qa_q3_browser_cache_view *view = clone_view(b->cached_view);
  qa_server_browser *core = browser_of(b, "q3");
  for (size_t li = 0; li < view->list_count; li++) {
    qa_q3_browser_cache_list *list = &view->lists[li];
    const char *membership = q3_source_name((double)list->source);
    size_t kept = 0;
    for (size_t i = 0; i < list->row_count; i++) {
      qa_net_address address; memset(&address, 0, sizeof address);
      address.kind = QA_NET_IPV4; memcpy(address.ipv4, list->rows[i].address.host, 4); address.port = list->rows[i].address.port;
      const qa_browser_entry *entry = qa_server_browser_get_entry(core, &address);
      if (entry != NULL && has_source(entry, membership)) list->rows[kept++] = list->rows[i];
      else free(list->rows[i].name);
    }
    list->row_count = kept;
    size_t capacity = list->source == 2 ? 4096 : 128;
    for (size_t i = 0; i < qa_server_browser_size(core) && list->row_count < capacity; i++) {
      const qa_browser_entry *entry = entry_at(core, i);
      if (!has_source(entry, membership)) continue;
      bool retained = false;
      for (size_t j = 0; j < list->row_count; j++) {
        qa_net_address row_address; memset(&row_address, 0, sizeof row_address);
        row_address.kind = QA_NET_IPV4; memcpy(row_address.ipv4, list->rows[j].address.host, 4); row_address.port = list->rows[j].address.port;
        if (qa_net_same_address(&row_address, &entry->address, true)) retained = true;
      }
      if (retained) continue;
      list->rows = realloc(list->rows, (list->row_count + 1) * sizeof *list->rows);
      if (!list->rows) qa_range_error("Out of memory");
      qa_q3_browser_cache_row *row = &list->rows[list->row_count++];
      memset(row, 0, sizeof *row);
      row->address.kind = QA_Q3_ADDR_IPV4;
      memcpy(row->address.host, entry->address.ipv4, 4);
      row->address.port = entry->address.port;
      const char *name = entry->has_status && entry->status.name ? entry->status.name : "";
      row->name = qa_input_dup_range(name, strlen(name) > 31 ? 31 : strlen(name));
      if (!row->name) qa_range_error("Out of memory");
      row->visible = 1;
      row->ping = -1;
    }
  }
  return view;
}
void qa_app_startup_server_browser_choose(qa_app_startup_server_browser *b, const char *protocol) {
  int index = protocol_index(protocol);
  if (index < 0) qa_range_error("Unsupported server protocol");
  b->protocol = PROTOCOLS[index];
  free(b->selected); b->selected = NULL;
}
void qa_app_startup_server_browser_choose_sort(qa_app_startup_server_browser *b, const char *order) {
  for (int i = 0; i < 8; i++) if (strcmp(qa_app_browser_sort_orders[i].id, order) == 0) { b->sort_order = qa_app_browser_sort_orders[i].id; return; }
  qa_range_error("Unsupported server sort order");
}
const qa_browser_entry *qa_app_startup_server_browser_selected_entry(qa_app_startup_server_browser *b) {
  if (b->selected == NULL) return NULL;
  qa_server_browser *core = browser_of(b, b->protocol);
  for (size_t i = 0; i < qa_server_browser_size(core); i++) {
    const qa_browser_entry *entry = entry_at(core, i);
    char *key = qa_net_address_key(&entry->address, true);
    bool same = key && strcmp(key, b->selected) == 0;
    free(key);
    if (same) return entry;
  }
  return NULL;
}
static char *clean_text(const char *value) {
  char *copy = xdup(value ? value : "");
  for (char *p = copy; *p; p++) if (((unsigned char)*p < 0x20) || (unsigned char)*p == 0x7f) *p = ' ';
  return copy;
}
char **qa_app_startup_server_browser_details(qa_app_startup_server_browser *b, size_t *out_count) {
  const qa_browser_entry *entry = qa_app_startup_server_browser_selected_entry(b);
  char **lines = calloc(64, sizeof *lines);
  if (!lines) qa_range_error("Out of memory");
  size_t n = 0, cap = 64;
  if (entry == NULL) { lines[n++] = xdup("Select a server first."); *out_count = n; return lines; }
  if (!entry->has_status) {
    lines[n++] = qa_app_browser_address(&entry->address);
    lines[n++] = xdup("No status response yet.");
    *out_count = n; return lines;
  }
  const qa_server_status *status = &entry->status;
  char buf[8192];
  lines[n++] = clean_text(status->name);
  lines[n++] = qa_app_browser_address(&entry->address);
  char *map = clean_text(status->map);
  char *players = qa_input_js_number(status->players);
  char *max_players = qa_input_js_number(status->max_players);
  snprintf(buf, sizeof buf, "Map: %s  Players: %s/%s", map, players, max_players);
  free(map); free(players); free(max_players);
  lines[n++] = xdup(buf);
  for (size_t i = 0; i < status->player_count; i++) {
    if (n == cap) { cap *= 2; lines = realloc(lines, cap * sizeof *lines); if (!lines) qa_range_error("Out of memory"); }
    char *name = clean_text(status->player_details[i].name);
    char *score = qa_input_js_number(status->player_details[i].score);
    char *ping = qa_input_js_number(status->player_details[i].ping);
    snprintf(buf, sizeof buf, "%s  score %s  ping %s", name, score, ping);
    free(name); free(score); free(ping);
    lines[n++] = xdup(buf);
  }
  /* Rules sorted by key (byte order approximates localeCompare). */
  size_t *order = calloc(status->rules.count ? status->rules.count : 1, sizeof *order);
  if (!order) qa_range_error("Out of memory");
  for (size_t i = 0; i < status->rules.count; i++) order[i] = i;
  for (size_t i = 1; i < status->rules.count; i++) {
    size_t at = order[i]; size_t j = i;
    while (j > 0 && strcmp(status->rules.entries[order[j - 1]].key, status->rules.entries[at].key) > 0) { order[j] = order[j - 1]; j--; }
    order[j] = at;
  }
  for (size_t i = 0; i < status->rules.count; i++) {
    if (n == cap) { cap *= 2; lines = realloc(lines, cap * sizeof *lines); if (!lines) qa_range_error("Out of memory"); }
    char *key = clean_text(status->rules.entries[order[i]].key);
    char *value = clean_text(status->rules.entries[order[i]].value);
    snprintf(buf, sizeof buf, "%s: %s", key, value);
    free(key); free(value);
    lines[n++] = xdup(buf);
  }
  free(order);
  *out_count = n;
  return lines;
}
static void queue_status(qa_app_startup_server_browser *b, const char *protocol, const qa_net_address *address) {
  char *address_key = qa_net_address_key(address, true);
  char key[1024];
  snprintf(key, sizeof key, "%s:%s", protocol, address_key ? address_key : "");
  free(address_key);
  for (size_t i = 0; i < b->pending_count; i++) if (strcmp(b->pending[i].key, key) == 0) {
    qa_net_address_destroy(&b->pending[i].address);
    if (!qa_net_address_clone(address, &b->pending[i].address)) qa_range_error("Out of memory");
    b->pending[i].protocol = protocol;
    return;
  }
  if (b->pending_count == b->pending_cap) { b->pending_cap = b->pending_cap ? b->pending_cap * 2 : 16; b->pending = realloc(b->pending, b->pending_cap * sizeof *b->pending); if (!b->pending) qa_range_error("Out of memory"); }
  pending_status *entry = &b->pending[b->pending_count++];
  entry->key = xdup(key);
  entry->protocol = protocol;
  if (!qa_net_address_clone(address, &entry->address)) qa_range_error("Out of memory");
}
void qa_app_startup_server_browser_discover(qa_app_startup_server_browser *b) {
  qa_app_startup_server_browser_assert_open(b);
  const char *protocol = b->protocol;
  char *remote_owned = qa_input_js_trim(qa_app_startup_server_browser_master_address(b));
  if (!remote_owned) qa_range_error("Out of memory");
  const char *remote = remote_owned;
  double generation = b->generation;
  if (remote[0] == '\0') { free(remote_owned); qa_range_error("Enter a master address or HTTP list URL"); }
  bool http = strncasecmp(remote, "http://", 7) == 0 || strncasecmp(remote, "https://", 8) == 0;
  if (http) {
    size_t count = 0; const char *error = NULL;
    char **names = b->host.fetch_master_list(b->host.ctx, remote, &count, &error);
    if (error != NULL) { free(remote_owned); qa_range_error("%s", error); }
    qa_net_address *addresses = calloc(count ? count : 1, sizeof *addresses);
    if (!addresses) qa_range_error("Out of memory");
    size_t address_count = 0;
    for (size_t i = 0; i < count; i++) {
      qa_net_address address; memset(&address, 0, sizeof address);
      const char *resolve_error = b->host.resolve(b->host.ctx, names[i], (int64_t)protocol_port(protocol), &address);
      if (resolve_error != NULL) {
        for (size_t j = 0; j < address_count; j++) qa_net_address_destroy(&addresses[j]);
        free(addresses);
        for (size_t j = 0; j < count; j++) free(names[j]);
        free(names); free(remote_owned);
        qa_range_error("%s", resolve_error);
      }
      addresses[address_count++] = address;
      free(names[i]);
    }
    free(names);
    assert_generation(b, generation, false);
    qa_server_browser *core = browser_of(b, protocol);
    for (size_t i = 0; i < address_count; i++) {
      if (qa_server_browser_size(core) >= 4096 && qa_server_browser_get_entry(core, &addresses[i]) == NULL) break;
      if (strcmp(protocol, "q3") == 0) qa_app_startup_server_browser_add_q3(b, 2, &addresses[i]);
      else qa_server_browser_add(core, &addresses[i], "master", 0);
      queue_status(b, protocol, &addresses[i]);
    }
    char buf[128];
    snprintf(buf, sizeof buf, "Found %zu servers; querying status", address_count);
    set_str(&b->status, buf);
    for (size_t i = 0; i < address_count; i++) qa_net_address_destroy(&addresses[i]);
    free(addresses);
  } else if (strcmp(protocol, "q2") == 0) {
    const char *target = strncmp(remote, "udp://", 6) == 0 ? remote + 6 : remote;
    qa_net_address address; memset(&address, 0, sizeof address);
    const char *error = b->host.resolve(b->host.ctx, qa_app_direct_server_text(target), 27900, &address);
    if (error != NULL) { free(remote_owned); qa_range_error("%s", error); }
    assert_generation(b, generation, false);
    if (b->has_q2_master) qa_net_address_destroy(&b->q2_master_address);
    b->q2_master_address = address;
    b->has_q2_master = true;
    b->q2_master_started = b->host.now(b->host.ctx);
    if (!qa_server_browser_query_master(browser_of(b, protocol), &address)) { free(remote_owned); qa_range_error("Could not send master query"); }
    set_str(&b->status, "Querying master...");
  } else if (strcmp(protocol, "q3") == 0) {
    const char *target = strncmp(remote, "udp://", 6) == 0 ? remote + 6 : remote;
    const char *keywords[] = {"empty", "full"};
    char *target_copy = xdup(target);
    free(remote_owned);
    remote_owned = target_copy;
    qa_app_startup_server_browser_request_q3_master(b, 2, target_copy, 68, keywords, 2, NULL, NULL);
    set_str(&b->status, "Querying master...");
  } else {
    free(remote_owned);
    qa_range_error("Use an HTTP master list for Quake or QuakeWorld");
  }
  char name[64];
  snprintf(name, sizeof name, "servers-master-%s", protocol);
  assert_generation(b, generation, false);
  qa_config_store_dump(b->config, name, remote);
  assert_generation(b, generation, false);
  free(remote_owned);
}
const char *qa_app_startup_server_browser_empty_message(qa_app_startup_server_browser *b) {
  return qa_server_browser_size(browser_of(b, b->protocol)) == 0
             ? "No servers yet. Enter an address or find LAN."
             : "No servers match these filters.";
}
static char *lower_dup(const char *text) {
  char *copy = xdup(text ? text : "");
  for (char *p = copy; *p; p++) *p = (char)tolower((unsigned char)*p);
  return copy;
}
static int entry_compare(qa_app_startup_server_browser *b, const qa_browser_entry *a, const qa_browser_entry *c) {
  if (a->has_status != c->has_status) return a->has_status ? -1 : 1;
  double compared = 0;
  const char *order_id = b->sort_order;
  if (strcmp(order_id, "ping-low") == 0 || strcmp(order_id, "ping-high") == 0) {
    if (a->has_ping != c->has_ping) return a->has_ping ? -1 : 1;
    compared = ((a->has_ping ? a->ping_ms : 0) - (c->has_ping ? c->ping_ms : 0)) * (strcmp(order_id, "ping-low") == 0 ? 1 : -1);
  } else if (strcmp(order_id, "name-az") == 0 || strcmp(order_id, "name-za") == 0) {
    compared = strcasecmp(a->has_status && a->status.name ? a->status.name : "", c->has_status && c->status.name ? c->status.name : "") * (strcmp(order_id, "name-az") == 0 ? 1 : -1);
  } else if (strcmp(order_id, "map-az") == 0 || strcmp(order_id, "map-za") == 0) {
    compared = strcasecmp(a->has_status && a->status.map ? a->status.map : "", c->has_status && c->status.map ? c->status.map : "") * (strcmp(order_id, "map-az") == 0 ? 1 : -1);
  } else {
    compared = ((a->has_status ? a->status.players : 0) - (c->has_status ? c->status.players : 0)) * (strcmp(order_id, "players-fewest") == 0 ? 1 : -1);
  }
  if (compared != 0) return compared < 0 ? -1 : 1;
  char *left = qa_app_browser_address(&a->address);
  char *right = qa_app_browser_address(&c->address);
  int result = strcmp(left, right);
  free(left); free(right);
  return result < 0 ? -1 : result > 0 ? 1 : 0;
}
const qa_browser_entry **qa_app_startup_server_browser_rows(qa_app_startup_server_browser *b, size_t *out_count) {
  char *trimmed = qa_input_js_trim(b->filter);
  char *search = lower_dup(trimmed ? trimmed : "");
  free(trimmed);
  qa_server_browser *core = browser_of(b, b->protocol);
  size_t total = qa_server_browser_size(core);
  free(b->row_scratch);
  b->row_scratch = calloc(total ? total : 1, sizeof *b->row_scratch);
  if (!b->row_scratch) qa_range_error("Out of memory");
  size_t n = 0;
  for (size_t i = 0; i < total; i++) {
    const qa_browser_entry *entry = entry_at(core, i);
    if (b->favorites_only && !has_source(entry, "favorite")) continue;
    if (entry->has_status && entry->status.max_players > 0) {
      if (b->hide_empty && entry->status.players <= 0) continue;
      if (b->hide_full && entry->status.players >= entry->status.max_players) continue;
    }
    char *address = qa_app_browser_address(&entry->address);
    const char *gamedir = entry->has_status ? qa_qw_info_get(&entry->status.rules, "gamedir") : NULL;
    if (gamedir == NULL && entry->has_status) gamedir = qa_qw_info_get(&entry->status.rules, "game");
    char *names = xdup("");
    if (entry->has_status) {
      for (size_t j = 0; j < entry->status.player_count; j++) {
        char *clean = xdup(entry->status.player_details[j].name ? entry->status.player_details[j].name : "");
        /* strip ^<digit> color codes */
        char *read = clean, *write = clean;
        while (*read) {
          if (read[0] == '^' && isdigit((unsigned char)read[1])) { read += 2; continue; }
          *write++ = *read++;
        }
        *write = '\0';
        size_t need = strlen(names) + strlen(clean) + 2;
        char *grown = realloc(names, need);
        if (!grown) qa_range_error("Out of memory");
        names = grown;
        strcat(names, clean); strcat(names, " ");
        free(clean);
      }
    }
    char haystack[8192];
    snprintf(haystack, sizeof haystack, "%s %s %s %s %s",
             entry->has_status && entry->status.name ? entry->status.name : "",
             entry->has_status && entry->status.map ? entry->status.map : "",
             gamedir ? gamedir : "", names, address);
    free(names); free(address);
    char *lower = lower_dup(haystack);
    bool match = search[0] == '\0' || strstr(lower, search) != NULL;
    free(lower);
    if (match) b->row_scratch[n++] = entry;
  }
  free(search);
  /* Insertion sort (stable index movement). */
  for (size_t i = 1; i < n; i++) {
    const qa_browser_entry *at = b->row_scratch[i];
    size_t j = i;
    while (j > 0 && entry_compare(b, b->row_scratch[j - 1], at) > 0) { b->row_scratch[j] = b->row_scratch[j - 1]; j--; }
    b->row_scratch[j] = at;
  }
  *out_count = n;
  return b->row_scratch;
}
void qa_app_startup_server_browser_select(qa_app_startup_server_browser *b, const char *key) {
  size_t count = 0;
  const qa_browser_entry **rows = qa_app_startup_server_browser_rows(b, &count);
  const qa_browser_entry *entry = NULL;
  for (size_t i = 0; i < count; i++) {
    char *entry_key = qa_net_address_key(&rows[i]->address, true);
    bool same = entry_key && strcmp(entry_key, key) == 0;
    free(entry_key);
    if (same) entry = rows[i];
  }
  if (entry == NULL) return;
  const char *direct_remote = NULL;
  if (has_source(entry, "direct")) {
    int index = protocol_index(b->protocol);
    for (size_t i = 0; i < b->direct_count[index]; i++) {
      char *server_key = qa_net_address_key(&b->direct[index][i].address, true);
      if (server_key && strcmp(server_key, key) == 0) direct_remote = b->direct[index][i].remote;
      free(server_key);
    }
  }
  set_str(&b->selected, key);
  if (direct_remote != NULL) qa_app_startup_server_browser_set_address(b, direct_remote);
  else {
    char *address = qa_app_browser_address(&entry->address);
    qa_app_startup_server_browser_set_address(b, address);
    free(address);
  }
}
static void remember_direct(qa_app_startup_server_browser *b, const char *protocol, const char *remote, const qa_net_address *address) {
  double generation = b->generation;
  int index = protocol_index(protocol);
  size_t count = 0;
  qa_app_direct_server_address *servers = calloc(QA_APP_MAXIMUM_DIRECT_SERVERS, sizeof *servers);
  if (!servers) qa_range_error("Out of memory");
  servers[count].remote = xdup(remote);
  if (!qa_net_address_clone(address, &servers[count].address)) qa_range_error("Out of memory");
  count++;
  char *new_key = qa_net_address_key(address, true);
  for (size_t i = 0; i < b->direct_count[index] && count < QA_APP_MAXIMUM_DIRECT_SERVERS; i++) {
    char *key = qa_net_address_key(&b->direct[index][i].address, true);
    bool dup = key && new_key && strcmp(key, new_key) == 0;
    free(key);
    if (dup) continue;
    servers[count].remote = xdup(b->direct[index][i].remote);
    if (!qa_net_address_clone(&b->direct[index][i].address, &servers[count].address)) qa_range_error("Out of memory");
    count++;
  }
  free(new_key);
  /* Serialize {"version":1,"servers":[...]} + "\n" */
  char *text = xdup("{\"version\":1,\"servers\":[");
  for (size_t i = 0; i < count; i++) {
    char *grown_prefix = NULL;
    size_t need = strlen(text) + 1024;
    grown_prefix = realloc(text, need);
    if (!grown_prefix) qa_range_error("Out of memory");
    text = grown_prefix;
    char *address_text = qa_app_browser_address(&servers[i].address);
    (void)address_text;
    /* Address record via browser-address JSON shape. */
    char record[1024];
    if (servers[i].address.kind == QA_NET_IPV4)
      snprintf(record, sizeof record, "%s{\"remote\":\"%s\",\"address\":{\"kind\":\"ipv4\",\"host\":[%u,%u,%u,%u],\"port\":%lld}}",
               i ? "," : "", servers[i].remote, servers[i].address.ipv4[0], servers[i].address.ipv4[1], servers[i].address.ipv4[2], servers[i].address.ipv4[3], (long long)servers[i].address.port);
    else
      snprintf(record, sizeof record, "%s{\"remote\":\"%s\",\"address\":{\"kind\":\"ipv6\",\"host\":\"%s\",\"port\":%lld}}",
               i ? "," : "", servers[i].remote, servers[i].address.host ? servers[i].address.host : "", (long long)servers[i].address.port);
    strcat(text, record);
    free(address_text);
  }
  size_t need = strlen(text) + 4;
  char *grown = realloc(text, need);
  if (!grown) qa_range_error("Out of memory");
  text = grown;
  strcat(text, "]}\n");
  char name[64];
  snprintf(name, sizeof name, "servers-direct-%s", protocol);
  qa_config_store_dump(b->config, name, text);
  free(text);
  assert_generation(b, generation, true);
  qa_app_direct_servers_free(b->direct[index], b->direct_count[index]);
  b->direct[index] = servers;
  b->direct_count[index] = count;
  qa_net_address first; memset(&first, 0, sizeof first);
  if (!qa_net_address_clone(address, &first)) qa_range_error("Out of memory");
  qa_server_browser_add(browser_of(b, protocol), &first, "direct", 0);
  qa_net_address_destroy(&first);
}
void qa_app_startup_server_browser_query(qa_app_startup_server_browser *b) {
  const char *protocol = b->protocol;
  char *trimmed = qa_input_js_trim(qa_app_startup_server_browser_address(b));
  const char *remote = qa_app_direct_server_text(trimmed ? trimmed : "");
  double generation = b->generation;
  qa_net_address address; memset(&address, 0, sizeof address);
  const char *error = b->host.resolve(b->host.ctx, remote, (int64_t)protocol_port(protocol), &address);
  if (error != NULL) { free(trimmed); qa_range_error("%s", error); }
  assert_generation(b, generation, true);
  remember_direct(b, protocol, remote, &address);
  assert_generation(b, generation, true);
  bool sent = qa_server_browser_query(browser_of(b, protocol), &address, b->host.now(b->host.ctx), "status");
  qa_net_address_destroy(&address);
  free(trimmed);
  if (!sent) qa_range_error("Could not send server query");
  set_str(&b->status, "Query sent");
}
void qa_app_startup_server_browser_scan(qa_app_startup_server_browser *b) {
  qa_app_startup_server_browser_assert_open(b);
  if (strcmp(b->protocol, "q3") == 0) {
    qa_app_startup_server_browser_scan_q3(b);
    set_str(&b->status, "Searching local network...");
    return;
  }
  if (strcmp(b->protocol, "q2") == 0) {
    if (b->kex_open) b->host.kex_query(b->host.ctx);
    else if (!b->kex_opening) {
      b->kex_opening = true;
      bool opened = b->host.kex_open(b->host.ctx);
      b->kex_opening = false;
      if (opened) {
        if (b->closing || b->closed) b->host.kex_close(b->host.ctx);
        else { b->kex_open = true; b->host.kex_query(b->host.ctx); }
      }
    }
  }
  qa_net_address target; memset(&target, 0, sizeof target);
  int64_t host[4] = {255, 255, 255, 255};
  if (!qa_net_make_ipv4(&target, host, (int64_t)protocol_port(b->protocol), true)) qa_range_error("Out of memory");
  qa_server_browser_broadcast(browser_of(b, b->protocol), &target, 1, b->host.now(b->host.ctx));
  qa_net_address_destroy(&target);
  set_str(&b->status, "Searching local network...");
}
void qa_app_startup_server_browser_kex_found(qa_app_startup_server_browser *b, const qa_net_address *address) {
  if (b->closing || b->closed) return;
  char *key = qa_net_address_key(address, true);
  double now = b->host.now(b->host.ctx);
  for (size_t i = 0; i < b->kex_query_count; i++)
    if (key && strcmp(b->kex_queries[i].key, key) == 0 && now - b->kex_queries[i].at < 1000) { free(key); return; }
  if (b->kex_query_count >= 256) { free(key); return; }
  bool replaced = false;
  for (size_t i = 0; i < b->kex_query_count; i++) if (key && strcmp(b->kex_queries[i].key, key) == 0) { b->kex_queries[i].at = now; replaced = true; }
  if (!replaced) {
    if (b->kex_query_count == b->kex_query_cap) { b->kex_query_cap = b->kex_query_cap ? b->kex_query_cap * 2 : 16; b->kex_queries = realloc(b->kex_queries, b->kex_query_cap * sizeof *b->kex_queries); if (!b->kex_queries) qa_range_error("Out of memory"); }
    b->kex_queries[b->kex_query_count].key = key;
    b->kex_queries[b->kex_query_count].at = now;
    b->kex_query_count++;
  } else free(key);
  size_t length = 0;
  const uint8_t *query = b->host.kex_discovery_query(b->host.ctx, &length);
  b->host.send(b->host.ctx, address, query, length);
}
void qa_app_startup_server_browser_kex_failed(qa_app_startup_server_browser *b, const char *message) {
  if (!b->closing && !b->closed) set_str(&b->status, message);
}
void qa_app_startup_server_browser_favorite(qa_app_startup_server_browser *b) {
  const char *protocol = b->protocol;
  char *trimmed = qa_input_js_trim(qa_app_startup_server_browser_address(b));
  const char *remote = qa_app_direct_server_text(trimmed ? trimmed : "");
  double generation = b->generation;
  qa_net_address address; memset(&address, 0, sizeof address);
  const char *error = b->host.resolve(b->host.ctx, remote, (int64_t)protocol_port(protocol), &address);
  free(trimmed);
  if (error != NULL) qa_range_error("%s", error);
  assert_generation(b, generation, true);
  qa_server_browser *core = browser_of(b, protocol);
  char *before = qa_server_browser_save_favorites(core);
  const qa_browser_entry *existing = qa_server_browser_get_entry(core, &address);
  bool removing = existing != NULL && has_source(existing, "favorite");
  if (removing) qa_server_browser_remove_favorite(core, &address);
  else qa_server_browser_add(core, &address, "favorite", 0);
  bool q3 = strcmp(protocol, "q3") == 0;
  if (q3) b->list_generations[3]++;
  qa_q3_browser_cache_view *view = q3 ? current_cache_view(b) : NULL;
  char *text = NULL;
  if (q3) {
    size_t count = 0;
    qa_browser_entry *entries = copy_browser_list(core, &count);
    text = qa_app_write_q3_browser_cache(entries, count, view);
    free_entry_copies(entries, count);
  } else text = qa_server_browser_save_favorites(core);
  char name[64];
  snprintf(name, sizeof name, "%s", q3 ? "servers-cache-q3" : "");
  if (!q3) snprintf(name, sizeof name, "servers-%s", protocol);
  bool dumped = qa_config_store_dump(b->config, name, text);
  free(text);
  if (!dumped) {
    qa_server_browser_restore_favorites(core, before);
    if (q3) b->list_generations[3]++;
    free(before);
    qa_net_address_destroy(&address);
    if (view) { qa_q3_browser_cache_view_destroy(view); free(view); }
    qa_range_error("Could not save favorites");
  }
  free(before);
  qa_net_address_destroy(&address);
  assert_generation(b, generation, true);
  if (q3) {
    if (b->cached_view) { qa_q3_browser_cache_view_destroy(b->cached_view); free(b->cached_view); }
    b->cached_view = view;
  }
  set_str(&b->status, removing ? "Favorite removed" : "Favorite added");
}
qa_app_browser_connection qa_app_startup_server_browser_connection(qa_app_startup_server_browser *b) {
  const char *protocol = b->protocol;
  char *trimmed = qa_input_js_trim(qa_app_startup_server_browser_address(b));
  const char *remote = qa_app_direct_server_text(trimmed ? trimmed : "");
  double generation = b->generation;
  qa_net_address address; memset(&address, 0, sizeof address);
  const char *error = b->host.resolve(b->host.ctx, remote, (int64_t)protocol_port(protocol), &address);
  if (error != NULL) { free(trimmed); qa_range_error("%s", error); }
  assert_generation(b, generation, true);
  remember_direct(b, protocol, remote, &address);
  assert_generation(b, generation, true);
  qa_net_address_destroy(&address);
  qa_server_browser *core = browser_of(b, protocol);
  const qa_browser_entry *selected = NULL;
  for (size_t i = 0; i < qa_server_browser_size(core); i++) {
    char *text = qa_app_browser_address(&entry_at(core, i)->address);
    bool same = strcmp(text, remote) == 0;
    free(text);
    if (same) selected = entry_at(core, i);
  }
  qa_app_browser_connection connection; memset(&connection, 0, sizeof connection);
  connection.protocol = protocol;
  connection.remote = xdup(remote);
  free(trimmed);
  if (strcmp(protocol, "q2") == 0 && selected != NULL && selected->has_status && !selected->status.wire.is_unified) {
    connection.has_q2_protocol = true;
    connection.q2_protocol_kind = "q2-kex";
    connection.q2_protocol_version = 2023;
  }
  (void)b;
  return connection;
}
static bool same_key_text(const qa_net_address *a, const qa_net_address *b) {
  char *left = qa_net_address_key(a, true);
  char *right = qa_net_address_key(b, true);
  bool same = left && right && strcmp(left, right) == 0;
  free(left); free(right);
  return same;
}
void qa_app_startup_server_browser_poll(qa_app_startup_server_browser *b) {
  qa_app_startup_server_browser_assert_open(b);
  for (;;) {
    qa_net_address from; memset(&from, 0, sizeof from);
    const uint8_t *payload = NULL; size_t length = 0; const char *error = NULL;
    int event = b->host.poll(b->host.ctx, &from, &payload, &length, &error);
    if (event == 0) break;
    if (event == 2) { set_str(&b->status, error ? error : ""); qa_net_address_destroy(&from); continue; }
    if (event != 1) { qa_net_address_destroy(&from); continue; }
    if (b->has_q2_master && same_key_text(&b->q2_master_address, &from)) {
      size_t count = 0; bool malformed = false;
      qa_net_address *addresses = b->host.read_q2_master_reply(b->host.ctx, payload, length, &count, &malformed);
      if (malformed) { set_str(&b->status, "Malformed master reply"); qa_net_address_destroy(&from); continue; }
      if (addresses != NULL) {
        qa_server_browser *core = browser_of(b, "q2");
        for (size_t i = 0; i < count; i++) {
          if (qa_server_browser_size(core) >= 4096 && qa_server_browser_get_entry(core, &addresses[i]) == NULL) break;
          qa_server_browser_add(core, &addresses[i], "master", 0);
          queue_status(b, "q2", &addresses[i]);
        }
        char buf[128];
        snprintf(buf, sizeof buf, "Found %zu servers; querying status", count);
        set_str(&b->status, buf);
        for (size_t i = 0; i < count; i++) qa_net_address_destroy(&addresses[i]);
        free(addresses);
        qa_net_address_destroy(&from);
        continue;
      }
    }
    if (b->has_master && b->master_has_address && same_key_text(&b->master_address, &from)) {
      size_t count = 0; bool complete = false, failed = false;
      qa_net_address *addresses = b->host.decode_q3_master_packet(b->host.ctx, payload, length, &count, &complete, &failed);
      if (!failed) {
        double source = b->master_source;
        for (size_t i = 0; i < count; i++) {
          qa_app_q3_list list = qa_app_startup_server_browser_q3_list(b, source);
          size_t existing = list.address_count;
          for (size_t j = 0; j < list.address_count; j++) qa_net_address_destroy(&list.addresses[j]);
          free(list.addresses);
          if (existing >= (source == 2 ? 8192u : 128u)) break;
          qa_app_startup_server_browser_add_q3(b, source, &addresses[i]);
          queue_status(b, "q3", &addresses[i]);
        }
        for (size_t i = 0; i < count; i++) qa_net_address_destroy(&addresses[i]);
        free(addresses);
        if (complete) { qa_net_address_destroy(&b->master_address); b->has_master = false; b->master_has_address = false; memset(&b->master_address, 0, sizeof b->master_address); }
        else b->master_received = true;
        qa_net_address_destroy(&from);
        continue;
      }
      for (size_t i = 0; i < count; i++) qa_net_address_destroy(&addresses[i]);
      free(addresses);
    }
    char *from_key = qa_net_address_key(&from, true);
    double kex_sent = 0; bool has_kex = false; size_t kex_index = 0;
    for (size_t i = 0; i < b->kex_query_count; i++) if (from_key && strcmp(b->kex_queries[i].key, from_key) == 0) { has_kex = true; kex_sent = b->kex_queries[i].at; kex_index = i; }
    if (has_kex) {
      qa_server_status status; memset(&status, 0, sizeof status);
      if (b->host.read_kex_discovery(b->host.ctx, payload, length, &status)) {
        qa_server_browser *core = browser_of(b, "q2");
        double now = b->host.now(b->host.ctx);
        qa_server_browser_add(core, &from, "lan", now);
        qa_browser_entry restored; memset(&restored, 0, sizeof restored);
        const qa_browser_entry *current = qa_server_browser_get_entry(core, &from);
        if (current != NULL) {
          restored = *current;
          if (!qa_net_address_clone(&from, &restored.address)) qa_range_error("Out of memory");
          restored.sources = calloc(current->source_count ? current->source_count : 1, sizeof *restored.sources);
          if (!restored.sources) qa_range_error("Out of memory");
          for (size_t i = 0; i < current->source_count; i++) restored.sources[i] = current->sources[i];
          restored.has_status = true;
          restored.status = status;
          restored.has_ping = true;
          double ping = now - kex_sent;
          restored.ping_ms = ping > 0 ? ping : 0;
          restored.updated_at = now;
          qa_server_browser_restore_entry(core, &restored);
          qa_app_browser_entry_dispose(&restored);
        } else qa_server_status_destroy(&status);
        free(b->kex_queries[kex_index].key);
        memmove(&b->kex_queries[kex_index], &b->kex_queries[kex_index + 1], (b->kex_query_count - kex_index - 1) * sizeof *b->kex_queries);
        b->kex_query_count--;
        set_str(&b->status, "Server updated");
        free(from_key);
        qa_net_address_destroy(&from);
        continue;
      }
    }
    free(from_key);
    for (int pi = 0; pi < 4; pi++) {
      qa_server_status status; memset(&status, 0, sizeof status);
      const char *challenge = NULL;
      if (!b->host.decode_status(b->host.ctx, PROTOCOLS[pi], payload, length, &status, &challenge)) continue;
      bool valid = status.players == trunc(status.players) && status.max_players == trunc(status.max_players)
          && status.players >= 0 && status.max_players >= 0 && status.players <= 1024 && status.max_players <= 1024
          && strlen(status.name ? status.name : "") <= 1024 && strlen(status.map ? status.map : "") <= 1024;
      if (valid) {
        char *name = clean_text(status.name);
        free(status.name); status.name = name;
        char *map = clean_text(status.map);
        free(status.map); status.map = map;
        qa_server_browser *core = b->browsers[pi];
        const qa_browser_entry *before = qa_server_browser_get_entry(core, &from);
        bool was_lan = before != NULL && has_source(before, "lan");
        if (qa_server_browser_receive(core, &from, &status, challenge, b->host.now(b->host.ctx), NULL)) {
          set_str(&b->status, "Server updated");
          if (pi == 3 && !was_lan) {
            const qa_browser_entry *after = qa_server_browser_get_entry(core, &from);
            if (after != NULL && has_source(after, "lan")) b->list_generations[0]++;
          }
        }
      }
      qa_server_status_destroy(&status);
    }
    qa_net_address_destroy(&from);
  }
  for (int pi = 0; pi < 4; pi++) {
    size_t expired_count = 0;
    qa_net_address *expired = qa_server_browser_expire_queries(b->browsers[pi], b->host.now(b->host.ctx), 3000, &expired_count);
    for (size_t i = 0; i < expired_count; i++) qa_net_address_destroy(&expired[i]);
    free(expired);
    if (expired_count > 0 && strcmp(PROTOCOLS[pi], b->protocol) == 0) set_str(&b->status, "No response from server");
  }
  size_t sent = 0;
  for (size_t i = 0; i < b->pending_count && sent < 4;) {
    pending_status query = b->pending[i];
    qa_server_browser *core = browser_of(b, query.protocol);
    if (qa_server_browser_pending_requests(core) >= 16) { i++; continue; }
    free(query.key);
    memmove(&b->pending[i], &b->pending[i + 1], (b->pending_count - i - 1) * sizeof(pending_status));
    b->pending_count--;
    qa_server_browser_query(core, &query.address, b->host.now(b->host.ctx), "status");
    qa_net_address_destroy(&query.address);
    sent++;
  }
  if (b->has_q2_master && b->host.now(b->host.ctx) - b->q2_master_started >= 3000) {
    qa_net_address_destroy(&b->q2_master_address);
    b->has_q2_master = false;
    memset(&b->q2_master_address, 0, sizeof b->q2_master_address);
  }
  if (b->has_master && b->host.now(b->host.ctx) - b->master_started >= 3000) {
    if (b->master_has_address) qa_net_address_destroy(&b->master_address);
    memset(&b->master_address, 0, sizeof b->master_address);
    b->master_has_address = false;
    b->has_master = false;
    b->master_epoch++;
  }
}
void qa_app_startup_server_browser_close(qa_app_startup_server_browser *b) {
  if (b->closed || b->closing) return;
  b->closing = true;
  if (b->kex_open) { b->host.kex_close(b->host.ctx); b->kex_open = false; }
  for (size_t i = 0; i < b->kex_query_count; i++) free(b->kex_queries[i].key);
  b->kex_query_count = 0;
  if (b->has_q2_master) { qa_net_address_destroy(&b->q2_master_address); b->has_q2_master = false; memset(&b->q2_master_address, 0, sizeof b->q2_master_address); }
  for (size_t i = 0; i < b->pending_count; i++) { free(b->pending[i].key); qa_net_address_destroy(&b->pending[i].address); }
  b->pending_count = 0;
  b->master_epoch++;
  if (b->has_master && b->master_has_address) { qa_net_address_destroy(&b->master_address); memset(&b->master_address, 0, sizeof b->master_address); b->master_has_address = false; }
  b->has_master = false;
  b->closed = true;
  b->generation++;
  b->host.transport_close(b->host.ctx);
}
