/* Port of src/network/q3/state/entity.ts (q_shared.h entityState_t).
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_STATE_ENTITY_H
#define QA_NETWORK_Q3_STATE_ENTITY_H
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/network/q3/state/trajectory.h"
/* TS EntityStateRecord<T> / EntityState / SourceEntityState: the one C type
 * for TS EntityState, used by the game (GameEntity.s), the presentation and
 * the wire. Fields are TS numbers, so storage is double; the wire codecs
 * apply `| 0` / Math.fround where TS does. Owned storage; vectors and
 * trajectories are value-copied on copy_from. */
typedef struct qa_q3_entity_state_record {
  double number, e_type, e_flags;
  qa_q3_wire_trajectory pos, apos;
  double time, time2;
  qa_vec3 origin, origin2, angles, angles2;
  double other_entity_num, other_entity_num2, ground_entity_num;
  double constant_light, loop_sound, modelindex, modelindex2;
  double client_num, frame, solid, event, event_parm;
  double powerups, weapon, legs_anim, torso_anim, generic1;
} qa_q3_entity_state_record;
typedef qa_q3_entity_state_record qa_q3_source_entity_state;
typedef qa_q3_entity_state_record qa_q3_wire_entity_state;
void qa_q3_entity_state_record_init(qa_q3_entity_state_record *s, double zero_type);
void qa_q3_entity_state_init(qa_q3_entity_state_record *s); /* super(0) */
void qa_q3_entity_state_record_copy_from(qa_q3_entity_state_record *dst, const qa_q3_entity_state_record *src);
void qa_q3_copy_entity_state_fields(qa_q3_entity_state_record *dst, const qa_q3_entity_state_record *src);
void qa_q3_entity_state_record_copy(qa_q3_entity_state_record *out, const qa_q3_entity_state_record *s);
#endif
