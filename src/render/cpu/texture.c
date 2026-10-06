#include "internal.h"
#include <fenv.h>

bool cpu_texture_components_init(qa_cpu_renderer *renderer, qa_error *error) {
  fenv_t environment;
  renderer->texture_components_ready = false;
  if (fegetenv(&environment) != 0)
    return true;
  bool ready = fesetenv(FE_DFL_ENV) == 0 && fegetround() == FE_TONEAREST;
  if (ready) {
    const qa_q3_texture_format formats[3] = {
        QA_Q3_TEXTURE_RGBA8, QA_Q3_TEXTURE_RGB5, QA_Q3_TEXTURE_RGBA4};
    for (size_t format = 0; format < 3; ++format)
      for (size_t value = 0; value < 256; ++value)
        renderer->texture_components[format][value] =
            qa_render_source_texture_component(formats[format], (uint8_t)value);
  }
  if (fesetenv(&environment) != 0) {
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                 "Restoring CPU texture floating-point environment");
    return false;
  }
  renderer->texture_components_ready = ready;
  return true;
}

static int64_t texel_axis(qa_scene_wrap wrap, uint32_t extent,
                           int64_t coordinate) {
  if (wrap != QA_SCENE_REPEAT) return coordinate;
  return coordinate < 0 ? coordinate + extent
         : coordinate >= extent ? coordinate - extent : coordinate;
}
static void texel(const cpu_sampler *sampler,
                  const qa_scene_image_level *level,
                  const cpu_framebuffer *target, int64_t x, int64_t y,
                  double out[4]) {
  const qa_scene_image *image = sampler->image;
  if (image->wrap != QA_SCENE_REPEAT &&
      (x < 0 || y < 0 || x >= level->width || y >= level->height)) {
    out[0] = image->border.x;
    out[1] = image->border.y;
    out[2] = image->border.z;
    out[3] = sampler->alpha ? image->border.w : 1;
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
      const uint8_t *pixel = target->color + index * 4;
      for (size_t c = 0; c < 3; ++c)
        out[c] = sampler->target_components
                     ? sampler->target_components[pixel[c]]
                     : pixel[c] / 255.0;
      out[3] = sampler->alpha
                   ? (sampler->target_components
                          ? sampler->target_components[pixel[3]]
                          : pixel[3] / 255.0)
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
      out[c] = sampler->components ? sampler->components[pixel[c]] :
          image->source_q3 ? qa_render_source_texture_component(image->source_format,pixel[c]) : pixel[c] / 255.0;
    out[3] = sampler->alpha ?
        (sampler->components ? sampler->components[pixel[3]] :
         image->source_q3 ? qa_render_source_texture_component(image->source_format,pixel[3]) : pixel[3] / 255.0) : 1;
  }
}
static void sample_level(const cpu_sampler *sampler, size_t index, double u,
                         double v, bool linear, double out[4]) {
  const qa_scene_image *image = sampler->image;
  const qa_scene_image_level *level = &image->levels[index];
  const cpu_framebuffer *target =
      index == 0 ? sampler->target : NULL;
  if (!linear) {
    int64_t x = (int64_t)fmin(level->width - 1, floor(u * level->width));
    int64_t y = (int64_t)fmin(level->height - 1, floor(v * level->height));
    texel(sampler, level, target,
          texel_axis(image->wrap, level->width, x),
          texel_axis(image->wrap, level->height, y), out);
    return;
  }
  double x = u * level->width - 0.5, y = v * level->height - 0.5;
  int64_t x0 = (int64_t)floor(x), y0 = (int64_t)floor(y);
  double fx = x - (double)x0, fy = y - (double)y0;
  int64_t sx0 = texel_axis(image->wrap, level->width, x0),
          sx1 = texel_axis(image->wrap, level->width, x0 + 1),
          sy0 = texel_axis(image->wrap, level->height, y0),
          sy1 = texel_axis(image->wrap, level->height, y0 + 1);
  if (!target && image->kind != QA_SCENE_DEPTH32F && sampler->components &&
      sx0 >= 0 && sx1 >= 0 && sy0 >= 0 && sy1 >= 0 &&
      sx0 < level->width && sx1 < level->width &&
      sy0 < level->height && sy1 < level->height) {
    const uint8_t *pixels = level->pixels;
    const uint8_t *taps[4] = {
        pixels + ((size_t)sy0 * level->width + (size_t)sx0) * 4,
        pixels + ((size_t)sy0 * level->width + (size_t)sx1) * 4,
        pixels + ((size_t)sy1 * level->width + (size_t)sx0) * 4,
        pixels + ((size_t)sy1 * level->width + (size_t)sx1) * 4};
    const double *components = sampler->components;
    for (size_t c = 0; c < (sampler->alpha ? 4u : 3u); ++c)
      out[c] = components[taps[0][c]] * (1 - fx) * (1 - fy) +
               components[taps[1][c]] * fx * (1 - fy) +
               components[taps[2][c]] * (1 - fx) * fy +
               components[taps[3][c]] * fx * fy;
    if (!sampler->alpha)
      out[3] = (1 - fx) * (1 - fy) + fx * (1 - fy) +
               (1 - fx) * fy + fx * fy;
    return;
  }
  double taps[4][4];
  texel(sampler, level, target, sx0, sy0, taps[0]);
  texel(sampler, level, target, sx1, sy0, taps[1]);
  texel(sampler, level, target, sx0, sy1, taps[2]);
  texel(sampler, level, target, sx1, sy1, taps[3]);
  for (size_t c = 0; c < 4; ++c)
    out[c] = taps[0][c] * (1 - fx) * (1 - fy) + taps[1][c] * fx * (1 - fy) +
             taps[2][c] * (1 - fx) * fy + taps[3][c] * fx * fy;
}
bool cpu_sampler_prepare(const qa_cpu_renderer *renderer,
                          const qa_scene_image *image, cpu_sampler *sampler) {
  *sampler = (cpu_sampler){.image = image};
  if (!image) return true;
  if (image->streamed) {
    image = cpu_stream_image_read(renderer, image);
    sampler->image = image;
  }
  if (renderer->texture_components_ready && fegetround() == FE_TONEAREST) {
    size_t format = !image->source_q3 ? 0 :
        image->source_format == QA_Q3_TEXTURE_RGB5 ? 1 :
        image->source_format == QA_Q3_TEXTURE_RGBA4 ? 2 : 0;
    sampler->components = renderer->texture_components[format];
    sampler->target_components = renderer->texture_components[0];
  }
  sampler->alpha = qa_render_source_texture_alpha(image);
  bool magnification_linear;
  qa_scene_filter filter;
  if (image->source_q3)
    filter=qa_cpu_source_image_sampling(&renderer->controls,image,&magnification_linear);
  else {
    filter=image->filter;
    magnification_linear=filter==QA_SCENE_LINEAR || filter==QA_SCENE_LINEAR_MIPMAP_NEAREST ||
        filter==QA_SCENE_LINEAR_MIPMAP_LINEAR;
  }
  bool linear = filter == QA_SCENE_LINEAR ||
                filter == QA_SCENE_LINEAR_MIPMAP_NEAREST ||
                filter == QA_SCENE_LINEAR_MIPMAP_LINEAR;
  bool mipmap =
      filter != QA_SCENE_NEAREST && filter != QA_SCENE_LINEAR;
  double magnification_limit=magnification_linear &&
      (filter==QA_SCENE_NEAREST_MIPMAP_NEAREST || filter==QA_SCENE_NEAREST_MIPMAP_LINEAR)?
      1.4142135623730951:1;
  size_t count = 1;
  bool image_mipmap = image->filter != QA_SCENE_NEAREST &&
                      image->filter != QA_SCENE_LINEAR;
  if (mipmap || image_mipmap) {
    uint32_t width = image->levels[0].width, height = image->levels[0].height;
    for (; count < image->level_count && (width > 1 || height > 1); ++count) {
      width = width > 1 ? width / 2 : 1;
      height = height > 1 ? height / 2 : 1;
      if (image->levels[count].width != width ||
          image->levels[count].height != height)
        return !image_mipmap;
    }
  }
  sampler->target = cpu_target_find(renderer, image);
  sampler->level_count = mipmap ? count : 1;
  sampler->linear = linear;
  sampler->magnification_linear = magnification_linear;
  sampler->magnification_limit = magnification_limit;
  sampler->blend = filter == QA_SCENE_NEAREST_MIPMAP_LINEAR ||
                    filter == QA_SCENE_LINEAR_MIPMAP_LINEAR;
  return true;
}
void cpu_sample_texture(const cpu_sampler *sampler, double u, double v,
                        double rho, double out[4]) {
  out[0] = out[1] = out[2] = out[3] = 1;
  if (!sampler->level_count || !isfinite(u) || !isfinite(v)) return;
  const qa_scene_image *image = sampler->image;
  if (image->wrap == QA_SCENE_REPEAT) {
    u -= floor(u);
    v -= floor(v);
  } else {
    u = cpu_clamp(u);
    v = cpu_clamp(v);
  }
  if (!(rho > sampler->magnification_limit) || sampler->level_count == 1) {
    bool magnification=!(rho>sampler->magnification_limit);
    bool sample_linear=magnification?sampler->magnification_linear:sampler->linear;
    sample_level(sampler, 0, u, v, sample_linear, out);
    return;
  }
  double lod = fmin((double)(sampler->level_count - 1), log2(rho));
  bool blend = sampler->blend;
  size_t first =
      blend ? (size_t)floor(lod) : (size_t)fmax(0, ceil(lod + 0.5) - 1);
  sample_level(sampler, first, u, v, sampler->linear, out);
  double fraction = lod - (double)first;
  if (blend && fraction > 0 && first + 1 < sampler->level_count) {
    double next[4];
    sample_level(sampler, first + 1, u, v, sampler->linear, next);
    for (size_t c = 0; c < 4; ++c)
      out[c] = out[c] * (1 - fraction) + next[c] * fraction;
  }
}
