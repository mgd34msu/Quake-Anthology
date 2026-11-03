/* Port of src/content/q2/foundation/weapons/hand-grenade.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/weapons/hand_grenade.h"
#include <math.h>
#include "qa/foundation/js.h"

double qa_q2w_hand_recovery_seconds(qa_q2w_hand_tempo t) {
  return t.edition == QA_Q2_EDITION_CLASSIC ? 1 : (t.haste ? 0.5 : 1) * (t.quad_fire ? 0.5 : 1);
}
double qa_q2w_hand_frame_seconds(qa_q2w_hand_tempo t) { return trunc(100 * qa_q2w_hand_recovery_seconds(t)) / 1000; }
double qa_q2w_hand_deadline(double now, double seconds, qa_q2_edition edition) {
  return edition == QA_Q2_EDITION_CLASSIC ? now + seconds : (qa_js_round(now * 1000) + qa_js_round(seconds * 1000)) / 1000;
}
double qa_q2w_hand_fuse_deadline(double now, qa_q2_edition edition) { return qa_q2w_hand_deadline(now, 3.2, edition); }

qa_q2w_hand_projectile_spec qa_q2w_calculate_hand_throw(qa_q2w_hand_throw_input in) {
  bool rerelease = in.edition == QA_Q2_EDITION_RERELEASE;
  qa_vec3 angles = in.angles;
  if (rerelease) angles.x = qa_js_max(-62.5, angles.x);
  qa_vec3 off = rerelease ? (qa_vec3){2, 0, -14} : (qa_vec3){8, 8, -8};
  qa_q2w_projection p = in.project.fn(in.project.ctx, angles, off);
  double fuse = in.fuse_deadline - in.now;
  double charged = 400 + (3 - fuse) * (400.0 / 3);
  double speed = trunc(rerelease ? (in.alive ? qa_js_min(800, charged) : 400) : charged);
  qa_q2w_hand_projectile_spec s;
  s.start = p.start; s.direction = p.direction;
  s.damage = 125 * in.damage_multiplier; s.speed = speed; s.fuse = fuse; s.radius = 165;
  s.held = in.held; s.gravity = in.gravity;
  return s;
}
