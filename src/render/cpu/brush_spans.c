#include "qa/platform_services.h"
#include "brush_spans.h"
#include "surface_cache.h"
#include <SDL_timer.h>
#include "fog_span_private.h"
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

#include <float.h>
#include <limits.h>
#include <fenv.h>

/* r_edge.c resolves brush visibility before r_scan.c samples a lit surface.
 * The shared framebuffer keeps normalized depth for models and depth fog. */
typedef struct brush_vertex {
  double clip[4], world[3], texel[2], x, y;
} brush_vertex;
typedef struct brush_plane { double x, y, origin; } brush_plane;
typedef struct brush_surface {
  brush_plane s, t, q, depth;
  cpu_surface_mip mips[2];
  double mip_blend;
  bool linear;
  cpu_fog_span_style fog;
} brush_surface;
typedef struct brush_edge {
  int64_t first, last;
  double x, step;
  size_t surface;
} brush_edge;
typedef struct brush_event { int64_t x; size_t surface; } brush_event;
typedef struct brush_span { uint32_t x, y, count; size_t surface; } brush_span;
struct cpu_brush_context {
  brush_surface *surfaces;
  size_t surface_count, surface_capacity;
  brush_edge *edges;
  size_t edge_count, edge_capacity;
  brush_vertex *clip[2];
  size_t clip_capacity[2];
  size_t *active_edges, *active_surfaces;
  size_t active_edge_capacity, active_surface_capacity;
  brush_event *events;
  size_t event_capacity;
  brush_span *spans;
  size_t span_count, span_capacity;
  size_t *rows;
  size_t row_capacity;
  int64_t first, last;
  int rounding;
  bool pinned, queued;
  struct cpu_brush_context *next;
};

static bool cache_pinned(const qa_cpu_renderer *renderer) {
  for (const struct cpu_brush_context *context = renderer->brush_spans;
       context; context = context->next)
    if (context->pinned) return true;
  return false;
}

static bool reserve(void **storage, size_t *capacity, size_t count,
                     size_t stride, qa_error *error) {
  if (count <= *capacity) return true;
  if (count > SIZE_MAX / stride) {
    qa_error_set(error, QA_ERROR_MEMORY, count, "CPU brush storage overflow");
    return false;
  }
  size_t grown = *capacity ? *capacity : 64;
  while (grown < count && grown <= SIZE_MAX / 2 / stride) grown *= 2;
  if (grown < count || grown > SIZE_MAX / stride) grown = count;
  void *replacement = realloc(*storage, grown * stride);
  if (!replacement) {
    qa_error_set(error, QA_ERROR_MEMORY, count, "Allocating CPU brush storage");
    return false;
  }
  *storage = replacement;
  *capacity = grown;
  return true;
}

