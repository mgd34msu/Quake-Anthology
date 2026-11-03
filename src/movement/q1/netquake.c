/* Port of src/movement/q1/netquake.ts. GPL-2.0-or-later. */
#include "qa/movement/q1/netquake.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/movement/client_outputs.h"
#include "qa/movement/q1/common.h"
#include "qa/movement/q1/player_actions.h"
#include "qa/movement/q1/result.h"
#include "qa/movement/q1/water_transition.h"
#include "qa/movement/swept_body.h"

typedef struct netquake_move {
  qa_q1_movement_state state;
  qa_q1_movement_context context;
  qa_q1_movement_input input;
  double frame_seconds;
  double time_seconds;
  bool ladder;
  qa_trace_result trace_scratch;
  qa_trace_result step_trace;
  bool has_step_trace;
  int blocked;
} netquake_move;

typedef struct fly_result {
  int blocked;
  bool has_step_trace;
  qa_trace_result step_trace;
} fly_result;

static bool str_eq(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

static qa_movement_state wrap_state(const netquake_move *move) {
  qa_movement_state state;
  memset(&state, 0, sizeof state);
  state.kind = QA_MOVEMENT_STATE_Q1;
  state.as.q1 = move->state;
  return state;
}

static void apply_client_mode(netquake_move *move) {
  qa_mod_client_movement_mode mode;
  if (qa_client_movement_mode(move->input.environment.client_outputs, move->input.environment.health, &mode)) {
    qa_q1_context_project_client_mode(&move->context);
    move->state.move_type = qa_client_movement_type(QA_MOVEMENT_STATE_Q1, mode);
  }
}

static void set_state(netquake_move *move, qa_movement_state state) {
  if (state.kind != QA_MOVEMENT_STATE_Q1)
    qa_range_error("NetQuake callback changed the movement provider during a frame");
  move->state = state.as.q1;
  apply_client_mode(move);
  if (move->input.environment.flight && move->input.environment.health > 0 && move->state.move_type == QA_Q1_MOVE_WALK)
    move->state.move_type = QA_Q1_MOVE_FLY;
}

static void move_link(netquake_move *move, bool touch_triggers) {
  set_state(move, qa_q1_context_link(&move->context, wrap_state(move), touch_triggers));
}

static void move_impact(netquake_move *move, qa_trace_result trace) {
  set_state(move, qa_q1_context_touch(&move->context, trace, wrap_state(move), true));
}

static bool move_think(netquake_move *move) {
  set_state(move, qa_q1_context_lifecycle(&move->context, wrap_state(move), QA_Q1_LIFECYCLE_THINK, NULL));
  return !move->context.removed;
}

static void velocity_bounds(netquake_move *move) {
  const qa_q1_movement_math *m = &move->context.math;
  qa_q1_movement_state *s = &move->state;
  const double maximum = move->context.options.has_max_velocity ? move->context.options.max_velocity : 2000;
  const qa_vec3 o = s->origin, v = s->velocity;
  s->origin = qa_q1_math_vec(m, isfinite(qa_q1_n_store(m->n, o.x)) ? o.x : 0, isfinite(qa_q1_n_store(m->n, o.y)) ? o.y : 0,
                             isfinite(qa_q1_n_store(m->n, o.z)) ? o.z : 0);
  s->velocity =
      qa_q1_math_vec(m, qa_q1_js_max(-maximum, qa_q1_js_min(maximum, isfinite(qa_q1_n_store(m->n, v.x)) ? v.x : 0)),
                     qa_q1_js_max(-maximum, qa_q1_js_min(maximum, isfinite(qa_q1_n_store(m->n, v.y)) ? v.y : 0)),
                     qa_q1_js_max(-maximum, qa_q1_js_min(maximum, isfinite(qa_q1_n_store(m->n, v.z)) ? v.z : 0)));
}

static void gravity_step(netquake_move *move) {
  const qa_q1_movement_math *m = &move->context.math;
  const qa_numeric_operations *n = m->n;
  qa_q1_movement_state *s = &move->state;
  const qa_q1_movement_parameters *p = &move->input.profile.parameters;
  const double entity_gravity = p->entity_gravity == 0 ? 1 : p->entity_gravity;
  const double gravity = qa_q1_n_multiply(
      n, qa_q1_n_multiply(n, qa_q1_n_multiply(n, entity_gravity, move->input.environment.gravity_multiplier), p->gravity),
      move->frame_seconds);
  s->velocity = qa_q1_math_vec(m, s->velocity.x, s->velocity.y, qa_q1_n_subtract(n, s->velocity.z, gravity));
}

static bool sweep_read(void *data, qa_swept_body_state *out) {
  netquake_move *move = data;
  if (move->context.removed) return false;
  out->origin = move->state.origin;
  out->velocity = move->state.velocity;
  return true;
}
static void sweep_write_origin(void *data, qa_vec3 origin) { ((netquake_move *)data)->state.origin = origin; }
static void sweep_write_velocity(void *data, qa_vec3 velocity, qa_swept_velocity_components components) {
  (void)components;
  ((netquake_move *)data)->state.velocity = velocity;
}
static void sweep_trace(void *data, qa_vec3 start, qa_vec3 end, qa_swept_body_trace *out) {
  netquake_move *move = data;
  move->trace_scratch = qa_q1_context_trace(&move->context, start, end);
  out->fraction = move->trace_scratch.fraction;
  out->end = move->trace_scratch.end;
  out->all_solid = move->trace_scratch.all_solid;
  out->start_solid = move->trace_scratch.start_solid;
  out->native = &move->trace_scratch;
}
static qa_vec3 sweep_normal(void *data, const qa_swept_body_trace *trace) {
  (void)data;
  const qa_trace_result *native = trace->native;
  if (native->hit.kind == QA_TRACE_HIT_NONE) qa_range_error("NetQuake slide trace blocked without a hit");
  return qa_q1_trace_source_normal(native);
}
static void sweep_impact(void *data, const qa_swept_body_trace *trace, qa_vec3 normal) {
  netquake_move *move = data;
  const qa_trace_result *native = trace->native;
  if (normal.z > 0.7) {
    move->blocked |= 1;
    if (qa_q1_context_is_bsp(&move->context, native->hit)) {
      move->state.flags = qa_q1_flag_set(move->state.flags, QA_Q1_FLAG_ONGROUND);
      move->state.ground = native->hit;
    }
  }
  if (normal.z == 0) {
    move->blocked |= 2;
    move->step_trace = *native;
    move->has_step_trace = true;
  }
  move_impact(move, *native);
}
static uintptr_t sweep_plane_identity(void *data, const qa_swept_body_trace *trace) {
  (void)data;
  return ((const qa_trace_result *)trace->native)->plane_identity;
}
static bool sweep_same_plane(void *data, qa_vec3 first, qa_vec3 second, bool same_slot) {
  (void)data;
  (void)first;
  (void)second;
  return same_slot;
}
static qa_vec3 sweep_advance(void *data, qa_vec3 origin, double time, qa_vec3 velocity) {
  return qa_q1_math_ma(data, origin, time, velocity);
}
static double sweep_remaining(void *data, double time, double fraction) {
  const qa_q1_movement_math *m = data;
  return qa_q1_n_subtract(m->n, time, qa_q1_n_multiply(m->n, time, fraction));
}
static qa_vec3 sweep_clip(void *data, qa_vec3 velocity, qa_vec3 normal) {
  return qa_q1_math_clip(data, velocity, normal, 1);
}
static double sweep_dot(void *data, qa_vec3 a, qa_vec3 b) { return qa_q1_math_dot(data, a, b); }
static qa_vec3 sweep_cross(void *data, qa_vec3 a, qa_vec3 b) { return qa_q1_math_cross(data, a, b); }
static qa_vec3 sweep_scale(void *data, qa_vec3 v, double amount) { return qa_q1_math_scale(data, v, amount); }

static fly_result fly_move(netquake_move *move, double time) {
  move->blocked = 0;
  move->has_step_trace = false;
  qa_swept_body_services services;
  memset(&services, 0, sizeof services);
  services.ctx = move;
  services.read = sweep_read;
  services.write_origin = sweep_write_origin;
  services.write_velocity = sweep_write_velocity;
  services.trace = sweep_trace;
  services.normal = sweep_normal;
  services.impact = sweep_impact;
  services.stop_when_still = true;
  services.same_plane = sweep_same_plane;
  services.plane_identity = sweep_plane_identity;
  services.math.ctx = &move->context.math;
  services.math.advance = sweep_advance;
  services.math.remaining = sweep_remaining;
  services.math.clip = sweep_clip;
  services.math.dot = sweep_dot;
  services.math.cross = sweep_cross;
  services.math.scale = sweep_scale;
  const qa_sweep_stop stop = qa_sweep_body(&services, time);
  fly_result result = {move->blocked, move->has_step_trace, move->step_trace};
  if (stop == QA_SWEEP_SOLID || stop == QA_SWEEP_PLANE_LIMIT) result.blocked = 3;
  else if (stop == QA_SWEEP_CREASE_BLOCKED) result.blocked = 7;
  return result;
}

static qa_trace_result push_entity(netquake_move *move, qa_vec3 push) {
  qa_q1_movement_context *c = &move->context;
  qa_q1_movement_state *s = &move->state;
  const qa_trace_shape shape = qa_q1_context_shape(c);
  const char *mode = s->move_type == QA_Q1_MOVE_FLYMISSILE ? "missile"
                     : str_eq(c->options.solid, "not") || str_eq(c->options.solid, "trigger") ? "no-monsters"
                                                                                                : "normal";
  const qa_trace_result trace =
      qa_q1_context_trace_ex(c, s->origin, qa_q1_math_add(&c->math, s->origin, push), &shape, mode);
  s->origin = trace.end;
  move_link(move, true);
  if (!c->removed && trace.hit.kind != QA_TRACE_HIT_NONE) move_impact(move, trace);
  return trace;
}

static void check_stuck(netquake_move *move) {
  qa_q1_movement_context *c = &move->context;
  qa_q1_movement_state *s = &move->state;
  if (qa_q1_context_position_free(c, s->origin)) {
    s->old_origin = s->origin;
    return;
  }
  const qa_vec3 original = s->origin;
  s->origin = s->old_origin;
  if (qa_q1_context_position_free(c, s->origin)) {
    move_link(move, true);
    return;
  }
  for (int z = 0; z < 18; z++)
    for (int x = -1; x <= 1; x++)
      for (int y = -1; y <= 1; y++) {
        const qa_vec3 offset = {(double)x, (double)y, (double)z};
        s->origin = qa_q1_math_add(&c->math, original, offset);
        if (qa_q1_context_position_free(c, s->origin)) {
          move_link(move, true);
          return;
        }
      }
  s->origin = original;
}

static bool check_water(netquake_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_q1_movement_state *s = &move->state;
  const qa_bounds bounds = qa_q1_context_bounds(c);
  qa_vec3 point = qa_q1_math_vec(m, s->origin.x, s->origin.y, qa_q1_n_add(n, qa_q1_n_add(n, s->origin.z, bounds.min.z), 1));
  s->water_level = 0;
  s->water_type = QA_Q1_CONTENTS_EMPTY;
  const double contents = qa_q1_context_contents(c, point);
  if (contents <= QA_Q1_CONTENTS_WATER) {
    s->water_type = contents;
    s->water_level = 1;
    point = qa_q1_math_vec(m, point.x, point.y,
                           qa_q1_n_add(n, s->origin.z, qa_q1_n_multiply(n, qa_q1_n_add(n, bounds.min.z, bounds.max.z), 0.5)));
    if (qa_q1_context_contents(c, point) <= QA_Q1_CONTENTS_WATER) {
      s->water_level = 2;
      point = qa_q1_math_vec(m, point.x, point.y, qa_q1_n_add(n, s->origin.z, qa_q1_context_view_height(c)));
      if (qa_q1_context_contents(c, point) <= QA_Q1_CONTENTS_WATER) s->water_level = 3;
    }
  }
  return s->water_level > 1;
}

static void wall_friction(netquake_move *move, const qa_trace_result *trace) {
  const qa_q1_movement_math *m = &move->context.math;
  const qa_numeric_operations *n = m->n;
  qa_q1_movement_state *s = &move->state;
  const qa_vec3 normal = qa_q1_trace_source_normal(trace);
  const qa_vec3 direction = qa_q1_math_angles(m, s->view_angles).forward;
  const double d = qa_q1_n_add(n, qa_q1_math_dot(m, normal, direction), 0.5);
  if (d >= 0) return;
  const qa_vec3 into = qa_q1_math_scale(m, normal, qa_q1_math_dot(m, normal, s->velocity));
  const qa_vec3 side = qa_q1_math_sub(m, s->velocity, into);
  s->velocity = qa_q1_math_vec(m, qa_q1_n_multiply(n, side.x, qa_q1_n_add(n, 1, d)),
                               qa_q1_n_multiply(n, side.y, qa_q1_n_add(n, 1, d)), s->velocity.z);
}

static fly_result try_unstick(netquake_move *move, qa_vec3 old_velocity) {
  qa_q1_movement_context *c = &move->context;
  qa_q1_movement_state *s = &move->state;
  const qa_vec3 original = s->origin;
  static const qa_vec3 directions[8] = {{2, 0, 0}, {0, 2, 0}, {-2, 0, 0}, {0, -2, 0},
                                        {2, 2, 0}, {-2, 2, 0}, {2, -2, 0}, {-2, -2, 0}};
  for (int i = 0; i < 8; i++) {
    (void)push_entity(move, directions[i]);
    if (c->removed) {
      const fly_result result = {7, false, move->step_trace};
      return result;
    }
    s->velocity = qa_q1_math_vec(&c->math, old_velocity.x, old_velocity.y, 0);
    const fly_result clip = fly_move(move, 0.1);
    if (c->removed || fabs(original.y - s->origin.y) > 4 || fabs(original.x - s->origin.x) > 4) return clip;
    s->origin = original;
  }
  s->velocity = qa_q1_zero;
  const fly_result result = {7, false, move->step_trace};
  return result;
}

static void walk_move(netquake_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_q1_movement_state *s = &move->state;
  const bool old_on_ground = qa_q1_flag_test(s->flags, QA_Q1_FLAG_ONGROUND);
  s->flags = qa_q1_flag_clear(s->flags, QA_Q1_FLAG_ONGROUND);
  const qa_vec3 original = s->origin, old_velocity = s->velocity;
  fly_result clip = fly_move(move, move->frame_seconds);
  if (c->removed || (clip.blocked & 2) == 0 || (!old_on_ground && s->water_level == 0) ||
      s->move_type != QA_Q1_MOVE_WALK || c->options.no_step || qa_q1_flag_test(s->flags, QA_Q1_FLAG_WATERJUMP))
    return;
  const qa_vec3 no_step_origin = s->origin, no_step_velocity = s->velocity;
  s->origin = original;
  const qa_vec3 step_up = {0, 0, QA_Q1_STEP_HEIGHT};
  (void)push_entity(move, step_up);
  if (c->removed) return;
  s->velocity = qa_q1_math_vec(m, old_velocity.x, old_velocity.y, 0);
  clip = fly_move(move, move->frame_seconds);
  if (c->removed) return;
  if (clip.blocked != 0 && fabs(original.y - s->origin.y) < 0.03125 && fabs(original.x - s->origin.x) < 0.03125) {
    const fly_result unstick = try_unstick(move, old_velocity);
    clip.blocked = unstick.blocked;
  }
  if (c->removed) return;
  if ((clip.blocked & 2) != 0 && clip.has_step_trace) wall_friction(move, &clip.step_trace);
  const qa_vec3 step_down = {0, 0, qa_q1_n_add(n, -QA_Q1_STEP_HEIGHT, qa_q1_n_multiply(n, old_velocity.z, move->frame_seconds))};
  const qa_trace_result down = push_entity(move, step_down);
  if (c->removed) return;
  if (qa_q1_trace_source_normal(&down).z > 0.7) {
    /* WinQuake checks the player's SOLID_BSP here, not the hit's solid type. */
    if (str_eq(c->options.solid, "bsp")) {
      s->flags = qa_q1_flag_set(s->flags, QA_Q1_FLAG_ONGROUND);
      s->ground = down.hit;
    }
  } else {
    s->origin = no_step_origin;
    s->velocity = no_step_velocity;
  }
}

static void friction_step(netquake_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_q1_movement_state *s = &move->state;
  const qa_q1_movement_parameters *p = &move->input.profile.parameters;
  const double speed = qa_q1_math_horizontal(m, s->velocity);
  if (speed == 0) return;
  const qa_bounds bounds = qa_q1_context_bounds(c);
  const qa_vec3 start = qa_q1_math_vec(m, qa_q1_n_add(n, s->origin.x, qa_q1_n_multiply(n, qa_q1_n_divide(n, s->velocity.x, speed), 16)),
                                       qa_q1_n_add(n, s->origin.y, qa_q1_n_multiply(n, qa_q1_n_divide(n, s->velocity.y, speed), 16)),
                                       qa_q1_n_add(n, s->origin.z, bounds.min.z));
  const qa_trace_shape point = {"point", {qa_q1_zero, qa_q1_zero}};
  const qa_vec3 down34 = {0, 0, -34};
  const qa_trace_result trace = qa_q1_context_trace_ex(c, start, qa_q1_math_add(m, start, down34), &point, "no-monsters");
  const double friction = trace.fraction == 1 ? qa_q1_n_multiply(n, p->friction, move->input.profile.edge_friction) : p->friction;
  const double new_speed =
      qa_q1_js_max(0, qa_q1_n_subtract(n, speed,
                                       qa_q1_n_multiply(n, qa_q1_n_multiply(n, move->frame_seconds, qa_q1_js_max(speed, p->stop_speed)), friction)));
  s->velocity = qa_q1_math_scale(m, s->velocity, qa_q1_n_divide(n, new_speed, speed));
}

static void accelerate_step(netquake_move *move, qa_vec3 direction, double speed, bool air) {
  const qa_q1_movement_math *m = &move->context.math;
  const qa_numeric_operations *n = m->n;
  qa_q1_movement_state *s = &move->state;
  const qa_q1_movement_parameters *p = &move->input.profile.parameters;
  const double add =
      qa_q1_n_subtract(n, air ? qa_q1_js_min(qa_q1_math_length(m, direction), 30) : speed,
                       qa_q1_math_dot(m, s->velocity, air ? qa_q1_math_normalize(m, direction).direction : direction));
  if (add <= 0) return;
  const double acceleration = qa_q1_js_min(add, air ? qa_q1_n_multiply(n, qa_q1_n_multiply(n, p->accelerate, speed), move->frame_seconds)
                                                    : qa_q1_n_multiply(n, qa_q1_n_multiply(n, p->accelerate, move->frame_seconds), speed));
  s->velocity = qa_q1_math_ma(m, s->velocity, acceleration, air ? qa_q1_math_normalize(m, direction).direction : direction);
}

static void water_move(netquake_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_q1_movement_state *s = &move->state;
  const qa_q1_movement_parameters *p = &move->input.profile.parameters;
  const qa_q1_user_command *command = &move->input.command.as.q1;
  const qa_angle_vectors axes = qa_q1_math_angles(m, s->view_angles);
  const double forward_speed = qa_q1_context_speed(c, command->forward_move);
  const double side_speed = qa_q1_context_speed(c, command->side_move);
  qa_vec3 wish = qa_q1_math_vec(m, qa_q1_n_add(n, qa_q1_n_multiply(n, axes.forward.x, forward_speed), qa_q1_n_multiply(n, axes.right.x, side_speed)),
                                qa_q1_n_add(n, qa_q1_n_multiply(n, axes.forward.y, forward_speed), qa_q1_n_multiply(n, axes.right.y, side_speed)),
                                qa_q1_n_add(n, qa_q1_n_multiply(n, axes.forward.z, forward_speed), qa_q1_n_multiply(n, axes.right.z, side_speed)));
  wish = qa_q1_math_vec(m, wish.x, wish.y,
                        qa_q1_n_add(n, wish.z,
                                    command->forward_move == 0 && command->side_move == 0 && command->up_move == 0
                                        ? -60
                                        : qa_q1_context_speed(c, command->up_move)));
  double wish_speed = qa_q1_math_length(m, wish);
  if (wish_speed > qa_q1_context_speed(c, p->max_speed)) {
    wish = qa_q1_math_scale(m, wish, qa_q1_n_divide(n, qa_q1_context_speed(c, p->max_speed), wish_speed));
    wish_speed = qa_q1_context_speed(c, p->max_speed);
  }
  wish_speed = qa_q1_n_multiply(n, wish_speed, 0.7);
  const double speed = qa_q1_math_length(m, s->velocity);
  double new_speed = 0;
  if (speed != 0) {
    new_speed = qa_q1_js_max(0, qa_q1_n_subtract(n, speed, qa_q1_n_multiply(n, qa_q1_n_multiply(n, move->frame_seconds, speed), p->friction)));
    s->velocity = qa_q1_math_scale(m, s->velocity, qa_q1_n_divide(n, new_speed, speed));
  }
  if (wish_speed == 0) return;
  const double add = qa_q1_n_subtract(n, wish_speed, new_speed);
  if (add <= 0) return;
  const double acceleration = qa_q1_js_min(add, qa_q1_n_multiply(n, qa_q1_n_multiply(n, p->accelerate, wish_speed), move->frame_seconds));
  s->velocity = qa_q1_math_ma(m, s->velocity, acceleration, qa_q1_math_normalize(m, wish).direction);
}

static void air_move(netquake_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_q1_movement_state *s = &move->state;
  const qa_q1_user_command *command = &move->input.command.as.q1;
  const qa_q1_movement_parameters *p = &move->input.profile.parameters;
  const qa_angle_vectors axes = qa_q1_math_angles(m, s->angles);
  const double forward_move = move->time_seconds < s->teleport_time_seconds && command->forward_move < 0 ? 0 : command->forward_move;
  const double forward_speed = qa_q1_context_speed(c, forward_move);
  const double side_speed = qa_q1_context_speed(c, command->side_move);
  qa_vec3 wish = qa_q1_math_vec(m, qa_q1_n_add(n, qa_q1_n_multiply(n, axes.forward.x, forward_speed), qa_q1_n_multiply(n, axes.right.x, side_speed)),
                                qa_q1_n_add(n, qa_q1_n_multiply(n, axes.forward.y, forward_speed), qa_q1_n_multiply(n, axes.right.y, side_speed)),
                                s->move_type == QA_Q1_MOVE_WALK ? 0 : qa_q1_context_speed(c, command->up_move));
  const qa_q1_normalized normalized = qa_q1_math_normalize(m, wish);
  double wish_speed = normalized.length;
  if (wish_speed > qa_q1_context_speed(c, p->max_speed)) {
    wish = qa_q1_math_scale(m, wish, qa_q1_n_divide(n, qa_q1_context_speed(c, p->max_speed), wish_speed));
    wish_speed = qa_q1_context_speed(c, p->max_speed);
  }
  if (s->move_type == QA_Q1_MOVE_NOCLIP) s->velocity = wish;
  else if (qa_q1_flag_test(s->flags, QA_Q1_FLAG_ONGROUND)) {
    friction_step(move);
    accelerate_step(move, normalized.direction, wish_speed, false);
  } else accelerate_step(move, wish, wish_speed, true);
}

static void alternate_noclip(netquake_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_q1_movement_state *s = &move->state;
  const qa_q1_user_command *command = &move->input.command.as.q1;
  const qa_angle_vectors axes = qa_q1_math_angles(m, s->view_angles);
  const double forward_speed = qa_q1_context_speed(c, command->forward_move);
  const double side_speed = qa_q1_context_speed(c, command->side_move);
  const qa_vec3 wish = qa_q1_math_vec(
      m, qa_q1_n_add(n, qa_q1_n_multiply(n, axes.forward.x, forward_speed), qa_q1_n_multiply(n, axes.right.x, side_speed)),
      qa_q1_n_add(n, qa_q1_n_multiply(n, axes.forward.y, forward_speed), qa_q1_n_multiply(n, axes.right.y, side_speed)),
      qa_q1_n_add(n, qa_q1_n_add(n, qa_q1_n_multiply(n, axes.forward.z, forward_speed), qa_q1_n_multiply(n, axes.right.z, side_speed)),
                  qa_q1_n_multiply(n, qa_q1_context_speed(c, command->up_move), 2)));
  const qa_q1_normalized normalized = qa_q1_math_normalize(m, wish);
  s->velocity = normalized.length > qa_q1_context_speed(c, move->input.profile.parameters.max_speed)
                    ? qa_q1_math_scale(m, normalized.direction, qa_q1_context_speed(c, move->input.profile.parameters.max_speed))
                    : wish;
}

static void ladder_move(netquake_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_q1_movement_state *s = &move->state;
  const qa_q1_movement_parameters *p = &move->input.profile.parameters;
  const qa_q1_user_command *command = &move->input.command.as.q1;
  const qa_angle_vectors axes = qa_q1_math_angles(m, s->view_angles);
  const double forward_speed = qa_q1_context_speed(c, command->forward_move);
  const double side_speed = qa_q1_context_speed(c, command->side_move);
  const qa_vec3 wish = qa_q1_math_vec(m, qa_q1_n_add(n, qa_q1_n_multiply(n, axes.forward.x, forward_speed), qa_q1_n_multiply(n, axes.right.x, side_speed)),
                                      qa_q1_n_add(n, qa_q1_n_multiply(n, axes.forward.y, forward_speed), qa_q1_n_multiply(n, axes.right.y, side_speed)),
                                      qa_q1_n_add(n, qa_q1_n_multiply(n, axes.forward.z, forward_speed), qa_q1_n_multiply(n, axes.right.z, side_speed)));
  const qa_vec3 wish_vel =
      qa_q1_math_vec(m, wish.x, wish.y, qa_q1_ladder_wish_z(wish.z, command->forward_move, command->up_move, s->velocity.z));
  const qa_q1_normalized normalized = qa_q1_math_normalize(m, wish_vel);
  const double wish_speed = qa_q1_js_min(normalized.length, qa_q1_context_speed(c, p->max_speed));
  /* Velocity shaping follows waterMove: friction first, then accelerate toward the wish. */
  const double speed = qa_q1_math_length(m, s->velocity);
  double new_speed = 0;
  if (speed != 0) {
    new_speed = qa_q1_js_max(0, qa_q1_n_subtract(n, speed, qa_q1_n_multiply(n, qa_q1_n_multiply(n, move->frame_seconds, speed), p->friction)));
    s->velocity = qa_q1_math_scale(m, s->velocity, qa_q1_n_divide(n, new_speed, speed));
  }
  if (wish_speed > 0) {
    const double add = qa_q1_n_subtract(n, wish_speed, new_speed);
    if (add > 0)
      s->velocity = qa_q1_math_ma(m, s->velocity,
                                  qa_q1_js_min(add, qa_q1_n_multiply(n, qa_q1_n_multiply(n, p->accelerate, wish_speed), move->frame_seconds)),
                                  normalized.direction);
  } else if (s->velocity.z > 0) {
    s->velocity = qa_q1_math_vec(m, s->velocity.x, s->velocity.y,
                                 qa_q1_js_max(0, qa_q1_n_subtract(n, s->velocity.z, qa_q1_n_multiply(n, p->gravity, move->frame_seconds))));
  } else {
    s->velocity = qa_q1_math_vec(m, s->velocity.x, s->velocity.y,
                                 qa_q1_js_min(0, qa_q1_n_add(n, s->velocity.z, qa_q1_n_multiply(n, p->gravity, move->frame_seconds))));
  }
}

static void client_think(netquake_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_q1_movement_state *s = &move->state;
  if (s->move_type == QA_Q1_MOVE_NONE) return;
  if (c->options.has_source_punch_angles) s->punch_angles = c->options.source_punch_angles;
  else {
    const qa_q1_normalized punch = qa_q1_math_normalize(m, s->punch_angles);
    s->punch_angles = qa_q1_math_scale(m, punch.direction,
                                       qa_q1_js_max(0, qa_q1_n_subtract(n, punch.length, qa_q1_n_multiply(n, 10, move->frame_seconds))));
  }
  if (s->health <= 0) return;
  const qa_vec3 angles = qa_q1_math_add(m, s->view_angles, s->punch_angles);
  const double side = qa_q1_math_dot(m, s->velocity, qa_q1_math_angles(m, s->angles).right);
  const double sign = side < 0 ? -1 : 1;
  const double roll_speed = c->options.has_roll_speed ? c->options.roll_speed : 200;
  const double roll_angle = c->options.has_roll_angle ? c->options.roll_angle : 2;
  const double roll = qa_q1_n_multiply(
      n, qa_q1_n_multiply(n, fabs(side) < roll_speed ? qa_q1_n_divide(n, qa_q1_n_multiply(n, fabs(side), roll_angle), roll_speed) : roll_angle, sign), 4);
  if (!s->fix_angle) s->angles = qa_q1_math_vec(m, qa_q1_n_divide(n, -angles.x, 3), angles.y, roll);
  else if (str_eq(c->options.fix_angle_roll != NULL ? c->options.fix_angle_roll : "source", "source"))
    s->angles = qa_q1_math_vec(m, s->angles.x, s->angles.y, roll);
  if (qa_q1_flag_test(s->flags, QA_Q1_FLAG_WATERJUMP)) {
    if (move->time_seconds > s->teleport_time_seconds || s->water_level == 0) {
      s->flags = qa_q1_flag_clear(s->flags, QA_Q1_FLAG_WATERJUMP);
      s->teleport_time_seconds = 0;
    }
    s->velocity = qa_q1_math_vec(m, s->water_jump_direction.x, s->water_jump_direction.y, s->velocity.z);
    return;
  }
  if (c->options.source_water_drag && s->water_level > 0 && s->move_type != QA_Q1_MOVE_NOCLIP)
    s->velocity = qa_q1_math_ma(m, s->velocity, -qa_q1_n_multiply(n, qa_q1_n_multiply(n, 0.8, s->water_level), move->frame_seconds), s->velocity);
  move->ladder = s->move_type == QA_Q1_MOVE_WALK && s->water_level < 2 &&
                 qa_q1_context_ladder_ahead(c, s->origin, qa_q1_math_angles(m, s->view_angles).forward);
  if (s->move_type == QA_Q1_MOVE_NOCLIP && move->input.profile.no_clip_angle_hack) alternate_noclip(move);
  else if (s->water_level >= 2 && s->move_type != QA_Q1_MOVE_NOCLIP) water_move(move);
  else if (move->ladder) ladder_move(move);
  else air_move(move);
}

static void player_actions(netquake_move *move) {
  qa_q1_movement_context *c = &move->context;
  qa_q1_movement_state *s = &move->state;
  if (str_eq(c->options.jump_authority, "source-gamecode") || s->health <= 0 || qa_q1_context_view_height(c) == 0 ||
      s->move_type != QA_Q1_MOVE_WALK)
    return;
  if (s->water_level == 2) {
    qa_q1_movement_input jump_input = move->input;
    jump_input.state = wrap_state(move);
    qa_movement_state jumped;
    memset(&jumped, 0, sizeof jumped);
    jumped.kind = QA_MOVEMENT_STATE_Q1;
    jumped.as.q1 = qa_q1_check_water_jump(jump_input, c->services, c->options);
    set_state(move, jumped);
  }
  if ((qa_int32(move->input.command.as.q1.buttons) & 2) != 0) {
    const qa_q1_jump_result result = qa_q1_player_jump(*s, c->services);
    qa_movement_state jumped;
    memset(&jumped, 0, sizeof jumped);
    jumped.kind = QA_MOVEMENT_STATE_Q1;
    jumped.as.q1 = result.state;
    set_state(move, jumped);
    if (!str_eq(result.action, "none") && c->options.hooks != NULL && c->options.hooks->player_action != NULL)
      c->options.hooks->player_action(c->options.hooks->ctx, move->input.actor, result.action, *s);
  } else s->flags = qa_q1_flag_set(s->flags, QA_Q1_FLAG_JUMPRELEASED);
}

static void ideal_pitch(netquake_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_q1_movement_state *s = &move->state;
  if (!qa_q1_flag_test(s->flags, QA_Q1_FLAG_ONGROUND)) return;
  const double radians = qa_q1_n_divide(n, qa_q1_n_multiply(n, qa_q1_n_multiply(n, s->angles.y, M_PI), 2), 360);
  const double sine = sin(radians), cosine = cos(radians);
  double heights[6];
  const qa_trace_shape point = {"point", {qa_q1_zero, qa_q1_zero}};
  for (int i = 0; i < 6; i++) {
    const qa_vec3 top = qa_q1_math_vec(m, qa_q1_n_add(n, s->origin.x, qa_q1_n_multiply(n, qa_q1_n_multiply(n, cosine, i + 3), 12)),
                                       qa_q1_n_add(n, s->origin.y, qa_q1_n_multiply(n, qa_q1_n_multiply(n, sine, i + 3), 12)),
                                       qa_q1_n_add(n, s->origin.z, qa_q1_context_view_height(c)));
    const qa_vec3 drop = {0, 0, -160};
    const qa_vec3 bottom = qa_q1_math_add(m, top, drop);
    const qa_trace_result trace = qa_q1_context_trace_ex(c, top, bottom, &point, "no-monsters");
    if (trace.all_solid || trace.fraction == 1) return;
    heights[i] = qa_q1_n_store(n, qa_q1_n_add(n, top.z, qa_q1_n_multiply(n, trace.fraction, qa_q1_n_subtract(n, bottom.z, top.z))));
  }
  double direction = 0, previous = heights[0];
  int steps = 0;
  for (int i = 1; i < 6; i++) {
    /* sv_user.c declares step and dir as int, so each probe difference truncates toward zero. */
    const double step = trunc(qa_q1_n_subtract(n, heights[i], previous));
    previous = heights[i];
    if (step > -0.1 && step < 0.1) continue;
    if (direction != 0 && fabs(qa_q1_n_subtract(n, step, direction)) > 0.1) return;
    steps++;
    direction = step;
  }
  if (direction == 0) s->ideal_pitch = 0;
  else if (steps >= 2)
    s->ideal_pitch =
        qa_q1_n_store(n, qa_q1_n_multiply(n, -direction, c->options.has_ideal_pitch_scale ? c->options.ideal_pitch_scale : 0.8));
}

static void water_transition(netquake_move *move) {
  qa_q1_movement_state *s = &move->state;
  const double contents = qa_q1_context_contents(&move->context, s->origin);
  const qa_q1_water_transition_result transition = qa_q1_water_transition(s->water_type, contents);
  const qa_q1_movement_hooks *hooks = move->context.options.hooks;
  if (transition.splash && hooks != NULL && hooks->sound != NULL)
    hooks->sound(hooks->ctx, move->input.actor, "misc/h2ohit1.wav", *s);
  s->water_type = transition.water_type;
  s->water_level = transition.water_level;
}

static void toss_step(netquake_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  qa_q1_movement_state *s = &move->state;
  if (qa_q1_flag_test(s->flags, QA_Q1_FLAG_ONGROUND)) return;
  velocity_bounds(move);
  if (s->move_type != QA_Q1_MOVE_FLY && s->move_type != QA_Q1_MOVE_FLYMISSILE) gravity_step(move);
  s->angles = qa_q1_math_ma(m, s->angles, move->frame_seconds, s->angular_velocity);
  const qa_trace_result trace = push_entity(move, qa_q1_math_scale(m, s->velocity, move->frame_seconds));
  if (trace.fraction == 1 || c->removed) return;
  const bool bounces =
      s->move_type == QA_Q1_MOVE_BOUNCE || (s->move_type == QA_Q1_MOVE_GIB && str_eq(move->input.profile.edition, "rerelease"));
  const qa_vec3 normal = qa_q1_trace_source_normal(&trace);
  s->velocity = qa_q1_math_clip(m, s->velocity, normal, bounces ? 1.5 : 1);
  if (normal.z > 0.7 && (s->velocity.z < 60 || !bounces)) {
    s->flags = qa_q1_flag_set(s->flags, QA_Q1_FLAG_ONGROUND);
    s->ground = trace.hit;
    s->velocity = qa_q1_zero;
    s->angular_velocity = qa_q1_zero;
  }
  water_transition(move);
}

static void netquake_init(netquake_move *move, qa_q1_movement_input input, qa_movement_services services,
                          qa_q1_movement_options options) {
  memset(move, 0, sizeof *move);
  if (str_eq(input.profile.edition, "quake64"))
    qa_range_error("Quake64 movement requires a qualified source physics profile");
  if (input.profile.clock.kind != QA_CLOCK_Q1_NETQUAKE)
    qa_range_error("NetQuake movement requires a NetQuake frame clock");
  move->input = input;
  move->state = input.state.as.q1;
  if (input.environment.flight && input.environment.health > 0 && move->state.move_type == QA_Q1_MOVE_WALK)
    move->state.move_type = QA_Q1_MOVE_FLY;
  qa_q1_movement_context_init(&move->context, input, services, options);
  apply_client_mode(move);
  /* The shared frame owner already applies host minimum/maximum/fixed frame rules. */
  move->frame_seconds = qa_q1_seconds(input.frame.elapsed);
  move->time_seconds = qa_q1_seconds(input.frame.time);
  if (!isfinite(move->frame_seconds) || move->frame_seconds < 0) qa_range_error("Invalid NetQuake frame interval");
}

qa_q1_movement_state qa_prepare_netquake(qa_q1_movement_input input, qa_movement_services services,
                                         qa_q1_movement_options options) {
  netquake_move move;
  netquake_init(&move, input, services, options);
  const qa_vec3 command_angles = input.command.as.q1.view_angles;
  move.state.view_angles = qa_q1_math_vec(&move.context.math, command_angles.x, command_angles.y, command_angles.z);
  client_think(&move);
  const qa_movement_state state = qa_q1_context_source_state(&move.context, wrap_state(&move));
  if (state.kind != QA_MOVEMENT_STATE_Q1) qa_range_error("NetQuake output changed movement dialect");
  const qa_q1_movement_state result = state.as.q1;
  qa_q1_movement_context_destroy(&move.context);
  return result;
}

static bool call_think(netquake_move *move, bool extension_think) {
  return extension_think ? !move->context.removed : move_think(move);
}

qa_movement_result qa_physics_netquake(qa_q1_movement_input input, qa_movement_services services,
                                        qa_q1_movement_options options) {
  netquake_move move;
  netquake_init(&move, input, services, options);
  qa_q1_movement_context *c = &move.context;
  qa_q1_movement_state *s = &move.state;
  /* Prepare and physics run as separate instances; recompute the ladder probe for the gravity gate. */
  move.ladder = move.ladder || (s->move_type == QA_Q1_MOVE_WALK && s->water_level < 2 &&
                                qa_q1_context_ladder_ahead(c, s->origin, qa_q1_math_angles(&c->math, s->view_angles).forward));
  set_state(&move, qa_q1_context_lifecycle(c, wrap_state(&move), QA_Q1_LIFECYCLE_BEFORE_PHYSICS, NULL));
  if (!c->removed && move.input.environment.pose == NULL) player_actions(&move);
  /* Preserve the existing extension paths; the branch-local order below is SV_Physics_Client's classic set. */
  const bool extension_think = s->move_type == QA_Q1_MOVE_STEP || s->move_type == QA_Q1_MOVE_FLYMISSILE ||
                               s->move_type == QA_Q1_MOVE_GIB;
  if (!c->removed && extension_think) (void)move_think(&move);
  if (!c->removed) {
    velocity_bounds(&move);
    if (move.input.environment.pose != NULL) {
      if (call_think(&move, extension_think)) {
        s->velocity = qa_q1_zero;
        (void)check_water(&move);
      }
    } else if (s->move_type == QA_Q1_MOVE_NONE) {
      (void)call_think(&move, extension_think);
    } else if (s->move_type == QA_Q1_MOVE_WALK) {
      if (call_think(&move, extension_think)) {
        if (!check_water(&move) && !move.ladder && !qa_q1_flag_test(s->flags, QA_Q1_FLAG_WATERJUMP)) gravity_step(&move);
        check_stuck(&move);
        if (!c->removed) walk_move(&move);
      }
    } else if (s->move_type == QA_Q1_MOVE_FLY) {
      if (call_think(&move, extension_think)) (void)fly_move(&move, move.frame_seconds);
    } else if (s->move_type == QA_Q1_MOVE_NOCLIP) {
      if (call_think(&move, extension_think)) s->origin = qa_q1_math_ma(&c->math, s->origin, move.frame_seconds, s->velocity);
    } else if (s->move_type == QA_Q1_MOVE_TOSS || s->move_type == QA_Q1_MOVE_BOUNCE) {
      if (call_think(&move, extension_think)) toss_step(&move);
    } else if (s->move_type == QA_Q1_MOVE_FLYMISSILE) {
      toss_step(&move);
    } else if (s->move_type == QA_Q1_MOVE_GIB) {
      if (!str_eq(move.input.profile.edition, "rerelease"))
        qa_range_error("MOVETYPE_GIB requires Quake rerelease behavior");
      toss_step(&move);
    } else if (s->move_type == QA_Q1_MOVE_STEP) {
      if (!qa_q1_flag_test(s->flags, QA_Q1_FLAG_ONGROUND | QA_Q1_FLAG_FLY | QA_Q1_FLAG_SWIM)) {
        const bool hit_sound =
            s->velocity.z < qa_q1_n_multiply(move.context.math.n, move.input.profile.parameters.gravity, -0.1);
        gravity_step(&move);
        velocity_bounds(&move);
        (void)fly_move(&move, move.frame_seconds);
        move_link(&move, true);
        const qa_q1_movement_hooks *hooks = c->options.hooks;
        if (!c->removed && hit_sound && qa_q1_flag_test(s->flags, QA_Q1_FLAG_ONGROUND) && hooks != NULL && hooks->sound != NULL)
          hooks->sound(hooks->ctx, move.input.actor, "demon/dland2.wav", *s);
      }
      if (!c->removed) water_transition(&move);
    } else {
      qa_range_error("Unsupported NetQuake player movetype %s", QA_JS_NUMBER(s->move_type));
    }
  }
  if (!c->removed) {
    move_link(&move, true);
    set_state(&move, qa_q1_context_lifecycle(c, wrap_state(&move), QA_Q1_LIFECYCLE_AFTER_PHYSICS, NULL));
  }
  qa_movement_result result;
  if (c->removed) {
    memset(&result, 0, sizeof result);
    result.kind = "q1-netquake";
    result.status = "actor-removed";
    result.actor = move.input.actor.id;
    result.command_sequence = move.input.command_sequence;
    qa_q1_context_take_effects(c, &result);
  } else {
    ideal_pitch(&move);
    result = qa_q1_finish_movement(c, wrap_state(&move), s->view_angles,
                                   qa_q1_flag_test(s->flags, QA_Q1_FLAG_ONGROUND) ? s->ground : qa_q1_none, s->water_level,
                                   s->water_type);
    if (!str_eq(result.kind, "q1-netquake")) qa_range_error("Weapon callback changed NetQuake movement family");
  }
  qa_q1_movement_context_destroy(c);
  return result;
}

qa_movement_result qa_move_netquake(qa_q1_movement_input input, qa_movement_services services,
                                     qa_q1_movement_options options) {
  const bool use_application = str_eq(input.execution, "authoritative") && services.has_input_application;
  if (!use_application) {
    qa_q1_movement_input prepared = input;
    prepared.state.as.q1 = qa_prepare_netquake(input, services, options);
    prepared.state.kind = QA_MOVEMENT_STATE_Q1;
    return qa_physics_netquake(prepared, services, options);
  }
  const qa_movement_input_application *application = &services.input_application;
  qa_q1_movement_input current = input;
  qa_movement_result result;
  /* TS catch: application end(current.state, true) then rethrow. C
   * movement errors abort, so no failure path reaches here. */
  const qa_movement_input_continuation before = application->begin(application->ctx, input.command, input.frame, input.state);
  if (!before.is_continue) {
    memset(&result, 0, sizeof result);
    result.kind = "q1-netquake";
    result.status = "actor-removed";
    result.actor = input.actor.id;
    result.command_sequence = input.command_sequence;
  } else {
    if (before.command.kind != QA_USER_COMMAND_Q1 || before.state.kind != QA_MOVEMENT_STATE_Q1)
      qa_range_error("Input output changed NetQuake dialect");
    current.command = before.command;
    current.state = before.state;
    qa_q1_movement_input prepared = current;
    prepared.state.as.q1 = qa_prepare_netquake(current, services, options);
    prepared.state.kind = QA_MOVEMENT_STATE_Q1;
    result = qa_physics_netquake(prepared, services, options);
  }
  const qa_movement_continuation after =
      application->end(application->ctx, str_eq(result.status, "active") ? result.state : current.state, false, false, NULL);
  if (!after.is_continue || str_eq(result.status, "actor-removed")) {
    if (str_eq(result.status, "active")) {
      /* The active result's taken arrays move into the removed result. */
      qa_movement_result removed;
      memset(&removed, 0, sizeof removed);
      removed.kind = "q1-netquake";
      removed.status = "actor-removed";
      removed.actor = input.actor.id;
      removed.command_sequence = input.command_sequence;
      removed.effects = result.effects;
      removed.effect_count = result.effect_count;
      free((void *)result.contacts);
      return removed;
    }
    result.kind = "q1-netquake";
    result.status = "actor-removed";
    result.actor = input.actor.id;
    result.command_sequence = input.command_sequence;
    return result;
  }
  if (after.state.kind != QA_MOVEMENT_STATE_Q1) qa_range_error("Input completion changed NetQuake dialect");
  result.state = after.state;
  return result;
}

typedef struct provider_options {
  qa_q1_movement_options options;
} provider_options;

static qa_movement_result provider_move(void *data, qa_movement_input input, qa_movement_services services) {
  const provider_options *held = data;
  return qa_move_netquake(input, services, held->options);
}

qa_movement_provider qa_create_q1_movement_provider(qa_provider_id id, qa_q1_movement_options options) {
  provider_options *held = malloc(sizeof *held);
  if (held == NULL) qa_range_error("Out of memory");
  held->options = options;
  qa_movement_provider provider;
  memset(&provider, 0, sizeof provider);
  provider.kind = "q1-netquake";
  provider.id = id;
  provider.ctx = held;
  provider.move = provider_move;
  return provider;
}
