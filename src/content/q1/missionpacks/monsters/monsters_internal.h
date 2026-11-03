/*
 * Private to src/content/q1/missionpacks/monsters: value adapters between
 * the Q1 value types (qa_q1_body, optional actor pointers) and the shared
 * world tables on qa_q1_host (TS game.host.bodies / combat / actors), and
 * small C accommodations (fixed frame-name buffers, String(n) field
 * writes). No logic beyond the TS call and the type conversion.
 * GPL-2.0-or-later.
 */
#ifndef QA_Q1MPM_MONSTERS_INTERNAL_H
#define QA_Q1MPM_MONSTERS_INTERNAL_H

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/foundation/spawns.h"
#include "qa/content/q1/foundation/weapons.h"
#include "qa/content/q1/missionpacks/hipnotic_weapons.h"
#include "qa/content/q1/missionpacks/monsters/runtime.h"
#include "qa/content/q1/missionpacks/rogue_weapons.h"
#include "qa/content/q1/missionpacks/types.h"
#include "qa/content/q1/missionpacks/world/hipnotic_misc.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

/* Zeroed storage owned by the runtime (freed by qa_q1mpm_runtime_destroy):
 * a factory definition's callbacks record lives as long as the runtime. */
void *qa_q1mpm_runtime_calloc(qa_q1mpm_runtime *runtime, size_t count, size_t size);

/* TS game.host.bodies.read(actor): null -> false. */
static inline bool q1mpm_read_body(qa_q1_services *game, qa_actor_id actor, qa_q1_body *out) {
  const qa_body_state_result result = qa_shared_body_table_read(game->host.bodies, actor);
  if (!result.has_value) return false;
  *out = qa_q1_body_from_state(result.value);
  return true;
}
/* TS game.host.combat.read(actor)?.canTakeDamage === true. */
static inline bool q1mpm_can_take_damage(qa_q1_services *game, qa_actor_id actor) {
  const qa_combat_state_result result = qa_gameplay_authority_read(game->host.combat, actor);
  return result.has_value && result.value.can_take_damage;
}
/* TS game.host.trace(request) with the optional members of Q1TraceRequest
 * left absent (missile undefined). ignore NULL = TS null. */
static inline qa_q1_trace q1mpm_trace(qa_q1_services *game, qa_vec3 start, qa_vec3 end, qa_bounds bounds,
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
/* TS game.host.emit({ kind: "sound", actor, path, channel, volume, attenuation }). */
static inline void q1mpm_emit_sound(qa_q1_services *game, qa_actor_id actor, const char *path,
                                    qa_q1_sound_channel channel, double volume, double attenuation) {
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "sound";
  event.has_actor = true;
  event.actor = actor;
  event.path = path;
  event.sound_channel = channel;
  event.volume = volume;
  event.attenuation = attenuation;
  qa_q1_emit(game, event);
}
/* TS game.host.checkClient(observer): false = TS null. */
static inline bool q1mpm_check_client(qa_q1_services *game, qa_owned_actor observer, qa_actor_id *out) {
  return game->host.check_client(game->host.ctx, observer, out);
}
/* TS game.host.players()[0] ?? null as a pointer into the host's list. */
static inline const qa_actor_id *q1mpm_first_player(qa_q1_services *game) {
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  return count == 0 ? NULL : &players[0];
}
/* TS `a === b` over two strings. */
static inline bool q1mpm_str_eq(const char *a, const char *b) {
  return a != NULL && b != NULL && strcmp(a, b) == 0;
}
/* TS `a === b` over ActorId | null. */
static inline bool q1mpm_same_opt(const qa_actor_id *a, const qa_actor_id *b) {
  if (a == NULL || b == NULL) return a == b;
  return qa_same_actor(*a, *b);
}
/* TS entity.fields.set(key, String(value)). */
static inline void q1mpm_set_number_text(qa_q1_actor *entity, const char *key, double value) {
  char text[40];
  qa_net_js_number(value, text, sizeof text);
  (void)qa_q1_actor_field_set(entity, key, text);
}
/* TS entity.fields.set(key, String(Math.fround(value))). */
static inline void q1mpm_set_fround_text(qa_q1_actor *entity, const char *key, double value) {
  q1mpm_set_number_text(entity, key, (double)(float)value);
}
/* Replace an owned actor string member (TS entity.model = value, ...). */
static inline void q1mpm_set_text(char **slot, const char *value) {
  if (value == NULL) value = "";
  const size_t length = strlen(value) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, value, length);
  free(*slot);
  *slot = copy;
}
/* TS monster.nextFrame = name (fixed buffer on qa_q1_base_monster). */
static inline void q1mpm_set_next_frame(qa_q1_base_monster *monster, const char *name) {
  size_t length = strlen(name);
  if (length >= sizeof monster->next_frame) length = sizeof monster->next_frame - 1;
  memcpy(monster->next_frame, name, length);
  monster->next_frame[length] = '\0';
}
/* TS entity.references.get(key) ?? null as a pointer into *slot. */
static inline const qa_actor_id *q1mpm_reference(const qa_q1_actor *entity, const char *key, qa_actor_id *slot) {
  return qa_q1_actor_reference_get(entity, key, slot) ? slot : NULL;
}
/* TS entity.monster?.enemy ?? null. */
static inline const qa_actor_id *q1mpm_state_enemy(const qa_q1_actor *entity) {
  return entity == NULL || entity->monster == NULL || !entity->monster->has_enemy ? NULL : &entity->monster->enemy;
}
/* TS game.player(actor)?.powerups.get(name) ?? 0. */
static inline double q1mpm_player_powerup(qa_q1_services *game, qa_actor_id actor, const char *name) {
  const qa_q1_player_state *player = qa_q1_player(game, actor);
  return player == NULL ? 0 : qa_q1_player_powerup_get(player, name);
}

#endif