static double plane_value(brush_plane plane, double x, double y) {
  return plane.x * x + plane.y * y + plane.origin;
}
static brush_plane plane_from_vertices(const brush_vertex *a,
    const brush_vertex *b, const brush_vertex *c, double va, double vb,
    double vc, double inverse_area) {
  double dx = ((vb - va) * (c->y - a->y) -
               (vc - va) * (b->y - a->y)) * inverse_area;
  double dy = ((b->x - a->x) * (vc - va) -
               (c->x - a->x) * (vb - va)) * inverse_area;
  return (brush_plane){dx, dy, va - dx * a->x - dy * a->y};
}
static bool plane_finite(brush_plane plane) {
  return isfinite(plane.x) && isfinite(plane.y) && isfinite(plane.origin);
}
static bool plane_matches(brush_plane plane, double x, double y, double value) {
  /* Four native float multiply/add operations have this forward-error bound.
   * Authored nonplanar fans keep their existing triangle interpolation. */
  const double unit = FLT_EPSILON * 0.5;
  const double bound = (4 * unit) / (1 - 4 * unit);
  double scale = fabs(plane.x * x) + fabs(plane.y * y) +
                 fabs(plane.origin) + fabs(value);
  return fabs(plane_value(plane, x, y) - value) <= bound * scale;
}
static double vertex_distance(const brush_vertex *vertex, unsigned plane,
                               const qa_scene_view *view) {
  if (plane < 6)
    return vertex->clip[3] + ((plane & 1u) ? -vertex->clip[plane / 2] :
                                                        vertex->clip[plane / 2]);
  return vertex->world[0] * view->clip_plane.normal.x +
         vertex->world[1] * view->clip_plane.normal.y +
         vertex->world[2] * view->clip_plane.normal.z - view->clip_plane.distance;
}
static brush_vertex vertex_intersection(const brush_vertex *a,
    const brush_vertex *b, double da, double db, unsigned plane) {
  double scale = fmax(fabs(da), fabs(db));
  double ad = fabs(da) / scale, bd = fabs(db) / scale;
  double wa = bd / (ad + bd), wb = ad / (ad + bd);
  brush_vertex out = {0};
  for (size_t axis = 0; axis < 4; ++axis)
    out.clip[axis] = a->clip[axis] * wa + b->clip[axis] * wb;
  for (size_t axis = 0; axis < 3; ++axis)
    out.world[axis] = a->world[axis] * wa + b->world[axis] * wb;
  for (size_t axis = 0; axis < 2; ++axis) {
    double anchor = fmin(a->texel[axis], b->texel[axis]);
    out.texel[axis] = anchor + (a->texel[axis] - anchor) * wa +
                             (b->texel[axis] - anchor) * wb;
  }
  if (plane < 6) out.clip[plane / 2] = plane & 1u ? out.clip[3] : -out.clip[3];
  return out;
}
static bool clip_brush(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
    struct cpu_brush_context *context, brush_vertex **result, size_t *result_count,
    qa_error *error) {
  size_t count = draw->brush.polygon_vertices;
  if (count > SIZE_MAX - 7) {
    qa_error_set(error, QA_ERROR_MEMORY, count, "CPU brush clipping overflow");
    return false;
  }
  for (size_t i = 0; i < 2; ++i)
    if (!reserve((void **)&context->clip[i], &context->clip_capacity[i], count + 7,
                  sizeof(*context->clip[i]), error)) return false;
  const float *mvp = draw->mvp.m, *model = draw->model.m;
  for (size_t i = 0; i < count; ++i) {
    qa_vec3 point = draw->mesh.vertices[draw->brush.polygon_indices ? draw->brush.polygon_indices[i] : i].position;
    if (!qa_vec_finite(point)) {
      qa_error_set(error, QA_ERROR_ARGUMENT, i, "Nonfinite CPU brush vertex");
      return false;
    }
    brush_vertex *vertex = &context->clip[0][i];
    for (size_t axis = 0; axis < 4; ++axis)
      vertex->clip[axis] = (double)mvp[axis] * point.x +
          (double)mvp[axis + 4] * point.y + (double)mvp[axis + 8] * point.z + mvp[axis + 12];
    for (size_t axis = 0; axis < 3; ++axis)
      vertex->world[axis] = (double)model[axis] * point.x +
          (double)model[axis + 4] * point.y + (double)model[axis + 8] * point.z + model[axis + 12];
    for (size_t axis = 0; axis < 2; ++axis) {
      const float *projection = draw->brush.texel_projection[axis];
      vertex->texel[axis] = (double)point.x * projection[0] +
          (double)point.y * projection[1] + (double)point.z * projection[2] + projection[3] -
          draw->brush.texture_mins[axis];
    }
  }
  unsigned source = 0;
  for (unsigned plane = 0; count && plane < (renderer->view.clip_enabled ? 7u : 6u); ++plane) {
    brush_vertex *input = context->clip[source], *output = context->clip[source ^ 1u];
    size_t produced = 0;
    const brush_vertex *previous = &input[count - 1];
    double previous_distance = vertex_distance(previous, plane, &renderer->view);
    for (size_t i = 0; i < count; ++i) {
      const brush_vertex *current = &input[i];
      double current_distance = vertex_distance(current, plane, &renderer->view);
      if ((current_distance >= 0) != (previous_distance >= 0)) {
        if (produced == context->clip_capacity[source ^ 1u]) {
          qa_error_set(error, QA_ERROR_FORMAT, i, "CPU brush polygon is not convex");
          return false;
        }
        output[produced++] = vertex_intersection(previous, current,
            previous_distance, current_distance, plane);
      }
      if (current_distance >= 0) {
        if (produced == context->clip_capacity[source ^ 1u]) {
          qa_error_set(error, QA_ERROR_FORMAT, i, "CPU brush polygon is not convex");
          return false;
        }
        output[produced++] = *current;
      }
      previous = current;
      previous_distance = current_distance;
    }
    count = produced;
    source ^= 1u;
  }
  size_t produced = 0;
  for (size_t i = 0; i < count; ++i) {
    brush_vertex vertex = context->clip[source][i];
    if (!(vertex.clip[3] > 0)) continue;
    vertex.x = renderer->view.viewport.x +
        (vertex.clip[0] / vertex.clip[3] + 1) * renderer->view.viewport.width * 0.5;
    vertex.y = renderer->view.viewport.y +
        (1 - vertex.clip[1] / vertex.clip[3]) * renderer->view.viewport.height * 0.5;
    context->clip[source][produced++] = vertex;
  }
  *result = context->clip[source];
  *result_count = produced;
  return true;
}
static double snap_coordinate(double coordinate, double subpixel) {
  double scaled = (float)coordinate * subpixel, lower = floor(scaled);
  double fraction = scaled - lower;
  return (fraction < 0.5 || (fraction == 0.5 && fmod(lower, 2) == 0) ?
          lower : lower + 1) / subpixel;
}
static bool span_fog_supported(const qa_scene_fog *fog) {
  if (fog->kind == QA_FOG_NONE || fog->kind == QA_FOG_Q2) return true;
  if (fog->kind != QA_FOG_CONSTANT && fog->kind != QA_FOG_EXP2) return false;
  return fog->color.x >= 0 && fog->color.x <= 1 &&
      fog->color.y >= 0 && fog->color.y <= 1 &&
      fog->color.z >= 0 && fog->color.z <= 1 &&
      (fog->kind != QA_FOG_CONSTANT || (fog->amount >= 0 && fog->amount <= 1));
}
static bool ordinary_brush(const qa_cpu_renderer *renderer, const qa_scene_draw *draw) {
  const qa_scene_state *state = &draw->state;
  return draw->brush.present && draw->brush.polygon_vertices >= 3 &&
      (draw->brush.polygon_indices || draw->brush.polygon_vertices == draw->mesh.vertex_count) &&
      draw->mesh.primitive == QA_SCENE_TRIANGLES && draw->single_coverage &&
      draw->mesh.identity && draw->mesh.geometry && !draw->source_arrays &&
      !draw->source_primitives && !draw->source_direct && !draw->source_stage_state &&
      !draw->source_retain_depth_range && !draw->source_retain_polygon_offset &&
      draw->lighting != QA_LIGHT_Q2_MODEL_SHADOW && !draw->light_count && !draw->shadow_atlas &&
      !draw->luminance_alpha && !renderer->overdraw && !renderer->preblend_gamma &&
      state->depth_test == QA_DEPTH_LEQUAL && state->depth_write && state->color_write &&
      state->depth_near == 0 && state->depth_far == 1 && !state->polygon_offset &&
      !state->stencil_enabled && !state->wireframe && state->alpha_test == QA_ALPHA_NONE &&
      state->blend_source == QA_BLEND_ONE && state->blend_destination == QA_BLEND_ZERO &&
      draw->vertex_inputs.constant_color && draw->vertex_inputs.color.w == 1 &&
      !draw->vertex_inputs.swap_uv && draw->texture_count && draw->textures[0] &&
      draw->textures[0]->kind != QA_SCENE_DEPTH32F && !draw->textures[0]->streamed &&
      ((draw->texture_count == 1 && draw->environment == QA_TEXTURE_MODULATE) ||
       (draw->texture_count == 2 && (draw->environment >= QA_TEXTURE_LIGHTMAP_MODULATE ||
         (draw->environment == QA_TEXTURE_MODULATE && draw->lighting == QA_LIGHT_VERTEX)))) &&
      span_fog_supported(&draw->fog) &&
      renderer->current->color && renderer->current->depth;
}
static bool prepare_surface(brush_surface *surface, const brush_vertex *vertices,
                            size_t count) {
  const brush_vertex *a = &vertices[0], *b = NULL, *c = NULL;
  double area = 0;
  for (size_t i = 1; i + 1 < count; ++i) {
    const brush_vertex *next_b = &vertices[i], *next_c = &vertices[i + 1];
    double candidate = (next_b->x - a->x) * (next_c->y - a->y) -
                       (next_c->x - a->x) * (next_b->y - a->y);
    if (isfinite(candidate) && fabs(candidate) > fabs(area)) {
      area = candidate; b = next_b; c = next_c;
    }
  }
  if (area == 0) return false;
  double inverse = 1 / area;
  double qa = 1 / a->clip[3], qb = 1 / b->clip[3], qc = 1 / c->clip[3];
  surface->s = plane_from_vertices(a, b, c, a->texel[0] * qa,
                                  b->texel[0] * qb, c->texel[0] * qc, inverse);
  surface->t = plane_from_vertices(a, b, c, a->texel[1] * qa,
                                  b->texel[1] * qb, c->texel[1] * qc, inverse);
  surface->q = plane_from_vertices(a, b, c, qa, qb, qc, inverse);
  surface->depth = plane_from_vertices(a, b, c, a->clip[2] * qa,
                                      b->clip[2] * qb, c->clip[2] * qc, inverse);
  if (!plane_finite(surface->s) || !plane_finite(surface->t) ||
      !plane_finite(surface->q) || !plane_finite(surface->depth)) return false;
  for (size_t i = 0; i < count; ++i) {
    const brush_vertex *vertex = &vertices[i];
    double q = 1 / vertex->clip[3];
    if (!plane_matches(surface->s, vertex->x, vertex->y, vertex->texel[0] * q) ||
        !plane_matches(surface->t, vertex->x, vertex->y, vertex->texel[1] * q) ||
        !plane_matches(surface->q, vertex->x, vertex->y, q) ||
        !plane_matches(surface->depth, vertex->x, vertex->y, vertex->clip[2] * q))
      return false;
  }
  return true;
}
static bool prepare_mips(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
    const brush_vertex *vertices, size_t count, brush_surface *surface) {
  const brush_vertex *nearest = &vertices[0];
  for (size_t i = 1; i < count; ++i)
    if (vertices[i].clip[3] < nearest->clip[3]) nearest = &vertices[i];
  double q = 1 / nearest->clip[3];
  double s = nearest->texel[0], t = nearest->texel[1];
  double rho = fmax(hypot((surface->s.x - s * surface->q.x) / q,
                         (surface->t.x - t * surface->q.x) / q),
                    hypot((surface->s.y - s * surface->q.y) / q,
                          (surface->t.y - t * surface->q.y) / q));
  cpu_sampler sampler;
  if (!cpu_sampler_prepare(renderer, draw->textures[0], &sampler)) return false;
  bool magnification = !(rho > sampler.magnification_limit);
  bool blend = sampler.blend;
  surface->linear = magnification ? sampler.magnification_linear : sampler.linear;
  unsigned maximum = sampler.level_count > CPU_SURFACE_MIPS ? CPU_SURFACE_MIPS - 1u : (unsigned)(sampler.level_count - 1);
  double lod = !magnification && maximum && rho > 1 ? log2(rho) : 0;
  lod = fmin((double)maximum, fmax(0, lod));
  unsigned first = (unsigned)floor(blend ? lod : lod + 0.5);
  unsigned second = blend && first < maximum ? first + 1 : first;
  surface->mip_blend = second != first ? lod - first : 0;
  if (!cpu_surface_cache_prepare(renderer, draw, first, &surface->mips[0])) return false;
  if (second == first) surface->mips[1] = surface->mips[0];
  else if (!cpu_surface_cache_prepare(renderer, draw, second, &surface->mips[1])) return false;
  return true;
}

