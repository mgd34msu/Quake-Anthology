/* Port of src/bots/behavior/rerelease/path-follow.ts. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/path_follow.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <string.h>

qa_bot_path_state qa_new_path_state(void) {
  qa_bot_path_state s; memset(&s, 0, sizeof s);
  s.lift_wait_since = -1;
  return s;
}
static bool path_link(const qa_nav_path *path, int64_t index, qa_nav_graph_link *out) {
  if (index < 0 || index >= (int64_t)path->link_count || !path->has_link[index]) return false;
  *out = path->links[index];
  return true;
}
void qa_bot_set_path(qa_bot_path_state *state, const qa_nav_path *path, qa_bot_vec3 origin, double now) {
  state->path = path;
  state->index = 0;
  if (path) {
    while (state->index + 1 < (int64_t)path->point_count) {
      qa_bot_vec3 here = path->points[state->index], next = path->points[state->index + 1];
      double back = (here.x - origin.x) * (next.x - origin.x) + (here.y - origin.y) * (next.y - origin.y);
      if (back >= 0) break;
      if (fabs(here.z - origin.z) > 64) break;
      qa_nav_graph_link next_link;
      if (path_link(path, state->index + 1, &next_link) &&
          (qa_nav_link_is_entity(next_link.type) || qa_nav_link_is_jump(next_link.type))) break;
      state->index++;
    }
  }
  state->stuck_origin = origin;
  state->stuck_since = now;
  state->stuck_count = 0;
  state->planned_at = now;
}
void qa_bot_clear_path(qa_bot_path_state *state) {
  state->path = NULL;
  state->index = 0;
  state->stuck_count = 0;
  state->lift_wait_since = -1;
}
static qa_bot_move_output make_output(int64_t status) {
  qa_bot_move_output o; memset(&o, 0, sizeof o); o.status = status; return o;
}
qa_bot_move_output qa_follow_path(qa_bot_path_state *state, qa_bot_follow_input input, qa_bot_movement_settings movement) {
  const qa_nav_path *path = state->path;
  if (!path || path->point_count == 0) return make_output(QA_BOT_PATH_NO_PATH);
  while (state->index < (int64_t)path->point_count) {
    qa_bot_vec3 point = path->points[state->index];
    qa_nav_graph_link link_v; bool has_link = path_link(path, state->index, &link_v);
    qa_nav_graph_link arriving; bool has_arriving = state->index > 0 && path_link(path, state->index - 1, &arriving);
    if (has_arriving && arriving.type == QA_NAV_LINK_TRAIN) {
      bool exits = false;
      if (input.transport) {
        qa_bot_transport_step step = input.transport(input.transport_ctx, arriving, input.origin);
        exits = step.has && step.kind == QA_TRANSPORT_MOVE && step.exit_stage;
      }
      if (!exits) break;
    }
    double tolerance = has_link && link_v.type != QA_NAV_LINK_WALK ? QA_BOT_TRAVERSAL_REACHED : QA_BOT_POINT_REACHED;
    if (qa_bvec_distance2d(input.origin, point) <= tolerance && fabs(input.origin.z - point.z) <= 64) {
      state->index++;
      state->stuck_origin = input.origin;
      state->stuck_since = input.now;
      state->stuck_count = 0;
      continue;
    }
    break;
  }
  if (state->index >= (int64_t)path->point_count) return make_output(QA_BOT_PATH_ARRIVED);
  qa_bot_vec3 target = path->points[state->index];
  qa_nav_graph_link link_v; bool has_link = path_link(path, state->index, &link_v);
  qa_nav_graph_link prev_v; bool has_prev = state->index > 0 && path_link(path, state->index - 1, &prev_v);
  qa_nav_graph_link train_link; bool has_train = false;
  if (has_link && link_v.type == QA_NAV_LINK_TRAIN) { train_link = link_v; has_train = true; }
  else if (has_prev && prev_v.type == QA_NAV_LINK_TRAIN) { train_link = prev_v; has_train = true; }
  if (has_train) {
    qa_bot_transport_step step; memset(&step, 0, sizeof step);
    if (input.transport) step = input.transport(input.transport_ctx, train_link, input.origin);
    if (!step.has || step.kind == QA_TRANSPORT_UNAVAILABLE) {
      qa_bot_move_output o = make_output(QA_BOT_PATH_STUCK);
      o.has_target = true; o.target = target; o.has_link = true; o.link = train_link;
      return o;
    }
    if (step.kind == QA_TRANSPORT_WAIT || step.kind == QA_TRANSPORT_RIDE) {
      if (state->lift_wait_since < 0 || qa_bvec_distance(input.origin, state->stuck_origin) > 8) {
        state->lift_wait_since = input.now; state->stuck_origin = input.origin;
      }
      if (input.now - state->lift_wait_since > QA_LIFT_WAIT_SECONDS) {
        qa_bot_move_output o = make_output(QA_BOT_PATH_STUCK);
        o.has_target = true; o.target = target; o.has_link = true; o.link = train_link;
        return o;
      }
      state->stuck_since = input.now;
      qa_bot_move_output o = make_output(QA_BOT_PATH_MOVING);
      o.has_riding = true; o.riding = step.kind == QA_TRANSPORT_RIDE;
      o.has_target = true; o.target = target; o.has_link = true; o.link = train_link;
      return o;
    }
    target = step.target;
    state->lift_wait_since = -1;
  }
  if (has_link && link_v.type == QA_NAV_LINK_WALK_OFF_LEDGE && input.on_ground && input.origin.z - target.z > 64) {
    qa_bot_vec3 prev = state->index > 0 ? path->points[state->index - 1] : input.origin;
    double ax = target.x - prev.x, ay = target.y - prev.y;
    double al = hypot(ax, ay);
    if (al < 1 && input.has_velocity) { ax = input.velocity.x; ay = input.velocity.y; al = hypot(ax, ay); }
    if (al >= 1) target = (qa_bot_vec3){target.x + (ax / al) * QA_LEDGE_OVERSHOOT, target.y + (ay / al) * QA_LEDGE_OVERSHOOT, target.z};
  }
  qa_nav_graph_link lift_link; bool has_lift = false;
  if (has_link && link_v.type == QA_NAV_LINK_ELEVATOR) { lift_link = link_v; has_lift = true; }
  else if (has_prev && prev_v.type == QA_NAV_LINK_ELEVATOR) { lift_link = prev_v; has_lift = true; }
  if (has_lift && lift_link.has_traversal && input.on_ground) {
    qa_nav_traversal ride = lift_link.traversal;
    bool below_end = ride.end.z - input.origin.z > 64;
    if (below_end && qa_bvec_distance2d(input.origin, ride.start) <= QA_LIFT_BOARD_RADIUS) {
      if (state->lift_wait_since < 0 || fabs(input.origin.z - state->lift_wait_z) > 8) {
        state->lift_wait_since = input.now;
        state->lift_wait_z = input.origin.z;
      }
      if (input.now - state->lift_wait_since <= QA_LIFT_WAIT_SECONDS) {
        state->stuck_origin = input.origin;
        state->stuck_since = input.now;
        qa_bot_move_output o = make_output(QA_BOT_PATH_MOVING);
        o.has_riding = true; o.riding = true;
        o.has_target = true; o.target = target;
        if (has_link) { o.has_link = true; o.link = link_v; }
        return o;
      }
    } else {
      state->lift_wait_since = -1;
    }
  } else {
    state->lift_wait_since = -1;
  }
  if (qa_bvec_distance(input.origin, state->stuck_origin) > 24) {
    state->stuck_origin = input.origin;
    state->stuck_since = input.now;
  } else if (input.now - state->stuck_since >= input.stuck_time) {
    state->stuck_count++;
    state->stuck_origin = input.origin;
    state->stuck_since = input.now;
    qa_bot_move_output o = make_output(QA_BOT_PATH_STUCK);
    o.has_target = true; o.target = target;
    if (has_link) { o.has_link = true; o.link = link_v; }
    return o;
  }
  double speed = movement.walk_only ? (input.has_walk_speed ? input.walk_speed : QA_BOT_WALK_SPEED)
                                     : (input.has_run_speed ? input.run_speed : QA_BOT_RUN_SPEED);
  qa_angle_vectors av = qa_bot_angle_vectors(0, input.yaw, 0);
  bool swimming = (input.has_water_level ? input.water_level : 0) >= 2;
  qa_bot_vec3 dir;
  double upmove = 0;
  if (swimming) {
    double dx = target.x - input.origin.x, dy = target.y - input.origin.y, dz = target.z - input.origin.z;
    double len = hypot(hypot(dx, dy), dz);
    dir = len > 0 ? (qa_bot_vec3){dx / len, dy / len, dz / len} : qa_bvec(0, 0, 0);
    upmove = qa_bot_clamp(dir.z * speed, -speed, speed);
    if (input.has_air_seconds && input.air_seconds < QA_SWIM_AIR_RESERVE && input.has_air_above && input.air_above && dir.z > -0.5) {
      double push = QA_SWIM_SURFACE_PUSH * speed;
      if (push > upmove) upmove = push;
    }
  } else {
    dir = qa_steer_direction(input.origin, target);
  }
  double wish_speed = speed;
  if (state->index + 1 < (int64_t)path->point_count) {
    qa_bot_vec3 next = path->points[state->index + 1];
    if (qa_bvec_distance2d(input.origin, target) < QA_CORNER_SLOW_DISTANCE) {
      double ax = target.x - input.origin.x, ay = target.y - input.origin.y;
      double bx = next.x - target.x, by = next.y - target.y;
      double la = hypot(ax, ay), lb = hypot(bx, by);
      if (la > 1 && lb > 1) {
        double cosv = (ax * bx + ay * by) / (la * lb);
        if (cosv < cos((QA_CORNER_TURN_DEGREES * acos(-1.0)) / 180)) {
          double walk = input.has_walk_speed ? input.walk_speed : QA_BOT_WALK_SPEED;
          if (walk < wish_speed) wish_speed = walk;
        }
      }
    }
  }
  double wx = dir.x * wish_speed, wy = dir.y * wish_speed;
  if (input.has_velocity) {
    double along = input.velocity.x * dir.x + input.velocity.y * dir.y;
    wx -= input.velocity.x - along * dir.x;
    wy -= input.velocity.y - along * dir.y;
    double wl = hypot(wx, wy);
    if (wl > speed) { wx *= speed / wl; wy *= speed / wl; }
  }
  double forwardmove = qa_bot_clamp(wx * av.forward.x + wy * av.forward.y, -speed, speed);
  double sidemove = qa_bot_clamp(wx * av.right.x + wy * av.right.y, -speed, speed);
  bool has_posture = has_link || has_prev;
  qa_nav_link_type posture = has_link ? link_v.type : prev_v.type;
  if (has_posture && posture == QA_NAV_LINK_CROUCH) upmove = -speed;
  if (has_posture && posture == QA_NAV_LINK_LADDER) upmove = qa_bot_clamp((target.z - input.origin.z) * 4, -speed, speed);
  bool jump = false;
  if (input.on_ground) {
    bool needs_jump = has_link && qa_nav_link_is_jump(link_v.type);
    bool step_up = target.z - input.origin.z > 24 && qa_bvec_distance2d(input.origin, target) < 96;
    if (needs_jump || (step_up && !(has_posture && posture == QA_NAV_LINK_LADDER))) jump = true;
  }
  qa_bot_move_output o = make_output(QA_BOT_PATH_MOVING);
  o.forwardmove = forwardmove; o.sidemove = sidemove; o.upmove = upmove; o.jump = jump;
  o.has_target = true; o.target = target;
  if (has_link) { o.has_link = true; o.link = link_v; }
  return o;
}
bool qa_roll_combat_jump(qa_bot_path_state *state, qa_bot_movement_settings movement, qa_bot_random_t rng, double now, bool on_ground) {
  if (!movement.allow_jumping_in_combat) return false;
  if (!on_ground) return false;
  if (now < state->jump_ready_at) return false;
  state->jump_ready_at = now + movement.jump_cooldown;
  return qa_bot_random_chance(rng, movement.jump_chance);
}
qa_bot_steer qa_steer_direct(qa_bot_vec3 origin, double yaw, qa_bot_vec3 target, bool walk_only, double run_speed, double walk_speed) {
  qa_bot_vec3 dir = qa_steer_direction(origin, target);
  double speed = walk_only ? walk_speed : run_speed;
  qa_angle_vectors av = qa_bot_angle_vectors(0, yaw, 0);
  return (qa_bot_steer){qa_bot_clamp((dir.x * av.forward.x + dir.y * av.forward.y) * speed, -speed, speed),
                        qa_bot_clamp((dir.x * av.right.x + dir.y * av.right.y) * speed, -speed, speed)};
}
