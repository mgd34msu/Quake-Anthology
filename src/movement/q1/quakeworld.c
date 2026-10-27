/* Port of src/movement/q1/quakeworld.ts. GPL-2.0-or-later. */
#include "qa/movement/q1/quakeworld.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include "qa/movement/client_outputs.h"
#include "qa/movement/q1/common.h"
#include "qa/movement/q1/result.h"
#include "qa/movement/swept_body.h"

static bool is_integer(double value) { return isfinite(value) && trunc(value) == value; }
static bool str_eq(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

typedef struct slice_list {
  qa_qw_user_command *items;
  size_t count;
  size_t capacity;
} slice_list;

static void slice_append(slice_list *list, qa_qw_user_command command, double maximum_milliseconds) {
  if (maximum_milliseconds < 1) qa_range_error("QuakeWorld command interval must be positive");
  if (!is_integer(command.milliseconds) || command.milliseconds < 0 || command.milliseconds > 255)
    qa_range_error("QuakeWorld command milliseconds must fit its source byte");
  if (command.milliseconds > maximum_milliseconds) {
    const double milliseconds = floor(command.milliseconds / 2);
    qa_qw_user_command first = command;
    first.milliseconds = milliseconds;
    slice_append(list, first, maximum_milliseconds);
    qa_qw_user_command second = command;
    second.milliseconds = milliseconds;
    second.impulse = 0;
    slice_append(list, second, maximum_milliseconds);
    return;
  }
  if (list->count == list->capacity) {
    const size_t capacity = list->capacity == 0 ? 4 : list->capacity * 2;
    qa_qw_user_command *grown = realloc(list->items, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    list->items = grown;
    list->capacity = capacity;
  }
  list->items[list->count++] = command;
}

qa_qw_user_command *qa_quakeworld_command_slices(qa_qw_user_command command, double maximum_milliseconds,
                                                 size_t *out_count) {
  slice_list list = {NULL, 0, 0};
  slice_append(&list, command, maximum_milliseconds);
  *out_count = list.count;
  return list.items;
}

typedef struct quakeworld_move {
  qa_qw_movement_state state;
  qa_q1_movement_context context;
  qa_qw_movement_input input;
  double frame_seconds;
  qa_vec3 forward;
  qa_vec3 right;
  double water_level;
  double water_type;
  bool ladder;
  qa_trace_hit *touched;
  size_t touched_count;
  size_t touched_capacity;
  qa_qw_user_command command;
  qa_trace_result trace_scratch;
  int blocked;
} quakeworld_move;

static qa_movement_state wrap_state(const quakeworld_move *move) {
  qa_movement_state state;
  memset(&state, 0, sizeof state);
  state.kind = QA_MOVEMENT_STATE_QW;
  state.as.qw = move->state;
  return state;
}

static void set_state(quakeworld_move *move, qa_movement_state state) {
  if (state.kind != QA_MOVEMENT_STATE_QW)
    qa_range_error("QuakeWorld callback changed the movement provider during a command");
  move->state = state.as.qw;
  qa_mod_client_movement_mode mode;
  if (qa_client_movement_mode(move->input.environment.client_outputs, move->input.environment.health, &mode)) {
    qa_q1_context_project_client_mode(&move->context);
    move->state.spectator = qa_client_movement_type(QA_MOVEMENT_STATE_QW, mode);
  }
}

static bool sweep_read(void *data, qa_swept_body_state *out) {
  quakeworld_move *move = data;
  out->origin = move->state.origin;
  out->velocity = move->state.velocity;
  return true;
}
static void sweep_write_origin(void *data, qa_vec3 origin) { ((quakeworld_move *)data)->state.origin = origin; }
static void sweep_write_velocity(void *data, qa_vec3 velocity, qa_swept_velocity_components components) {
  (void)components;
  ((quakeworld_move *)data)->state.velocity = velocity;
}
static void sweep_trace(void *data, qa_vec3 start, qa_vec3 end, qa_swept_body_trace *out) {
  quakeworld_move *move = data;
  move->trace_scratch = qa_q1_context_trace(&move->context, start, end);
  out->fraction = move->trace_scratch.fraction;
  out->end = move->trace_scratch.end;
  out->all_solid = move->trace_scratch.all_solid;
  out->start_solid = move->trace_scratch.start_solid;
  out->native = &move->trace_scratch;
}
static qa_vec3 sweep_normal(void *data, const qa_swept_body_trace *trace) {
  (void)data;
  return qa_q1_trace_source_normal(trace->native);
}
static void sweep_impact(void *data, const qa_swept_body_trace *trace, qa_vec3 normal) {
  quakeworld_move *move = data;
  qa_q1_context_record_contact(&move->context, *(const qa_trace_result *)trace->native);
  if (normal.z > 0.7) move->blocked |= 1;
  if (normal.z == 0) move->blocked |= 2;
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

static int fly_move(quakeworld_move *move) {
  qa_qw_movement_state *s = &move->state;
  const qa_vec3 primal = s->velocity;
  move->blocked = 0;
  qa_swept_body_services services;
  memset(&services, 0, sizeof services);
  services.ctx = move;
  services.read = sweep_read;
  services.write_origin = sweep_write_origin;
  services.write_velocity = sweep_write_velocity;
  services.trace = sweep_trace;
  services.normal = sweep_normal;
  services.impact = sweep_impact;
  services.stop_when_still = false;
  services.same_plane = sweep_same_plane;
  services.has_collision_policy = true;
  services.stop_on_start_solid = true;
  services.original_velocity = QA_SWEPT_ORIGINAL_INITIAL;
  services.crease_velocity = QA_SWEPT_CREASE_LAST_CANDIDATE;
  services.math.ctx = &move->context.math;
  services.math.advance = sweep_advance;
  services.math.remaining = sweep_remaining;
  services.math.clip = sweep_clip;
  services.math.dot = sweep_dot;
  services.math.cross = sweep_cross;
  services.math.scale = sweep_scale;
  const qa_sweep_stop stop = qa_sweep_body(&services, move->frame_seconds);
  if (stop == QA_SWEEP_SOLID) return 3;
  if (s->water_jump_time_seconds != 0) s->velocity = primal;
  return move->blocked;
}

static void ground_move(quakeworld_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_qw_movement_state *s = &move->state;
  s->velocity = qa_q1_math_vec(m, s->velocity.x, s->velocity.y, 0);
  if (s->velocity.x == 0 && s->velocity.y == 0) return;
  const qa_vec3 destination = qa_q1_math_ma(m, s->origin, move->frame_seconds, s->velocity);
  qa_trace_result trace = qa_q1_context_trace(c, s->origin, destination);
  if (trace.fraction == 1) {
    s->origin = trace.end;
    return;
  }
  const qa_vec3 original = s->origin, original_velocity = s->velocity;
  (void)fly_move(move);
  const qa_vec3 down = s->origin, down_velocity = s->velocity;
  s->origin = original;
  s->velocity = original_velocity;
  const qa_vec3 step_up = {0, 0, QA_Q1_STEP_HEIGHT};
  trace = qa_q1_context_trace(c, s->origin, qa_q1_math_add(m, s->origin, step_up));
  if (!trace.start_solid && !trace.all_solid) s->origin = trace.end;
  (void)fly_move(move);
  const qa_vec3 step_down = {0, 0, -QA_Q1_STEP_HEIGHT};
  trace = qa_q1_context_trace(c, s->origin, qa_q1_math_add(m, s->origin, step_down));
  bool use_down = qa_q1_trace_source_normal(&trace).z < 0.7;
  if (!use_down) {
    if (!trace.start_solid && !trace.all_solid) s->origin = trace.end;
    const qa_vec3 down_delta = qa_q1_math_sub(m, down, original), up_delta = qa_q1_math_sub(m, s->origin, original);
    const double down_distance =
        qa_q1_n_add(n, qa_q1_n_multiply(n, down_delta.x, down_delta.x), qa_q1_n_multiply(n, down_delta.y, down_delta.y));
    const double up_distance =
        qa_q1_n_add(n, qa_q1_n_multiply(n, up_delta.x, up_delta.x), qa_q1_n_multiply(n, up_delta.y, up_delta.y));
    use_down = down_distance > up_distance;
  }
  if (use_down) {
    s->origin = down;
    s->velocity = down_velocity;
  } else s->velocity = qa_q1_math_vec(m, s->velocity.x, s->velocity.y, down_velocity.z);
}

static void friction_step(quakeworld_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_qw_movement_state *s = &move->state;
  const qa_q1_movement_parameters *p = &move->input.profile.parameters;
  if (s->water_jump_time_seconds != 0) return;
  const double speed = qa_q1_math_length(m, s->velocity);
  if (speed < 1) {
    s->velocity = qa_q1_math_vec(m, 0, 0, s->velocity.z);
    return;
  }
  double friction = p->friction;
  if (s->ground.kind != QA_TRACE_HIT_NONE) {
    const qa_bounds bounds = qa_q1_context_bounds(c);
    const qa_vec3 start = qa_q1_math_vec(m, qa_q1_n_add(n, s->origin.x, qa_q1_n_multiply(n, qa_q1_n_divide(n, s->velocity.x, speed), 16)),
                                         qa_q1_n_add(n, s->origin.y, qa_q1_n_multiply(n, qa_q1_n_divide(n, s->velocity.y, speed), 16)),
                                         qa_q1_n_add(n, s->origin.z, bounds.min.z));
    const qa_vec3 down34 = {0, 0, -34};
    /* QW uses the player hull here; NetQuake uses a point trace. */
    if (qa_q1_context_trace(c, start, qa_q1_math_add(m, start, down34)).fraction == 1)
      friction = qa_q1_n_multiply(n, friction, 2);
  }
  double drop = 0;
  if (move->water_level >= 2)
    drop = qa_q1_n_multiply(n, qa_q1_n_multiply(n, qa_q1_n_multiply(n, speed, p->water_friction), move->water_level), move->frame_seconds);
  else if (s->ground.kind != QA_TRACE_HIT_NONE)
    drop = qa_q1_n_multiply(n, qa_q1_n_multiply(n, qa_q1_js_max(speed, p->stop_speed), friction), move->frame_seconds);
  s->velocity = qa_q1_math_scale(m, s->velocity, qa_q1_n_divide(n, qa_q1_js_max(0, qa_q1_n_subtract(n, speed, drop)), speed));
}

static void accelerate_step(quakeworld_move *move, qa_vec3 direction, double speed, double acceleration, bool air) {
  const qa_q1_movement_math *m = &move->context.math;
  const qa_numeric_operations *n = m->n;
  qa_qw_movement_state *s = &move->state;
  if (s->dead || s->water_jump_time_seconds != 0) return;
  const double add = qa_q1_n_subtract(n, air ? qa_q1_js_min(speed, 30) : speed, qa_q1_math_dot(m, s->velocity, direction));
  if (add <= 0) return;
  const double amount = qa_q1_js_min(add, air ? qa_q1_n_multiply(n, qa_q1_n_multiply(n, acceleration, speed), move->frame_seconds)
                                              : qa_q1_n_multiply(n, qa_q1_n_multiply(n, acceleration, move->frame_seconds), speed));
  s->velocity = qa_q1_math_ma(m, s->velocity, amount, direction);
}

static qa_vec3 wish_velocity(quakeworld_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  const qa_qw_user_command *command = &move->command;
  const double forward_speed = qa_q1_context_speed(c, command->forward_move);
  const double side_speed = qa_q1_context_speed(c, command->side_move);
  return qa_q1_math_vec(m, qa_q1_n_add(n, qa_q1_n_multiply(n, move->forward.x, forward_speed), qa_q1_n_multiply(n, move->right.x, side_speed)),
                        qa_q1_n_add(n, qa_q1_n_multiply(n, move->forward.y, forward_speed), qa_q1_n_multiply(n, move->right.y, side_speed)),
                        qa_q1_n_add(n, qa_q1_n_multiply(n, move->forward.z, forward_speed), qa_q1_n_multiply(n, move->right.z, side_speed)));
}

static void water_move(quakeworld_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_qw_movement_state *s = &move->state;
  const qa_qw_user_command *command = &move->command;
  const qa_q1_movement_parameters *p = &move->input.profile.parameters;
  qa_vec3 wish = wish_velocity(move);
  wish = qa_q1_math_vec(m, wish.x, wish.y,
                        qa_q1_n_add(n, wish.z,
                                    command->forward_move == 0 && command->side_move == 0 && command->up_move == 0
                                        ? -60
                                        : qa_q1_context_speed(c, command->up_move)));
  const qa_q1_normalized normalized = qa_q1_math_normalize(m, wish);
  const double speed = qa_q1_n_multiply(n, qa_q1_js_min(normalized.length, qa_q1_context_speed(c, p->max_speed)), 0.7);
  accelerate_step(move, normalized.direction, speed, p->water_accelerate, false);
  const qa_vec3 destination = qa_q1_math_ma(m, s->origin, move->frame_seconds, s->velocity);
  const qa_vec3 raise = {0, 0, QA_Q1_STEP_HEIGHT + 1};
  const qa_vec3 start = qa_q1_math_add(m, destination, raise);
  const qa_trace_result trace = qa_q1_context_trace(c, start, destination);
  if (!trace.start_solid && !trace.all_solid) {
    s->origin = trace.end;
    return;
  }
  (void)fly_move(move);
}

static void ladder_move(quakeworld_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_qw_movement_state *s = &move->state;
  const qa_q1_movement_parameters *p = &move->input.profile.parameters;
  const qa_qw_user_command *command = &move->command;
  const qa_vec3 wish = wish_velocity(move);
  const qa_vec3 wish_vel =
      qa_q1_math_vec(m, wish.x, wish.y, qa_q1_ladder_wish_z(wish.z, command->forward_move, command->up_move, s->velocity.z));
  const qa_q1_normalized normalized = qa_q1_math_normalize(m, wish_vel);
  const double speed = qa_q1_js_min(normalized.length, qa_q1_context_speed(c, p->max_speed));
  accelerate_step(move, normalized.direction, speed, p->accelerate, false);
  if (wish_vel.z == 0) {
    const double gravity = qa_q1_n_multiply(
        n, qa_q1_n_multiply(n, qa_q1_n_multiply(n, p->entity_gravity, move->input.environment.gravity_multiplier), p->gravity),
        move->frame_seconds);
    if (s->velocity.z > 0)
      s->velocity = qa_q1_math_vec(m, s->velocity.x, s->velocity.y, qa_q1_js_max(0, qa_q1_n_subtract(n, s->velocity.z, gravity)));
    else
      s->velocity = qa_q1_math_vec(m, s->velocity.x, s->velocity.y, qa_q1_js_min(0, qa_q1_n_add(n, s->velocity.z, gravity)));
  }
  (void)fly_move(move);
}

static void air_move(quakeworld_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_qw_movement_state *s = &move->state;
  const qa_q1_movement_parameters *p = &move->input.profile.parameters;
  move->forward = qa_q1_math_normalize(m, qa_q1_math_vec(m, move->forward.x, move->forward.y, 0)).direction;
  move->right = qa_q1_math_normalize(m, qa_q1_math_vec(m, move->right.x, move->right.y, 0)).direction;
  const qa_q1_normalized wish = qa_q1_math_normalize(m, wish_velocity(move));
  const double speed = qa_q1_js_min(wish.length, qa_q1_context_speed(c, p->max_speed));
  const double gravity = qa_q1_n_multiply(
      n, qa_q1_n_multiply(n, qa_q1_n_multiply(n, p->entity_gravity, move->input.environment.gravity_multiplier), p->gravity),
      move->frame_seconds);
  if (s->ground.kind != QA_TRACE_HIT_NONE) {
    s->velocity = qa_q1_math_vec(m, s->velocity.x, s->velocity.y, 0);
    accelerate_step(move, wish.direction, speed, p->accelerate, false);
    s->velocity = qa_q1_math_vec(m, s->velocity.x, s->velocity.y, qa_q1_n_subtract(n, s->velocity.z, gravity));
    ground_move(move);
  } else {
    /* Original pmove.c uses accelerate here; airaccelerate is transmitted but unused. */
    accelerate_step(move, wish.direction, speed, p->accelerate, true);
    s->velocity = qa_q1_math_vec(m, s->velocity.x, s->velocity.y, qa_q1_n_subtract(n, s->velocity.z, gravity));
    (void)fly_move(move);
  }
}

static void categorize(quakeworld_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_qw_movement_state *s = &move->state;
  if (s->velocity.z > 180) s->ground = qa_q1_none;
  else {
    const qa_vec3 down1 = {0, 0, -1};
    const qa_trace_result trace = qa_q1_context_trace(c, s->origin, qa_q1_math_add(m, s->origin, down1));
    s->ground = qa_q1_trace_source_normal(&trace).z < 0.7 ? qa_q1_none : trace.hit;
    if (s->ground.kind != QA_TRACE_HIT_NONE) {
      s->water_jump_time_seconds = 0;
      if (!trace.start_solid && !trace.all_solid) s->origin = trace.end;
    }
    if (trace.hit.kind == QA_TRACE_HIT_ACTOR) qa_q1_context_record_contact(c, trace);
  }
  move->ladder = s->water_jump_time_seconds == 0 && qa_q1_context_ladder_ahead(c, s->origin, move->forward);
  move->water_level = 0;
  move->water_type = QA_Q1_CONTENTS_EMPTY;
  const qa_bounds bounds = qa_q1_context_bounds(c);
  qa_vec3 point = qa_q1_math_vec(m, s->origin.x, s->origin.y, qa_q1_n_add(n, qa_q1_n_add(n, s->origin.z, bounds.min.z), 1));
  const double contents = qa_q1_context_contents(c, point);
  if (contents > QA_Q1_CONTENTS_WATER) return;
  move->water_type = contents;
  move->water_level = 1;
  point = qa_q1_math_vec(m, point.x, point.y,
                         qa_q1_n_add(n, s->origin.z, qa_q1_n_multiply(n, qa_q1_n_add(n, bounds.min.z, bounds.max.z), 0.5)));
  if (qa_q1_context_contents(c, point) > QA_Q1_CONTENTS_WATER) return;
  move->water_level = 2;
  point = qa_q1_math_vec(m, point.x, point.y, qa_q1_n_add(n, s->origin.z, qa_q1_context_view_height(c)));
  if (qa_q1_context_contents(c, point) <= QA_Q1_CONTENTS_WATER) move->water_level = 3;
}

static void jump_step(quakeworld_move *move) {
  const qa_q1_movement_math *m = &move->context.math;
  const qa_numeric_operations *n = m->n;
  qa_qw_movement_state *s = &move->state;
  if (s->dead) {
    s->old_buttons = (double)(qa_int32(s->old_buttons) | 2);
    return;
  }
  if (s->water_jump_time_seconds != 0) {
    s->water_jump_time_seconds = qa_q1_js_max(0, qa_q1_n_subtract(n, s->water_jump_time_seconds, move->frame_seconds));
    return;
  }
  if (move->water_level >= 2) {
    s->ground = qa_q1_none;
    s->velocity = qa_q1_math_vec(m, s->velocity.x, s->velocity.y,
                                 move->water_type == QA_Q1_CONTENTS_WATER ? 100 : move->water_type == QA_Q1_CONTENTS_SLIME ? 80 : 50);
    return;
  }
  if (s->ground.kind == QA_TRACE_HIT_NONE || (qa_int32(s->old_buttons) & 2) != 0) return;
  s->ground = qa_q1_none;
  s->velocity = qa_q1_math_vec(m, s->velocity.x, s->velocity.y, qa_q1_n_add(n, s->velocity.z, 270));
  s->old_buttons = (double)(qa_int32(s->old_buttons) | 2);
}

static void check_water_jump(quakeworld_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  qa_qw_movement_state *s = &move->state;
  if (s->water_jump_time_seconds != 0 || s->velocity.z < -180) return;
  const qa_vec3 forward = qa_q1_math_normalize(m, qa_q1_math_vec(m, move->forward.x, move->forward.y, 0)).direction;
  const qa_vec3 up8 = {0, 0, 8}, up24 = {0, 0, 24};
  const qa_vec3 spot = qa_q1_math_add(m, qa_q1_math_ma(m, s->origin, 24, forward), up8);
  if (qa_q1_context_contents(c, spot) != QA_Q1_CONTENTS_SOLID ||
      qa_q1_context_contents(c, qa_q1_math_add(m, spot, up24)) != QA_Q1_CONTENTS_EMPTY)
    return;
  const qa_vec3 velocity = qa_q1_math_scale(m, forward, 50);
  s->velocity = qa_q1_math_vec(m, velocity.x, velocity.y, 310);
  s->water_jump_time_seconds = 2;
  s->old_buttons = (double)(qa_int32(s->old_buttons) | 2);
}

static void nudge(quakeworld_move *move) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  qa_qw_movement_state *s = &move->state;
  const qa_vec3 base = s->origin;
  static const int offsets[3] = {0, -1, 1};
  /* Source loops use the unsnapped base, including the initial zero-offset try. */
  for (int zi = 0; zi < 3; zi++)
    for (int xi = 0; xi < 3; xi++)
      for (int yi = 0; yi < 3; yi++) {
        const qa_vec3 offset = {offsets[xi] / 8.0, offsets[yi] / 8.0, offsets[zi] / 8.0};
        s->origin = qa_q1_math_add(m, base, offset);
        if (qa_q1_context_position_free(c, s->origin)) return;
      }
  s->origin = base;
}

static void spectator_move(quakeworld_move *move, bool collide) {
  qa_q1_movement_context *c = &move->context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_qw_movement_state *s = &move->state;
  const qa_q1_movement_parameters *p = &move->input.profile.parameters;
  const double speed = qa_q1_math_length(m, s->velocity);
  if (speed < 1) s->velocity = qa_q1_zero;
  else {
    const double friction = qa_q1_n_multiply(n, p->friction, 1.5);
    const double drop = qa_q1_n_multiply(n, qa_q1_n_multiply(n, qa_q1_js_max(speed, p->stop_speed), friction), move->frame_seconds);
    s->velocity = qa_q1_math_scale(m, s->velocity, qa_q1_n_divide(n, qa_q1_js_max(0, qa_q1_n_subtract(n, speed, drop)), speed));
  }
  move->forward = qa_q1_math_normalize(m, move->forward).direction;
  move->right = qa_q1_math_normalize(m, move->right).direction;
  qa_vec3 wish = wish_velocity(move);
  wish = qa_q1_math_vec(m, wish.x, wish.y, qa_q1_n_add(n, wish.z, qa_q1_context_speed(c, move->command.up_move)));
  const qa_q1_normalized normalized = qa_q1_math_normalize(m, wish);
  const double wish_speed =
      qa_q1_js_min(normalized.length, collide ? qa_q1_context_speed(c, p->max_speed) : qa_q1_context_speed(c, p->spectator_max_speed));
  const double add = qa_q1_n_subtract(n, wish_speed, qa_q1_math_dot(m, s->velocity, normalized.direction));
  /* This early return also skips origin integration in original SpectatorMove. */
  if (add <= 0 && !collide) return;
  const double acceleration =
      qa_q1_js_max(0, qa_q1_js_min(add, qa_q1_n_multiply(n, qa_q1_n_multiply(n, p->accelerate, move->frame_seconds), wish_speed)));
  s->velocity = qa_q1_math_ma(m, s->velocity, acceleration, normalized.direction);
  if (collide) (void)fly_move(move);
  else s->origin = qa_q1_math_ma(m, s->origin, move->frame_seconds, s->velocity);
}

static bool already_touched(const quakeworld_move *move, qa_trace_hit hit) {
  for (size_t i = 0; i < move->touched_count; i++) {
    const qa_trace_hit prior = move->touched[i];
    if (prior.kind == QA_TRACE_HIT_WORLD && hit.kind == QA_TRACE_HIT_WORLD) {
      if (prior.model == hit.model) return true;
    } else if (prior.kind == QA_TRACE_HIT_ACTOR && hit.kind == QA_TRACE_HIT_ACTOR) {
      if (qa_same_actor(prior.actor, hit.actor)) return true;
    }
  }
  return false;
}

static void push_touched(quakeworld_move *move, qa_trace_hit hit) {
  if (move->touched_count == move->touched_capacity) {
    const size_t capacity = move->touched_capacity == 0 ? 8 : move->touched_capacity * 2;
    qa_trace_hit *grown = realloc(move->touched, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    move->touched = grown;
    move->touched_capacity = capacity;
  }
  move->touched[move->touched_count++] = hit;
}

static qa_frame_context step_frame(const quakeworld_move *move, qa_qw_user_command command) {
  qa_frame_context frame = move->input.frame;
  frame.elapsed.kind = QA_SOURCE_TIME_MILLISECONDS;
  frame.elapsed.value = command.milliseconds;
  return frame;
}

static void step_physics(quakeworld_move *move, qa_qw_user_command command) {
  qa_q1_movement_context *c = &move->context;
  qa_qw_movement_state *s = &move->state;
  move->command = command;
  move->frame_seconds = qa_q1_n_multiply(c->math.n, command.milliseconds, 0.001);
  qa_q1_player_input lifecycle_input = move->input;
  lifecycle_input.command.kind = QA_USER_COMMAND_QW;
  lifecycle_input.command.as.qw = command;
  lifecycle_input.frame = step_frame(move, command);
  set_state(move, qa_q1_context_lifecycle(c, wrap_state(move), QA_Q1_LIFECYCLE_BEFORE_PHYSICS, &lifecycle_input));
  if (c->removed) return;
  const qa_mod_client_movement_outputs *outputs = move->input.environment.client_outputs;
  qa_user_command stance_command;
  memset(&stance_command, 0, sizeof stance_command);
  stance_command.kind = QA_USER_COMMAND_QW;
  stance_command.as.qw = command;
  const qa_user_command effective = qa_client_stance_command(
      stance_command, outputs != NULL && outputs->has_stance, outputs != NULL && outputs->stance);
  if (effective.kind != QA_USER_COMMAND_QW) qa_range_error("Client stance changed movement dialect");
  command = effective.as.qw;
  move->command = command;
  /* CL_PredictUsercmd seeds pmove.angles before PlayerMove computes its axes. */
  if (str_eq(move->input.execution, "prediction"))
    s->angles = qa_q1_math_vec(&c->math, command.angles.x, command.angles.y, command.angles.z);
  const qa_angle_vectors axes = qa_q1_math_angles(&c->math, s->angles);
  move->forward = axes.forward;
  move->right = axes.right;
  qa_mod_client_movement_mode mode;
  const bool has_mode = qa_client_movement_mode(outputs, move->input.environment.health, &mode);
  if (has_mode && mode == QA_MOD_CLIENT_MOVEMENT_FREEZE) {
    s->velocity = qa_q1_zero;
    return;
  }
  if (s->spectator != 0) {
    spectator_move(move, false);
    return;
  }
  const size_t contact_start = c->contact_count;
  if (move->input.environment.pose == NULL) nudge(move);
  s->angles = qa_q1_math_vec(&c->math, command.angles.x, command.angles.y, command.angles.z);
  categorize(move);
  if (move->input.environment.pose != NULL) s->velocity = qa_q1_zero;
  else if (move->input.environment.flight && move->input.environment.health > 0) {
    s->ground = qa_q1_none;
    s->water_jump_time_seconds = 0;
    spectator_move(move, true);
  } else {
    if (move->water_level == 2) check_water_jump(move);
    if (s->velocity.z < 0) s->water_jump_time_seconds = 0;
    if ((qa_int32(command.buttons) & 2) != 0) jump_step(move);
    else s->old_buttons = (double)(qa_int32(s->old_buttons) & ~2);
    friction_step(move);
    if (move->water_level >= 2) water_move(move);
    else if (move->ladder) ladder_move(move);
    else air_move(move);
  }
  categorize(move);
  const qa_q1_movement_hooks *hooks = c->options.hooks;
  if (hooks != NULL && hooks->qw_state != NULL) hooks->qw_state(hooks->ctx, move->water_level, move->water_type);
  set_state(move, qa_q1_context_link(c, wrap_state(move), true));
  /* QW records impacts during PMove, then invokes each target once after linking. */
  const size_t contact_end = c->contact_count;
  for (size_t i = contact_start; i < contact_end; i++) {
    if (c->removed) break;
    const qa_movement_contact contact = c->contacts[i];
    if (contact.target.kind == QA_TRACE_HIT_NONE || already_touched(move, contact.target)) continue;
    push_touched(move, contact.target);
    set_state(move, qa_q1_context_touch(c, contact.trace, wrap_state(move), false));
  }
}

static void step(quakeworld_move *move, qa_qw_user_command command) {
  qa_q1_movement_context *c = &move->context;
  const bool use_application = str_eq(move->input.execution, "authoritative") && c->services.has_input_application;
  if (!use_application) {
    step_physics(move, command);
    return;
  }
  const qa_movement_input_application *application = &c->services.input_application;
  /* TS catch: application end(state, true) then rethrow; C errors abort. */
  qa_user_command begin_command;
  memset(&begin_command, 0, sizeof begin_command);
  begin_command.kind = QA_USER_COMMAND_QW;
  begin_command.as.qw = command;
  const qa_movement_input_continuation before =
      application->begin(application->ctx, begin_command, step_frame(move, command), wrap_state(move));
  if (!before.is_continue) c->removed = true;
  else {
    if (before.command.kind != QA_USER_COMMAND_QW) qa_range_error("Input output changed command dialect");
    set_state(move, before.state);
    step_physics(move, before.command.as.qw);
  }
  const qa_movement_continuation after = application->end(application->ctx, wrap_state(move), false, false, NULL);
  if (!after.is_continue) c->removed = true;
  else set_state(move, after.state);
}

static void run_command(quakeworld_move *move, qa_qw_user_command command) {
  if (move->context.removed) return;
  if (move->input.profile.clock.kind != QA_CLOCK_Q1_QUAKEWORLD)
    qa_range_error("QuakeWorld movement needs a QuakeWorld command clock");
  size_t slice_count = 0;
  qa_qw_user_command *slices =
      qa_quakeworld_command_slices(command, move->input.profile.clock.maximum_command_milliseconds, &slice_count);
  for (size_t i = 0; i < slice_count; i++) {
    if (move->context.removed) break;
    step(move, slices[i]);
    move->context.substep += 1;
  }
  free(slices);
}

qa_movement_result qa_move_quakeworld(qa_qw_movement_input input, qa_movement_services services,
                                      qa_q1_movement_options options) {
  quakeworld_move move;
  memset(&move, 0, sizeof move);
  move.input = input;
  move.state = input.state.as.qw;
  move.command = input.command.as.qw;
  move.water_type = QA_Q1_CONTENTS_EMPTY;
  move.forward = qa_q1_zero;
  move.right = qa_q1_zero;
  qa_q1_movement_context_init(&move.context, input, services, options);
  if (!is_integer(input.command.as.qw.milliseconds) || input.command.as.qw.milliseconds < 0 ||
      input.command.as.qw.milliseconds > 255)
    qa_range_error("QuakeWorld command milliseconds must fit its source byte");
  run_command(&move, input.command.as.qw);
  set_state(&move, qa_q1_context_lifecycle(&move.context, wrap_state(&move), QA_Q1_LIFECYCLE_AFTER_PHYSICS, NULL));
  qa_movement_result result;
  if (move.context.removed) {
    memset(&result, 0, sizeof result);
    result.kind = "q1-quakeworld";
    result.status = "actor-removed";
    result.actor = input.actor.id;
    result.command_sequence = input.command_sequence;
    qa_q1_context_take_effects(&move.context, &result);
  } else {
    result = qa_q1_finish_movement(&move.context, wrap_state(&move), move.state.angles, move.state.ground,
                                   move.water_level, move.water_type);
    if (!str_eq(result.kind, "q1-quakeworld")) qa_range_error("Weapon callback changed QuakeWorld movement family");
  }
  qa_q1_movement_context_destroy(&move.context);
  free(move.touched);
  return result;
}

typedef struct provider_options {
  qa_q1_movement_options options;
} provider_options;

static qa_movement_result provider_move(void *data, qa_movement_input input, qa_movement_services services) {
  const provider_options *held = data;
  return qa_move_quakeworld(input, services, held->options);
}

qa_movement_provider qa_create_qw_movement_provider(qa_provider_id id, qa_q1_movement_options options) {
  provider_options *held = malloc(sizeof *held);
  if (held == NULL) qa_range_error("Out of memory");
  held->options = options;
  qa_movement_provider provider;
  memset(&provider, 0, sizeof provider);
  provider.kind = "q1-quakeworld";
  provider.id = id;
  provider.ctx = held;
  provider.move = provider_move;
  return provider;
}
