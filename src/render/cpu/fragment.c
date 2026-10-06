#include "internal.h"
#include "fog_private.h"
#include "triangle_private.h"
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

static double bound(double value, double low, double high) {
  return fmin(high, fmax(low, value));
}
static double depth_sample(const qa_cpu_renderer *renderer,
                           const qa_scene_image *image, double u, double v) {
  const qa_scene_image_level *level = &image->levels[0];
  uint32_t x = (uint32_t)bound(floor(u * level->width), 0, level->width - 1);
  uint32_t y = (uint32_t)bound(floor(v * level->height), 0, level->height - 1);
  const cpu_framebuffer *target = cpu_target_find(renderer, image);
  if (target)
    return target->depth[(size_t)(target->height - 1 - y) * target->width + x];
  float depth;
  memcpy(&depth,
         (const uint8_t *)level->pixels +
             ((size_t)y * level->width + x) * sizeof(depth),
         sizeof(depth));
  return depth;
}
static double shadow_visibility(const qa_cpu_renderer *renderer,
                                const qa_scene_draw *draw,
                                const qa_scene_shadow_light *shadow,
                                qa_vec3 position, bool model) {
  if (!shadow->shadow_valid || !draw->shadow_atlas)
    return 1;
  qa_scene_vec4 rect = shadow->atlas_rect;
  double texel = 1.0 / draw->shadow_atlas->levels[0].width;
  double base_x, base_y, low_x, low_y, high_x, high_y;
  double z = 0, axial = 0, bias = 0, pa = 0, pb = 0;
  if (!shadow->point_shadow) {
    qa_scene_vec4 clip = qa_scene_matrix_point(shadow->shadow_matrix, position);
    if (clip.w <= 0)
      return 1;
    double x = (double)clip.x / clip.w, y = (double)clip.y / clip.w;
    z = (double)clip.z / clip.w;
    if (x < 0 || x > 1 || y < 0 || y > 1 || z > 1)
      return 1;
    base_x = x * rect.z + rect.x;
    base_y = y * rect.w + rect.y;
    low_x = rect.x + texel;
    low_y = rect.y + texel;
    high_x = rect.x + rect.z - texel;
    high_y = rect.y + rect.w - texel;
    bias = model ? 0.0025 : 0.0005;
  } else {
    double x = position.x - shadow->light.origin.x,
           y = position.y - shadow->light.origin.y,
           zv = position.z - shadow->light.origin.z;
    double ax = fabs(x), ay = fabs(y), az = fabs(zv), right, up;
    int face;
    if (ax >= ay && ax >= az) {
      face = x >= 0 ? 0 : 1;
      right = x >= 0 ? -y : y;
      up = zv;
      axial = ax;
    } else if (ay >= az) {
      face = y >= 0 ? 2 : 3;
      right = y >= 0 ? x : -x;
      up = zv;
      axial = ay;
    } else {
      face = zv >= 0 ? 4 : 5;
      right = zv >= 0 ? y : -y;
      up = x;
      axial = az;
    }
    double near_clip = draw->shadow_near;
    if (axial <= near_clip)
      return 1;
    double far_clip = fmax(shadow->light.radius, near_clip * 2);
    pa = (far_clip + near_clip) / (near_clip - far_clip);
    pb = 2 * far_clip * near_clip / (near_clip - far_clip);
    double width = rect.z / 3, height = rect.w / 2;
    double x0 = rect.x + (face % 3) * width, y0 = rect.y + (face / 3) * height;
    base_x = x0 + (right / axial * 0.5 + 0.5) * width;
    base_y = y0 + (up / axial * 0.5 + 0.5) * height;
    low_x = x0 + texel;
    low_y = y0 + texel;
    high_x = x0 + width - texel;
    high_y = y0 + height - texel;
    bias = (model ? 5 : 1) + axial * (2 / (width / texel)) * (model ? 6 : 2);
  }
  unsigned lit = 0;
  for (unsigned y = 0; y < 2; ++y)
    for (unsigned x = 0; x < 2; ++x) {
      double stored = depth_sample(
          renderer, draw->shadow_atlas,
          bound(base_x + ((double)x - 0.5) * texel, low_x, high_x),
          bound(base_y + ((double)y - 0.5) * texel, low_y, high_y));
      if (shadow->point_shadow ? axial - bias <= pb / (2 * stored - 1 + pa)
                               : z - bias <= stored)
        ++lit;
    }
  return lit * 0.25;
}
static void alias_shade(const qa_cpu_renderer *renderer,
                        const qa_scene_draw *draw, const cpu_fragment *fragment,
                        double out[3]) {
  double keep[3] = {1, 1, 1};
  for (size_t i = 0; i < draw->light_count; ++i) {
    const qa_scene_shadow_light *shadow = &draw->lights[i];
    qa_vec3 fraction = shadow->model_fraction;
    if (fraction.x == 0 && fraction.y == 0 && fraction.z == 0)
      continue;
    double occlusion = 1 - shadow_visibility(renderer, draw, shadow,
                                             fragment->world_position, true);
    keep[0] -= fraction.x * occlusion;
    keep[1] -= fraction.y * occlusion;
    keep[2] -= fraction.z * occlusion;
  }
  for (size_t c = 0; c < 3; ++c)
    out[c] = fmin(fragment->color[c] * draw->shade_scale * fmax(keep[c], 0), 1);
}
static inline void vertex_shade(const cpu_fragment *fragment, const double texel[4], double out[4]) {
  for (size_t c = 0; c < 4; ++c)
    out[c] = fragment->color[c] * texel[c];
}
static void shade(const qa_cpu_renderer *renderer, const qa_scene_draw *draw,
                  const cpu_fragment *fragment, const double texel[4],
                  double out[4]) {
  vertex_shade(fragment, texel, out);
  if (draw->lighting == QA_LIGHT_VERTEX)
    return;
  if (draw->lighting == QA_LIGHT_Q2_MODEL_SHADOW) {
    alias_shade(renderer, draw, fragment, out);
    for (size_t c = 0; c < 3; ++c)
      out[c] *= texel[c];
    return;
  }
  for (size_t c = 0; c < 3; ++c)
    out[c] = draw->light_pass == QA_LIGHT_PASS_MODEL ? fragment->color[c]
             : draw->light_pass != QA_LIGHT_PASS_TEXTURE
                 ? texel[c]
                 : texel[c] * fragment->color[c];
  if (draw->light_pass == QA_LIGHT_PASS_MODEL && draw->model_shade_scale)
    alias_shade(renderer, draw, fragment, out);
  for (size_t i = 0; i < draw->light_count; ++i) {
    const qa_scene_shadow_light *shadow = &draw->lights[i];
    const qa_scene_light *light = &shadow->light;
    if (light->scale == 0 ||
        (light->color.x == 0 && light->color.y == 0 && light->color.z == 0))
      continue;
    qa_vec3 normal = fragment->world_normal;
    double dx = light->origin.x - fragment->world_position.x +
                (light->spot ? 0 : normal.x * 16);
    double dy = light->origin.y - fragment->world_position.y +
                (light->spot ? 0 : normal.y * 16);
    double dz = light->origin.z - fragment->world_position.z +
                (light->spot ? 0 : normal.z * 16);
    double distance = hypot(hypot(dx, dy), dz), inverse = 1 / fmax(distance, 1);
    double falloff = fmax(light->radius - distance, 0) / (light->radius + 64);
    double lambert =
        light->color.x < 0
            ? 1
            : fmax((dx * normal.x + dy * normal.y + dz * normal.z) * inverse,
                   0);
    double scale = falloff * lambert * light->scale;
    if (light->spot) {
      double magnitude = -(dx * light->direction.x + dy * light->direction.y +
                           dz * light->direction.z) *
                         inverse;
      scale *= light->cos_half_angle >= 1
                   ? 0
                   : fmax(1 - (1 - magnitude) / (1 - light->cos_half_angle), 0);
    }
    scale *= shadow_visibility(renderer, draw, shadow, fragment->world_position,
                               false);
    out[0] += light->color.x * scale;
    out[1] += light->color.y * scale;
    out[2] += light->color.z * scale;
  }
  if (draw->light_pass == QA_LIGHT_PASS_MODEL)
    for (size_t c = 0; c < 3; ++c)
      out[c] *= texel[c];
  if (draw->light_pass == QA_LIGHT_PASS_MATERIAL_LIGHTMAP)
    for (size_t c = 0; c < 3; ++c)
      out[c] *= fragment->color[c];
  if (draw->light_pass == QA_LIGHT_PASS_LIGHTMAP)
    out[3] = 1;
}
static double blend_factor(qa_scene_blend factor, double source,
                           double destination, double source_alpha,
                           double destination_alpha, bool alpha) {
  switch (factor) {
  case QA_BLEND_ZERO:
    return 0;
  case QA_BLEND_ONE:
    return 1;
  case QA_BLEND_SRC_COLOR:
    return source;
  case QA_BLEND_ONE_MINUS_SRC_COLOR:
    return 1 - source;
  case QA_BLEND_SRC_ALPHA:
    return source_alpha;
  case QA_BLEND_ONE_MINUS_SRC_ALPHA:
    return 1 - source_alpha;
  case QA_BLEND_DST_ALPHA:
    return destination_alpha;
  case QA_BLEND_ONE_MINUS_DST_ALPHA:
    return 1 - destination_alpha;
  case QA_BLEND_DST_COLOR:
    return destination;
  case QA_BLEND_ONE_MINUS_DST_COLOR:
    return 1 - destination;
  case QA_BLEND_SRC_ALPHA_SATURATE:
    return alpha ? 1 : fmin(source_alpha, 1 - destination_alpha);
  }
  return 0;
}
static uint32_t stencil_operation(uint32_t current,
                                  qa_scene_stencil_op operation,
                                  uint32_t reference, uint32_t maximum) {
  switch (operation) {
  case QA_STENCIL_KEEP:
    return current;
  case QA_STENCIL_ZERO:
    return 0;
  case QA_STENCIL_REPLACE:
    return reference & maximum;
  case QA_STENCIL_INCREMENT:
    return current < maximum ? current + 1 : maximum;
  case QA_STENCIL_DECREMENT:
    return current > 0 ? current - 1 : 0;
  case QA_STENCIL_INVERT:
    return (~current) & maximum;
  }
  return current;
}
static void sample_fragment_texture(const cpu_sampler *sampler,
                                    const cpu_derivative *derivative,
                                    const double uv[2], double out[4]) {
  double rho = 0;
  if (cpu_sampler_requires_derivatives(sampler)) {
    const qa_scene_image_level *level = &sampler->image->levels[0];
    rho = fmax(hypot(derivative->dudx * level->width,
                     derivative->dvdx * level->height),
               hypot(derivative->dudy * level->width,
                     derivative->dvdy * level->height));
  }
  cpu_sample_texture(sampler, uv[0], uv[1], rho, out);
}
static inline void lightmap_color(const qa_scene_draw *draw,
    const cpu_sampler samplers[2], const cpu_fragment *fragment,
    qa_scene_texture_environment environment, double color[4]) {
  double base[4], light[4];
  sample_fragment_texture(&samplers[0], &fragment->derivative[0], fragment->uv[0], base);
  sample_fragment_texture(&samplers[1], &fragment->derivative[1], fragment->uv[1], light);
  vertex_shade(fragment, base, color);
  if (draw->lighting == QA_LIGHT_Q2_WORLD) light[3] = 1;
  for (size_t c = 0; c < 4; ++c) {
    double first = cpu_byte(color[c]) / 255.0;
    double factor = environment == QA_TEXTURE_LIGHTMAP_INVERT_ALPHA ? 1 - cpu_clamp(light[3]) :
        environment == QA_TEXTURE_LIGHTMAP_INVERT_COLOR ? 1 - cpu_clamp(light[c]) : cpu_clamp(light[c]);
    color[c] = first * factor;
  }
}
static inline bool fragment_color(const qa_cpu_renderer *renderer, const qa_scene_draw *draw,
    const cpu_sampler samplers[2], const cpu_fragment *fragment, bool vertex_opaque,
    size_t texture_count, double color[4]) {
  const qa_scene_state *state = &draw->state;
  if (draw->environment >= QA_TEXTURE_LIGHTMAP_MODULATE) {
    lightmap_color(draw, samplers, fragment, draw->environment, color);
    return true;
  }
  double texel[4] = {1, 1, 1, 1};
  if (texture_count && draw->textures[0]) {
    sample_fragment_texture(&samplers[0], &fragment->derivative[0],
                            fragment->uv[0], texel);
    if (!vertex_opaque && draw->luminance_alpha) {
      double luminance =
          (texel[0] + texel[1] + texel[2]) / 3 * fragment->color[3];
      for (size_t c = 0; c < 3; ++c)
        texel[c] *= luminance;
    }
  }
  if (vertex_opaque) vertex_shade(fragment, texel, color);
  else shade(renderer, draw, fragment, texel, color);
  if (texture_count > 1 && draw->textures[1]) {
    sample_fragment_texture(&samplers[1], &fragment->derivative[1],
                            fragment->uv[1], texel);
    for (size_t c = 0; c < 3; ++c) {
      if (vertex_opaque || draw->environment == QA_TEXTURE_MODULATE)
        color[c] *= texel[c];
      else if (draw->environment == QA_TEXTURE_ADD)
        color[c] = cpu_clamp(color[c] + texel[c]);
      else
        color[c] = texel[c];
    }
    if (samplers[1].alpha)
      color[3] = !vertex_opaque && draw->environment == QA_TEXTURE_REPLACE
                     ? texel[3] : color[3] * texel[3];
  }
  if (!vertex_opaque) cpu_fog_color(&draw->fog, fragment->eye_depth, color);
  if (!vertex_opaque && ((state->alpha_test == QA_ALPHA_GT0 && !(color[3] > 0)) ||
      (state->alpha_test == QA_ALPHA_LT128 && !(color[3] < 0.5)) ||
      (state->alpha_test == QA_ALPHA_GE128 && !(color[3] >= 0.5)) ||
      (state->alpha_test == QA_ALPHA_GT666 && !(color[3] > (double)0.666f))))
    return false;
  return true;
}

