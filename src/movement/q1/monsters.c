/* Port of src/movement/q1/monsters.ts. GPL-2.0-or-later. */
#include "qa/movement/q1/monsters.h"

#include <math.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/movement/q1/types.h"

void qa_q1_monster_movement_init(qa_q1_monster_movement *movement, qa_q1_monster_move_services services) {
  movement->services = services;
  qa_q1_movement_math_init(&movement->math, &services.numeric);
}

static qa_trace_result monster_trace(qa_q1_monster_movement *movement, qa_actor_id actor, qa_vec3 start, qa_vec3 end,
                                     const qa_bounds *bounds) {
  qa_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = start;
  query.end = end;
  query.shape.kind = bounds == NULL ? "point" : "box";
  if (bounds != NULL) query.shape.bounds = *bounds;
  query.target.is_model = false;
  query.policy.kind = "q1";
  query.policy.move = bounds == NULL ? "no-monsters" : "normal";
  query.policy.has_hull = false;
  query.numeric = movement->services.numeric.profile;
  query.pass_actor = &actor;
  return movement->services.scene.trace(movement->services.scene.ctx, query);
}

static double monster_contents(qa_q1_monster_movement *movement, qa_actor_id actor, qa_vec3 point) {
  qa_point_contents_query query;
  memset(&query, 0, sizeof query);
  query.point = point;
  query.target.is_model = false;
  query.policy.kind = "q1";
  query.policy.move = "normal";
  query.policy.has_hull = false;
  query.numeric = movement->services.numeric.profile;
  query.pass_actor = &actor;
  const qa_point_contents_result result = movement->services.scene.point_contents(movement->services.scene.ctx, query);
  if (result.kind == NULL || strcmp(result.kind, "q1") != 0)
    qa_range_error("Quake monster contents require Q1 policy translation");
  return result.contents;
}

static double angle_mod(qa_q1_monster_movement *movement, double angle) {
  const qa_numeric_operations *n = &movement->services.numeric;
  return qa_q1_n_multiply(n, 360.0 / 65536, (double)(qa_int32(qa_q1_n_to_int32(n, qa_q1_n_multiply(n, angle, 65536.0 / 360))) & 65535));
}

void qa_q1_monster_change_yaw(qa_q1_monster_movement *movement, qa_owned_actor actor) {
  qa_q1_monster_move_state state;
  if (!movement->services.read(movement->services.ctx, actor.id, &state)) return;
  const qa_numeric_operations *n = &movement->services.numeric;
  const double current = angle_mod(movement, state.angles.y), ideal = state.ideal_yaw;
  if (current == ideal) return;
  double move = qa_q1_n_subtract(n, ideal, current);
  if (ideal > current) {
    if (move >= 180) move = qa_q1_n_subtract(n, move, 360);
  } else if (move <= -180) move = qa_q1_n_add(n, move, 360);
  move = move > 0 ? qa_q1_js_min(move, state.yaw_speed) : qa_q1_js_max(move, -state.yaw_speed);
  state.angles = qa_q1_math_vec(&movement->math, state.angles.x, angle_mod(movement, qa_q1_n_add(n, current, move)), state.angles.z);
  movement->services.write(movement->services.ctx, actor, state);
}

bool qa_q1_monster_check_bottom(qa_q1_monster_movement *movement, qa_actor_id actor) {
  qa_q1_monster_move_state state;
  if (!movement->services.read(movement->services.ctx, actor, &state)) return false;
  const qa_q1_movement_math *m = &movement->math;
  const qa_numeric_operations *n = m->n;
  const qa_vec3 minimum = qa_q1_math_add(m, state.origin, state.bounds.min);
  const qa_vec3 maximum = qa_q1_math_add(m, state.origin, state.bounds.max);
  bool easy = true;
  const double xs[2] = {minimum.x, maximum.x};
  const double ys[2] = {minimum.y, maximum.y};
  for (int xi = 0; xi < 2 && easy; xi++)
    for (int yi = 0; yi < 2; yi++) {
      if (monster_contents(movement, actor, qa_q1_math_vec(m, xs[xi], ys[yi], qa_q1_n_subtract(n, minimum.z, 1))) !=
          QA_Q1_CONTENTS_SOLID) {
        easy = false;
        break;
      }
    }
  if (easy) return true;
  const qa_vec3 middle = qa_q1_math_vec(m, qa_q1_n_multiply(n, qa_q1_n_add(n, minimum.x, maximum.x), 0.5),
                                        qa_q1_n_multiply(n, qa_q1_n_add(n, minimum.y, maximum.y), 0.5), minimum.z);
  const double end_z = qa_q1_n_subtract(n, minimum.z, 2 * QA_Q1_STEP_HEIGHT);
  const qa_trace_result trace = monster_trace(movement, actor, middle, qa_q1_math_vec(m, middle.x, middle.y, end_z), NULL);
  if (trace.fraction == 1) return false;
  const double middle_height = trace.end.z;
  for (int xi = 0; xi < 2; xi++)
    for (int yi = 0; yi < 2; yi++) {
      const qa_trace_result corner = monster_trace(
          movement, actor, qa_q1_math_vec(m, xs[xi], ys[yi], minimum.z), qa_q1_math_vec(m, xs[xi], ys[yi], end_z), NULL);
      if (corner.fraction == 1 || qa_q1_n_subtract(n, middle_height, corner.end.z) > QA_Q1_STEP_HEIGHT) return false;
    }
  return true;
}

