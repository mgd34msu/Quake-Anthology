/* Port of src/render/cpu/rasterizer.ts. TS WeakMap validation memos and the
 * per-batch lighting cache only skip repeated work, so C re-validates and
 * resolves per draw. Prepared draws free themselves in cleanup; a prepared draw
 * must not be used after it. GPL-2.0-or-later. */
#ifndef QA_RENDER_CPU_RASTERIZER_H
#define QA_RENDER_CPU_RASTERIZER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/materials/legacy_fog.h"
#include "qa/render/cpu/source.h"
#include "qa/render/cpu/triangle_kernel.h"
extern const qa_render_state QA_CPU_OPAQUE_STATE;
#define QA_CPU_OFFSET_DEPTH_BITS 24
/* TS SoftwareRenderer.capabilities. */
#define QA_CPU_TEXTURE_UNITS 2
#define QA_CPU_TEXTURE_ENV_ADD true
typedef struct qa_software_renderer qa_software_renderer;
struct qa_software_renderer { void *impl; double width, height; qa_renderer_resource_owner owner; double subpixel_bits, stencil_bits, alpha_bits; };
bool qa_software_renderer_init(qa_software_renderer *r, double width, double height, qa_renderer_resource_owner owner, double subpixel_bits, double stencil_bits, double alpha_bits);
bool qa_software_renderer_init_default(qa_software_renderer *r, double width, double height, qa_renderer_resource_owner owner);
const uint8_t *qa_software_renderer_pixels(qa_software_renderer *r, size_t *out_length);
/* TS rowPoolFailure: why row-parallel drawing fell back to serial, when worker startup failed. */
const char *qa_software_renderer_row_pool_failure(qa_software_renderer *r);
double qa_software_renderer_row_parallel_batches(qa_software_renderer *r);
typedef void (*qa_software_draw_fn)(void *ctx);
void qa_software_renderer_with_object_opacity(qa_software_renderer *r, double opacity, qa_software_draw_fn draw, void *draw_ctx);
void qa_software_renderer_set_output_gamma(qa_software_renderer *r, double gamma);
void qa_software_renderer_finish(qa_software_renderer *r);
/* out pixels are malloc'd; free with qa_image_level_destroy (mip.h). */
bool qa_software_renderer_read_rgba(qa_software_renderer *r, qa_image_level *out);
void qa_software_renderer_apply_q1_fog(qa_software_renderer *r, qa_mat4 projection, qa_q1_fog fog, double sky_fraction, const char *output);
void qa_software_renderer_close(qa_software_renderer *r);
void qa_software_renderer_apply_image_resource(qa_software_renderer *r, qa_image_resource_operation operation);
void qa_software_renderer_select_draw_buffer(qa_software_renderer *r, const char *buffer, bool clear);
void qa_software_renderer_begin_view(qa_software_renderer *r, qa_render_view_state view);
void qa_software_renderer_clear_color_buffer(qa_software_renderer *r);
double qa_software_renderer_read_depth_pixel(qa_software_renderer *r, double window_x, double window_y);
void qa_software_renderer_set_overdraw_measurement(qa_software_renderer *r, bool enabled);
void qa_software_renderer_read_stencil_overdraw(qa_software_renderer *r, uint8_t *destination, size_t length);
void qa_software_renderer_draw(qa_software_renderer *r, qa_draw_batch batch);
qa_prepared_backend_draw qa_software_renderer_prepare_geometry(qa_software_renderer *r, qa_draw_batch batch);
void qa_software_renderer_draw_stretch_pic(qa_software_renderer *r, qa_renderer_image image, qa_rect rect, qa_texture_rect uv, qa_vec4 color);
void qa_software_renderer_draw_show_image(qa_software_renderer *r, qa_renderer_image image, qa_rect rect, bool proportional);
void qa_software_renderer_draw_immediate(qa_software_renderer *r, qa_render_operation operation);
/* The scratch cells are copied; the stage's batch must outlive the prepared draw
 * (TS reads secondTexture.environment through it when the texture slot runs). */
void qa_software_renderer_begin_source_iterator(qa_software_renderer *r, bool set_arrays_once, const qa_source_stage_cell *scratch, size_t scratch_count);
qa_prepared_source_draw qa_software_renderer_prepare_source_geometry(qa_software_renderer *r, const qa_source_stage_data *stage);
qa_renderer_backend qa_software_renderer_backend(qa_software_renderer *r);
#endif