static inline void clamp_color(double color[4]) {
  for (size_t c = 0; c < 4; ++c) color[c] = cpu_clamp(color[c]);
}
static inline void replace_color(cpu_framebuffer *buffer, size_t index,
                                 const double color[4]) {
  for (size_t c = 0; c < 4; ++c) buffer->color[index * 4 + c] = cpu_byte(color[c]);
  if (!buffer->alpha) buffer->color[index * 4 + 3] = 255;
}
static inline void fragment_store(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
    const cpu_fragment *fragment, cpu_fragment_admission admission, double color[4],
    bool depth_write) {
  cpu_framebuffer *buffer = renderer->current;
  const qa_scene_state *state = &draw->state;
  size_t index = admission.index;
  bool passed = admission.depth_passed, stencil = admission.stencil;
  if (stencil) {
    uint32_t current = buffer->stencil[index];
    uint32_t mask = state->stencil_compare_mask & renderer->stencil_maximum;
    bool stencil_pass =
        renderer->overdraw || state->stencil_test == QA_STENCIL_ALWAYS ||
        (state->stencil_test == QA_STENCIL_EQUAL &&
         (current & mask) == (state->stencil_reference & mask)) ||
        (state->stencil_test == QA_STENCIL_NOTEQUAL &&
         (current & mask) != (state->stencil_reference & mask));
    qa_scene_stencil_op operation = renderer->overdraw ? QA_STENCIL_INCREMENT
                                    : !stencil_pass    ? state->stencil_fail
                                    : passed ? state->stencil_depth_pass
                                             : state->stencil_depth_fail;
    uint32_t write_mask = renderer->overdraw ? renderer->stencil_maximum
                                             : state->stencil_write_mask &
                                                   renderer->stencil_maximum;
    uint32_t result =
        stencil_operation(current, operation, state->stencil_reference,
                          renderer->stencil_maximum);
    buffer->stencil[index] = (current & ~write_mask) | (result & write_mask);
    if (!stencil_pass)
      return;
  }
  if (!passed)
    return;
  if (state->color_write && buffer->color) {
    clamp_color(color);
    if (renderer->preblend_gamma && renderer->gamma_enabled)
      for (size_t c = 0; c < 3; ++c)
        color[c] = renderer->gamma[cpu_byte(color[c])] / 255.0;
    uint8_t *destination = buffer->color + index * 4;
    if (state->blend_source == QA_BLEND_ONE &&
        state->blend_destination == QA_BLEND_ZERO) {
      replace_color(buffer, index, color);
    } else {
      double alpha = buffer->alpha ? destination[3] / 255.0 : 1;
      for (size_t c = 0; c < 4; ++c) {
        double old = destination[c] / 255.0;
        double source_factor = blend_factor(state->blend_source, color[c], old,
                                            color[3], alpha, c == 3);
        double destination_factor = blend_factor(
            state->blend_destination, color[c], old, color[3], alpha, c == 3);
        destination[c] =
            cpu_byte(color[c] * source_factor + old * destination_factor);
      }
      if (!buffer->alpha) destination[3] = 255;
    }
  }
  if (depth_write)
    buffer->depth[index] = fragment->depth;
  if (cpu_row_statistics && ((state->color_write && buffer->color) || depth_write))
    ++cpu_row_statistics->generic_written;
}

