/*
 * Port of src/compat/qvm/client-browser-syscalls.ts.
 * qa_qvmx_browser_view mirrors network/q3/browser-view.ts
 * Q3BrowserView's used surface (network unported). String writes
 * the TS performs through callbacks are fn+ctx `write` closures.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CLIENT_BROWSER_SYSCALLS_H
#define QA_COMPAT_QVM_CLIENT_BROWSER_SYSCALLS_H

#include <stdbool.h>

#include "qa/compat/qvm/syscalls.h"

typedef struct qa_qvm_browser_write {
  void *ctx;
  void (*call)(void *ctx, const char *text);
} qa_qvm_browser_write;

typedef struct qa_qvm_browser_string_source {
  void *ctx;
  /* Malloc'd string; caller (browser service adapter) owns release
   * convention: the dispatcher frees after the service returns. */
  char *(*call)(void *ctx);
} qa_qvm_browser_string_source;

typedef struct qa_qvmx_browser_ping { const char *address; double time; } qa_qvmx_browser_ping;

typedef struct qa_qvmx_browser_view {
  void *ctx;
  double (*get_server_count)(void *ctx, double source);
  void (*get_server_address_string)(void *ctx, double source, double index, double size,
                                    qa_qvm_browser_write write);
  void (*get_server_info)(void *ctx, double source, double index, double size,
                          qa_qvm_browser_write write);
  double (*get_server_ping)(void *ctx, double source, double index);
  void (*mark_server_visible_value)(void *ctx, double source, double index, double visible);
  double (*server_visibility_value)(void *ctx, double source, double index);
  void (*reset_pings)(void *ctx, double source);
  double (*compare_servers)(void *ctx, double source, double key, double direction, double first,
                            double second);
  double (*get_ping_queue_count)(void *ctx);
  void (*clear_ping)(void *ctx, double index);
  /* write receives address NULL for the TS null address. */
  qa_qvmx_browser_ping (*get_ping)(void *ctx, double index, double size,
                                   qa_qvm_browser_write write);
  /* Returns borrowed info string or NULL. */
  const char *(*source_ping_info)(void *ctx, double index, double size,
                                  qa_qvm_browser_write write);
  bool (*update_visible_pings)(void *ctx, double source);
  void (*load_cached_servers)(void *ctx);
  void (*save_servers_to_cache)(void *ctx);
  double (*add_server)(void *ctx, double source, qa_qvm_browser_string_source name,
                       qa_qvm_browser_string_source address);
  void (*remove_server)(void *ctx, double source, qa_qvm_browser_string_source address);
  /* address/size NULL-ness via has_*; returns borrowed status or NULL. */
  const char *(*server_status)(void *ctx, const char *address, bool has_size, double size,
                               qa_qvm_browser_write write);
} qa_qvmx_browser_view;

bool qa_qvm_client_browser_syscall(const qa_qvm_host_call *call, qa_qvmx_browser_view browser,
                                   qa_qvm_host_result *out);

#endif
