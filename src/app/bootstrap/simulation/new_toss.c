/* Port of src/app/bootstrap/simulation/new-toss.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/new_toss.h"
#include <math.h>
#include <string.h>
typedef struct ctxn { qa_numeric_operations n; } ctxn;
static qa_vec3 vec(qa_numeric_operations *n, double x, double y, double z) {
  return (qa_vec3){n->store(n->ctx, x), n->store(n->ctx, y), n->store(n->ctx, z)};
}
static qa_vec3 scale_v(qa_numeric_operations *n, qa_vec3 v, double a) {
  return vec(n, n->multiply(n->ctx, v.x, a), n->multiply(n->ctx, v.y, a), n->multiply(n->ctx, v.z, a));
}
static qa_vec3 add_v(qa_numeric_operations *n, qa_vec3 l, qa_vec3 r) {
  return vec(n, n->add(n->ctx, l.x, r.x), n->add(n->ctx, l.y, r.y), n->add(n->ctx, l.z, r.z));
}
static double length_v(qa_numeric_operations *n, qa_vec3 v) {
  return n->store(n->ctx, n->square_root(n->ctx, n->add(n->ctx, n->add(n->ctx, n->multiply(n->ctx, v.x, v.x), n->multiply(n->ctx, v.y, v.y)), n->multiply(n->ctx, v.z, v.z))));
}
static bool moving_v(qa_vec3 v) { return v.x != 0 || v.y != 0 || v.z != 0; }
const char *qa_app_sim_step_q2_new_toss(qa_owned_actor actor, double elapsed, qa_q2_motion motion, qa_app_sim_q2_new_toss_services host) {
  qa_numeric_operations *n = &host.numeric;
  qa_body_state_result read = qa_shared_body_table_read(host.bodies, actor.id);
  if (!read.has_value) return "removed";
  if (host.team_slave) return "team-slave";
  qa_body_state body = read.value;
  qa_trace_result trace = host.trace(host.ctx, body.origin, vec(n, body.origin.x, body.origin.y, n->subtract(n->ctx, body.origin.z, 0.25)));
  bool has_ground = false; qa_actor_id ground_id; memset(&ground_id, 0, sizeof ground_id);
  if (body.ground != NULL && qa_session_actor_registry_is_live(host.actors, *body.ground))
    has_ground = host.hit_actor(host.ctx, trace, &ground_id);
  static qa_actor_id ground_storage;
  if (has_ground) { ground_storage = ground_id; body.ground = &ground_storage; } else body.ground = NULL;
  qa_shared_body_table_write(host.bodies, actor, body);
  qa_vec3 normal = trace.contact.has_plane ? trace.contact.plane.normal : trace.source_plane.plane.normal;
  if (has_ground && normal.z == 1 && !moving_v(body.velocity)) return "stopped";
  qa_vec3 old_origin = body.origin;
  qa_vec3 velocity = body.velocity;
  bool rerelease = host.edition && strcmp(host.edition, "rerelease") == 0;
  if (rerelease) {
    double speed = length_v(n, velocity);
    if (speed > host.max_velocity)
      velocity = scale_v(n, vec(n, n->divide(n->ctx, velocity.x, speed), n->divide(n->ctx, velocity.y, speed), n->divide(n->ctx, velocity.z, speed)), host.max_velocity);
  } else {
    double cx = velocity.x > host.max_velocity ? host.max_velocity : velocity.x < -host.max_velocity ? -host.max_velocity : velocity.x;
    double cy = velocity.y > host.max_velocity ? host.max_velocity : velocity.y < -host.max_velocity ? -host.max_velocity : velocity.y;
    double cz = velocity.z > host.max_velocity ? host.max_velocity : velocity.z < -host.max_velocity ? -host.max_velocity : velocity.z;
    velocity = vec(n, cx, cy, cz);
  }
  double gravity = n->multiply(n->ctx, n->multiply(n->ctx, motion.gravity, host.world_gravity), elapsed);
  velocity = (rerelease || motion.gravity_vector.z > 0) ? add_v(n, velocity, scale_v(n, motion.gravity_vector, gravity))
      : vec(n, velocity.x, velocity.y, n->subtract(n->ctx, velocity.z, gravity));
  body.velocity = velocity;
  qa_shared_body_table_write(host.bodies, actor, body);
  if (moving_v(motion.angular_velocity)) {
    qa_vec3 angles = add_v(n, body.angles, scale_v(n, motion.angular_velocity, elapsed));
    double adjustment = n->store(n->ctx, n->multiply(n->ctx, n->multiply(n->ctx, elapsed, host.stop_speed), 6));
    double fx = motion.angular_velocity.x > 0 ? fmax(0, n->store(n->ctx, n->subtract(n->ctx, motion.angular_velocity.x, adjustment))) : fmin(0, n->store(n->ctx, n->add(n->ctx, motion.angular_velocity.x, adjustment)));
    double fy = motion.angular_velocity.y > 0 ? fmax(0, n->store(n->ctx, n->subtract(n->ctx, motion.angular_velocity.y, adjustment))) : fmin(0, n->store(n->ctx, n->add(n->ctx, motion.angular_velocity.y, adjustment)));
    double fz = motion.angular_velocity.z > 0 ? fmax(0, n->store(n->ctx, n->subtract(n->ctx, motion.angular_velocity.z, adjustment))) : fmin(0, n->store(n->ctx, n->add(n->ctx, motion.angular_velocity.z, adjustment)));
    body.angles = angles;
    qa_shared_body_table_write(host.bodies, actor, body);
    host.write_angular_velocity(host.ctx, vec(n, fx, fy, fz));
  }
  double speed = length_v(n, velocity);
  double water_level = 0, water_type0 = 0; host.water(host.ctx, &water_level, &water_type0);
  double loss = water_level != 0 ? n->multiply(n->ctx, 6, water_level) : !has_ground ? 6 : 36;
  double newspeed = n->store(n->ctx, n->divide(n->ctx, fmax(0, n->store(n->ctx, n->subtract(n->ctx, speed, loss))), speed));
  read = qa_shared_body_table_read(host.bodies, actor.id);
  if (!read.has_value) return "removed";
  body = read.value; body.velocity = scale_v(n, velocity, newspeed);
  qa_shared_body_table_write(host.bodies, actor, body);
  host.fly_move(host.ctx, elapsed);
  if (!qa_session_actor_registry_is_live(host.actors, actor.id)) return "removed";
  qa_shared_body_table_link(host.bodies, actor, NULL);
  host.touch_triggers(host.ctx);
  read = qa_shared_body_table_read(host.bodies, actor.id);
  if (!read.has_value) return "removed";
  body = read.value;
  double wl2 = 0, wt2 = 0; host.water(host.ctx, &wl2, &wt2);
  bool was_in_water = (((int)wt2 & 56) != 0);
  double water_type = host.point_contents(host.ctx, body.origin);
  bool in_water = (((int)water_type & 56) != 0);
  host.write_water(host.ctx, in_water ? 1 : 0, water_type);
  if (was_in_water != in_water) host.water_sound(host.ctx, in_water ? old_origin : body.origin);
  return qa_session_actor_registry_is_live(host.actors, actor.id) ? "moved" : "removed";
}
