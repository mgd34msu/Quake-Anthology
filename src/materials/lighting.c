/* Port of src/materials/lighting.ts. GPL-2.0-or-later. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/materials/lighting.h"
#include "qa/materials/shader_types.h"

static float fr(double v) { return (float)v; }
static bool kind_is(const char *kind, const char *name) {
  return kind != NULL && strcmp(kind, name) == 0;
}

double qa_q1_light_style(const char *map, double time, int interpolation) {
  size_t length = strlen(map);
  if (length == 0) return 256;
  double phase = time * 10;
  double frame = floor(phase);
  int64_t f = (int64_t)frame;
  int64_t n = (int64_t)length;
  size_t index = (size_t)(((f % n) + n) % n);
  int current = (unsigned char)map[index] - 97;
  int next = (unsigned char)map[(index + 1) % length] - 97;
  if (interpolation < 2 && abs(next - current) >= 6) next = current;
  return trunc(current * 22 + (next - current) * 22 *
               (interpolation == 0 ? 0 : phase - frame));
}

qa_q2_light_style qa_q2_light_style_value(const char *map, double milliseconds) {
  int64_t frame = (int64_t)trunc(milliseconds / 100);
  size_t length = strlen(map);
  double value = 1;
  if (length != 0) {
    int64_t n = (int64_t)length;
    size_t index = (size_t)(((frame % n) + n) % n);
    value = ((unsigned char)map[index] - 97) / 12.0;
  }
  float component = fr(value);
  qa_q2_light_style out = {{component, component, component},
      fr((double)fr(component + component) + component)};
  return out;
}

qa_vec2 qa_lightmap_coordinates(qa_vec3 position,
                                qa_lightmap_projection projection) {
  if (kind_is(projection.kind, "decoupled")) {
    qa_vec2 out = {qa_dot3(position, projection.mapping.axes[0]) +
                       projection.mapping.offset.x,
                   qa_dot3(position, projection.mapping.axes[1]) +
                       projection.mapping.offset.y};
    return out;
  }
  qa_vec4 s = projection.texture.s, t = projection.texture.t;
  qa_vec3 sv = {s.x, s.y, s.z}, tv = {t.x, t.y, t.z};
  qa_vec2 out = {(qa_dot3(position, sv) + s.w - projection.texture_mins.x) / 16,
                 (qa_dot3(position, tv) + t.w - projection.texture_mins.y) / 16};
  return out;
}

static double byte_at(const uint8_t *values, size_t length, int64_t index) {
  if (index < 0 || (size_t)index >= length)
    qa_range_error("Lightmap sample %lld is outside %zu values",
                   (long long)index, length);
  return values[index];
}
static double style_at(const double *values, size_t length, int64_t index) {
  if (index < 0 || (size_t)index >= length)
    qa_range_error("Lightmap sample %lld is outside %zu values",
                   (long long)index, length);
  return values[index];
}

static int64_t face_size(qa_lightmap_face face) {
  if (face.width != trunc(face.width) || face.height != trunc(face.height) ||
      !(fabs(face.width) <= 9007199254740991.0) ||
      !(fabs(face.height) <= 9007199254740991.0) ||
      face.width < 1 || face.height < 1)
    qa_range_error("Lightmap dimensions must be positive integers");
  return (int64_t)(face.width * face.height);
}

static double sample(qa_lightmap_face face, int64_t style, int64_t pixel,
                     int channel) {
  if (face.lighting == NULL) return 255;
  int channels = face.lighting->is_rgb8 ? 3 : 1;
  int64_t index = (int64_t)face.offset +
      (style * (int64_t)(face.width * face.height) + pixel) * channels +
      (channels == 1 ? 0 : channel);
  return byte_at(face.lighting->samples.bytes, face.lighting->samples.length,
                 index);
}

/* One body for the Uint32 (Q1) and Float32 (Q2) accumulators; stores
 * reproduce typed-array conversion on assignment. */
