/* Port of src/network/services/http-downloads.ts. fetch -> libcurl (HTTP and HTTPS). The
 * promise queue becomes an explicitly pumped state machine over one curl multi handle:
 * enqueue records the request; qa_http_download_queue_poll runs one TS schedule step and
 * drives the concurrent transfers, whose bodies stream into the staging sink;
 * qa_http_download_queue_pump polls until the queue is idle. Results are read back with
 * qa_http_download_queue_result. AbortSignal -> abort flags checked at every step.
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_SERVICES_HTTP_DOWNLOADS_H
#define QA_NETWORK_SERVICES_HTTP_DOWNLOADS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/services/downloads.h"
typedef void (*qa_http_validate_fn)(void *ctx, const char *staged_path);
typedef struct qa_http_download_request {
  const char *path, *url;
  const char *kind; /* "asset" | "package" */
  qa_download_expectation expected;
  qa_http_validate_fn validate; void *validate_ctx;
} qa_http_download_request;
typedef struct qa_http_download_result {
  const char *kind; /* "downloaded"|"resolved"|"cancelled"|"fallback"|"failed" */
  char *digest; char *reason;
} qa_http_download_result;
void qa_http_download_result_destroy(qa_http_download_result *r);
typedef struct qa_http_queue_options {
  const char *root; double concurrency, range_streams; bool has_concurrency, has_range_streams;
  /* TS totalBudgetBytes: total bytes the queue may receive over its lifetime. */
  bool has_total_budget_bytes; double total_budget_bytes;
  void *ctx;
  void (*assert_current)(void *ctx);
  bool (*resolved)(void *ctx, const char *path);
  void (*refresh_package)(void *ctx, const char *path);
  void (*progress)(void *ctx, const char *path, double received, bool has_total, double total);
} qa_http_queue_options;
typedef struct qa_download_progress {
  const char *path, *phase; double received; bool has_total; double total; const char *result;
} qa_download_progress;
typedef struct qa_http_download_queue qa_http_download_queue;
void qa_http_download_queue_init(qa_http_download_queue *q, qa_http_queue_options options);
void qa_http_download_queue_destroy(qa_http_download_queue *q);
/* Heap queue for callers outside this module, where the struct is opaque. */
qa_http_download_queue *qa_http_download_queue_create(qa_http_queue_options options);
void qa_http_download_queue_free(qa_http_download_queue *q);
void qa_http_download_queue_enqueue(qa_http_download_queue *q, qa_http_download_request request);
/* One step; waits up to timeout_milliseconds for network activity. Returns the running count. */
int qa_http_download_queue_poll(qa_http_download_queue *q, int timeout_milliseconds);
/* No running transfer and nothing pending that could start. */
bool qa_http_download_queue_idle(qa_http_download_queue *q);
/* Polls until idle. */
void qa_http_download_queue_pump(qa_http_download_queue *q);
int qa_http_download_queue_result(qa_http_download_queue *q, const char *path, qa_http_download_result *out);
size_t qa_http_download_queue_progress_count(qa_http_download_queue *q);
qa_download_progress qa_http_download_queue_progress_at(qa_http_download_queue *q, size_t index);
void qa_http_download_queue_retry(qa_http_download_queue *q, const char *path);
void qa_http_download_queue_cancel_file(qa_http_download_queue *q, const char *path);
void qa_http_download_queue_cancel(qa_http_download_queue *q);
/* Returns malloc'd bytes or NULL on any failure (TS catches all). */
uint8_t *qa_fetch_http_download_metadata(const char *url, double maximum_bytes, const bool *aborted, size_t *out_len);
#endif
