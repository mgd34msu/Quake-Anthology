#ifndef QA_CPU_TRIANGLE_PRIVATE_H
#define QA_CPU_TRIANGLE_PRIVATE_H
#include "internal.h"

typedef struct cpu_attribute_plane {
  double x, y, c;
} cpu_attribute_plane;
typedef struct cpu_triangle_attributes {
  cpu_attribute_plane weights[3], q, color[4], uv[2][2], world[3], normal[3];
  double inverse_area, z[3], scale, near_depth, depth_range, offset;
  double uv_anchor[2][2];
  bool constant_depth, unit_color;
} cpu_triangle_attributes;
typedef struct cpu_triangle_row {
  double weights[3], q, color[4], uv[2][2], world[3], normal[3];
} cpu_triangle_row;

static inline double cpu_attribute_row(cpu_attribute_plane plane, double y) {
  return plane.y * y + plane.c;
}
static inline cpu_triangle_row cpu_triangle_row_prepare(
    const cpu_triangle_attributes *a, const qa_scene_draw *draw,
    size_t texture_count, uint32_t y) {
  double pixel_y = (double)y + 0.5;
  cpu_triangle_row row = {.q = cpu_attribute_row(a->q, pixel_y)};
  if (!a->constant_depth)
    for (size_t i = 0; i < 3; ++i)
      row.weights[i] = a->weights[i].y * pixel_y;
  if (!a->unit_color)
    for (size_t c = 0; c < 4; ++c)
      row.color[c] = cpu_attribute_row(a->color[c], pixel_y);
  for (size_t unit = 0; unit < texture_count; ++unit)
    if (draw->textures[unit])
      for (size_t axis = 0; axis < 2; ++axis)
        row.uv[unit][axis] = cpu_attribute_row(a->uv[unit][axis], pixel_y);
  if (draw->lighting != QA_LIGHT_VERTEX)
    for (size_t axis = 0; axis < 3; ++axis) {
      row.world[axis] = cpu_attribute_row(a->world[axis], pixel_y);
      row.normal[axis] = cpu_attribute_row(a->normal[axis], pixel_y);
    }
  return row;
}
static inline bool cpu_triangle_fragment_depth(const cpu_triangle_attributes *a,
    const cpu_triangle_row *row, uint32_t x, uint32_t y,
    cpu_fragment *fragment, double *reciprocal) {
  double pixel_x = (double)x + 0.5;
  double q = a->q.x * pixel_x + row->q;
  if (q == 0 || !isfinite(q)) return false;
  *reciprocal = 1 / q;
  double z = a->z[0];
  if (!a->constant_depth) {
    /* Retain barycentric depth arithmetic for EQUAL-depth passes. */
    z = 0;
    for (size_t i = 0; i < 3; ++i) {
      double weight = (a->weights[i].x * pixel_x + row->weights[i] +
                       a->weights[i].c) * a->inverse_area;
      z += a->z[i] * weight;
    }
  }
  fragment->x = x; fragment->y = y;
  fragment->eye_depth = a->scale * *reciprocal;
  fragment->depth = cpu_clamp(cpu_clamp(z * 0.5 + 0.5) * a->depth_range +
                             a->near_depth + a->offset);
  if (a->unit_color) {
    double color = cpu_clamp(q * *reciprocal);
    for (size_t c = 0; c < 4; ++c) fragment->color[c] = color;
  }
  return true;
}
static inline void cpu_triangle_fragment_attributes(
    const cpu_triangle_attributes *a, const cpu_triangle_row *row,
    const qa_scene_draw *draw, size_t texture_count, const bool derivatives[2], double reciprocal,
    cpu_fragment *fragment) {
  double pixel_x = (double)fragment->x + 0.5;
  if (!a->unit_color)
    for (size_t c = 0; c < 4; ++c)
      fragment->color[c] = cpu_clamp((a->color[c].x * pixel_x + row->color[c]) * reciprocal);
  for (size_t unit = 0; unit < texture_count; ++unit) {
    if (!draw->textures[unit]) continue;
    double dx[2], dy[2];
    for (size_t axis = 0; axis < 2; ++axis) {
      const cpu_attribute_plane *uv = &a->uv[unit][axis];
      double coordinate = (uv->x * pixel_x + row->uv[unit][axis]) * reciprocal;
      fragment->uv[unit][axis] = a->uv_anchor[unit][axis] + coordinate;
      if (derivatives[unit]) {
        dx[axis] = (uv->x - coordinate * a->q.x) * reciprocal;
        dy[axis] = (uv->y - coordinate * a->q.y) * reciprocal;
      }
    }
    if (derivatives[unit])
      fragment->derivative[unit] = (cpu_derivative){dx[0], dx[1], dy[0], dy[1]};
  }
  if (draw->lighting != QA_LIGHT_VERTEX) {
    double position[3], normal[3];
    for (size_t axis = 0; axis < 3; ++axis) {
      position[axis] = (a->world[axis].x * pixel_x + row->world[axis]) * reciprocal;
      normal[axis] = (a->normal[axis].x * pixel_x + row->normal[axis]) * reciprocal;
    }
    fragment->world_position = (qa_vec3){(float)position[0], (float)position[1], (float)position[2]};
    fragment->world_normal = (qa_vec3){(float)normal[0], (float)normal[1], (float)normal[2]};
  }
}

typedef void (*cpu_fragment_row_kernel)(qa_cpu_renderer *, const qa_scene_draw *,
    const cpu_sampler[2], const cpu_triangle_attributes *, uint32_t, uint32_t, uint32_t);
cpu_fragment_row_kernel cpu_fragment_row_select(cpu_fragment_kernel);
#endif