bool cpu_brush_draw_queued(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
                           qa_error *error, bool *handled) {
  *handled = false;
  qa_scene_draw bound;
  if (draw->brush.normalized_texture && draw->textures[0] && draw->textures[0]->level_count) {
    bound = *draw;
    uint32_t size[2] = {draw->textures[0]->levels[0].width, draw->textures[0]->levels[0].height};
    for (unsigned axis = 0; axis < 2; ++axis) {
      if (!size[axis]) return true;
      double first = floor((double)draw->brush.texture_mins[axis] * size[axis] / 16) * 16;
      double last = ceil((double)draw->brush.texture_maxs[axis] * size[axis] / 16) * 16;
      double extent = fmax(16, last - first);
      if (!isfinite(first) || fabs(first) > FLT_MAX || !isfinite(extent) || extent > UINT32_MAX) return true;
      bound.brush.texture_size[axis] = size[axis];
      bound.brush.texture_mins[axis] = (float)first;
      bound.brush.texture_extents[axis] = (uint32_t)extent;
      for (unsigned c = 0; c < 4; ++c) bound.brush.texel_projection[axis][c] *= (float)size[axis];
      for (unsigned c = 0; c < 2; ++c) bound.brush.lightmap_from_texel[c][axis] /= (float)size[axis];
    }
    bound.brush.normalized_texture = false;
    draw = &bound;
  }
  CPU_STATS_ADD(renderer, brush_candidates, 1);
  if (!ordinary_brush(renderer, draw)) {
    CPU_STATS_ADD(renderer, brush_predicate_rejects, 1);
    return true;
  }
  struct cpu_brush_context *context = renderer->brush_spans;
  struct cpu_brush_context **link = &renderer->brush_spans;
  while (context && context->queued) {
    link = &context->next;
    context = context->next;
  }
  int rounding = fegetround();
  if (rounding < 0) return true;
  if (context && context->surface_count && context->rounding != rounding) {
    if (!cpu_brush_flush(renderer, error)) return false;
    return cpu_brush_draw_queued(renderer, draw, error, handled);
  }
  if (!context) {
    context = calloc(1, sizeof(*context));
    if (!context) {
      qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating CPU brush owner");
      return false;
    }
    *link = context;
  }
  context->rounding = rounding;
  brush_vertex *vertices;
  size_t count;
  if (!clip_brush(renderer, draw, context, &vertices, &count, error)) return false;
  if (count < 3) { *handled = true; return true; }
  brush_surface surface = {.fog = cpu_fog_span_style_prepare(&draw->fog)};
  if (!prepare_surface(&surface, vertices, count)) {
    CPU_STATS_ADD(renderer, brush_planarity_rejects, 1);
    return true;
  }
  double subpixel = (double)(UINT32_C(1) << renderer->options.subpixel_bits);
  for (size_t i = 0; i < count; ++i) {
    vertices[i].x = snap_coordinate(vertices[i].x, subpixel);
    vertices[i].y = snap_coordinate(vertices[i].y, subpixel);
  }
  double area = 0;
  for (size_t i = 0; i < count; ++i) {
    const brush_vertex *a = &vertices[i], *b = &vertices[(i + 1) % count];
    area += a->x * b->y - a->y * b->x;
  }
  if (!isfinite(area) || area == 0 ||
      (draw->state.cull == QA_CULL_BACK && area > 0) ||
      (draw->state.cull == QA_CULL_FRONT && area < 0)) {
    *handled = true;
    return true;
  }
  if (context->surface_count == SIZE_MAX || count > SIZE_MAX - context->edge_count) {
    qa_error_set(error, QA_ERROR_MEMORY, count, "CPU brush queue overflow");
    return false;
  }
  if (!reserve((void **)&context->surfaces, &context->surface_capacity,
               context->surface_count + 1, sizeof(*context->surfaces), error) ||
      !reserve((void **)&context->edges, &context->edge_capacity,
               context->edge_count + count, sizeof(*context->edges), error)) return false;
  if (!context->pinned) {
    if (!cache_pinned(renderer)) cpu_surface_cache_begin(renderer);
    context->pinned = true;
  }
  if (!prepare_mips(renderer, draw, vertices, count, &surface)) {
    CPU_STATS_ADD(renderer, brush_cache_rejects, 1);
    return true;
  }
  int64_t top = renderer->view.viewport.y > 0 ? renderer->view.viewport.y : 0;
  int64_t bottom = (int64_t)renderer->view.viewport.y + renderer->view.viewport.height - 1;
  if (bottom >= renderer->current->height) bottom = (int64_t)renderer->current->height - 1;
  size_t start = context->edge_count;
  for (size_t i = 0; i < count; ++i) {
    const brush_vertex *a = &vertices[i], *b = &vertices[(i + 1) % count];
    if (a->y == b->y) continue;
    if (a->y > b->y) { const brush_vertex *swap = a; a = b; b = swap; }
    /* raster.c's horizontal top edge is exclusive; its bottom is inclusive. */
    int64_t first = (int64_t)floor(a->y - 0.5) + 1, last = (int64_t)floor(b->y - 0.5);
    if (first < top) first = top;
    if (last > bottom) last = bottom;
    if (first > last) continue;
    double step = (b->x - a->x) / (b->y - a->y);
    context->edges[context->edge_count++] = (brush_edge){first, last,
        a->x + ((double)first + 0.5 - a->y) * step, step, context->surface_count};
    if (!context->surface_count && context->edge_count == start + 1) {
      context->first = first; context->last = last;
    } else {
      if (first < context->first) context->first = first;
      if (last > context->last) context->last = last;
    }
  }
  if (context->edge_count != start)
    context->surfaces[context->surface_count++] = surface;
  CPU_STATS_ADD(renderer, brush_queued, context->edge_count != start);
  *handled = true;
  return true;
}

