/* Port of src/render/worker-runtime.ts. Direct/pumped: the runtime
 * owns a SoftwareRenderer (cpu) or a caller-supplied GL backend and a
 * RenderExecutor + image journal, all in-process. GPL-2.0-or-later. */
#ifndef QA_RENDER_WORKER_RUNTIME_H
#define QA_RENDER_WORKER_RUNTIME_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/render.h"
#include "qa/render/worker_transport.h"
/* TS initialization record {backend,width,height,gamma} / {backend:"gl",gamma}. */
typedef struct qa_render_worker_init {
  const char *backend; /* "cpu"|"gl" */
  double width, height, gamma;
  /* For "gl": caller-owned backend (TS adopts a transferred SDL GL
   * context and constructs GlRenderer; C accepts the built backend). */
  qa_renderer_backend gl_backend; bool has_gl_backend;
  double swap_interval;
} qa_render_worker_init;
bool qa_create_render_worker_runtime(void *ctx, const void *payload, size_t payload_size, qa_render_worker_host host, qa_render_worker_runtime *out);
#endif
