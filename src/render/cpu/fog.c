#include "brush_spans.h"
#include "fog_private.h"
#include <fenv.h>

static void blend_fog(uint8_t *pixel, qa_vec3 color, double amount,
                      bool alpha) {
  double factor = cpu_clamp(amount), inverse = 1 - factor;
  pixel[0] = cpu_byte(cpu_clamp(color.x) * factor + pixel[0] / 255.0 * inverse);
  pixel[1] = cpu_byte(cpu_clamp(color.y) * factor + pixel[1] / 255.0 * inverse);
  pixel[2] = cpu_byte(cpu_clamp(color.z) * factor + pixel[2] / 255.0 * inverse);
  pixel[3] =
      alpha ? cpu_byte(factor * factor + pixel[3] / 255.0 * inverse) : 255;
}
typedef struct cpu_fog_rows {
  cpu_framebuffer *buffer;
  const qa_scene_fog *fog;
  const qa_scene_view *view;
  int64_t x0, x1;
  float a, b, tan_x, tan_y, density;
  bool q2, global, height, sky;
  double origin_extinction;
} cpu_fog_rows;

typedef struct cpu_fog_distance_span {
  double length, first, second, third, z, z_step;
  int64_t count;
} cpu_fog_distance_span;

static cpu_fog_distance_span fog_distance_span(const cpu_fog_rows *rows,
    int64_t x, int64_t y, int64_t count) {
  const qa_scene_view *view = rows->view;
  double ndc_x = ((double)x + .5 - view->viewport.x) * 2 / view->viewport.width - 1;
  double ndc_y = 1 - ((double)y + .5 - view->viewport.y) * 2 / view->viewport.height;
  double ray[3], step[3];
  const float forward[3] = {view->axis[0].x, view->axis[0].y, view->axis[0].z};
  const float right[3] = {view->axis[1].x, view->axis[1].y, view->axis[1].z};
  const float up[3] = {view->axis[2].x, view->axis[2].y, view->axis[2].z};
  for (size_t c = 0; c < 3; ++c) {
    ray[c] = forward[c] - right[c] * ndc_x * rows->tan_x +
             up[c] * ndc_y * rows->tan_y;
    step[c] = -right[c] * (2.0 / view->viewport.width) * rows->tan_x;
  }
  double start = hypot(hypot(ray[0], ray[1]), ray[2]);
  double end, c1, c2, c3;
  for (;;) {
    double end_ray[3];
    for (size_t c = 0; c < 3; ++c) end_ray[c] = ray[c] + step[c] * (double)count;
    end = hypot(hypot(end_ray[0], end_ray[1]), end_ray[2]);
    double start_slope = 0, end_slope = 0;
    for (size_t c = 0; c < 3; ++c) {
      if (start != 0) start_slope += ray[c] / start * step[c];
      if (end != 0) end_slope += end_ray[c] / end * step[c];
    }
    c1 = (double)count * start_slope;
    c2 = 3 * (end - start) - 2 * c1 - (double)count * end_slope;
    c3 = 2 * (start - end) + c1 + (double)count * end_slope;
    if (count <= 1) break;
    double mid_ray[3];
    for (size_t c = 0; c < 3; ++c) mid_ray[c] = ray[c] + step[c] * ((double)count * .5);
    double middle = hypot(hypot(mid_ray[0], mid_ray[1]), mid_ray[2]);
    double estimate = start + .5 * (c1 + .5 * (c2 + .5 * c3));
    if (fabs(estimate - middle) <= middle * 1e-8) break;
    count = (count + 1) / 2;
  }
  double inverse = 1.0 / (double)count;
  double square = inverse * inverse, cube = square * inverse;
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
        double stored = buffer->depth[index];
        if (!rows->q2) {
          double normalized = stored * 2 - 1;
          double eye = fabs((p[14] - normalized * p[15]) /
                            (normalized * p[11] - p[10]));
          double amount = stored == view->depth ? cpu_clamp(fog->sky_factor)
                                                : cpu_fog_exp2(fog->density, eye);
          pixel[0] = cpu_byte(pixel[0] / 255.0 + (fog->color.x - pixel[0] / 255.0) * amount);
          pixel[1] = cpu_byte(pixel[1] / 255.0 + (fog->color.y - pixel[1] / 255.0) * amount);
          pixel[2] = cpu_byte(pixel[2] / 255.0 + (fog->color.z - pixel[2] / 255.0) * amount);
        } else {
          float depth = (float)stored;
          if (depth >= fog->far_depth) {
            if (rows->sky) blend_fog(pixel, fog->color, fog->sky_factor, buffer->alpha);
          } else {
            double eye = rows->b / (rows->a - (2 * (double)depth - 1));
            double fragment_depth = depth * eye;
            if (rows->global) {
              double scaled = rows->density * fragment_depth;
              blend_fog(pixel, fog->color, 1 - cpu_fog_exp(scaled * scaled), buffer->alpha);
            }
            if (rows->height) {
              double dz = span.z * eye, world_z = view->origin.z + dz;
              double distance = span.length * fabs(eye);
              double direction = distance == 0 ? 0 : dz / distance;
              if (direction == 0) direction = .00001;
              double extinction_density = (rows->origin_extinction -
                  cpu_fog_exp(fog->height_falloff * (world_z - fog->height_start))) /
                  (fog->height_falloff * direction);
              double extinction = 1 - cpu_clamp(cpu_fog_exp(extinction_density));
              /* The rerelease shader applies the authored start twice. */
              double fraction = fog->height_end == fog->height_start ? 0 :
                  cpu_clamp((world_z - 2 * fog->height_start) / (fog->height_end - fog->height_start));
              qa_vec3 color = {
                  (float)((fog->height_color.x + (fog->height_end_color.x - fog->height_color.x) * fraction) * extinction),
                  (float)((fog->height_color.y + (fog->height_end_color.y - fog->height_color.y) * fraction) * extinction),
                  (float)((fog->height_color.z + (fog->height_end_color.z - fog->height_color.z) * fraction) * extinction)};
              double amount = (1 - cpu_fog_exp(fog->height_density * fragment_depth)) * extinction;
              blend_fog(pixel, color, amount, buffer->alpha);
            }
          }
        }
        span.length += span.first;
        span.first += span.second;
        span.second += span.third;
        span.z += span.z_step;
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
      .density = fog->density / 64, .q2 = q2, .global = global,
      .height = height, .sky = sky,
      .origin_extinction = height ? cpu_fog_exp(fog->height_falloff *
          (view->origin.z - fog->height_start)) : 0};
  cpu_raster_queue_rows(renderer, y0, y1 - 1, depth_fog_rows, &rows,
                        NULL, fegetround());
  cpu_raster_flush(renderer);
  return true;
}