bool qa_q1_monster_move_step(qa_q1_monster_movement *movement, qa_owned_actor actor, qa_vec3 move, bool relink) {
  qa_q1_monster_move_state state;
  if (!movement->services.read(movement->services.ctx, actor.id, &state)) return false;
  const qa_q1_movement_math *m = &movement->math;
  const qa_numeric_operations *n = m->n;
  const qa_vec3 original = state.origin;
  qa_vec3 destination = qa_q1_math_add(m, state.origin, move);
  if (qa_q1_flag_test(state.flags, QA_Q1_FLAG_SWIM | QA_Q1_FLAG_FLY)) {
    for (int attempt = 0; attempt < 2; attempt++) {
      destination = qa_q1_math_add(m, state.origin, move);
      qa_q1_monster_target enemy;
      const bool has_enemy = state.has_enemy && movement->services.read_target(movement->services.ctx, state.enemy, &enemy);
      if (attempt == 0 && has_enemy) {
        const double dz = qa_q1_n_subtract(n, state.origin.z, enemy.origin.z);
        if (dz > 40) destination = qa_q1_math_vec(m, destination.x, destination.y, qa_q1_n_subtract(n, destination.z, 8));
        if (dz < 30) destination = qa_q1_math_vec(m, destination.x, destination.y, qa_q1_n_add(n, destination.z, 8));
      }
      const qa_trace_result trace = monster_trace(movement, actor.id, state.origin, destination, &state.bounds);
      if (trace.fraction == 1) {
        if (qa_q1_flag_test(state.flags, QA_Q1_FLAG_SWIM) &&
            monster_contents(movement, actor.id, trace.end) == QA_Q1_CONTENTS_EMPTY)
          return false;
        state.origin = trace.end;
        movement->services.write(movement->services.ctx, actor, state);
        if (relink) movement->services.link(movement->services.ctx, actor, true);
        return true;
      }
      if (!has_enemy) break;
    }
    return false;
  }
  destination = qa_q1_math_vec(m, destination.x, destination.y, qa_q1_n_add(n, destination.z, QA_Q1_STEP_HEIGHT));
  const qa_vec3 end = qa_q1_math_vec(m, destination.x, destination.y, qa_q1_n_subtract(n, destination.z, QA_Q1_STEP_HEIGHT * 2));
  qa_trace_result trace = monster_trace(movement, actor.id, destination, end, &state.bounds);
  if (trace.all_solid) return false;
  if (trace.start_solid) {
    destination = qa_q1_math_vec(m, destination.x, destination.y, qa_q1_n_subtract(n, destination.z, QA_Q1_STEP_HEIGHT));
    trace = monster_trace(movement, actor.id, destination, end, &state.bounds);
    if (trace.all_solid || trace.start_solid) return false;
  }
  if (trace.fraction == 1) {
    if (!qa_q1_flag_test(state.flags, QA_Q1_FLAG_PARTIALGROUND)) return false;
    state.origin = qa_q1_math_add(m, state.origin, move);
    movement->services.write(movement->services.ctx, actor, state);
    if (relink) movement->services.link(movement->services.ctx, actor, true);
    qa_q1_monster_move_state after;
    if (movement->services.read(movement->services.ctx, actor.id, &after)) {
      after.flags = qa_q1_flag_clear(after.flags, QA_Q1_FLAG_ONGROUND);
      movement->services.write(movement->services.ctx, actor, after);
    }
    return true;
  }
  state.origin = trace.end;
  movement->services.write(movement->services.ctx, actor, state);
  if (!qa_q1_monster_check_bottom(movement, actor.id)) {
    if (qa_q1_flag_test(state.flags, QA_Q1_FLAG_PARTIALGROUND)) {
      if (relink) movement->services.link(movement->services.ctx, actor, true);
      return true;
    }
    state.origin = original;
    movement->services.write(movement->services.ctx, actor, state);
    return false;
  }
  if (trace.hit.kind == QA_TRACE_HIT_NONE) qa_range_error("Quake monster step landed without a ground hit");
  state.flags = qa_q1_flag_clear(state.flags, QA_Q1_FLAG_PARTIALGROUND);
  state.ground = trace.hit;
  movement->services.write(movement->services.ctx, actor, state);
  if (relink) movement->services.link(movement->services.ctx, actor, true);
  return true;
}

