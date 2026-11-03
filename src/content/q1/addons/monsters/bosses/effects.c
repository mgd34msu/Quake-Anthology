/* Port of src/content/q1/addons/monsters/bosses/effects.ts.
 * boss_final.qc pain_lightning. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/effects.h"

#include <string.h>

#include "../support.h"

void qa_q1am_pain_lightning(qa_q1ad_context *context, qa_q1_actor *entity, qa_vec3 offset) {
  qa_q1_services *game = context->game;
  qa_vec3 angles;
  angles.x = qa_q1am_random(game) * 180 + 180;
  angles.y = qa_q1am_random(game) * 360;
  angles.z = 0;
  const qa_vec3 forward = qa_q1_make_vectors(game, angles).forward;
  const qa_vec3 origin = qa_add3(qa_q1_get_body(game, entity).origin, offset);
  const qa_q1_trace trace = qa_q1am_trace(game, origin, qa_add3(origin, qa_scale3(forward, 1000)), QA_Q1_POINT,
                                          &entity->actor.id, false);
  qa_q1_sound(game, entity->actor.id, "misc/power.wav", "body", 1, 1);
  qa_q1ad_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q1AD_EVENT_LIGHTNING;
  event.has_actor = true;
  event.actor = entity->actor.id;
  event.style = 3;
  event.start = origin;
  event.end = trace.end;
  context->services.emit(context->services.ctx, &event);
}
