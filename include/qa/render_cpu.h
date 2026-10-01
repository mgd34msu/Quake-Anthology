#ifndef QA_RENDER_CPU_H
#define QA_RENDER_CPU_H
#include "qa/scene.h"

typedef struct qa_cpu_renderer qa_cpu_renderer;
typedef struct qa_cpu_surface_ticket qa_cpu_surface_ticket;
/* Present receives borrowed RGBA8 rows from top to bottom. The callback must
 * consume them before returning and must not reenter the renderer. */
typedef bool (*qa_cpu_present)(void *context, qa_bytes rgba, uint32_t width,
                               uint32_t height, qa_error *error);
typedef struct qa_cpu_options {
  uint32_t width, height;
  uint8_t subpixel_bits, stencil_bits, alpha_bits;
  uint64_t owner;
  qa_cpu_present present;
  void *present_context;
} qa_cpu_options;
void qa_cpu_options_default(qa_cpu_options *options);
qa_cpu_renderer *qa_cpu_create(const qa_cpu_options *options, qa_error *error);
/* A retained surface ticket defers destruction until checked close. */
void qa_cpu_destroy(qa_cpu_renderer *renderer);
bool qa_cpu_resize(qa_cpu_renderer *renderer, uint32_t width, uint32_t height,
                   qa_error *error);
/* Executes ordered retained commands synchronously; no per-frame texture copy.
 */
bool qa_cpu_execute(qa_cpu_renderer *renderer, const qa_scene_frame *frame,
                    qa_error *error);
bool qa_cpu_present_frame(qa_cpu_renderer *renderer, qa_error *error);
bool qa_cpu_set_gamma(qa_cpu_renderer *renderer, float gamma, qa_error *error);
/* Output remains owned by the renderer until its next render/resize/destroy. */
qa_bytes qa_cpu_pixels(qa_cpu_renderer *renderer);
/* Copies display pixels, including output gamma, into a new owned buffer. */
bool qa_cpu_capture(qa_cpu_renderer *renderer, qa_buffer *out, qa_error *error);
/* Depth and stencil window coordinates follow bottom-left GL readback. */
bool qa_cpu_read_depth(const qa_cpu_renderer *renderer, uint32_t x, uint32_t y,
                       float *out, qa_error *error);
bool qa_cpu_set_overdraw(qa_cpu_renderer *renderer, bool enabled,
                         qa_error *error);
bool qa_cpu_read_overdraw(const qa_cpu_renderer *renderer, uint8_t *destination,
                          size_t bytes, qa_error *error);
/* Allocate before publication, preserving the renderer's retained targets and
 * images. Partial failures retain a ticket; source save/render/mutation are
 * excluded until abort or checked retirement. */
bool qa_cpu_surface_prepare(qa_cpu_renderer *, uint32_t, uint32_t, float gamma,
                            qa_cpu_present, void *, qa_cpu_surface_ticket **, qa_error *);
bool qa_cpu_surface_ready(const qa_cpu_surface_ticket *, qa_error *);
/* Publish only after the complete surface/child bundle is ready; this does
 * only private ownership transfers. Retire the retained ticket afterward. */
void qa_cpu_surface_publish(qa_cpu_surface_ticket *);
bool qa_cpu_surface_abort(qa_cpu_surface_ticket **, qa_error *);
bool qa_cpu_surface_retire(qa_cpu_surface_ticket **, qa_error *);
#endif
