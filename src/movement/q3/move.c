/*
 * Port of src/movement/q3/move.ts (bg_pmove.c). GPL-2.0-or-later.
 * Math.fround staged as (float) casts at the TS points; JS bitwise
 * edits apply qa_int32. The TS MoveStep class becomes move_step with
 * an embedded slide context whose ctx is the step itself.
 */
#include "qa/movement/q3/move.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/grapple.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/body_shape.h"
#include "qa/movement/q3/constants.h"
#include "qa/movement/q3/postures.h"
#include "qa/movement/q3/slide_move.h"
#include "qa/movement/q3/view.h"

#define ALL_TIMES (QA_Q3_FLAG_TIME_WATERJUMP | QA_Q3_FLAG_TIME_LAND | QA_Q3_FLAG_TIME_KNOCKBACK)
#define MASK_WATER (32 | 16 | 8)
#define CONTENTS_BODY 0x2000000
#define SURF_SLICK 2
#define SURF_LADDER 8
#define SURF_NODAMAGE 1
#define SURF_METALSTEPS 0x1000
#define SURF_NOSTEPS 0x2000

static double f32(double v) { return (double)(float)v; }
static qa_vec3 v3(double x, double y, double z) { qa_vec3 r = {x, y, z}; return r; }
static bool flag(double v, int m) { return (qa_int32(v) & m) != 0; }
static double flag_set(double v, int m) { return (double)(qa_int32(v) | m); }
static double flag_clear(double v, int m) { return (double)(qa_int32(v) & ~m); }
static double js_max(double a, double b) { return fmax(a, b); }
static double js_min(double a, double b) { return fmin(a, b); }

void qa_q3_drop_movement_timers(qa_q3_motion *state, double milliseconds) {
  if (state->pm_time) {
    if (milliseconds >= state->pm_time) { state->pm_flags = flag_clear(state->pm_flags, ALL_TIMES); state->pm_time = 0; }
    else state->pm_time -= milliseconds;
  }
}
void qa_q3_update_view_angles(qa_q3_motion *ps, qa_q3_move_command cmd) {
  static const double intermission[2] = {QA_Q3_PM_INTERMISSION, QA_Q3_PM_SPINTERMISSION};
  qa_q3_view_angles_result value = qa_q3_view_angles(cmd.angles, ps->delta_angles, ps->viewangles,
                                                     ps->health, ps->pm_type, intermission, 2);
  ps->viewangles = value.angles; ps->delta_angles = value.delta;
}

static double snap_value(double value) {
  double floor_v = floor(value);
  double fraction = value - floor_v;
  if (fraction < 0.5) return floor_v;
  if (fraction > 0.5) return floor_v + 1;
  return fmod(floor_v, 2) == 0 ? floor_v : floor_v + 1;
}
/* q3GrappleVelocity is qa_q3_grapple_velocity (TS owner:
 * src/content/q3/base/game/grapple.ts). */

typedef struct move_step {
  qa_q3_slide_move_context slide;
  qa_q3_motion *state;
  qa_q3_move_command cmd;
  qa_q3_motion_options *options;
  qa_trace_result *contacts; size_t contact_count, contact_capacity;
  double ground_surface_flags; bool walking, ladder;
  double waterlevel, watertype, xyspeed;
  qa_vec3 forward, right;
  double msec;
  qa_vec3 previous_origin, previous_velocity;
} move_step;

static qa_trace_result step_trace(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds,
                                  qa_actor_id pass_actor, double mask);
static void step_touch(void *ctx, qa_trace_result trace);
static void step_event(void *ctx, double event);
static void step_debug(void *ctx, const char *message);

