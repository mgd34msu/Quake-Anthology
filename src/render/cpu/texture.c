#include "internal.h"

static void texel(const qa_scene_image *image,
                  const qa_scene_image_level *level,
                  const cpu_framebuffer *target, int64_t x, int64_t y,
                  double out[4]) {
  if (image->wrap == QA_SCENE_REPEAT) {
    x = (x + (int64_t)level->width) % level->width;
    y = (y + (int64_t)level->height) % level->height;
  } else if (x < 0 || y < 0 || x >= level->width || y >= level->height) {
    out[0] = image->border.x;
    out[1] = image->border.y;
    out[2] = image->border.z;
    out[3] = qa_render_source_texture_alpha(image) ? image->border.w : 1;
    return;
  }
  size_t index = (size_t)y * level->width + (size_t)x;
  if (target) {
    index =
        (size_t)(target->height - 1 - (uint32_t)y) * target->width + (size_t)x;
    if (image->kind == QA_SCENE_DEPTH32F) {
      out[0] = out[1] = out[2] = target->depth[index];
      out[3] = 1;
    } else {
      for (size_t c = 0; c < 3; ++c)
        out[c] = target->color[index * 4 + c] / 255.0;
      out[3] = qa_render_source_texture_alpha(image)
                   ? target->color[index * 4 + 3] / 255.0
                   : 1;
    }
  } else if (image->kind == QA_SCENE_DEPTH32F) {
    float value;
    memcpy(&value, (const uint8_t *)level->pixels + index * sizeof(value),
           sizeof(value));
    out[0] = out[1] = out[2] = value;
    out[3] = 1;
  } else {
    const uint8_t *pixel = (const uint8_t *)level->pixels + index * 4;
    for (size_t c = 0; c < 3; ++c)
      out[c] = image->source_q3 ? qa_render_source_texture_component(image->source_format,pixel[c]) : pixel[c] / 255.0;
    out[3] = qa_render_source_texture_alpha(image) ?
        (image->source_q3 ? qa_render_source_texture_component(image->source_format,pixel[3]) : pixel[3] / 255.0) : 1;
  }
}
static void sample_level(const qa_cpu_renderer *renderer,
                         const qa_scene_image *image, size_t index, double u,
                         double v, bool linear, double out[4]) {
  const qa_scene_image_level *level = &image->levels[index];
  const cpu_framebuffer *target =
      index == 0 ? cpu_target_find(renderer, image) : NULL;
  if (image->wrap == QA_SCENE_REPEAT) {
    u -= floor(u);
    v -= floor(v);
  } else {
    u = cpu_clamp(u);
    v = cpu_clamp(v);
  }
  if (!linear) {
    int64_t x = (int64_t)fmin(level->width - 1, floor(u * level->width));
    int64_t y = (int64_t)fmin(level->height - 1, floor(v * level->height));
    texel(image, level, target, x, y, out);
    return;
  }
  double x = u * level->width - 0.5, y = v * level->height - 0.5;
  int64_t x0 = (int64_t)floor(x), y0 = (int64_t)floor(y);
  double fx = x - (double)x0, fy = y - (double)y0;
  double taps[4][4];
  texel(image, level, target, x0, y0, taps[0]);
  texel(image, level, target, x0 + 1, y0, taps[1]);
  texel(image, level, target, x0, y0 + 1, taps[2]);
  texel(image, level, target, x0 + 1, y0 + 1, taps[3]);
  for (size_t c = 0; c < 4; ++c)
    out[c] = taps[0][c] * (1 - fx) * (1 - fy) + taps[1][c] * fx * (1 - fy) +
             taps[2][c] * (1 - fx) * fy + taps[3][c] * fx * fy;
}
void cpu_sample_texture(const qa_cpu_renderer *renderer,
                        const qa_scene_image *image, double u, double v,
                        double rho, double out[4]) {
  out[0] = out[1] = out[2] = out[3] = 1;
  if (!image || !image->levels || !image->level_count || !isfinite(u) ||
      !isfinite(v))
    return;
  qa_scene_filter filter=qa_render_controls_image_filter(&renderer->controls,image);
  bool linear = filter == QA_SCENE_LINEAR ||
                filter == QA_SCENE_LINEAR_MIPMAP_NEAREST ||
                filter == QA_SCENE_LINEAR_MIPMAP_LINEAR;
  bool mipmap =
      filter != QA_SCENE_NEAREST && filter != QA_SCENE_LINEAR;
  bool magnification_linear=image->source_q3?
      qa_cpu_source_image_magnification_linear(&renderer->controls,image):linear;
  double magnification_limit=magnification_linear &&
      (filter==QA_SCENE_NEAREST_MIPMAP_NEAREST || filter==QA_SCENE_NEAREST_MIPMAP_LINEAR)?
      1.4142135623730951:1;
  size_t count = 1;
  if (mipmap) {
    uint32_t width = image->levels[0].width, height = image->levels[0].height;
    for (; count < image->level_count && (width > 1 || height > 1); ++count) {
      width = width > 1 ? width / 2 : 1;
      height = height > 1 ? height / 2 : 1;
      if (image->levels[count].width != width ||
          image->levels[count].height != height)
        return;
    }
  }
  if (!(rho > magnification_limit) || !mipmap || count == 1) {
    bool magnification=!(rho>magnification_limit);
    bool sample_linear=magnification?magnification_linear:linear;
    sample_level(renderer, image, 0, u, v, sample_linear, out);
    return;
  }
  double lod = fmin((double)(count - 1), log2(rho));
  bool blend = filter == QA_SCENE_NEAREST_MIPMAP_LINEAR ||
               filter == QA_SCENE_LINEAR_MIPMAP_LINEAR;
  size_t first =
      blend ? (size_t)floor(lod) : (size_t)fmax(0, ceil(lod + 0.5) - 1);
  sample_level(renderer, image, first, u, v, linear, out);
  double fraction = lod - (double)first;
  if (blend && fraction > 0 && first + 1 < count) {
    double next[4];
    sample_level(renderer, image, first + 1, u, v, linear, next);
    for (size_t c = 0; c < 4; ++c)
      out[c] = out[c] * (1 - fraction) + next[c] * fraction;
  }
}
