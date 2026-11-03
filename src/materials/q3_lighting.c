/* Port of src/materials/q3-lighting.ts. GPL-2.0-or-later. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

#include "qa/core/math.h"
#include "qa/core/renderer_math.h"
#include "qa/foundation/support.h"
#include "qa/materials/q3_lighting.h"
#include "qa/materials/shader_types.h"

static float fr(double v) { return (float)v; }
#define RF_FIRST_PERSON_FLAG 4

void qa_light_grid_destroy(qa_light_grid *grid) {
  free(grid->samples);
  grid->samples = NULL;
  grid->sample_count = 0;
}

static qa_vec3 shifted_color(qa_vec3 color, int shift) {
  int32_t r = qa_mat_to_int32(color.x) << shift;
  int32_t g = qa_mat_to_int32(color.y) << shift;
  int32_t b = qa_mat_to_int32(color.z) << shift;
  if ((r | g | b) <= 255) {
    qa_vec3 out = {(double)r, (double)g, (double)b};
    return out;
  }
  double maximum = r > g ? (r > b ? r : b) : (g > b ? g : b);
  qa_vec3 out = {trunc(r * 255 / maximum), trunc(g * 255 / maximum),
                 trunc(b * 255 / maximum)};
  return out;
}

/* gridFloat: TS parses a pre-matched numeric token. */
static double grid_float(const char *token, size_t length) {
  char *copy = malloc(length + 1);
  if (copy == NULL) qa_range_error("light grid allocation failed");
  for (size_t i = 0; i < length; i++)
    copy[i] = (char)tolower((unsigned char)token[i]);
  copy[length] = '\0';
  /* token may carry leading whitespace from the sscanf-style match */
  char *p = copy;
  while (isspace((unsigned char)*p)) p++;
  bool hex = (p[0] == '+' || p[0] == '-' ? p[1] == '0' && p[2] == 'x'
                                         : p[0] == '0' && p[1] == 'x');
  double result;
  if (!hex) {
    result = fr(qa_js_parse_float(p)); /* parseFloat of the lowercased token: "inf" and "infinity" are NaN */
  } else {
    bool negative = p[0] == '-';
    char *mantissa = p + ((p[0] == '+' || p[0] == '-') ? 3 : 2);
    char *exp_at = strchr(mantissa, 'p');
    double exponent = exp_at != NULL ? qa_js_parse_int(exp_at + 1, 10) : 0;
    if (exp_at != NULL) *exp_at = '\0';
    char *dot = strchr(mantissa, '.');
    double fractional = dot != NULL ? (double)strlen(dot + 1) : 0;
    if (dot != NULL) memmove(dot, dot + 1, strlen(dot + 1) + 1);
    double digits = qa_js_parse_int(mantissa, 16); /* strtoull saturated past 2^64 */
    result = fr((negative ? -1.0 : 1.0) * digits * qa_js_pow(2.0, exponent - fractional * 4));
  }
  free(copy);
  return result;
}

/* Scan one sscanf-style float token at *cursor; returns token length
 * (including leading whitespace) or 0 for no match. */
static size_t scan_float_token(const char *text) {
  size_t i = 0;
  while (isspace((unsigned char)text[i])) i++;
  size_t start = i;
  if (text[i] == '+' || text[i] == '-') i++;
  /* inf / infinity / nan, case-insensitive */
  const char *words[] = {"infinity", "inf", "nan"};
  for (int w = 0; w < 3; w++) {
    size_t len = strlen(words[w]);
    bool match = true;
    for (size_t k = 0; k < len; k++) {
      if (tolower((unsigned char)text[i + k]) != words[w][k]) {
        match = false;
        break;
      }
    }
    if (match) return i + len;
  }
  if (text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
    size_t j = i + 2;
    bool any = false, dot = false;
    while (isxdigit((unsigned char)text[j])) { j++; any = true; }
    if (text[j] == '.') {
      dot = true;
      j++;
      while (isxdigit((unsigned char)text[j])) { j++; any = true; }
    }
    if (!any || (!dot && j == i + 2)) return 0;
    if (text[j] == 'p' || text[j] == 'P') {
      size_t k = j + 1;
      if (text[k] == '+' || text[k] == '-') k++;
      if (isdigit((unsigned char)text[k])) {
        while (isdigit((unsigned char)text[k])) k++;
        j = k;
      }
    }
    (void)start;
    return j;
  }
  size_t j = i;
  bool any = false;
  while (isdigit((unsigned char)text[j])) { j++; any = true; }
  if (text[j] == '.') {
    j++;
    while (isdigit((unsigned char)text[j])) { j++; any = true; }
  }
  if (!any) return 0;
  if (text[j] == 'e' || text[j] == 'E') {
    size_t k = j + 1;
    if (text[k] == '+' || text[k] == '-') k++;
    if (isdigit((unsigned char)text[k])) {
      while (isdigit((unsigned char)text[k])) k++;
      j = k;
    }
  }
  return j;
}

