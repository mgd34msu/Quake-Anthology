/*
 * Port of the accessor subset of src/content/q1/foundation/
 * entity-services.ts: entity(), player(), isPlayer(), body(), and
 * sourceTarget() (the renamed qa_q1_get_* getters; a qa_q1_entity()
 * function would collide with the qa_q1_entity typedef). The rest
 * of Q1EntityServices is declared in entity_services.h and lands
 * with the foundation runtime slice. GPL-2.0-or-later.
 *
 * TS resolves actors through host.actors.resolveOwned and the
 * entities/players OwnedActor maps; the C qa_q1_host mirror keeps
 * bodies/live state actor-local (see types.h), so resolution is a
 * scan of the services' entity/player arrays gated on the actor's
 * live flag (resolveOwned returns null for a released actor).
 */
#include "qa/content/q1/foundation/entity_services.h"

#include <string.h>

#include "qa/foundation/support.h"

qa_q1_actor *qa_q1_get_entity(qa_q1_services *game, const qa_actor_id *actor) {
  if (actor == NULL) return NULL;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (entity != NULL && entity->live && qa_same_actor(entity->actor.id, *actor))
      return entity;
  }
  return NULL;
}
qa_q1_player_state *qa_q1_player(qa_q1_services *game, qa_actor_id actor) {
  for (size_t i = 0; i < game->player_count; i++) {
    qa_q1_player_state *player = game->players[i];
    if (player != NULL && qa_same_actor(player->actor.id, actor)) return player;
  }
  return NULL;
}
bool qa_q1_is_player(qa_q1_services *game, const qa_actor_id *actor) {
  if (actor == NULL) return false;
  if (qa_q1_player(game, *actor) != NULL) return true;
  if (game->host.players == NULL) return false;
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  for (size_t i = 0; i < count; i++)
    if (qa_same_actor(players[i], *actor)) return true;
  return false;
}
qa_q1_body qa_q1_get_body(qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  /* TS: host.bodies.read(entity.actor.id); null throws. The
   * actor-local mirror has no body once the actor is released. */
  if (entity == NULL || !entity->live) qa_range_error("Missing Q1 body");
  return entity->body;
}
qa_q1_source_target qa_q1_get_source_target(qa_q1_services *game, qa_actor_id actor) {
  if (game->host.source_target != NULL) {
    qa_q1_source_target observed;
    if (game->host.source_target(game->host.ctx, actor, &observed)) return observed;
  }
  qa_q1_actor *entity = qa_q1_get_entity(game, &actor);
  qa_q1_source_target result = {
      entity != NULL && entity->aimed_damage,
      entity != NULL && entity->movement != NULL && strcmp(entity->movement, "push") == 0,
      qa_q1_is_player(game, &actor),
      entity != NULL && entity->solid != NULL && strcmp(entity->solid, "slidebox") == 0};
  return result;
}
