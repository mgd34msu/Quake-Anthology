/* Port of src/render/scene/models/lighting.ts. GPL-2.0-or-later. */
#include "qa/render/scene/models/lighting.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/numeric.h"

static double fr(double value) { return (double)(float)value; }
static bool bit(double flags, int32_t mask) {
  return (qa_int32(flags) & mask) != 0;
}

bool qa_q2_shell_color(double flags, qa_vec3 *out) {
  if ((qa_int32(flags) & QA_Q2_SHELL_MASK) == 0) return false;
  bool red = bit(flags, 1024), green = bit(flags, 2048), blue = bit(flags, 4096);
  bool dbl = bit(flags, 65536), half = bit(flags, 131072);
  if (red && green && blue) *out = (qa_vec3){1, 1, 1};
  else if (red) *out = (qa_vec3){1, 0, (blue || dbl) ? 1 : 0};
  else if (blue) *out = (qa_vec3){0, dbl ? 1 : 0, 1};
  else if (dbl) *out = (qa_vec3){0.9, 0.7, 0};
  else
    *out = (qa_vec3){half ? 0.56 : 0, green ? 1 : half ? 0.59 : 0,
                     half ? 0.45 : 0};
  return true;
}

qa_vec3 qa_q2_alias_light(double flags, qa_vec3 sampled, double time_seconds,
                          bool monochrome, bool infrared) {
  qa_vec3 shell;
  bool has_shell = qa_q2_shell_color(flags, &shell);
  qa_vec3 light = has_shell ? shell
                  : bit(flags, 8) ? (qa_vec3){1, 1, 1}
                                  : sampled;
  if (!has_shell && !bit(flags, 8) && monochrome) {
    double value = fmax(light.x, fmax(light.y, light.z));
    light = (qa_vec3){value, value, value};
  }
  if (bit(flags, 1) && light.x <= 0.1 && light.y <= 0.1 && light.z <= 0.1)
    light = (qa_vec3){0.1, 0.1, 0.1};
  if (bit(flags, 512)) {
    double pulse = 0.1 * sin(time_seconds * 7);
    light = (qa_vec3){fmax(light.x * 0.8, light.x + pulse),
                      fmax(light.y * 0.8, light.y + pulse),
                      fmax(light.z * 0.8, light.z + pulse)};
  }
  return (infrared && bit(flags, 32768)) ? (qa_vec3){1, 0, 0} : light;
}

qa_vec3 qa_alias_shadow_point(qa_vec3 point, qa_vec3 shade_vector,
                              double entity_height, double floor_height) {
  double height = entity_height - floor_height;
  return (qa_vec3){point.x - shade_vector.x * (point.z + height),
                   point.y - shade_vector.y * (point.z + height),
                   -height + 1};
}

qa_vec3 qa_q1_alias_shadow_point(qa_vec3 point, qa_vec3 shade_vector,
                                 double entity_height, double floor_height) {
  double height = fr(entity_height - floor_height);
  double elevation = fr(point.z + height);
  return (qa_vec3){fr(point.x - fr(shade_vector.x * elevation)),
                   fr(point.y - fr(shade_vector.y * elevation)),
                   fr(-height + 1)};
}

qa_vec3 qa_q1_alias_shadow_direction(double yaw) {
  double angle = fr(yaw), x = fr(cos(-angle)), y = fr(sin(-angle));
  double length = fr(sqrt(fr(fr(fr(x * x) + fr(y * y)) + 1)));
  double inverse = fr(1 / length);
  return (qa_vec3){fr(x * inverse), fr(y * inverse), inverse};
}

static double share_of(double contribution, double channel) {
  return (channel > 0 && contribution > 0)
             ? fmin(1, contribution / channel)
             : 0;
}

qa_q2_model_shadow_light *qa_alias_shadow_light_fractions(
    qa_vec3 origin, qa_vec3 shade, const qa_q2_fragment_light *lights,
    size_t light_count, double modulate, bool monochrome, size_t *out_count) {
  *out_count = 0;
  qa_q2_model_shadow_light *result = NULL;
  size_t capacity = 0;
  for (size_t i = 0; i < light_count; i++) {
    const qa_q2_fragment_light *light = &lights[i];
    if (strcmp(light->shadow.kind, "none") == 0) continue;
    double dx = origin.x - light->origin.x, dy = origin.y - light->origin.y,
           dz = origin.z - light->origin.z;
    double amount =
        (light->radius - sqrt(dx * dx + dy * dy + dz * dz)) / 256 * modulate;
    if (amount <= 0) continue;
    double r = amount * light->color.x, g = amount * light->color.y,
           b = amount * light->color.z;
    if (monochrome) r = g = b = fmax(r, fmax(g, b));
    qa_vec3 fraction = {fr(share_of(r, shade.x)), fr(share_of(g, shade.y)),
                        fr(share_of(b, shade.z))};
    if (fraction.x <= 0 && fraction.y <= 0 && fraction.z <= 0) continue;
    if (*out_count == capacity) {
      capacity = capacity ? capacity * 2 : 4;
      result = realloc(result, capacity * sizeof *result);
      if (result == NULL) { *out_count = 0; return NULL; }
    }
    result[*out_count] = (qa_q2_model_shadow_light){
        light->origin, light->radius, fraction, light->shadow};
    (*out_count)++;
  }
  return result;
}

double qa_alias_shade_divisor(qa_vec3 shade) {
  return fmax(1, fmax(shade.x, fmax(shade.y, shade.z)) * 2);
}
