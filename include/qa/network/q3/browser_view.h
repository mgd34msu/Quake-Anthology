/* Port of src/network/q3/browser-view.ts. GPL-2.0-or-later.
 * TS-owner mirrors: services/discovery.ts ServerBrowser request surface
 * and common/endpoint.ts NetworkAddress/addressKey (admission.h). */
#ifndef QA_NETWORK_Q3_BROWSER_VIEW_H
#define QA_NETWORK_Q3_BROWSER_VIEW_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/admission.h"
#include "qa/network/q3/discovery.h"
#include "qa/network/q3/message.h"
typedef int64_t qa_q3_mirror_discovery_handle; /* 0 = null (TS symbol) */
typedef enum qa_q3_mirror_request_result_kind {
  QA_Q3_REQ_PENDING, QA_Q3_REQ_COMPLETED, QA_Q3_REQ_EXPIRED, QA_Q3_REQ_CANCELLED
} qa_q3_mirror_request_result_kind;
typedef struct qa_q3_mirror_request_result {
  qa_q3_mirror_request_result_kind kind;
  const qa_q3_mirror_server_status *status; /* borrowed, completed */
  double ping_milliseconds;
} qa_q3_mirror_request_result;
/* Mirror of the ServerBrowser surface used here (q3Core). */
typedef bool (*qa_q3_browser_entry_fn)(void *ctx, const qa_q3_mirror_network_address *address, const qa_q3_mirror_server_status **status_out);
typedef void (*qa_q3_browser_release_fn)(void *ctx, qa_q3_mirror_discovery_handle handle);
typedef bool (*qa_q3_browser_result_fn)(void *ctx, qa_q3_mirror_discovery_handle handle, qa_q3_mirror_request_result *out);
typedef qa_q3_mirror_discovery_handle (*qa_q3_browser_request_fn)(void *ctx, const qa_q3_mirror_network_address *address, double now, qa_q3_discovery_request_kind kind);
typedef struct qa_q3_mirror_server_browser {
  qa_q3_browser_entry_fn entry_status;
  qa_q3_browser_release_fn release_request;
  qa_q3_browser_result_fn request_result;
  qa_q3_browser_request_fn request;
  void *ctx;
} qa_q3_mirror_server_browser;
typedef struct qa_q3_browser_list_state {
  const qa_q3_mirror_network_address *addresses; size_t address_count;
  bool pending; int64_t generation, reset_generation;
} qa_q3_browser_list_state;
typedef void (*qa_q3_browser_assert_fn)(void *ctx);
typedef qa_q3_browser_list_state (*qa_q3_browser_list_fn)(void *ctx, int64_t source);
typedef bool (*qa_q3_browser_resolve_fn)(void *ctx, const char *text, qa_q3_mirror_network_address *out);
typedef void (*qa_q3_browser_add_fn)(void *ctx, int64_t source, const qa_q3_mirror_network_address *address);
typedef void (*qa_q3_browser_thunk_fn)(void *ctx);
typedef void (*qa_q3_browser_master_fn)(void *ctx, int64_t source, const char *remote, int64_t protocol,
                                        const char *const *keywords, size_t keyword_count,
                                        qa_q3_browser_assert_fn assert_current, void *assert_ctx);
