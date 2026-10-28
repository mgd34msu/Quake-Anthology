/* Port of src/app/bootstrap/network/q2-effects.ts. The content
 * presentation 'effect' event and weapon 'beam' event ride as the
 * value structs below (their content owners use the same fields).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q2_EFFECTS_H
#define QA_APP_BOOTSTRAP_NETWORK_Q2_EFFECTS_H
#include <stdbool.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/network/q2/temp_entities.h"
typedef struct qa_app_q2_beam_event {
  const char *effect; /* rail|bubble-trail|bfg-laser|bfg-zap */
  bool has_actor; qa_actor_id actor;
  qa_vec3 start, end; double duration;
} qa_app_q2_beam_event;
/* false = TS null. Missing endpoints abort with the TS message. */
bool qa_app_q2_beam_from_wire(const qa_q2_temp_entity *value, qa_app_q2_beam_event *out);
typedef struct qa_app_q2_effect_event {
  const char *effect; qa_vec3 origin, direction; double count, color;
} qa_app_q2_effect_event;
/* false = TS null. On success out->raw is NULL with length 0. */
bool qa_app_q2_effect_to_wire(const qa_app_q2_effect_event *event, qa_q2_temp_entity *out);
bool qa_app_q2_effect_from_wire(const qa_q2_temp_entity *value, qa_app_q2_effect_event *out);
#endif
