#include "brush_spans.h"
#include "fog_private.h"
#include <fenv.h>
#include <float.h>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

static void blend_fog(uint8_t *pixel, const float color[3], float amount,
                      bool alpha) {
  float factor = cpu_fog_clamp(amount), inverse = 1 - factor;
  for (size_t c = 0; c < 3; ++c)
    pixel[c] = cpu_fog_byte(color[c] * factor + (float)pixel[c] * inverse);
  pixel[3] = alpha ? cpu_fog_byte(factor * factor * 255 + (float)pixel[3] * inverse) : 255;
}
typedef struct cpu_fog_rows {
  cpu_framebuffer *buffer;
  const qa_scene_fog *fog;
  const qa_scene_view *view;
  int64_t x0, x1;
  float a, b, tan_x, tan_y, density, color[3], origin_extinction;
  float height_inverse;
  bool q2, global, height, sky;
} cpu_fog_rows;

typedef struct cpu_fog_distance_span {
  float length, first, second, third, z, z_step;
  int64_t count;
} cpu_fog_distance_span;

static cpu_fog_distance_span fog_distance_span(const cpu_fog_rows *rows,
    int64_t x, int64_t y, int64_t count) {
  const qa_scene_view *view = rows->view;
  float ndc_x = ((float)x + .5f - (float)view->viewport.x) * 2 /
      (float)view->viewport.width - 1;
  float ndc_y = 1 - ((float)y + .5f - (float)view->viewport.y) * 2 /
      (float)view->viewport.height;
  float ray[3], step[3];
  const float forward[3] = {view->axis[0].x, view->axis[0].y, view->axis[0].z};
  const float right[3] = {view->axis[1].x, view->axis[1].y, view->axis[1].z};
  const float up[3] = {view->axis[2].x, view->axis[2].y, view->axis[2].z};
  for (size_t c = 0; c < 3; ++c) {
    ray[c] = forward[c] - right[c] * ndc_x * rows->tan_x +
             up[c] * ndc_y * rows->tan_y;
    step[c] = -right[c] * (2.0f / (float)view->viewport.width) * rows->tan_x;
  }
  float start = sqrtf(ray[0] * ray[0] + ray[1] * ray[1] + ray[2] * ray[2]);
  float c1, c2, c3;
  for (;;) {
    float end_ray[3], n = (float)count;
    for (size_t c = 0; c < 3; ++c) end_ray[c] = ray[c] + step[c] * n;
    float end = sqrtf(end_ray[0] * end_ray[0] + end_ray[1] * end_ray[1] + end_ray[2] * end_ray[2]);
    float start_slope = 0, end_slope = 0;
    for (size_t c = 0; c < 3; ++c) {
      if (start != 0) start_slope += ray[c] / start * step[c];
      if (end != 0) end_slope += end_ray[c] / end * step[c];
    }
    c1 = n * start_slope;
    c2 = 3 * (end - start) - 2 * c1 - n * end_slope;
    c3 = 2 * (start - end) + c1 + n * end_slope;
    if (count <= 1) break;
    float mid_ray[3];
    for (size_t c = 0; c < 3; ++c) mid_ray[c] = ray[c] + step[c] * (n * .5f);
    float middle = sqrtf(mid_ray[0] * mid_ray[0] + mid_ray[1] * mid_ray[1] + mid_ray[2] * mid_ray[2]);
    float estimate = start + .5f * (c1 + .5f * (c2 + .5f * c3));
    if (fabsf(estimate - middle) <= middle * (2 * FLT_EPSILON)) break;
    count = (count + 1) / 2;
  }
  float inverse = 1.0f / (float)count;
  float square = inverse * inverse, cube = square * inverse;
  return (cpu_fog_distance_span){start,
      c1 * inverse + c2 * square + c3 * cube,
      2 * c2 * square + 6 * c3 * cube, 6 * c3 * cube,
      ray[2], step[2], count};
}

