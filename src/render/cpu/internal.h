#ifndef QA_CPU_INTERNAL_H
#define QA_CPU_INTERNAL_H
#include "qa/render_cpu.h"
#include "../controls_private.h"
#include "../output_domain.h"
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
struct qa_cpu_renderer {
  qa_cpu_options options;
  qa_render_controls controls;
  qa_output_domains output_domains;
  cpu_framebuffer display, opacity;
  cpu_framebuffer *current, *opacity_parent;
  cpu_target *targets;
  qa_scene_view view;
  bool depth_write, color_write;
  float clear_depth;
  qa_scene_rect opacity_viewport;
  bool opacity_active, opacity_skip, gamma_enabled, overdraw;
  bool executing, presenting, capturing;
  qa_cpu_surface_ticket *surface_ticket;
  bool destroy_pending;
  float opacity_value, gamma_value;
  uint32_t stencil_maximum;
  uint8_t gamma[256], *output;
  struct cpu_vertex *vertices;
  size_t vertex_capacity;
  const qa_scene_image *bound[2];
};
typedef struct cpu_derivative {
  double dudx, dvdx, dudy, dvdy;
} cpu_derivative;
typedef struct cpu_fragment {
  uint32_t x, y;
  double depth, eye_depth;
  double color[4], uv[2][2];
  cpu_derivative derivative[2];
  qa_vec3 world_position, world_normal;
} cpu_fragment;
static inline double cpu_clamp(double value) { return fmin(1, fmax(0, value)); }
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
void cpu_write_fragment(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
                        const cpu_fragment *fragment);
void cpu_sample_texture(const qa_cpu_renderer *renderer,
                        const qa_scene_image *image, double u, double v,
                        double rho, double out[4]);
bool cpu_depth_fog(qa_cpu_renderer *renderer, const qa_scene_fog *fog,
                   const qa_scene_view *view, qa_error *error);
#endif