static bool key_eq_gridsize(const char *key) {
  const char *want = "gridsize";
  for (int i = 0; i < 8; i++)
    if (tolower((unsigned char)key[i]) != want[i]) return false;
  return key[8] == '\0';
}

static qa_vec3 world_grid_size(qa_lighting_map map) {
  qa_vec3 size = {64, 64, 128};
  if (map.entity_record_count == 0) return size;
  qa_lighting_map_record worldspawn = map.entity_records[0];
  for (size_t e = 0; e < worldspawn.entry_count; e++) {
    if (!key_eq_gridsize(worldspawn.entries[e].key)) continue;
    const char *rest = worldspawn.entries[e].value;
    double *axes[3] = {&size.x, &size.y, &size.z};
    for (int axis = 0; axis < 3; axis++) {
      size_t length = scan_float_token(rest);
      if (length == 0) break;
      *axes[axis] = grid_float(rest, length);
      rest += length;
    }
  }
  return size;
}

struct axis_layout { double origin, count; };
static struct axis_layout grid_axis(double minimum, double maximum,
                                    double size) {
  double origin = fr(size * ceil(fr(minimum / size)));
  double end = fr(size * floor(fr(maximum / size)));
  struct axis_layout out = {origin, trunc(fr(fr(fr(end - origin) / size) + 1))};
  return out;
}

static bool safe_positive_integer(double v) {
  return isfinite(v) && v == trunc(v) && fabs(v) <= 9007199254740991.0 && v > 0;
}
static bool is_integer_value(double v) {
  return isfinite(v) && v == trunc(v);
}

qa_prepare_light_grid_result qa_prepare_light_grid(qa_lighting_map map,
    double map_overbright_bits, double overbright_bits) {
  qa_prepare_light_grid_result result;
  memset(&result, 0, sizeof result);
  double shift_d = map_overbright_bits - overbright_bits;
  if (!is_integer_value(map_overbright_bits) ||
      !is_integer_value(overbright_bits) || shift_d < 0 || shift_d > 15)
    qa_range_error("Light-grid overbright shift must be an integer in 0..15; other values have undefined source C shifts or overflow");
  int shift = (int)shift_d;
  if (map.model_count == 0)
    qa_range_error("Light-grid loading requires BSP world model zero");
  qa_bounds world = map.model_bounds[0];
  qa_vec3 size = world_grid_size(map);
  if (!(isfinite(size.x) && size.x > 0) || !(isfinite(size.y) && size.y > 0) ||
      !(isfinite(size.z) && size.z > 0)) {
    snprintf(result.diagnostic, sizeof result.diagnostic,
             "Invalid worldspawn gridsize: dimensions must be finite and positive");
    return result;
  }
  struct axis_layout x = grid_axis(world.min.x, world.max.x, size.x);
  struct axis_layout y = grid_axis(world.min.y, world.max.y, size.y);
  struct axis_layout z = grid_axis(world.min.z, world.max.z, size.z);
  double count = x.count * y.count * z.count;
  if (!safe_positive_integer(x.count) || !safe_positive_integer(y.count) ||
      !safe_positive_integer(z.count) ||
      !(isfinite(count) && count == trunc(count) &&
        fabs(count) <= 9007199254740991.0)) {
    snprintf(result.diagnostic, sizeof result.diagnostic,
             "Invalid light-grid bounds: no finite positive sample layout");
    return result;
  }
  if ((double)map.light_grid_count != count) {
    snprintf(result.diagnostic, sizeof result.diagnostic,
             "Light grid mismatch: expected %lld samples, found %zu",
             (long long)count, map.light_grid_count);
    return result;
  }
  qa_q3_light_grid_point *samples = malloc((size_t)count * sizeof *samples);
  if (samples == NULL) qa_range_error("light grid allocation failed");
  for (size_t i = 0; i < (size_t)count; i++) {
    samples[i].ambient = shifted_color(map.light_grid[i].ambient, shift);
    samples[i].directed = shifted_color(map.light_grid[i].directed, shift);
    samples[i].lat_long = map.light_grid[i].lat_long;
  }
  result.has_grid = true;
  result.grid.origin = (qa_vec3){x.origin, y.origin, z.origin};
  result.grid.size = size;
  result.grid.inverse_size = (qa_vec3){1 / size.x, 1 / size.y, 1 / size.z};
  result.grid.bounds = (qa_vec3){x.count, y.count, z.count};
  result.grid.samples = samples;
  result.grid.sample_count = (size_t)count;
  return result;
}

