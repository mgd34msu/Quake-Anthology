/* Port of src/app/bootstrap/renderer.ts. Concrete window/backend
 * opening (SDL + cpu/gl/worker adapters in platform/render) is a host
 * factory returning a bundle; capture queue, restart preparation
 * phases, ownership guards and diagnostics assembly are kept here.
 * Async open/prepare -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_RENDERER_H
#define QA_APP_BOOTSTRAP_RENDERER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/platform/sdl.h"
#include "qa/render/image_journal.h"
#include "qa/render/scene/resources.h"
/* TS GlRenderer["driver"] mirror (render/gl description). */
typedef struct qa_app_gl_driver { const char *vendor, *renderer, *version, *shading_language; } qa_app_gl_driver;
typedef struct qa_app_gl_config { double max_texture_size, texture_units, color_bits, depth_bits; bool stereo_enabled; } qa_app_gl_config;
typedef struct qa_app_renderer_image_description {
  double ordinal; const char *name; double width, height; const char *encoding; double mip_levels;
} qa_app_renderer_image_description;
typedef struct qa_app_renderer_diagnostics {
  const char *backend; /* "cpu"|"gl" */
  double width, height;
  const qa_app_gl_driver *driver; /* NULL = TS null */
  qa_sdl_display_mode *display_modes; size_t display_mode_count;
  qa_app_renderer_image_description *images; size_t image_count;
} qa_app_renderer_diagnostics;
void qa_app_renderer_diagnostics_free(qa_app_renderer_diagnostics *d);
/* One opened native surface (host-owned concrete backend). */
typedef struct qa_app_native_backend_bundle {
  void *ctx;
  qa_sdl_window *window;         /* borrowed from host bundle */
  qa_renderer_backend backend;
  bool is_worker, is_gl, is_cpu;
  const qa_app_gl_driver *driver;
  const qa_app_gl_config *gl_config;
  double worker_swap_interval;
  void (*set_output_gamma)(void *ctx, double gamma);
  void (*set_swap_interval)(void *ctx, double interval);
  void (*synchronize)(void *ctx);
  void (*apply_image_resource)(void *ctx, qa_image_resource_operation operation);
  void (*execute_commands)(void *ctx, const qa_render_command *commands, size_t count, const double *captures, size_t capture_count);
  void (*execute_set_color)(void *ctx, qa_vec4 color);
  void (*finish)(void *ctx);
  const uint8_t *(*pixels)(void *ctx, size_t *out_length);
  uint8_t *(*read_pixels)(void *ctx, size_t *out_length);
  void (*present_cpu)(void *ctx, const uint8_t *pixels, size_t length);
  void (*present)(void *ctx);
  void (*resize)(void *ctx, double width, double height);
  void (*close_backend)(void *ctx);
  void (*close_window)(void *ctx);
  /* Added for the concrete factory: the backend after a resize that
   * replaced it (serial CPU), else the opened backend. */
  qa_renderer_backend (*current_backend)(void *ctx);
  /* Added for worker bundles: installs the renderer's capture
   * completion/failure hooks (TS WorkerRenderer swap/failed callbacks
   * calling FrameCaptures). Hook args carry the bundle ctx so the
   * renderer can apply the TS current-backend check. */
  void (*set_capture_hooks)(void *ctx,
      void (*complete)(void *hook_ctx, void *bundle_ctx, const qa_image_level *image, const double *ids, size_t count),
      void (*fail)(void *hook_ctx, void *bundle_ctx, const char *error), void *hook_ctx);
} qa_app_native_backend_bundle;
/* Concrete bundle factory (TS SdlWindow.open + openBackend over the
 * landed cpu/gl/worker backends). Used by qa_app_native_renderer_open
 * and prepare_restart when host.open_bundle is NULL. False only on
 * OOM/backend-open failure; invalid arguments abort. */
bool qa_app_open_native_backend_bundle(void *unused, const char *kind, double width, double height, bool hidden, bool worker,
                                       qa_renderer_resource_owner owner, double gamma, qa_app_native_backend_bundle *out);