void cpu_write_fragment(qa_cpu_renderer *renderer, const qa_scene_draw *draw,
    const cpu_sampler samplers[2], const cpu_fragment *fragment, cpu_fragment_admission admission) {
  if (!admission.depth_passed && !admission.stencil) return;
  double color[4];
  if (!fragment_color(renderer, draw, samplers, fragment, false, draw->texture_count, color)) return;
  fragment_store(renderer, draw, fragment, admission, color,
      draw->state.depth_write && draw->state.depth_test != QA_DEPTH_DISABLED);
}

static inline bool depth_block_rejected(qa_scene_depth test, double depth,
                                        const double *stored) {
#if defined(__SSE2__)
  __m128d mapped = _mm_set1_pd(depth), passed = _mm_setzero_pd();
  for (size_t i = 0; i < 8; i += 2) {
    __m128d previous = _mm_loadu_pd(stored + i), comparison;
    switch (test) {
    case QA_DEPTH_LEQUAL: comparison = _mm_cmple_pd(mapped, previous); break;
    case QA_DEPTH_EQUAL: comparison = _mm_cmpeq_pd(mapped, previous); break;
    case QA_DEPTH_LESS: comparison = _mm_cmplt_pd(mapped, previous); break;
    case QA_DEPTH_GEQUAL: comparison = _mm_cmpge_pd(mapped, previous); break;
    default: return false;
    }
    passed = _mm_or_pd(passed, comparison);
  }
  return _mm_movemask_pd(passed) == 0;
#else
  for (size_t i = 0; i < 8; ++i)
    if (cpu_depth_passes(test, depth, stored[i])) return false;
  return true;
#endif
}

