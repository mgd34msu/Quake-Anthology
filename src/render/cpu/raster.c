#include "internal.h"
#include <limits.h>
#include <fenv.h>
#include <SDL_thread.h>
#include <SDL_mutex.h>
#include <SDL_cpuinfo.h>

/* Clip attributes remain unpacked only in reusable transform storage and the
 * bounded stack polygon. Texture versions and submitted meshes stay shared. */
typedef struct screen_vertex {
  double x, y, z, q, scale;
  const cpu_vertex *vertex;
} screen_vertex;
typedef struct edge_equation {
  double x, y, c;
  bool inclusive;
} edge_equation;

typedef struct cpu_scissor {
  int64_t x0, y0, x1, y1;
} cpu_scissor;
typedef struct cpu_triangle {
  cpu_vertex values[3];
  screen_vertex vertices[3];
  edge_equation coverage[3], attributes[3];
  cpu_scissor bounds;
  double inverse_area, near_depth, far_depth, q_dx, q_dy, offset;
  double uv[2][2][3], uv_dx[2][2], uv_dy[2][2];
  bool constant_depth;
} cpu_triangle;
typedef struct cpu_triangle_output {
  struct cpu_raster_pool *pool;
  bool failed;
} cpu_triangle_output;
typedef struct cpu_raster_command {
  qa_scene_draw draw;
  cpu_sampler samplers[2];
  cpu_scissor bounds;
  int64_t first_y, last_y;
  size_t first, count;
  fenv_t environment;
} cpu_raster_command;
typedef struct cpu_raster_job {
  qa_cpu_renderer *renderer;
  const qa_scene_draw *draw;
  const cpu_sampler *samplers;
  qa_render_primitive_mode mode;
  cpu_scissor bounds;
  const cpu_triangle *triangles;
  size_t triangle_count;
  const cpu_raster_command *commands;
  size_t command_count, completed_commands;
  fenv_t environment;
  int exceptions;
  bool completed;
} cpu_raster_job;
typedef struct cpu_raster_worker {
  SDL_Thread *thread;
  SDL_sem *start, *done;
  cpu_raster_job job;
  bool stop;
} cpu_raster_worker;
struct cpu_raster_pool {
  cpu_triangle *triangles;
  size_t triangle_count, triangle_capacity;
  cpu_raster_command *commands;
  size_t command_count, command_capacity;
  unsigned count;
  cpu_raster_worker workers[];
};
static void raster_prepared_draw(const cpu_raster_job *job);
static bool raster_command_overlaps(const cpu_raster_job *batch, size_t index) {
  const cpu_raster_command *command = &batch->commands[index];
  return command->first_y <= batch->bounds.y1 &&
         command->last_y >= batch->bounds.y0;
}
static cpu_raster_job raster_command_job(const cpu_raster_job *batch,
                                         size_t index) {
  const cpu_raster_command *command = &batch->commands[index];
  cpu_raster_job job = *batch;
  job.draw = &command->draw;
  job.samplers = command->samplers;
  job.triangles = batch->triangles + command->first;
  job.triangle_count = command->count;
  job.bounds = command->bounds;
  if (job.bounds.y0 < batch->bounds.y0) job.bounds.y0 = batch->bounds.y0;
  if (job.bounds.y1 > batch->bounds.y1) job.bounds.y1 = batch->bounds.y1;
  job.commands = NULL;
  job.command_count = 0;
  return job;
}
static int SDLCALL raster_worker(void *context) {
  cpu_raster_worker *worker = context;
  for (;;) {
    SDL_SemWait(worker->start);
    if (worker->stop) return 0;
    if (worker->job.command_count) {
      worker->job.exceptions = 0;
      worker->job.completed_commands = 0;
      for (size_t i = 0; i < worker->job.command_count; ++i) {
        if (raster_command_overlaps(&worker->job, i)) {
          if (fesetenv(&worker->job.commands[i].environment) != 0) break;
          cpu_raster_job job = raster_command_job(&worker->job, i);
          raster_prepared_draw(&job);
          worker->job.exceptions |= fetestexcept(FE_ALL_EXCEPT);
        }
        ++worker->job.completed_commands;
      }
      worker->job.completed =
          worker->job.completed_commands == worker->job.command_count;
    } else {
      worker->job.completed = fesetenv(&worker->job.environment) == 0;
      if (worker->job.completed) {
        raster_prepared_draw(&worker->job);
        worker->job.exceptions = fetestexcept(FE_ALL_EXCEPT);
      }
    }
    SDL_SemPost(worker->done);
  }
}
void cpu_raster_pool_destroy(qa_cpu_renderer *renderer) {
  struct cpu_raster_pool *pool = renderer->raster_pool;
  if (!pool) return;
  cpu_raster_flush(renderer);
  for (unsigned i = 0; i < pool->count; ++i)
    if (pool->workers[i].thread) {
      pool->workers[i].stop = true;
      SDL_SemPost(pool->workers[i].start);
    }
  for (unsigned i = 0; i < pool->count; ++i) {
    cpu_raster_worker *worker = &pool->workers[i];
    if (worker->thread) SDL_WaitThread(worker->thread, NULL);
    if (worker->start) SDL_DestroySemaphore(worker->start);
    if (worker->done) SDL_DestroySemaphore(worker->done);
  }
  free(pool->triangles);
  free(pool->commands);
  free(pool);
  renderer->raster_pool = NULL;
}
void cpu_raster_pool_create(qa_cpu_renderer *renderer) {
  int cpus = SDL_GetCPUCount();
  if (cpus < 2) return;
  unsigned count = (unsigned)(cpus - 1);
  if (sizeof(cpu_raster_worker) >
      (SIZE_MAX - sizeof(struct cpu_raster_pool)) / count) return;
  struct cpu_raster_pool *pool = calloc(1, sizeof(*pool) +
      (size_t)count * sizeof(cpu_raster_worker));
  if (!pool) return;
  renderer->raster_pool = pool;
  pool->count = count;
  for (unsigned i = 0; i < pool->count; ++i) {
    cpu_raster_worker *worker = &pool->workers[i];
    worker->start = SDL_CreateSemaphore(0);
    worker->done = SDL_CreateSemaphore(0);
    if (worker->start && worker->done)
      worker->thread = SDL_CreateThread(raster_worker, "CPU raster", worker);
    if (!worker->thread) {
      cpu_raster_pool_destroy(renderer);
      return;
    }
  }
}
static cpu_scissor scissor(const qa_cpu_renderer *renderer) {
  qa_scene_rect v = renderer->view.viewport;
  int64_t right = (int64_t)v.x + v.width - 1,
          bottom = (int64_t)v.y + v.height - 1;
  return (cpu_scissor){v.x > 0 ? v.x : 0, v.y > 0 ? v.y : 0,
                       right < (int64_t)renderer->current->width - 1
                           ? right
                           : (int64_t)renderer->current->width - 1,
                       bottom < (int64_t)renderer->current->height - 1
                           ? bottom
                           : (int64_t)renderer->current->height - 1};
}
static bool finite3(qa_vec3 v) {
  return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}
