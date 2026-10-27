/* Port of src/content/q2/base/player/landmarks.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/player/landmarks.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "qa/content/q2/foundation/fields.h"

typedef struct side { int axis; double sign; } side; /* axis 0=x,1=y,2=z */
static const side SIDES[] = {{2, 1}, {2, -1}, {0, 1}, {0, -1}, {1, 1}, {1, -1}};
static const int AXES[] = {0, 1, 2};

static double axis_get(qa_vec3 v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }
static qa_vec3 axis_set(qa_vec3 v, int axis, double value) {
  if (axis == 0) v.x = value;
  else if (axis == 1) v.y = value;
  else v.z = value;
  return v;
}

qa_vec3 qa_q2_rotate_landmark(qa_vec3 vector, qa_vec3 angles) {
  double pitch = angles.x * M_PI / 180, roll = angles.z * M_PI / 180, yaw = angles.y * M_PI / 180;
  qa_vec3 x = {vector.x, vector.y * cos(pitch) - vector.z * sin(pitch), vector.y * sin(pitch) + vector.z * cos(pitch)};
  qa_vec3 y = {x.x * cos(roll) + x.z * sin(roll), x.y, -x.x * sin(roll) + x.z * cos(roll)};
  return (qa_vec3){y.x * cos(yaw) - y.y * sin(yaw), y.x * sin(yaw) + y.y * cos(yaw), y.z};
}

static qa_trace_result trace_at(qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 start, const qa_bounds *shape, qa_vec3 end) {
  qa_q2_trace_request request = {0};
  request.start = start; request.end = end; request.bounds = shape;
  request.ignore = &entity->actor.id; request.mask = 0x2010003;
  return game->host->trace(game->host->ctx, request);
}

typedef struct candidate { qa_vec3 origin; double distance; } candidate;

/** Source p_move.cpp face probes, including its final-unsorted-candidate quirk. */
bool qa_q2_fix_stuck_player(qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 origin, qa_bounds bounds, qa_vec3 *out) {
  if (!trace_at(entity, game, origin, &bounds, origin).start_solid) { *out = origin; return true; }
  candidate good[6];
  size_t good_count = 0;
  for (size_t s = 0; s < sizeof SIDES / sizeof SIDES[0]; s++) {
    int axis = SIDES[s].axis;
    double sign = SIDES[s].sign;
    double component = sign < 0 ? axis_get(bounds.min, axis) : axis_get(bounds.max, axis);
    qa_vec3 start = axis_set(origin, axis, axis_get(origin, axis) + component);
    qa_bounds face = {axis_set(bounds.min, axis, 0), axis_set(bounds.max, axis, 0)};
    qa_trace_result hit = trace_at(entity, game, start, &face, start);
    bool has_epsilon = false;
    int epsilon_axis = 0;
    double epsilon_distance = 0;
    if (hit.start_solid) {
      for (size_t a = 0; a < sizeof AXES / sizeof AXES[0]; a++) {
        int other = AXES[a];
        if (other == axis) continue;
        qa_vec3 positive = axis_set(start, other, axis_get(start, other) + 1);
        qa_trace_result first = trace_at(entity, game, positive, &face, positive);
        if (!first.start_solid) { start = positive; hit = first; has_epsilon = true; epsilon_axis = other; epsilon_distance = 1; break; }
        qa_vec3 negative = axis_set(start, other, axis_get(start, other) - 1);
        qa_trace_result second = trace_at(entity, game, negative, &face, negative);
        if (!second.start_solid) { start = negative; hit = second; has_epsilon = true; epsilon_axis = other; epsilon_distance = -1; break; }
      }
    }
    if (hit.start_solid) continue;
    double opposite_component = sign < 0 ? axis_get(bounds.max, axis) : axis_get(bounds.min, axis);
    qa_vec3 opposite = axis_set(origin, axis, axis_get(origin, axis) + opposite_component);
    if (has_epsilon) opposite = axis_set(opposite, epsilon_axis, axis_get(opposite, epsilon_axis) + epsilon_distance);
    hit = trace_at(entity, game, start, &face, opposite);
    if (hit.start_solid) continue;
    qa_vec3 normal = axis_set(QA_Q2_ZERO, axis, sign);
    qa_vec3 delta = qa_q2_subtract(qa_q2_add(hit.end, qa_q2_scale(normal, 0.125)), opposite);
    qa_vec3 position = qa_q2_add(origin, delta);
    if (has_epsilon) position = axis_set(position, epsilon_axis, axis_get(position, epsilon_axis) + epsilon_distance);
    if (!trace_at(entity, game, position, &bounds, position).start_solid)
      good[good_count++] = (candidate){position, qa_q2_dot(delta, delta)};
  }
  if (good_count > 1) {
    candidate last = good[--good_count];
    for (size_t i = 1; i < good_count; i++) {
      candidate key = good[i];
      size_t j = i;
      while (j > 0 && good[j - 1].distance > key.distance) { good[j] = good[j - 1]; j--; }
      good[j] = key;
    }
    good[good_count++] = last;
  }
  if (good_count == 0) return false;
  *out = good[0].origin;
  return true;
}

bool qa_q2_place_landmark(qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_landmark_carry carry,
                          qa_q2_entity *spawn, qa_bounds bounds, qa_q2_landmark_placement *out) {
  if (carry.name[0] == '\0') return false;
  qa_q2_entity *landmark = game->pick_target(game->ctx, carry.name);
  if (!landmark) return false;
  qa_body_state reference = qa_q2_game_body(game, landmark);
  qa_vec3 origin = qa_q2_add(qa_q2_rotate_landmark(carry.relative_origin, reference.angles), reference.origin);
  /* This is source bit 0, also authored by the shipped rerelease BSPs. */
  if (((int64_t)landmark->spawnflags & 1) != 0) origin.z = qa_q2_game_body(game, spawn).origin.z;
  qa_vec3 clear;
  if (!qa_q2_fix_stuck_player(entity, game, origin, bounds, &clear)) return false;
  out->origin = clear;
  out->velocity = qa_q2_rotate_landmark(carry.relative_velocity, reference.angles);
  out->angles = qa_q2_add(carry.relative_view_angles, reference.angles);
  return true;
}
