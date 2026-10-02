#include "internal.h"

static void blend_fog(uint8_t *pixel, qa_vec3 color, double amount,
                      bool alpha) {
  double factor = cpu_clamp(amount), inverse = 1 - factor;
  pixel[0] = cpu_byte(cpu_clamp(color.x) * factor + pixel[0] / 255.0 * inverse);
  pixel[1] = cpu_byte(cpu_clamp(color.y) * factor + pixel[1] / 255.0 * inverse);
  pixel[2] = cpu_byte(cpu_clamp(color.z) * factor + pixel[2] / 255.0 * inverse);
  pixel[3] =
      alpha ? cpu_byte(factor * factor + pixel[3] / 255.0 * inverse) : 255;
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
  float a = -p[10], b = -p[14], tan_x = q2 ? 1 / p[0] : 0,
        tan_y = q2 ? 1 / p[5] : 0;
  float density = fog->density / 64;
  for (int64_t y = y0; y < y1; ++y)
    for (int64_t x = x0; x < x1; ++x) {
      size_t index = (size_t)y * buffer->width + (size_t)x;
      uint8_t *pixel = buffer->color + index * 4;
      double stored = buffer->depth[index];
      if (!q2) {
        double normalized = stored * 2 - 1;
        double eye =
            fabs((p[14] - normalized * p[15]) / (normalized * p[11] - p[10]));
        double d = fog->density * eye / 64;
        double amount = stored == view->depth ? cpu_clamp(fog->sky_factor)
                                              : 1 - exp(-d * d);
        pixel[0] = cpu_byte(pixel[0] / 255.0 +
                            (fog->color.x - pixel[0] / 255.0) * amount);
        pixel[1] = cpu_byte(pixel[1] / 255.0 +
                            (fog->color.y - pixel[1] / 255.0) * amount);
        pixel[2] = cpu_byte(pixel[2] / 255.0 +
                            (fog->color.z - pixel[2] / 255.0) * amount);
        continue;
      }
      float depth = (float)stored;
      if (depth >= fog->far_depth) {
        if (sky)
          blend_fog(pixel, fog->color, fog->sky_factor, buffer->alpha);
        continue;
      }
      double eye = b / (a - (2 * (double)depth - 1)),
             fragment_depth = depth * eye;
      if (global) {
        double d = density * fragment_depth;
        blend_fog(pixel, fog->color, 1 - exp(-d * d), buffer->alpha);
      }
      if (height) {
        double ndc_x =
            ((double)x + 0.5 - view->viewport.x) * 2 / view->viewport.width - 1;
        double ndc_y =
            1 - ((double)y + 0.5 - view->viewport.y) * 2 / view->viewport.height;
        double dx = (view->axis[0].x - view->axis[1].x * ndc_x * tan_x +
                     view->axis[2].x * ndc_y * tan_y) *
                    eye;
        double dy = (view->axis[0].y - view->axis[1].y * ndc_x * tan_x +
                     view->axis[2].y * ndc_y * tan_y) *
                    eye;
        double dz = (view->axis[0].z - view->axis[1].z * ndc_x * tan_x +
                     view->axis[2].z * ndc_y * tan_y) *
                    eye;
        double world_z = view->origin.z + dz,
               distance = hypot(hypot(dx, dy), dz);
        double direction = distance == 0 ? 0 : dz / distance;
        if (direction == 0)
          direction = 0.00001;
        double extinction_density =
            (exp(-fog->height_falloff * (view->origin.z - fog->height_start)) -
             exp(-fog->height_falloff * (world_z - fog->height_start))) /
            (fog->height_falloff * direction);
        double extinction = 1 - cpu_clamp(exp(-extinction_density));
        /* Q2 rerelease applies the authored start distance twice. */
        double fraction =
            fog->height_end == fog->height_start
                ? 0
                : cpu_clamp((world_z - 2 * fog->height_start) /
                            (fog->height_end - fog->height_start));
        qa_vec3 color = {
            (float)((fog->height_color.x +
                     (fog->height_end_color.x - fog->height_color.x) *
                         fraction) *
                    extinction),
            (float)((fog->height_color.y +
                     (fog->height_end_color.y - fog->height_color.y) *
                         fraction) *
                    extinction),
            (float)((fog->height_color.z +
                     (fog->height_end_color.z - fog->height_color.z) *
                         fraction) *
                    extinction)};
        double amount =
            (1 - exp(-fog->height_density * fragment_depth)) * extinction;
        blend_fog(pixel, color, amount, buffer->alpha);
      }
    }
  return true;
}
