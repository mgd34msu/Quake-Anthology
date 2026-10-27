/* Port of src/render/worker.ts. Direct/pumped: WorkerRenderer forwards
 * through the in-process transport synchronously. GPL-2.0-or-later. */
#ifndef QA_RENDER_WORKER_H
#define QA_RENDER_WORKER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/render.h"
#include "qa/render/gl/renderer.h"
#include "qa/render/worker_runtime.h"
typedef struct qa_worker_gl_config { double max_texture_size, texture_units, color_bits, depth_bits; bool stereo_enabled; } qa_worker_gl_config;
typedef struct qa_worker_description {
  const char *backend; /* "cpu"|"gl" */
  double width, height, stencil_bits;
  const qa_gl_driver_info *driver; /* nullable */
  const qa_worker_gl_config *gl_config; /* nullable */
  double swap_interval; /* -1|0|1 */
} qa_worker_description;
typedef void (*qa_worker_swap_fn)(void *ctx, const qa_image_level *image, const double *captures, size_t capture_count);
typedef void (*qa_worker_failed_fn)(void *ctx, const char *message);
typedef struct qa_worker_renderer qa_worker_renderer;
struct qa_worker_renderer { void *impl; qa_renderer_resource_owner owner; double width, height, stencil_bits; };
/* TS async open() becomes synchronous. init describes the backend. */
bool qa_worker_renderer_open(qa_worker_renderer *renderer, qa_render_worker_init init, qa_renderer_resource_owner owner, qa_worker_swap_fn swap, void *swap_ctx, qa_worker_failed_fn failed, void *failed_ctx);
qa_worker_description qa_worker_renderer_description(qa_worker_renderer *renderer, qa_gl_driver_info *driver_storage, qa_worker_gl_config *config_storage);
void qa_worker_renderer_execute(qa_worker_renderer *renderer, qa_render_frame frame, const double *captures, size_t capture_count);
void qa_worker_renderer_set_output_gamma(qa_worker_renderer *renderer, double gamma);
qa_renderer_backend qa_worker_renderer_backend(qa_worker_renderer *renderer);
void qa_worker_renderer_close(qa_worker_renderer *renderer);
#endif
