#ifndef QA_RENDER_CPU_H
#define QA_RENDER_CPU_H
#include "qa/scene.h"
#include "qa/render_controls.h"
#include "qa/render_workers.h"

typedef struct qa_cpu_renderer qa_cpu_renderer;
typedef struct qa_cpu_surface_ticket qa_cpu_surface_ticket;
typedef struct qa_cpu_capabilities {
  uint32_t color_bits, alpha_bits, depth_bits, stencil_bits;
} qa_cpu_capabilities;
typedef struct qa_cpu_statistics {
  uint64_t draws, brush_candidates, brush_predicate_rejects;
  uint64_t brush_planarity_rejects, brush_cache_rejects, brush_queued;
  uint64_t brush_batches, brush_spans, brush_covered, brush_written;
  uint64_t generic_batches, generic_commands, generic_triangles;
  uint64_t generic_covered, generic_fragments, generic_written;
  uint64_t worker_dispatches, worker_posts, worker_joins;
  uint64_t skin_jobs, skin_vertices, skin_cached_draws;
} qa_cpu_statistics;
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
  qa_render_workers *workers; /* Optional borrowed handle, retained by create. */
} qa_cpu_options;
void qa_cpu_options_default(qa_cpu_options *options);
qa_cpu_renderer *qa_cpu_create(const qa_cpu_options *options, qa_error *error);
qa_render_workers *qa_cpu_workers(const qa_cpu_renderer *); /* Borrowed. */
bool qa_cpu_capabilities_read(const qa_cpu_renderer *, qa_cpu_capabilities *, qa_error *);
/* Optional totals retained until reset. Disabled collection preserves totals.
 * Read/reset require returned render jobs; fragments count kernel calls and
 * written pixels count accepted color/depth stores. */
bool qa_cpu_statistics_enable(qa_cpu_renderer *, bool, qa_error *);
bool qa_cpu_statistics_read(const qa_cpu_renderer *, qa_cpu_statistics *, qa_error *);
bool qa_cpu_statistics_reset(qa_cpu_renderer *, qa_error *);
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
/* Read the actual renderer-owned scalar, without callbacks or mutation. */
bool qa_cpu_gamma_read(const qa_cpu_renderer *, float *, qa_error *);
/* Pure uniform viewport observation after Source command issue. */
bool qa_cpu_output_domain_read(const qa_cpu_renderer *, qa_scene_rect, bool *, qa_error *);
/* Prepare gamma on the existing endpoint. No native present callback runs;
 * publish the actual table/output ownership with surface_publish, then retire.
 * A failed prepare may retain *out for checked surface_abort. */
bool qa_cpu_gamma_prepare(qa_cpu_renderer *, float, qa_cpu_present, void *,
                          qa_cpu_surface_ticket **, qa_error *);
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
/* Rebuild and present the actual prepared pixels after a native ramp child.
 * Uses existing ticket storage and retains the checked endpoint on failure. */
bool qa_cpu_surface_refresh(qa_cpu_surface_ticket *, qa_error *);
/* Pure held-ticket/resource identity proof after successful preparation.
 * No presenter, native query, allocation, error write or mutation runs. */
bool qa_cpu_surface_ready_is(const qa_cpu_surface_ticket *);
/* Publish only after the complete surface/child bundle is ready; this does
 * only private ownership transfers. Retire the retained ticket afterward. */
void qa_cpu_surface_publish(qa_cpu_surface_ticket *);
bool qa_cpu_surface_abort(qa_cpu_surface_ticket **, qa_error *);
bool qa_cpu_surface_retire(qa_cpu_surface_ticket **, qa_error *);
#endif