bool qa_q1_monster_step_direction(qa_q1_monster_movement *movement, qa_owned_actor actor, double yaw, double distance) {
  qa_q1_monster_move_state state;
  if (!movement->services.read(movement->services.ctx, actor.id, &state)) return false;
  state.ideal_yaw = qa_q1_n_store(&movement->services.numeric, yaw);
  movement->services.write(movement->services.ctx, actor, state);
  qa_q1_monster_change_yaw(movement, actor);
  if (!movement->services.read(movement->services.ctx, actor.id, &state)) return false;
  const qa_q1_movement_math *m = &movement->math;
  const qa_numeric_operations *n = m->n;
  const double radians = qa_q1_n_divide(n, qa_q1_n_multiply(n, qa_q1_n_multiply(n, yaw, M_PI), 2), 360);
  const qa_vec3 move =
      qa_q1_math_vec(m, qa_q1_n_multiply(n, cos(radians), distance), qa_q1_n_multiply(n, sin(radians), distance), 0);
  const qa_vec3 original = state.origin;
  const bool moved = qa_q1_monster_move_step(movement, actor, move, false);
  if (!movement->services.read(movement->services.ctx, actor.id, &state)) return moved;
  if (moved) {
    const double delta = qa_q1_n_subtract(n, state.angles.y, state.ideal_yaw);
    if (delta > 45 && delta < 315) {
      state.origin = original;
      movement->services.write(movement->services.ctx, actor, state);
    }
  }
  movement->services.link(movement->services.ctx, actor, true);
  return moved;
}

bool qa_q1_monster_walk_move(qa_q1_monster_movement *movement, qa_owned_actor actor, double yaw, double distance) {
  qa_q1_monster_move_state state;
  if (!movement->services.read(movement->services.ctx, actor.id, &state) ||
      !qa_q1_flag_test(state.flags, QA_Q1_FLAG_ONGROUND | QA_Q1_FLAG_FLY | QA_Q1_FLAG_SWIM))
    return false;
  const qa_q1_movement_math *m = &movement->math;
  const qa_numeric_operations *n = m->n;
  const double radians = qa_q1_n_divide(n, qa_q1_n_multiply(n, qa_q1_n_multiply(n, yaw, M_PI), 2), 360);
  return qa_q1_monster_move_step(
      movement, actor,
      qa_q1_math_vec(m, qa_q1_n_multiply(n, cos(radians), distance), qa_q1_n_multiply(n, sin(radians), distance), 0), true);
}

bool qa_q1_monster_close_enough(qa_q1_monster_movement *movement, qa_actor_id actor, qa_actor_id goal, double distance) {
  qa_q1_monster_move_state self;
  qa_q1_monster_target target;
  if (!movement->services.read(movement->services.ctx, actor, &self) ||
      !movement->services.read_target(movement->services.ctx, goal, &target))
    return false;
  const qa_numeric_operations *n = &movement->services.numeric;
  const qa_bounds a = self.absolute_bounds, b = target.absolute_bounds;
  return b.min.x <= qa_q1_n_add(n, a.max.x, distance) && b.max.x >= qa_q1_n_subtract(n, a.min.x, distance) &&
         b.min.y <= qa_q1_n_add(n, a.max.y, distance) && b.max.y >= qa_q1_n_subtract(n, a.min.y, distance) &&
         b.min.z <= qa_q1_n_add(n, a.max.z, distance) && b.max.z >= qa_q1_n_subtract(n, a.min.z, distance);
}