static void step_init(move_step *s, qa_q3_motion *state, qa_q3_move_command cmd, qa_q3_motion_options *options) {
  memset(s, 0, sizeof *s);
  s->state = state; s->cmd = cmd; s->options = options;
  s->slide.bounds = options->has_current_bounds ? options->current_bounds : options->standing_bounds;
  double raw = cmd.server_time - state->command_time;
  s->msec = js_max(1, js_min(200, raw));
  s->slide.frame_time = f32(s->msec * f32(0.001));
  s->previous_origin = state->origin; s->previous_velocity = state->velocity;
  double mask = options->has_trace_mask ? options->trace_mask : (double)(1 | 0x10000 | CONTENTS_BODY);
  s->slide.mask = state->health <= 0 ? (double)(qa_int32(mask) & ~CONTENTS_BODY) : mask;
  s->slide.ctx = s; s->slide.state = state;
  s->slide.trace = step_trace; s->slide.touch = step_touch;
  s->slide.event = step_event; s->slide.debug = step_debug;
}
static qa_trace_result step_trace(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds,
                                  qa_actor_id pass_actor, double mask) {
  move_step *s = ctx;
  return s->options->trace(s->options->ctx, start, end, bounds, pass_actor, mask);
}
static void step_event(void *ctx, double event) { move_step *s = ctx; s->options->event(s->options->ctx, event); }
static void step_debug(void *ctx, const char *message) {
  move_step *s = ctx;
  qa_q3_diagnostics *d = s->options->diagnostics;
  if (d != NULL && d->level != 0) {
    char buffer[256];
    snprintf(buffer, sizeof buffer, "%.0f:%s\n", d->count, message);
    d->print(d->ctx, buffer);
  }
}
static void step_touch(void *ctx, qa_trace_result trace) {
  move_step *s = ctx;
  qa_trace_hit hit = trace.hit;
  if (hit.kind != QA_TRACE_HIT_ACTOR || s->contact_count >= 32) return;
  for (size_t i = 0; i < s->contact_count; i++)
    if (s->contacts[i].hit.kind == QA_TRACE_HIT_ACTOR && qa_same_actor(s->contacts[i].hit.actor, hit.actor)) return;
  if (s->contact_count == s->contact_capacity) {
    s->contact_capacity = s->contact_capacity ? s->contact_capacity * 2 : 8;
    s->contacts = realloc(s->contacts, s->contact_capacity * sizeof *s->contacts);
    if (s->contacts == NULL) qa_range_error("Out of memory");
  }
  s->contacts[s->contact_count++] = trace;
  s->options->contact(s->options->ctx, trace);
}
static qa_trace_result test_trace(move_step *s, qa_vec3 start, qa_vec3 end) {
  return s->slide.trace(s, start, end, s->slide.bounds, s->state->actor, s->slide.mask);
}
static double contents_at(move_step *s, qa_vec3 point) {
  return s->options->point_contents(s->options->ctx, point, s->state->actor);
}
static void anim_request(move_step *s, qa_q3_animation_request request) {
  s->options->animation(s->options->ctx, request);
}
static void legs(move_step *s, double animation, bool force) {
  qa_q3_animation_request r = {QA_Q3_ANIM_LEGS, animation, force, 0};
  anim_request(s, r);
}
static void jump_animation(move_step *s) {
  if (s->cmd.forwardmove >= 0) { legs(s, QA_Q3_ANIM_LEGS_JUMP, true); s->state->pm_flags = flag_clear(s->state->pm_flags, QA_Q3_FLAG_BACKWARDS_JUMP); }
  else { legs(s, QA_Q3_ANIM_LEGS_JUMPB, true); s->state->pm_flags = flag_set(s->state->pm_flags, QA_Q3_FLAG_BACKWARDS_JUMP); }
}
static void friction(move_step *s) {
  qa_q3_motion *ps = s->state;
  qa_vec3 velocity = ps->velocity;
  double speed = f32(qa_length3(s->walking ? v3(velocity.x, velocity.y, 0) : velocity));
  if (speed < 1) { ps->velocity = v3(0, 0, velocity.z); return; }
  double drop = 0, frame_time = s->slide.frame_time;
  if (s->waterlevel <= 1 && s->walking && !(qa_int32(s->ground_surface_flags) & SURF_SLICK) &&
      !flag(ps->pm_flags, QA_Q3_FLAG_TIME_KNOCKBACK))
    drop = f32(f32(js_max(speed, 100) * 6) * frame_time);
  if (s->waterlevel) drop = f32(drop + f32(f32(speed * s->waterlevel) * frame_time));
  if (ps->flight) drop = f32(drop + f32(f32(speed * 3) * frame_time));
  if (ps->pm_type == QA_Q3_PM_SPECTATOR) drop = f32(drop + f32(f32(speed * 5) * frame_time));
  ps->velocity = qa_scale3(velocity, f32(js_max(0, f32(speed - drop)) / speed));
}
static void accelerate(move_step *s, qa_vec3 direction, double speed, double acceleration) {
  speed = f32(speed);
  double current_speed = f32(qa_dot3(s->state->velocity, direction));
  double add_speed = f32(speed - current_speed);
  if (add_speed <= 0) return;
  double amount = js_min(f32(f32(acceleration * s->slide.frame_time) * speed), add_speed);
  s->state->velocity = qa_add3(s->state->velocity, qa_scale3(direction, amount));
}
static double command_scale(move_step *s) {
  double f = s->cmd.forwardmove, r = s->cmd.rightmove, u = s->cmd.upmove;
  double max_v = js_max(fabs(f), js_max(fabs(r), fabs(u)));
  double total = f32(sqrt(f * f + r * r + u * u));
  return max_v == 0 ? 0 : f32(f32(f32(s->state->speed) * max_v) / f32(127 * total));
}
static void movement_direction(move_step *s) {
  double f = s->cmd.forwardmove, r = s->cmd.rightmove;
  if (f || r) s->state->movement_dir = f > 0 ? (r < 0 ? 1 : r > 0 ? 7 : 0) :
      f < 0 ? (r < 0 ? 3 : r > 0 ? 5 : 4) : r < 0 ? 2 : 6;
  else if (s->state->movement_dir == 2) s->state->movement_dir = 1;
  else if (s->state->movement_dir == 6) s->state->movement_dir = 7;
}
static bool check_jump(move_step *s) {
  qa_q3_motion *ps = s->state;
  if (flag(ps->pm_flags, QA_Q3_FLAG_RESPAWNED) || s->cmd.upmove < 10) return false;
  if (flag(ps->pm_flags, QA_Q3_FLAG_JUMP_HELD)) { s->cmd.upmove = 0; return false; }
  s->slide.has_ground_normal = false; s->walking = false;
  ps->pm_flags = flag_set(ps->pm_flags, QA_Q3_FLAG_JUMP_HELD);
  ps->ground = (qa_trace_hit){0};
  ps->velocity = v3(ps->velocity.x, ps->velocity.y, 270);
  step_event(s, QA_Q3_EV_JUMP);
  jump_animation(s);
  return true;
}
static bool check_water_jump(move_step *s) {
  qa_q3_motion *ps = s->state;
  if (ps->pm_time != 0 || s->waterlevel != 2) return false;
  qa_vec3 flat = qa_normalize3(v3(s->forward.x, s->forward.y, 0));
  qa_vec3 point = qa_add3(ps->origin, qa_scale3(flat, 30));
  if (!(qa_int32(contents_at(s, v3(point.x, point.y, point.z + 4))) & 1) ||
      contents_at(s, v3(point.x, point.y, point.z + 20)) != 0) return false;
  qa_vec3 velocity = qa_scale3(s->forward, 200);
  ps->velocity = v3(velocity.x, velocity.y, 350);
  ps->pm_flags = flag_set(ps->pm_flags, QA_Q3_FLAG_TIME_WATERJUMP);
  ps->pm_time = 2000;
  return true;
}
static void water_jump_move(move_step *s) {
  qa_q3_step_slide_move(&s->slide, true);
  qa_q3_motion *ps = s->state;
  ps->velocity = v3(ps->velocity.x, ps->velocity.y, ps->velocity.z - f32(ps->gravity * s->slide.frame_time));
  if (ps->velocity.z < 0) { ps->pm_flags = flag_clear(ps->pm_flags, ALL_TIMES); ps->pm_time = 0; }
}
static qa_vec3 wish_velocity(move_step *s, double scale) {
  double cx = f32(f32(f32(scale * s->forward.x) * s->cmd.forwardmove) + f32(f32(scale * s->right.x) * s->cmd.rightmove));
  double cy = f32(f32(f32(scale * s->forward.y) * s->cmd.forwardmove) + f32(f32(scale * s->right.y) * s->cmd.rightmove));
  double cz = f32(f32(f32(scale * s->forward.z) * s->cmd.forwardmove) + f32(f32(scale * s->right.z) * s->cmd.rightmove));
  return v3(cx, cy, cz + f32(scale * s->cmd.upmove));
}
static void water_move(move_step *s) {
  if (check_water_jump(s)) { water_jump_move(s); return; }
  friction(s);
  double scale = command_scale(s);
  qa_vec3 wish = scale == 0 ? v3(0, 0, -60) : wish_velocity(s, scale);
  accelerate(s, qa_normalize3(wish), js_min(qa_length3(wish), s->state->speed * 0.5), 4);
  if (s->slide.has_ground_normal && qa_dot3(s->state->velocity, s->slide.ground_normal) < 0) {
    double speed = f32(qa_length3(s->state->velocity));
    s->state->velocity = qa_scale3(qa_normalize3(qa_q3_clip_velocity(s->state->velocity, s->slide.ground_normal, 1.001)), speed);
  }
  (void)qa_q3_slide_move(&s->slide, false);
}
static void fly_move(move_step *s) {
  friction(s);
  qa_vec3 wish = wish_velocity(s, command_scale(s));
  accelerate(s, qa_normalize3(wish), qa_length3(wish), 8);
  qa_q3_step_slide_move(&s->slide, false);
}
static void air_move(move_step *s) {
  friction(s);
  double scale = command_scale(s);
  movement_direction(s);
  s->forward = qa_normalize3(v3(s->forward.x, s->forward.y, 0));
  s->right = qa_normalize3(v3(s->right.x, s->right.y, 0));
  qa_vec3 wish = qa_add3(qa_scale3(s->forward, s->cmd.forwardmove), qa_scale3(s->right, s->cmd.rightmove));
  accelerate(s, qa_normalize3(wish), f32(f32(qa_length3(wish)) * scale), 1);
  if (s->slide.has_ground_normal) s->state->velocity = qa_q3_clip_velocity(s->state->velocity, s->slide.ground_normal, 1.001);
  qa_q3_step_slide_move(&s->slide, true);
}
static void check_ladder(move_step *s) {
  s->ladder = false;
  if (s->state->pm_time) return;
  qa_vec3 flat = qa_normalize3(v3(s->forward.x, s->forward.y, 0));
  qa_trace_result trace = test_trace(s, s->state->origin, qa_add3(s->state->origin, flat));
  if (trace.fraction < 1 && trace.kind != NULL && strcmp(trace.kind, "q3") == 0 &&
      (qa_int32(trace.surface_flags) & SURF_LADDER) != 0) s->ladder = true;
}
static void ladder_move(move_step *s) {
  if (s->walking && check_jump(s)) { air_move(s); return; }
  friction(s);
  qa_q3_motion *ps = s->state;
  qa_vec3 wish = wish_velocity(s, command_scale(s));
  double wish_z = wish.z;
  if (fabs(ps->velocity.z) <= 200) {
    if (ps->viewangles.x <= -15 && s->cmd.forwardmove > 0) wish_z = 200;
    else if (ps->viewangles.x >= 15 && s->cmd.forwardmove > 0) wish_z = -200;
    else if (s->cmd.upmove > 0) wish_z = 200;
    else if (s->cmd.upmove < 0) wish_z = -200;
  }
  qa_vec3 wish_vel = v3(wish.x, wish.y, f32(wish_z));
  accelerate(s, qa_normalize3(wish_vel), f32(qa_length3(wish_vel)), 10);
  if (wish_vel.z == 0) {
    if (ps->velocity.z > 0) ps->velocity = v3(ps->velocity.x, ps->velocity.y, js_max(0, f32(ps->velocity.z - f32(ps->gravity * s->slide.frame_time))));
    else ps->velocity = v3(ps->velocity.x, ps->velocity.y, js_min(0, f32(ps->velocity.z + f32(ps->gravity * s->slide.frame_time))));
  }
  qa_q3_step_slide_move(&s->slide, false);
}
static void grapple_move(move_step *s) {
  s->state->velocity = qa_q3_grapple_velocity(s->state->origin, s->state->grapple_point, s->forward);
  s->slide.has_ground_normal = false;
}
static void walk_move(move_step *s) {
  if (!s->slide.has_ground_normal) qa_range_error("Walking requires a ground plane");
  qa_vec3 normal = s->slide.ground_normal;
  if (s->waterlevel > 2 && qa_dot3(s->forward, normal) > 0) { water_move(s); return; }
  if (check_jump(s)) { if (s->waterlevel > 1) water_move(s); else air_move(s); return; }
  friction(s);
  qa_q3_motion *ps = s->state;
  double scale = command_scale(s);
  movement_direction(s);
  s->forward = qa_normalize3(qa_q3_clip_velocity(v3(s->forward.x, s->forward.y, 0), normal, 1.001));
  s->right = qa_normalize3(qa_q3_clip_velocity(v3(s->right.x, s->right.y, 0), normal, 1.001));
  qa_vec3 wish = qa_add3(qa_scale3(s->forward, s->cmd.forwardmove), qa_scale3(s->right, s->cmd.rightmove));
  double wish_speed = f32(f32(qa_length3(wish)) * scale);
  if (flag(ps->pm_flags, QA_Q3_FLAG_DUCKED)) wish_speed = js_min(wish_speed, ps->speed * 0.25);
  if (s->waterlevel) {
    double water_scale = f32(1 - 0.5 * f32(s->waterlevel / 3));
    wish_speed = js_min(wish_speed, f32(ps->speed * water_scale));
  }
  bool sliding = (qa_int32(s->ground_surface_flags) & SURF_SLICK) != 0 || flag(ps->pm_flags, QA_Q3_FLAG_TIME_KNOCKBACK);
  accelerate(s, qa_normalize3(wish), wish_speed, sliding ? 1 : 10);
  if (sliding) ps->velocity = v3(ps->velocity.x, ps->velocity.y, ps->velocity.z - f32(ps->gravity * s->slide.frame_time));
  double speed = f32(qa_length3(ps->velocity));
  ps->velocity = qa_scale3(qa_normalize3(qa_q3_clip_velocity(ps->velocity, normal, 1.001)), speed);
  if (ps->velocity.x || ps->velocity.y) qa_q3_step_slide_move(&s->slide, false);
}
static void noclip_move(move_step *s) {
  qa_q3_motion *ps = s->state;
  ps->viewheight = s->options->postures.standing_view_height;
  double speed = f32(qa_length3(ps->velocity));
  ps->velocity = speed < 1 ? v3(0, 0, 0) : qa_scale3(ps->velocity,
      f32(js_max(0, f32(speed - f32(f32(js_max(100, speed) * 9) * s->slide.frame_time))) / speed));
  qa_vec3 wish = wish_velocity(s, 1);
  accelerate(s, qa_normalize3(wish), f32(f32(qa_length3(wish)) * command_scale(s)), 10);
  ps->origin = qa_add3(ps->origin, qa_scale3(ps->velocity, s->slide.frame_time));
}
static double footstep_for_surface(move_step *s) {
  return qa_int32(s->ground_surface_flags) & SURF_NOSTEPS ? 0 :
      qa_int32(s->ground_surface_flags) & SURF_METALSTEPS ? QA_Q3_EV_FOOTSTEP_METAL : QA_Q3_EV_FOOTSTEP;
}
static void crash_land(move_step *s) {
  qa_q3_motion *ps = s->state;
  legs(s, flag(ps->pm_flags, QA_Q3_FLAG_BACKWARDS_JUMP) ? QA_Q3_ANIM_LEGS_LANDB : QA_Q3_ANIM_LEGS_LAND, true);
  qa_q3_animation_request timer = {QA_Q3_ANIM_LEGS_TIMER, 0, false, 130};
  anim_request(s, timer);
  double dist = f32(ps->origin.z - s->previous_origin.z);
  double velocity = s->previous_velocity.z;
  double acceleration = f32(qa_int32(-ps->gravity));
  double a = f32(acceleration / 2);
  double discriminant = f32(f32(velocity * velocity) - f32(f32(4 * a) * -dist));
  if (discriminant < 0) return;
  double t = f32(f32(-velocity - f32(sqrt(discriminant))) / f32(2 * a));
  double impact = f32(velocity + f32(t * acceleration));
  double delta = f32(f32(impact * impact) * f32(0.0001));
  if (flag(ps->pm_flags, QA_Q3_FLAG_DUCKED)) delta = f32(delta * 2);
  if (s->waterlevel == 3) return;
  if (s->waterlevel == 2) delta = f32(delta * 0.25);
  if (s->waterlevel == 1) delta = f32(delta * 0.5);
  if (delta < 1) return;
  if (!(qa_int32(s->ground_surface_flags) & SURF_NODAMAGE)) {
    if (delta > 60) step_event(s, QA_Q3_EV_FALL_FAR);
    else if (delta > 40) { if (ps->health > 0) step_event(s, QA_Q3_EV_FALL_MEDIUM); }
    else if (delta > 7) step_event(s, QA_Q3_EV_FALL_SHORT);
    else step_event(s, footstep_for_surface(s));
  }
  ps->bob_cycle = 0;
}
static void leave_ground(move_step *s) {
  s->state->ground = (qa_trace_hit){0};
  s->slide.has_ground_normal = false; s->walking = false;
}
static void ground_trace(move_step *s) {
  qa_q3_motion *ps = s->state;
  qa_vec3 down = v3(ps->origin.x, ps->origin.y, ps->origin.z - 0.25);
  qa_trace_result trace = test_trace(s, ps->origin, down);
  if (trace.all_solid) {
    step_debug(s, "allsolid");
    bool corrected = false;
    for (int i = -1; i <= 1 && !corrected; i++)
      for (int j = -1; j <= 1 && !corrected; j++)
        for (int k = -1; k <= 1; k++) {
          qa_vec3 point = qa_add3(ps->origin, v3(i, j, k));
          if (!test_trace(s, point, point).all_solid) { trace = test_trace(s, ps->origin, down); corrected = true; break; }
        }
    if (!corrected) { leave_ground(s); return; }
  }
  s->ground_surface_flags = trace.kind != NULL && strcmp(trace.kind, "q3") == 0 ? trace.surface_flags :
      trace.kind != NULL && strcmp(trace.kind, "q2") == 0 ? (trace.surface != NULL ? trace.surface->flags : 0) : 0;
  if (trace.fraction == 1) {
    if (ps->ground.kind != QA_TRACE_HIT_NONE) {
      step_debug(s, "lift");
      if (test_trace(s, ps->origin, v3(ps->origin.x, ps->origin.y, ps->origin.z - 64)).fraction == 1) jump_animation(s);
    }
    leave_ground(s); return;
  }
  if (!trace.contact.has_plane) { leave_ground(s); return; }
  qa_vec3 normal = trace.contact.plane.normal;
  if (ps->velocity.z > 0 && qa_dot3(ps->velocity, normal) > 10) {
    step_debug(s, "kickoff"); jump_animation(s); leave_ground(s); return;
  }
  s->slide.has_ground_normal = true; s->slide.ground_normal = normal;
  if (normal.z < f32(0.7)) {
    step_debug(s, "steep");
    ps->ground = (qa_trace_hit){0}; s->walking = false; return;
  }
  s->walking = true;
  if (flag(ps->pm_flags, QA_Q3_FLAG_TIME_WATERJUMP)) {
    ps->pm_flags = flag_clear(ps->pm_flags, QA_Q3_FLAG_TIME_WATERJUMP | QA_Q3_FLAG_TIME_LAND); ps->pm_time = 0;
  }
  if (ps->ground.kind == QA_TRACE_HIT_NONE) {
    step_debug(s, "Land"); crash_land(s);
    if (s->previous_velocity.z < -200) { ps->pm_flags = flag_set(ps->pm_flags, QA_Q3_FLAG_TIME_LAND); ps->pm_time = 250; }
  }
  ps->ground = trace.hit;
  step_touch(s, trace);
}
static void set_water_level(move_step *s) {
  s->waterlevel = 0; s->watertype = 0;
  qa_q3_motion *ps = s->state;
  double min_z = s->options->standing_bounds.min.z;
  double contents = contents_at(s, v3(ps->origin.x, ps->origin.y, ps->origin.z + min_z + 1));
  if (!(qa_int32(contents) & MASK_WATER)) return;
  s->watertype = contents; s->waterlevel = 1;
  double sample2 = ps->viewheight - min_z;
  double sample1 = trunc(sample2 / 2);
  if (!(qa_int32(contents_at(s, v3(ps->origin.x, ps->origin.y, ps->origin.z + min_z + sample1))) & MASK_WATER)) return;
  s->waterlevel = 2;
  if (qa_int32(contents_at(s, v3(ps->origin.x, ps->origin.y, ps->origin.z + min_z + sample2))) & MASK_WATER) s->waterlevel = 3;
}
typedef struct duck_clear { move_step *step; } duck_clear;
static bool duck_clear_fn(void *ctx, qa_bounds bounds) {
  move_step *s = ((duck_clear *)ctx)->step;
  qa_trace_result trace = s->options->trace(s->options->ctx, s->state->origin, s->state->origin,
                                             bounds, s->state->actor, s->slide.mask);
  return !trace.all_solid;
}
static bool bounds_equal(qa_bounds a, qa_bounds b) { return memcmp(&a, &b, sizeof a) == 0; }
static void check_duck(move_step *s) {
  qa_q3_motion *ps = s->state;
  qa_bounds standing = s->options->standing_bounds;
  qa_bounds previous_bounds = s->slide.bounds;
  bool previous_duck = flag(ps->pm_flags, QA_Q3_FLAG_DUCKED);
  qa_q3_postures postures = s->options->postures;
  if (ps->invulnerable || s->options->pose != NULL) {
    qa_fixed_movement_pose pose = s->options->pose != NULL ? *s->options->pose :
        qa_q3_invulnerability_pose(flag(ps->pm_flags, QA_Q3_FLAG_INVULEXPAND), postures);
    s->slide.bounds = pose.bounds;
    ps->pm_flags = pose.crouched ? flag_set(ps->pm_flags, QA_Q3_FLAG_DUCKED) : flag_clear(ps->pm_flags, QA_Q3_FLAG_DUCKED);
    ps->viewheight = pose.view_height;
    return;
  }
  ps->pm_flags = flag_clear(ps->pm_flags, QA_Q3_FLAG_INVULEXPAND);
  if (ps->pm_type == QA_Q3_PM_DEAD) {
    s->slide.bounds = postures.dead.bounds; ps->viewheight = postures.dead.view_height; return;
  }
  if (s->cmd.upmove < 0) ps->pm_flags = flag_set(ps->pm_flags, QA_Q3_FLAG_DUCKED);
  else if (flag(ps->pm_flags, QA_Q3_FLAG_DUCKED)) {
    s->slide.bounds = s->options->has_body_bounds && s->options->body_bounds != NULL ? *s->options->body_bounds : standing;
    if (!test_trace(s, ps->origin, ps->origin).all_solid) ps->pm_flags = flag_clear(ps->pm_flags, QA_Q3_FLAG_DUCKED);
  }
  s->slide.bounds = flag(ps->pm_flags, QA_Q3_FLAG_DUCKED) ? postures.crouched.bounds : standing;
  ps->viewheight = flag(ps->pm_flags, QA_Q3_FLAG_DUCKED) ? postures.crouched.view_height : postures.standing_view_height;
  qa_bounds requested = s->options->has_body_bounds && s->options->body_bounds != NULL ? *s->options->body_bounds : s->slide.bounds;
  duck_clear clear_ctx = {s};
  qa_bounds accepted = qa_movement_bounds(previous_bounds, requested, duck_clear_fn, &clear_ctx);
  s->slide.bounds = accepted;
  if (!bounds_equal(accepted, requested)) {
    ps->pm_flags = previous_duck ? flag_set(flag_clear(ps->pm_flags, QA_Q3_FLAG_DUCKED), QA_Q3_FLAG_DUCKED)
                                 : flag_clear(ps->pm_flags, QA_Q3_FLAG_DUCKED);
    ps->viewheight = previous_duck ? postures.crouched.view_height : postures.standing_view_height;
  }
}
static void footsteps(move_step *s) {
  qa_q3_motion *ps = s->state;
  double x_sq = f32(ps->velocity.x * ps->velocity.x), y_sq = f32(ps->velocity.y * ps->velocity.y);
  s->xyspeed = f32(sqrt(f32(x_sq + y_sq)));
  if (ps->ground.kind == QA_TRACE_HIT_NONE) {
    if (ps->invulnerable) legs(s, QA_Q3_ANIM_LEGS_IDLECR, false);
    if (s->waterlevel > 1) legs(s, QA_Q3_ANIM_LEGS_SWIM, false);
    return;
  }
  if (!s->cmd.forwardmove && !s->cmd.rightmove) {
    if (s->xyspeed < 5) { ps->bob_cycle = 0; legs(s, flag(ps->pm_flags, QA_Q3_FLAG_DUCKED) ? QA_Q3_ANIM_LEGS_IDLECR : QA_Q3_ANIM_LEGS_IDLE, false); }
    return;
  }
  double bob; bool footstep = false;
  bool backwards = flag(ps->pm_flags, QA_Q3_FLAG_BACKWARDS_RUN);
  if (flag(ps->pm_flags, QA_Q3_FLAG_DUCKED)) { bob = 0.5; legs(s, backwards ? QA_Q3_ANIM_LEGS_BACKCR : QA_Q3_ANIM_LEGS_WALKCR, false); }
  else if (!(qa_int32(s->cmd.buttons) & QA_Q3_BUTTON_WALKING)) { bob = 0.4; footstep = true; legs(s, backwards ? QA_Q3_ANIM_LEGS_BACK : QA_Q3_ANIM_LEGS_RUN, false); }
  else { bob = 0.3; legs(s, backwards ? QA_Q3_ANIM_LEGS_BACKWALK : QA_Q3_ANIM_LEGS_WALK, false); }
  double old = ps->bob_cycle;
  ps->bob_cycle = (double)(qa_int32(trunc(f32(f32(old) + f32(f32(bob) * s->msec)))) & 255);
  if (((qa_int32(old + 64) ^ qa_int32(ps->bob_cycle + 64)) & 128) != 0) {
    if (s->waterlevel == 0 && footstep && !s->options->no_footsteps) step_event(s, footstep_for_surface(s));
    else if (s->waterlevel == 1) step_event(s, QA_Q3_EV_FOOTSPLASH);
    else if (s->waterlevel == 2) step_event(s, QA_Q3_EV_SWIM);
  }
}
static void water_events(move_step *s, double previous) {
  if (!previous && s->waterlevel) step_event(s, QA_Q3_EV_WATER_TOUCH);
  if (previous && !s->waterlevel) step_event(s, QA_Q3_EV_WATER_LEAVE);
  if (previous != 3 && s->waterlevel == 3) step_event(s, QA_Q3_EV_WATER_UNDER);
  if (previous == 3 && s->waterlevel != 3) step_event(s, QA_Q3_EV_WATER_CLEAR);
}
static void drop_timers(move_step *s) {
  qa_q3_drop_movement_timers(s->state, s->msec);
  qa_q3_animation_request r = {QA_Q3_ANIM_DROP_TIMERS, 0, false, 0};
  anim_request(s, r);
}
static void step_run(move_step *s) {
  qa_q3_motion *ps = s->state;
  qa_q3_diagnostics *diagnostics = s->options->diagnostics;
  if (diagnostics != NULL) diagnostics->count = (double)qa_int32(diagnostics->count + 1);
  if (fabs(s->cmd.forwardmove) > 64 || fabs(s->cmd.rightmove) > 64)
    s->cmd.buttons = flag_clear(s->cmd.buttons, QA_Q3_BUTTON_WALKING);
  if (flag(s->cmd.buttons, QA_Q3_BUTTON_TALK)) ps->e_flags = flag_set(ps->e_flags, 0x1000);
  else ps->e_flags = flag_clear(ps->e_flags, 0x1000);
  if (!flag(ps->pm_flags, QA_Q3_FLAG_RESPAWNED) && ps->pm_type != QA_Q3_PM_INTERMISSION &&
      flag(s->cmd.buttons, QA_Q3_BUTTON_ATTACK) && s->options->firing(s->options->ctx))
    ps->e_flags = flag_set(ps->e_flags, 0x100);
  else ps->e_flags = flag_clear(ps->e_flags, 0x100);
  if (ps->health > 0 && !flag(s->cmd.buttons, QA_Q3_BUTTON_ATTACK | QA_Q3_BUTTON_USE_HOLDABLE))
    ps->pm_flags = flag_clear(ps->pm_flags, QA_Q3_FLAG_RESPAWNED);
  if (flag(s->cmd.buttons, QA_Q3_BUTTON_TALK)) {
    s->cmd.buttons = QA_Q3_BUTTON_TALK; s->cmd.forwardmove = 0; s->cmd.rightmove = 0; s->cmd.upmove = 0;
  }
  ps->command_time = s->cmd.server_time;
  qa_q3_update_view_angles(ps, s->cmd);
  qa_angle_vectors axes = qa_qvm_angle_vectors(ps->viewangles);
  s->forward = axes.forward; s->right = axes.right;
  if (s->cmd.upmove < 10) ps->pm_flags = flag_clear(ps->pm_flags, QA_Q3_FLAG_JUMP_HELD);
  if (s->cmd.forwardmove < 0) ps->pm_flags = flag_set(ps->pm_flags, QA_Q3_FLAG_BACKWARDS_RUN);
  else if (s->cmd.forwardmove > 0 || (!s->cmd.forwardmove && s->cmd.rightmove))
    ps->pm_flags = flag_clear(ps->pm_flags, QA_Q3_FLAG_BACKWARDS_RUN);
  if (ps->pm_type >= QA_Q3_PM_DEAD) { s->cmd.forwardmove = 0; s->cmd.rightmove = 0; s->cmd.upmove = 0; }
  if (ps->pm_type == QA_Q3_PM_SPECTATOR) { check_duck(s); fly_move(s); drop_timers(s); return; }
  if (ps->pm_type == QA_Q3_PM_NOCLIP) { noclip_move(s); drop_timers(s); return; }
  if (ps->pm_type == QA_Q3_PM_FREEZE || ps->pm_type == QA_Q3_PM_INTERMISSION || ps->pm_type == QA_Q3_PM_SPINTERMISSION) return;
  set_water_level(s);
  double previous_waterlevel = s->waterlevel;
  check_duck(s); ground_trace(s); check_ladder(s);
  if (ps->pm_type == QA_Q3_PM_DEAD && s->walking) {
    double speed = f32(f32(qa_length3(ps->velocity)) - 20);
    ps->velocity = speed <= 0 ? v3(0, 0, 0) : qa_scale3(qa_normalize3(ps->velocity), speed);
  }
  drop_timers(s);
  if (s->options->pose != NULL ||
      (ps->product != NULL && strcmp(ps->product, "missionpack") == 0 && ps->invulnerable)) {
    s->cmd.forwardmove = 0; s->cmd.rightmove = 0; s->cmd.upmove = 0; ps->velocity = v3(0, 0, 0);
  } else if (ps->flight) fly_move(s);
  else if (flag(ps->pm_flags, QA_Q3_FLAG_GRAPPLE_PULL)) { grapple_move(s); air_move(s); }
  else if (flag(ps->pm_flags, QA_Q3_FLAG_TIME_WATERJUMP)) water_jump_move(s);
  else if (s->waterlevel > 1) water_move(s);
  else if (s->ladder) ladder_move(s);
  else if (s->walking) walk_move(s);
  else air_move(s);
  qa_q3_animation_request gesture = {QA_Q3_ANIM_GESTURE, 0, false, 0};
  anim_request(s, gesture);
  ground_trace(s); set_water_level(s);
  if (!s->options->weapon(s->options->ctx)) return;
  s->options->torso(s->options->ctx);
  footsteps(s); water_events(s, previous_waterlevel);
  ps->velocity = v3(snap_value(ps->velocity.x), snap_value(ps->velocity.y), snap_value(ps->velocity.z));
}

