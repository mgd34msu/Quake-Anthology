/* Port of src/materials/color.ts. GPL-2.0-or-later. */
#include <math.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/core/renderer_math.h"
#include "qa/foundation/support.h"
#include "qa/materials/color.h"

static float fr(double v) { return (float)v; }
static bool eq(const char *a, const char *b) {
  return a != NULL && strcmp(a, b) == 0;
}
static int64_t byte_value(double value) {
  double integer = trunc(value);
  if (!isfinite(integer) || integer < -2147483648.0 || integer > 2147483647.0)
    qa_range_error("Shader color reaches undefined source float-to-integer conversion");
  return (int64_t)integer & 255;
}
static int64_t normalized_byte(double value) {
  return byte_value(fr(fr(value) * 255.0));
}
static int64_t wave_byte(double value) {
  double clamped = value < 0 ? 0 : value > 1 ? 1 : value;
  return normalized_byte(clamped);
}

qa_vec3 qa_diffuse_color(qa_vec3 normal, qa_entity_lighting lighting) {
  double incoming = qa_dot3(normal, lighting.light_dir);
  if (incoming <= 0) {
    qa_vec3 out = {(double)(lighting.ambient_light_int & 255),
                   (double)((lighting.ambient_light_int >> 8) & 255),
                   (double)((lighting.ambient_light_int >> 16) & 255)};
    return out;
  }
  double parts[3] = {lighting.ambient_light.x, lighting.ambient_light.y,
                     lighting.ambient_light.z};
  double directed[3] = {lighting.directed_light.x, lighting.directed_light.y,
                        lighting.directed_light.z};
  double out_parts[3];
  for (int i = 0; i < 3; i++) {
    double v = trunc(fr(parts[i] + fr(incoming * directed[i])));
    out_parts[i] = (double)(qa_mat_to_int32(v < 255 ? v : 255) & 255);
  }
  qa_vec3 out = {out_parts[0], out_parts[1], out_parts[2]};
  return out;
}

double qa_specular_alpha(qa_vec3 position, qa_vec3 normal,
                         qa_vec3 viewer_origin) {
  qa_vec3 target = {-960, 1980, 96};
  qa_vec3 light = qa_normalize_fast3(qa_sub3(target, position));
  double d = qa_dot3(normal, light);
  qa_vec3 reflected = {fr(fr(normal.x * 2.0) * d) - light.x,
                       fr(fr(normal.y * 2.0) * d) - light.y,
                       fr(fr(normal.z * 2.0) * d) - light.z};
  /* TS vec3() stores each component through Math.fround again. */
  reflected = (qa_vec3){fr(reflected.x), fr(reflected.y), fr(reflected.z)};
  qa_vec3 viewer = qa_sub3(viewer_origin, position);
  double l = fr(qa_dot3(reflected, viewer) *
                qa_inverse_sqrt32(qa_dot3(viewer, viewer)));
  if (l < 0) return 0;
  l = fr(l * l);
  l = fr(l * l);
  double value = trunc(fr(l * 255));
  return value < 255 ? value : 255;
}

static qa_vec4 waveform_color(qa_waveform wave, double time,
                              double identity_light,
                              const qa_renderer_noise *noise) {
  double glow;
  if (eq(wave.kind, "noise")) {
    double t = fr(fr(time + wave.phase) * wave.frequency);
    glow = fr(wave.base +
              fr(qa_renderer_noise_sample(noise, 0, 0, 0, t) * wave.amplitude));
  } else {
    glow = fr(qa_mat_evaluate_waveform(wave, time) * identity_light);
  }
  double color = (double)wave_byte(glow) / 255;
  qa_vec4 out = {color, color, color, 1};
  return out;
}

