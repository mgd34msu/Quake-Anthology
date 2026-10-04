#ifndef QA_RENDER_GL_H
#define QA_RENDER_GL_H

#include "qa/display.h"
#include "qa/scene.h"
#include "qa/render_controls.h"

typedef struct qa_gl_renderer qa_gl_renderer;
typedef struct qa_gl_surface_ticket qa_gl_surface_ticket;

typedef struct qa_gl_options {
    qa_display *display;
    uint64_t owner;
} qa_gl_options;

typedef struct qa_gl_capabilities {
    unsigned color_bits, alpha_bits, depth_bits, stencil_bits;
    uint32_t maximum_texture_size, texture_units, vertex_attributes;
    /* Actual default color buffers: front-left, front-right, back-left, back-right. */
    uint32_t native_buffer_mask;
    bool stereo, floating_depth, compiled_vertex_arrays, s3tc;
    char vendor[128], renderer[128], version[128], shading_language[128];
} qa_gl_capabilities;

void qa_gl_options_default(qa_gl_options *options);
qa_gl_renderer *qa_gl_create(const qa_gl_options *options, qa_error *error);
/* The display must outlive its renderer. A retained surface ticket defers
 * destruction until its checked abort or retirement closes the ticket. */
void qa_gl_destroy(qa_gl_renderer *renderer);
const qa_gl_capabilities *qa_gl_capabilities_get(const qa_gl_renderer *renderer);
/* Current GPU residency. Array in scratch; images borrow until backend
 * progress/destruction or resource mutation. Does not prune or upload. */
bool qa_gl_resident_images(const qa_gl_renderer *, qa_arena *,
                           const qa_scene_image *const **, size_t *, qa_error *);

/* Executes finalized scene commands in their published order. The call is
 * synchronous; callers may reset the frame after it returns. */
bool qa_gl_execute(qa_gl_renderer *renderer, const qa_scene_frame *frame,
                   qa_error *error);
bool qa_gl_finish(qa_gl_renderer *renderer, qa_error *error);
bool qa_gl_swap(qa_gl_renderer *renderer, qa_error *error);
bool qa_gl_set_gamma(qa_gl_renderer *renderer, float gamma, qa_error *error);
/* Read the actual renderer-owned scalar without entering the native context. */
bool qa_gl_gamma_read(const qa_gl_renderer *, float *, qa_error *);
/* Pure uniform viewport observation on the actual selected draw buffer. */
bool qa_gl_output_domain_read(const qa_gl_renderer *, qa_scene_rect, bool *, qa_error *);
/* Retain the existing window/context and prepare its real gamma targets and
 * native presentation. Ready/publish/abort/retire use the surface ticket API;
 * failure retains any entered ticket for checked restoration and disposal. */
bool qa_gl_gamma_prepare(qa_gl_renderer *, qa_display *, float, qa_gl_surface_ticket **, qa_error *);
/* Captures tightly packed RGBA8 rows from top to bottom after output gamma. */
bool qa_gl_capture(qa_gl_renderer *renderer, qa_buffer *out,
                   uint32_t *width, uint32_t *height, qa_error *error);
/* Captures the image published by the last successful scene swap, including
 * output gamma. Back-only drawables retain that image before swap discards it.
 * Valid until the next execution or drawable change. */
bool qa_gl_capture_presented(qa_gl_renderer *, qa_buffer *, uint32_t *, uint32_t *, qa_error *);
/* Window coordinates use OpenGL's bottom-left origin. */
bool qa_gl_read_depth(qa_gl_renderer *renderer, uint32_t x, uint32_t y,
                      float *out, qa_error *error);
/* Depth-image samples are returned as float32 in texture order: V=0 first. */
bool qa_gl_capture_depth_image(qa_gl_renderer *renderer,
                               const qa_scene_image *image, qa_buffer *out,
                               uint32_t *width, uint32_t *height,
                               qa_error *error);
bool qa_gl_set_overdraw(qa_gl_renderer *renderer, bool enabled,
                        qa_error *error);
/* Stencil rows follow GL readback order and four-byte row alignment. */
bool qa_gl_read_overdraw(qa_gl_renderer *renderer, uint8_t *destination,
                         size_t bytes, qa_error *error);

/* Builds a complete replacement window, context, and renderer before
 * publishing them. Failure leaves both caller-owned objects unchanged. */
bool qa_gl_restart(qa_gl_renderer **renderer, qa_display **display,
                   const qa_display_options *display_options,
                   const qa_gl_options *renderer_options, qa_error *error);

/* Capture the actual completed native presentation before display staging.
 * Partial failures retain a ticket for checked abort. Active tickets exclude
 * rendering, save and mutation until abort or checked retirement. Destruction
 * is deferred; keep both displays alive until the ticket has closed. */
bool qa_gl_surface_begin(qa_gl_renderer *, qa_gl_surface_ticket **, qa_error *);
/* Candidate must already be current on the SAME actual captured context. */
bool qa_gl_surface_prepare(qa_gl_surface_ticket *, qa_display *, float gamma, qa_error *);
/* Checked native readiness seals the actual endpoint and target receipt.
 * A later failure invalidates that receipt before returning. */
bool qa_gl_surface_ready(qa_gl_surface_ticket *, qa_error *);
/* Pure retained identity proof after checked native readiness. */
bool qa_gl_surface_ready_is(const qa_gl_surface_ticket *);
/* Only private ownership transfers; retire afterward at a checked boundary. */
void qa_gl_surface_publish(qa_gl_surface_ticket *);
/* Abort follows display native rollback, before releasing either display. */
bool qa_gl_surface_abort(qa_gl_surface_ticket **, qa_error *);
bool qa_gl_surface_retire(qa_gl_surface_ticket **, qa_error *);

#endif
