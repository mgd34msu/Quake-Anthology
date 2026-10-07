#include "internal.h"
#include <fenv.h>
#include <float.h>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

bool cpu_texture_components_init(qa_cpu_renderer *renderer, qa_error *error) {
  fenv_t environment;
  bool captured = fegetenv(&environment) == 0;
  renderer->texture_components_ready = false;
  if (captured) (void)fesetenv(FE_DFL_ENV);
  const qa_q3_texture_format formats[3] = {
      QA_Q3_TEXTURE_RGBA8, QA_Q3_TEXTURE_RGB5, QA_Q3_TEXTURE_RGBA4};
  for (size_t format = 0; format < 3; ++format)
    for (size_t value = 0; value < 256; ++value)
      renderer->texture_components[format][value] =
          (float)qa_render_source_texture_component(formats[format], (uint8_t)value);
  if (captured && fesetenv(&environment) != 0) {
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                 "Restoring CPU texture floating-point environment");
    return false;
  }
  renderer->texture_components_ready = true;
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
                  float out[4]) {
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
                     : pixel[c] / 255.0f;
      out[3] = sampler->alpha
                   ? (sampler->target_components
                          ? sampler->target_components[pixel[3]]
                          : pixel[3] / 255.0f)
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
          pixel[c] / 255.0f;
    out[3] = sampler->alpha ?
        (sampler->components ? sampler->components[pixel[3]] :
         pixel[3] / 255.0f) : 1;
  }
}
bool cpu_sampler_prepare(const qa_cpu_renderer *renderer,
                          const qa_scene_image *image, cpu_sampler *sampler) {
  *sampler = (cpu_sampler){.image = image};
  if (!image) return true;
  if (image->streamed) {
    image = cpu_stream_image_read(renderer, image);
    sampler->image = image;
  }
  if (renderer->texture_components_ready) {
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
    filter=qa_render_controls_image_filter(&renderer->controls,image);
    magnification_linear=filter==QA_SCENE_LINEAR || filter==QA_SCENE_LINEAR_MIPMAP_NEAREST ||
        filter==QA_SCENE_LINEAR_MIPMAP_LINEAR;
  }
  bool linear = filter == QA_SCENE_LINEAR ||
                filter == QA_SCENE_LINEAR_MIPMAP_NEAREST ||
                filter == QA_SCENE_LINEAR_MIPMAP_LINEAR;
  bool mipmap =
      filter != QA_SCENE_NEAREST && filter != QA_SCENE_LINEAR;
  float magnification_limit=magnification_linear &&
      (filter==QA_SCENE_NEAREST_MIPMAP_NEAREST || filter==QA_SCENE_NEAREST_MIPMAP_LINEAR)?
      2:1;
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
static size_t nearest_level(float squared, size_t count) {
  if (!(squared > 2)) return 0;
#if FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128
  uint32_t bits;
  memcpy(&bits, &squared, sizeof(bits));
  int exponent = (int)(bits >> 23) - 127;
  size_t level = (size_t)((exponent + 1) / 2);
  /* Exact 2,8,32,... ties select the lower mip. */
  if ((exponent & 1) && !(bits & UINT32_C(0x7fffff))) --level;
  return level < count ? level : count - 1;
#else
  size_t level = 0;
  float threshold = 2;
  while (level + 1 < count && squared > threshold) {
    ++level; threshold *= 4;
  }
  return level;
#endif
}

static void sample_levels(const cpu_sampler *sampler,
    const size_t levels[CPU_PIXEL_LANES], const bool linear[CPU_PIXEL_LANES],
    const float u[CPU_PIXEL_LANES], const float v[CPU_PIXEL_LANES],
    uint8_t active, cpu_texture_color *out) {
  float taps[4][4][CPU_PIXEL_LANES];
  float fx[CPU_PIXEL_LANES] = {0}, fy[CPU_PIXEL_LANES] = {0};
  const qa_scene_image *image = sampler->image;
  bool single = !(active & (active - 1u));
  for (size_t lane = 0; lane < CPU_PIXEL_LANES; ++lane) {
    if (!(active & (1u << lane))) {
      if (!single)
        for (size_t tap = 0; tap < 4; ++tap)
          for (size_t c = 0; c < 4; ++c) taps[tap][c][lane] = 0;
      continue;
    }
    size_t mip = levels[lane];
    const qa_scene_image_level *level = image->levels + mip;
    const cpu_framebuffer *target = mip == 0 ? sampler->target : NULL;
    int64_t x0, y0;
    unsigned count = linear[lane] ? 4u : 1u;
    if (linear[lane]) {
      float x = u[lane] * (float)level->width - .5f;
      float y = v[lane] * (float)level->height - .5f;
      x0 = (int64_t)floorf(x); y0 = (int64_t)floorf(y);
      fx[lane] = x - (float)x0; fy[lane] = y - (float)y0;
    } else {
      x0 = (int64_t)fminf((float)(level->width - 1), floorf(u[lane] * (float)level->width));
      y0 = (int64_t)fminf((float)(level->height - 1), floorf(v[lane] * (float)level->height));
    }
    if (count == 1)
      for (size_t tap = 1; tap < 4; ++tap)
        for (size_t c = 0; c < 4; ++c) taps[tap][c][lane] = 0;
    for (unsigned tap = 0; tap < count; ++tap) {
      int64_t x = texel_axis(image->wrap, level->width, x0 + (tap & 1u));
      int64_t y = texel_axis(image->wrap, level->height, y0 + (tap >> 1));
      float value[4];
      texel(sampler, level, target, x, y, value);
      for (size_t c = 0; c < 4; ++c) taps[tap][c][lane] = value[c];
    }
  }
  if (single) {
    for (size_t c = 0; c < 4; ++c)
      for (size_t lane = 0; lane < CPU_PIXEL_LANES; ++lane) {
        if (!(active & (1u << lane))) { out->channel[c][lane] = 0; continue; }
        out->channel[c][lane] = linear[lane] ?
            taps[0][c][lane] * (1 - fx[lane]) * (1 - fy[lane]) +
            taps[1][c][lane] * fx[lane] * (1 - fy[lane]) +
            taps[2][c][lane] * (1 - fx[lane]) * fy[lane] +
            taps[3][c][lane] * fx[lane] * fy[lane] : taps[0][c][lane];
      }
    return;
  }
#if defined(__SSE2__)
  __m128 x = _mm_loadu_ps(fx), y = _mm_loadu_ps(fy);
  __m128 ix = _mm_sub_ps(_mm_set1_ps(1), x), iy = _mm_sub_ps(_mm_set1_ps(1), y);
  for (size_t c = 0; c < 4; ++c) {
    __m128 value = _mm_add_ps(_mm_mul_ps(_mm_mul_ps(_mm_loadu_ps(taps[0][c]), ix), iy),
        _mm_mul_ps(_mm_mul_ps(_mm_loadu_ps(taps[1][c]), x), iy));
    value = _mm_add_ps(value, _mm_mul_ps(_mm_mul_ps(_mm_loadu_ps(taps[2][c]), ix), y));
    value = _mm_add_ps(value, _mm_mul_ps(_mm_mul_ps(_mm_loadu_ps(taps[3][c]), x), y));
    _mm_storeu_ps(out->channel[c], value);
  }
#else
  for (size_t c = 0; c < 4; ++c)
    for (size_t lane = 0; lane < CPU_PIXEL_LANES; ++lane)
      out->channel[c][lane] =
          taps[0][c][lane] * (1 - fx[lane]) * (1 - fy[lane]) +
          taps[1][c][lane] * fx[lane] * (1 - fy[lane]) +
          taps[2][c][lane] * (1 - fx[lane]) * fy[lane] +
          taps[3][c][lane] * fx[lane] * fy[lane];
#endif
}

void cpu_sample_texture(const cpu_sampler *sampler,
    const cpu_texture_coordinates *coordinates, cpu_texture_color *out) {
  float u[CPU_PIXEL_LANES] = {0}, v[CPU_PIXEL_LANES] = {0};
  float squared[CPU_PIXEL_LANES] = {0}, fraction[CPU_PIXEL_LANES] = {0};
  size_t first[CPU_PIXEL_LANES] = {0}, second[CPU_PIXEL_LANES] = {0};
  bool linear[CPU_PIXEL_LANES] = {false};
  uint8_t active = 0, blend = 0;
  if (!sampler->level_count) {
    for (size_t c = 0; c < 4; ++c)
      for (size_t lane = 0; lane < CPU_PIXEL_LANES; ++lane) out->channel[c][lane] = 1;
    return;
  }
  if (cpu_sampler_requires_derivatives(sampler)) {
    const qa_scene_image_level *level = sampler->image->levels;
    float width = (float)level->width, height = (float)level->height;
#if defined(__SSE2__)
    if (coordinates->active & (coordinates->active - 1u)) {
    __m128 dx = _mm_mul_ps(_mm_loadu_ps(coordinates->dudx), _mm_set1_ps(width));
    __m128 dy = _mm_mul_ps(_mm_loadu_ps(coordinates->dvdx), _mm_set1_ps(height));
    __m128 x = _mm_add_ps(_mm_mul_ps(dx, dx), _mm_mul_ps(dy, dy));
    dx = _mm_mul_ps(_mm_loadu_ps(coordinates->dudy), _mm_set1_ps(width));
    dy = _mm_mul_ps(_mm_loadu_ps(coordinates->dvdy), _mm_set1_ps(height));
    __m128 y = _mm_add_ps(_mm_mul_ps(dx, dx), _mm_mul_ps(dy, dy));
    __m128 value = _mm_max_ps(x, y);
    /* fmaxf retains a finite norm when the other norm is NaN. */
    value = _mm_or_ps(_mm_and_ps(_mm_cmpunord_ps(y, y), x),
        _mm_andnot_ps(_mm_cmpunord_ps(y, y), value));
    _mm_storeu_ps(squared, value);
    } else
#endif
    {
    for (size_t lane = 0; lane < CPU_PIXEL_LANES; ++lane) {
      if (!(coordinates->active & (1u << lane))) continue;
      float x = coordinates->dudx[lane] * width, y = coordinates->dvdx[lane] * height;
      float a = x * x + y * y;
      x = coordinates->dudy[lane] * width; y = coordinates->dvdy[lane] * height;
      float b = x * x + y * y;
      squared[lane] = isnan(b) || a > b ? a : b;
    }
    }

  }
  for (size_t lane = 0; lane < CPU_PIXEL_LANES; ++lane) {
    if (!(coordinates->active & (1u << lane))) continue;
    float x = coordinates->u[lane], y = coordinates->v[lane];
    if (!isfinite(x) || !isfinite(y)) continue;
    if (sampler->image->wrap == QA_SCENE_REPEAT) {
      x -= floorf(x); y -= floorf(y);
    } else { x = cpu_clamp(x); y = cpu_clamp(y); }
    u[lane] = x; v[lane] = y; active |= (uint8_t)(1u << lane);
    bool magnification = !(squared[lane] > sampler->magnification_limit);
    linear[lane] = magnification ? sampler->magnification_linear : sampler->linear;
    if (!magnification && sampler->level_count > 1) {
      if (!sampler->blend) first[lane] = nearest_level(squared[lane], sampler->level_count);
      else {
        float lod = fminf((float)(sampler->level_count - 1), .5f * log2f(squared[lane]));
        first[lane] = (size_t)floorf(lod);
        fraction[lane] = lod - (float)first[lane];
        if (fraction[lane] > 0 && first[lane] + 1 < sampler->level_count) {
          second[lane] = first[lane] + 1; blend |= (uint8_t)(1u << lane);
        }
      }
    }
  }
  if (!active) {
    for (size_t c = 0; c < 4; ++c)
      for (size_t lane = 0; lane < CPU_PIXEL_LANES; ++lane) out->channel[c][lane] = 1;
    return;
  }
  cpu_texture_color sampled;
  sample_levels(sampler, first, linear, u, v, active, &sampled);
  if (blend) {
    cpu_texture_color next;
    sample_levels(sampler, second, linear, u, v, blend, &next);
#if defined(__SSE2__)
    __m128 f = _mm_loadu_ps(fraction), keep = _mm_sub_ps(_mm_set1_ps(1), f);
    for (size_t c = 0; c < 4; ++c)
      _mm_storeu_ps(sampled.channel[c], _mm_add_ps(
          _mm_mul_ps(_mm_loadu_ps(sampled.channel[c]), keep),
          _mm_mul_ps(_mm_loadu_ps(next.channel[c]), f)));
#else
    for (size_t c = 0; c < 4; ++c)
      for (size_t lane = 0; lane < CPU_PIXEL_LANES; ++lane)
        sampled.channel[c][lane] = sampled.channel[c][lane] * (1 - fraction[lane]) +
                                    next.channel[c][lane] * fraction[lane];
#endif
  }
  for (size_t c = 0; c < 4; ++c)
    for (size_t lane = 0; lane < CPU_PIXEL_LANES; ++lane)
      out->channel[c][lane] = active & (1u << lane) ? sampled.channel[c][lane] : 1;
}