static qa_vec4 evaluate_rgb(qa_mat_shader_stage stage,
                            qa_material_vertex vertex,
                            qa_stage_color_context context,
                            bool has_source_rgb, int source_rgb) {
  double identity_light = context.identity_light;
  qa_vec4 entity = context.entity_rgba;
  double red = 0, green = 0, blue = 0;
  double alpha = (double)normalized_byte(context.previous_color.w);
  const char *kind = stage.rgb_gen.kind;
  if (has_source_rgb && source_rgb == QA_MAT_CGEN_BAD) {
    red = green = blue = alpha = (double)normalized_byte(identity_light);
  } else if (eq(kind, "identity")) {
    red = green = blue = alpha = 255;
  } else if (eq(kind, "identitylighting")) {
    red = green = blue = alpha = (double)normalized_byte(identity_light);
  } else if (eq(kind, "entity")) {
    red = entity.x; green = entity.y; blue = entity.z; alpha = entity.w;
  } else if (eq(kind, "oneminusentity")) {
    red = 255 - entity.x; green = 255 - entity.y;
    blue = 255 - entity.z; alpha = 255 - entity.w;
  } else if (eq(kind, "vertex")) {
    red = (double)byte_value(fr(vertex.color.x * identity_light));
    green = (double)byte_value(fr(vertex.color.y * identity_light));
    blue = (double)byte_value(fr(vertex.color.z * identity_light));
    alpha = vertex.color.w;
  } else if (eq(kind, "exactvertex")) {
    red = vertex.color.x; green = vertex.color.y;
    blue = vertex.color.z; alpha = vertex.color.w;
  } else if (eq(kind, "oneminusvertex")) {
    red = (double)byte_value(fr((255 - vertex.color.x) * identity_light));
    green = (double)byte_value(fr((255 - vertex.color.y) * identity_light));
    blue = (double)byte_value(fr((255 - vertex.color.z) * identity_light));
  } else if (eq(kind, "const")) {
    red = (double)normalized_byte(stage.rgb_gen.color.x);
    green = (double)normalized_byte(stage.rgb_gen.color.y);
    blue = (double)normalized_byte(stage.rgb_gen.color.z);
    alpha = eq(stage.alpha_gen.kind, "const")
        ? (double)byte_value(stage.alpha_gen.alpha * 255) : 0;
  } else if (eq(kind, "wave")) {
    return waveform_color(stage.rgb_gen.wave, context.time, identity_light,
                          context.noise);
  } else if (eq(kind, "lightingdiffuse")) {
    if (context.lighting == NULL)
      qa_range_error("lightingDiffuse requires entity lighting");
    qa_vec3 color = qa_diffuse_color(vertex.normal, *context.lighting);
    red = color.x; green = color.y; blue = color.z;
    alpha = qa_dot3(vertex.normal, context.lighting->light_dir) <= 0
        ? (double)(context.lighting->ambient_light_int >> 24) : 255;
  }
  qa_vec4 out = {(double)byte_value(red) / 255, (double)byte_value(green) / 255,
                 (double)byte_value(blue) / 255, (double)byte_value(alpha) / 255};
  return out;
}

static qa_vec4 evaluate_alpha(qa_mat_shader_stage stage,
                              qa_material_vertex vertex,
                              qa_stage_color_context context,
                              bool has_source_rgb, int source_rgb) {
  double identity_light = context.identity_light;
  qa_vec4 entity = context.entity_rgba;
  double alpha = (double)normalized_byte(context.previous_color.w);
  const char *kind = stage.alpha_gen.kind;
  if (eq(kind, "identity")) {
    if ((has_source_rgb && source_rgb == QA_MAT_CGEN_BAD) ||
        (!eq(stage.rgb_gen.kind, "identity") &&
         (!eq(stage.rgb_gen.kind, "vertex") || identity_light != 1)))
      alpha = 255;
  } else if (eq(kind, "entity")) {
    if (has_source_rgb || (!eq(stage.rgb_gen.kind, "identity") &&
                           !eq(stage.rgb_gen.kind, "lightingdiffuse")))
      alpha = entity.w;
  } else if (eq(kind, "oneminusentity")) {
    alpha = 255 - entity.w;
  } else if (eq(kind, "vertex")) {
    alpha = vertex.color.w;
  } else if (eq(kind, "oneminusvertex")) {
    alpha = 255 - vertex.color.w;
  } else if (eq(kind, "const")) {
    alpha = (double)byte_value(stage.alpha_gen.alpha * 255);
  } else if (eq(kind, "wave")) {
    alpha = (double)wave_byte(
        qa_mat_evaluate_waveform(stage.alpha_gen.wave, context.time));
  } else if (eq(kind, "portal")) {
    qa_vec3 delta = qa_sub3(vertex.position, context.view_origin);
    alpha = (double)wave_byte(
        fr(fr(sqrt(qa_dot3(delta, delta))) / stage.alpha_gen.range));
  } else if (eq(kind, "lightingspecular")) {
    alpha = qa_specular_alpha(vertex.position, vertex.normal,
                              context.local_view_origin);
  }
  qa_vec4 out = context.previous_color;
  out.w = (double)byte_value(alpha) / 255;
  return out;
}

qa_vec4 qa_evaluate_stage_color(qa_mat_shader_stage stage,
    qa_material_vertex vertex, qa_stage_color_context context,
    bool skip_alpha, bool has_source_rgb, int source_rgb) {
  qa_vec4 color = evaluate_rgb(stage, vertex, context, has_source_rgb,
                               source_rgb);
  if (skip_alpha) return color;
  context.previous_color = color;
  return evaluate_alpha(stage, vertex, context, has_source_rgb, source_rgb);
}

double qa_animated_picture_index(double time, double frequency, double count) {
  if (!(isfinite(count) && count == trunc(count) &&
        fabs(count) <= 9007199254740991.0) || count < 1)
    qa_range_error("Animated picture needs registered frames");
  double value = trunc(fr(fr(time * frequency) * 1024.0));
  if (!isfinite(value) || value < -2147483648.0 || value > 2147483647.0)
    qa_range_error("Animated picture index reaches undefined source float-to-int conversion");
  int32_t index = (int32_t)value >> 10;
  int64_t used = index > 0 ? index : 0;
  return (double)(used % (int64_t)count);
}
