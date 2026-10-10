#ifndef QA_CPU_INTERNAL_H
#define QA_CPU_INTERNAL_H
#include "qa/render_cpu.h"
#include "../controls_private.h"
#include "../output_domain.h"
#include "../resource_index_private.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define CPU_STATISTICS_FIELDS(X) \
  X(draws) X(brush_candidates) X(brush_predicate_rejects) \
  X(brush_planarity_rejects) X(brush_cache_rejects) X(brush_queued) \
  X(brush_batches) X(brush_spans) X(brush_covered) X(brush_written) \
  X(generic_batches) X(generic_commands) X(generic_triangles) \
  X(generic_covered) X(generic_fragments) X(generic_written) \
  X(worker_dispatches) X(worker_posts) X(worker_joins) \
  X(skin_jobs) X(skin_vertices) X(skin_cached_draws) \
  X(brush_sort_ticks) X(brush_generate_ticks) X(brush_shade_ticks) \
  X(surface_build_ticks) X(surface_builds) X(present_copy_ticks) X(presents)
extern _Thread_local qa_cpu_statistics *cpu_row_statistics;
#define CPU_STATS_ADD(renderer, field, value) do { \
  if ((renderer)->statistics_enabled) \
    (renderer)->statistics.field += (uint64_t)(value); \
} while (0)
static inline void cpu_statistics_merge(qa_cpu_statistics *to,
                                        const qa_cpu_statistics *from) {
#define CPU_STATS_MERGE(field) to->field += from->field;
  CPU_STATISTICS_FIELDS(CPU_STATS_MERGE)
#undef CPU_STATS_MERGE
}

typedef struct cpu_framebuffer {
  uint32_t width, height;
  bool depth_only, alpha;
  uint8_t *color;
  float *depth;
  uint32_t *stencil;
} cpu_framebuffer;
typedef struct cpu_target {
  const qa_scene_image *image;
  cpu_framebuffer framebuffer;
  struct cpu_target *next;
} cpu_target;
typedef struct cpu_vertex {
  double clip[4], color[4], uv[2][2], world[3], normal[3];
  uint8_t clip_mask;
} cpu_vertex;
#define CPU_SOURCE_IMAGES_QA 2048u
typedef struct cpu_source_image {
  const qa_scene_image *image;
  qa_scene_resources *owner;
  qa_scene_filter filter;
  qa_render_source_texture texture;
} cpu_source_image;
typedef struct cpu_stream_image {
  uint64_t writes;
  const qa_scene_image *image;
  qa_scene_image view;
  qa_scene_image_level level;
  struct cpu_stream_image *next;
} cpu_stream_image;
typedef struct cpu_skin_binding {
  qa_scene_skin_sample *sample;
  size_t offset;
} cpu_skin_binding;
typedef struct cpu_skin_slot {
  const void *owner;
  const qa_scene_skin_pose *pose;
  uint64_t epoch;
  size_t job;
} cpu_skin_slot;
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
  qa_vec4 clear_color;
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
  qa_render_workers *raster_pool;
  qa_render_model_job *skin_jobs;
  cpu_skin_binding *skin_bindings;
  cpu_skin_slot *skin_slots;
  qa_model_vertex *skin_transient;
  size_t skin_job_count, skin_job_capacity, skin_slot_capacity;
  size_t skin_transient_capacity;
  uint64_t skin_epoch;
  bool skin_frame_prepared;
  struct cpu_brush_context *brush_spans;
  struct cpu_surface_cache *surface_cache;
  const qa_scene_image *bound[2];
  cpu_source_image source_images[CPU_SOURCE_IMAGES_QA];
  qa_render_resource_index source_image_index;
  qa_render_resource_index stream_image_index;
  cpu_stream_image *stream_images;
  uint32_t source_image_count;
  float texture_components[3][256];
  bool texture_components_ready;
  qa_cpu_statistics statistics;
  bool statistics_enabled;
};
typedef struct cpu_derivative {
  float dudx, dvdx, dudy, dvdy;
} cpu_derivative;
void cpu_source_image_used(qa_cpu_renderer *, const qa_scene_image *);
typedef struct cpu_sampler {
  const qa_scene_image *image;
  const cpu_framebuffer *target;
  const float *components, *target_components;
  size_t level_count;
  bool linear, magnification_linear, blend, alpha;
  float magnification_limit;
} cpu_sampler;
static inline bool cpu_sampler_requires_derivatives(const cpu_sampler *sampler) {
  return sampler->level_count > 1 ||
         sampler->magnification_linear != sampler->linear;
}
typedef struct cpu_fragment {
  uint32_t x, y;
  float depth, eye_depth;
  float color[4], uv[2][2];
  cpu_derivative derivative[2];
  qa_vec3 world_position, world_normal;
} cpu_fragment;
static inline bool cpu_depth_passes(qa_scene_depth test, float depth,
                                    float old_depth) {
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
typedef void (*cpu_fragment_kernel)(qa_cpu_renderer *, const qa_scene_draw *,
    const cpu_sampler[2], const cpu_fragment *, cpu_fragment_admission);
cpu_fragment_kernel cpu_fragment_select(const qa_cpu_renderer *, const qa_scene_draw *);
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
static inline float cpu_clamp(float value) {
  return isnan(value) ? fminf(1, fmaxf(0, value))
                     : value <= 0 ? 0 : value < 1 ? value : 1;
}
static inline uint8_t cpu_byte(float value) {
  return (uint8_t)floorf(cpu_clamp(value) * 255 + 0.5f);
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
bool cpu_raster_image_pending(const qa_cpu_renderer *, const qa_scene_image *);
void cpu_raster_pool_destroy(qa_cpu_renderer *renderer);
bool cpu_raster_acquire(qa_cpu_renderer *, qa_error *);
bool cpu_skin_geometry(qa_cpu_renderer *, const qa_scene_draw *,
    const qa_model_vertex **, const uint32_t **, int *, qa_error *);
void cpu_write_fragment(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
                        const cpu_sampler samplers[2], const cpu_fragment *fragment,
                        cpu_fragment_admission admission);
bool cpu_sampler_prepare(const qa_cpu_renderer *renderer,
                          const qa_scene_image *image, cpu_sampler *sampler);
bool cpu_stream_image_admit(qa_cpu_renderer *, const qa_scene_image *, qa_error *);
const qa_scene_image *cpu_stream_image_read(const qa_cpu_renderer *, const qa_scene_image *);
bool cpu_image_region_update(qa_cpu_renderer *, const qa_scene_image_region *, qa_error *);
bool cpu_image_stream_admit(qa_cpu_renderer *, const qa_scene_image_stream *, qa_error *);
bool cpu_texture_components_init(qa_cpu_renderer *renderer, qa_error *error);
#define CPU_PIXEL_LANES 4u
typedef struct cpu_texture_coordinates {
  float u[CPU_PIXEL_LANES], v[CPU_PIXEL_LANES];
  float dudx[CPU_PIXEL_LANES], dvdx[CPU_PIXEL_LANES];
  float dudy[CPU_PIXEL_LANES], dvdy[CPU_PIXEL_LANES];
  uint8_t active;
} cpu_texture_coordinates;
typedef struct cpu_texture_color {
  float channel[4][CPU_PIXEL_LANES];
} cpu_texture_color;
void cpu_sample_texture(const cpu_sampler *, const cpu_texture_coordinates *,
                         cpu_texture_color *);
bool cpu_depth_fog(qa_cpu_renderer *renderer, const qa_scene_fog *fog,
                   const qa_scene_view *view, qa_error *error);
#endif
