/* Port of src/app/bootstrap/server-browser.ts (StartupServerBrowser) over the
 * shared network code: one UDP transport (qa_udp_transport bound to
 * 0.0.0.0:0 with broadcast), the four ServerBrowser cores with the Q1, QW, Q2 and
 * Q3 discovery wires, the protocol status decoders, resolveAddress, the HTTP
 * master list (server_master_list.h) and KEX mDNS discovery.
 *
 * Async -> synchronous (PORTING async rule): every TS await completes inside the
 * call; persist() runs its write in place after assertOpen. KEX mDNS datagrams are
 * pumped by poll(). performance.now() is qa_net_default_now.
 *
 * Errors: the TS throws are qa_raise with the TS class name and message ("Error"
 * for `new Error`), so a caller holding qa_try recovers as the TS caller's catch
 * does (the startup menu reports them in status). poll() never raises for remote
 * input: malformed and foreign datagrams are skipped as the TS try/catch skips them.
 * GPL-2.0-or-later. */
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
/* TS BrowserConnection. protocol is static text; remote is malloc'd (free it, or
 * call qa_app_browser_connection_destroy). q2_protocol is {kind "q2-kex", version 2023}. */
typedef struct qa_app_browser_connection {
  const char *protocol; char *remote;
  bool has_q2_protocol; const char *q2_protocol_kind; double q2_protocol_version;
} qa_app_browser_connection;
void qa_app_browser_connection_destroy(qa_app_browser_connection *connection);
/* TS browserAddress: malloc'd; raises "Server requires an IP address". */
char *qa_app_browser_address(const qa_net_address *address);
typedef struct qa_app_startup_server_browser qa_app_startup_server_browser;
/* TS StartupServerBrowser.open(config): binds the transport (raises the bind
 * error), restores the saved lists, skipping and reporting invalid ones in status.
 * NULL only on OOM. `config` is borrowed for the browser's life. */
