/* Port of src/content/q2/foundation/monsters/alternate-fly.ts.
 * Math.fround staging: computed in double, cast to float at the TS
 * Math.fround points. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/monsters/alternate_fly.h"
#include "qa/foundation/js.h"

#include <math.h>

#include "monsters_internal.h"

static float f32(double v) { return (float)v; }
static double ts_minimum(double a, double b) { return b < a ? b : a; }
static double ts_maximum(double a, double b) { return a < b ? b : a; }
static qa_vec3 fv_add(qa_vec3 a, qa_vec3 b) { return q2m_v3(f32(a.x + b.x), f32(a.y + b.y), f32(a.z + b.z)); }
static qa_vec3 fv_sub(qa_vec3 a, qa_vec3 b) { return q2m_v3(f32(a.x - b.x), f32(a.y - b.y), f32(a.z - b.z)); }
static qa_vec3 fv_scale(qa_vec3 a, double s) { return q2m_v3(f32(a.x * s), f32(a.y * s), f32(a.z * s)); }
static qa_vec3 fv_scaled(qa_vec3 a, qa_vec3 b) { return q2m_v3(f32(a.x * b.x), f32(a.y * b.y), f32(a.z * b.z)); }
static double fv_dot(qa_vec3 a, qa_vec3 b) { return f32(f32(f32(a.x * b.x) + f32(a.y * b.y)) + f32(a.z * b.z)); }
static bool fv_nonzero(qa_vec3 v) { return v.x != 0 || v.y != 0 || v.z != 0; }
static bool fv_nan(qa_vec3 v) { return isnan(v.x) || isnan(v.y) || isnan(v.z); }
typedef struct fnormal { qa_vec3 direction; double length; } fnormal;
static fnormal normal_of(qa_vec3 v) {
  double length = f32(sqrt(fv_dot(v, v)));
  fnormal n = {length == 0 ? v : fv_scale(v, f32(1.0 / length)), length};
  return n;
}
static double pi_f(void) { return f32(3.141592653589793); }
static double fv_yaw(qa_vec3 v) {
  if (v.x == 0) return v.y == 0 ? 0 : v.y > 0 ? 90 : 270;
  double angle = f32(f32(atan2(v.y, v.x)) * f32(180.0 / pi_f()));
  return angle < 0 ? f32(angle + 360) : angle;
}
static double fv_pitch(qa_vec3 v) {
  if (v.x == 0 && v.y == 0) return v.z > 0 ? -90 : -270;
  double forward = f32(sqrt(f32(f32(v.x * v.x) + f32(v.y * v.y))));
  double angle = f32(f32(atan2(v.z, forward)) * f32(180.0 / pi_f()));
  return -(angle < 0 ? f32(angle + 360) : angle);
}
static qa_vec3 fv_slerp(qa_vec3 from, qa_vec3 to, double t) {
  double product = fv_dot(from, to);
  double a = f32(1 - t), b = t;
  if (!(fabs(product) > f32(0.9995))) {
    double angle = f32(acos(product)), sine = f32(sin(angle));
    a = f32(f32(sin(f32(f32(1 - t) * angle))) / sine);
    b = f32(f32(sin(f32(t * angle))) / sine);
  }
  return fv_add(fv_scale(from, a), fv_scale(to, b));
}
static const qa_bounds fit_bounds = {{-8, -8, -8}, {8, 8, 8}};
#define SOLID_MASK (1 | 2 | 0x20000)

static qa_vec3 ideal_hover(qa_q2m_alternate_fly_input input, qa_q2_rerelease_random *random) {
  qa_q2m_state *state = input.state;
  qa_q2m_alternate_fly_state *alt = &state->alt;
  if ((input.enemy == NULL && !state->medic) || state->combat_point || state->has_sound_target || alt->hint_path || alt->has_pathing)
    return q2m_zero();
  double theta = qa_q2_rerelease_random_float_max(random, f32(2 * pi_f()));
  double phi = f32(acos(alt->fly_above ? f32(f32(0.7) + qa_q2_rerelease_random_float_max(random, f32(0.3)))
                                       : alt->fly_buzzard || state->medic
                                           ? qa_q2_rerelease_random_float_unit(random)
                                           : f32(qa_q2_rerelease_random_float_range(random, -1, 1) * f32(0.06))));
  qa_vec3 dir = q2m_v3(f32(f32(sin(phi)) * f32(cos(theta))), f32(f32(sin(phi)) * f32(sin(theta))), f32(cos(phi)));
  return fv_scale(dir, qa_q2_rerelease_random_float_range(random, alt->fly_min_distance, alt->fly_max_distance));
}

qa_q2m_alternate_fly_result qa_q2m_steer_alternate_fly(qa_q2m_alternate_fly_input input,
                                                       const qa_q2m_alternate_fly_services *services) {
  qa_q2m_state *state = input.state;
  qa_q2m_alternate_fly_state *alt = &state->alt;
  qa_body_state body = input.body;
  const qa_body_state *enemy = input.enemy;
  qa_q2m_alternate_fly_result result;
  memset(&result, 0, sizeof result);
  if ((qa_js_to_uint32(input.flags) & 2u) != 0 && state->water_level < 3) {
    result.kind = QA_Q2M_FLY_STEERED; result.velocity = body.velocity; result.has_pitch = false;
    return result;
  }
  if (alt->fly_position_time <= input.now || (enemy != NULL && alt->fly_pinned && !services->visible_enemy(services->ctx))) {
    alt->fly_pinned = false;
    alt->fly_position_time = (qa_js_round(input.now * 1000) + qa_q2_rerelease_random_time_milliseconds(services->random, 3000, 10000)) / 1000;
    alt->fly_ideal_position = ideal_hover(input, services->random);
  }
  fnormal initial = normal_of(body.velocity);
  qa_vec3 direction = initial.direction;
  double current_speed = initial.length;
  if (fv_nan(direction)) { result.kind = QA_Q2M_FLY_FALLBACK; return result; }
  qa_vec3 target, target_velocity = q2m_zero();
  bool has_target = true;
  if (alt->has_pathing) target = alt->pathing.traversal_pending ? alt->pathing.second_move_point : alt->pathing.first_move_point;
  else if (enemy != NULL && !state->combat_point && !state->has_sound_target && !state->lost_sight) {
    target = enemy->origin; target_velocity = enemy->velocity;
  } else if (input.goal != NULL) target = *input.goal;
  else has_target = false;
  if (!has_target) {
    if (current_speed > 0) current_speed = ts_maximum(0, f32(current_speed - alt->fly_acceleration));
    else if (current_speed < 0) current_speed = ts_minimum(0, f32(current_speed + alt->fly_acceleration));
    result.kind = QA_Q2M_FLY_STEERED;
    result.velocity = current_speed == initial.length ? body.velocity : fv_scale(direction, current_speed);
    return result;
  }
  qa_vec3 wanted_position = alt->fly_pinned ? alt->fly_ideal_position
    : (alt->has_pathing || state->combat_point || state->has_sound_target || state->lost_sight)
        ? target : fv_add(fv_add(target, fv_scale(target_velocity, f32(0.25))), alt->fly_ideal_position);
  qa_trace_result fit = services->trace(services->ctx, target, wanted_position, &fit_bounds, SOLID_MASK);
  if (!fit.all_solid) wanted_position = fit.end;
  qa_vec3 difference = fv_sub(wanted_position, body.origin);
  if (difference.z > body.bounds.min.z && difference.z < body.bounds.max.z) difference.z = 0;
  fnormal wanted = normal_of(difference);
  qa_vec3 wanted_direction = wanted.direction;
  if (!state->manual_steering) state->ideal_yaw = fv_yaw(normal_of(fv_sub(target, body.origin)).direction);
  qa_trace_result obstruction = services->trace(services->ctx, body.origin,
                                                fv_add(body.origin, fv_scale(wanted_direction, alt->fly_acceleration)), &body.bounds, SOLID_MASK);
  double angle = f32(body.angles.y * f32(f32(pi_f() * 2) / 360));
  double sine = f32(sin(angle)), cosine = f32(cos(angle));
  qa_vec3 forward = q2m_v3(cosine, sine, -0.0), right = q2m_v3(sine, -cosine, -0.0);
  if (obstruction.fraction < f32(0.25)) {
    bool vis[2];
    double heights[2] = {body.bounds.min.z, body.bounds.max.z};
    double ends[2] = {f32(body.bounds.min.z - alt->fly_acceleration), f32(body.bounds.max.z + alt->fly_acceleration)};
    for (int i = 0; i < 2; i++) {
      qa_vec3 start = fv_add(body.origin, q2m_v3(0, 0, heights[i]));
      vis[i] = services->trace(services->ctx, start, wanted_position, NULL, SOLID_MASK).fraction == 1
            && services->trace(services->ctx, body.origin, fv_add(body.origin, q2m_v3(0, 0, ends[i])), &body.bounds, SOLID_MASK).fraction == 1;
    }
    bool bottom_visible = vis[0], top_visible = vis[1];
    if (bottom_visible == top_visible) {
      qa_vec3 front = fv_add(body.origin, fv_scaled(forward, body.bounds.max));
      qa_vec3 side = fv_scaled(right, body.bounds.max);
      bool left_visible = services->trace(services->ctx, fv_sub(front, side), wanted_position, NULL, SOLID_MASK).fraction == 1;
      bool right_visible = services->trace(services->ctx, fv_add(front, side), wanted_position, NULL, SOLID_MASK).fraction == 1;
      if (left_visible != right_visible) wanted_direction = right_visible ? fv_add(wanted_direction, right) : fv_sub(wanted_direction, right);
      else {
        qa_vec3 n = {obstruction.source_plane.plane.normal.x, obstruction.source_plane.plane.normal.y, obstruction.source_plane.plane.normal.z};
        wanted_direction = n;
      }
    } else wanted_direction = top_visible ? fv_add(wanted_direction, q2m_v3(0, 0, 1)) : fv_sub(wanted_direction, q2m_v3(0, 0, 1));
    wanted_direction = normal_of(wanted_direction).direction;
  }
  bool direct = (alt->fly_thrusters && !alt->fly_pinned) || alt->has_pathing || state->combat_point || state->lost_sight;
  double turn_factor = direct && fv_dot(direction, wanted_direction) > 0 ? f32(0.45)
                       : ts_minimum(1, f32(f32(0.84) + f32(f32(0.08) * f32(current_speed / alt->fly_speed))));
  qa_vec3 final_direction = fv_nonzero(direction) ? direction : wanted_direction;
  if (fv_nan(final_direction)) { result.kind = QA_Q2M_FLY_FALLBACK; return result; }
  bool swimming = (qa_js_to_uint32(input.flags) & 2u) != 0, flying = (qa_js_to_uint32(input.flags) & 1u) != 0;
  bool avoid_water = swimming || (flying && state->water_level < 3);
  bool water_ahead = avoid_water && (qa_js_to_uint32(services->point_contents(services->ctx, fv_add(body.origin, fv_scale(wanted_direction, current_speed)))) & 32u) != 0;
  bool bad_direction = swimming ? !water_ahead : (flying && state->water_level < 3 && water_ahead);
  if (bad_direction) {
    if (alt->fly_recovery_time < input.now) {
      double rx = qa_q2_rerelease_random_float_range(services->random, -1, 1);
      double ry = qa_q2_rerelease_random_float_range(services->random, -1, 1);
      double rz = qa_q2_rerelease_random_float_range(services->random, -1, 1);
      alt->fly_recovery_direction = normal_of(q2m_v3(rx, ry, rz)).direction;
      alt->fly_recovery_time = (qa_js_round(input.now * 1000) + 1000) / 1000;
    }
    wanted_direction = alt->fly_recovery_direction;
  }
  if (fv_nonzero(direction) && turn_factor > 0) final_direction = normal_of(fv_slerp(direction, wanted_direction, f32(1 - turn_factor))).direction;
  double speed_factor = enemy == NULL || direct ? 1
    : fv_dot(forward, wanted_direction) < -0.25 && fv_nonzero(direction) ? 0 : ts_minimum(1, f32(wanted.length / alt->fly_speed));
  if (bad_direction) speed_factor = -speed_factor;
  double acceleration = alt->fly_acceleration;
  if (fv_dot(final_direction, wanted_direction) < f32(0.25)) acceleration = f32(acceleration * 2);
  double wanted_speed = state->manual_steering ? 0 : f32(alt->fly_speed * speed_factor);
  if (current_speed > wanted_speed) current_speed = ts_maximum(wanted_speed, f32(current_speed - acceleration));
  else if (current_speed < wanted_speed) current_speed = ts_minimum(wanted_speed, f32(current_speed + acceleration));
  if (fv_nan(final_direction) || isnan(current_speed)) { result.kind = QA_Q2M_FLY_FALLBACK; return result; }
  double new_pitch = 0;
  if (enemy != NULL && (alt->fly_buzzard || state->medic)) {
    double desired = -fv_pitch(normal_of(fv_sub(body.origin, target)).direction);
    if (f32(desired - body.angles.x) > 180) desired = f32(desired - 360);
    if (f32(desired - body.angles.x) < -180) desired = f32(desired + 360);
    new_pitch = f32(body.angles.x + f32(f32(f32(input.frame_seconds) * 4) * f32(desired - body.angles.x)));
  }
  result.kind = QA_Q2M_FLY_STEERED;
  result.velocity = fv_scale(final_direction, current_speed);
  result.has_pitch = true; result.pitch = new_pitch;
  return result;
}

typedef struct step_env {
  qa_q2_game_services *game; qa_q2_entity *entity; qa_body_state body;
  bool has_enemy; qa_body_state enemy; qa_actor_id enemy_id;
} step_env;
static qa_trace_result step_trace(void *ctx, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds, double mask) {
  step_env *e = ctx;
  return q2m_trace(e->game, start, end, bounds, &e->entity->actor.id, mask);
}
static double step_contents(void *ctx, qa_vec3 point) {
  step_env *e = ctx;
  return e->game->host->point_contents(e->game->host->ctx, point);
}
static bool step_visible(void *ctx) {
  step_env *e = ctx;
  if (!e->has_enemy) return false;
  qa_q2_entity *enemy_entity = e->game->entity(e->game->ctx, &e->enemy_id);
  double vh = enemy_entity ? enemy_entity->view_height : 22;
  /* TS alternateFlyStep uses this module's binary32 `add` for both points. */
  qa_vec3 start = fv_add(e->body.origin, q2m_v3(0, 0, e->entity->view_height));
  qa_vec3 end = fv_add(e->enemy.origin, q2m_v3(0, 0, vh));
  return q2m_trace(e->game, start, end, NULL, &e->entity->actor.id, 1 | 8 | 16).fraction == 1;
}
bool qa_q2m_alternate_fly_step(qa_q2m_context *context) {
  qa_q2_game_services *game = context->game;
  qa_q2_entity *entity = context->entity;
  qa_body_state body = q2m_body(game, entity);
  qa_q2_rerelease_random *random = game->host->rerelease_random;
  if (!random) qa_raise("Error", 0, 0, "Rerelease alternate flying requires the shared rerelease random stream");
  qa_body_state_result enemy_r = {false, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {{0, 0, 0}, {0, 0, 0}}, NULL}};
  if (entity->has_enemy) enemy_r = qa_shared_body_table_read(game->host->bodies, entity->enemy);
  qa_vec3 goal_origin;
  bool has_goal = false;
  if (entity->has_goal) {
    qa_body_state_result goal_r = qa_shared_body_table_read(game->host->bodies, entity->goal);
    if (goal_r.has_value) { goal_origin = goal_r.value.origin; has_goal = true; }
  }
  step_env env;
  env.game = game; env.entity = entity; env.body = body;
  env.has_enemy = enemy_r.has_value;
  if (enemy_r.has_value) { env.enemy = enemy_r.value; env.enemy_id = entity->enemy; }
  qa_q2m_alternate_fly_input input;
  input.state = context->state; input.body = body;
  input.enemy = enemy_r.has_value ? &enemy_r.value : NULL;
  input.goal = has_goal ? &goal_origin : NULL;
  input.flags = entity->flags; input.now = q2m_now(game);
  input.frame_seconds = game->host->frame_seconds(game->host->ctx);
  qa_q2m_alternate_fly_services services;
  services.random = random; services.ctx = &env;
  services.trace = step_trace; services.point_contents = step_contents; services.visible_enemy = step_visible;
  qa_q2m_alternate_fly_result result = qa_q2m_steer_alternate_fly(input, &services);
  if (result.kind == QA_Q2M_FLY_FALLBACK) return false;
  qa_q2_body_changes c;
  memset(&c, 0, sizeof c);
  c.has_velocity = true; c.velocity = result.velocity;
  if (result.has_pitch) {
    c.has_angles = true;
    c.angles = body.angles; c.angles.x = result.pitch;
  }
  q2m_move(game, entity, c, true, false);
  return true;
}
