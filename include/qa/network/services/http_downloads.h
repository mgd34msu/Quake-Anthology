/* Port of src/network/services/http-downloads.ts. fetch -> synchronous
 * socket HTTP/1.0 client (plain HTTP). HTTPS URLs fail the transfer
 * (no TLS in this slice) instead of silently downgrading. The promise
 * queue becomes an explicitly pumped queue: enqueue records the request,
 * qa_http_queue_pump runs pending transfers; results are read back with
 * qa_http_queue_result. AbortSignal -> bool* flag. GPL-2.0-or-later. */
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
void qa_http_download_queue_enqueue(qa_http_download_queue *q, qa_http_download_request request);
void qa_http_download_queue_pump(qa_http_download_queue *q);
int qa_http_download_queue_result(qa_http_download_queue *q, const char *path, qa_http_download_result *out);
size_t qa_http_download_queue_progress_count(qa_http_download_queue *q);
qa_download_progress qa_http_download_queue_progress_at(qa_http_download_queue *q, size_t index);
void qa_http_download_queue_retry(qa_http_download_queue *q, const char *path);
void qa_http_download_queue_cancel_file(qa_http_download_queue *q, const char *path);
void qa_http_download_queue_cancel(qa_http_download_queue *q);
/* Returns malloc'd bytes or NULL on any failure (TS catches all). */
uint8_t *qa_fetch_http_download_metadata(const char *url, double maximum_bytes, const bool *aborted, size_t *out_len);
/* Shared synchronous fetch used by qa_download_http. */
char *qa_http_download_to_sink(const char *url, qa_download_sink *sink);
#endif