struct axis_sample { double position, fraction; };
static struct axis_sample sample_axis(double coordinate, double inverse_size,
                                      double count) {
  double v = fr(coordinate * inverse_size);
  double position = floor(v);
  double clamped = position < 0 ? 0 : position > count - 1 ? count - 1 : position;
  struct axis_sample out = {clamped, fr(v - position)};
  return out;
}

bool qa_light_for_point(const qa_light_grid *grid, qa_vec3 point,
                        qa_lighting_scales scales, qa_lighting_sample *out) {
  if (grid == NULL) return false;
  if (!isfinite(point.x) || !isfinite(point.y) || !isfinite(point.z))
    qa_range_error("Light sample origin must be finite");
  qa_vec3 relative = qa_sub3(point, grid->origin);
  struct axis_sample x = sample_axis(relative.x, grid->inverse_size.x, grid->bounds.x);
  struct axis_sample y = sample_axis(relative.y, grid->inverse_size.y, grid->bounds.y);
  struct axis_sample z = sample_axis(relative.z, grid->inverse_size.z, grid->bounds.z);
  qa_vec3 ambient = {0, 0, 0}, directed = {0, 0, 0}, direction = {0, 0, 0};
  double total_factor = 0;
  for (int i = 0; i < 8; i++) {
    double factor = (i & 1) != 0 ? x.fraction : fr(1 - x.fraction);
    factor = fr(factor * ((i & 2) != 0 ? y.fraction : fr(1 - y.fraction)));
    factor = fr(factor * ((i & 4) != 0 ? z.fraction : fr(1 - z.fraction)));
    double sx = x.position + (i & 1), sy = y.position + ((i >> 1) & 1),
           sz = z.position + ((i >> 2) & 1);
    double index = sx + grid->bounds.x * (sy + grid->bounds.y * sz);
    if (index >= (double)grid->sample_count) {
      double cx = sx < grid->bounds.x - 1 ? sx : grid->bounds.x - 1;
      double cy = sy < grid->bounds.y - 1 ? sy : grid->bounds.y - 1;
      double cz = sz < grid->bounds.z - 1 ? sz : grid->bounds.z - 1;
      index = cx + grid->bounds.x * (cy + grid->bounds.y * cz);
    }
    if (index < 0 || (size_t)index >= grid->sample_count)
      qa_range_error("Prepared light-grid sample layout is inconsistent");
    qa_q3_light_grid_point data = grid->samples[(size_t)index];
    if (data.ambient.x + data.ambient.y + data.ambient.z == 0) continue;
    total_factor = fr(total_factor + factor);
    ambient = qa_add3(ambient, qa_scale3(data.ambient, factor));
    directed = qa_add3(directed, qa_scale3(data.directed, factor));
    double latitude = data.lat_long.y * 4, longitude = data.lat_long.x * 4;
    float left = qa_renderer_sine(latitude + 256);
    float right = qa_renderer_sine(longitude);
    double x2 = left * right;
    float left2 = qa_renderer_sine(latitude);
    float right2 = qa_renderer_sine(longitude);
    double y2 = left2 * right2;
    double z2 = qa_renderer_sine(longitude + 256);
    qa_vec3 normal = {x2, y2, z2};
    direction = qa_add3(direction, qa_scale3(normal, factor));
  }
  if (total_factor > 0 && total_factor < 0.99) {
    float inverse = fr(1.0 / total_factor);
    ambient = qa_scale3(ambient, inverse);
    directed = qa_scale3(directed, inverse);
  }
  out->ambient_light = qa_scale3(ambient, fr(scales.ambient_scale));
  out->directed_light = qa_scale3(directed, fr(scales.directed_scale));
  out->light_dir = qa_normalize3_or_zero(direction);
  return true;
}

