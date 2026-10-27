/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/senses.h"
qa_bot_awareness qa_new_awareness(int64_t id, double now, qa_bot_vec3 o) { return (qa_bot_awareness){id, 0, 0, now, -1, -1, o}; }
qa_sight_geometry qa_evaluate_sight_geometry(qa_bot_vec3 eye, double p, double y, qa_bot_vec3 target, bool inv, qa_bot_senses_settings s, qa_bot_weapon_sense_settings w) {
  qa_bot_vec3 dir = qa_bvec_sub(target, eye); double dist = qa_bvec_distance(target, eye); double ang = qa_bot_angle_between(p, y, dir);
  return (qa_sight_geometry){ang <= s.fov_angle / 2, ang <= w.fov_angle / 2, !inv || s.max_invis_enemy_sight_dist <= 0 || dist <= s.max_invis_enemy_sight_dist, dist};
}
qa_bot_awareness *qa_sense_step(qa_bot_awareness *aw, qa_bot_contact c, qa_bot_senses_settings s, qa_bot_weapon_sense_settings w, double dt, double now) {
  bool visible = c.line_of_sight && c.in_sight_fov; double fill = s.sight_time;
  if (c.invisible && s.invis_enemy_sight_scalar > 0) fill *= s.invis_enemy_sight_scalar;
  if (visible) { aw->sight = fill > 0 ? qa_bot_clamp(aw->sight + dt / fill, 0, 1) : 1; aw->last_seen = now; aw->last_contact = now; aw->last_known_origin = c.origin; }
  else if (c.audible) { aw->sight = s.sound_time > 0 ? qa_bot_clamp(aw->sight + dt / s.sound_time, 0, 1) : 1; aw->last_heard = now; aw->last_contact = now; aw->last_known_origin = c.origin; }
  else { double decay = aw->last_seen >= aw->last_heard ? s.sight_decay_time : s.sound_decay_time; aw->sight = decay > 0 ? qa_bot_clamp(aw->sight - dt / decay, 0, 1) : 0; }
  if (c.line_of_sight && c.in_weapon_fov) aw->weapon = w.sight_time > 0 ? qa_bot_clamp(aw->weapon + dt / w.sight_time, 0, 1) : 1;
  else aw->weapon = w.decay_time > 0 ? qa_bot_clamp(aw->weapon - dt / w.decay_time, 0, 1) : 0;
  return aw;
}
bool qa_bot_is_aware(const qa_bot_awareness *aw) { return aw->sight >= 1; }
bool qa_bot_can_fire(const qa_bot_awareness *aw) { return aw->weapon >= 1; }
bool qa_bot_should_forget(const qa_bot_awareness *aw, qa_bot_senses_settings s, double now) { if (aw->sight > 0) return false; return now - aw->last_contact >= s.forget_non_vis_enemy_time; }
bool qa_bot_sound_audible(qa_bot_sound snd, qa_bot_vec3 listener, qa_bot_senses_settings s, double now) { if (now - snd.time > s.sound_persist_time) return false; double range = s.sound_range * (snd.loudness > 0 ? snd.loudness : 1); return qa_bvec_distance(snd.origin, listener) <= range; }