static void add_dynamic(void *block, bool is_u32, qa_lightmap_face face,
                        const qa_surface_dynamic_light *lights, size_t count,
                        double scale) {
  for (size_t li = 0; li < count; li++) {
    qa_surface_dynamic_light light = lights[li];
    double distance = fr(qa_dot3(light.origin, face.plane.normal) -
                         face.plane.distance);
    double radius = fr(light.radius - fabs(distance));
    if (radius < light.minimum) continue;
    double threshold = radius - light.minimum;
    qa_vec3 impact = qa_sub3(light.origin,
                             qa_scale3(face.plane.normal, distance));
    qa_vec2 uv = qa_lightmap_coordinates(impact, face.projection);
    double step_s, step_t;
    if (kind_is(face.projection.kind, "classic")) {
      step_s = 16; step_t = 16;
    } else {
      step_s = 1 / qa_hypot3(face.projection.mapping.axes[0]);
      step_t = 1 / qa_hypot3(face.projection.mapping.axes[1]);
    }
    int64_t w = (int64_t)face.width, h = (int64_t)face.height;
    for (int64_t y = 0; y < h; y++) {
      for (int64_t x = 0; x < w; x++) {
        int64_t sd = llabs((int64_t)trunc((uv.x - (double)x) * step_s));
        int64_t td = llabs((int64_t)trunc((uv.y - (double)y) * step_t));
        int64_t separation = sd > td ? sd + (td >> 1) : td + (sd >> 1);
        if ((double)separation >= threshold) continue;
        double value = (radius - (double)separation) * scale;
        int64_t offset = (y * w + x) * 3;
        double add[3] = {value * light.color.x, value * light.color.y,
                         value * light.color.z};
        for (int c = 0; c < 3; c++) {
          if (is_u32) {
            uint32_t *b = block;
            b[offset + c] = qa_mat_to_uint32((double)b[offset + c] + add[c]);
          } else {
            float *b = block;
            b[offset + c] = fr((double)b[offset + c] + add[c]);
          }
        }
      }
    }
  }
}

void qa_built_lightmap_destroy(qa_built_lightmap *built) {
  free((void *)built->image.pixels.bytes);
  built->image.pixels.bytes = NULL;
  built->image.pixels.length = 0;
}

qa_built_lightmap qa_build_q1_lightmap(qa_lightmap_face face,
    const double *styles, size_t style_count, qa_build_q1_options options) {
  int64_t size = face_size(face);
  uint32_t *block = calloc((size_t)size * 3, sizeof *block);
  if (block == NULL) qa_range_error("lightmap allocation failed");
  const char *encoding = options.encoding != NULL ? options.encoding
      : (face.lighting != NULL && face.lighting->is_rgb8)
          ? "rgb" : "inverted-luminance";
  if (options.fullbright || face.lighting == NULL) {
    for (int64_t i = 0; i < size * 3; i++) block[i] = 255 * 256;
  } else {
    for (size_t map_index = 0; map_index < face.style_count; map_index++) {
      double style = face.styles[map_index];
      if (style == 255) break;
      double scale = style_at(styles, style_count, (int64_t)style);
      for (int64_t pixel = 0; pixel < size; pixel++) {
        for (int channel = 0; channel < 3; channel++) {
          int64_t offset = pixel * 3 + channel;
          block[offset] = qa_mat_to_uint32(
              (double)block[offset] +
              sample(face, (int64_t)map_index, pixel, channel) * scale);
        }
      }
    }
  }
  if (!options.fullbright && face.lighting != NULL)
    add_dynamic(block, true, face, options.dynamic_lights,
                options.dynamic_light_count, 256);
  uint8_t *pixels = malloc((size_t)size * 4);
  if (pixels == NULL) qa_range_error("lightmap allocation failed");
  for (int64_t pixel = 0; pixel < size; pixel++) {
    int brightness[3];
    for (int c = 0; c < 3; c++) {
      uint32_t v = block[pixel * 3 + c] >> 7;
      brightness[c] = v > 255 ? 255 : (int)v;
    }
    uint8_t *p = pixels + pixel * 4;
    if (kind_is(encoding, "rgb")) {
      p[0] = (uint8_t)brightness[0]; p[1] = (uint8_t)brightness[1];
      p[2] = (uint8_t)brightness[2]; p[3] = 255;
    } else if (kind_is(encoding, "inverted-luminance")) {
      uint8_t v = (uint8_t)(255 - brightness[0]);
      p[0] = v; p[1] = v; p[2] = v; p[3] = 255;
    } else {
      p[0] = 0; p[1] = 0; p[2] = 0; p[3] = (uint8_t)(255 - brightness[0]);
    }
  }
  free(block);
  qa_built_lightmap out = {{face.width, face.height,
      {pixels, (size_t)size * 4}}, encoding};
  return out;
}

