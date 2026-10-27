/* Port of src/render/worker-transport.ts. Direct/pumped (PORTING.md):
 * there is no worker thread or message channel; the transport holds a
 * runtime created synchronously from the initialization payload and
 * every request executes in-process before returning.
 * GPL-2.0-or-later. */
#ifndef QA_RENDER_WORKER_TRANSPORT_H
#define QA_RENDER_WORKER_TRANSPORT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/render.h"
#include "qa/render/execution.h"
/* TS worker request payloads (unknown records) become this tagged struct. */
typedef enum qa_worker_request_kind {
  QA_WORKER_REQUEST_COMMAND, QA_WORKER_REQUEST_PREPARE, QA_WORKER_REQUEST_BEGIN,
  QA_WORKER_REQUEST_APPLY_TEXTURE, QA_WORKER_REQUEST_DRAW, QA_WORKER_REQUEST_CLEANUP,
  QA_WORKER_REQUEST_SET_GAMMA, QA_WORKER_REQUEST_SET_OVERDRAW, QA_WORKER_REQUEST_READ_PIXELS,
  QA_WORKER_REQUEST_READ_DEPTH, QA_WORKER_REQUEST_READ_STENCIL, QA_WORKER_REQUEST_DESCRIBE,
  QA_WORKER_REQUEST_PARK, QA_WORKER_REQUEST_RESUME, QA_WORKER_REQUEST_CLOSE
} qa_worker_request_kind;
typedef struct qa_worker_request {
  qa_worker_request_kind kind;
  qa_execution_command command;
  qa_draw_batch batch; double draw_id, unit;
  qa_texture_binding binding;
  double gamma; bool enabled; double x, y;
} qa_worker_request;
typedef struct qa_worker_response {
  qa_worker_request_kind kind;
  qa_image_level image; bool has_image;
  double number;
  const uint8_t *bytes; size_t byte_length;
} qa_worker_response;
/* Callbacks the runtime makes back to the frontend (image applied,
 * dynamic resolve, swap): TS request() channel, as fn+ctx. */
typedef enum qa_worker_callback_kind { QA_WORKER_CALLBACK_IMAGE_APPLIED, QA_WORKER_CALLBACK_DYNAMIC_IMAGE, QA_WORKER_CALLBACK_SWAP } qa_worker_callback_kind;
typedef struct qa_worker_callback {
  qa_worker_callback_kind kind;
  double token;
  const double *captures; size_t capture_count;
  qa_image_level image; bool has_image;
} qa_worker_callback;
typedef struct qa_render_worker_host {
  void *ctx;
  /* Returns a wire image for dynamic-image callbacks. */
  bool (*on_callback)(void *ctx, qa_worker_callback callback, double *out_ordinal, double *out_width, double *out_height, const char **out_name);
  void (*failed)(void *ctx, const char *message);
} qa_render_worker_host;
typedef struct qa_render_worker_runtime qa_render_worker_runtime;
struct qa_render_worker_runtime { void *impl; };
typedef bool (*qa_render_worker_initialize_fn)(void *ctx, const void *payload, size_t payload_size, qa_render_worker_host host, qa_render_worker_runtime *out);
bool qa_render_worker_runtime_handle(qa_render_worker_runtime *runtime, qa_worker_request request, qa_worker_response *out);
void qa_render_worker_runtime_close(qa_render_worker_runtime *runtime);
typedef struct qa_render_worker_transport qa_render_worker_transport;
struct qa_render_worker_transport { void *impl; };
/* initialize/ctx create the runtime (qa_create_render_worker_runtime
 * for the built-in CPU/GL runtime). payload is the initialization
 * struct for that factory. False only on OOM/factory failure. */
bool qa_render_worker_transport_open(qa_render_worker_transport *transport, qa_render_worker_initialize_fn initialize, void *initialize_ctx, const void *payload, size_t payload_size, qa_render_worker_host host);
bool qa_render_worker_transport_request(qa_render_worker_transport *transport, qa_worker_request request, qa_worker_response *out);
void qa_render_worker_transport_close(qa_render_worker_transport *transport);
/* TS serveRenderWorker: in C there is no worker entry loop; this binds
 * a factory for qa_render_worker_transport_open callers using the
 * default factory slot. */
void qa_serve_render_worker(qa_render_worker_initialize_fn initialize, void *initialize_ctx);
bool qa_render_worker_transport_open_default(qa_render_worker_transport *transport, const void *payload, size_t payload_size, qa_render_worker_host host);
#endif