#define ROW_KERNEL(name, pixel_kernel, sample_count) \
  static void name(qa_cpu_renderer *renderer, const qa_scene_draw *draw, \
      const cpu_sampler samplers[2], const cpu_triangle_attributes *attributes, \
      uint32_t left, uint32_t right, uint32_t y) { \
    size_t texture_count = (sample_count); \
    cpu_triangle_row row = cpu_triangle_row_prepare(attributes, draw, texture_count, y); \
    bool stencil = cpu_stencil_active(renderer, &draw->state); \
    bool derivatives[2] = {false, false}; \
    for (size_t unit = 0; unit < texture_count; ++unit) \
      if (draw->textures[unit]) \
        derivatives[unit] = cpu_sampler_requires_derivatives(&samplers[unit]); \
    qa_cpu_statistics *statistics = cpu_row_statistics; \
    uint64_t fragments = 0; \
    bool depth_blocks = !stencil && attributes->depth_range == 0 && \
        draw->state.depth_test != QA_DEPTH_ALWAYS && draw->state.depth_test != QA_DEPTH_DISABLED; \
    /* The inner clamp is finite, so a zero range removes interpolated z. */ \
    double mapped_depth = depth_blocks ? cpu_clamp(attributes->near_depth + attributes->offset) : 0; \
    const double *stored_depth = renderer->current->depth + (size_t)y * renderer->current->width; \
    uint64_t next_block = left; \
    for (uint32_t x = left; x <= right; ++x) { \
      if (depth_blocks && x == next_block && right - x >= 7) { \
        next_block = (uint64_t)x + 8; \
        if (depth_block_rejected(draw->state.depth_test, mapped_depth, stored_depth + x)) { \
          x += 7; continue; \
        } \
      } \
      cpu_fragment fragment; double q; \
      if (!cpu_triangle_fragment_depth(attributes, &row, x, y, &fragment, &q)) continue; \
      cpu_fragment_admission admission = {0}; \
      if (!stencil) { \
        admission = cpu_fragment_admit(renderer, &draw->state, &fragment, false); \
        if (!admission.depth_passed) continue; \
      } \
      cpu_triangle_fragment_attributes(attributes, &row, draw, texture_count, derivatives, q, &fragment); \
      if (stencil) admission = cpu_fragment_admit(renderer, &draw->state, &fragment, true); \
      pixel_kernel(renderer, draw, samplers, &fragment, admission); \
      if (statistics) ++fragments; \
    } \
    if (statistics) statistics->generic_fragments += fragments; \
  }
