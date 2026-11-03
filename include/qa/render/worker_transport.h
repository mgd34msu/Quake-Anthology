/* Port of src/render/worker-transport.ts.
 *
 * qa_rwt_*: the TS RenderWorkerTransport and serveRenderWorker. A pthread is
 * the worker, a mutex-guarded queue pair the MessageChannel, a condition
 * variable plus counter the Atomics signal. Payloads are byte blobs copied on
 * send (structured clone). TS throws here are recoverable by contract, so they
 * become false returns with an error object; error identity is the pointer.
 * The Node event loop becomes qa_rwt_pump, which runs message delivery, worker
 * error/exit events and issue timers.
 * GPL-2.0-or-later. */
#ifndef QA_RENDER_WORKER_TRANSPORT_H
#define QA_RENDER_WORKER_TRANSPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/render.h"
#include "qa/render/execution.h"
/* --- RenderWorkerTransport --- */
/* Borrowed bytes, copied when sent. When owned is set the holder must release it
 * (qa_rwt_value_release). A host or runtime returning a value may set owned; the
 * transport then releases it after copying. size 0 with NULL bytes is undefined. */
typedef struct qa_rwt_value { const void *bytes; size_t size; void *owned; } qa_rwt_value;
void qa_rwt_value_release(qa_rwt_value *value);
/* Error objects live until qa_rwt_destroy; pointer equality is TS identity. */
typedef struct qa_rwt_error { char name[64]; char message[1024]; struct qa_rwt_error *next; } qa_rwt_error;
typedef struct qa_rwt_transport qa_rwt_transport;
typedef struct qa_rwt_host {
  void *ctx;
  /* False = throw; set *error (qa_rwt_error_new, or an error a call returned). */
  bool (*request)(void *ctx, qa_rwt_value payload, qa_rwt_value *out, qa_rwt_error **error);
  bool (*completed)(void *ctx, qa_rwt_value payload, qa_rwt_error **error);
  void (*failed)(void *ctx, qa_rwt_error *error); /* optional */
} qa_rwt_host;
/* Worker-side failure (TS Failure union), passed across threads by value. */
typedef struct qa_rwt_failure { bool is_host; int64_t token; char name[64]; char message[1024]; } qa_rwt_failure;
void qa_rwt_failure_set(qa_rwt_failure *failure, const char *name, const char *message);
typedef struct qa_rwt_worker qa_rwt_worker;
/* TS RenderWorkerRuntime. Everything here runs on the worker thread. */
typedef struct qa_rwt_runtime {
  void *ctx;
  qa_rwt_value description;
  bool (*dispatch)(void *ctx, qa_rwt_value payload, qa_rwt_value *out, qa_rwt_failure *failure);
  bool (*close)(void *ctx, qa_rwt_failure *failure);
} qa_rwt_runtime;
/* TS serveRenderWorker's initialize; with its ctx it stands in for the worker URL. */
typedef bool (*qa_rwt_initialize_fn)(void *ctx, qa_rwt_value payload, qa_rwt_worker *worker, qa_rwt_runtime *out, qa_rwt_failure *failure);
/* TS request(), worker thread only. On true, *out is owned by the caller. */
bool qa_rwt_worker_request(qa_rwt_worker *worker, qa_rwt_value payload, qa_rwt_value *out, qa_rwt_failure *failure);

/* TS RenderWorkerTransport.open (default timeout 30000). *out is set whenever
 * the transport was allocated, also on false, so the error stays readable until
 * qa_rwt_destroy. Invalid timeouts abort, as the TS RangeError precedes any worker. */
bool qa_rwt_open(qa_rwt_transport **out, qa_rwt_initialize_fn initialize, void *initialize_ctx, qa_rwt_value initialization, qa_rwt_host host, double timeout_milliseconds, qa_rwt_error **error);
qa_rwt_error *qa_rwt_error_new(qa_rwt_transport *transport, const char *name, const char *message);
bool qa_rwt_active(const qa_rwt_transport *transport);
bool qa_rwt_settled(const qa_rwt_transport *transport);
bool qa_rwt_retired(const qa_rwt_transport *transport);
qa_rwt_value qa_rwt_description(const qa_rwt_transport *transport); /* borrowed */
bool qa_rwt_issue(qa_rwt_transport *transport, qa_rwt_value payload, qa_rwt_error **error);
/* out may be NULL to drop the result; otherwise it is owned by the caller. */
bool qa_rwt_call(qa_rwt_transport *transport, qa_rwt_value payload, qa_rwt_value *out, qa_rwt_error **error);
bool qa_rwt_callback_call(qa_rwt_transport *transport, qa_rwt_value payload, qa_rwt_value *out, qa_rwt_error **error);
bool qa_rwt_synchronize(qa_rwt_transport *transport, qa_rwt_value *out, qa_rwt_error **error);
/* TS boolean result in *retired; false return = throw from the nested close. */
bool qa_rwt_retire_after_failure(qa_rwt_transport *transport, bool *retired, qa_rwt_error **error);
bool qa_rwt_close(qa_rwt_transport *transport, qa_rwt_error **error);
/* One event-loop turn; a no-op inside a host callback or after retirement. */
void qa_rwt_pump(qa_rwt_transport *transport);
/* Frees the frontend. A worker that never acknowledged close keeps running with
 * its runtime, as TS leaves an unacknowledged context borrowed. */
void qa_rwt_destroy(qa_rwt_transport *transport);
#endif