typedef struct qa_app_native_renderer_host {
  void *ctx;
  /* Opens window+backend of kind ("cpu"|"gl"); hidden/size as given.
   * Returns false only on OOM; backend errors abort in host. */
  bool (*open_bundle)(void *ctx, const char *kind, double width, double height, bool hidden, bool worker,
                      qa_renderer_resource_owner owner, double gamma, qa_app_native_backend_bundle *out);
  /* WorkerRenderer.withParkedContexts equivalent. */
  void (*with_parked_contexts)(void *ctx, void (*operation)(void *op_ctx), void *op_ctx);
} qa_app_native_renderer_host;
typedef struct qa_app_native_renderer qa_app_native_renderer;
typedef struct qa_app_prepared_renderer_restart qa_app_prepared_renderer_restart;
/* TS NativeRenderer.open(options subset, owner, images?). images may be
 * NULL to create an owned registry (caller inits/destroys otherwise). */
qa_app_native_renderer *qa_app_native_renderer_open(qa_app_native_renderer_host host,
    const char *renderer_kind, double width, double height, bool hidden, double gamma, bool render_worker,
    qa_renderer_resource_owner owner, qa_scene_image_registry *images);
void qa_app_native_renderer_destroy(qa_app_native_renderer *r);
qa_sdl_window *qa_app_native_renderer_window(qa_app_native_renderer *r);
qa_renderer_backend qa_app_native_renderer_backend(qa_app_native_renderer *r);
qa_renderer_resource_owner qa_app_native_renderer_owner(const qa_app_native_renderer *r);
qa_scene_image_registry *qa_app_native_renderer_images(qa_app_native_renderer *r);
bool qa_app_native_renderer_render_worker(const qa_app_native_renderer *r);
const qa_app_gl_driver *qa_app_native_renderer_driver(const qa_app_native_renderer *r);
const qa_app_gl_config *qa_app_native_renderer_gl_config(const qa_app_native_renderer *r);
double qa_app_native_renderer_swap_interval(qa_app_native_renderer *r);
void qa_app_native_renderer_set_swap_interval(qa_app_native_renderer *r, double interval);
void qa_app_native_renderer_mutate_window(qa_app_native_renderer *r, void (*operation)(void *ctx), void *ctx);
double qa_app_native_renderer_output_gamma(const qa_app_native_renderer *r);
void qa_app_native_renderer_synchronize(qa_app_native_renderer *r);
qa_app_prepared_renderer_restart *qa_app_native_renderer_prepare_restart(qa_app_native_renderer *r, const char *kind, bool has_worker, bool worker);
qa_sdl_window *qa_app_prepared_renderer_restart_window(qa_app_prepared_renderer_restart *p);
qa_renderer_backend qa_app_prepared_renderer_restart_backend(qa_app_prepared_renderer_restart *p);
bool qa_app_prepared_renderer_restart_published(const qa_app_prepared_renderer_restart *p);
/* TS publish() returns the retire closure; call qa_app_prepared_renderer_restart_retire. */
void qa_app_prepared_renderer_restart_publish(qa_app_prepared_renderer_restart *p);
void qa_app_prepared_renderer_restart_retire(qa_app_prepared_renderer_restart *p);
void qa_app_prepared_renderer_restart_discard(qa_app_prepared_renderer_restart *p);
qa_app_renderer_diagnostics qa_app_native_renderer_diagnostics(qa_app_native_renderer *r);
void qa_app_native_renderer_set_output_gamma(qa_app_native_renderer *r, double gamma);
void qa_app_native_renderer_execute(qa_app_native_renderer *r, qa_render_frame frame);
uint8_t *qa_app_native_renderer_read_pixels(qa_app_native_renderer *r, size_t *out_length);
/* Capture delivery is synchronous-on-swap: resolve receives a copy. */
typedef struct qa_app_frame_capture_request {
  void *ctx;
  void (*resolve)(void *ctx, qa_image_level image);
  void (*reject)(void *ctx, const char *error);
  bool aborted; const char *abort_reason; /* NULL -> "Frame capture aborted" */
} qa_app_frame_capture_request;
void qa_app_native_renderer_capture_next_frame(qa_app_native_renderer *r, qa_app_frame_capture_request request);
void qa_app_native_renderer_close(qa_app_native_renderer *r);
#endif