static bool is_integer(double v) { return isfinite(v) && trunc(v) == v; }

qa_q3_motion_result qa_q3_move_player(qa_q3_motion *state, qa_q3_move_command command,
                                      qa_q3_motion_options *options) {
  if (!is_integer(command.server_time) || !isfinite(command.server_time))
    qa_range_error("Command time must be an integer");
  const double axes_values[3] = {command.forwardmove, command.rightmove, command.upmove};
  for (int i = 0; i < 3; i++)
    if (!is_integer(axes_values[i]) || axes_values[i] < -128 || axes_values[i] > 127)
      qa_range_error("Command movement must be a signed byte");
  double fixed = options->has_fixed_msec ? options->fixed_msec : 66;
  if (!is_integer(fixed) || fixed < 1 || fixed > 0x7fffffff)
    qa_range_error("Movement step must be a positive signed 32-bit integer");
  qa_q3_motion_result result = {NULL, 0,
      options->has_current_bounds ? options->current_bounds : options->standing_bounds, 0, 0, 0};
  double final_time = command.server_time;
  if (final_time < state->command_time) return result;
  if (final_time > state->command_time + 1000) state->command_time = final_time - 1000;
  state->pmove_framecount = (double)(qa_int32(state->pmove_framecount + 1) & 63);
  qa_q3_move_command cmd = command;
  double substep = 0;
  while (state->command_time != final_time) {
    cmd.server_time = state->command_time + js_min(final_time - state->command_time, fixed);
    double step_msec = js_max(1, js_min(200, cmd.server_time - state->command_time));
    bool proceed = options->begin_step(options->ctx, state, &cmd, step_msec, substep);
    substep++;
    if (!proceed) break;
    move_step step;
    step_init(&step, state, cmd, options);
    if (substep > 1) step.slide.bounds = result.bounds;
    step_run(&step);
    cmd = step.cmd; /* TS MoveStep mutates the shared cmd (upmove/buttons/moves) */
    free(result.contacts);
    result.contacts = step.contacts; result.contact_count = step.contact_count;
    result.bounds = step.slide.bounds; result.waterlevel = step.waterlevel;
    result.watertype = step.watertype; result.xyspeed = step.xyspeed;
    if (options->end_step != NULL && !options->end_step(options->ctx, state)) break;
    if (flag(state->pm_flags, QA_Q3_FLAG_JUMP_HELD)) cmd.upmove = 20;
  }
  return result;
}