ROW_KERNEL(generic_row, cpu_write_fragment, draw->texture_count)

#define OPAQUE_KERNEL(name, textures, write_depth) \
  static void name(qa_cpu_renderer *renderer, const qa_scene_draw *draw, \
      const cpu_sampler samplers[2], const cpu_fragment *fragment, cpu_fragment_admission admission) { \
    if (!admission.depth_passed) return; \
    double color[4]; \
    if (!fragment_color(renderer, draw, samplers, fragment, true, textures, color)) return; \
    clamp_color(color); \
    replace_color(renderer->current, admission.index, color); \
    if (write_depth) renderer->current->depth[admission.index] = fragment->depth; \
    if (cpu_row_statistics) ++cpu_row_statistics->generic_written; \
  } \
  ROW_KERNEL(name##_row, name, textures)
OPAQUE_KERNEL(opaque_color, 0, false)
OPAQUE_KERNEL(opaque_color_depth, 0, true)
OPAQUE_KERNEL(opaque_texture, 1, false)
OPAQUE_KERNEL(opaque_texture_depth, 1, true)
OPAQUE_KERNEL(opaque_lightmap, 2, false)
OPAQUE_KERNEL(opaque_lightmap_depth, 2, true)
#undef OPAQUE_KERNEL

#define LIGHTMAP_KERNEL(name, environment, write_depth) \
  static void name(qa_cpu_renderer *renderer, const qa_scene_draw *draw, \
      const cpu_sampler samplers[2], const cpu_fragment *fragment, cpu_fragment_admission admission) { \
    if (!admission.depth_passed) return; \
    double color[4]; \
    lightmap_color(draw, samplers, fragment, environment, color); \
    replace_color(renderer->current, admission.index, color); \
    if (write_depth) renderer->current->depth[admission.index] = fragment->depth; \
    if (cpu_row_statistics) ++cpu_row_statistics->generic_written; \
  } \
  ROW_KERNEL(name##_row, name, 2)
LIGHTMAP_KERNEL(lightmap_modulate, QA_TEXTURE_LIGHTMAP_MODULATE, false)
LIGHTMAP_KERNEL(lightmap_modulate_depth, QA_TEXTURE_LIGHTMAP_MODULATE, true)
LIGHTMAP_KERNEL(lightmap_invert_color, QA_TEXTURE_LIGHTMAP_INVERT_COLOR, false)
LIGHTMAP_KERNEL(lightmap_invert_color_depth, QA_TEXTURE_LIGHTMAP_INVERT_COLOR, true)
LIGHTMAP_KERNEL(lightmap_invert_alpha, QA_TEXTURE_LIGHTMAP_INVERT_ALPHA, false)
LIGHTMAP_KERNEL(lightmap_invert_alpha_depth, QA_TEXTURE_LIGHTMAP_INVERT_ALPHA, true)
#undef LIGHTMAP_KERNEL
#undef ROW_KERNEL

cpu_fragment_row_kernel cpu_fragment_row_select(cpu_fragment_kernel kernel) {
#define ROW_FOR(name) if (kernel == name) return name##_row;
  ROW_FOR(opaque_color)
  ROW_FOR(opaque_color_depth)
  ROW_FOR(opaque_texture)
  ROW_FOR(opaque_texture_depth)
  ROW_FOR(opaque_lightmap)
  ROW_FOR(opaque_lightmap_depth)
  ROW_FOR(lightmap_modulate)
  ROW_FOR(lightmap_modulate_depth)
  ROW_FOR(lightmap_invert_color)
  ROW_FOR(lightmap_invert_color_depth)
  ROW_FOR(lightmap_invert_alpha)
  ROW_FOR(lightmap_invert_alpha_depth)
#undef ROW_FOR
  return generic_row;
}

cpu_fragment_kernel cpu_fragment_select(const qa_cpu_renderer *renderer, const qa_scene_draw *draw) {
  const qa_scene_state *state = &draw->state;
  if (draw->environment >= QA_TEXTURE_LIGHTMAP_MODULATE) {
    static const cpu_fragment_kernel lightmaps[3][2] = {
        {lightmap_modulate, lightmap_modulate_depth},
        {lightmap_invert_color, lightmap_invert_color_depth},
        {lightmap_invert_alpha, lightmap_invert_alpha_depth}};
    return lightmaps[draw->environment - QA_TEXTURE_LIGHTMAP_MODULATE]
                    [state->depth_write && state->depth_test != QA_DEPTH_DISABLED];
  }
  if (draw->lighting != QA_LIGHT_VERTEX || draw->fog.kind == QA_FOG_CONSTANT ||
      draw->fog.kind == QA_FOG_EXP2 || state->alpha_test != QA_ALPHA_NONE ||
      draw->luminance_alpha || (renderer->preblend_gamma && renderer->gamma_enabled) ||
      cpu_stencil_active(renderer, state) || !state->color_write || !renderer->current->color ||
      state->blend_source != QA_BLEND_ONE || state->blend_destination != QA_BLEND_ZERO ||
      (draw->texture_count > 1 && draw->environment != QA_TEXTURE_MODULATE)) return cpu_write_fragment;
  static const cpu_fragment_kernel kernels[3][2] = {
      {opaque_color, opaque_color_depth}, {opaque_texture, opaque_texture_depth},
      {opaque_lightmap, opaque_lightmap_depth}};
  return kernels[draw->texture_count][state->depth_write && state->depth_test != QA_DEPTH_DISABLED];
}
