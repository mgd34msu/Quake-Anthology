/* Port of src/render/gl/renderer.ts. The SDL render context surface is
 * fn+ctx (makeCurrent/drawable size/rendering toggle). FBO-backed
 * opacity/gamma/depth-atlas/fog passes use the reduced helpers in this
 * slice. GPL-2.0-or-later. */
#ifndef QA_RENDER_GL_RENDERER_H
#define QA_RENDER_GL_RENDERER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/render.h"
#include "qa/platform/gl.h"
typedef struct qa_gl_render_context {
  void *ctx;
  qa_gl_context gl_context;
  void (*make_current)(void *ctx);
  void (*drawable_size)(void *ctx, double *width, double *height);
  void (*set_rendering_enabled)(void *ctx, bool enabled);
} qa_gl_render_context;
typedef struct qa_gl_driver_info { const char *vendor; const char *renderer; const char *version; const char *shading_language; } qa_gl_driver_info;
typedef struct qa_gl_renderer qa_gl_renderer;
struct qa_gl_renderer { void *impl; qa_renderer_resource_owner owner; double stencil_bits, depth_bits, color_bits, alpha_bits, max_texture_size, texture_units; bool stereo_enabled; qa_gl_driver_info driver; };
bool qa_gl_renderer_init(qa_gl_renderer *renderer, qa_gl_render_context window, qa_renderer_resource_owner owner);
double qa_gl_renderer_width(qa_gl_renderer *renderer);
double qa_gl_renderer_height(qa_gl_renderer *renderer);
void qa_gl_renderer_set_output_gamma(qa_gl_renderer *renderer, double gamma);
void qa_gl_renderer_apply_image_resource(qa_gl_renderer *renderer, qa_image_resource_operation operation);
void qa_gl_renderer_select_draw_buffer(qa_gl_renderer *renderer, const char *buffer, bool clear);
void qa_gl_renderer_begin_view(qa_gl_renderer *renderer, qa_render_view_state view);
qa_prepared_backend_draw qa_gl_renderer_prepare_geometry(qa_gl_renderer *renderer, qa_draw_batch batch);
typedef void (*qa_gl_draw_fn)(void *ctx);
void qa_gl_renderer_with_object_opacity(qa_gl_renderer *renderer, double opacity, qa_gl_draw_fn draw, void *draw_ctx);
void qa_gl_renderer_draw_immediate(qa_gl_renderer *renderer, qa_render_operation operation);
void qa_gl_renderer_clear_color_buffer(qa_gl_renderer *renderer);
void qa_gl_renderer_draw_show_image(qa_gl_renderer *renderer, qa_renderer_image image, qa_rect rect, bool proportional);
void qa_gl_renderer_set_overdraw_measurement(qa_gl_renderer *renderer, bool enabled);
void qa_gl_renderer_read_stencil_overdraw(qa_gl_renderer *renderer, uint8_t *destination, size_t length);
double qa_gl_renderer_read_depth_pixel(qa_gl_renderer *renderer, double window_x, double window_y);
/* Malloc'd RGBA bytes (width*height*4); NULL on OOM. Caller frees. */
uint8_t *qa_gl_renderer_read_pixels(qa_gl_renderer *renderer, size_t *out_length);
bool qa_gl_renderer_read_depth_image(qa_gl_renderer *renderer, qa_renderer_image image, qa_depth_image_level *out, float *storage, size_t storage_count);
void qa_gl_renderer_finish(qa_gl_renderer *renderer);
void qa_gl_renderer_close(qa_gl_renderer *renderer);
qa_renderer_backend qa_gl_renderer_backend(qa_gl_renderer *renderer);
#endif
