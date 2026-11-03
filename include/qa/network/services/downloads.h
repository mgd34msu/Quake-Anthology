/* Port of src/network/services/downloads.ts. downloadPath re-exports
 * qa_contained_file_parts. Hashing is one-shot over staged bytes (the
 * TS incremental hash is replaced by hashing at finish; same digest).
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_SERVICES_DOWNLOADS_H
#define QA_NETWORK_SERVICES_DOWNLOADS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/content.h"
#include "qa/foundation/support.h"
#include "qa/network/common/session.h"
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
  bool resumable, suspended;
  double resumed_from; /* bytes already staged and hash-verified when continuing an interrupted download */
  qa_net_sha256 hash;   /* TS createHash("sha256") over appended bytes */
  char failure[QA_CAUGHT_MESSAGE_CAPACITY]; /* message behind qa_download_sink_try_finish's *error */
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
/* Returns malloc'd digest string, caller frees. Closes the sink on every path (TS
 * finally) and raises the TS Error on failure. */
char *qa_download_sink_finish(qa_download_sink *s);
/* finish under a catch: any failure sets *error (the TS message, stored in s->failure
 * until the sink is destroyed) and returns NULL. */
char *qa_download_sink_try_finish(qa_download_sink *s, const char **error);
typedef void (*qa_download_inspect_fn)(void *ctx, const char *path);
void qa_download_sink_inspect_staged(qa_download_sink *s, qa_download_inspect_fn inspect, void *ctx);
/* Closes; a one-shot sink whose staged file vanished raises the unlink Error after releasing
 * everything (TS close's finally chain). */
void qa_download_sink_close(qa_download_sink *s);
/* TS DownloadSink.resumable: stage under a stable `<name>.part` with a `.part.sha256` record so an
 * interrupted transfer can continue. A partial file is reused only when its length and SHA-256
 * match the record; anything else is discarded. expected must be a protocol expectation. */
int qa_download_sink_create_resumable(qa_download_sink *s, const char *root, const char *name, qa_download_expectation expected);
/* Keep the staged bytes and their hash record for a later resumable sink; other sinks are discarded. */
void qa_download_sink_suspend(qa_download_sink *s);


#endif
