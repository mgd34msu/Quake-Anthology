/* Port of src/app/bootstrap/server-browser.ts over the landed
 * ServerBrowser cores. UDP transport, protocol wires/decoders, DNS and
 * KEX mDNS (network slices) are host fn+ctx; browser state, Q3 list
 * generations, filtering/sorting, persistence and master flows are
 * kept here. Async -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SERVER_BROWSER_H
#define QA_APP_BOOTSTRAP_SERVER_BROWSER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/server_browser_addresses.h"
#include "qa/app/bootstrap/server_browser_cache.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/q3/browser_view.h"
#include "qa/network/services/discovery.h"
#include "qa/settings/config.h"
/* TS BrowserProtocol / BrowserSortOrder exact strings. */
typedef const char *qa_app_browser_protocol;
typedef const char *qa_app_browser_sort_order;
typedef struct qa_app_browser_sort_choice { const char *id; const char *label; } qa_app_browser_sort_choice;
extern const qa_app_browser_sort_choice qa_app_browser_sort_orders[8];
typedef struct qa_app_browser_connection {
  const char *protocol; char *remote;
  bool has_q2_protocol; const char *q2_protocol_kind; double q2_protocol_version;
} qa_app_browser_connection;
char *qa_app_browser_address(const qa_net_address *address); /* malloc'd */
typedef struct qa_app_server_browser_host {
  void *ctx;
  qa_discovery_wire (*wire)(void *ctx, const char *protocol);
  qa_discovery_wire (*q3_wire)(void *ctx, const char *const *keywords, size_t count, double protocol);
  bool (*send)(void *ctx, const qa_net_address *to, const uint8_t *bytes, size_t length);
  /* Transport poll: 0 none, 1 packet, 2 error (message filled). */
  int (*poll)(void *ctx, qa_net_address *out_from, const uint8_t **out_payload, size_t *out_length, const char **out_error);
  void (*transport_close)(void *ctx);
  const char *(*resolve)(void *ctx, const char *text, int64_t default_port, qa_net_address *out);
  char **(*fetch_master_list)(void *ctx, const char *url, size_t *out_count, const char **out_error);
  double (*now)(void *ctx);
  /* Packet decoders: true + status filled when recognized. */
  bool (*decode_status)(void *ctx, const char *protocol, const uint8_t *payload, size_t length,
                        qa_server_status *out_status, const char **out_challenge);
  qa_net_address *(*read_q2_master_reply)(void *ctx, const uint8_t *payload, size_t length, size_t *out_count, bool *out_malformed);
  qa_net_address *(*decode_q3_master_packet)(void *ctx, const uint8_t *payload, size_t length, size_t *out_count, bool *out_complete, bool *out_failed);
  bool (*read_kex_discovery)(void *ctx, const uint8_t *payload, size_t length, qa_server_status *out_status);
  const uint8_t *(*kex_discovery_query)(void *ctx, size_t *out_length);
  /* KEX mDNS lifecycle. */
  bool (*kex_open)(void *ctx);
  void (*kex_query)(void *ctx);
  void (*kex_close)(void *ctx);
} qa_app_server_browser_host;
typedef struct qa_app_startup_server_browser qa_app_startup_server_browser;
qa_app_startup_server_browser *qa_app_startup_server_browser_open(qa_app_server_browser_host host, const qa_config_store *config);
void qa_app_startup_server_browser_destroy(qa_app_startup_server_browser *b);
const char *qa_app_startup_server_browser_protocol(const qa_app_startup_server_browser *b);
const char *qa_app_startup_server_browser_master_address(const qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_set_master_address(qa_app_startup_server_browser *b, const char *value);
const char *qa_app_startup_server_browser_address(qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_set_address(qa_app_startup_server_browser *b, const char *value);
const char *qa_app_startup_server_browser_filter(const qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_set_filter(qa_app_startup_server_browser *b, const char *value);
bool qa_app_startup_server_browser_favorites_only(const qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_set_favorites_only(qa_app_startup_server_browser *b, bool value);
bool qa_app_startup_server_browser_hide_empty(const qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_set_hide_empty(qa_app_startup_server_browser *b, bool value);
bool qa_app_startup_server_browser_hide_full(const qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_set_hide_full(qa_app_startup_server_browser *b, bool value);
const char *qa_app_startup_server_browser_selected(const qa_app_startup_server_browser *b);
const char *qa_app_startup_server_browser_status(const qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_assert_open(qa_app_startup_server_browser *b);
qa_server_browser *qa_app_startup_server_browser_q3_core(qa_app_startup_server_browser *b);
typedef struct qa_app_q3_list {
  qa_net_address *addresses; size_t address_count; /* malloc'd, caller destroys */
  bool pending; double generation, reset_generation;
} qa_app_q3_list;
qa_app_q3_list qa_app_startup_server_browser_q3_list(qa_app_startup_server_browser *b, double source);
bool qa_app_startup_server_browser_resolve_q3(qa_app_startup_server_browser *b, const char *text, qa_net_address *out);
void qa_app_startup_server_browser_add_q3(qa_app_startup_server_browser *b, double source, const qa_net_address *address);
void qa_app_startup_server_browser_remove_q3(qa_app_startup_server_browser *b, double source, const qa_net_address *address);
void qa_app_startup_server_browser_scan_q3(qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_request_q3_master(qa_app_startup_server_browser *b, double source, const char *remote, double protocol,
                                                     const char *const *keywords, size_t keyword_count,
                                                     void (*assert_current)(void *ctx), void *assert_ctx);
bool qa_app_startup_server_browser_load_q3_cache(qa_app_startup_server_browser *b, void (*assert_current)(void *ctx), void *assert_ctx, qa_q3_browser_cache_view **out_view);
void qa_app_startup_server_browser_save_q3_cache(qa_app_startup_server_browser *b, const qa_q3_browser_cache_view *view);
void qa_app_startup_server_browser_choose(qa_app_startup_server_browser *b, const char *protocol);
void qa_app_startup_server_browser_choose_sort(qa_app_startup_server_browser *b, const char *order);
const qa_browser_entry *qa_app_startup_server_browser_selected_entry(qa_app_startup_server_browser *b);
char **qa_app_startup_server_browser_details(qa_app_startup_server_browser *b, size_t *out_count);
void qa_app_startup_server_browser_discover(qa_app_startup_server_browser *b);
const char *qa_app_startup_server_browser_empty_message(qa_app_startup_server_browser *b);
const qa_browser_entry **qa_app_startup_server_browser_rows(qa_app_startup_server_browser *b, size_t *out_count);
void qa_app_startup_server_browser_select(qa_app_startup_server_browser *b, const char *key);
void qa_app_startup_server_browser_query(qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_scan(qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_favorite(qa_app_startup_server_browser *b);
qa_app_browser_connection qa_app_startup_server_browser_connection(qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_poll(qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_kex_found(qa_app_startup_server_browser *b, const qa_net_address *address);
void qa_app_startup_server_browser_kex_failed(qa_app_startup_server_browser *b, const char *message);
void qa_app_startup_server_browser_close(qa_app_startup_server_browser *b);
#endif