qa_app_startup_server_browser *qa_app_startup_server_browser_open(const qa_config_store *config);
/* close() then frees. */
void qa_app_startup_server_browser_destroy(qa_app_startup_server_browser *b);
const char *qa_app_startup_server_browser_protocol(const qa_app_startup_server_browser *b);
const char *qa_app_startup_server_browser_master_address(const qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_set_master_address(qa_app_startup_server_browser *b, const char *value);
/* The address getter's default text lives in the browser until the next call. */
const char *qa_app_startup_server_browser_address(qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_set_address(qa_app_startup_server_browser *b, const char *value);
const char *qa_app_startup_server_browser_filter(const qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_set_filter(qa_app_startup_server_browser *b, const char *value);
bool qa_app_startup_server_browser_favorites_only(const qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_set_favorites_only(qa_app_startup_server_browser *b, bool value);
const char *qa_app_startup_server_browser_sort_order(const qa_app_startup_server_browser *b);
bool qa_app_startup_server_browser_hide_empty(const qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_set_hide_empty(qa_app_startup_server_browser *b, bool value);
bool qa_app_startup_server_browser_hide_full(const qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_set_hide_full(qa_app_startup_server_browser *b, bool value);
/* NULL is the TS null. */
const char *qa_app_startup_server_browser_selected(const qa_app_startup_server_browser *b);
const char *qa_app_startup_server_browser_status(const qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_set_status(qa_app_startup_server_browser *b, const char *value);
void qa_app_startup_server_browser_assert_open(qa_app_startup_server_browser *b);
qa_server_browser *qa_app_startup_server_browser_q3_core(qa_app_startup_server_browser *b);
/* TS q3List(source). addresses is the browser's snapshot (borrowed): the same
 * pointer is returned while the source's generation is unchanged, and it stays
 * valid until a later q3List rebuilds that source or the browser is destroyed. */
typedef struct qa_app_q3_list {
  const qa_net_address *addresses; size_t address_count;
  bool pending; double generation, reset_generation;
} qa_app_q3_list;
qa_app_q3_list qa_app_startup_server_browser_q3_list(qa_app_startup_server_browser *b, double source);
/* TS resolveQ3: false is the TS null; *out owned by the caller on true. */
bool qa_app_startup_server_browser_resolve_q3(qa_app_startup_server_browser *b, const char *text, qa_net_address *out);
void qa_app_startup_server_browser_add_q3(qa_app_startup_server_browser *b, double source, const qa_net_address *address);
void qa_app_startup_server_browser_remove_q3(qa_app_startup_server_browser *b, double source, const qa_net_address *address);
void qa_app_startup_server_browser_scan_q3(qa_app_startup_server_browser *b);
/* assert_current NULL is the TS default `() => {}`; it raises to retire the call. */
void qa_app_startup_server_browser_request_q3_master(qa_app_startup_server_browser *b, double source, const char *remote, double protocol,
                                                     const char *const *keywords, size_t keyword_count,
                                                     void (*assert_current)(void *ctx), void *assert_ctx);
/* TS loadQ3Cache: true with *out_view (malloc'd; destroy with qa_q3_browser_cache_view_destroy
 * then free) when the cache had a view; false is the TS null view. */
bool qa_app_startup_server_browser_load_q3_cache(qa_app_startup_server_browser *b, void (*assert_current)(void *ctx), void *assert_ctx,
                                                 qa_q3_browser_cache_view **out_view);
void qa_app_startup_server_browser_save_q3_cache(qa_app_startup_server_browser *b, const qa_q3_browser_cache_view *view);
void qa_app_startup_server_browser_choose(qa_app_startup_server_browser *b, const char *protocol);
void qa_app_startup_server_browser_choose_sort(qa_app_startup_server_browser *b, const char *order);
const qa_browser_entry *qa_app_startup_server_browser_selected_entry(qa_app_startup_server_browser *b);
/* malloc'd array of malloc'd lines. */
char **qa_app_startup_server_browser_details(qa_app_startup_server_browser *b, size_t *out_count);
void qa_app_startup_server_browser_discover(qa_app_startup_server_browser *b);
const char *qa_app_startup_server_browser_empty_message(qa_app_startup_server_browser *b);
/* The rows array is owned by the browser until the next rows() call; entries are
 * the core's and stay valid until it changes. */
const qa_browser_entry **qa_app_startup_server_browser_rows(qa_app_startup_server_browser *b, size_t *out_count);
void qa_app_startup_server_browser_select(qa_app_startup_server_browser *b, const char *key);
void qa_app_startup_server_browser_query(qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_scan(qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_favorite(qa_app_startup_server_browser *b);
qa_app_browser_connection qa_app_startup_server_browser_connection(qa_app_startup_server_browser *b);
void qa_app_startup_server_browser_poll(qa_app_startup_server_browser *b);
/* TS close(): idempotent; the browser stays readable until destroy. */
void qa_app_startup_server_browser_close(qa_app_startup_server_browser *b);
/* The bound transport address (tests). */
bool qa_app_startup_server_browser_transport_address(const qa_app_startup_server_browser *b, qa_net_address *out);
/* StartupServerBrowser as a Q3BrowserHost (TS passes the browser itself to
 * Q3BrowserView). The adapter converts the browser's NetworkAddress values to the Q3
 * view's address records; borrowed results stay valid until the next call of the
 * same kind. Free with qa_app_startup_server_browser_q3_host_destroy after the view closes. */
typedef struct qa_app_startup_q3_browser_host qa_app_startup_q3_browser_host;
qa_app_startup_q3_browser_host *qa_app_startup_server_browser_q3_host_create(qa_app_startup_server_browser *b);
qa_q3_browser_host qa_app_startup_q3_browser_host_view(qa_app_startup_q3_browser_host *host);
void qa_app_startup_server_browser_q3_host_destroy(qa_app_startup_q3_browser_host *host);
#endif