qa_built_lightmap qa_build_q2_lightmap(qa_lightmap_face face,
    const qa_q2_light_style *styles, size_t style_count,
    qa_build_q2_options options) {
  int64_t size = face_size(face);
  float *block = calloc((size_t)size * 3, sizeof *block);
  if (block == NULL) qa_range_error("lightmap allocation failed");
  double modulate = options.has_modulate ? options.modulate : 1;
  const char *mono = options.mono != NULL ? options.mono : "0";
  if (face.lighting == NULL) {
    for (int64_t i = 0; i < size * 3; i++) block[i] = 255;
  } else {
    for (size_t map_index = 0; map_index < face.style_count; map_index++) {
      double style_index = face.styles[map_index];
      if (style_index == 255) break;
      if (style_index < 0 || (size_t)style_index >= style_count)
        qa_range_error("Missing Q2 lightstyle %lld", (long long)style_index);
      qa_q2_light_style style = styles[(size_t)style_index];
      float scales[3] = {fr(style.rgb.x * modulate),
                         fr(style.rgb.y * modulate),
                         fr(style.rgb.z * modulate)};
      for (int64_t pixel = 0; pixel < size; pixel++) {
        for (int channel = 0; channel < 3; channel++) {
          int64_t offset = pixel * 3 + channel;
          block[offset] = fr((double)block[offset] +
              fr(sample(face, (int64_t)map_index, pixel, channel) *
                 scales[channel]));
        }
      }
    }
  }
  if (face.lighting != NULL)
    add_dynamic(block, false, face, options.dynamic_lights,
                options.dynamic_light_count, 1);
  uint8_t *pixels = malloc((size_t)size * 4);
  if (pixels == NULL) qa_range_error("lightmap allocation failed");
  for (int64_t pixel = 0; pixel < size; pixel++) {
    double rd = trunc(block[pixel * 3]), gd = trunc(block[pixel * 3 + 1]),
           bd = trunc(block[pixel * 3 + 2]);
    int64_t r = rd > 0 ? (int64_t)rd : 0, g = gd > 0 ? (int64_t)gd : 0,
            b = bd > 0 ? (int64_t)bd : 0;
    int64_t a = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (a > 255) {
      float s = fr(255.0 / (double)a);
      r = (int64_t)trunc(fr((double)r * s));
      g = (int64_t)trunc(fr((double)g * s));
      b = (int64_t)trunc(fr((double)b * s));
      a = (int64_t)trunc(fr((double)a * s));
    }
    if (kind_is(mono, "L") || kind_is(mono, "I")) { r = a; g = 0; b = 0; }
    else if (kind_is(mono, "A")) { r = 0; g = 0; b = 0; a = 255 - a; }
    else if (kind_is(mono, "C")) {
      a = 255 - (r + g + b) / 3;
      r = (int64_t)trunc((double)r * ((double)a / 255));
      g = (int64_t)trunc((double)g * ((double)a / 255));
      b = (int64_t)trunc((double)b * ((double)a / 255));
    }
    uint8_t *p = pixels + pixel * 4;
    /* Uint8Array store wraps mod 256. */
    p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = (uint8_t)a;
  }
  free(block);
  qa_built_lightmap out = {{face.width, face.height,
      {pixels, (size_t)size * 4}}, kind_is(mono, "0") ? "q2-rgb" : "q2-mono"};
  return out;
}

qa_vec2 qa_lightmap_atlas_coordinates(qa_vec3 position,
    qa_lightmap_projection projection, qa_vec2 location,
    double atlas_width, double atlas_height) {
  qa_vec2 uv = qa_lightmap_coordinates(position, projection);
  qa_vec2 out = {(uv.x + location.x + 0.5) / atlas_width,
                 (uv.y + location.y + 0.5) / atlas_height};
  return out;
}

