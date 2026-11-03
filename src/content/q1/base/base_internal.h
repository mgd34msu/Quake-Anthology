/* Private to src/content/q1/base: value adapters between the Q1 value
 * types (qa_q1_body, optional actor pointers) and the shared world
 * tables on qa_q1_host (TS game.host.bodies / combat). No logic beyond
 * the TS call and the type conversion. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_BASE_INTERNAL_H
#define QA_CONTENT_Q1_BASE_BASE_INTERNAL_H

#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/entity_services.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

/* TS game.host.bodies.read(actor): null -> false. */
static inline bool q1base_read_body(qa_q1_services *game, qa_actor_id actor, qa_q1_body *out) {
  const qa_body_state_result result = qa_shared_body_table_read(game->host.bodies, actor);
  if (!result.has_value) return false;
  *out = qa_q1_body_from_state(result.value);
  return true;
}
/* TS game.host.bodies.write(actor, body). */
static inline void q1base_write_body(qa_q1_services *game, qa_owned_actor actor, qa_q1_body body) {
  qa_shared_body_table_write(game->host.bodies, actor, qa_q1_body_to_state(&body));
}
/* TS game.host.combat.read(actor): null -> false. */
static inline bool q1base_read_combat(qa_q1_services *game, qa_actor_id actor, qa_combat_state *out) {
  const qa_combat_state_result result = qa_gameplay_authority_read(game->host.combat, actor);
  if (!result.has_value) return false;
  *out = result.value;
  return true;
}
/* TS game.host.combat.setHealth(actor, health). */
static inline void q1base_set_health(qa_q1_services *game, qa_owned_actor actor, double health) {
  qa_gameplay_authority_set_health(game->host.combat, actor, health);
}
/* TS game.host.combat.setTraits(actor, { canTakeDamage }). */
static inline void q1base_set_can_take_damage(qa_q1_services *game, qa_owned_actor actor, bool value) {
  qa_combat_trait_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_can_take_damage = true;
  changes.can_take_damage = value;
  qa_gameplay_authority_set_traits(game->host.combat, actor, changes);
}
/* TS game.host.actors.resolveSaved({ slot, generation }): null -> false. */
static inline bool q1base_resolve_saved(qa_q1_services *game, double slot, double generation, qa_owned_actor *out) {
  const qa_saved_actor_id saved = {slot, generation};
  return qa_session_actor_registry_resolve_saved(game->host.actors, saved, out);
}
/* TS game.host.players() as a borrowed list. */
static inline const qa_actor_id *q1base_players(qa_q1_services *game, size_t *out_count) {
  *out_count = 0;
  return game->host.players(game->host.ctx, out_count);
}
/* TS String(n) for a field write. */
static inline void q1base_set_number_field(qa_q1_actor *entity, const char *key, double value) {
  char text[40];
  qa_net_js_number(value, text, sizeof text);
  (void)qa_q1_actor_field_set(entity, key, text);
}
/* Replace an owned actor string field (classname/model/target...). */
static inline void q1base_set_text(char **slot, const char *value) {
  if (value == NULL) value = "";
  const size_t length = strlen(value) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, value, length);
  free(*slot);
  *slot = copy;
}

#endif
