/* Port of src/content/q2/foundation/weapons/presentation.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/weapons/presentation.h"
#include "qa/content/q2/foundation/weapons/generic_frame.h"
#include "qa/core/math.h"

void qa_q2w_set_recoil(qa_q2w_state *s, qa_q2f_edition edition, double now, qa_vec3 origin, qa_vec3 angles, double duration) {
  s->kick_origin = origin; s->kick_angles = angles; s->kick_time = now; s->kick_duration = duration;
  s->kick_until = edition == QA_Q2F_RERELEASE ? qa_q2w_millisecond_sum(now, duration) : now + duration;
}
void qa_q2w_set_recoil_default(qa_q2w_state *s, qa_q2f_edition edition, double now, qa_vec3 origin, qa_vec3 angles) {
  qa_q2w_set_recoil(s, edition, now, origin, angles, edition == QA_Q2F_RERELEASE ? 0.2 : 0);
}
qa_q2w_recoil_result qa_q2w_weapon_recoil(const qa_q2w_state *s, qa_q2f_edition edition, double now) {
  double impulse = now == s->kick_time ? 1 : 0;
  double ratio = (s->kick_until - now) / s->kick_duration;
  double factor = s->kick_duration == 0 ? impulse : (ratio < 0 ? 0 : ratio > 1 ? 1 : ratio);
  qa_q2w_recoil_result r;
  r.kick_origin = qa_scale3_double(s->kick_origin, edition == QA_Q2F_CLASSIC ? impulse : factor);
  r.kick_angles = qa_scale3_double(s->kick_angles, factor);
  return r;
}
qa_q2w_frame_range qa_q2w_attack_frames(bool ducked, double offset) {
  qa_q2w_frame_range r = {(ducked ? 160 : 46) - offset, ducked ? 168 : 53};
  return r;
}
qa_q2w_frame_range qa_q2w_reverse_frames(bool ducked) {
  qa_q2w_frame_range r = {ducked ? 173 : 66, ducked ? 169 : 62};
  return r;
}
double qa_q2w_animation_rate(qa_q2w_animation_rate_input in) {
  double rate = in.quick_switch && in.frame_seconds <= 0.05 && (in.phase == QA_Q2W_ACTIVATING || in.phase == QA_Q2W_DROPPING) ? 20 : 10;
  if (in.frame != 0) { if (in.quad_fire_until > in.now) rate *= 2; if (in.haste) rate *= 2; }
  return rate;
}
const char *qa_q2w_powerup_sound(qa_q2w_powerup_sound_input in) {
  if (in.quad_until > in.now && in.double_until > in.now && in.rerelease) return "ctf/tech2x.wav";
  if (in.quad_until > in.now) return "items/damage3.wav";
  if (in.double_until > in.now) return "misc/ddamage3.wav";
  return NULL;
}
