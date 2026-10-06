#include "brush_spans.h"
#include "fog_private.h"
#include <fenv.h>
#include <float.h>

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
      for (int64_t x = start; x < start + count; ++x) {
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
          span.length += span.first;
          span.first += span.second;
          span.second += span.third;
          span.z += span.z_step;
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