/* TS Q3BrowserCacheRow/List/View. Rows own address + name. */
typedef struct qa_q3_browser_cache_row {
  qa_q3_mirror_network_address address; char *name; int64_t visible, ping;
} qa_q3_browser_cache_row;
typedef struct qa_q3_browser_cache_list { int64_t source; qa_q3_browser_cache_row *rows; size_t row_count; } qa_q3_browser_cache_list;
typedef struct qa_q3_browser_cache_view { qa_q3_browser_cache_list *lists; size_t list_count; } qa_q3_browser_cache_view;
void qa_q3_browser_cache_view_destroy(qa_q3_browser_cache_view *v);
typedef bool (*qa_q3_browser_load_cache_fn)(void *ctx, qa_q3_browser_assert_fn assert_current, void *assert_ctx, qa_q3_browser_cache_view *out);
typedef void (*qa_q3_browser_save_cache_fn)(void *ctx, const qa_q3_browser_cache_view *view);
/* TS Q3BrowserHost. */
typedef struct qa_q3_browser_host {
  qa_q3_mirror_server_browser q3_core;
  qa_q3_browser_list_fn q3_list;
  qa_q3_browser_resolve_fn resolve_q3;
  qa_q3_browser_add_fn add_q3, remove_q3;
  qa_q3_browser_thunk_fn scan_q3;
  qa_q3_browser_master_fn request_q3_master;
  qa_q3_browser_load_cache_fn load_q3_cache;
  qa_q3_browser_save_cache_fn save_q3_cache;
  qa_q3_browser_thunk_fn assert_open;
  void *ctx;
} qa_q3_browser_host;
typedef double (*qa_q3_browser_now_fn)(void *ctx);
typedef int64_t (*qa_q3_browser_int_fn)(void *ctx);
/* TS Q3BrowserViewOptions. */
typedef struct qa_q3_browser_view_options {
  qa_q3_browser_host browser;
  qa_q3_browser_now_fn now;
  qa_q3_browser_int_fn max_ping, status_resend_time;
  qa_q3_message_print_fn print; void *print_ctx;
  void *ctx;
} qa_q3_browser_view_options;
typedef void (*qa_q3_browser_write_fn)(void *ctx, const char *text);
typedef void (*qa_q3_browser_write_nullable_fn)(void *ctx, const char *text); /* NULL text allowed */
typedef const char *(*qa_q3_browser_string_fn)(void *ctx);
typedef struct qa_q3_browser_view qa_q3_browser_view;
bool qa_q3_browser_view_init(qa_q3_browser_view **out, const qa_q3_browser_view_options *options);
void qa_q3_browser_view_destroy(qa_q3_browser_view *v);
const qa_q3_browser_view_options *qa_q3_browser_view_get_options(const qa_q3_browser_view *v);
int64_t qa_q3_browser_view_get_server_count(qa_q3_browser_view *v, int64_t source);
/* write may be NULL; result is malloc'd copyString. */
char *qa_q3_browser_view_get_server_address_string(qa_q3_browser_view *v, int64_t source, int64_t index, int64_t size, qa_q3_browser_write_fn write, void *write_ctx);
char *qa_q3_browser_view_get_server_info(qa_q3_browser_view *v, int64_t source, int64_t index, int64_t size, qa_q3_browser_write_fn write, void *write_ctx);
int64_t qa_q3_browser_view_get_server_ping(qa_q3_browser_view *v, int64_t source, int64_t index);
void qa_q3_browser_view_mark_server_visible_value(qa_q3_browser_view *v, int64_t source, int64_t index, int64_t visible);
int64_t qa_q3_browser_view_server_visibility_value(qa_q3_browser_view *v, int64_t source, int64_t index);
void qa_q3_browser_view_reset_pings(qa_q3_browser_view *v, int64_t source);
int64_t qa_q3_browser_view_compare_servers(qa_q3_browser_view *v, int64_t source, int64_t key, int64_t direction, int64_t first, int64_t second);
int64_t qa_q3_browser_view_add_server(qa_q3_browser_view *v, int64_t source, qa_q3_browser_string_fn name, void *name_ctx, qa_q3_browser_string_fn address, void *address_ctx);
void qa_q3_browser_view_remove_server(qa_q3_browser_view *v, int64_t source, qa_q3_browser_string_fn address, void *address_ctx);
int64_t qa_q3_browser_view_get_ping_queue_count(qa_q3_browser_view *v);
void qa_q3_browser_view_clear_ping(qa_q3_browser_view *v, int64_t index);
/* getPing: address_out malloc'd. */
bool qa_q3_browser_view_get_ping(qa_q3_browser_view *v, int64_t index, int64_t size, qa_q3_browser_write_nullable_fn write, void *write_ctx, char **address_out, int64_t *time_out);
/* sourcePingInfo: NULL return = TS null. */
char *qa_q3_browser_view_source_ping_info(qa_q3_browser_view *v, int64_t index, int64_t size, qa_q3_browser_write_fn write, void *write_ctx);
bool qa_q3_browser_view_update_visible_pings(qa_q3_browser_view *v, int64_t source);
void qa_q3_browser_view_local_servers(qa_q3_browser_view *v);
void qa_q3_browser_view_global_servers(qa_q3_browser_view *v, int64_t source, const char *remote, int64_t protocol, const char *const *keywords, size_t keyword_count);
void qa_q3_browser_view_ping(qa_q3_browser_view *v, const char *address);
/* serverStatus: has_address/has_size model TS nulls. Returns malloc'd or NULL. */
char *qa_q3_browser_view_server_status(qa_q3_browser_view *v, const char *address, bool has_size, int64_t size, qa_q3_browser_write_fn write, void *write_ctx);
void qa_q3_browser_view_server_status_command(qa_q3_browser_view *v, const char *address);
void qa_q3_browser_view_poll(qa_q3_browser_view *v);
void qa_q3_browser_view_load_cached_servers(qa_q3_browser_view *v);
void qa_q3_browser_view_save_servers_to_cache(qa_q3_browser_view *v);
void qa_q3_browser_view_close(qa_q3_browser_view *v);
#endif
