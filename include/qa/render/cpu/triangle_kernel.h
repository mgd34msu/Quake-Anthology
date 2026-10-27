/* Port of src/render/cpu/triangle-kernel.ts. The general fragment loop is
 * ported; the TS opaque/depth-only fast paths are behavior-identical
 * specializations and are folded into it. GPL-2.0-or-later. */
#ifndef QA_RENDER_CPU_TRIANGLE_KERNEL_H
#define QA_RENDER_CPU_TRIANGLE_KERNEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
typedef struct qa_cpu_sample { double r, g, b, a; } qa_cpu_sample;
/* TS ImageInternalFormat: "rgb8"|"rgba8". */
typedef const char *qa_image_internal_format;
extern const bool QA_CPU_LITTLE_ENDIAN;
double qa_cpu_clamp01(double value);
double qa_cpu_byte(double value);
uint32_t qa_cpu_color_word(double r, double g, double b, double a);
double qa_cpu_blend(double source, double destination, double source_alpha, double destination_alpha, qa_blend_state blend, bool alpha_channel);
bool qa_cpu_passes_alpha(double alpha, const char *test);
double qa_cpu_texture_color(double previous, double texel, const char *environment);
bool qa_cpu_texture_has_alpha(const char *internal_format);
typedef struct qa_texture_storage { double width, height; uint8_t *pixels; size_t pixel_length; const char *internal_format; bool has_alpha; double component_maximum; bool has_uniform; qa_cpu_sample uniform; } qa_texture_storage;
typedef enum qa_bound_texture_kind { QA_BOUND_TEXTURE_IMAGE, QA_BOUND_TEXTURE_CONSTANT, QA_BOUND_TEXTURE_INCOMPLETE } qa_bound_texture_kind;
typedef struct qa_bound_texture { qa_bound_texture_kind kind; const char *internal_format; const char *wrap; bool minify_linear, magnify_linear; const char *mipmapping; double magnification_limit; const qa_texture_storage *levels; size_t level_count; qa_vec4 border_color; qa_cpu_sample sample; } qa_bound_texture;
typedef struct qa_texture_plane_derivative { double u_anchor, v_anchor, u_dx, v_dx, q_dx, u_dy, v_dy, q_dy; } qa_texture_plane_derivative;
void qa_cpu_sample_bound(const qa_bound_texture *texture, double u, double v, double rho, qa_cpu_sample *output);
typedef struct qa_line_texture_derivative { double ds_per_pixel, dt_per_pixel; } qa_line_texture_derivative;
void qa_cpu_sample_line_bound(const qa_bound_texture *texture, qa_vec2 coordinate, qa_line_texture_derivative derivative, qa_cpu_sample *output);
typedef struct qa_cpu_framebuffer { double width, height; uint8_t *pixels; int32_t *color_words; double *depth; uint32_t *stencil; double origin_x, origin_y, stride; size_t storage_count; } qa_cpu_framebuffer;
struct qa_cpu_lighting; struct qa_cpu_triangle_lighting;
typedef struct qa_triangle_setup {
  bool has_fog_depth_scale; double fog_depth_scale; const qa_batch_fog *fog; const char *texture_effect;
  const struct qa_cpu_triangle_lighting *lighting;
  double min_x, max_x, min_y, max_y, inverse_area, depth_near, depth_far;
  double edge_ax, edge_ay, edge_ac, edge_bx, edge_by, edge_bc, edge_cx, edge_cy, edge_cc;
  double attribute_ax, attribute_ay, attribute_ac, attribute_bx, attribute_by, attribute_bc, attribute_cx, attribute_cy, attribute_cc;
  double az, bz, cz, polygon_depth_offset, plane_depth, aiw, biw, ciw;
  double au, bu, cu, av, bv, cv, au2, bu2, cu2, av2, bv2, cv2;
  double ar, br, cr, ag, bg, cg, ab, bb, cb, aa, ba, ca;
  bool edge_a_inclusive, edge_b_inclusive, edge_c_inclusive, white, depth_write, stencil_enabled, color_write, texture_consumed, primary_alpha, secondary_alpha, constant_depth;
  double width, height; qa_blend_state blend; double alpha_bits; const char *blending; const char *depth_test; const char *alpha_test; const char *secondary_environment;
  qa_bound_texture texture, secondary_texture; qa_texture_plane_derivative derivative, secondary_derivative;
  const char *stencil_function; double stencil_compare_mask, stencil_write_mask, stencil_maximum; const char *stencil_depth_fail; const char *stencil_depth_pass;
} qa_triangle_setup;
bool qa_run_triangle_rows(const qa_triangle_setup *setup, qa_cpu_framebuffer *framebuffer, qa_cpu_sample *sampled, double first_y, double last_y);
bool qa_run_triangle_rows_default(const qa_triangle_setup *setup, qa_cpu_framebuffer *framebuffer, qa_cpu_sample *sampled);
bool qa_run_worker_triangle_rows(const qa_triangle_setup *setup, qa_cpu_framebuffer *framebuffer, qa_cpu_sample *sampled, double first_y, double last_y);
bool qa_cpu_stencil_fragment(uint32_t *stencil, size_t stencil_count, double index, bool depth_passed, const char *function, double compare_mask, double write_mask, double maximum, const char *depth_fail, const char *depth_pass);
#endif
