/* Port of src/content/q1/addons/monsters/bosses/effects.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/effects.h"

void qa_q1am_pain_lightning(qa_q1am_context *context, qa_q1am_actor *entity, qa_vec3 offset) {
  qa_q1am_game *game = context->game;
  qa_vec3 forward = qa_q1am_game_make_vectors(
      game, (qa_vec3){game->random(game->ctx) * 180 + 180, game->random(game->ctx) * 360, 0}).forward;
  qa_vec3 origin = qa_add3(game->body(game->ctx, entity).origin, offset);
  qa_q1am_trace_request request = {origin, qa_add3(origin, qa_scale3(forward, 1000)),
                                   QA_Q1AM_POINT, true, entity->actor.id, false};
  qa_q1am_trace trace = game->trace(game->ctx, &request);
  game->sound(game->ctx, entity, "misc/power.wav", "body", 1);
  qa_q1am_event event = {0};
  event.kind = "lightning";
  event.has_actor = true;
  event.actor = entity->actor.id;
  event.style = 3;
  event.start = origin;
  event.end = trace.end;
  context->services_emit(context->services_ctx, &event);
}
