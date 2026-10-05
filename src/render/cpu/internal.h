#ifndef QA_CPU_INTERNAL_H
#define QA_CPU_INTERNAL_H
#include "qa/render_cpu.h"
#include "../controls_private.h"
#include "../output_domain.h"
#include "../resource_index_private.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct cpu_framebuffer {
  uint32_t width, height;
  bool depth_only, alpha;
  uint8_t *color;
  double *depth;
  uint32_t *stencil;
} cpu_framebuffer;
typedef struct cpu_target {
  const qa_scene_image *image;
  cpu_framebuffer framebuffer;
  struct cpu_target *next;
} cpu_target;
typedef struct cpu_vertex {
  double clip[4], color[4], uv[2][2], world[3], normal[3];
} cpu_vertex;
#define CPU_SOURCE_IMAGES_QA 2048u
typedef struct cpu_source_image {
  const qa_scene_image *image;
  qa_scene_resources *owner;
  qa_scene_filter filter;
  qa_render_source_texture texture;
} cpu_source_image;
struct qa_cpu_renderer {
  qa_cpu_options options;
  qa_render_controls controls;
  qa_output_domains output_domains;
  cpu_framebuffer display, opacity;
  cpu_framebuffer *current, *opacity_parent;
  cpu_target *targets;
  qa_scene_view view;
  qa_scene_state pipeline;
  float clear_depth;
  qa_scene_vec4 clear_color;
  qa_scene_rect opacity_viewport;
  bool opacity_active, opacity_skip, gamma_enabled, overdraw;
  bool preblend_gamma, source_frame;
  bool executing, presenting, capturing;
  qa_cpu_surface_ticket *surface_ticket;
  bool destroy_pending;
  float opacity_value, gamma_value;
  uint32_t stencil_maximum;
  uint8_t gamma[256], *output;
  struct cpu_vertex *vertices;
  size_t vertex_capacity;
  struct cpu_raster_pool *raster_pool;
  const qa_scene_image *bound[2];
  cpu_source_image source_images[CPU_SOURCE_IMAGES_QA];
  qa_render_resource_index source_image_index;
  uint32_t source_image_count;
  double texture_components[3][256];
  bool texture_components_ready;
};
typedef struct cpu_derivative {
  double dudx, dvdx, dudy, dvdy;
} cpu_derivative;
typedef struct cpu_sampler {
  const qa_scene_image *image;
  const cpu_framebuffer *target;
  const double *components, *target_components;
  size_t level_count;
  bool linear, magnification_linear, blend, alpha, inexact;
  double magnification_limit;
} cpu_sampler;
static inline bool cpu_sampler_requires_derivatives(const cpu_sampler *sampler) {
  return sampler->level_count > 1 ||
         sampler->magnification_linear != sampler->linear;
}
typedef struct cpu_fragment {
  uint32_t x, y;
  double depth, eye_depth;
  double color[4], uv[2][2];
  cpu_derivative derivative[2];
  qa_vec3 world_position, world_normal;
} cpu_fragment;
static inline bool cpu_depth_passes(qa_scene_depth test, double depth,
                                    double old_depth) {
  return test == QA_DEPTH_ALWAYS || test == QA_DEPTH_DISABLED ||
         (test == QA_DEPTH_LEQUAL && depth <= old_depth) ||
         (test == QA_DEPTH_EQUAL && depth == old_depth) ||
         (test == QA_DEPTH_LESS && depth < old_depth) ||
         (test == QA_DEPTH_GEQUAL && depth >= old_depth);
}
static inline bool cpu_stencil_active(const qa_cpu_renderer *renderer,
                                      const qa_scene_state *state) {
  return (state->stencil_enabled || renderer->overdraw) &&
         renderer->current->stencil;
}
typedef struct cpu_fragment_admission {
  size_t index;
  bool depth_passed, stencil;
} cpu_fragment_admission;
static inline cpu_fragment_admission cpu_fragment_admit(
    const qa_cpu_renderer *renderer, const qa_scene_state *state,
    const cpu_fragment *fragment, bool stencil) {
  size_t index = (size_t)fragment->y * renderer->current->width + fragment->x;
  return (cpu_fragment_admission){
      .index = index,
      .depth_passed = cpu_depth_passes(state->depth_test, fragment->depth,
                                     renderer->current->depth[index]),
      .stencil = stencil};
}
static inline double cpu_clamp(double value) {
  return isnan(value) ? fmin(1, fmax(0, value))
                     : value <= 0 ? 0 : value < 1 ? value : 1;
}
static inline uint8_t cpu_byte(double value) {
  return (uint8_t)floor(cpu_clamp(value) * 255 + 0.5);
}
/* Target versions are backend-owned; returned storage borrows renderer
 * lifetime. */
const cpu_framebuffer *cpu_target_find(const qa_cpu_renderer *renderer,
                                       const qa_scene_image *image);
bool cpu_image_valid(const qa_scene_image *image, qa_error *error);
bool cpu_draw(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
              qa_error *error);
bool cpu_draw_queued(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
                     qa_error *error);
void cpu_raster_flush(qa_cpu_renderer *renderer);
void cpu_raster_pool_create(qa_cpu_renderer *renderer);
void cpu_raster_pool_destroy(qa_cpu_renderer *renderer);
void cpu_write_fragment(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
                        const cpu_sampler samplers[2], const cpu_fragment *fragment,
                        cpu_fragment_admission admission);
bool cpu_sampler_prepare(const qa_cpu_renderer *renderer,
                          const qa_scene_image *image, cpu_sampler *sampler);
bool cpu_texture_components_init(qa_cpu_renderer *renderer, qa_error *error);
void cpu_sample_texture(const cpu_sampler *sampler, double u, double v,
                        double rho, double out[4]);
bool cpu_depth_fog(qa_cpu_renderer *renderer, const qa_scene_fog *fog,
                   const qa_scene_view *view, qa_error *error);
#endif
