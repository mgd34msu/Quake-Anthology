/* Port of src/movement/q1/pusher.ts. GPL-2.0-or-later. */
#include "qa/movement/q1/pusher.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include "qa/movement/q1/common.h"

static bool str_eq(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

static bool overlap(qa_bounds a, qa_bounds b) {
  return a.min.x < b.max.x && a.min.y < b.max.y && a.min.z < b.max.z && a.max.x > b.min.x && a.max.y > b.min.y &&
         a.max.z > b.min.z;
}

static bool rider(const qa_q1_physics_entity *entity, qa_actor_id pusher) {
  return qa_q1_flag_test(entity->state.flags, QA_Q1_FLAG_ONGROUND) && entity->state.ground.kind == QA_TRACE_HIT_ACTOR &&
         qa_same_actor(entity->state.ground.actor, pusher);
}

typedef struct pushed_entry {
  qa_actor_id actor;
  qa_vec3 origin;
} pushed_entry;

typedef struct pushed_list {
  pushed_entry *items;
  size_t count;
  size_t capacity;
} pushed_list;

static void pushed_append(pushed_list *list, qa_actor_id actor, qa_vec3 origin) {
  if (list->count == list->capacity) {
    const size_t capacity = list->capacity == 0 ? 8 : list->capacity * 2;
    pushed_entry *grown = realloc(list->items, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    list->items = grown;
    list->capacity = capacity;
  }
  list->items[list->count].actor = actor;
  list->items[list->count].origin = origin;
  list->count++;
}

static qa_q1_pusher_result make_result(qa_actor_id actor, const char *status, pushed_list *pushed) {
  qa_q1_pusher_result result;
  memset(&result, 0, sizeof result);
  result.actor = actor;
  result.status = status;
  if (pushed != NULL && pushed->count > 0) {
    result.moved = malloc(pushed->count * sizeof *result.moved);
    if (result.moved == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < pushed->count; i++) result.moved[i] = pushed->items[i].actor;
    result.moved_count = pushed->count;
  }
  return result;
}

qa_q1_pusher_result qa_move_q1_pusher(qa_q1_pusher_input input, qa_q1_pusher_services services) {
  qa_q1_physics_entity pusher;
  if (!services.read(services.ctx, input.actor, &pusher)) return make_result(input.actor, "actor-removed", NULL);
  qa_q1_movement_math math;
  qa_q1_movement_math_init(&math, &services.numeric);
  const qa_vec3 zero = qa_q1_math_vec(&math, 0, 0, 0);
  const bool rotate = str_eq(input.movement, "rotate");
  const qa_vec3 delta = qa_q1_math_scale(&math, rotate ? pusher.state.angular_velocity : pusher.state.velocity, input.elapsed_seconds);
  const qa_q1_push_input push = {input.actor, rotate ? zero : delta, rotate ? delta : zero, input.elapsed_seconds};
  return qa_push_q1_pusher(push, services);
}

qa_q1_pusher_result qa_push_q1_pusher(qa_q1_push_input input, qa_q1_pusher_services services) {
  qa_q1_physics_entity pusher;
  if (!services.read(services.ctx, input.actor, &pusher)) return make_result(input.actor, "actor-removed", NULL);
  if (!isfinite(input.elapsed_seconds) || input.elapsed_seconds < 0) qa_range_error("Invalid pusher interval");
  qa_q1_movement_math math;
  qa_q1_movement_math_init(&math, &services.numeric);
  const qa_q1_movement_math *m = &math;
  const qa_numeric_operations *n = m->n;
  const qa_vec3 original_origin = pusher.state.origin, original_angles = pusher.state.angles;
  const qa_bounds original_bounds = pusher.absolute_bounds;
  const qa_vec3 delta = input.displacement, angular = input.angular_displacement;
  const bool rotating = angular.x != 0 || angular.y != 0 || angular.z != 0;
  if (delta.x == 0 && delta.y == 0 && delta.z == 0 && !rotating) {
    pusher.local_time_seconds = qa_q1_n_store(n, qa_q1_n_add(n, pusher.local_time_seconds, input.elapsed_seconds));
    services.write(services.ctx, pusher);
    return make_result(input.actor, "moved", NULL);
  }
  pusher.state.origin = qa_q1_math_add(m, pusher.state.origin, delta);
  pusher.state.angles = qa_q1_math_add(m, pusher.state.angles, angular);
  pusher.local_time_seconds = qa_q1_n_store(n, qa_q1_n_add(n, pusher.local_time_seconds, input.elapsed_seconds));
  services.write(services.ctx, pusher);
  services.link(services.ctx, pusher.actor, false);
  pushed_list pushed = {NULL, 0, 0};
  const qa_angle_vectors axes = qa_q1_math_angles(m, qa_q1_math_scale(m, angular, -1));
  qa_q1_physics_entity linked_pusher;
  if (!services.read(services.ctx, input.actor, &linked_pusher)) {
    free(pushed.items);
    return make_result(input.actor, "actor-removed", NULL);
  }
  qa_bounds pusher_bounds;
  if (!rotating) {
    pusher_bounds.min = qa_q1_math_add(m, original_bounds.min, delta);
    pusher_bounds.max = qa_q1_math_add(m, original_bounds.max, delta);
  } else pusher_bounds = linked_pusher.absolute_bounds;
  size_t candidate_count = 0;
  const qa_actor_id *candidates = services.candidates(services.ctx, &candidate_count);
  qa_q1_pusher_result result;
  bool finished = false;
  result = make_result(input.actor, "moved", NULL);
  for (size_t index = 0; index < candidate_count && !finished; index++) {
    const qa_actor_id actor = candidates[index];
    qa_q1_physics_entity entity;
    if (!services.read(services.ctx, actor, &entity) || qa_same_actor(actor, input.actor)) continue;
    if (entity.state.move_type == QA_Q1_MOVE_PUSH || entity.state.move_type == QA_Q1_MOVE_NONE ||
        entity.state.move_type == QA_Q1_MOVE_NOCLIP)
      continue;
    if (!rider(&entity, input.actor)) {
      if (!overlap(entity.absolute_bounds, pusher_bounds)) continue;
      if (services.test_position(services.ctx, entity).kind == QA_TRACE_HIT_NONE) continue;
    }
    if (entity.state.move_type != QA_Q1_MOVE_WALK) entity.state.flags = qa_q1_flag_clear(entity.state.flags, QA_Q1_FLAG_ONGROUND);
    const qa_vec3 original = entity.state.origin;
    pushed_append(&pushed, actor, original);
    qa_vec3 displacement = delta;
    if (rotating) {
      const qa_vec3 offset = qa_q1_math_sub(m, qa_q1_math_add(m, entity.state.origin, delta), pusher.state.origin);
      const qa_vec3 rotated = qa_q1_math_vec(m, qa_q1_math_dot(m, offset, axes.forward), -qa_q1_math_dot(m, offset, axes.right),
                                             qa_q1_math_dot(m, offset, axes.up));
      displacement = qa_q1_math_add(m, delta, qa_q1_math_sub(m, rotated, offset));
    }
    services.collision_enabled(services.ctx, pusher.actor, false);
    const qa_q1_push_result pushed_result = services.push(services.ctx, entity, displacement);
    /* A synchronous trigger may remove the pusher. Never relink a stale actor. */
    qa_q1_physics_entity probe;
    if (services.read(services.ctx, input.actor, &probe)) services.collision_enabled(services.ctx, pusher.actor, true);
    qa_q1_physics_entity live_pusher;
    if (!services.read(services.ctx, input.actor, &live_pusher)) {
      result = make_result(input.actor, "actor-removed", &pushed);
      finished = true;
      break;
    }
    pusher = live_pusher;
    if (!pushed_result.has_entity) continue;
    entity = pushed_result.entity;
    if (services.test_position(services.ctx, entity).kind == QA_TRACE_HIT_NONE) {
      if (rotating) {
        entity.state.angles = qa_q1_math_add(m, entity.state.angles, angular);
        services.write(services.ctx, entity);
      }
      continue;
    }
    if (entity.bounds.min.x == entity.bounds.max.x) continue;
    if (str_eq(entity.solid, "not") || str_eq(entity.solid, "trigger") || str_eq(entity.solid, "corpse")) {
      const qa_vec3 minimum = qa_q1_math_vec(m, 0, 0, entity.bounds.min.z);
      entity.bounds.min = minimum;
      entity.bounds.max = minimum;
      services.write(services.ctx, entity);
      continue;
    }
    entity.state.origin = original;
    services.write(services.ctx, entity);
    services.link(services.ctx, entity.actor, true);
    qa_q1_physics_entity current_pusher;
    if (!services.read(services.ctx, input.actor, &current_pusher)) {
      result = make_result(input.actor, "actor-removed", &pushed);
      finished = true;
      break;
    }
    current_pusher.state.origin = original_origin;
    current_pusher.state.angles = original_angles;
    current_pusher.local_time_seconds =
        qa_q1_n_store(n, qa_q1_n_subtract(n, current_pusher.local_time_seconds, input.elapsed_seconds));
    services.write(services.ctx, current_pusher);
    services.link(services.ctx, current_pusher.actor, false);
    services.blocked(services.ctx, current_pusher.actor, actor);
    /* Source blocked() runs before rollback, so preserve its damage, removals and
     * state changes while restoring only the positions owned by this transaction. */
    for (size_t moved_index = 0; moved_index < pushed.count; moved_index++) {
      qa_q1_physics_entity current;
      if (!services.read(services.ctx, pushed.items[moved_index].actor, &current)) continue;
      current.state.origin = pushed.items[moved_index].origin;
      if (rotating) current.state.angles = qa_q1_math_sub(m, current.state.angles, angular);
      services.write(services.ctx, current);
      services.link(services.ctx, current.actor, false);
    }
    qa_q1_physics_entity final_probe;
    result = make_result(input.actor, services.read(services.ctx, input.actor, &final_probe) ? "blocked" : "actor-removed", &pushed);
    finished = true;
  }
  if (!finished) result = make_result(input.actor, "moved", &pushed);
  free(pushed.items);
  return result;
}

qa_q1_pusher_result qa_step_q1_pusher(qa_q1_pusher_input input, qa_q1_pusher_services services) {
  qa_q1_physics_entity pusher;
  if (!services.read(services.ctx, input.actor, &pusher)) return make_result(input.actor, "actor-removed", NULL);
  const qa_numeric_operations *n = &services.numeric;
  const double old_time = pusher.local_time_seconds, think_time = pusher.next_think_seconds;
  const double move_time = think_time < qa_q1_n_add(n, old_time, input.elapsed_seconds)
                               ? qa_q1_js_max(0, qa_q1_n_subtract(n, think_time, old_time))
                               : input.elapsed_seconds;
  qa_q1_pusher_result result;
  if (move_time == 0) {
    result = make_result(input.actor, "moved", NULL);
  } else {
    qa_q1_pusher_input step_input = input;
    step_input.elapsed_seconds = move_time;
    result = qa_move_q1_pusher(step_input, services);
  }
  qa_q1_physics_entity current;
  if (!services.read(services.ctx, input.actor, &current)) {
    result.status = "actor-removed";
    return result;
  }
  if (think_time > old_time && think_time <= current.local_time_seconds) {
    current.next_think_seconds = 0;
    services.write(services.ctx, current);
    services.think(services.ctx, current.actor);
  }
  qa_q1_physics_entity final_probe;
  if (!services.read(services.ctx, input.actor, &final_probe)) result.status = "actor-removed";
  return result;
}
