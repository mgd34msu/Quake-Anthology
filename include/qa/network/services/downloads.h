/* Port of src/network/services/downloads.ts. downloadPath re-exports
 * qa_contained_file_parts. Hashing is one-shot over staged bytes (the
 * TS incremental hash is replaced by hashing at finish; same digest).
 * downloadHttp: fetch -> synchronous plain-HTTP GET in http-downloads
 * transport helper (see qa_http_fetch_to_sink); HTTPS needs platform TLS.
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_SERVICES_DOWNLOADS_H
#define QA_NETWORK_SERVICES_DOWNLOADS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/content.h"
#include "qa/platform/files/contained.h"
/* TS `downloadPath` === containedFileParts. */
#define qa_download_path qa_contained_file_parts
typedef struct qa_download_source qa_download_source;
struct qa_download_source {
  void *ctx;
  double byte_length;
  uint8_t *(*read)(void *ctx, double offset, double max_bytes, size_t *out_len);
  void (*close)(void *ctx);
};
typedef struct qa_download_file { int fd; double byte_length; bool ended; } qa_download_file;
int qa_download_file_open(qa_download_file *f, const char *root, const char *name);
uint8_t *qa_download_file_read(qa_download_file *f, double offset, double max_bytes, size_t *out_len);
void qa_download_file_close(qa_download_file *f);
qa_download_source qa_download_file_as_source(qa_download_file *f);

typedef struct qa_download_expectation {
  bool is_protocol; /* false: digest+byteLength; true: maximumBytes */
  const char *digest; double byte_length, maximum_bytes;
} qa_download_expectation;
typedef struct qa_download_span { double start, end; } qa_download_span;
struct qa_download_span_state { double start, end, written; };
typedef struct qa_download_sink {
  int parent_fd, fd; char *temporary, *target, *parent_path, *temp_path;
  qa_download_expectation expected;
  bool ranged; double total; struct qa_download_span_state *spans; size_t span_count;
  double count; bool ended, inspecting;
} qa_download_sink;
int qa_download_sink_create(qa_download_sink *s, const char *root, const char *name, qa_download_expectation expected);
int qa_download_sink_create_ranged(qa_download_sink *s, const char *root, const char *name,
                                   qa_download_expectation expected, double total,
                                   const qa_download_span *spans, size_t span_count);
void qa_download_sink_destroy(qa_download_sink *s);
double qa_download_sink_byte_length(const qa_download_sink *s);
qa_download_expectation qa_download_sink_get_expected(const qa_download_sink *s);
void qa_download_sink_append(qa_download_sink *s, const uint8_t *bytes, size_t length);
void qa_download_sink_append_range(qa_download_sink *s, double span_start, const uint8_t *bytes, size_t length);
/* Returns malloc'd digest string, caller frees. */
char *qa_download_sink_finish(qa_download_sink *s);
typedef void (*qa_download_inspect_fn)(void *ctx, const char *path);
void qa_download_sink_inspect_staged(qa_download_sink *s, qa_download_inspect_fn inspect, void *ctx);
void qa_download_sink_close(qa_download_sink *s);

typedef struct qa_download_block { double index; bool has_total; double total_bytes; uint8_t *bytes; size_t length; } qa_download_block;
typedef struct qa_download_window qa_download_window;
int qa_download_window_init(qa_download_window *w, qa_download_source source, double block_bytes, double window_blocks);
void qa_download_window_destroy(qa_download_window *w);
bool qa_download_window_closed(const qa_download_window *w);
qa_download_block *qa_download_window_packets(qa_download_window *w, double now, double count, size_t *out_count);
/* Returns "continue" | "complete". */
const char *qa_download_window_acknowledge(qa_download_window *w, double block, double now);
void qa_download_window_close(qa_download_window *w);

/* Synchronous HTTP(S) URL fetch into sink; url is a string. Aborts with
 * TS messages on protocol/status errors. Returns malloc'd digest. */
char *qa_download_http(const char *url, qa_download_sink *sink);
/* Returns malloc'd array of borrowed digest strings missing from available. */
const char **qa_verify_pure_content(const char *const *required, size_t required_count,
                                    const char *const *available, size_t available_count, size_t *out_count);
#endif
