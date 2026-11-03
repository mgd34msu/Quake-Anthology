/* Port of src/render/worker.ts: one immutable command packet overlaps frontend work;
 * synchronous calls are barriers. Host callbacks run on the calling thread while it
 * waits in a barrier or in qa_worker_renderer_pump (the TS event loop turn).
 * TS throws raise qa_range_error on the calling thread. GPL-2.0-or-later. */
#ifndef QA_RENDER_WORKER_H
#define QA_RENDER_WORKER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/render.h"
#include "qa/render/gl/renderer.h"
#include "qa/render/image_journal.h"
#include "qa/render/scene/resources.h"
#include "qa/render/worker_runtime.h"
typedef struct qa_worker_description {
  const char *backend; /* "cpu"|"gl" */
  double width, height, stencil_bits;
  const qa_gl_driver_info *driver; /* nullable */
  const qa_worker_gl_config *gl_config; /* nullable */
  double swap_interval; /* -1|0|1 */
} qa_worker_description;
/* The SdlWindow surface TS uses. fn+ctx so bootstrap passes a qa_sdl_window. */
typedef struct qa_worker_window {
  void *ctx;
  bool is_gl;
  void (*drawable_size)(void *ctx, double *width, double *height);
  qa_sdl_render_context_transfer (*detach_render_context)(void *ctx);
  void (*restore_render_context)(void *ctx);
} qa_worker_window;
typedef void (*qa_worker_swap_fn)(void *ctx, const qa_image_level *image, const double *captures, size_t capture_count);
typedef void (*qa_worker_failed_fn)(void *ctx, const char *message);
typedef struct qa_worker_renderer qa_worker_renderer;
struct qa_worker_renderer { void *impl; qa_renderer_resource_owner owner; double width, height, stencil_bits; };
/* TS static async open, synchronous here. images may be NULL when no registry feeds
 * this renderer; journal may be NULL until qa_worker_renderer_bind_images. platform
 * is NULL except in tests. False = startup failed, `failed` was told, context restored. */
bool qa_worker_renderer_open(qa_worker_renderer *renderer, qa_worker_window window, qa_renderer_resource_owner owner, double gamma,
    qa_scene_image_registry *images, qa_render_image_journal *journal, qa_worker_swap_fn swap, void *swap_ctx,
    qa_worker_failed_fn failed, void *failed_ctx, const qa_render_worker_platform *platform);
/* NativeRenderer swaps its journal across a restart publish. */
void qa_worker_renderer_bind_images(qa_worker_renderer *renderer, qa_scene_image_registry *images, qa_render_image_journal *journal);
/* TS static withParkedContexts over every open GL worker. */
void qa_worker_renderer_with_parked_contexts(void (*operation)(void *ctx), void *ctx);
qa_worker_description qa_worker_renderer_description(qa_worker_renderer *renderer, qa_gl_driver_info *driver_storage, qa_worker_gl_config *config_storage);
void qa_worker_renderer_synchronize(qa_worker_renderer *renderer);
/* One TS event-loop turn: delivers callbacks and failures with no barrier. */
void qa_worker_renderer_pump(qa_worker_renderer *renderer);
void qa_worker_renderer_execute(qa_worker_renderer *renderer, const qa_render_command *commands, size_t command_count,
    const double *captures, size_t capture_count, const qa_image_resource_operation *suffix, size_t suffix_count);
void qa_worker_renderer_resize(qa_worker_renderer *renderer, double width, double height);
void qa_worker_renderer_set_output_gamma(qa_worker_renderer *renderer, double gamma);
void qa_worker_renderer_set_swap_interval(qa_worker_renderer *renderer, double interval);
/* Malloc'd pixels; free out->pixels.bytes. */
void qa_worker_renderer_read_pixels(qa_worker_renderer *renderer, qa_image_level *out);
qa_renderer_backend qa_worker_renderer_backend(qa_worker_renderer *renderer);
/* TS close. False = a failure was thrown (see last_error); the renderer is closed only
 * when the worker acknowledged retirement, and only then is the context restored. */
bool qa_worker_renderer_close(qa_worker_renderer *renderer);
bool qa_worker_renderer_closed(const qa_worker_renderer *renderer);
const char *qa_worker_renderer_last_error(const qa_worker_renderer *renderer);
/* C ownership. Closes first if needed. */
void qa_worker_renderer_destroy(qa_worker_renderer *renderer);
#endif