static bool finite4(qa_scene_vec4 v) {
  return isfinite(v.x) && isfinite(v.y) && isfinite(v.z) && isfinite(v.w);
}
bool cpu_image_valid(const qa_scene_image *image, qa_error *error) {
  if (!image)
    return true;
  if (!image->levels || !image->level_count ||
      (unsigned)image->kind > QA_SCENE_DEPTH32F ||
      (unsigned)image->filter > QA_SCENE_LINEAR_MIPMAP_LINEAR ||
      (unsigned)image->wrap > QA_SCENE_CLAMP || !finite4(image->border) ||
      (image->source_q3 && (unsigned)image->source_format>QA_Q3_TEXTURE_RGB4_S3TC)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid CPU texture descriptor");
    return false;
  }
  if (image->source_q3 && image->source_format==QA_Q3_TEXTURE_RGB4_S3TC) {
    qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"CPU texture storage does not support the S3TC diagnostic profile");
    return false;
  }
  for (size_t i = 0; i < image->level_count; ++i) {
    const qa_scene_image_level *level = &image->levels[i];
    if (!level->width || !level->height ||
        (size_t)level->width > SIZE_MAX / level->height ||
        (size_t)level->width * level->height > SIZE_MAX / 4 || !level->pixels ||
        level->bytes < (size_t)level->width * level->height * 4) {
      qa_error_set(error, QA_ERROR_ARGUMENT, i,
                   "Invalid CPU texture level storage");
      return false;
    }
  }
  return true;
}
static bool draw_valid(const qa_scene_draw *draw, qa_error *error) {
  const qa_scene_state *s = &draw->state;
  if (draw->texture_count > 2 ||
      (unsigned)draw->environment > QA_TEXTURE_REPLACE ||
      (unsigned)draw->lighting > QA_LIGHT_Q2_MODEL_SHADOW ||
      (unsigned)draw->light_pass > QA_LIGHT_PASS_MODEL ||
      (unsigned)draw->mesh.primitive > QA_SCENE_LINES ||
      (unsigned)s->blend_source > QA_BLEND_SRC_ALPHA_SATURATE ||
      (unsigned)s->blend_destination > QA_BLEND_SRC_ALPHA_SATURATE ||
      (unsigned)s->depth_test > QA_DEPTH_DISABLED ||
      (unsigned)s->alpha_test > QA_ALPHA_GE128 ||
      (unsigned)s->cull > QA_CULL_BACK ||
      (unsigned)s->stencil_test > QA_STENCIL_NOTEQUAL ||
      (unsigned)s->stencil_fail > QA_STENCIL_INVERT ||
      (unsigned)s->stencil_depth_fail > QA_STENCIL_INVERT ||
      (unsigned)s->stencil_depth_pass > QA_STENCIL_INVERT ||
      !isfinite(s->depth_near) || !isfinite(s->depth_far) ||
      !isfinite(s->offset_factor) || !isfinite(s->offset_units) ||
      !isfinite(s->line_width) ||
      ((draw->mesh.primitive == QA_SCENE_LINES || s->wireframe) &&
       s->line_width <= 0) ||
      (unsigned)draw->fog.kind > QA_FOG_Q2 ||
      (unsigned)draw->fog.effect > QA_FOG_NO_EFFECT ||
      !finite3(draw->fog.color) || !isfinite(draw->fog.density) ||
      !isfinite(draw->fog.amount) || !isfinite(draw->shade_scale) ||
      !isfinite(draw->shadow_near) ||
      !material_source_vertex_storage_valid(draw) ||
      (draw->mesh.vertex_count && !draw->mesh.vertices) ||
      (draw->mesh.index_count && !draw->mesh.indices) ||
      (draw->light_count && !draw->lights)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid CPU draw state or storage");
    return false;
  }
  for (size_t i = 0; i < 16; ++i)
    if (!isfinite(draw->model.m[i]) || !isfinite(draw->mvp.m[i])) {
      qa_error_set(error, QA_ERROR_ARGUMENT, i, "Nonfinite CPU draw matrix");
      return false;
    }
  size_t stride = draw->mesh.primitive == QA_SCENE_LINES ? 2 : 3;
  if (draw->mesh.index_count % stride) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "CPU primitive index count is incomplete");
    return false;
  }
  for (size_t i = 0; i < draw->texture_count; ++i)
    if (!cpu_image_valid(draw->textures[i], error))
      return false;
  if (draw->shadow_atlas && (!cpu_image_valid(draw->shadow_atlas, error) ||
                             draw->shadow_atlas->kind != QA_SCENE_DEPTH32F)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "CPU shadow atlas must contain depth pixels");
    return false;
  }
  if ((draw->lighting == QA_LIGHT_Q2_MODEL_SHADOW || draw->model_shade_scale) &&
      !draw->shadow_atlas) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "CPU model shadows require a depth atlas");
    return false;
  }
  for (size_t i = 0; i < draw->light_count; ++i) {
    const qa_scene_shadow_light *shadow = &draw->lights[i];
    const qa_scene_light *light = &shadow->light;
    if (!finite3(light->origin) || !finite3(light->direction) ||
        !finite3(light->color) || !finite3(shadow->model_fraction) ||
        !isfinite(light->radius) || light->radius < 0 ||
        !isfinite(light->scale) || !isfinite(light->cos_half_angle) ||
        (shadow->shadow_valid &&
         (!draw->shadow_atlas || !finite4(shadow->atlas_rect) ||
          shadow->atlas_rect.z <= 0 || shadow->atlas_rect.w <= 0 ||
          (shadow->point_shadow && draw->shadow_near <= 0)))) {
      qa_error_set(error, QA_ERROR_ARGUMENT, i, "Invalid CPU fragment light");
      return false;
    }
    if (shadow->shadow_valid)
      for (size_t c = 0; c < 16; ++c)
        if (!isfinite(shadow->shadow_matrix.m[c])) {
          qa_error_set(error, QA_ERROR_ARGUMENT, i,
                       "Nonfinite CPU shadow matrix");
          return false;
        }
  }
  return true;
}
static bool transform(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
                      qa_render_primitive_mode mode, qa_error *error) {
  size_t count = draw->mesh.vertex_count;
  bool referenced[QA_SOURCE_TESS_VERTICES]={0};
  if (draw->source_vertex_storage)
    for (size_t i=0;i<draw->mesh.index_count;++i) {
      size_t index=draw->mesh.indices[i];
      referenced[index]=true;
      if (index>=count) count=index+1;
    }
  if (count > SIZE_MAX / sizeof(cpu_vertex)) {
    qa_error_set(error, QA_ERROR_MEMORY, 0,
                 "CPU transformed vertex storage overflow");
    return false;
  }
  if (count > renderer->vertex_capacity) {
    size_t capacity =
        renderer->vertex_capacity ? renderer->vertex_capacity : 256;
    while (capacity < count && capacity <= SIZE_MAX / 2)
      capacity *= 2;
    if (capacity < count || capacity > SIZE_MAX / sizeof(cpu_vertex))
      capacity = count;
    cpu_vertex *vertices =
        realloc(renderer->vertices, capacity * sizeof(*vertices));
    if (!vertices) {
      qa_error_set(error, QA_ERROR_MEMORY, 0,
                   "Allocating CPU transformed vertices");
      return false;
    }
    renderer->vertices = vertices;
    renderer->vertex_capacity = capacity;
  }
  const float *model = draw->model.m;
  /* Inverse transpose keeps fragment normals correct for scaled model
   * instances. */
  double normal[9] = {
      (double)model[5] * model[10] - (double)model[9] * model[6],
      (double)model[9] * model[2] - (double)model[1] * model[10],
      (double)model[1] * model[6] - (double)model[5] * model[2],
      (double)model[8] * model[6] - (double)model[4] * model[10],
      (double)model[0] * model[10] - (double)model[8] * model[2],
      (double)model[4] * model[2] - (double)model[0] * model[6],
      (double)model[4] * model[9] - (double)model[8] * model[5],
      (double)model[8] * model[1] - (double)model[0] * model[9],
      (double)model[0] * model[5] - (double)model[4] * model[1]};
  double determinant =
      model[0] * normal[0] + model[4] * normal[1] + model[8] * normal[2];
  if (determinant != 0)
    for (size_t c = 0; c < 9; ++c)
      normal[c] /= determinant;
  for (size_t i = 0; i < count; ++i) {
    if (draw->source_vertex_storage && i>=draw->mesh.vertex_count && !referenced[i]) continue;
    const qa_scene_vertex *v = &draw->mesh.vertices[i];
    if (!finite3(v->position) || !finite3(v->normal) || !finite4(v->color) ||
        !isfinite(v->texcoord.x) || !isfinite(v->texcoord.y) ||
        !isfinite(v->lightmap.x) || !isfinite(v->lightmap.y)) {
      qa_error_set(error, QA_ERROR_ARGUMENT, i, "Nonfinite CPU vertex");
      return false;
    }
    cpu_vertex *out = &renderer->vertices[i];
    const float *m = draw->mvp.m;
    for (size_t c = 0; c < 4; ++c)
      out->clip[c] = (double)m[c] * v->position.x +
                     (double)m[c + 4] * v->position.y +
                     (double)m[c + 8] * v->position.z + m[c + 12];
    for (size_t c = 0; c < 3; ++c) {
      out->world[c] = (double)model[c] * v->position.x +
                      (double)model[c + 4] * v->position.y +
                      (double)model[c + 8] * v->position.z + model[c + 12];
      out->normal[c] = normal[c * 3] * v->normal.x +
                       normal[c * 3 + 1] * v->normal.y +
                       normal[c * 3 + 2] * v->normal.z;
    }
    qa_scene_vec4 color; qa_scene_vec2 uv[2];
    qa_render_source_attributes_vertex(&renderer->controls,draw,mode,i,v,&color,uv);
    out->color[0] = color.x;
    out->color[1] = color.y;
    out->color[2] = color.z;
    out->color[3] = color.w;
    out->uv[0][0] = uv[0].x;
    out->uv[0][1] = uv[0].y;
    out->uv[1][0] = uv[1].x;
    out->uv[1][1] = uv[1].y;
  }
  return true;
}
static double distance(const cpu_vertex *vertex, unsigned plane,
                       const qa_scene_view *view) {
  if (plane < 6)
    return vertex->clip[3] +
           ((plane & 1) ? -vertex->clip[plane / 2] : vertex->clip[plane / 2]);
  return vertex->world[0] * view->clip_plane.normal.x +
         vertex->world[1] * view->clip_plane.normal.y +
         vertex->world[2] * view->clip_plane.normal.z -
         view->clip_plane.distance;
}
static cpu_vertex intersection(const cpu_vertex *a, const cpu_vertex *b,
                               double da, double db, unsigned plane) {
  double scale = fmax(fabs(da), fabs(db)), ad = fabs(da) / scale,
         bd = fabs(db) / scale;
  double aw = bd / (ad + bd), bw = ad / (ad + bd);
  cpu_vertex out;
  for (size_t c = 0; c < 4; ++c) {
    out.clip[c] = a->clip[c] * aw + b->clip[c] * bw;
    out.color[c] = a->color[c] * aw + b->color[c] * bw;
  }
  for (size_t c = 0; c < 3; ++c) {
    out.world[c] = a->world[c] * aw + b->world[c] * bw;
    out.normal[c] = a->normal[c] * aw + b->normal[c] * bw;
  }
  for (size_t unit = 0; unit < 2; ++unit)
    for (size_t c = 0; c < 2; ++c) {
      double anchor = fmin(a->uv[unit][c], b->uv[unit][c]);
      out.uv[unit][c] = anchor + (a->uv[unit][c] - anchor) * aw +
                        (b->uv[unit][c] - anchor) * bw;
    }
  if (plane < 6)
    out.clip[plane / 2] = (plane & 1) ? out.clip[3] : -out.clip[3];
  return out;
}
static size_t clip_polygon(const cpu_vertex input[3], const qa_scene_view *view,
                           cpu_vertex result[32]) {
  cpu_vertex work[2][32];
  memcpy(work[0], input, 3 * sizeof(*input));
  size_t count = 3;
  unsigned source = 0;
  for (unsigned plane = 0; plane < (view->clip_enabled ? 7u : 6u) && count;
       ++plane) {
    unsigned destination = source ^ 1;
    size_t produced = 0;
    const cpu_vertex *previous = &work[source][count - 1];
    double previous_distance = distance(previous, plane, view);
    for (size_t i = 0; i < count; ++i) {
      const cpu_vertex *current = &work[source][i];
      double current_distance = distance(current, plane, view);
      if ((current_distance >= 0) != (previous_distance >= 0))
        work[destination][produced++] = intersection(
            previous, current, previous_distance, current_distance, plane);
      if (current_distance >= 0)
        work[destination][produced++] = *current;
      previous = current;
      previous_distance = current_distance;
    }
    count = produced;
    source = destination;
  }
  size_t produced = 0;
  for (size_t i = 0; i < count; ++i)
    if (work[source][i].clip[3] > 0)
      result[produced++] = work[source][i];
  return produced;
}
static double snap(double value, double scale) {
  double scaled = (float)value * scale, lower = floor(scaled),
         fraction = scaled - lower;
  return (fraction < 0.5 || (fraction == 0.5 && fmod(lower, 2) == 0)
              ? lower
              : lower + 1) /
         scale;
}
static screen_vertex project(const cpu_vertex *vertex,
                             const qa_cpu_renderer *renderer, double scale) {
  qa_scene_rect view = renderer->view.viewport;
  double subpixel = (double)(UINT32_C(1) << renderer->options.subpixel_bits),
         w = vertex->clip[3];
  return (screen_vertex){
      snap(view.x + (vertex->clip[0] / w + 1) * view.width * 0.5, subpixel),
      snap(view.y + (1 - vertex->clip[1] / w) * view.height * 0.5, subpixel),
      vertex->clip[2] / w,
      scale / w,
      scale,
      vertex};
}
static edge_equation edge(screen_vertex a, screen_vertex b) {
  return (edge_equation){a.y - b.y, b.x - a.x, a.x * b.y - a.y * b.x,
                         b.y < a.y || (b.y == a.y && b.x < a.x)};
}
static double evaluate(edge_equation edge, double x, double y) {
  return edge.x * x + edge.y * y + edge.c;
}
/* Trim once per row, then shade its contiguous covered span. */
static void trim(int64_t *left, int64_t *right, edge_equation edge, double y) {
  if (*left > *right)
    return;
  double row = edge.y * y;
  if (edge.x == 0) {
    double value = row + edge.c;
    if (value < 0 || (value == 0 && !edge.inclusive))
      *right = *left - 1;
    return;
  }
  double crossing = -(row + edge.c) / edge.x - 0.5;
  if (edge.x > 0) {
    int64_t x = (int64_t)fmax((double)*left,
                              fmin((double)(*right + 1), floor(crossing)));
    while (x > *left) {
      double value = edge.x * ((double)x - 0.5) + row + edge.c;
      if (!(value > 0 || (value == 0 && edge.inclusive)))
        break;
      --x;
    }
    while (x <= *right) {
      double value = edge.x * ((double)x + 0.5) + row + edge.c;
      if (value > 0 || (value == 0 && edge.inclusive))
        break;
      ++x;
    }
    *left = x;
  } else {
    int64_t x = (int64_t)fmax((double)(*left - 1),
                              fmin((double)*right, ceil(crossing)));
    while (x < *right) {
      double value = edge.x * ((double)x + 1.5) + row + edge.c;
      if (!(value > 0 || (value == 0 && edge.inclusive)))
        break;
      ++x;
    }
    while (x >= *left) {
      double value = edge.x * ((double)x + 0.5) + row + edge.c;
      if (value > 0 || (value == 0 && edge.inclusive))
        break;
      --x;
    }
    *right = x;
  }
}
static bool triangle_prepare(cpu_triangle *out, const qa_scene_draw *draw,
                     screen_vertex a, screen_vertex b, screen_vertex c,
                     const screen_vertex *interpolation, cpu_scissor bounds) {
  double area = evaluate(edge(a, b), c.x, c.y);
  if (!isfinite(area) || area == 0 ||
      (draw->state.cull == QA_CULL_BACK && area > 0) ||
      (draw->state.cull == QA_CULL_FRONT && area < 0))
    return false;
  if (area < 0) {
    screen_vertex swap = b;
    b = c;
    c = swap;
    area = -area;
  }
  double min_x = fmax((double)bounds.x0, ceil(fmin(a.x, fmin(b.x, c.x)) - 0.5));
  double max_x = fmin((double)bounds.x1, floor(fmax(a.x, fmax(b.x, c.x)) - 0.5));
  double min_y = fmax((double)bounds.y0, ceil(fmin(a.y, fmin(b.y, c.y)) - 0.5));
  double max_y = fmin((double)bounds.y1, floor(fmax(a.y, fmax(b.y, c.y)) - 0.5));
  if (min_x > max_x || min_y > max_y)
    return false;
  edge_equation coverage[3] = {edge(b, c), edge(c, a), edge(a, b)};
  screen_vertex vertices[3] = {a, b, c};
  if (interpolation)
    memcpy(vertices, interpolation, sizeof(vertices));
  edge_equation attributes[3] = {edge(vertices[1], vertices[2]),
                                 edge(vertices[2], vertices[0]),
                                 edge(vertices[0], vertices[1])};
  double inverse_area =
      1 / evaluate(attributes[2], vertices[2].x, vertices[2].y);
  double near_depth = cpu_clamp(draw->state.depth_near),
         far_depth = cpu_clamp(draw->state.depth_far);
  double slope_x = 0, slope_y = 0, q_dx = 0, q_dy = 0;
  double uv[2][2][3] = {{{0}}}, uv_dx[2][2] = {{0}}, uv_dy[2][2] = {{0}};
  for (size_t i = 0; i < 3; ++i) {
    slope_x += vertices[i].z * attributes[i].x;
    slope_y += vertices[i].z * attributes[i].y;
    q_dx += vertices[i].q * attributes[i].x;
    q_dy += vertices[i].q * attributes[i].y;
    for (size_t unit = 0; unit < draw->texture_count; ++unit) {
      if (!draw->textures[unit])
        continue;
      for (size_t axis = 0; axis < 2; ++axis) {
        uv[unit][axis][i] = (vertices[i].vertex->uv[unit][axis] -
                             vertices[0].vertex->uv[unit][axis]) *
                            vertices[i].q;
        uv_dx[unit][axis] += uv[unit][axis][i] * attributes[i].x;
        uv_dy[unit][axis] += uv[unit][axis][i] * attributes[i].y;
      }
    }
  }
  double slope = fmax(fabs(slope_x), fabs(slope_y)) * fabs(inverse_area) * 0.5 *
                 fabs(far_depth - near_depth);
  double offset = draw->state.polygon_offset
                      ? slope * draw->state.offset_factor +
                            0x1p-24 * draw->state.offset_units
                      : 0;
  q_dx *= inverse_area;
  q_dy *= inverse_area;
  for (size_t unit = 0; unit < draw->texture_count; ++unit) {
    if (!draw->textures[unit])
      continue;
    for (size_t axis = 0; axis < 2; ++axis) {
      uv_dx[unit][axis] *= inverse_area;
      uv_dy[unit][axis] *= inverse_area;
    }
  }
  bool constant_depth =
      vertices[0].z == vertices[1].z && vertices[1].z == vertices[2].z;
  *out = (cpu_triangle){
      .bounds = {(int64_t)min_x, (int64_t)min_y, (int64_t)max_x, (int64_t)max_y},
      .inverse_area = inverse_area, .near_depth = near_depth,
      .far_depth = far_depth, .q_dx = q_dx, .q_dy = q_dy,
      .offset = offset, .constant_depth = constant_depth};
  memcpy(out->coverage, coverage, sizeof(coverage));
  memcpy(out->attributes, attributes, sizeof(attributes));
  memcpy(out->uv, uv, sizeof(uv));
  memcpy(out->uv_dx, uv_dx, sizeof(uv_dx));
  memcpy(out->uv_dy, uv_dy, sizeof(uv_dy));
  for (size_t i = 0; i < 3; ++i) {
    out->values[i] = *vertices[i].vertex;
    out->vertices[i] = vertices[i];
    out->vertices[i].vertex = NULL;
  }
  return true;
}
static void triangle_fill(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
                          const cpu_sampler samplers[2],
                          const cpu_triangle *triangle, cpu_scissor bounds) {
  int64_t min_y = triangle->bounds.y0 > bounds.y0 ? triangle->bounds.y0 : bounds.y0;
  int64_t max_y = triangle->bounds.y1 < bounds.y1 ? triangle->bounds.y1 : bounds.y1;
  if (min_y > max_y) return;
  int64_t min_x = triangle->bounds.x0, max_x = triangle->bounds.x1;
  const edge_equation *coverage = triangle->coverage, *attributes = triangle->attributes;
  screen_vertex vertices[3];
  for (size_t i = 0; i < 3; ++i) {
    vertices[i] = triangle->vertices[i];
    vertices[i].vertex = &triangle->values[i];
  }
  double inverse_area = triangle->inverse_area, near_depth = triangle->near_depth,
      far_depth = triangle->far_depth, q_dx = triangle->q_dx, q_dy = triangle->q_dy,
      offset = triangle->offset;
  const double (*uv)[2][3] = triangle->uv;
  const double (*uv_dx)[2] = triangle->uv_dx, (*uv_dy)[2] = triangle->uv_dy;
  bool constant_depth = triangle->constant_depth;
  bool derivatives[2] = {false, false};
  for (size_t unit = 0; unit < draw->texture_count; ++unit)
    if (draw->textures[unit])
      derivatives[unit] = cpu_sampler_requires_derivatives(&samplers[unit]);
  for (int64_t y = (int64_t)min_y; y <= (int64_t)max_y; ++y) {
    int64_t left = (int64_t)min_x, right = (int64_t)max_x;
    for (size_t i = 0; i < 3; ++i)
      trim(&left, &right, coverage[i], (double)y + 0.5);
    for (int64_t x = left; x <= right; ++x) {
      double weight[3], q = 0, z = 0;
      for (size_t i = 0; i < 3; ++i) {
        weight[i] = evaluate(attributes[i], (double)x + 0.5, (double)y + 0.5) * inverse_area;
        q += vertices[i].q * weight[i];
        z += vertices[i].z * weight[i];
      }
      if (q == 0 || !isfinite(q))
        continue;
      double reciprocal = 1 / q;
      cpu_fragment fragment = {.x = (uint32_t)x,
                               .y = (uint32_t)y,
                               .eye_depth = vertices[0].scale * reciprocal};
      fragment.depth = cpu_clamp(
          cpu_clamp((constant_depth ? vertices[0].z : z) * 0.5 + 0.5) *
              (far_depth - near_depth) +
          near_depth + offset);
      if (!cpu_stencil_active(renderer, &draw->state) &&
          !cpu_depth_passes(draw->state.depth_test, fragment.depth,
              renderer->current->depth[(size_t)fragment.y *
                  renderer->current->width + fragment.x]))
        continue;
      for (size_t channel = 0; channel < 4; ++channel) {
        double color = 0;
        for (size_t i = 0; i < 3; ++i)
          color +=
              vertices[i].vertex->color[channel] * vertices[i].q * weight[i];
        fragment.color[channel] = cpu_clamp(color * reciprocal);
      }
      for (size_t unit = 0; unit < draw->texture_count; ++unit) {
        if (!draw->textures[unit])
          continue;
        double derivative_x[2], derivative_y[2];
        for (size_t axis = 0; axis < 2; ++axis) {
          double coordinate = 0;
          for (size_t i = 0; i < 3; ++i)
            coordinate += uv[unit][axis][i] * weight[i];
          coordinate *= reciprocal;
          fragment.uv[unit][axis] =
              vertices[0].vertex->uv[unit][axis] + coordinate;
          if (derivatives[unit]) {
            derivative_x[axis] =
                (uv_dx[unit][axis] - coordinate * q_dx) * reciprocal;
            derivative_y[axis] =
                (uv_dy[unit][axis] - coordinate * q_dy) * reciprocal;
          }
        }
        if (derivatives[unit])
          fragment.derivative[unit] = (cpu_derivative){
              derivative_x[0], derivative_x[1], derivative_y[0], derivative_y[1]};
      }
      if (draw->lighting != QA_LIGHT_VERTEX) {
        double perspective[3];
        for (size_t i = 0; i < 3; ++i)
          perspective[i] = vertices[i].q * weight[i] * reciprocal;
        double position[3] = {0}, normal[3] = {0};
        for (size_t axis = 0; axis < 3; ++axis)
          for (size_t i = 0; i < 3; ++i) {
            position[axis] += vertices[i].vertex->world[axis] * perspective[i];
            normal[axis] += vertices[i].vertex->normal[axis] * perspective[i];
          }
        fragment.world_position = (qa_vec3){
            (float)position[0], (float)position[1], (float)position[2]};
        fragment.world_normal =
            (qa_vec3){(float)normal[0], (float)normal[1], (float)normal[2]};
      }
      cpu_write_fragment(renderer, draw, samplers, &fragment);
    }
  }
}
static void triangle(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
                     const cpu_sampler samplers[2], screen_vertex a,
                     screen_vertex b, screen_vertex c,
                     const screen_vertex *interpolation, cpu_scissor bounds,
                     cpu_triangle_output *output) {
  cpu_triangle prepared;
  if (!triangle_prepare(&prepared, draw, a, b, c, interpolation, bounds)) return;
  if (!output) {
    triangle_fill(renderer, draw, samplers, &prepared, bounds);
    return;
  }
  struct cpu_raster_pool *pool = output->pool;
  if (pool->triangle_count == pool->triangle_capacity) {
    size_t maximum = SIZE_MAX / sizeof(cpu_triangle);
    if (pool->triangle_capacity == maximum) { output->failed = true; return; }
    size_t capacity = pool->triangle_capacity ? pool->triangle_capacity : 16;
    capacity = capacity > maximum / 2 ? maximum : capacity * 2;
    cpu_triangle *triangles = realloc(pool->triangles, capacity * sizeof(*triangles));
    if (!triangles) { output->failed = true; return; }
    pool->triangles = triangles;
    pool->triangle_capacity = capacity;
  }
  pool->triangles[pool->triangle_count++] = prepared;
}
static cpu_vertex interpolate_line(const cpu_vertex *a, const cpu_vertex *b,
                                   double t) {
  cpu_vertex out;
  for (size_t c = 0; c < 4; ++c) {
    out.clip[c] = a->clip[c] * (1 - t) + b->clip[c] * t;
    out.color[c] = a->color[c] * (1 - t) + b->color[c] * t;
  }
  for (size_t c = 0; c < 3; ++c) {
    out.world[c] = a->world[c] * (1 - t) + b->world[c] * t;
    out.normal[c] = a->normal[c] * (1 - t) + b->normal[c] * t;
  }
  for (size_t unit = 0; unit < 2; ++unit)
    for (size_t c = 0; c < 2; ++c)
      out.uv[unit][c] = a->uv[unit][c] + (b->uv[unit][c] - a->uv[unit][c]) * t;
  return out;
}
static bool exits_diamond(double ax, double ay, double bx, double by, double x,
                          double y) {
  double enter = 0, exit = 1;
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2) {
      double a = 0.5 - sx * (ax - x) - sy * (ay - y),
             b = 0.5 - sx * (bx - x) - sy * (by - y);
      if (a <= 0 && b <= 0)
        return false;
      if ((a <= 0) != (b <= 0)) {
        double t = a / (a - b);
        if (a <= 0)
          enter = fmax(enter, t);
        else
          exit = fmin(exit, t);
      }
    }
  return enter < exit && exit < 1;
}
static void line(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
                 const cpu_sampler samplers[2],
                 cpu_vertex a, cpu_vertex b, bool portal_clip) {
  cpu_scissor bounds = scissor(renderer);
  if (bounds.x0 > bounds.x1 || bounds.y0 > bounds.y1)
    return;
  if (portal_clip && renderer->view.clip_enabled) {
    double da = distance(&a, 6, &renderer->view),
           db = distance(&b, 6, &renderer->view);
    if (da < 0 && db < 0)
      return;
    if (da < 0)
      a = intersection(&a, &b, da, db, 6);
    else if (db < 0)
      b = intersection(&a, &b, da, db, 6);
  }
  double begin = 0, end = 1;
  for (unsigned plane = 0; plane < 6; ++plane) {
    double da = distance(&a, plane, &renderer->view),
           db = distance(&b, plane, &renderer->view);
    if (da < 0 && db < 0)
      return;
    if ((da < 0) != (db < 0)) {
      double scale = fmax(fabs(da), fabs(db));
      double crossing = (da / scale) / (da / scale - db / scale);
      if (da < 0)
        begin = fmax(begin, crossing);
      else
        end = fmin(end, crossing);
    }
  }
  if (begin >= end)
    return;
  cpu_vertex first = interpolate_line(&a, &b, begin),
             last = interpolate_line(&a, &b, end);
  a = first;
  b = last;
  if (a.clip[3] <= 0 || b.clip[3] <= 0)
    return;
  qa_scene_rect view = renderer->view.viewport;
  double ax = (a.clip[0] / a.clip[3] + 1) * view.width / 2,
         ay = (a.clip[1] / a.clip[3] + 1) * view.height / 2;
  double bx = (b.clip[0] / b.clip[3] + 1) * view.width / 2,
         by = (b.clip[1] / b.clip[3] + 1) * view.height / 2;
  double dx = bx - ax, dy = by - ay, length_squared = dx * dx + dy * dy;
  if (!(length_squared > 0))
    return;
  double length = sqrt(length_squared), inverse_a = 1 / a.clip[3],
         inverse_b = 1 / b.clip[3];
  bool x_major = fabs(dx) >= fabs(dy);
  /* A line wider than twice its clipped viewport already covers every
   * possible minor-axis pixel; bound the replication arithmetic accordingly. */
  double maximum_width = 2 * fmax(view.width, view.height) + 1;
  int64_t thickness = (int64_t)fmin(
      maximum_width, fmax(1, floor(draw->state.line_width + 0.5)));
  double shift = (double)(thickness - 1) * 0.5;
  double pa_x = ax - (x_major ? 0 : shift) - 1e-5,
         pa_y = ay - (x_major ? shift : 0) - 1e-10;
  double pb_x = bx - (x_major ? 0 : shift) - 1e-5,
         pb_y = by - (x_major ? shift : 0) - 1e-10;
  double major_a = x_major ? pa_x : pa_y, major_b = x_major ? pb_x : pb_y;
  double minor_a = x_major ? pa_y : pa_x, minor_b = x_major ? pb_y : pb_x;
  int64_t left = bounds.x0 - view.x, right = bounds.x1 - view.x;
  int64_t bottom = (int64_t)view.height - 1 - (bounds.y1 - view.y),
          top = (int64_t)view.height - 1 - (bounds.y0 - view.y);
  int64_t major_min = x_major ? left : bottom,
          major_max = x_major ? right : top;
  int64_t minor_min = x_major ? bottom : left,
          minor_max = x_major ? top : right;
  int64_t first_major = (int64_t)fmax((double)major_min, floor(fmin(major_a, major_b)));
  int64_t last_major = (int64_t)fmin((double)major_max, floor(fmax(major_a, major_b)));
  double near_depth = cpu_clamp(draw->state.depth_near),
         far_depth = cpu_clamp(draw->state.depth_far);
  for (int64_t major = first_major; major <= last_major; ++major) {
    double fraction = ((double)major + 0.5 - major_a) / (major_b - major_a);
    int64_t center = (int64_t)floor(minor_a + (minor_b - minor_a) * fraction);
    for (int64_t minor = center - 1; minor <= center + 1; ++minor) {
      int64_t x = x_major ? major : minor, y = x_major ? minor : major;
      if (!exits_diamond(pa_x, pa_y, pb_x, pb_y, (double)x + 0.5, (double)y + 0.5))
        continue;
      double base_x = (double)x + (x_major ? 0 : shift),
             base_y = (double)y + (x_major ? shift : 0);
      double t =
          cpu_clamp(((base_x + 0.5 - ax) * dx + (base_y + 0.5 - ay) * dy) /
                    length_squared);
      double q = (1 - t) * inverse_a + t * inverse_b, reciprocal = 1 / q;
      double wa = (1 - t) * inverse_a * reciprocal,
             wb = t * inverse_b * reciprocal;
      cpu_fragment fragment = {.eye_depth = fabs(reciprocal)};
      fragment.depth = cpu_clamp(((1 - t) * a.clip[2] * inverse_a +
                                  t * b.clip[2] * inverse_b) *
                                     0.5 +
                                 0.5) *
                           (far_depth - near_depth) +
                       near_depth;
      for (size_t c = 0; c < 4; ++c)
        fragment.color[c] = cpu_clamp(
            (a.color[c] * (1 - t) * inverse_a + b.color[c] * t * inverse_b) *
            reciprocal);
      for (size_t unit = 0; unit < draw->texture_count; ++unit) {
        if (!draw->textures[unit])
          continue;
        double derivative[2];
        for (size_t c = 0; c < 2; ++c) {
          double difference = b.uv[unit][c] - a.uv[unit][c];
          double residual = difference * t * inverse_b * reciprocal;
          fragment.uv[unit][c] = a.uv[unit][c] + residual;
          derivative[c] =
              (difference * inverse_b - residual * (inverse_b - inverse_a)) *
              reciprocal / length;
        }
        fragment.derivative[unit] =
            (cpu_derivative){derivative[0], derivative[1], 0, 0};
      }
      if (draw->lighting != QA_LIGHT_VERTEX) {
        fragment.world_position =
            (qa_vec3){(float)(a.world[0] * wa + b.world[0] * wb),
                      (float)(a.world[1] * wa + b.world[1] * wb),
                      (float)(a.world[2] * wa + b.world[2] * wb)};
        fragment.world_normal =
            (qa_vec3){(float)(a.normal[0] * wa + b.normal[0] * wb),
                      (float)(a.normal[1] * wa + b.normal[1] * wb),
                      (float)(a.normal[2] * wa + b.normal[2] * wb)};
      }
      int64_t actual_min = minor > minor_min ? minor : minor_min;
      int64_t actual_max =
          minor + thickness - 1 < minor_max ? minor + thickness - 1 : minor_max;
      for (int64_t actual = actual_min; actual <= actual_max; ++actual) {
        int64_t actual_x = (x_major ? major : actual) + view.x;
        int64_t actual_y =
            (int64_t)view.height - 1 - (x_major ? actual : major) + view.y;
        fragment.x = (uint32_t)actual_x;
        fragment.y = (uint32_t)actual_y;
        cpu_write_fragment(renderer, draw, samplers, &fragment);
      }
    }
  }
}
static void draw_triangle(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
                          const cpu_sampler samplers[2],
                          const cpu_vertex original[3], cpu_scissor bounds,
                          cpu_triangle_output *output) {
  cpu_vertex polygon[32];
  size_t count = clip_polygon(original, &renderer->view, polygon);
  if (count < 3)
    return;
  if (draw->state.wireframe) {
    double area = 0;
    screen_vertex previous = project(&polygon[count - 1], renderer, 1);
    for (size_t i = 0; i < count; ++i) {
      screen_vertex current = project(&polygon[i], renderer, 1);
      area += previous.x * current.y - previous.y * current.x;
      previous = current;
    }
    if (!isfinite(area) || (draw->state.cull == QA_CULL_BACK && area >= 0) ||
        (draw->state.cull == QA_CULL_FRONT && area < 0))
      return;
    for (size_t i = 0; i < count; ++i)
      line(renderer, draw, samplers, polygon[i], polygon[(i + 1) % count], false);
    return;
  }
  bool z_inside = true, xy_outside = false;
  for (size_t i = 0; i < 3; ++i) {
    const double *p = original[i].clip;
    z_inside = z_inside && p[3] > 0 && p[2] >= -p[3] && p[2] <= p[3];
    xy_outside = xy_outside || fabs(p[0]) > p[3] || fabs(p[1]) > p[3];
  }
  screen_vertex interpolation[3], *attributes = NULL;
  if (z_inside && xy_outside) {
    double scale = fmin(original[0].clip[3],
                        fmin(original[1].clip[3], original[2].clip[3]));
    for (size_t i = 0; i < 3; ++i)
      interpolation[i] = project(&original[i], renderer, scale);
    double area = evaluate(edge(interpolation[0], interpolation[1]),
                           interpolation[2].x, interpolation[2].y);
    if (isfinite(area)) {
      if (area == 0)
        return;
      attributes = interpolation;
    }
  }
  for (size_t i = 1; i + 1 < count; ++i) {
    double scale = fmin(polygon[0].clip[3],
                        fmin(polygon[i].clip[3], polygon[i + 1].clip[3]));
    triangle(renderer, draw, samplers, project(&polygon[0], renderer, scale),
             project(&polygon[i], renderer, scale),
             project(&polygon[i + 1], renderer, scale), attributes, bounds, output);
    if (output && output->failed) return;
  }
}
static cpu_vertex source_vertex(const qa_cpu_renderer *renderer, uint32_t index,
                                bool discrete) {
  cpu_vertex vertex = renderer->vertices[index];
  if (discrete) {
    for (size_t channel = 0; channel < 4; ++channel)
      vertex.color[channel] = (double)cpu_byte(vertex.color[channel]) / 255.0;
    for (size_t unit = 0; unit < 2; ++unit)
      for (size_t axis = 0; axis < 2; ++axis)
        vertex.uv[unit][axis] = (float)vertex.uv[unit][axis];
  }
  return vertex;
}
static void draw_source_strips(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
                                const cpu_sampler samplers[2],
                                bool discrete, cpu_scissor bounds,
                                cpu_triangle_output *output) {
  size_t cursor = 0;
  qa_render_strip strip;
  while (qa_render_strip_next(draw->mesh.indices, draw->mesh.index_count, &cursor, &strip)) {
    cpu_vertex a = source_vertex(renderer, qa_render_strip_vertex(&strip, 0), discrete);
    cpu_vertex b = source_vertex(renderer, qa_render_strip_vertex(&strip, 1), discrete);
    for (size_t ordinal = 2; ordinal < strip.triangles + 2; ++ordinal) {
      cpu_vertex c = source_vertex(renderer, qa_render_strip_vertex(&strip, ordinal), discrete);
      cpu_vertex vertices[3] = {ordinal & 1 ? b : a, ordinal & 1 ? a : b, c};
      draw_triangle(renderer, draw, samplers, vertices, bounds, output);
      if (output && output->failed) return;
      a = b; b = c;
    }
  }
}
static void raster_geometry(const cpu_raster_job *job, cpu_triangle_output *output) {
  qa_cpu_renderer *renderer = job->renderer;
  const qa_scene_draw *draw = job->draw;
  if (job->mode == QA_RENDER_PRIMITIVES_ARRAY_STRIPS ||
      job->mode == QA_RENDER_PRIMITIVES_DISCRETE_STRIPS) {
    draw_source_strips(renderer, draw, job->samplers,
        job->mode == QA_RENDER_PRIMITIVES_DISCRETE_STRIPS, job->bounds, output);
  } else if (draw->mesh.primitive == QA_SCENE_LINES) {
    for (size_t i = 0; i < draw->mesh.index_count; i += 2)
      line(renderer, draw, job->samplers, renderer->vertices[draw->mesh.indices[i]],
           renderer->vertices[draw->mesh.indices[i + 1]], true);
  } else {
    for (size_t i = 0; i < draw->mesh.index_count; i += 3) {
      cpu_vertex vertices[3] = {renderer->vertices[draw->mesh.indices[i]],
                              renderer->vertices[draw->mesh.indices[i + 1]],
                              renderer->vertices[draw->mesh.indices[i + 2]]};
      draw_triangle(renderer, draw, job->samplers, vertices, job->bounds, output);
      if (output && output->failed) return;
    }
  }
}
static void raster_prepared_draw(const cpu_raster_job *job) {
  if (!job->triangles) {
    raster_geometry(job, NULL);
    return;
  }
  for (size_t i = 0; i < job->triangle_count; ++i)
    triangle_fill(job->renderer, job->draw, job->samplers,
                  &job->triangles[i], job->bounds);
}
static unsigned raster_worker_count(const struct cpu_raster_pool *pool,
    int64_t full_rows, int64_t draw_rows) {
  int64_t bands = (int64_t)pool->count + 1;
  int64_t rows_per_band = (full_rows + bands - 1) / bands;
  int64_t active = (draw_rows + rows_per_band - 1) / rows_per_band;
  if (active < 2) return 0;
  return (unsigned)(active < bands ? active : bands) - 1;
}
static unsigned raster_parallel(cpu_raster_job *job) {
  qa_cpu_renderer *renderer = job->renderer;
  const qa_scene_draw *draw = job->draw;
  struct cpu_raster_pool *pool = renderer->raster_pool;
  if (!pool || draw->mesh.primitive != QA_SCENE_TRIANGLES ||
      draw->state.wireframe || !draw->mesh.index_count ||
      job->bounds.x0 > job->bounds.x1 ||
      job->bounds.y1 <= job->bounds.y0)
    return 0;
  for (size_t i = 0; i < draw->texture_count; ++i)
    if (draw->textures[i] && job->samplers[i].target == renderer->current)
      return 0;
  if (draw->shadow_atlas &&
      cpu_target_find(renderer, draw->shadow_atlas) == renderer->current)
    return 0;
  int64_t full_rows = job->bounds.y1 - job->bounds.y0 + 1;
  double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
  for (size_t i = 0; i < draw->mesh.index_count; ++i) {
    const cpu_vertex *vertex = &renderer->vertices[draw->mesh.indices[i]];
    if (!(vertex->clip[3] > 0))
      return raster_worker_count(pool, full_rows, full_rows);
    screen_vertex v = project(vertex, renderer, 1);
    if (!isfinite(v.x) || !isfinite(v.y))
      return raster_worker_count(pool, full_rows, full_rows);
    x0 = fmin(x0, v.x); y0 = fmin(y0, v.y);
    x1 = fmax(x1, v.x); y1 = fmax(y1, v.y);
  }
  double width = fmax(0, fmin(x1, (double)job->bounds.x1 + 1) -
                        fmax(x0, (double)job->bounds.x0));
  double height = fmax(0, fmin(y1, (double)job->bounds.y1 + 1) -
                         fmax(y0, (double)job->bounds.y0));
  if (width * height < 128 * 128) return 0;
  double first_y = fmax((double)job->bounds.y0, ceil(y0 - 0.5));
  double last_y = fmin((double)job->bounds.y1, floor(y1 - 0.5));
  if (first_y > last_y) return 0;
  job->bounds.y0 = (int64_t)first_y;
  job->bounds.y1 = (int64_t)last_y;
  return raster_worker_count(pool, full_rows,
      job->bounds.y1 - job->bounds.y0 + 1);
}
static void raster_draw(cpu_raster_job *job) {
  struct cpu_raster_pool *pool = job->renderer->raster_pool;
  unsigned workers = raster_parallel(job);
  if (!workers) {
    raster_prepared_draw(job);
    return;
  }
  pool->triangle_count = 0;
  cpu_triangle_output output = {.pool = pool};
  raster_geometry(job, &output);
  if (output.failed) {
    pool->triangle_count = 0;
    raster_prepared_draw(job);
    return;
  }
  if (!pool->triangle_count) return;
  job->triangles = pool->triangles;
  job->triangle_count = pool->triangle_count;
  if (fegetenv(&job->environment) != 0) {
    raster_prepared_draw(job);
    return;
  }
  int64_t first = job->bounds.y0, rows = job->bounds.y1 - first + 1;
  unsigned bands = workers + 1;
  for (unsigned i = 0; i < workers; ++i) {
    cpu_raster_worker *worker = &pool->workers[i];
    worker->job = *job;
    worker->job.bounds.y0 = first + rows * i / bands;
    worker->job.bounds.y1 = first + rows * (i + 1) / bands - 1;
    SDL_SemPost(worker->start);
  }
  cpu_raster_job main = *job;
  main.bounds.y0 = first + rows * workers / bands;
  raster_prepared_draw(&main);
  for (unsigned i = 0; i < workers; ++i) {
    cpu_raster_worker *worker = &pool->workers[i];
    SDL_SemWait(worker->done);
    if (!worker->job.completed) raster_prepared_draw(&worker->job);
    else feraiseexcept(worker->job.exceptions);
    memset(&worker->job, 0, sizeof(worker->job));
  }
}
void cpu_raster_flush(qa_cpu_renderer *renderer) {
  struct cpu_raster_pool *pool = renderer->raster_pool;
  if (!pool || !pool->command_count) return;
  cpu_raster_job batch = {.renderer = renderer, .commands = pool->commands,
      .command_count = pool->command_count, .triangles = pool->triangles,
      .bounds = pool->commands[0].bounds};
  int64_t first = pool->triangles[0].bounds.y0;
  int64_t last = pool->triangles[0].bounds.y1;
  double area = 0;
  for (size_t i = 0; i < pool->triangle_count; ++i) {
    const cpu_scissor *bounds = &pool->triangles[i].bounds;
    if (bounds->y0 < first) first = bounds->y0;
    if (bounds->y1 > last) last = bounds->y1;
    area += (double)(bounds->x1 - bounds->x0 + 1) *
            (double)(bounds->y1 - bounds->y0 + 1);
  }
  unsigned workers = area < 128 * 128 ? 0 : raster_worker_count(pool,
      batch.bounds.y1 - batch.bounds.y0 + 1, last - first + 1);
  batch.bounds.y0 = first;
  batch.bounds.y1 = last;
  int64_t rows = last - first + 1;
  unsigned bands = workers + 1;
  for (unsigned i = 0; i < workers; ++i) {
    cpu_raster_worker *worker = &pool->workers[i];
    worker->job = batch;
    worker->job.bounds.y0 = first + rows * i / bands;
    worker->job.bounds.y1 = first + rows * (i + 1) / bands - 1;
    SDL_SemPost(worker->start);
  }
  cpu_raster_job main = batch;
  main.bounds.y0 = first + rows * workers / bands;
  for (size_t i = 0; i < main.command_count; ++i) {
    if (!raster_command_overlaps(&main, i)) continue;
    cpu_raster_job job = raster_command_job(&main, i);
    raster_prepared_draw(&job);
  }
  for (unsigned i = 0; i < workers; ++i) {
    cpu_raster_worker *worker = &pool->workers[i];
    SDL_SemWait(worker->done);
    for (size_t j = worker->job.completed_commands;
         j < worker->job.command_count; ++j) {
      if (!raster_command_overlaps(&worker->job, j)) continue;
      cpu_raster_job job = raster_command_job(&worker->job, j);
      raster_prepared_draw(&job);
    }
    feraiseexcept(worker->job.exceptions);
    memset(&worker->job, 0, sizeof(worker->job));
  }
  pool->command_count = pool->triangle_count = 0;
}
static bool raster_queue(cpu_raster_job *job) {
  struct cpu_raster_pool *pool = job->renderer->raster_pool;
  if (!pool->command_count) pool->triangle_count = 0;
  if (pool->command_count == pool->command_capacity) {
    size_t maximum = SIZE_MAX / sizeof(*pool->commands);
    if (pool->command_capacity == maximum) return false;
    size_t capacity = pool->command_capacity ? pool->command_capacity : 16;
    capacity = capacity > maximum / 2 ? maximum : capacity * 2;
    cpu_raster_command *commands = realloc(pool->commands,
                                          capacity * sizeof(*commands));
    if (!commands) return false;
    pool->commands = commands;
    pool->command_capacity = capacity;
  }
  size_t first = pool->triangle_count;
  cpu_triangle_output output = {.pool = pool};
  raster_geometry(job, &output);
  cpu_raster_command command = {.draw = *job->draw, .bounds = job->bounds,
      .first = first, .count = pool->triangle_count - first};
  if (output.failed || fegetenv(&command.environment) != 0) {
    pool->triangle_count = first;
    return false;
  }
  if (!command.count) return true;
  command.first_y = pool->triangles[first].bounds.y0;
  command.last_y = pool->triangles[first].bounds.y1;
  for (size_t i = first + 1; i < pool->triangle_count; ++i) {
    if (pool->triangles[i].bounds.y0 < command.first_y)
      command.first_y = pool->triangles[i].bounds.y0;
    if (pool->triangles[i].bounds.y1 > command.last_y)
      command.last_y = pool->triangles[i].bounds.y1;
  }
  for (size_t i = 0; i < command.draw.texture_count; ++i)
    command.samplers[i] = job->samplers[i];
  pool->commands[pool->command_count++] = command;
  return true;
}
static bool cpu_draw_impl(qa_cpu_renderer *renderer, const qa_scene_draw *input,
                         qa_error *error, bool queued) {
  bool batchable = queued && renderer->raster_pool &&
      !renderer->controls.source.issuing && !input->source_arrays &&
      !input->source_retain_depth_range &&
      input->source_direct == QA_SOURCE_DIRECT_NONE &&
      input->mesh.primitive == QA_SCENE_TRIANGLES && !input->state.wireframe;
  if (!batchable) cpu_raster_flush(renderer);
  qa_scene_draw resolved = *input;
  if ((unsigned)input->source_direct>QA_SOURCE_DIRECT_IMAGE_GRID) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid Source direct draw provenance");
    return false;
  }
  qa_render_source_direct_state(&resolved.state,&renderer->pipeline,input);
  if (resolved.texture_count > 2) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "CPU draw exceeds texture unit count");
    return false;
  }
  bool source_pipeline=input->source_arrays || input->source_retain_depth_range || input->source_direct!=QA_SOURCE_DIRECT_NONE;
  for (size_t i = 0; i < resolved.texture_count; ++i) {
    size_t unit=source_pipeline && !input->source_arrays && i==0?renderer->controls.attributes.texture_unit:i;
    if (source_pipeline && input->textures[i]) qa_render_source_image_used(&renderer->controls,input->textures[i]);
    if (resolved.retain_texture[i]) resolved.textures[i] = renderer->bound[unit];
    else if (!input->source_arrays && input->textures[i] && renderer->bound[unit]!=input->textures[i]) {
      qa_scene_image_retain(input->textures[i]);
      qa_scene_image_release(renderer->bound[unit]);
      renderer->bound[unit]=input->textures[i];
      if (source_pipeline) qa_render_source_image_used(&renderer->controls,input->textures[i]);
      renderer->controls.attributes.actual_empty[unit]=false;
    }
  }
  const qa_scene_draw *draw = &resolved;
  if (!draw_valid(draw, error))
    return false;
  if (draw->state.stencil_enabled && !renderer->current->stencil) {
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                 "CPU stencil draw requires stencil storage");
    return false;
  }
  for (size_t i = 0; i < draw->mesh.index_count; ++i)
    if (draw->mesh.indices[i] >= (draw->source_vertex_storage ? draw->source_vertex_storage : draw->mesh.vertex_count)) {
      qa_error_set(error, QA_ERROR_ARGUMENT, i,
                   "CPU draw index is outside vertex storage");
      return false;
    }
  renderer->pipeline=draw->state;
  if (draw->source_arrays && draw->mesh.primitive==QA_SCENE_TRIANGLES && !draw->state.wireframe)
    renderer->controls.counters.total_indexes+=draw->mesh.index_count;
  if (draw->source_direct==QA_SOURCE_DIRECT_SHADOW_FINISH) renderer->view.clip_enabled=false;
  qa_render_primitive_mode mode = draw->source_primitives && draw->mesh.primitive == QA_SCENE_TRIANGLES
      ? qa_render_primitives_mode(renderer->controls.values.primitives, false) : QA_RENDER_PRIMITIVES_INDEXED;
  if (!qa_render_source_attributes_resolve(&renderer->controls,&resolved,renderer->bound,mode,error)) return false;
  cpu_sampler samplers[2];
  for (size_t i = 0; i < resolved.texture_count; ++i)
    if (!cpu_sampler_prepare(renderer, resolved.textures[i], &samplers[i]))
      resolved.textures[i] = NULL;
  if (mode == QA_RENDER_PRIMITIVES_NONE) return true;
  if (draw->mesh.index_count && !transform(renderer, draw, mode, error)) return false;
  cpu_raster_job job = {.renderer = renderer, .draw = draw,
      .samplers = samplers, .mode = mode, .bounds = scissor(renderer)};
  for (size_t i = 0; batchable && i < draw->texture_count; ++i)
    if (draw->textures[i] && samplers[i].target == renderer->current)
      batchable = false;
  if (draw->shadow_atlas &&
      cpu_target_find(renderer, draw->shadow_atlas) == renderer->current)
    batchable = false;
  if (!batchable) {
    cpu_raster_flush(renderer);
    raster_draw(&job);
  } else if (!raster_queue(&job)) {
    cpu_raster_flush(renderer);
    raster_prepared_draw(&job);
  }
  if (mode == QA_RENDER_PRIMITIVES_ARRAY_STRIPS || mode == QA_RENDER_PRIMITIVES_DISCRETE_STRIPS) {
    qa_render_source_attributes_finish(&renderer->controls,draw,mode);
    return true;
  }
  if (draw->source_direct==QA_SOURCE_DIRECT_AXIS) renderer->pipeline.line_width=1;
  if (draw->source_direct==QA_SOURCE_DIRECT_SHADOW_FINISH) renderer->pipeline.stencil_enabled=false;
  if (draw->source_direct==QA_SOURCE_DIRECT_SHADOW_VOLUME_END) renderer->pipeline.color_write=true;
  qa_render_source_attributes_finish(&renderer->controls,draw,mode);
  return true;
}
bool cpu_draw(qa_cpu_renderer *renderer, const qa_scene_draw *input,
              qa_error *error) {
  return cpu_draw_impl(renderer, input, error, false);
}
bool cpu_draw_queued(qa_cpu_renderer *renderer, const qa_scene_draw *input,
                     qa_error *error) {
  bool ok = cpu_draw_impl(renderer, input, error, true);
  if (!ok) cpu_raster_flush(renderer);
  return ok;
}
