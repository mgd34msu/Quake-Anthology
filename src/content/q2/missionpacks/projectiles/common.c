/* Port of src/content/q2/missionpacks/projectiles/common.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/projectiles/common.h"

#include <stdio.h>
#include <string.h>

#include "../missionpacks_internal.h"

double qa_q2mp_projectile_mask(qa_q2_game_services *game) { return q2mp_rr(game) ? (double)(int64_t)0x46004003 : QA_Q2MP_SHOT_MASK; }

void qa_q2mp_free_projectile_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)ctx;
  game->remove(game->ctx, entity);
}
qa_q2_think qa_q2mp_free_projectile_think(void) { qa_q2_think t = {NULL, qa_q2mp_free_projectile_fn}; return t; }

qa_q2_entity *qa_q2mp_projectile(qa_owned_actor self, qa_q2_game_services *game, const char *classname, qa_vec3 start,
                                  qa_vec3 direction, double speed, const char *model, const char *motion,
                                  double effects) {
  qa_q2_entity *entity = game->create(game->ctx, classname, NULL, 0);
  entity->owner = self.id; entity->has_owner = true; entity->projectile = true; entity->dodgeable = true;
  qa_q2_game_mark_dodgeable(game, entity);
  entity->speed = speed; entity->model = model; entity->effects = effects;
  entity->clip_mask = qa_q2mp_projectile_mask(game); entity->motion = motion;
  entity->movedir = direction;
  qa_q2_body_changes c = q2mp_changes();
  c.has_origin = true; c.origin = start;
  c.has_velocity = true; c.velocity = q2mp_scale(direction, speed);
  c.has_angles = true; c.angles = q2mp_vta(direction);
  c.has_bounds = true; c.bounds = (qa_bounds){q2mp_zero(), q2mp_zero()};
  q2mp_move(game, entity, c, true, false);
  return entity;
}

void qa_q2mp_publish_projectile(qa_q2_entity *entity, qa_q2_game_services *game, const char *sound,
                                bool has_behavior, qa_q2mp_projectile_behavior behavior) {
  game->solid(game->ctx, entity, "box");
  game->motion(game->ctx, entity, entity->motion);
  if (has_behavior && entity->has_owner && game->host->is_player(game->host->ctx, entity->owner) &&
      game->host->weapon_behavior != NULL) {
    qa_weapon_behavior_launch in;
    memset(&in, 0, sizeof in);
    in.projectile = entity->actor; in.shooter = entity->owner; in.weapon = behavior.weapon; in.role = behavior.role;
    in.time_seconds = q2mp_now(game); in.body = q2mp_body(game, entity);
    qa_weapon_trajectory_update_result update = game->host->weapon_behavior->launch(game->host->weapon_behavior->ctx, in);
    if (update.has_value) game->project_trajectory(game->ctx, entity, update.value);
  }
  game->show(game->ctx, entity);
  if (strcmp(sound, "") != 0) {
    qa_q2_presentation_event e;
    memset(&e, 0, sizeof e);
    e.kind = "sound"; e.has_actor = true; e.actor = entity->actor.id; e.origin = q2mp_body(game, entity).origin;
    e.path = sound; e.channel = 0; e.volume = 1; e.attenuation = 1; e.reliable = false; e.loop = "start";
    game->host->emit(game->host->ctx, e);
  }
}

void qa_q2mp_effect(qa_q2_entity *entity, qa_q2_game_services *game, const char *name, qa_vec3 direction,
                    double count, double color) {
  char full[128];
  (void)snprintf(full, sizeof full, "q2:%s", name);
  qa_q2_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = "effect"; e.effect = full; e.origin = q2mp_body(game, entity).origin;
  e.direction = direction; e.count = count; e.color = color;
  game->host->emit(game->host->ctx, e);
}

void qa_q2mp_explode(qa_q2_entity *entity, qa_q2_game_services *game, const char *name) {
  qa_q2mp_effect(entity, game, name, q2mp_zero(), 1, 0);
  game->remove(game->ctx, entity);
}
void qa_q2mp_explode_default(qa_q2_entity *entity, qa_q2_game_services *game) { qa_q2mp_explode(entity, game, "explosion1"); }

bool qa_q2mp_sight(qa_q2_game_services *game, qa_q2_entity *from, qa_actor_id target) {
  qa_body_state_result body = q2mp_bread(game, target);
  if (!body.has_value) return false;
  qa_q2_entity *target_entity = q2mp_ent(game, true, target);
  double eye = target_entity != NULL ? target_entity->view_height : game->host->is_player(game->host->ctx, target) ? 22 : 0;
  qa_trace_result trace = q2mp_trace(game, q2mp_add(q2mp_body(game, from).origin, q2mp_v3(0, 0, from->view_height)),
                                     q2mp_add(body.value.origin, q2mp_v3(0, 0, eye)), NULL, &from->actor.id, 25);
  return trace.fraction == 1;
}

void qa_q2mp_velocity(qa_q2_game_services *game, qa_actor_id actor, qa_vec3 value, bool lift_ground) {
  qa_body_state_result body = q2mp_bread(game, actor);
  qa_owned_actor owned; bool has_owned = qa_session_actor_registry_resolve_owned(game->host->actors, actor, &owned);
  if (!body.has_value || !has_owned) return;
  qa_body_state next = body.value;
  next.velocity = value;
  if (lift_ground) next.ground = NULL;
  qa_shared_body_table_write(game->host->bodies, owned, next);
  qa_q2_entity *entity = q2mp_ent(game, true, actor);
  if (entity != NULL) game->motion(game->ctx, entity, entity->motion);
}