static int edge_order(const void *left, const void *right) {
  const brush_edge *a = left, *b = right;
  if (a->first != b->first) return a->first < b->first ? -1 : 1;
  if (a->x != b->x) return a->x < b->x ? -1 : 1;
  return a->surface < b->surface ? -1 : a->surface > b->surface;
}
static bool append_span(struct cpu_brush_context *context, int64_t first,
    int64_t last, int64_t y, size_t surface, qa_error *error) {
  if (first >= last) return true;
  if (context->span_count) {
    brush_span *previous = &context->spans[context->span_count - 1];
    if (previous->y == (uint32_t)y && previous->surface == surface &&
        (uint64_t)previous->x + previous->count == (uint64_t)first) {
      previous->count += (uint32_t)(last - first);
      return true;
    }
  }
  if (context->span_count == SIZE_MAX) {
    qa_error_set(error, QA_ERROR_MEMORY, 0, "CPU brush span overflow");
    return false;
  }
  if (!reserve((void **)&context->spans, &context->span_capacity,
               context->span_count + 1, sizeof(*context->spans), error)) return false;
  context->spans[context->span_count++] = (brush_span){(uint32_t)first,
      (uint32_t)y, (uint32_t)(last - first), surface};
  return true;
}
static double surface_depth(const brush_surface *surface, int64_t x, int64_t y) {
  return plane_value(surface->depth, (double)x + 0.5, (double)y + 0.5);
}
static bool nearer(const struct cpu_brush_context *context, size_t candidate,
                    size_t winner, int64_t x, int64_t y) {
  double a = surface_depth(&context->surfaces[candidate], x, y);
  double b = surface_depth(&context->surfaces[winner], x, y);
  return a < b || (a == b && candidate > winner);
}
static bool visible_interval(struct cpu_brush_context *context, size_t active_count,
    int64_t first, int64_t last, int64_t y, qa_error *error) {
  if (!active_count) return true;
  while (first < last) {
    size_t winner = context->active_surfaces[0];
    for (size_t i = 1; i < active_count; ++i) {
      size_t candidate = context->active_surfaces[i];
      if (nearer(context, candidate, winner, first, y)) winner = candidate;
    }
    int64_t end = last;
    /* Inline brushes can cross a world plane inside an edge interval. Find
     * that crossing once, rather than sorting surfaces for every pixel. */
    for (size_t i = 0; i < active_count; ++i) {
      size_t candidate = context->active_surfaces[i];
      if (candidate == winner ||
          !nearer(context, candidate, winner, end - 1, y)) continue;
      int64_t left = first + 1, right = end - 1;
      while (left < right) {
        int64_t middle = left + (right - left) / 2;
        if (nearer(context, candidate, winner, middle, y)) right = middle;
        else left = middle + 1;
      }
      end = left;
    }
    if (!append_span(context, first, end, y, winner, error)) return false;
    first = end;
  }
  return true;
}
static bool generate_spans(qa_cpu_renderer *renderer, struct cpu_brush_context *context,
                            qa_error *error) {
  size_t row_count = (size_t)(context->last - context->first + 1);
  if (!reserve((void **)&context->active_edges, &context->active_edge_capacity,
               context->edge_count, sizeof(*context->active_edges), error) ||
      !reserve((void **)&context->events, &context->event_capacity,
               context->edge_count, sizeof(*context->events), error) ||
      !reserve((void **)&context->active_surfaces, &context->active_surface_capacity,
               context->surface_count, sizeof(*context->active_surfaces), error) ||
      row_count == SIZE_MAX || !reserve((void **)&context->rows, &context->row_capacity,
               row_count + 1, sizeof(*context->rows), error)) return false;
  uint64_t sort_start = renderer->statistics_enabled ? qa_platform_time_ns() : 0;
  qsort(context->edges, context->edge_count, sizeof(*context->edges), edge_order);
  CPU_STATS_ADD(renderer, brush_sort_ticks, qa_platform_time_ns() - sort_start);
  size_t next = 0, edge_count = 0;
  int64_t x0 = renderer->view.viewport.x > 0 ? renderer->view.viewport.x : 0;
  int64_t x1 = (int64_t)renderer->view.viewport.x + renderer->view.viewport.width;
  if (x1 > renderer->current->width) x1 = renderer->current->width;
  for (int64_t y = context->first; y <= context->last; ++y) {
    context->rows[(size_t)(y - context->first)] = context->span_count;
    size_t retained = 0;
    for (size_t i = 0; i < edge_count; ++i) {
      size_t index = context->active_edges[i];
      if (context->edges[index].last >= y) context->active_edges[retained++] = index;
    }
    edge_count = retained;
    while (next < context->edge_count && context->edges[next].first <= y)
      context->active_edges[edge_count++] = next++;
    for (size_t i = 0; i < edge_count; ++i) {
      const brush_edge *edge = &context->edges[context->active_edges[i]];
      double x = edge->x + edge->step * (double)(y - edge->first);
      int64_t pixel = (int64_t)ceil(x - 0.5);
      if (pixel < x0) pixel = x0;
      if (pixel > x1) pixel = x1;
      brush_event event = {pixel, edge->surface};
      size_t position = i;
      while (position && context->events[position - 1].x > pixel) {
        context->events[position] = context->events[position - 1];
        --position;
      }
      context->events[position] = event;
    }
    size_t active = 0;
    int64_t previous = x0;
    for (size_t i = 0; i < edge_count;) {
      int64_t x = context->events[i].x;
      if (!visible_interval(context, active, previous, x, y, error)) return false;
      do {
        size_t surface = context->events[i++].surface, position = 0;
        while (position < active && context->active_surfaces[position] != surface) ++position;
        if (position == active) context->active_surfaces[active++] = surface;
        else context->active_surfaces[position] = context->active_surfaces[--active];
      } while (i < edge_count && context->events[i].x == x);
      previous = x;
    }
    if (!visible_interval(context, active, previous, x1, y, error)) return false;
  }
  context->rows[row_count] = context->span_count;
  return true;
}

