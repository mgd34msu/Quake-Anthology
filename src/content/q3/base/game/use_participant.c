/* Port of src/content/q3/base/game/use-participant.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/use_participant.h"
#include "qa/foundation/support.h"
qa_actor_id qa_q3_use_actor(qa_q3_use_participant p) { return p.entity ? qa_q3_game_entity_actor(p.entity).id : p.actor; }
qa_q3_use_participant qa_q3_require_use_participant(const qa_q3_use_participant *p) {
  if (!p) qa_range_error("Target handler requires an activator");
  return *p;
}
qa_q3_game_entity *qa_q3_use_client(qa_q3_use_participant p, const qa_q3_use_participant_services *services) {
  if (p.entity) return p.entity->client == NULL ? NULL : p.entity;
  if (!services) qa_range_error("Shared target activator requires actor observations");
  if (!services->live(services->ctx, p.actor) || !services->is_player(services->ctx, p.actor)) return NULL;
  qa_q3_game_entity *e = services->native(services->ctx, p.actor);
  if (!e || !e->client) qa_range_error("Admitted Q3 map player has no native client behavior record");
  return e;
}
