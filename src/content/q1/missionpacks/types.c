/* Port of src/content/q1/missionpacks/types.ts. Also defines
 * qa_q1mp_ext_aim, the missionpacks adapter for TS aim() (owner:
 * src/content/q1/foundation/weapons.ts; the height correction is
 * qa_aim_q1, owner src/world/gameplay/q1-aim.ts) over the qa_q1b
 * host surface. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/types.h"

#include <stdlib.h>

#include "missionpacks_internal.h"
#include "qa/world/gameplay/q1_aim.h"

const qa_q1mp_mission_weapon_definition qa_q1mp_mission_weapons[8] = {
  {"hipnotic:laser", "progs/v_laserg.mdl", "weapon_laser_gun", "progs/g_laserg.mdl", "q1:ammo/cells", 30, 3},
  {"hipnotic:mjolnir", "progs/v_hammer.mdl", "weapon_mjolnir", "progs/g_hammer.mdl", NULL, 30, 9},
  {"hipnotic:proximity", "progs/v_prox.mdl", "weapon_proximity_gun", "progs/g_prox.mdl", "q1:ammo/rockets", 6, 5},
  {"rogue:lava-nailgun", "progs/v_lava.mdl", NULL, "progs/g_nail.mdl", "rogue:ammo/lava-nails", 0, 4},
  {"rogue:lava-supernailgun", "progs/v_lava2.mdl", NULL, "progs/g_nail2.mdl", "rogue:ammo/lava-nails", 0, 2},
  {"rogue:multi-grenade", "progs/v_multi.mdl", NULL, "progs/g_rock.mdl", "rogue:ammo/multi-rockets", 0, 6},
  {"rogue:multi-rocket", "progs/v_multi2.mdl", NULL, "progs/g_rock2.mdl", "rogue:ammo/multi-rockets", 0, 1},
  {"rogue:plasma", "progs/v_plasma.mdl", NULL, "progs/g_light.mdl", "rogue:ammo/plasma", 0, 0},
};

void qa_q1mp_set_mission_reference(qa_q1mp_actor *entity, const char *key, qa_q1mp_opt_actor actor) {
  qa_q1b_references_set(entity, key, actor);
}
qa_q1mp_opt_actor qa_q1mp_mission_reference(qa_q1mp_game *game, qa_q1mp_actor *entity, const char *key) {
  qa_q1mp_opt_actor actor = qa_q1b_references_get(entity, key);
  return actor.has && game->base->host->actors_is_live(game->base->host->ctx, actor.id) ? actor : QA_Q1MP_NONE;
}
void qa_q1mp_set_mission_number(qa_q1mp_actor *entity, const char *key, double value) {
  char buf[32];
  qa_q1b_format_fround(buf, sizeof buf, value);
  qa_q1b_fields_set(entity, key, buf);
}
qa_vec3 qa_q1mp_velocity_angles(qa_vec3 velocity) {
  double yaw = velocity.x == 0 && velocity.y == 0 ? 0 : atan2(velocity.y, velocity.x) * 180 / 3.141592653589793;
  double pitch = velocity.x == 0 && velocity.y == 0 ? (velocity.z > 0 ? 90 : 270)
                 : atan2(velocity.z, hypot(velocity.x, velocity.y)) * 180 / 3.141592653589793;
  return QA_Q1MP_V3(qa_q1mp_fround(pitch < 0 ? pitch + 360 : pitch), qa_q1mp_fround(yaw < 0 ? yaw + 360 : yaw), 0);
}
qa_vec3 qa_q1mp_grenade_velocity(qa_q1mp_game *game, qa_vec3 angles, qa_vec3 aimed) {
  qa_q1b_basis basis = qa_q1mp_vectors(game, angles);
  if (angles.x == 0) {
    qa_vec3 v = qa_q1mp_vscale(aimed, 600);
    v.z = 200;
    return v;
  }
  return qa_q1mp_vadd(qa_q1mp_vadd(qa_q1mp_vadd(qa_q1mp_vscale(basis.forward, 600), qa_q1mp_vscale(basis.up, 200)),
                                   qa_q1mp_vscale(basis.right, (qa_q1mp_random(game) * 2 - 1) * 10)),
                      qa_q1mp_vscale(basis.up, (qa_q1mp_random(game) * 2 - 1) * 10));
}
/* TS aim() (foundation/weapons.ts). sourceTarget has no qa_q1b host
 * override, so the TS fallback applies: aimedDamage from the entity,
 * player from game.isPlayer. */