static bool try_step(qa_q1_monster_movement *movement, qa_owned_actor actor, double direction, double distance) {
  qa_q1_monster_move_state state;
  if (!movement->services.read(movement->services.ctx, actor.id, &state)) return true;
  return qa_q1_monster_step_direction(movement, actor, direction, distance);
}

static double next_integer(qa_q1_monster_movement *movement) {
  return movement->services.random.next_integer(movement->services.random.ctx);
}

void qa_q1_monster_new_chase_direction(qa_q1_monster_movement *movement, qa_owned_actor actor, qa_actor_id goal,
                                       double distance) {
  qa_q1_monster_move_state state;
  qa_q1_monster_target enemy;
  if (!movement->services.read(movement->services.ctx, actor.id, &state) ||
      !movement->services.read_target(movement->services.ctx, goal, &enemy))
    return;
  const qa_numeric_operations *n = &movement->services.numeric;
  const double old_direction = angle_mod(movement, qa_q1_n_multiply(n, qa_q1_n_to_int32(n, qa_q1_n_divide(n, state.ideal_yaw, 45)), 45));
  const double turnaround = angle_mod(movement, qa_q1_n_subtract(n, old_direction, 180));
  const double dx = qa_q1_n_subtract(n, enemy.origin.x, state.origin.x);
  const double dy = qa_q1_n_subtract(n, enemy.origin.y, state.origin.y);
  double first = dx > 10 ? 0 : dx < -10 ? 180 : -1;
  double second = dy < -10 ? 270 : dy > 10 ? 90 : -1;
  if (first != -1 && second != -1) {
    /* The southwest constant is 215 in both released source and donor. */
    const double diagonal = first == 0 ? (second == 90 ? 45 : 315) : (second == 90 ? 135 : 215);
    if (diagonal != turnaround && try_step(movement, actor, diagonal, distance)) return;
  }
  if (((qa_int32(next_integer(movement)) & 3) & 1) != 0 || fabs(dy) > fabs(dx)) {
    const double swap = first;
    first = second;
    second = swap;
  }
  if (first != -1 && first != turnaround && try_step(movement, actor, first, distance)) return;
  if (second != -1 && second != turnaround && try_step(movement, actor, second, distance)) return;
  if (old_direction != -1 && try_step(movement, actor, old_direction, distance)) return;
  if ((qa_int32(next_integer(movement)) & 1) != 0) {
    for (double direction = 0; direction <= 315; direction += 45)
      if (direction != turnaround && try_step(movement, actor, direction, distance)) return;
  } else {
    for (double direction = 315; direction >= 0; direction -= 45)
      if (direction != turnaround && try_step(movement, actor, direction, distance)) return;
  }
  if (turnaround != -1 && try_step(movement, actor, turnaround, distance)) return;
  qa_q1_monster_move_state current;
  if (movement->services.read(movement->services.ctx, actor.id, &current)) {
    current.ideal_yaw = old_direction;
    movement->services.write(movement->services.ctx, actor, current);
    if (!qa_q1_monster_check_bottom(movement, actor.id)) {
      qa_q1_monster_move_state after;
      if (movement->services.read(movement->services.ctx, actor.id, &after)) {
        after.flags = qa_q1_flag_set(after.flags, QA_Q1_FLAG_PARTIALGROUND);
        movement->services.write(movement->services.ctx, actor, after);
      }
    }
  }
}

void qa_q1_monster_move_to_goal(qa_q1_monster_movement *movement, qa_owned_actor actor, qa_actor_id goal,
                                double distance, const char *mode) {
  qa_q1_monster_move_state state;
  if (!movement->services.read(movement->services.ctx, actor.id, &state) ||
      !qa_q1_flag_test(state.flags, QA_Q1_FLAG_ONGROUND | QA_Q1_FLAG_FLY | QA_Q1_FLAG_SWIM))
    return;
  const bool range = mode == NULL || strcmp(mode, "range") == 0;
  if (range && state.has_enemy && qa_q1_monster_close_enough(movement, actor.id, goal, distance)) return;
  if ((qa_int32(next_integer(movement)) & 3) == 1 ||
      !qa_q1_monster_step_direction(movement, actor, state.ideal_yaw, distance)) {
    qa_q1_monster_move_state alive;
    if (movement->services.read(movement->services.ctx, actor.id, &alive))
      qa_q1_monster_new_chase_direction(movement, actor, goal, distance);
  }
}
