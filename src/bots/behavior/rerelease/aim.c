/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/aim.h"
#include <math.h>
qa_bot_aim_state qa_new_aim_state(double p, double y) { return (qa_bot_aim_state){p, y, 0, 0, 0}; }
qa_bot_vec3 qa_aim_lead_point(qa_bot_vec3 t, qa_bot_vec3 v, qa_bot_aiming_settings s) { if (s.velocity_offset == 0) return t; return qa_bvec_ma(t, s.velocity_offset, v); }
typedef struct { double angle, velocity; } Adv;
static Adv advance_axis(double angle, double velocity, double error_to, double stiffness, double damping, double max_speed, int substeps, double h) {
  double a = angle, v = velocity;
  for (int i = 0; i < substeps; i++) { double error = error_to - (a - angle); double accel = stiffness * error - damping * v; v += accel * h; if (max_speed > 0) v = qa_bot_clamp(v, -max_speed, max_speed); a += v * h; }
  return (Adv){a, v};
}
qa_bot_pitch_yaw qa_aim_step(qa_bot_aim_state *st, qa_bot_vec3 ideal_dir, qa_bot_aiming_settings s, double dt, double now) {
  if (dt <= 0) return (qa_bot_pitch_yaw){st->pitch, st->yaw};
  qa_bot_pitch_yaw ideal = qa_bot_vector_to_angles(ideal_dir);
  double pe = qa_bot_angle_delta(st->pitch, ideal.pitch), ye = qa_bot_angle_delta(st->yaw, ideal.yaw);
  double worst = fmax(fabs(pe), fabs(ye));
  if (s.modifier_apply_time > 0 && s.modifier_max_angle > 0 && worst > s.modifier_max_angle) st->modifier_until = now + s.modifier_apply_time;
  bool modified = st->modifier_until > now;
  double max_speed = s.max_acceleration * (modified ? s.modifier_accel_scalar : 1);
  double stiffness = s.spring_stiffness * (modified ? s.modifier_spring_scalar : 1);
  double damping = s.damping * (modified ? s.modifier_damping_scalar : 1);
  double omega = sqrt(fmax(stiffness, 1)); int sub = (int)ceil((dt * omega) / 0.25); if (sub < 1) sub = 1; if (sub > 64) sub = 64; double h = dt / sub;
  Adv p = advance_axis(st->pitch, st->pitch_velocity, pe, stiffness, damping, max_speed, sub, h);
  Adv y = advance_axis(st->yaw, st->yaw_velocity, ye, stiffness, damping, max_speed, sub, h);
  st->pitch = qa_bot_clamp(p.angle, -80, 80); st->pitch_velocity = st->pitch == p.angle ? p.velocity : 0;
  st->yaw = qa_bot_angle_mod(y.angle); st->yaw_velocity = y.velocity;
  return (qa_bot_pitch_yaw){st->pitch, st->yaw};
}
double qa_aim_error(const qa_bot_aim_state *st, qa_bot_vec3 ideal_dir) { qa_bot_pitch_yaw i = qa_bot_vector_to_angles(ideal_dir); return fmax(fabs(qa_bot_angle_delta(st->pitch, i.pitch)), fabs(qa_bot_angle_delta(st->yaw, i.yaw))); }
