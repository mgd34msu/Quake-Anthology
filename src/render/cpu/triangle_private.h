#ifndef QA_CPU_TRIANGLE_PRIVATE_H
#define QA_CPU_TRIANGLE_PRIVATE_H
#include "internal.h"
#include <float.h>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

typedef struct cpu_attribute_plane {
  float x, y, c;
} cpu_attribute_plane;
typedef struct cpu_triangle_attributes {
  cpu_attribute_plane weights[3], q, color[4], uv[2][2], world[3], normal[3];
  float inverse_area, z[3], scale, near_depth, depth_range, offset;
  float uv_anchor[2][2];
  bool constant_depth, unit_color;
} cpu_triangle_attributes;
typedef struct cpu_triangle_row {
  float weights[3], q, color[4], uv[2][2], world[3], normal[3];
} cpu_triangle_row;

static inline float cpu_attribute_row(cpu_attribute_plane plane, float y) {
  return plane.y * y + plane.c;
}
static inline cpu_triangle_row cpu_triangle_row_prepare(
    const cpu_triangle_attributes *a, const qa_scene_draw *draw,
    size_t texture_count, uint32_t y) {
  float pixel_y = (float)y + 0.5f;
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
    cpu_fragment *fragment, float *q_out) {
  float pixel_x = (float)x + 0.5f;
  float q = a->q.x * pixel_x + row->q;
  if (q == 0 || !isfinite(q)) return false;
  *q_out = q;
  float z = a->z[0];
  if (!a->constant_depth) {
    /* Retain barycentric depth arithmetic for EQUAL-depth passes. */
    z = 0;
    for (size_t i = 0; i < 3; ++i) {
      float weight = (a->weights[i].x * pixel_x + row->weights[i] +
                       a->weights[i].c) * a->inverse_area;
      z += a->z[i] * weight;
    }
  }
  fragment->x = x; fragment->y = y;
  fragment->depth = cpu_clamp(cpu_clamp(z * 0.5f + 0.5f) * a->depth_range +
                             a->near_depth + a->offset);
  return true;
}
static inline void cpu_triangle_fragment_attributes(
    const cpu_triangle_attributes *a, const cpu_triangle_row *row,
    const qa_scene_draw *draw, size_t texture_count, const bool derivatives[2], float q,
    cpu_fragment *fragment) {
  float reciprocal = 1 / q;
  fragment->eye_depth = a->scale * reciprocal;
  if (a->unit_color) {
    float color = cpu_clamp(q * reciprocal);
    for (size_t c = 0; c < 4; ++c) fragment->color[c] = color;
  }
  float pixel_x = (float)fragment->x + 0.5f;
  if (!a->unit_color)
    for (size_t c = 0; c < 4; ++c)
      fragment->color[c] = cpu_clamp((a->color[c].x * pixel_x + row->color[c]) * reciprocal);
  for (size_t unit = 0; unit < texture_count; ++unit) {
    if (!draw->textures[unit]) continue;
    float dx[2], dy[2];
    for (size_t axis = 0; axis < 2; ++axis) {
      const cpu_attribute_plane *uv = &a->uv[unit][axis];
      float coordinate = (uv->x * pixel_x + row->uv[unit][axis]) * reciprocal;
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
    float position[3], normal[3];
    for (size_t axis = 0; axis < 3; ++axis) {
      position[axis] = (a->world[axis].x * pixel_x + row->world[axis]) * reciprocal;
      normal[axis] = (a->normal[axis].x * pixel_x + row->normal[axis]) * reciprocal;
    }
    fragment->world_position = (qa_vec3){(float)position[0], (float)position[1], (float)position[2]};
    fragment->world_normal = (qa_vec3){(float)normal[0], (float)normal[1], (float)normal[2]};
  }
}

typedef struct cpu_fragment_packet {
  float q[CPU_PIXEL_LANES], depth[CPU_PIXEL_LANES], eye_depth[CPU_PIXEL_LANES];
  float color[4][CPU_PIXEL_LANES];
  cpu_texture_coordinates texture[2];
  float world[3][CPU_PIXEL_LANES], normal[3][CPU_PIXEL_LANES];
  uint8_t active;
} cpu_fragment_packet;

#if defined(__SSE2__)
static inline __m128 cpu_clamp_four(__m128 value) {
  return _mm_min_ps(_mm_max_ps(value, _mm_setzero_ps()), _mm_set1_ps(1));
}
static inline __m128 cpu_plane_four(cpu_attribute_plane plane, __m128 x,
                                     float row) {
  return _mm_add_ps(_mm_mul_ps(_mm_set1_ps(plane.x), x), _mm_set1_ps(row));
}
#endif

static inline void cpu_triangle_packet_depth(const cpu_triangle_attributes *a,
    const cpu_triangle_row *row, uint32_t left, uint32_t y, unsigned count,
    cpu_fragment_packet *packet) {
  packet->active = (uint8_t)((1u << count) - 1u);
#if defined(__SSE2__)
  __m128 x = _mm_set_ps((float)left + 3.5f, (float)left + 2.5f,
      (float)left + 1.5f, (float)left + .5f);
  __m128 q = cpu_plane_four(a->q, x, row->q);
  __m128 finite = _mm_cmple_ps(_mm_andnot_ps(_mm_set1_ps(-0.0f), q), _mm_set1_ps(FLT_MAX));
  packet->active &= (uint8_t)_mm_movemask_ps(_mm_and_ps(finite, _mm_cmpneq_ps(q, _mm_setzero_ps())));
  _mm_storeu_ps(packet->q, q);
  __m128 z = _mm_set1_ps(a->z[0]);
  if (!a->constant_depth) {
    z = _mm_setzero_ps();
    for (size_t i = 0; i < 3; ++i) {
      __m128 weight = _mm_mul_ps(_mm_add_ps(cpu_plane_four(a->weights[i], x, row->weights[i]),
          _mm_set1_ps(a->weights[i].c)), _mm_set1_ps(a->inverse_area));
      z = _mm_add_ps(z, _mm_mul_ps(_mm_set1_ps(a->z[i]), weight));
    }
  }
  __m128 depth = cpu_clamp_four(_mm_add_ps(_mm_mul_ps(z, _mm_set1_ps(.5f)), _mm_set1_ps(.5f)));
  depth = _mm_add_ps(_mm_add_ps(_mm_mul_ps(depth, _mm_set1_ps(a->depth_range)),
      _mm_set1_ps(a->near_depth)), _mm_set1_ps(a->offset));
  _mm_storeu_ps(packet->depth, cpu_clamp_four(depth));
#else
  for (unsigned lane = 0; lane < count; ++lane) {
    cpu_fragment fragment;
    if (!cpu_triangle_fragment_depth(a, row, left + lane, y, &fragment, packet->q + lane))
      packet->active &= (uint8_t)~(1u << lane);
    else packet->depth[lane] = fragment.depth;
  }
#endif
  (void)y;
}

static inline void cpu_triangle_packet_attributes(const cpu_triangle_attributes *a,
    const cpu_triangle_row *row, const qa_scene_draw *draw, size_t texture_count,
    const bool derivatives[2], uint32_t left, uint32_t y, cpu_fragment_packet *packet) {
#if defined(__SSE2__)
  __m128 x = _mm_set_ps((float)left + 3.5f, (float)left + 2.5f,
      (float)left + 1.5f, (float)left + .5f);
  __m128 mask = _mm_castsi128_ps(_mm_set_epi32(packet->active & 8 ? -1 : 0,
      packet->active & 4 ? -1 : 0, packet->active & 2 ? -1 : 0, packet->active & 1 ? -1 : 0));
  __m128 q = _mm_or_ps(_mm_and_ps(mask, _mm_loadu_ps(packet->q)),
      _mm_andnot_ps(mask, _mm_set1_ps(1)));
  __m128 reciprocal = _mm_div_ps(_mm_set1_ps(1), q);
  _mm_storeu_ps(packet->eye_depth, _mm_mul_ps(_mm_set1_ps(a->scale), reciprocal));
  for (size_t c = 0; c < 4; ++c) {
    __m128 color = a->unit_color ? _mm_mul_ps(q, reciprocal) :
        _mm_mul_ps(cpu_plane_four(a->color[c], x, row->color[c]), reciprocal);
    _mm_storeu_ps(packet->color[c], cpu_clamp_four(color));
  }
  for (size_t unit = 0; unit < texture_count; ++unit) {
    cpu_texture_coordinates *texture = packet->texture + unit;
    texture->active = packet->active;
    if (!draw->textures[unit]) continue;
    for (size_t axis = 0; axis < 2; ++axis) {
      const cpu_attribute_plane *uv = &a->uv[unit][axis];
      __m128 coordinate = _mm_mul_ps(cpu_plane_four(*uv, x, row->uv[unit][axis]), reciprocal);
      _mm_storeu_ps(axis ? texture->v : texture->u,
          _mm_add_ps(_mm_set1_ps(a->uv_anchor[unit][axis]), coordinate));
      if (derivatives[unit]) {
        _mm_storeu_ps(axis ? texture->dvdx : texture->dudx, _mm_mul_ps(
            _mm_sub_ps(_mm_set1_ps(uv->x), _mm_mul_ps(coordinate, _mm_set1_ps(a->q.x))), reciprocal));
        _mm_storeu_ps(axis ? texture->dvdy : texture->dudy, _mm_mul_ps(
            _mm_sub_ps(_mm_set1_ps(uv->y), _mm_mul_ps(coordinate, _mm_set1_ps(a->q.y))), reciprocal));
      }
    }
  }
  if (draw->lighting != QA_LIGHT_VERTEX)
    for (size_t axis = 0; axis < 3; ++axis) {
      _mm_storeu_ps(packet->world[axis], _mm_mul_ps(cpu_plane_four(a->world[axis], x, row->world[axis]), reciprocal));
      _mm_storeu_ps(packet->normal[axis], _mm_mul_ps(cpu_plane_four(a->normal[axis], x, row->normal[axis]), reciprocal));
    }
#else
  for (unsigned lane = 0; lane < CPU_PIXEL_LANES; ++lane) {
    if (!(packet->active & (1u << lane))) continue;
    cpu_fragment fragment = {.x = left + lane, .y = y};
    cpu_triangle_fragment_attributes(a, row, draw, texture_count, derivatives,
                                     packet->q[lane], &fragment);
    packet->eye_depth[lane] = fragment.eye_depth;
    for (size_t c = 0; c < 4; ++c) packet->color[c][lane] = fragment.color[c];
    for (size_t unit = 0; unit < texture_count; ++unit) {
      cpu_texture_coordinates *texture = packet->texture + unit;
      texture->active = packet->active;
      texture->u[lane] = fragment.uv[unit][0]; texture->v[lane] = fragment.uv[unit][1];
      if (derivatives[unit]) {
        texture->dudx[lane] = fragment.derivative[unit].dudx;
        texture->dvdx[lane] = fragment.derivative[unit].dvdx;
        texture->dudy[lane] = fragment.derivative[unit].dudy;
        texture->dvdy[lane] = fragment.derivative[unit].dvdy;
      }
    }
    if (draw->lighting != QA_LIGHT_VERTEX) {
      packet->world[0][lane] = fragment.world_position.x;
      packet->world[1][lane] = fragment.world_position.y;
      packet->world[2][lane] = fragment.world_position.z;
      packet->normal[0][lane] = fragment.world_normal.x;
      packet->normal[1][lane] = fragment.world_normal.y;
      packet->normal[2][lane] = fragment.world_normal.z;
    }
  }
#endif
  (void)y;
}

static inline cpu_fragment cpu_fragment_packet_lane(const cpu_fragment_packet *packet,
    const qa_scene_draw *draw, const bool derivatives[2], uint32_t x, uint32_t y, size_t lane) {
  cpu_fragment fragment = {.x = x, .y = y, .depth = packet->depth[lane],
      .eye_depth = packet->eye_depth[lane]};
  for (size_t c = 0; c < 4; ++c) fragment.color[c] = packet->color[c][lane];
  for (size_t unit = 0; unit < draw->texture_count; ++unit) {
    if (!draw->textures[unit]) continue;
    const cpu_texture_coordinates *texture = packet->texture + unit;
    fragment.uv[unit][0] = texture->u[lane]; fragment.uv[unit][1] = texture->v[lane];
    if (derivatives[unit]) fragment.derivative[unit] = (cpu_derivative){texture->dudx[lane], texture->dvdx[lane],
        texture->dudy[lane], texture->dvdy[lane]};
  }
  if (draw->lighting != QA_LIGHT_VERTEX) {
    fragment.world_position = (qa_vec3){packet->world[0][lane],packet->world[1][lane],packet->world[2][lane]};
    fragment.world_normal = (qa_vec3){packet->normal[0][lane],packet->normal[1][lane],packet->normal[2][lane]};
  }
  return fragment;
}

typedef void (*cpu_fragment_row_kernel)(qa_cpu_renderer *, const qa_scene_draw *,
    const cpu_sampler[2], const cpu_triangle_attributes *, uint32_t, uint32_t, uint32_t);
cpu_fragment_row_kernel cpu_fragment_row_select(cpu_fragment_kernel);
#endif
