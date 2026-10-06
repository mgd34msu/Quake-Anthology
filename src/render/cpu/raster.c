#if defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif
#include "internal.h"
#include "brush_spans.h"
#include "triangle_private.h"
#include "../normal_matrix.h"
#include <limits.h>
#include <fenv.h>
#include <stdatomic.h>
#include <SDL_thread.h>
#include <SDL_mutex.h>
#include <SDL_cpuinfo.h>
#include <stdio.h>
#if defined(__linux__)
#include <sched.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

_Thread_local qa_cpu_statistics *cpu_row_statistics;

/* Clip attributes remain unpacked only in reusable transform storage and the
 * bounded stack polygon. Texture versions and submitted meshes stay shared. */
typedef struct screen_vertex {
  double x, y, z, q, scale;
  const cpu_vertex *vertex;
  size_t source_index;
} screen_vertex;
typedef struct edge_equation {
  double x, y, c;
  bool inclusive;
} edge_equation;

typedef struct cpu_scissor {
  int64_t x0, y0, x1, y1;
} cpu_scissor;
typedef struct cpu_triangle {
  screen_vertex vertices[3];
  edge_equation coverage[3];
  cpu_scissor bounds;
  cpu_triangle_attributes attributes;
} cpu_triangle;
static const double unit_color[4] = {1, 1, 1, 1};
typedef struct cpu_triangle_output {
  struct cpu_raster_pool *pool;
  size_t *projected;
  size_t projected_count;
  bool failed;
} cpu_triangle_output;
typedef struct cpu_raster_command {
  qa_scene_draw draw;
  cpu_sampler samplers[2];
  cpu_fragment_kernel kernel;
  cpu_scissor bounds;
  int64_t first_y, last_y;
  size_t first, count;
  int rounding;
  cpu_brush_rows_fn row_kernel;
  void *row_context;
  void (*retire)(qa_cpu_renderer *, void *);
} cpu_raster_command;
typedef struct cpu_raster_job {
  qa_cpu_renderer *renderer;
  const qa_scene_draw *draw;
  const cpu_sampler *samplers;
  cpu_fragment_kernel kernel;
  qa_render_primitive_mode mode;
  cpu_scissor bounds;
  const cpu_triangle *triangles;
  size_t triangle_count;
  const cpu_raster_command *commands;
  size_t command_count, completed_commands;
  int rounding;
  bool completed;
  cpu_brush_rows_fn row_kernel;
  void *row_context;
} cpu_raster_job;
typedef struct cpu_raster_worker {
  SDL_Thread *thread;
  SDL_sem *start, *done;
  cpu_raster_job job;
  qa_cpu_statistics statistics;
  bool stop;
} cpu_raster_worker;
typedef struct cpu_raster_slice {
  int64_t first, last;
  size_t completed_commands;
  qa_cpu_statistics statistics;
} cpu_raster_slice;
struct cpu_raster_pool {
  cpu_triangle *triangles;
  size_t triangle_count, triangle_capacity;
  cpu_raster_command *commands;
  size_t command_count, command_capacity;
  size_t *projected;
  size_t projected_capacity;
  cpu_raster_slice *slices;
  size_t slice_count, slice_capacity;
  atomic_size_t next_slice;
  cpu_raster_job batch;
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
  job.kernel = command->kernel;
  job.triangles = command->row_kernel ? NULL : batch->triangles + command->first;
  job.row_kernel = command->row_kernel;
  job.row_context = command->row_context;
  job.triangle_count = command->count;
  job.bounds = command->bounds;
  if (job.bounds.y0 < batch->bounds.y0) job.bounds.y0 = batch->bounds.y0;
  if (job.bounds.y1 > batch->bounds.y1) job.bounds.y1 = batch->bounds.y1;
  job.commands = NULL;
  job.command_count = 0;
  return job;
}
static bool raster_commands(cpu_raster_job *batch) {
  int rounding = -1;
  for (; batch->completed_commands < batch->command_count;
       ++batch->completed_commands) {
    size_t i = batch->completed_commands;
    if (!raster_command_overlaps(batch, i)) continue;
    int next = batch->commands[i].rounding;
    if (rounding != next) {
      if (fesetround(next) != 0) return false;
      rounding = next;
    }
    cpu_raster_job job = raster_command_job(batch, i);
    if (job.row_kernel)
      job.row_kernel(job.renderer, job.row_context, job.bounds.y0, job.bounds.y1);
    else raster_prepared_draw(&job);
  }
  return true;
}
static cpu_raster_job raster_slice_job(const struct cpu_raster_pool *pool,
                                       size_t index) {
  const cpu_raster_slice *slice = &pool->slices[index];
  cpu_raster_job job = pool->batch;
  job.bounds.y0 = slice->first;
  job.bounds.y1 = slice->last;
  job.completed_commands = slice->completed_commands;
  return job;
}
static void raster_slices(struct cpu_raster_pool *pool) {
  for (;;) {
    size_t index = atomic_fetch_add_explicit(&pool->next_slice, 1,
                                             memory_order_relaxed);
    if (index >= pool->slice_count) return;
    cpu_raster_job job = raster_slice_job(pool, index);
    qa_cpu_statistics *previous = cpu_row_statistics;
    cpu_row_statistics = job.renderer->statistics_enabled ? &pool->slices[index].statistics : NULL;
    bool completed = raster_commands(&job);
    cpu_row_statistics = previous;
    pool->slices[index].completed_commands = job.completed_commands;
    if (!completed) return;
  }
}
static int SDLCALL raster_worker(void *context) {
  cpu_raster_worker *worker = context;
  for (;;) {
    SDL_SemWait(worker->start);
    if (worker->stop) return 0;
    cpu_row_statistics = worker->job.renderer->statistics_enabled ? &worker->statistics : NULL;
    if (worker->job.row_kernel) {
      worker->job.completed = fesetround(worker->job.rounding) == 0;
      if (worker->job.completed)
        worker->job.row_kernel(worker->job.renderer, worker->job.row_context,
            worker->job.bounds.y0, worker->job.bounds.y1);
    } else if (worker->job.command_count) {
      struct cpu_raster_pool *pool = worker->job.renderer->raster_pool;
      if (pool->slice_count) raster_slices(pool);
      else worker->job.completed = raster_commands(&worker->job);
    } else {
      worker->job.completed = fesetround(worker->job.rounding) == 0;
      if (worker->job.completed) {
        raster_prepared_draw(&worker->job);
      }
    }
    cpu_row_statistics = NULL;
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
  free(pool->projected);
  free(pool->slices);
  free(pool);
  renderer->raster_pool = NULL;
}
static int raster_physical_cores(void) {
#if defined(__linux__)
  cpu_set_t allowed;
  if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0) {
    int logical = CPU_COUNT(&allowed);
    if (logical < 2) return logical;
    int packages[CPU_SETSIZE], cores[CPU_SETSIZE], count = 0;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
      if (!CPU_ISSET((size_t)cpu, &allowed)) continue;
      char path[128];
      int package, core;
      (void)snprintf(path, sizeof(path),
          "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
      FILE *file = fopen(path, "r");
      if (!file) return logical;
      int parsed = fscanf(file, "%d", &package);
      fclose(file);
      if (parsed != 1) return logical;
      (void)snprintf(path, sizeof(path),
          "/sys/devices/system/cpu/cpu%d/topology/core_id", cpu);
      file = fopen(path, "r");
      if (!file) return logical;
      parsed = fscanf(file, "%d", &core);
      fclose(file);
      if (parsed != 1) return logical;
      int i = 0;
      while (i < count && (packages[i] != package || cores[i] != core)) ++i;
      if (i == count) { packages[count] = package; cores[count++] = core; }
    }
    return count ? count : logical;
  }
  return 1;
#elif defined(__APPLE__)
  int count = 0;
  size_t size = sizeof(count);
  if (sysctlbyname("hw.physicalcpu", &count, &size, NULL, 0) == 0 && count > 0)
    return count;
#elif defined(_WIN32)
  DWORD bytes = 0;
  (void)GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &bytes);
  SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *topology = malloc(bytes);
  if (topology) {
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, topology, &bytes)) {
      DWORD offset = 0;
      int count = 0;
      while (offset < bytes) {
        const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *entry =
            (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)
            ((const uint8_t *)topology + offset);
        if (!entry->Size || entry->Size > bytes - offset) break;
        if (entry->Relationship == RelationProcessorCore) ++count;
        offset += entry->Size;
      }
      free(topology);
      if (offset == bytes && count > 0) return count;
    } else free(topology);
  }
