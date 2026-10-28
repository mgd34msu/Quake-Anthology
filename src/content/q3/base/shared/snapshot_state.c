/* Port of src/content/q3/base/shared/snapshot-state.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/shared/snapshot_state.h"
#include <math.h>
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/core/math.h"

static double source_snap_component(double component) {
  double value = (double)(float)component;
  return value >= -2147483648.0 && value < 2147483648.0 ? trunc(value) + 0 : -2147483648.0;
}
static qa_vec3 copy_position(qa_vec3 value, bool snap) {
  if (snap)
    return qa_math_vec3(source_snap_component(value.x), source_snap_component(value.y), source_snap_component(value.z));
  qa_vec3 copy = {value.x, value.y, value.z};
  return copy;
}
static int32_t to_int32(double v) {
  if (!isfinite(v)) return 0;
  double m = fmod(trunc(v), 4294967296.0);
  if (m >= 2147483648.0) m -= 4294967296.0;
  if (m < -2147483648.0) m += 4294967296.0;
  return (int32_t)m;
}
static void convert(qa_q3_base_source_player_state *ps, qa_q3_entity_state_record *s, bool snap,
    bool has_extrapolation, double extrapolation_time) {
  double health = qa_q3_base_player_state_record_health(ps);
  s->e_type = ps->pm_type == QA_Q3_PM_INTERMISSION || ps->pm_type == QA_Q3_PM_SPECTATOR
      || health <= QA_Q3_BASE_GIB_HEALTH ? QA_Q3_BASE_ET_INVISIBLE : QA_Q3_BASE_ET_PLAYER;
  s->number = to_int32(ps->client_num);
  qa_vec3 origin = qa_q3_base_player_state_record_origin(ps);
  qa_vec3 velocity = qa_q3_base_player_state_record_velocity(ps);
  s->pos.type = has_extrapolation ? QA_Q3_BASE_TR_LINEAR_STOP : QA_Q3_BASE_TR_INTERPOLATE;
  s->pos.base = copy_position(origin, snap);
  s->pos.delta = (qa_vec3){velocity.x, velocity.y, velocity.z};
  if (has_extrapolation) { s->pos.time = to_int32(extrapolation_time); s->pos.duration = 50; }
  s->apos.type = QA_Q3_BASE_TR_INTERPOLATE;
  s->apos.base = copy_position(ps->viewangles, snap);
  s->angles2.y = (double)(float)ps->movement_dir;
  s->legs_anim = to_int32(ps->legs_anim);
  s->torso_anim = to_int32(ps->torso_anim);
  s->client_num = to_int32(ps->client_num);
  s->e_flags = health <= 0 ? (to_int32(ps->e_flags) | 1) : (to_int32(ps->e_flags) & ~1);
  if (ps->external_event != 0) {
    s->event = to_int32(ps->external_event);
    s->event_parm = to_int32(ps->external_event_parm);
  } else if (ps->entity_event_sequence < ps->event_sequence) {
    double oldest = (double)to_int32(ps->event_sequence - 2);
    if (ps->entity_event_sequence < oldest) ps->entity_event_sequence = oldest;
    int32_t ees = to_int32(ps->entity_event_sequence);
    int32_t slot = ees & 1;
    double ev = qa_q3_base_player_state_slots_get(&ps->events, (double)slot);
    s->event = to_int32(ev) | ((ees & 3) << 8);
    s->event_parm = to_int32(qa_q3_base_player_state_slots_get(&ps->event_parms, (double)slot));
    ps->entity_event_sequence = (double)to_int32(ps->entity_event_sequence + 1);
  }
  s->weapon = to_int32(ps->weapon);
  s->ground_entity_num = to_int32(ps->ground_entity_num);
  s->powerups = 0;
  for (double index = 0; index < ps->powerups.length; index++) {
    if (qa_q3_base_player_state_slots_get(&ps->powerups, index) != 0)
      s->powerups |= (int32_t)(1u << (unsigned)index);
  }
  s->loop_sound = to_int32(ps->loop_sound);
  s->generic1 = to_int32(ps->generic1);
}
void qa_q3_base_player_state_to_entity_state(qa_q3_base_source_player_state *ps,
    qa_q3_entity_state_record *destination, bool snap) {
  convert(ps, destination, snap, false, 0);
}
void qa_q3_base_player_state_to_entity_state_extrapolate(qa_q3_base_source_player_state *ps,
    qa_q3_entity_state_record *destination, double time, bool snap) {
  convert(ps, destination, snap, true, time);
}