bool qa_sample_lightmap(qa_image_level image, qa_vec2 uv,
                        const char *filtering, qa_vec4 *out) {
  if (uv.x < 0 || uv.y < 0 || uv.x > image.width - 1 ||
      uv.y > image.height - 1)
    return false;
  int64_t x = (int64_t)floor(uv.x), y = (int64_t)floor(uv.y);
  int64_t w = (int64_t)image.width;
  double comps[4];
  for (int channel = 0; channel < 4; channel++) {
    if (!kind_is(filtering, "bilinear")) {
      comps[channel] = byte_at(image.pixels.bytes, image.pixels.length,
                               (y * w + x) * 4 + channel);
    } else {
      int64_t x1 = x + 1 < w - 1 ? x + 1 : w - 1;
      int64_t y1 = y + 1 < (int64_t)image.height - 1 ? y + 1
                                                     : (int64_t)image.height - 1;
      double fx = uv.x - (double)x, fy = uv.y - (double)y;
      double c00 = byte_at(image.pixels.bytes, image.pixels.length, (y * w + x) * 4 + channel);
      double c10 = byte_at(image.pixels.bytes, image.pixels.length, (y * w + x1) * 4 + channel);
      double c01 = byte_at(image.pixels.bytes, image.pixels.length, (y1 * w + x) * 4 + channel);
      double c11 = byte_at(image.pixels.bytes, image.pixels.length, (y1 * w + x1) * 4 + channel);
      comps[channel] = (c00 * (1 - fx) + c10 * fx) * (1 - fy) +
                       (c01 * (1 - fx) + c11 * fx) * fy;
    }
  }
  out->x = comps[0]; out->y = comps[1]; out->z = comps[2]; out->w = comps[3];
  return true;
}

static bool is_integer(double v) { return isfinite(v) && v == trunc(v); }

void qa_lightmap_atlas_init(qa_lightmap_atlas *atlas, double width,
                            double height) {
  if (!is_integer(width) || !is_integer(height) || width < 1 || height < 1)
    qa_range_error("Invalid lightmap atlas dimensions");
  atlas->width = width;
  atlas->height = height;
  atlas->heights = calloc((size_t)width, sizeof *atlas->heights);
  if (atlas->heights == NULL) qa_range_error("lightmap allocation failed");
}
void qa_lightmap_atlas_destroy(qa_lightmap_atlas *atlas) {
  free(atlas->heights);
  atlas->heights = NULL;
}
void qa_lightmap_atlas_clear(qa_lightmap_atlas *atlas) {
  memset(atlas->heights, 0, (size_t)atlas->width * sizeof *atlas->heights);
}
bool qa_lightmap_atlas_allocate(qa_lightmap_atlas *atlas, double width,
                                double height, qa_vec2 *out) {
  if (!is_integer(width) || !is_integer(height) || width < 1 || height < 1)
    qa_range_error("Invalid lightmap rectangle");
  int64_t aw = (int64_t)atlas->width, rw = (int64_t)width;
  int64_t best = (int64_t)atlas->height, location = -1;
  for (int64_t x = 0; x <= aw - rw; x++) {
    int64_t top = 0;
    bool accepted = true;
    for (int64_t column = 0; column < rw; column++) {
      int32_t value = atlas->heights[x + column];
      if (value >= best) { accepted = false; break; }
      if (value > top) top = value;
    }
    if (accepted) { best = top; location = x; }
  }
  if (location < 0 || best + (int64_t)height > (int64_t)atlas->height)
    return false;
  for (int64_t column = 0; column < rw; column++)
    atlas->heights[location + column] = (int32_t)(best + (int64_t)height);
  out->x = (double)location;
  out->y = (double)best;
  return true;
}

qa_image_level qa_direct_lightmap_pixels(qa_built_lightmap built) {
  if (kind_is(built.encoding, "q2-mono"))
    qa_range_error("Q2 alternate monochrome encodings require their matching texture environment");
  size_t length = built.image.pixels.length;
  uint8_t *pixels = malloc(length > 0 ? length : 1);
  if (pixels == NULL) qa_range_error("lightmap allocation failed");
  if (length > 0) memcpy(pixels, built.image.pixels.bytes, length);
  for (size_t offset = 0; offset + 3 < length; offset += 4) {
    if (kind_is(built.encoding, "inverted-alpha")) {
      uint8_t value = (uint8_t)(255 - pixels[offset + 3]);
      pixels[offset] = value; pixels[offset + 1] = value;
      pixels[offset + 2] = value;
    } else if (kind_is(built.encoding, "inverted-luminance")) {
      for (int channel = 0; channel < 3; channel++)
        pixels[offset + channel] = (uint8_t)(255 - pixels[offset + channel]);
    }
    pixels[offset + 3] = 255;
  }
  qa_image_level out = {built.image.width, built.image.height,
                        {pixels, length}};
  return out;
}