#endif
  return SDL_GetCPUCount();
}
void cpu_raster_pool_create(qa_cpu_renderer *renderer) {
  int cpus = raster_physical_cores();
  if (cpus < 2) return;
  unsigned count = (unsigned)(cpus - 1);
  if (sizeof(cpu_raster_worker) >
      (SIZE_MAX - sizeof(struct cpu_raster_pool)) / count) return;
  struct cpu_raster_pool *pool = calloc(1, sizeof(*pool) +
      (size_t)count * sizeof(cpu_raster_worker));
  if (!pool) return;
  renderer->raster_pool = pool;
  pool->count = count;
  atomic_init(&pool->next_slice, 0);
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
      (unsigned)draw->environment > QA_TEXTURE_LIGHTMAP_INVERT_ALPHA ||
      (unsigned)draw->lighting > QA_LIGHT_Q2_MODEL_SHADOW ||
      (unsigned)draw->light_pass > QA_LIGHT_PASS_MODEL ||
      (unsigned)draw->mesh.primitive > QA_SCENE_LINES ||
      (unsigned)s->blend_source > QA_BLEND_SRC_ALPHA_SATURATE ||
      (unsigned)s->blend_destination > QA_BLEND_SRC_ALPHA_SATURATE ||
      (unsigned)s->depth_test > QA_DEPTH_DISABLED ||
      (unsigned)s->alpha_test > QA_ALPHA_GT666 ||
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
      (draw->vertex_inputs.constant_color && !finite4(draw->vertex_inputs.color)) ||
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
static double distance(const cpu_vertex *, unsigned, const qa_scene_view *);
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
  double normal[9];
  qa_render_normal_matrix(&draw->model, normal);
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
    out->clip_mask = 0;
    if (draw->mesh.primitive == QA_SCENE_TRIANGLES) {
      if (!(out->clip[3] > 0)) out->clip_mask = UINT8_C(0x80);
      for (unsigned plane = 0; plane < (renderer->view.clip_enabled ? 7u : 6u); ++plane)
        if (!(distance(out, plane, &renderer->view) >= 0))
          out->clip_mask |= (uint8_t)(1u << plane);
    }
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
  out.clip_mask = 0;
  return out;
}
static size_t clip_polygon(const cpu_vertex input[3], const qa_scene_view *view,
                           cpu_vertex result[32], bool *clipped) {
  *clipped = false;
  cpu_vertex work[2][32];
  memcpy(work[0], input, 3 * sizeof(*input));
  size_t count = 3;
  unsigned source = 0;
  for (unsigned plane = 0; plane < (view->clip_enabled ? 7u : 6u) && count;
       ++plane) {
    unsigned destination = source ^ 1;
    size_t produced = 0;
    bool changed = false;
    const cpu_vertex *previous = &work[source][count - 1];
    double previous_distance = distance(previous, plane, view);
    for (size_t i = 0; i < count; ++i) {
      const cpu_vertex *current = &work[source][i];
      double current_distance = distance(current, plane, view);
      if ((current_distance >= 0) != (previous_distance >= 0)) {
        if (!changed) {
          memcpy(work[destination], work[source], produced * sizeof(*input));
          changed = true;
        }
        work[destination][produced++] = intersection(
            previous, current, previous_distance, current_distance, plane);
      }
      if (current_distance >= 0) {
        if (changed) work[destination][produced] = *current;
        ++produced;
      } else if (!changed) {
        memcpy(work[destination], work[source], produced * sizeof(*input));
        changed = true;
      }
      previous = current;
      previous_distance = current_distance;
    }
    if (changed) {
      *clipped = true;
      count = produced;
      source = destination;
    }
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
                             const qa_cpu_renderer *renderer, double scale,
                             cpu_triangle_output *output, size_t index) {
  qa_scene_rect view = renderer->view.viewport;
  double subpixel = (double)(UINT32_C(1) << renderer->options.subpixel_bits),
         w = vertex->clip[3];
  bool retained = output && output->projected && index < output->projected_count;
  if (retained && output->projected[index] != SIZE_MAX) {
    size_t location = output->projected[index];
    const screen_vertex *previous =
        &output->pool->triangles[location / 3].vertices[location % 3];
    return (screen_vertex){previous->x, previous->y, previous->z,
        previous->scale == scale ? previous->q : scale / w, scale, vertex, index};
  }
  return (screen_vertex){
      snap(view.x + (vertex->clip[0] / w + 1) * view.width * 0.5, subpixel),
      snap(view.y + (1 - vertex->clip[1] / w) * view.height * 0.5, subpixel),
      vertex->clip[2] / w,
      scale / w,
      scale,
      vertex,
      retained ? index : SIZE_MAX};
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
static cpu_attribute_plane attribute_plane(const edge_equation edges[3],
    const double values[3], double inverse_area) {
  cpu_attribute_plane plane = {0};
  for (size_t i = 0; i < 3; ++i) {
    plane.x += values[i] * edges[i].x;
    plane.y += values[i] * edges[i].y;
    plane.c += values[i] * edges[i].c;
  }
  plane.x *= inverse_area;
  plane.y *= inverse_area;
  plane.c *= inverse_area;
  return plane;
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
  double slope_x = 0, slope_y = 0;
  double q[3], color[4][3], uv[2][2][3] = {{{0}}};
  double world[3][3] = {{0}}, normal[3][3] = {{0}};
  for (size_t i = 0; i < 3; ++i) {
    slope_x += vertices[i].z * attributes[i].x;
    slope_y += vertices[i].z * attributes[i].y;
    q[i] = vertices[i].q;
    for (size_t channel = 0; channel < 4; ++channel)
      color[channel][i] = vertices[i].vertex->color[channel] * q[i];
    if (draw->lighting != QA_LIGHT_VERTEX)
      for (size_t axis = 0; axis < 3; ++axis) {
        world[axis][i] = vertices[i].vertex->world[axis] * q[i];
        normal[axis][i] = vertices[i].vertex->normal[axis] * q[i];
      }
    for (size_t unit = 0; unit < draw->texture_count; ++unit) {
      if (!draw->textures[unit])
        continue;
      for (size_t axis = 0; axis < 2; ++axis) {
        uv[unit][axis][i] = (vertices[i].vertex->uv[unit][axis] -
                             vertices[0].vertex->uv[unit][axis]) *
                            vertices[i].q;
      }
    }
  }
  double slope = fmax(fabs(slope_x), fabs(slope_y)) * fabs(inverse_area) * 0.5 *
                 fabs(far_depth - near_depth);
  double offset = draw->state.polygon_offset
                      ? slope * draw->state.offset_factor +
                            0x1p-24 * draw->state.offset_units
                      : 0;
  bool constant_depth =
      vertices[0].z == vertices[1].z && vertices[1].z == vertices[2].z;
  *out = (cpu_triangle){
      .bounds = {(int64_t)min_x, (int64_t)min_y, (int64_t)max_x, (int64_t)max_y},
      .attributes = {.inverse_area = inverse_area, .near_depth = near_depth,
          .depth_range = far_depth - near_depth, .scale = vertices[0].scale,
          .q = attribute_plane(attributes, q, inverse_area), .offset = offset,
          .constant_depth = constant_depth, .unit_color = true}};
  memcpy(out->coverage, coverage, sizeof(coverage));
  for (size_t i = 0; i < 3; ++i) {
    out->attributes.weights[i] = (cpu_attribute_plane){
        attributes[i].x, attributes[i].y, attributes[i].c};
    out->attributes.z[i] = vertices[i].z;
    if (memcmp(vertices[i].vertex->color, unit_color, sizeof(unit_color)) != 0)
      out->attributes.unit_color = false;
    out->vertices[i] = vertices[i];
    out->vertices[i].vertex = NULL;
  }
  if (!out->attributes.unit_color)
    for (size_t channel = 0; channel < 4; ++channel)
      out->attributes.color[channel] = attribute_plane(attributes, color[channel], inverse_area);
  for (size_t unit = 0; unit < draw->texture_count; ++unit)
    if (draw->textures[unit])
      for (size_t axis = 0; axis < 2; ++axis) {
        out->attributes.uv[unit][axis] = attribute_plane(attributes, uv[unit][axis], inverse_area);
        out->attributes.uv_anchor[unit][axis] = vertices[0].vertex->uv[unit][axis];
      }
  if (draw->lighting != QA_LIGHT_VERTEX)
    for (size_t axis = 0; axis < 3; ++axis) {
      out->attributes.world[axis] = attribute_plane(attributes, world[axis], inverse_area);
      out->attributes.normal[axis] = attribute_plane(attributes, normal[axis], inverse_area);
    }
  return true;
}
static void triangle_fill(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
                          const cpu_sampler samplers[2], cpu_fragment_kernel kernel,
                          const cpu_triangle *triangle, cpu_scissor bounds) {
  qa_cpu_statistics *statistics = cpu_row_statistics;
  uint64_t audit_covered = 0;
  int64_t min_y = triangle->bounds.y0 > bounds.y0 ? triangle->bounds.y0 : bounds.y0;
  int64_t max_y = triangle->bounds.y1 < bounds.y1 ? triangle->bounds.y1 : bounds.y1;
  if (min_y > max_y) return;
  int64_t min_x = triangle->bounds.x0, max_x = triangle->bounds.x1;
  const edge_equation *coverage = triangle->coverage;
  cpu_fragment_row_kernel row_kernel = cpu_fragment_row_select(kernel);
  for (int64_t y = (int64_t)min_y; y <= (int64_t)max_y; ++y) {
    int64_t left = (int64_t)min_x, right = (int64_t)max_x;
    double pixel_y = (double)y + 0.5;
    for (size_t i = 0; i < 3; ++i)
      trim(&left, &right, coverage[i], pixel_y);
    if (left > right) continue;
    if (statistics) audit_covered += (uint64_t)(right - left + 1);
    row_kernel(renderer, draw, samplers, &triangle->attributes,
               (uint32_t)left, (uint32_t)right, (uint32_t)y);
  }
  if (statistics) {
    statistics->generic_covered += audit_covered;
  }
}
static void triangle(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
                     const cpu_sampler samplers[2], cpu_fragment_kernel kernel, screen_vertex a,
                     screen_vertex b, screen_vertex c,
                     const screen_vertex *interpolation, cpu_scissor bounds,
                     cpu_triangle_output *output) {
  cpu_triangle prepared;
  if (!triangle_prepare(&prepared, draw, a, b, c, interpolation, bounds)) return;
  if (!output) {
    triangle_fill(renderer, draw, samplers, kernel, &prepared, bounds);
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
  size_t first = pool->triangle_count++;
  pool->triangles[first] = prepared;
  if (output->projected)
    for (size_t i = 0; i < 3; ++i)
      if (prepared.vertices[i].source_index != SIZE_MAX)
        output->projected[prepared.vertices[i].source_index] = first * 3 + i;
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
  out.clip_mask = 0;
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
                 const cpu_sampler samplers[2], cpu_fragment_kernel kernel,
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
  bool stencil = cpu_stencil_active(renderer, &draw->state);
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
      cpu_fragment fragment;
      fragment.eye_depth = fabs(reciprocal);
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
        cpu_fragment_admission admission =
            cpu_fragment_admit(renderer, &draw->state, &fragment, stencil);
        kernel(renderer, draw, samplers, &fragment, admission);
      }
    }
  }
}
static void draw_triangle(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
                          const cpu_sampler samplers[2], cpu_fragment_kernel kernel,
                          const cpu_vertex *a, const cpu_vertex *b,
                          const cpu_vertex *c, const uint32_t indices[3],
                          cpu_scissor bounds,
                          cpu_triangle_output *output) {
  if (a->clip_mask & b->clip_mask & c->clip_mask) return;
  if (!(a->clip_mask | b->clip_mask | c->clip_mask) && !draw->state.wireframe) {
    double scale = fmin(a->clip[3], fmin(b->clip[3], c->clip[3]));
    triangle(renderer, draw, samplers, kernel,
             project(a, renderer, scale, output, indices[0]),
             project(b, renderer, scale, output, indices[1]),
             project(c, renderer, scale, output, indices[2]), NULL, bounds, output);
    return;
  }
  cpu_vertex original[3] = {*a, *b, *c};
  cpu_vertex polygon[32];
  bool clipped;
  size_t count = clip_polygon(original, &renderer->view, polygon, &clipped);
  if (count < 3)
    return;
  if (draw->state.wireframe) {
    double area = 0;
    screen_vertex previous = project(&polygon[count - 1], renderer, 1, NULL, SIZE_MAX);
    for (size_t i = 0; i < count; ++i) {
      screen_vertex current = project(&polygon[i], renderer, 1, NULL, SIZE_MAX);
      area += previous.x * current.y - previous.y * current.x;
      previous = current;
    }
    if (!isfinite(area) || (draw->state.cull == QA_CULL_BACK && area >= 0) ||
        (draw->state.cull == QA_CULL_FRONT && area < 0))
      return;
    for (size_t i = 0; i < count; ++i)
      line(renderer, draw, samplers, kernel, polygon[i], polygon[(i + 1) % count], false);
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
      interpolation[i] = project(&original[i], renderer, scale, NULL, SIZE_MAX);
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
    triangle(renderer, draw, samplers, kernel,
             project(&polygon[0], renderer, scale, output,
                     clipped ? SIZE_MAX : indices[0]),
             project(&polygon[i], renderer, scale, output,
                     clipped ? SIZE_MAX : indices[i]),
             project(&polygon[i + 1], renderer, scale, output,
                     clipped ? SIZE_MAX : indices[i + 1]), attributes, bounds, output);
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
                                const cpu_sampler samplers[2], cpu_fragment_kernel kernel,
                                bool discrete, cpu_scissor bounds,
                                cpu_triangle_output *output) {
  size_t cursor = 0;
  qa_render_strip strip;
  while (qa_render_strip_next(draw->mesh.indices, draw->mesh.index_count, &cursor, &strip)) {
    uint32_t a_index = qa_render_strip_vertex(&strip, 0);
    uint32_t b_index = qa_render_strip_vertex(&strip, 1);
    cpu_vertex a = source_vertex(renderer, a_index, discrete);
    cpu_vertex b = source_vertex(renderer, b_index, discrete);
    for (size_t ordinal = 2; ordinal < strip.triangles + 2; ++ordinal) {
      uint32_t c_index = qa_render_strip_vertex(&strip, ordinal);
      cpu_vertex c = source_vertex(renderer, c_index, discrete);
      uint32_t indices[3] = {ordinal & 1 ? b_index : a_index,
                            ordinal & 1 ? a_index : b_index, c_index};
      draw_triangle(renderer, draw, samplers, kernel, ordinal & 1 ? &b : &a,
                    ordinal & 1 ? &a : &b, &c, indices, bounds, output);
      if (output && output->failed) return;
      a = b; b = c;
      a_index = b_index; b_index = c_index;
    }
  }
}
static void raster_geometry(const cpu_raster_job *job, cpu_triangle_output *output) {
  qa_cpu_renderer *renderer = job->renderer;
  const qa_scene_draw *draw = job->draw;
  if (output) {
    struct cpu_raster_pool *pool = output->pool;
    size_t count = draw->source_vertex_storage
        ? draw->source_vertex_storage : draw->mesh.vertex_count;
    output->projected = NULL;
    output->projected_count = 0;
    if (count && count <= SIZE_MAX / sizeof(*pool->projected)) {
      if (pool->projected_capacity < count) {
        size_t *projected = realloc(pool->projected, count * sizeof(*projected));
        if (projected) {
          pool->projected = projected;
          pool->projected_capacity = count;
        }
      }
      if (pool->projected_capacity >= count) {
        output->projected = pool->projected;
        output->projected_count = count;
        memset(output->projected, 0xff, count * sizeof(*output->projected));
      }
    }
  }
  if (job->mode == QA_RENDER_PRIMITIVES_ARRAY_STRIPS ||
      job->mode == QA_RENDER_PRIMITIVES_DISCRETE_STRIPS) {
    draw_source_strips(renderer, draw, job->samplers, job->kernel,
        job->mode == QA_RENDER_PRIMITIVES_DISCRETE_STRIPS, job->bounds, output);
  } else if (draw->mesh.primitive == QA_SCENE_LINES) {
    for (size_t i = 0; i < draw->mesh.index_count; i += 2)
      line(renderer, draw, job->samplers, job->kernel, renderer->vertices[draw->mesh.indices[i]],
           renderer->vertices[draw->mesh.indices[i + 1]], true);
  } else {
    for (size_t i = 0; i < draw->mesh.index_count; i += 3) {
      draw_triangle(renderer, draw, job->samplers, job->kernel,
                    &renderer->vertices[draw->mesh.indices[i]],
                    &renderer->vertices[draw->mesh.indices[i + 1]],
                    &renderer->vertices[draw->mesh.indices[i + 2]],
                    &draw->mesh.indices[i], job->bounds, output);
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
    triangle_fill(job->renderer, job->draw, job->samplers, job->kernel,
                  &job->triangles[i], job->bounds);
}
static unsigned raster_worker_count(const struct cpu_raster_pool *pool,
    int64_t full_rows, int64_t draw_rows) {
  int64_t bands = (int64_t)pool->count + 1;
  int64_t rows_per_band = (full_rows + bands - 1) / bands;
  if (rows_per_band < 16) rows_per_band = 16;
  int64_t active = (draw_rows + rows_per_band - 1) / rows_per_band;
  if (active < 2) return 0;
  return (unsigned)(active < bands ? active : bands) - 1;
}
static unsigned raster_prepared_bounds(cpu_raster_job *job) {
  struct cpu_raster_pool *pool = job->renderer->raster_pool;
  int64_t full_rows = job->renderer->current->height;
  int64_t first = INT64_MAX, last = INT64_MIN;
  double area = 0;
  for (size_t i = 0; i < job->triangle_count; ++i) {
    const cpu_scissor *bounds = &job->triangles[i].bounds;
    if (bounds->y0 < first) first = bounds->y0;
    if (bounds->y1 > last) last = bounds->y1;
    area += (double)(bounds->x1 - bounds->x0 + 1) *
            (double)(bounds->y1 - bounds->y0 + 1);
  }
  for (size_t i = 0; i < job->command_count; ++i) {
    const cpu_raster_command *command = &job->commands[i];
    if (!command->row_kernel) continue;
    if (command->first_y < first) first = command->first_y;
    if (command->last_y > last) last = command->last_y;
    area += (double)job->renderer->current->width *
            (double)(command->last_y - command->first_y + 1);
  }
  job->bounds.y0 = first;
  job->bounds.y1 = last;
  return area < 128 * 128 ? 0 :
      raster_worker_count(pool, full_rows, last - first + 1);
}
static void raster_draw(cpu_raster_job *job) {
  CPU_STATS_ADD(job->renderer, generic_batches, 1);
  CPU_STATS_ADD(job->renderer, generic_commands, 1);
  qa_cpu_renderer *renderer = job->renderer;
  const qa_scene_draw *draw = job->draw;
  struct cpu_raster_pool *pool = renderer->raster_pool;
  bool prepare = pool && draw->mesh.primitive == QA_SCENE_TRIANGLES &&
      !draw->state.wireframe;
  for (size_t i = 0; prepare && i < draw->texture_count; ++i)
    if (draw->textures[i] && job->samplers[i].target == renderer->current)
      prepare = false;
  if (prepare && draw->shadow_atlas &&
      cpu_target_find(renderer, draw->shadow_atlas) == renderer->current)
    prepare = false;
  if (!prepare) {
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
  CPU_STATS_ADD(renderer, generic_triangles, pool->triangle_count);
  CPU_STATS_ADD(renderer, worker_dispatches, 1);
  unsigned workers = raster_prepared_bounds(job);
  if (!workers) {
    raster_prepared_draw(job);
    return;
  }
  job->rounding = fegetround();
  if (job->rounding < 0) {
    raster_prepared_draw(job);
    return;
  }
  int64_t first = job->bounds.y0, rows = job->bounds.y1 - first + 1;
  unsigned bands = workers + 1;
  CPU_STATS_ADD(renderer, worker_posts, workers);
  CPU_STATS_ADD(renderer, worker_joins, workers);
  for (unsigned i = 0; i < workers; ++i) {
    cpu_raster_worker *worker = &pool->workers[i];
    worker->job = *job;
    if (renderer->statistics_enabled)
      memset(&worker->statistics, 0, sizeof(worker->statistics));
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
    if (!worker->job.completed) {
      qa_cpu_statistics *previous = cpu_row_statistics;
      cpu_row_statistics = renderer->statistics_enabled ? &worker->statistics : NULL;
      raster_prepared_draw(&worker->job);
      cpu_row_statistics = previous;
    }
    if (renderer->statistics_enabled)
      cpu_statistics_merge(&renderer->statistics, &worker->statistics);
    memset(&worker->job, 0, sizeof(worker->job));
  }
}
static bool raster_slice_prepare(struct cpu_raster_pool *pool,
                                  const cpu_raster_job *batch, unsigned bands) {
  uint64_t rows = (uint64_t)(batch->bounds.y1 - batch->bounds.y0 + 1);
  uint64_t desired = (uint64_t)bands * 4;
  uint64_t height = (rows + desired - 1) / desired;
  if (height < 16) height = 16;
  uint64_t count = (rows + height - 1) / height;
  if (count > SIZE_MAX / sizeof(*pool->slices) ||
      count > SIZE_MAX - pool->count - 1) return false;
  if (pool->slice_capacity < (size_t)count) {
    cpu_raster_slice *slices = realloc(pool->slices,
        (size_t)count * sizeof(*slices));
    if (!slices) return false;
    pool->slices = slices;
    pool->slice_capacity = (size_t)count;
  }
  for (size_t i = 0; i < (size_t)count; ++i) {
    uint64_t end = height * (i + 1);
    if (end > rows) end = rows;
    pool->slices[i].first = batch->bounds.y0 + (int64_t)(height * i);
    pool->slices[i].last = batch->bounds.y0 + (int64_t)end - 1;
    pool->slices[i].completed_commands = 0;
    if (batch->renderer->statistics_enabled)
      memset(&pool->slices[i].statistics, 0, sizeof(pool->slices[i].statistics));
  }
  pool->batch = *batch;
  pool->slice_count = (size_t)count;
  atomic_store_explicit(&pool->next_slice, 0, memory_order_relaxed);
  return true;
}
static bool raster_image_aliases(const qa_scene_image *sampled,
                                 const qa_scene_image *updated) {
  if (sampled == updated) return true;
  if (!sampled) return false;
  for (size_t i = 0; i < sampled->level_count; ++i)
    for (size_t j = 0; j < updated->level_count; ++j)
      if (sampled->levels[i].pixels == updated->levels[j].pixels) return true;
  return false;
}
bool cpu_raster_image_pending(const qa_cpu_renderer *renderer,
                               const qa_scene_image *image) {
  const struct cpu_raster_pool *pool = renderer->raster_pool;
  if (!pool || !image) return false;
  for (size_t i = 0; i < pool->command_count; ++i) {
    const cpu_raster_command *command = &pool->commands[i];
    /* Brush rows retain pinned, baked RGBA. Postprocess rows drain at their
     * compositor boundary and do not sample streamed image storage. */
    if (command->row_kernel) continue;
    for (size_t unit = 0; unit < command->draw.texture_count; ++unit)
      if (raster_image_aliases(command->samplers[unit].image, image)) return true;
    if (raster_image_aliases(command->draw.shadow_atlas, image)) return true;
  }
  return false;
}
void cpu_raster_flush(qa_cpu_renderer *renderer) {
  struct cpu_raster_pool *pool = renderer->raster_pool;
  if (!pool || !pool->command_count) return;
  if (renderer->statistics_enabled) {
    size_t generic = 0;
    for (size_t i = 0; i < pool->command_count; ++i)
      if (!pool->commands[i].row_kernel) ++generic;
    renderer->statistics.generic_batches += generic != 0;
    renderer->statistics.generic_commands += generic;
    renderer->statistics.generic_triangles += pool->triangle_count;
  }
  int rounding = fegetround();
  cpu_raster_job batch = {.renderer = renderer, .commands = pool->commands,
      .command_count = pool->command_count, .triangles = pool->triangles,
      .triangle_count = pool->triangle_count,
      .bounds = pool->commands[0].bounds};
  unsigned workers = raster_prepared_bounds(&batch);
  int64_t first = batch.bounds.y0, last = batch.bounds.y1;
  int64_t rows = last - first + 1;
  unsigned bands = workers + 1;
  CPU_STATS_ADD(renderer, worker_dispatches, 1);
  CPU_STATS_ADD(renderer, worker_posts, workers);
  CPU_STATS_ADD(renderer, worker_joins, workers);
  pool->slice_count = 0;
  bool sliced = workers && raster_slice_prepare(pool, &batch, bands);
  for (unsigned i = 0; i < workers; ++i) {
    cpu_raster_worker *worker = &pool->workers[i];
    worker->job = batch;
    if (renderer->statistics_enabled)
      memset(&worker->statistics, 0, sizeof(worker->statistics));
    if (!sliced) {
      worker->job.bounds.y0 = first + rows * i / bands;
      worker->job.bounds.y1 = first + rows * (i + 1) / bands - 1;
    }
    SDL_SemPost(worker->start);
  }
  qa_cpu_statistics *previous = cpu_row_statistics;
  cpu_row_statistics = renderer->statistics_enabled ? &renderer->statistics : NULL;
  cpu_raster_job main = batch;
  if (sliced) raster_slices(pool);
  else {
    main.bounds.y0 = first + rows * workers / bands;
    (void)raster_commands(&main);
  }
  for (unsigned i = 0; i < workers; ++i) {
    cpu_raster_worker *worker = &pool->workers[i];
    SDL_SemWait(worker->done);
    if (!sliced) {
      cpu_row_statistics = renderer->statistics_enabled ? &worker->statistics : NULL;
      (void)raster_commands(&worker->job);
      if (renderer->statistics_enabled)
        cpu_statistics_merge(&renderer->statistics, &worker->statistics);
    }
    memset(&worker->job, 0, sizeof(worker->job));
  }
  if (sliced) {
    for (size_t i = 0; i < pool->slice_count; ++i) {
      cpu_raster_job job = raster_slice_job(pool, i);
      cpu_row_statistics = renderer->statistics_enabled ? &pool->slices[i].statistics : NULL;
      (void)raster_commands(&job);
      if (renderer->statistics_enabled)
        cpu_statistics_merge(&renderer->statistics, &pool->slices[i].statistics);
    }
  }
  cpu_row_statistics = previous;
  if (rounding >= 0) (void)fesetround(rounding);
  pool->slice_count = 0;
  memset(&pool->batch, 0, sizeof(pool->batch));
  size_t count = pool->command_count;
  pool->command_count = pool->triangle_count = 0;
  for (size_t i = 0; i < count; ++i)
    if (pool->commands[i].retire)
      pool->commands[i].retire(renderer, pool->commands[i].row_context);
}
static bool raster_command_reserve(struct cpu_raster_pool *pool) {
  if (pool->command_count < pool->command_capacity) return true;
  size_t maximum = SIZE_MAX / sizeof(*pool->commands);
  if (pool->command_capacity == maximum) return false;
  size_t capacity = pool->command_capacity ? pool->command_capacity : 16;
  capacity = capacity > maximum / 2 ? maximum : capacity * 2;
  cpu_raster_command *commands = realloc(pool->commands,
                                        capacity * sizeof(*commands));
  if (!commands) return false;
  pool->commands = commands;
  pool->command_capacity = capacity;
  return true;
}
void cpu_raster_queue_rows(qa_cpu_renderer *renderer, int64_t first, int64_t last,
    cpu_brush_rows_fn kernel, void *context,
    void (*retire)(qa_cpu_renderer *, void *), int rounding) {
  struct cpu_raster_pool *pool = renderer->raster_pool;
  if (last >= first && pool && rounding >= 0 && raster_command_reserve(pool)) {
    if (!pool->command_count) pool->triangle_count = 0;
    pool->commands[pool->command_count++] = (cpu_raster_command){
        .row_kernel = kernel, .row_context = context, .retire = retire,
        .rounding = rounding, .first_y = first, .last_y = last,
        .bounds = {0, first, (int64_t)renderer->current->width - 1, last}};
    return;
  }
  cpu_raster_flush(renderer);
  qa_cpu_statistics *previous = cpu_row_statistics;
  cpu_row_statistics = renderer->statistics_enabled ? &renderer->statistics : NULL;
  int original = fegetround();
  if (rounding >= 0) (void)fesetround(rounding);
  if (last >= first) {
    CPU_STATS_ADD(renderer, worker_dispatches, 1);
    kernel(renderer, context, first, last);
  }
  if (original >= 0) (void)fesetround(original);
  cpu_row_statistics = previous;
  if (retire) retire(renderer, context);
}
static bool raster_queue(cpu_raster_job *job) {
  struct cpu_raster_pool *pool = job->renderer->raster_pool;
  if (!pool->command_count) pool->triangle_count = 0;
  if (!raster_command_reserve(pool)) return false;
  size_t first = pool->triangle_count;
  cpu_triangle_output output = {.pool = pool};
  raster_geometry(job, &output);
  cpu_raster_command command = {.draw = *job->draw, .kernel = job->kernel, .bounds = job->bounds,
      .first = first, .count = pool->triangle_count - first};
  command.rounding = fegetround();
  if (output.failed || command.rounding < 0) {
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
  qa_scene_draw base, lightmap;
  bool fused = qa_scene_draw_lightmap_split(input, &base, &lightmap);
  if (fused && (renderer->overdraw || renderer->preblend_gamma || !renderer->current->color))
    return cpu_draw_impl(renderer, &base, error, queued) &&
           cpu_draw_impl(renderer, &lightmap, error, queued);
  bool batchable = queued && renderer->raster_pool &&
      input->mesh.primitive == QA_SCENE_TRIANGLES && !input->state.wireframe;
  if (!batchable) {
    if (!cpu_brush_flush(renderer, error)) return false;
    cpu_raster_flush(renderer);
  }
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
    if (source_pipeline && input->textures[i]) cpu_source_image_used(renderer,input->textures[i]);
    if (resolved.retain_texture[i]) resolved.textures[i] = renderer->bound[unit];
    else if (!(fused && i == 1) && !input->source_arrays && input->textures[i] && renderer->bound[unit]!=input->textures[i]) {
      qa_scene_image_retain(input->textures[i]);
      qa_scene_image_release(renderer->bound[unit]);
      renderer->bound[unit]=input->textures[i];
      if (source_pipeline) cpu_source_image_used(renderer,input->textures[i]);
      renderer->controls.attributes.actual_empty[unit]=false;
    }
  }
  const qa_scene_draw *draw = &resolved;
  if (!draw_valid(draw, error)) {
    if (fused) return cpu_draw_impl(renderer, &base, error, queued) &&
                      cpu_draw_impl(renderer, &lightmap, error, queued);
    return false;
  }
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
    if (resolved.textures[i] && resolved.textures[i]->streamed &&
        !cpu_stream_image_admit(renderer, resolved.textures[i], error)) return false;
  for (size_t i = 0; i < resolved.texture_count; ++i)
    if (!cpu_sampler_prepare(renderer, resolved.textures[i], &samplers[i]))
      resolved.textures[i] = NULL;
  if (mode == QA_RENDER_PRIMITIVES_NONE) return true;
  CPU_STATS_ADD(renderer, draws, 1);
  bool brush_handled = false;
  if (draw->brush.present && mode == QA_RENDER_PRIMITIVES_INDEXED) {
    if (!cpu_brush_draw_queued(renderer, draw, error, &brush_handled)) return false;
  }
  if (!brush_handled) {
    if (!cpu_brush_flush(renderer, error)) return false;
    if (draw->mesh.index_count && !transform(renderer, draw, mode, error)) return false;
    cpu_raster_job job = {.renderer = renderer, .draw = draw,
        .samplers = samplers, .kernel = cpu_fragment_select(renderer, draw),
        .mode = mode, .bounds = scissor(renderer)};
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
  } else if (!queued) {
    if (!cpu_brush_flush(renderer, error)) return false;
    cpu_raster_flush(renderer);
  }
  if (mode == QA_RENDER_PRIMITIVES_ARRAY_STRIPS || mode == QA_RENDER_PRIMITIVES_DISCRETE_STRIPS) {
    qa_render_source_attributes_finish(&renderer->controls,draw,mode);
    return true;
  }
  if (draw->source_direct==QA_SOURCE_DIRECT_AXIS) renderer->pipeline.line_width=1;
  if (draw->source_direct==QA_SOURCE_DIRECT_SHADOW_FINISH) renderer->pipeline.stencil_enabled=false;
  if (draw->source_direct==QA_SOURCE_DIRECT_SHADOW_VOLUME_END) renderer->pipeline.color_write=true;
  qa_render_source_attributes_finish(&renderer->controls,draw,mode);
  if (fused) {
    renderer->pipeline = lightmap.state;
    if (renderer->bound[0] != lightmap.textures[0]) {
      qa_scene_image_retain(lightmap.textures[0]);
      qa_scene_image_release(renderer->bound[0]);
      renderer->bound[0] = lightmap.textures[0];
    }
  }
  return true;
}
bool cpu_draw(qa_cpu_renderer *renderer, const qa_scene_draw *input,
              qa_error *error) {
  qa_cpu_statistics *previous = cpu_row_statistics;
  cpu_row_statistics = renderer->statistics_enabled ? &renderer->statistics : NULL;
  bool ok = cpu_draw_impl(renderer, input, error, false);
  cpu_row_statistics = previous;
  return ok;
}
bool cpu_draw_queued(qa_cpu_renderer *renderer, const qa_scene_draw *input,
                     qa_error *error) {
  qa_cpu_statistics *previous = cpu_row_statistics;
  cpu_row_statistics = renderer->statistics_enabled ? &renderer->statistics : NULL;
  bool ok = cpu_draw_impl(renderer, input, error, true);
  if (!ok) {
    cpu_raster_flush(renderer);
    cpu_brush_clear(renderer);
  }
  cpu_row_statistics = previous;
  return ok;
}