typedef struct mp_aim_ctx {
  qa_q1mp_game *game;
  qa_owned_actor actor;
  const char *team; /* NULL = TS null */
} mp_aim_ctx;
static qa_body_state_result mp_aim_body(void *ctx, qa_actor_id actor) {
  mp_aim_ctx *aim = ctx;
  qa_body_state_result result = {0};
  qa_q1b_body body;
  if (!aim->game->base->host->bodies_read(aim->game->base->host->ctx, actor, &body))
    return result;
  result.has_value = true;
  result.value.origin = body.origin;
  result.value.angles = body.angles;
  result.value.velocity = body.velocity;
  result.value.bounds = body.bounds;
  result.value.ground = NULL; /* aimQ1 reads origin/bounds only */
  return result;
}
static bool mp_aim_eligible(void *ctx, qa_actor_id target) {
  mp_aim_ctx *aim = ctx;
  qa_q1b_game *base = aim->game->base;
  qa_q1mp_actor *entity = base->entity(base, QA_Q1MP_SOME(target));
  bool aimed_damage = entity != NULL && entity->aimed_damage;
  bool player = base->is_player(base, QA_Q1MP_SOME(target));
  qa_combat_state state;
  bool has_state = base->host->combat_read(base->host->ctx, target, &state);
  bool same_team = (base->options.has_teamplay ? base->options.teamplay : 0) != 0
      && aim->team != NULL && has_state && state.team != NULL
      && strcmp(aim->team, state.team) == 0;
  return !qa_same_actor(target, aim->actor.id) && (aimed_damage || player)
      && has_state && state.can_take_damage && !same_team;
}
static bool mp_aim_trace(void *ctx, qa_vec3 start, qa_vec3 end, qa_actor_id *out) {
  mp_aim_ctx *aim = ctx;
  qa_q1b_trace trace = qa_q1mp_trace(aim->game, start, end,
                                     QA_Q1MP_SOME(aim->actor.id), true, false);
  if (!trace.actor.has) return false;
  *out = trace.actor.id;
  return true;
}
qa_vec3 qa_q1mp_ext_aim(qa_q1mp_game *game, qa_owned_actor actor, qa_vec3 forward) {
  qa_q1b_game *base = game->base;
  qa_q1b_body body;
  if (!base->host->bodies_read(base->host->ctx, actor.id, &body)) return forward;
  qa_combat_state combat;
  const char *team = base->host->combat_read(base->host->ctx, actor.id, &combat)
      ? combat.team : NULL;
  /* TS host.actors.observations().map(id): grow until the entity
   * listing fits, preserving listing order. */
  size_t capacity = 64, count = 0;
  qa_q1mp_actor **entities = NULL;
  for (;;) {
    qa_q1mp_actor **grown = realloc(entities, capacity * sizeof *grown);
    if (grown == NULL) { free(entities); qa_range_error("Out of memory"); }
    entities = grown;
    count = base->entities(base, entities, capacity);
    if (count < capacity) break;
    capacity *= 2;
  }
  qa_actor_id *targets = malloc((count != 0 ? count : 1) * sizeof *targets);
  if (targets == NULL) { free(entities); qa_range_error("Out of memory"); }
  for (size_t i = 0; i < count; i++) targets[i] = entities[i]->actor.id;
  free(entities);
  mp_aim_ctx aim = {game, actor, team};
  qa_q1_aim_targets host = {targets, count, &aim, mp_aim_body, mp_aim_eligible, mp_aim_trace};
  double threshold = game->has_aim_threshold ? game->aim_threshold : 0.93;
  qa_vec3 result = qa_aim_q1(body.origin, forward, threshold, host);
  free(targets);
  return result;
}
void qa_q1mp_move_missile(qa_q1mp_game *game, qa_q1mp_actor *entity, qa_vec3 velocity) {
  qa_q1b_body_patch patch = {0};
  patch.has_velocity = true; patch.velocity = velocity;
  patch.has_angles = true; patch.angles = qa_q1mp_velocity_angles(qa_q1mp_normalize(velocity));
  patch.has_ground = true; patch.ground = QA_Q1MP_NONE;
  game->base->set_body(game->base, entity, &patch);
}