static void fog_distance_step(cpu_fog_distance_span *span) {
  span->length += span->first;
  span->first += span->second;
  span->second += span->third;
  span->z += span->z_step;
}

#if defined(__SSE2__)
static __m128 fog_clamp_four(__m128 value) {
  return _mm_min_ps(_mm_max_ps(value, _mm_setzero_ps()), _mm_set1_ps(1));
}

static __m128 fog_select_four(__m128 mask, __m128 yes, __m128 no) {
  return _mm_or_ps(_mm_and_ps(mask, yes), _mm_andnot_ps(mask, no));
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static __m128 fog_exp_repair_four(__m128 attenuation, __m128 value,
                                  unsigned mask) {
  float inputs[4], values[4];
  _mm_storeu_ps(inputs, attenuation);
  _mm_storeu_ps(values, value);
  for (size_t i = 0; i < 4; ++i)
    if ((mask & (1u << i)) == 0) values[i] = cpu_fog_exp(inputs[i]);
  return _mm_loadu_ps(values);
}

static __m128 fog_exp_four(__m128 attenuation) {
  const __m128 zero = _mm_setzero_ps(), one = _mm_set1_ps(1);
  __m128 ordinary = _mm_and_ps(_mm_cmpgt_ps(attenuation, _mm_set1_ps(-89)),
                               _mm_cmplt_ps(attenuation, _mm_set1_ps(104)));
  __m128 input = fog_select_four(ordinary, attenuation, zero);
  __m128 scaled = _mm_mul_ps(input, _mm_set1_ps(CPU_FOG_INVERSE_LOG_TWO));
  __m128i exponent = _mm_cvttps_epi32(scaled);
  exponent = _mm_sub_epi32(exponent, _mm_and_si128(
      _mm_castps_si128(_mm_cmpgt_ps(_mm_cvtepi32_ps(exponent), scaled)),
      _mm_set1_epi32(1)));
  __m128 exponent_float = _mm_cvtepi32_ps(exponent);
  __m128 position = _mm_mul_ps(_mm_sub_ps(_mm_sub_ps(input,
      _mm_mul_ps(exponent_float, _mm_set1_ps(CPU_FOG_LOG_TWO_HIGH))),
      _mm_mul_ps(exponent_float, _mm_set1_ps(CPU_FOG_LOG_TWO_LOW))),
      _mm_set1_ps(256 * CPU_FOG_INVERSE_LOG_TWO));
  __m128 low_position = _mm_cmplt_ps(position, zero);
  __m128 high_position = _mm_cmpgt_ps(position, _mm_set1_ps(256));
  /* Keep scalar endpoint folding at range-reduction rounding boundaries. */
  ordinary = _mm_andnot_ps(_mm_or_ps(low_position, high_position), ordinary);
  position = fog_select_four(low_position, zero, position);
  position = fog_select_four(high_position, _mm_set1_ps(256), position);
  __m128i index = _mm_cvttps_epi32(position);
  index = _mm_add_epi32(index, _mm_cmpeq_epi32(index, _mm_set1_epi32(256)));
  __m128 fraction = _mm_sub_ps(position, _mm_cvtepi32_ps(index));
  int i0 = _mm_cvtsi128_si32(index);
  int i1 = _mm_cvtsi128_si32(_mm_shuffle_epi32(index, _MM_SHUFFLE(1, 1, 1, 1)));
  int i2 = _mm_cvtsi128_si32(_mm_shuffle_epi32(index, _MM_SHUFFLE(2, 2, 2, 2)));
  int i3 = _mm_cvtsi128_si32(_mm_shuffle_epi32(index, _MM_SHUFFLE(3, 3, 3, 3)));
  __m128 left = _mm_set_ps(cpu_fog_exp_table[i3], cpu_fog_exp_table[i2],
      cpu_fog_exp_table[i1], cpu_fog_exp_table[i0]);
  __m128 right = _mm_set_ps(cpu_fog_exp_table[i3 + 1], cpu_fog_exp_table[i2 + 1],
      cpu_fog_exp_table[i1 + 1], cpu_fog_exp_table[i0 + 1]);
  __m128 difference = _mm_sub_ps(right, left);
  __m128 value = CPU_FOG_EXP_CURVE(left, right, fraction, difference,
      _mm_set1_ps(CPU_FOG_EXP_STEP), _mm_set1_ps(2), _mm_set1_ps(3));
  __m128i power = _mm_sub_epi32(_mm_setzero_si128(), exponent);
  __m128i high = _mm_cmpgt_epi32(power, _mm_set1_epi32(127));
  value = _mm_mul_ps(value, fog_select_four(_mm_castsi128_ps(high), _mm_set1_ps(2), one));
  power = _mm_add_epi32(power, high);
  __m128i subnormal = _mm_cmpgt_epi32(_mm_set1_epi32(-126), power);
  power = _mm_add_epi32(power, _mm_and_si128(subnormal, _mm_set1_epi32(126)));
  __m128 scale = _mm_castsi128_ps(_mm_slli_epi32(_mm_add_epi32(power,
      _mm_set1_epi32(127)), 23));
  value = _mm_mul_ps(value, scale);
  value = _mm_mul_ps(value, fog_select_four(_mm_castsi128_ps(subnormal),
      _mm_set1_ps(0x1p-126f), one));
  unsigned mask = (unsigned)_mm_movemask_ps(ordinary);
  return mask == 15 ? value : fog_exp_repair_four(attenuation, value, mask);
}

static __m128i fog_bytes_four(__m128 value) {
  value = _mm_min_ps(_mm_max_ps(value, _mm_setzero_ps()), _mm_set1_ps(255));
  return _mm_cvttps_epi32(_mm_add_ps(value, _mm_set1_ps(.5f)));
}

static void fog_blend_four(__m128 color[4], const __m128 fog_color[3],
                           __m128 amount, __m128 admitted, bool alpha) {
  __m128 factor = fog_clamp_four(amount);
  __m128 inverse = _mm_sub_ps(_mm_set1_ps(1), factor);
  for (size_t c = 0; c < 4; ++c) {
    __m128 blended;
    if (c < 3)
      blended = _mm_add_ps(_mm_mul_ps(fog_color[c], factor),
                           _mm_mul_ps(color[c], inverse));
    else if (alpha)
      blended = _mm_add_ps(_mm_mul_ps(_mm_mul_ps(factor, factor), _mm_set1_ps(255)),
                           _mm_mul_ps(color[c], inverse));
    else blended = _mm_set1_ps(255);
    color[c] = fog_select_four(admitted, _mm_cvtepi32_ps(fog_bytes_four(blended)), color[c]);
  }
}

/* Four adjacent pixels retain the scalar operation order and both byte
 * rounding boundaries when global and height fog are applied together. */
static void q2_fog_four(const cpu_fog_rows *rows, size_t index,
                        cpu_fog_distance_span *span) {
  const qa_scene_fog *fog = rows->fog;
  cpu_framebuffer *buffer = rows->buffer;
  const double *depths = buffer->depth + index;
  __m128 depth = _mm_cvtpd_ps(_mm_loadu_pd(depths));
  depth = _mm_movelh_ps(depth, _mm_cvtpd_ps(_mm_loadu_pd(depths + 2)));
  __m128 sky = _mm_cmpge_ps(depth, _mm_set1_ps(fog->far_depth));
  __m128 geometry = _mm_cmpnge_ps(depth, _mm_set1_ps(fog->far_depth));
  const __m128 one = _mm_set1_ps(1), zero = _mm_setzero_ps();
  uint8_t *pixels = buffer->color + index * 4;
  __m128i packed = _mm_loadu_si128((const __m128i *)(const void *)pixels);
  __m128 color[4];
  for (size_t c = 0; c < 4; ++c) {
    __m128i channel = _mm_and_si128(packed, _mm_set1_epi32(255));
    color[c] = _mm_cvtepi32_ps(channel);
    packed = _mm_srli_epi32(packed, 8);
  }
  __m128 global_color[3];
  for (size_t c = 0; c < 3; ++c) global_color[c] = _mm_set1_ps(rows->color[c]);
  if (rows->sky)
    fog_blend_four(color, global_color, _mm_set1_ps(fog->sky_factor), sky, buffer->alpha);
  if (_mm_movemask_ps(geometry) != 0 && (rows->global || rows->height)) {
    depth = fog_select_four(geometry, depth, zero);
    __m128 eye = _mm_div_ps(_mm_set1_ps(rows->b),
        _mm_sub_ps(_mm_set1_ps(rows->a), _mm_sub_ps(_mm_mul_ps(_mm_set1_ps(2), depth), one)));
    __m128 fragment_depth = _mm_mul_ps(depth, eye);
    if (rows->global) {
      __m128 scaled = _mm_mul_ps(_mm_set1_ps(rows->density), fragment_depth);
      fog_blend_four(color, global_color,
          _mm_sub_ps(one, fog_exp_four(_mm_mul_ps(scaled, scaled))), geometry, buffer->alpha);
    }
    if (rows->height) {
      float ray_z[4], ray_length[4];
      for (size_t i = 0; i < 4; ++i) {
        ray_z[i] = span->z;
        ray_length[i] = span->length;
        fog_distance_step(span);
      }
      __m128 z = _mm_loadu_ps(ray_z);
      __m128 world_z = _mm_add_ps(_mm_set1_ps(rows->view->origin.z), _mm_mul_ps(z, eye));
      __m128 direction = _mm_div_ps(z, _mm_loadu_ps(ray_length));
      direction = fog_select_four(_mm_cmplt_ps(eye, zero), _mm_sub_ps(zero, direction), direction);
      direction = fog_select_four(_mm_cmpeq_ps(direction, zero), _mm_set1_ps(.00001f), direction);
      __m128 extinction_density = _mm_div_ps(
          _mm_sub_ps(_mm_set1_ps(rows->origin_extinction),
              fog_exp_four(_mm_mul_ps(_mm_set1_ps(fog->height_falloff),
                  _mm_sub_ps(world_z, _mm_set1_ps(fog->height_start))))),
          _mm_mul_ps(_mm_set1_ps(fog->height_falloff), direction));
      __m128 extinction = _mm_sub_ps(one, fog_clamp_four(fog_exp_four(extinction_density)));
      __m128 fraction = fog_clamp_four(_mm_mul_ps(
          _mm_sub_ps(world_z, _mm_set1_ps(2 * fog->height_start)), _mm_set1_ps(rows->height_inverse)));
      const float start[3] = {fog->height_color.x, fog->height_color.y, fog->height_color.z};
      const float end[3] = {fog->height_end_color.x, fog->height_end_color.y, fog->height_end_color.z};
      __m128 height_color[3];
      for (size_t c = 0; c < 3; ++c)
        height_color[c] = _mm_mul_ps(_mm_set1_ps(255), fog_clamp_four(_mm_mul_ps(
            _mm_add_ps(_mm_set1_ps(start[c]), _mm_mul_ps(_mm_set1_ps(end[c] - start[c]), fraction)), extinction)));
      __m128 amount = _mm_mul_ps(_mm_sub_ps(one,
          fog_exp_four(_mm_mul_ps(_mm_set1_ps(fog->height_density), fragment_depth))), extinction);
      fog_blend_four(color, height_color, amount, geometry, buffer->alpha);
    }
  } else if (rows->height) {
    for (size_t i = 0; i < 4; ++i) fog_distance_step(span);
  }
  packed = fog_bytes_four(color[3]);
  for (size_t c = 3; c-- > 0;)
    packed = _mm_or_si128(_mm_slli_epi32(packed, 8), fog_bytes_four(color[c]));
  _mm_storeu_si128((__m128i *)(void *)pixels, packed);
}
#endif

static void depth_fog_rows(qa_cpu_renderer *renderer, void *context,
                          int64_t first, int64_t last) {
  (void)renderer;
  const cpu_fog_rows *rows = context;
  cpu_framebuffer *buffer = rows->buffer;
  const qa_scene_fog *fog = rows->fog;
  const qa_scene_view *view = rows->view;
  const float *p = view->projection.m;
  for (int64_t y = first; y <= last; ++y) {
    for (int64_t start = rows->x0; start < rows->x1;) {
      int64_t count = rows->x1 - start < 16 ? rows->x1 - start : 16;
      cpu_fog_distance_span span = {0};
      if (rows->height) {
        span = fog_distance_span(rows, start, y, count);
        count = span.count;
      }
      int64_t x = start;
#if defined(__SSE2__)
      if (rows->q2)
        for (; x + 4 <= start + count; x += 4)
          q2_fog_four(rows, (size_t)y * buffer->width + (size_t)x, &span);
#endif
      for (; x < start + count; ++x) {
        size_t index = (size_t)y * buffer->width + (size_t)x;
        uint8_t *pixel = buffer->color + index * 4;
        float depth = (float)buffer->depth[index];
        if (!rows->q2) {
          float normalized = depth * 2 - 1;
          float eye = fabsf((p[14] - normalized * p[15]) /
                            (normalized * p[11] - p[10]));
          float scaled = rows->density * eye;
          float amount = buffer->depth[index] == view->depth ? cpu_fog_clamp(fog->sky_factor)
                                                : 1 - cpu_fog_exp(scaled * scaled);
          pixel[0] = cpu_fog_byte((float)pixel[0] + (fog->color.x * 255 - (float)pixel[0]) * amount);
          pixel[1] = cpu_fog_byte((float)pixel[1] + (fog->color.y * 255 - (float)pixel[1]) * amount);
          pixel[2] = cpu_fog_byte((float)pixel[2] + (fog->color.z * 255 - (float)pixel[2]) * amount);
        } else if (depth >= fog->far_depth) {
          if (rows->sky) blend_fog(pixel, rows->color, fog->sky_factor, buffer->alpha);
        } else {
          float eye = rows->b / (rows->a - (2 * depth - 1));
          float fragment_depth = depth * eye;
          if (rows->global) {
            float scaled = rows->density * fragment_depth;
            blend_fog(pixel, rows->color, 1 - cpu_fog_exp(scaled * scaled), buffer->alpha);
          }
          if (rows->height) {
            float world_z = view->origin.z + span.z * eye;
            float direction = span.z / span.length;
            if (eye < 0) direction = -direction;
            if (direction == 0) direction = .00001f;
            float extinction_density = (rows->origin_extinction -
                cpu_fog_exp(fog->height_falloff * (world_z - fog->height_start))) /
                (fog->height_falloff * direction);
            float extinction = 1 - cpu_fog_clamp(cpu_fog_exp(extinction_density));
            /* The rerelease shader applies the authored start twice. */
            float fraction = cpu_fog_clamp((world_z - 2 * fog->height_start) * rows->height_inverse);
            float color[3] = {
                cpu_fog_clamp((fog->height_color.x + (fog->height_end_color.x - fog->height_color.x) * fraction) * extinction) * 255,
                cpu_fog_clamp((fog->height_color.y + (fog->height_end_color.y - fog->height_color.y) * fraction) * extinction) * 255,
                cpu_fog_clamp((fog->height_color.z + (fog->height_end_color.z - fog->height_color.z) * fraction) * extinction) * 255};
            float amount = (1 - cpu_fog_exp(fog->height_density * fragment_depth)) * extinction;
            blend_fog(pixel, color, amount, buffer->alpha);
          }
        }
        if (rows->height) {
          fog_distance_step(&span);
        }
      }
      start += count;
    }
  }
}

bool cpu_depth_fog(qa_cpu_renderer *renderer, const qa_scene_fog *fog,
                   const qa_scene_view *view, qa_error *error) {
  cpu_framebuffer *buffer = renderer->current;
  if ((unsigned)fog->kind > QA_FOG_Q2 || !qa_vec_finite(fog->color) ||
      !qa_vec_finite(fog->height_color) ||
      !qa_vec_finite(fog->height_end_color) || !qa_vec_finite(view->origin) ||
      !qa_vec_finite(view->axis[0]) || !qa_vec_finite(view->axis[1]) ||
      !qa_vec_finite(view->axis[2]) || !isfinite(fog->density) ||
      !isfinite(fog->sky_factor) || !isfinite(fog->height_density) ||
      !isfinite(fog->height_start) || !isfinite(fog->height_end) ||
      !isfinite(fog->height_falloff) || !isfinite(fog->far_depth) ||
      !isfinite(view->depth)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Nonfinite CPU depth fog parameters");
    return false;
  }
  for (size_t i = 0; i < 16; ++i)
    if (!isfinite(view->projection.m[i])) {
      qa_error_set(error, QA_ERROR_ARGUMENT, i,
                   "Nonfinite CPU depth fog projection");
      return false;
    }
  if (!buffer->color || fog->kind == QA_FOG_NONE)
    return true;
  bool q2 = fog->kind == QA_FOG_Q2;
  bool global = fog->density > 0;
  bool height = q2 && fog->height_density > 0 && fog->height_falloff > 0;
  bool sky = q2 && fog->sky_drawn && fog->sky_factor > 0;
  if (!global && !height && !sky)
    return true;
  const float *p = view->projection.m;
  if (!view->viewport.width || !view->viewport.height || p[2] != 0 ||
      p[6] != 0 || p[3] != 0 || p[7] != 0 ||
      (q2 &&
       (p[1] != 0 || p[4] != 0 || p[8] != 0 || p[9] != 0 || p[12] != 0 ||
        p[13] != 0 || p[11] != -1 || p[15] != 0 || p[0] == 0 || p[5] == 0))) {
    qa_error_set(
        error, QA_ERROR_ARGUMENT, 0,
        "CPU depth fog projection is incompatible with source equations");
    return false;
  }
  int64_t right = (int64_t)view->viewport.x + view->viewport.width;
  int64_t bottom = (int64_t)view->viewport.y + view->viewport.height;
  int64_t x0 = view->viewport.x > 0 ? view->viewport.x : 0,
          y0 = view->viewport.y > 0 ? view->viewport.y : 0;
  int64_t x1 = right < buffer->width ? right : buffer->width,
          y1 = bottom < buffer->height ? bottom : buffer->height;
  if (x1 <= x0 || y1 <= y0) return true;
  cpu_fog_rows rows = {.buffer = buffer, .fog = fog, .view = view,
      .x0 = x0, .x1 = x1, .a = -p[10], .b = -p[14],
      .tan_x = q2 ? 1 / p[0] : 0, .tan_y = q2 ? 1 / p[5] : 0,
      .density = fog->density / 64,
      .color = {cpu_fog_clamp(fog->color.x) * 255, cpu_fog_clamp(fog->color.y) * 255, cpu_fog_clamp(fog->color.z) * 255},
      .height_inverse = fog->height_end == fog->height_start ? 0 : 1 / (fog->height_end - fog->height_start),
      .q2 = q2, .global = global,
      .height = height, .sky = sky,
      .origin_extinction = height ? cpu_fog_exp(fog->height_falloff *
          (view->origin.z - fog->height_start)) : 0};
  cpu_raster_queue_rows(renderer, y0, y1 - 1, depth_fog_rows, &rows,
                        NULL, fegetround());
  cpu_raster_flush(renderer);
  return true;
}
