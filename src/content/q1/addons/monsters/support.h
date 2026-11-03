/* Private C helpers shared by the q1/addons/monsters port (no TS
 * counterpart: they spell TS one-liners over the real foundation API).
 * GPL-2.0-or-later. */
#ifndef QA_SRC_CONTENT_Q1_ADDONS_MONSTERS_SUPPORT_H
#define QA_SRC_CONTENT_Q1_ADDONS_MONSTERS_SUPPORT_H

#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/entity_services.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

/* TS Math.fround. */
#define QA_Q1AM_FR(x) ((double)(float)(x))
/* TS `flags & mask` over a number field. */
#define QA_Q1AM_FLAGS(x) qa_js_to_int32(x)

/* TS `entity.fields.set(name, String(value))`. */
static inline void qa_q1am_field_number(qa_q1_actor *entity, const char *name, double value) {
  char text[40];
  qa_net_js_number(value, text, sizeof text);
  if (!qa_q1_actor_field_set(entity, name, text)) qa_range_error("out of memory");
}
/* TS `entity.fields.set(name, String(Math.fround(value)))`. */
static inline void qa_q1am_field_fround(qa_q1_actor *entity, const char *name, double value) {
  qa_q1am_field_number(entity, name, QA_Q1AM_FR(value));
}
/* TS `entity.fields.set(name, text)`. */
static inline void qa_q1am_field_text(qa_q1_actor *entity, const char *name, const char *text) {
  if (!qa_q1_actor_field_set(entity, name, text)) qa_range_error("out of memory");
}
/* TS `entity.references.set(name, actor)`; actor NULL = TS null. */
static inline void qa_q1am_reference(qa_q1_actor *entity, const char *name, const qa_actor_id *actor) {
  if (!qa_q1_actor_reference_set(entity, name, actor)) qa_range_error("out of memory");
}
/* TS `game.find(targetname)` as a malloc'd array (NULL when empty). */
static inline qa_q1_actor **qa_q1am_find(qa_q1_services *game, const char *targetname, size_t *count) {
  *count = qa_q1_find(game, targetname, NULL, 0);
  if (*count == 0) return NULL;
  qa_q1_actor **found = malloc(*count * sizeof *found);
  if (found == NULL) qa_range_error("out of memory");
  (void)qa_q1_find(game, targetname, found, *count);
  return found;
}
/* TS `game.find(targetname)[0]` (NULL = undefined). */
static inline qa_q1_actor *qa_q1am_find_first(qa_q1_services *game, const char *targetname) {
  qa_q1_actor *found[1];
  return qa_q1_find(game, targetname, found, 1) == 0 ? NULL : found[0];
}
/* TS `game.host.random()`. */
static inline double qa_q1am_random(qa_q1_services *game) { return game->host.random(game->host.ctx); }
/* TS `game.host.walkMove(actor, yaw, distance)`. */
static inline bool qa_q1am_walk_move(qa_q1_services *game, qa_owned_actor actor, double yaw, double distance) {
  return game->host.walk_move(game->host.ctx, actor, yaw, distance);
}
/* TS `game.host.trace({ start, end, bounds, ignore, monsters })`. */
static inline qa_q1_trace qa_q1am_trace(qa_q1_services *game, qa_vec3 start, qa_vec3 end, qa_bounds bounds,
                                        const qa_actor_id *ignore, bool monsters) {
  qa_q1_trace_request request;
  memset(&request, 0, sizeof request);
  request.start = start;
  request.end = end;
  request.bounds = bounds;
  request.has_ignore = ignore != NULL;
  if (ignore != NULL) request.ignore = *ignore;
  request.monsters = monsters;
  return game->host.trace(game->host.ctx, request);
}
/* TS `game.host.bodies.read(actor)`: false = null. */
static inline bool qa_q1am_read_body(qa_q1_services *game, qa_actor_id actor, qa_q1_body *out) {
  const qa_body_state_result result = qa_shared_body_table_read(game->host.bodies, actor);
  if (!result.has_value) return false;
  *out = qa_q1_body_from_state(result.value);
  return true;
}
/* TS `game.host.combat.read(actor)?.canTakeDamage === true`. */
static inline bool qa_q1am_can_take_damage(qa_q1_services *game, qa_actor_id actor) {
  const qa_combat_state_result state = qa_gameplay_authority_read(game->host.combat, actor);
  return state.has_value && state.value.can_take_damage;
}
/* TS `game.setBody(entity, { velocity })`. */
static inline void qa_q1am_set_velocity_body(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 velocity) {
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_velocity = true;
  patch.velocity = velocity;
  qa_q1_set_body(game, entity, &patch);
}
/* TS `actor.monster.enemy` style nullable reads. */
static inline const qa_actor_id *qa_q1am_opt(bool has, const qa_actor_id *id) { return has ? id : NULL; }
/* TS `game.setBody(entity, { origin })`. */
static inline void qa_q1am_set_origin_body(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 origin) {
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = origin;
  qa_q1_set_body(game, entity, &patch);
}
/* TS `same(first, second)` over nullable actor ids. */
static inline bool qa_q1am_same(const qa_actor_id *first, const qa_actor_id *second) {
  return first == NULL || second == NULL ? first == second : qa_same_actor(*first, *second);
}
/* Bounded name copy for the qa_q1_base_monster frame slots. */
static inline void qa_q1am_copy_name(char *dst, size_t size, const char *src) {
  size_t length = strlen(src);
  if (length >= size) length = size - 1;
  memmove(dst, src, length);
  dst[length] = '\0';
}

#endif
