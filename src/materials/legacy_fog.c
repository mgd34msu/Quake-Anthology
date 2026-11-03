/* Port of src/materials/legacy-fog.ts. GPL-2.0-or-later. */
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/materials/legacy_fog.h"
#include "qa/foundation/js.h"

static double clamp01(double value) {
  return value < 0 ? 0 : value > 1 ? 1 : value;
}
static qa_vec3 mix3(qa_vec3 a, qa_vec3 b, double fraction) {
  qa_vec3 out = {a.x + (b.x - a.x) * fraction, a.y + (b.y - a.y) * fraction,
                 a.z + (b.z - a.z) * fraction};
  return out;
}

void qa_q1_fog_state_init(qa_q1_fog_state *state) {
  state->previous = (qa_q1_fog){0, {0.3, 0.3, 0.3}};
  state->target = state->previous;
  state->start = 0;
  state->duration = 0;
}
void qa_q1_fog_state_update(qa_q1_fog_state *state, qa_q1_fog value,
                            double time, double duration) {
  state->previous = qa_q1_fog_state_sample(state, time);
  state->target = (qa_q1_fog){value.density > 0 ? value.density : 0,
      {clamp01(value.color.x), clamp01(value.color.y), clamp01(value.color.z)}};
  state->start = time;
  state->duration = duration > 0 ? duration : 0;
}
qa_q1_fog_transition qa_q1_fog_state_capture(const qa_q1_fog_state *state) {
  qa_q1_fog_transition out = {state->previous, state->target, state->start,
                              state->duration};
  return out;
}
void qa_q1_fog_state_install(qa_q1_fog_state *state,
                             qa_q1_fog_transition value) {
  state->previous = value.previous;
  state->target = value.target;
  state->start = value.start;
  state->duration = value.duration;
}
qa_q1_fog qa_q1_fog_state_sample(const qa_q1_fog_state *state, double time) {
  double fraction = state->duration == 0
      ? 1 : clamp01((time - state->start) / state->duration);
  qa_q1_fog out = {state->previous.density +
                       (state->target.density - state->previous.density) * fraction,
                   mix3(state->previous.color, state->target.color, fraction)};
  return out;
}

/* JS Number(token) via strtod over a trimmed token; empty token is 0 in
 * JS but split of a trimmed string yields "" only for empty input. */
static double js_number(const char *start, size_t length) {
  char *copy = malloc(length + 1);
  if (copy == NULL) return NAN;
  memcpy(copy, start, length);
  copy[length] = '\0';
  double value = qa_js_string_to_number(copy);
  free(copy);
  return value;
}

qa_q1_fog_transition qa_q1_world_fog(const char *entities) {
  qa_q1_fog_state state;
  qa_q1_fog_state_init(&state);
  size_t count = 0;
  qa_q1_entity *parsed = qa_q1_parse_entities(entities, &count);
  if (count > 0) {
    const qa_q1_entity *world = &parsed[0];
    for (size_t i = 0; i < world->property_count; i++) {
      const char *key = world->properties[i].key;
      if (key[0] == '_') key++;
      /* trimEnd then compare to "fog" */
      size_t length = strlen(key);
      while (length > 0 && isspace((unsigned char)key[length - 1])) length--;
      if (length != 3 || strncmp(key, "fog", 3) != 0) continue;
      double defaults[4] = {0, 0.3, 0.3, 0.3};
      const char *p = world->properties[i].value;
      int index = 0;
      while (index < 4) {
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;
        const char *start = p;
        while (*p != '\0' && !isspace((unsigned char)*p)) p++;
        double value = js_number(start, (size_t)(p - start));
        if (!isfinite(value)) break;
        defaults[index++] = value;
      }
      qa_q1_fog fog = {defaults[0], {defaults[1], defaults[2], defaults[3]}};
      qa_q1_fog_state_update(&state, fog, 0, 0);
    }
  }
  qa_q1_entities_destroy(parsed, count);
  return qa_q1_fog_state_capture(&state);
}

double qa_global_fog_amount(double density_scaled, double frag_depth) {
  double d = density_scaled * frag_depth;
  return 1 - exp(-(d * d));
}
double qa_height_fog_fraction(double world_z, double start, double end) {
  if (end == start) return 0;
  return clamp01((world_z - start - start) / (end - start));
}
double qa_height_fog_dir_z(double direction_z) {
  double sign = qa_js_sign(direction_z);
  return direction_z + 0.00001 * (1 - sign * sign);
}
double qa_height_fog_extinction(double view_z, double world_z, double start,
                                double falloff, double direction_z) {
  double density = (exp(-falloff * (view_z - start)) -
                    exp(-falloff * (world_z - start))) /
                   (falloff * direction_z);
  return 1 - clamp01(exp(-density));
}
double qa_height_fog_amount(double density, double depth, double extinction) {
  return (1 - exp(-(density * depth))) * extinction;
}
qa_vec3 qa_q2_fog_color(qa_vec3 color, qa_scene_fog fog, qa_vec3 point,
                        qa_vec3 view, double depth, bool sky) {
  qa_vec3 result = mix3(color, fog.color, qa_global_fog_amount(fog.density / 64, depth));
  if (fog.height.density > 0 && fog.height.falloff > 0) {
    qa_vec3 delta = {point.x - view.x, point.y - view.y, point.z - view.z};
    double distance = qa_hypot3(delta);
    double direction_z = qa_height_fog_dir_z(
        distance == 0 ? 0 : (point.z - view.z) / distance);
    double extinction = qa_height_fog_extinction(view.z, point.z,
        fog.height.start.distance, fog.height.falloff, direction_z);
    double amount = qa_height_fog_amount(fog.height.density, depth, extinction);
    qa_vec3 height_color = mix3(fog.height.start.color, fog.height.end.color,
        qa_height_fog_fraction(point.z, fog.height.start.distance,
                               fog.height.end.distance));
    qa_vec3 scaled = {height_color.x * extinction, height_color.y * extinction,
                      height_color.z * extinction};
    result = mix3(result, scaled, amount);
  }
  if (sky) result = mix3(result, fog.color, fog.sky_factor);
  return result;
}
qa_vec3 qa_q1_fog_color(qa_vec3 color, qa_q1_fog fog, double depth) {
  return mix3(color, fog.color, qa_global_fog_amount(fog.density / 64, depth));
}