qa_entity_lighting qa_setup_entity_lighting(qa_lighting_entity entity,
    qa_entity_lighting_state state, qa_entity_lighting *output,
    const qa_entity_lighting_diagnostics *diagnostics) {
  qa_entity_lighting local = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 0};
  if (output == NULL) output = &local;
  int32_t flags = qa_mat_to_int32(entity.render_flags);
  qa_vec3 origin = (flags & QA_RF_LIGHTING_ORIGIN) != 0
      ? entity.lighting_origin : entity.origin;
  double fallback = fr(fr(state.identity_light) * 150.0);
  qa_lighting_sample sample;
  qa_lighting_scales scales = {state.ambient_scale, state.directed_scale};
  bool have = !state.no_world_model &&
              qa_light_for_point(state.grid, origin, scales, &sample);
  if (!have) {
    sample.ambient_light = (qa_vec3){fallback, fallback, fallback};
    sample.directed_light = (qa_vec3){fallback, fallback, fallback};
    sample.light_dir = state.sun_direction;
  }
  double minimum = fr(fr(state.identity_light) * 32.0);
  qa_vec3 ambient = qa_add3(sample.ambient_light,
                            (qa_vec3){minimum, minimum, minimum});
  qa_vec3 directed = sample.directed_light;
  qa_vec3 direction = qa_scale3(sample.light_dir, qa_length3(directed));
  for (size_t i = 0; i < state.dynamic_light_count; i++) {
    qa_q3_dynamic_light light = state.dynamic_lights[i];
    qa_vec3 relative = qa_sub3(light.origin, origin);
    double length = qa_length3(relative);
    double distance = length > 16 ? length : 16;
    double power = fr(16.0 * fr(fr(light.radius) * fr(light.radius)));
    double attenuation = fr(power / fr(distance * distance));
    directed = qa_add3(directed, qa_scale3(light.color, attenuation));
    direction = qa_add3(direction,
                        qa_scale3(qa_normalize3(relative), attenuation));
  }
  ambient = (qa_vec3){
      ambient.x < state.identity_light_byte ? ambient.x : state.identity_light_byte,
      ambient.y < state.identity_light_byte ? ambient.y : state.identity_light_byte,
      ambient.z < state.identity_light_byte ? ambient.z : state.identity_light_byte};
  output->light_dir = sample.light_dir;
  output->ambient_light = ambient;
  output->directed_light = directed;
  if (diagnostics != NULL && diagnostics->enabled &&
      (flags & RF_FIRST_PERSON_FLAG) != 0) {
    int64_t max1 = (int64_t)trunc(output->ambient_light.x);
    int64_t max2 = (int64_t)trunc(output->directed_light.x);
    if (output->ambient_light.y > (double)max1)
      max1 = (int64_t)trunc(output->ambient_light.y);
    else if (output->ambient_light.z > (double)max1)
      max1 = (int64_t)trunc(output->ambient_light.z);
    if (output->directed_light.y > (double)max2)
      max2 = (int64_t)trunc(output->directed_light.y);
    else if (output->directed_light.z > (double)max2)
      max2 = (int64_t)trunc(output->directed_light.z);
    char text[64];
    snprintf(text, sizeof text, "amb:%lld  dir:%lld\n",
             (long long)max1, (long long)max2);
    diagnostics->print(diagnostics->ctx, text);
  }
  output->ambient_light_int =
      (uint32_t)(qa_mat_to_int32(output->ambient_light.x) & 255) |
      ((uint32_t)(qa_mat_to_int32(output->ambient_light.y) & 255) << 8) |
      ((uint32_t)(qa_mat_to_int32(output->ambient_light.z) & 255) << 16) |
      0xff000000u;
  direction = qa_normalize3(direction);
  output->light_dir = (qa_vec3){
      qa_dot3(direction, entity.axis.vectors[0]),
      qa_dot3(direction, entity.axis.vectors[1]),
      qa_dot3(direction, entity.axis.vectors[2])};
  return *output;
}