/* Q1/Q2 native spans use float perspective endpoints and 16.16 texel
 * increments. Clamp only those endpoints; every interpolated texel is bounded. */
static int64_t fixed_texel(float value, uint32_t extent, int64_t minimum) {
  int64_t maximum = ((int64_t)extent << 16) - 1;
  if (!(value > (float)minimum)) return minimum;
  if (value >= (float)maximum) return maximum;
  return (int64_t)value;
}
#if defined(__SSE2__)
static __m128 mip_channels(const uint8_t *pixel) {
  int32_t packed;
  memcpy(&packed, pixel, sizeof(packed));
  __m128i channels = _mm_cvtsi32_si128(packed);
  channels = _mm_unpacklo_epi8(channels, _mm_setzero_si128());
  channels = _mm_unpacklo_epi16(channels, _mm_setzero_si128());
  return _mm_cvtepi32_ps(channels);
}
#endif
static void mip_point_color(const uint8_t *pixel, float color[4]) {
#if defined(__SSE2__)
  _mm_storeu_ps(color, mip_channels(pixel));
#else
  for (size_t channel = 0; channel < 4; ++channel) color[channel] = pixel[channel];
#endif
}
static void mip_sample(const cpu_surface_mip *mip, int64_t s, int64_t t,
                         float color[4]) {
  int64_t sx = s - INT64_C(32768), sy = t - INT64_C(32768);
  uint32_t x, y, x1, y1;
  float u, v;
  if (sx < 0) { x = x1 = 0; u = 0; }
  else {
    x = (uint32_t)((uint64_t)sx >> 16);
    if (x >= mip->width - 1) { x = x1 = mip->width - 1; u = 0; }
    else { x1 = x + 1; u = (float)(sx & INT64_C(65535)) * (1.0f / 65536.0f); }
  }
  if (sy < 0) { y = y1 = 0; v = 0; }
  else {
    y = (uint32_t)((uint64_t)sy >> 16);
    if (y >= mip->height - 1) { y = y1 = mip->height - 1; v = 0; }
    else { y1 = y + 1; v = (float)(sy & INT64_C(65535)) * (1.0f / 65536.0f); }
  }
  const uint8_t *a = mip->pixels + (size_t)y * mip->stride + (size_t)x * 4;
  const uint8_t *b = mip->pixels + (size_t)y * mip->stride + (size_t)x1 * 4;
  const uint8_t *c = mip->pixels + (size_t)y1 * mip->stride + (size_t)x * 4;
  const uint8_t *d = mip->pixels + (size_t)y1 * mip->stride + (size_t)x1 * 4;
#if defined(__SSE2__)
  __m128 av = mip_channels(a), bv = mip_channels(b);
  __m128 cv = mip_channels(c), dv = mip_channels(d);
  __m128 top = _mm_add_ps(av, _mm_mul_ps(_mm_sub_ps(bv, av), _mm_set1_ps(u)));
  __m128 bottom = _mm_add_ps(cv, _mm_mul_ps(_mm_sub_ps(dv, cv), _mm_set1_ps(u)));
  _mm_storeu_ps(color, _mm_add_ps(top,
      _mm_mul_ps(_mm_sub_ps(bottom, top), _mm_set1_ps(v))));
#else
  for (size_t channel = 0; channel < 4; ++channel) {
    float top = a[channel] + ((float)b[channel] - a[channel]) * u;
    float bottom = c[channel] + ((float)d[channel] - c[channel]) * u;
    color[channel] = top + (bottom - top) * v;
  }
#endif
}
static const uint8_t *mip_point(const cpu_surface_mip *mip, int64_t s, int64_t t) {
  uint32_t x = (uint32_t)((uint64_t)s >> 16), y = (uint32_t)((uint64_t)t >> 16);
  if (x >= mip->width) x = mip->width - 1;
  if (y >= mip->height) y = mip->height - 1;
  return mip->pixels + (size_t)y * mip->stride + (size_t)x * 4;
}
static void shade_span(qa_cpu_renderer *renderer, const brush_surface *surface,
                        const brush_span *span) {
  qa_cpu_statistics *statistics = cpu_row_statistics;
  uint64_t written = 0;
  cpu_framebuffer *buffer = renderer->current;
  size_t index = (size_t)span->y * buffer->width + span->x;
  float x = (float)span->x + 0.5f, y = (float)span->y + 0.5f;
  float sdx = (float)surface->s.x, tdx = (float)surface->t.x, qdx = (float)surface->q.x;
  float sdivw = sdx * x + (float)surface->s.y * y + (float)surface->s.origin;
  float tdivw = tdx * x + (float)surface->t.y * y + (float)surface->t.origin;
  float q = qdx * x + (float)surface->q.y * y + (float)surface->q.origin;
  float depth_step = (float)(surface->depth.x * 0.5);
  float depth = depth_step * x + (float)(surface->depth.y * 0.5) * y +
                (float)(surface->depth.origin * 0.5 + 0.5);
  float first_depth = depth;
  const cpu_surface_mip *first = &surface->mips[0], *second = &surface->mips[1];
  float scale = 65536.0f / (float)(UINT32_C(1) << first->mip);
  float reciprocal = scale / q;
  int64_t s = fixed_texel(sdivw * reciprocal, first->width, 0);
  int64_t t = fixed_texel(tdivw * reciprocal, first->height, 0);
  unsigned mip_shift = second->mip - first->mip;
  float blend = (float)surface->mip_blend;
  uint32_t remaining = span->count;
  cpu_fog_span fog = {0};
  uint32_t fog_left = 0;
  while (remaining) {
    depth = first_depth + depth_step * (float)(span->count - remaining);
    uint32_t count = remaining >= 8 ? 8 : remaining;
    remaining -= count;
    uint32_t advance = remaining ? count : count - 1;
    float fog_q = q;
    sdivw += sdx * (float)advance;
    tdivw += tdx * (float)advance;
    q += qdx * (float)advance;
    reciprocal = scale / q;
    /* qsrc's eight fixed units keep negative steps inside the cached surface. */
    int64_t next_s = fixed_texel(sdivw * reciprocal, first->width, 8);
    int64_t next_t = fixed_texel(tdivw * reciprocal, first->height, 8);
    int64_t ds = remaining ? (next_s - s) >> 3 : advance ? (next_s - s) / advance : 0;
    int64_t dt = remaining ? (next_t - t) >> 3 : advance ? (next_t - t) / advance : 0;
    for (uint32_t i = 0; i < count; ++i, ++index, depth += depth_step) {
      if (surface->fog.enabled && !fog_left) {
        fog = cpu_fog_span_prepare(&surface->fog, fog_q + qdx * (float)i, qdx,
                                   remaining + count - i);
        fog_left = fog.count;
      }
      float written_depth = depth < 0 ? 0 : depth > 1 ? 1 : depth;
      if (written_depth <= buffer->depth[index]) {
        uint8_t *output = buffer->color + index * 4;
        if (!surface->linear && blend == 0) {
          memcpy(output, mip_point(first, s, t), 4);
        } else {
          float color[4];
          if (surface->linear) mip_sample(first, s, t, color);
          else mip_point_color(mip_point(first, s, t), color);
          if (blend > 0) {
            float next[4];
            int64_t ms = s >> mip_shift, mt = t >> mip_shift;
            if (surface->linear) mip_sample(second, ms, mt, next);
            else mip_point_color(mip_point(second, ms, mt), next);
#if defined(__SSE2__)
            __m128 first_color = _mm_loadu_ps(color);
            __m128 next_color = _mm_loadu_ps(next);
            _mm_storeu_ps(color, _mm_add_ps(first_color,
                _mm_mul_ps(_mm_sub_ps(next_color, first_color), _mm_set1_ps(blend))));
#else
            for (size_t channel = 0; channel < 4; ++channel)
              color[channel] += (next[channel] - color[channel]) * blend;
#endif
          }
#if defined(__SSE2__)
          __m128 rounded = _mm_add_ps(_mm_loadu_ps(color), _mm_set1_ps(.5f));
          __m128i channels = _mm_cvttps_epi32(rounded);
          channels = _mm_packs_epi32(channels, _mm_setzero_si128());
          channels = _mm_packus_epi16(channels, _mm_setzero_si128());
          int32_t packed = _mm_cvtsi128_si32(channels);
          memcpy(output, &packed, sizeof(packed));
#else
          for (size_t channel = 0; channel < 4; ++channel)
            output[channel] = (uint8_t)(color[channel] + 0.5f);
#endif
        }
        if (surface->fog.enabled)
          cpu_fog_span_apply(output, &surface->fog, fog.amount, buffer->alpha);
        if (!buffer->alpha) output[3] = 255;
        buffer->depth[index] = written_depth;
        if (statistics) ++written;
      }
      if (surface->fog.enabled) {
        cpu_fog_span_step(&fog);
        --fog_left;
      }
      s += ds; t += dt;
    }
    s = next_s; t = next_t;
  }
  if (statistics) statistics->brush_written += written;
}
static void shade_rows(qa_cpu_renderer *renderer, void *owner,
                        int64_t first, int64_t last) {
  qa_cpu_statistics *statistics = cpu_row_statistics;
  uint64_t start = statistics ? qa_platform_time_ns() : 0;
  const struct cpu_brush_context *context = owner;
  size_t begin = context->rows[(size_t)(first - context->first)];
  size_t end = context->rows[(size_t)(last - context->first + 1)];
  for (size_t i = begin; i < end; ++i)
    shade_span(renderer, &context->surfaces[context->spans[i].surface], &context->spans[i]);
  if (statistics) statistics->brush_shade_ticks += qa_platform_time_ns() - start;
}
static void retire_brush(qa_cpu_renderer *renderer, void *owner) {
  struct cpu_brush_context *context = owner;
  bool pinned = context->pinned;
  context->queued = false;
  context->pinned = false;
  context->surface_count = context->edge_count = context->span_count = 0;
  if (pinned && !cache_pinned(renderer)) cpu_surface_cache_end(renderer);
}
void cpu_brush_clear(qa_cpu_renderer *renderer) {
  for (struct cpu_brush_context *context = renderer->brush_spans;
       context; context = context->next)
    if (!context->queued) retire_brush(renderer, context);
}
bool cpu_brush_flush(qa_cpu_renderer *renderer, qa_error *error) {
  struct cpu_brush_context *context = renderer->brush_spans;
  while (context && context->queued) context = context->next;
  if (!context || !context->surface_count) { cpu_brush_clear(renderer); return true; }
  uint64_t start = renderer->statistics_enabled ? qa_platform_time_ns() : 0;
  bool okay = generate_spans(renderer, context, error);
  CPU_STATS_ADD(renderer, brush_generate_ticks, qa_platform_time_ns() - start);
  CPU_STATS_ADD(renderer, brush_batches, 1);
  CPU_STATS_ADD(renderer, brush_spans, context->span_count);
  if (renderer->statistics_enabled) {
    uint64_t covered = 0;
    for (size_t i = 0; i < context->span_count; ++i) covered += context->spans[i].count;
    renderer->statistics.brush_covered += covered;
  }
  if (okay) {
    context->queued = true;
    cpu_raster_queue_rows(renderer, context->first, context->last, shade_rows,
        context, retire_brush, context->rounding);
  } else cpu_brush_clear(renderer);
  return okay;
}
void cpu_brush_destroy(qa_cpu_renderer *renderer) {
  cpu_raster_flush(renderer);
  struct cpu_brush_context *context = renderer->brush_spans;
  cpu_brush_clear(renderer);
  while (context) {
    struct cpu_brush_context *next = context->next;
    free(context->surfaces); free(context->edges);
    free(context->clip[0]); free(context->clip[1]);
    free(context->active_edges); free(context->active_surfaces); free(context->events);
    free(context->spans); free(context->rows);
    free(context);
    context = next;
  }
  renderer->brush_spans = NULL;
}
