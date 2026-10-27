/* Port of src/content/q2/missionpacks/monsters/turret.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/turret.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_turret.h"

qa_q2f_entity *qa_q2mpm_throw_debris(qa_q2f_entity *self, qa_q2f_game *game, const char *model, double speed,
                                     qa_vec3 origin) {
  qa_q2f_entity *chunk = game->create(game->ctx, "debris", NULL);
  chunk->model = model;
  qa_vec3 random_velocity = {100 * (q2mpm_rnd(game) * 2 - 1), 100 * (q2mpm_rnd(game) * 2 - 1),
                             100 + 100 * (q2mpm_rnd(game) * 2 - 1)};
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = origin;
  change.has_velocity = true;
  change.velocity = qa_add3_double(q2mpm_body(game, self).velocity, qa_scale3_double(random_velocity, speed));
  q2mpm_move_body(game, chunk, change, true, false);
  chunk->angular_velocity = q2mpm_v3(q2mpm_rnd(game) * 600, q2mpm_rnd(game) * 600, q2mpm_rnd(game) * 600);
  qa_combat_state combat;
  memset(&combat, 0, sizeof combat);
  combat.health = 0;
  combat.armor.powered.kind = "none";
  combat.mass = 0;
  combat.can_take_damage = true;
  game->host->combat.create(game->host->combat.ctx, chunk->actor, combat);
  chunk->die = qa_q2f_free_entity_die();
  game->motion(game->ctx, chunk, QA_Q2F_MOTION_BOUNCE);
  game->solid(game->ctx, chunk, QA_Q2F_SOLID_NONE);
  game->show(game->ctx, chunk);
  game->schedule(game->ctx, chunk, 5 + q2mpm_rnd(game) * 5, qa_q2f_free_entity_think());
  return chunk;
}

typedef struct turret_bundle {
  qa_q2m_monsters *monsters;
  qa_q2mpm_movers movers;
  qa_q2mpm_monster_state *source;
} turret_bundle;
static double clamp_value(double value, double low, double high) { return fmax(low, fmin(high, value)); }
static bool target_or_world(qa_q2m_context *c, qa_trace_result trace) {
  if (trace.hit.kind == QA_TRACE_HIT_NONE || trace.hit.kind == QA_TRACE_HIT_WORLD) return true;
  if (qa_same_actor(trace.hit.actor, c->game->host->world_actor(c->game->host->ctx))) return true;
  return c->entity->has_enemy && qa_same_actor(trace.hit.actor, c->entity->enemy);
}
static void ready_move(qa_q2m_context *c) { q2mpm_set_move(c, "turret_move_ready_gun"); }
static void run_move(qa_q2m_context *c) {
  if (c->entity->frame < qa_q2mpm_rogue_turret_frames_value.run01) ready_move(c);
  else q2mpm_set_move(c, "turret_move_run");
}
static void run_fn(void *ctx, qa_q2m_context *c) { (void)ctx; run_move(c); }
static void aim_at(turret_bundle *bundle, qa_q2m_context *c) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  const qa_q2mpm_rogue_turret_frames *f = &qa_q2mpm_rogue_turret_frames_value;
  if (!entity->has_enemy || qa_same_actor(entity->enemy, game->host->world_actor(game->host->ctx))) {
    if (!c->find_target(c->impl_ctx, c)) return;
  }
  if (entity->frame < f->active01) { ready_move(c); return; }
  if (entity->frame < f->run01) return;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy) || !entity->has_enemy) return;
  qa_vec3 end = enemy.origin;
  qa_q2f_entity *enemy_entity = game->entity(game->ctx, &entity->enemy);
  double view_height = enemy_entity ? enemy_entity->view_height : 22;
  if (strcmp(state->move->name, "turret_move_fire_blind") == 0) {
    end = state->blind_fire_target;
    end.z += enemy.origin.z < state->blind_fire_target.z ? view_height + 10 : enemy.bounds.min.z - 10;
  } else if (game->host->is_player(game->host->ctx, entity->enemy)) {
    end.z += view_height;
  }
  qa_body_state body = q2mpm_body(game, entity);
  qa_vec3 ideal = qa_vector_to_angles_with_rule(qa_sub3_double(end, body.origin), QA_VECTOR_TO_ANGLES_NATIVE_AI);
  double pitch = ideal.x;
  double yaw = ideal.y;
  double orientation = qa_q2mpm_state_get(bundle->source, entity)->turret_orientation;
  if (orientation == -1) {
    if (pitch < -90) pitch += 360;
    pitch = fmin(pitch, -5);
  } else if (orientation == -2) {
    if (pitch > -90) pitch -= 360;
    pitch = clamp_value(pitch, -355, -185);
  } else if (orientation == 0) {
    if (pitch < -180) pitch += 360;
    pitch = clamp_value(pitch, -85, 85);
    if (yaw > 180) yaw -= 360;
    yaw = clamp_value(yaw, -85, 85);
  } else if (orientation == 90) {
    if (pitch < -180) pitch += 360;
    pitch = clamp_value(pitch, -85, 85);
    if (yaw > 270) yaw -= 360;
    yaw = clamp_value(yaw, 5, 175);
  } else if (orientation == 180) {
    if (pitch < -180) pitch += 360;
    pitch = clamp_value(pitch, -85, 85);
    yaw = clamp_value(yaw, 95, 265);
  } else if (orientation == 270) {
    if (pitch < -180) pitch += 360;
    pitch = clamp_value(pitch, -85, 85);
    if (yaw < 90) yaw += 360;
    yaw = clamp_value(yaw, 185, 355);
  }
  double pitch_move = pitch - body.angles.x;
  while (pitch_move >= 360) pitch_move -= 360;
  if (pitch_move >= 90) pitch_move -= 360;
  while (pitch_move <= -360) pitch_move += 360;
  if (pitch_move <= -90) pitch_move += 360;
  double yaw_move = yaw - body.angles.y;
  if (yaw_move >= 180) yaw_move -= 360;
  if (yaw_move <= -180) yaw_move += 360;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_angles = true;
  change.angles.x = pitch == body.angles.x ? body.angles.x
      : qa_angle_mod_scaled(body.angles.x + clamp_value(pitch_move, -state->yaw_speed, state->yaw_speed));
  change.angles.y = yaw == body.angles.y ? body.angles.y
      : qa_angle_mod_scaled(body.angles.y + clamp_value(yaw_move, -state->yaw_speed, state->yaw_speed));
  change.angles.z = body.angles.z;
  q2mpm_move_body(game, entity, change, false, false);
}
static void aim_fn(void *ctx, qa_q2m_context *c) { aim_at(ctx, c); }
static double rocket_speed(qa_q2m_context *c) {
  double skill = c->game->options.skill;
  return 550 + (skill == 2 ? trunc(200 * q2mpm_rnd(c->game)) : skill == 3 ? trunc(100 + 200 * q2mpm_rnd(c->game)) : 0);
}
static void fire_at(turret_bundle *bundle, qa_q2m_context *c, bool blind) {
  aim_at(bundle, c);
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy) || !entity->has_enemy) return;
  qa_vec3 start = q2mpm_body(game, entity).origin;
  qa_vec3 direct = qa_normalize3_double_hypot(qa_sub3_double(blind ? state->blind_fire_target : enemy.origin, start));
  if (qa_dot3_double(direct, qa_math_angle_vectors_rule(q2mpm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).forward) < 0.98)
    return;
  if (!blind) (void)q2mpm_rnd(game);
  double speed = ((uint32_t)entity->spawnflags & 32u) != 0 ? rocket_speed(c)
      : game->options.skill == 0 ? 600 : game->options.skill == 1 ? 800 : 1000;
  if (!blind && !qa_q2m_visible_enemy(c)) return;
  qa_q2f_entity *enemy_entity = game->entity(game->ctx, &entity->enemy);
  double view_height = enemy_entity ? enemy_entity->view_height : 22;
  qa_vec3 end;
  if (blind) {
    end = state->blind_fire_target;
    end.z += enemy.origin.z < state->blind_fire_target.z ? view_height + 10 : enemy.bounds.min.z - 10;
  } else {
    end = enemy.origin;
    end.z += game->host->is_player(game->host->ctx, entity->enemy) ? view_height : 22;
  }
  double distance = qa_hypot3(qa_sub3_double(end, start));
  if (!blind && ((uint32_t)entity->spawnflags & 80u) == 0 && distance < 512
      && q2mpm_rnd(game) + (3 - game->options.skill) * 0.1 < 0.8)
    end = qa_add3_double(end, qa_scale3_double(enemy.velocity, distance / 1000));
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(end, start));
  qa_trace_result trace;
  bool has_trace = !blind;
  if (has_trace) trace = q2mpm_trace(game, start, end, NULL, &entity->actor.id, qa_q2m_attack_trace_mask(game));
  else memset(&trace, 0, sizeof trace);
  if (has_trace && !target_or_world(c, trace)) return;
  if (((uint32_t)entity->spawnflags & 8u) != 0) {
    (void)c->weapons.fire_blaster(c->weapons.ctx, entity, game, start, direction, 20, blind ? 1000 : speed, 8,
                                  false, false, 0);
    qa_q2mpm_monster_flash(c, 143, start, direction);
  } else if (!blind && ((uint32_t)entity->spawnflags & 16u) != 0) {
    c->weapons.fire_bullet(c->weapons.ctx, entity, game, start, direction, 4, 0, 300, 500, 0);
    qa_q2mpm_monster_flash(c, 141, start, direction);
  } else if (((uint32_t)entity->spawnflags & 32u) != 0 && (!has_trace || distance * trace.fraction > 72)) {
    (void)c->weapons.fire_rocket(c->weapons.ctx, entity, game, start, direction, 50, speed, 70, 50);
    qa_q2mpm_monster_flash(c, 142, start, direction);
  }
}
static void fire_fn(void *ctx, qa_q2m_context *c) { fire_at(ctx, c, false); }
static void fire_blind_fn(void *ctx, qa_q2m_context *c) { fire_at(ctx, c, true); }
static void wake_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  turret_bundle *bundle = ctx;
  if (((uint32_t)entity->flags & 1024u) != 0) return;
  qa_q2m_context *context = qa_q2m_monsters_context(bundle->monsters, entity->actor.id);
  if (!context) qa_q2f_error("Turret wall wake has no source monster context");
  context->state->can_take_damage = true;
  q2mpm_set_damageable(game, entity, true);
  game->motion(game->ctx, entity, QA_Q2F_MOTION_STATIONARY);
  q2mpm_set_move(context, "turret_move_stand");
  entity->frame = floor(q2mpm_rnd(game) * 2);
  game->link(game->ctx, entity);
  game->show(game->ctx, entity);
  entity->use = qa_q2mpm_use_resolve("monster_use");
  qa_q2f_think_result start = game->source_callbacks.think_resolve(game->source_callbacks.ctx, "monster_start_go");
  if (!start.has_value) qa_q2f_error("Turret wake has no source stationarymonster_start callback");
  game->schedule(game->ctx, entity, 0.1, start.value);
}
static void activate_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, const qa_actor_id *other,
                        const qa_actor_id *activator) {
  turret_bundle *bundle = ctx;
  (void)other; (void)activator;
  game->motion(game->ctx, entity, QA_Q2F_MOTION_PUSH);
  if (entity->speed == 0) entity->speed = 15;
  entity->accel = entity->decel = entity->speed;
  qa_vec3 angles = q2mpm_body(game, entity).angles;
  qa_vec3 forward = q2mpm_zero();
  if (angles.x == 270) forward = q2mpm_v3(0, 0, 1);
  else if (angles.x == 90) forward = q2mpm_v3(0, 0, -1);
  else if (angles.y == 0) forward = q2mpm_v3(1, 0, 0);
  else if (angles.y == 90) forward = q2mpm_v3(0, 1, 0);
  else if (angles.y == 180) forward = q2mpm_v3(-1, 0, 0);
  else if (angles.y == 270) forward = q2mpm_v3(0, -1, 0);
  qa_q2f_think wake = {bundle, wake_fn};
  bundle->movers.linear_move_to(bundle->movers.ctx, entity, game,
                                qa_add3_double(q2mpm_body(game, entity).origin, qa_scale3_double(forward, 32)), wake);
  qa_q2f_entity *base = entity->has_team_chain ? game->entity(game->ctx, &entity->team_chain) : NULL;
  if (base) {
    game->motion(game->ctx, base, QA_Q2F_MOTION_PUSH);
    base->speed = base->accel = base->decel = entity->speed;
    bundle->movers.linear_move_to(bundle->movers.ctx, base, game,
                                  qa_add3_double(q2mpm_body(game, base).origin, qa_scale3_double(forward, 32)), wake);
  }
  game->sound(game->ctx, entity->actor, "world/dr_short.wav", true, 2, false, 0, false, 0);
}
static void walk_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->entity->frame < qa_q2mpm_rogue_turret_frames_value.run01) ready_move(c);
  else q2mpm_set_move(c, "turret_move_seek");
}
static qa_q2m_start_mode start_mode_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  return ((uint32_t)c->entity->spawnflags & 128u) != 0 ? QA_Q2M_START_MANUAL : QA_Q2M_START_AUTOMATIC;
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  turret_bundle *bundle = ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  qa_body_state body = q2mpm_body(game, entity);
  entity->flags = q2mpm_or(entity->flags, 8192);
  entity->gravity = 0;
  state->manual_steering = true;
  state->ignore_shots = true;
  if (((uint32_t)entity->spawnflags & 120u) == 0) entity->spawnflags = q2mpm_or(entity->spawnflags, 8);
  if (((uint32_t)entity->spawnflags & 64u) != 0)
    entity->spawnflags = (double)(((uint32_t)entity->spawnflags & ~64u) | 8u);
  double orientation = trunc(body.angles.y);
  qa_q2mpm_state_get(bundle->source, entity)->turret_orientation = orientation;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  if (orientation == -1) {
    change.has_angles = true; change.angles = body.angles; change.angles.x = 270; change.angles.y = 0;
    change.has_origin = true; change.origin = qa_add3_double(body.origin, q2mpm_v3(0, 0, 2));
    q2mpm_move_body(game, entity, change, false, false);
  } else if (orientation == -2) {
    change.has_angles = true; change.angles = body.angles; change.angles.x = 90; change.angles.y = 0;
    change.has_origin = true; change.origin = qa_add3_double(body.origin, q2mpm_v3(0, 0, -2));
    q2mpm_move_body(game, entity, change, false, false);
  } else if (orientation == 0) {
    change.has_origin = true; change.origin = qa_add3_double(body.origin, q2mpm_v3(2, 0, 0));
    q2mpm_move_body(game, entity, change, false, false);
  } else if (orientation == 90) {
    change.has_origin = true; change.origin = qa_add3_double(body.origin, q2mpm_v3(0, 2, 0));
    q2mpm_move_body(game, entity, change, false, false);
  } else if (orientation == 180) {
    change.has_origin = true; change.origin = qa_add3_double(body.origin, q2mpm_v3(-2, 0, 0));
    q2mpm_move_body(game, entity, change, false, false);
  } else if (orientation == 270) {
    change.has_origin = true; change.origin = qa_add3_double(body.origin, q2mpm_v3(0, -2, 0));
    q2mpm_move_body(game, entity, change, false, false);
  }
  const char *targetname = entity->targetname ? entity->targetname : "";
  if (((uint32_t)entity->spawnflags & 128u) != 0 && targetname[0] == '\0') game->remove(game->ctx, entity);
}
static void after_spawn_fn(void *ctx, qa_q2m_context *c) {
  turret_bundle *bundle = ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  if (((uint32_t)entity->spawnflags & 128u) != 0) {
    c->state->can_take_damage = false;
    q2mpm_set_damageable(game, entity, false);
    entity->use = (qa_q2f_use){bundle, activate_fn};
    game->cancel(game->ctx, entity);
    qa_q2f_entity *base = game->create(game->ctx, "turret_wall", NULL);
    qa_body_state body = q2mpm_body(game, entity);
    double orientation = body.angles.x == 90 ? -1 : body.angles.x == 270 ? -2 : trunc(body.angles.y);
    qa_bounds bounds = {{0, 0, 0}, {0, 0, 0}};
    if (orientation == -1) { bounds.min = q2mpm_v3(-16, -16, -8); bounds.max = q2mpm_v3(16, 16, 0); }
    else if (orientation == -2) { bounds.min = q2mpm_v3(-16, -16, 0); bounds.max = q2mpm_v3(16, 16, 8); }
    else if (orientation == 0) { bounds.min = q2mpm_v3(-8, -16, -16); bounds.max = q2mpm_v3(0, 16, 16); }
    else if (orientation == 90) { bounds.min = q2mpm_v3(-16, -8, -16); bounds.max = q2mpm_v3(16, 0, 16); }
    else if (orientation == 180) { bounds.min = q2mpm_v3(0, -16, -16); bounds.max = q2mpm_v3(8, 16, 16); }
    else if (orientation == 270) { bounds.min = q2mpm_v3(-16, 0, -16); bounds.max = q2mpm_v3(16, 8, 16); }
    qa_q2f_body_changes change;
    memset(&change, 0, sizeof change);
    change.has_origin = true; change.origin = body.origin;
    change.has_angles = true; change.angles = body.angles;
    change.has_bounds = true; change.bounds = bounds;
    q2mpm_move_body(game, base, change, true, false);
    base->has_team_master = true; base->team_master = entity->actor.id;
    entity->has_team_master = true; entity->team_master = entity->actor.id;
    entity->has_team_chain = true; entity->team_chain = base->actor.id;
    base->has_team_chain = false;
    base->flags = q2mpm_or(base->flags, 1024);
    base->has_owner = true; base->owner = entity->actor.id;
    base->model = "models/monsters/turretbase/tris.md2";
    game->motion(game->ctx, base, QA_Q2F_MOTION_PUSH);
    game->solid(game->ctx, base, QA_Q2F_SOLID_NONE);
    game->link(game->ctx, base);
    game->show(game->ctx, base);
  }
  if (((uint32_t)entity->spawnflags & 16u) != 0) entity->skin = 1;
  else if (((uint32_t)entity->spawnflags & 32u) != 0) entity->skin = 2;
  else entity->spawnflags = q2mpm_or(entity->spawnflags, 8);
  game->show(game->ctx, entity);
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  const qa_q2mpm_rogue_turret_frames *f = &qa_q2mpm_rogue_turret_frames_value;
  if (entity->frame < f->run01) { ready_move(c); return; }
  if (state->attack_state != QA_Q2M_ATTACK_BLIND) {
    state->next_frame = f->pow01;
    q2mpm_set_move(c, "turret_move_fire");
    return;
  }
  double chance = state->blind_fire_delay < 1 ? 1 : state->blind_fire_delay < 7.5 ? 0.4 : 0.1;
  double random = q2mpm_rnd(game);
  state->blind_fire_delay += 3.4 + q2mpm_rnd(game) * 4;
  if (qa_hypot3(state->blind_fire_target) == 0 || random > chance) return;
  state->next_frame = f->pow01;
  q2mpm_set_move(c, "turret_move_fire_blind");
}
static bool check_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_vec3 eye;
  if (!entity->has_enemy || !qa_q2m_enemy_eye(c, &eye)) return false;
  qa_q2f_entity *enemy = game->entity(game->ctx, &entity->enemy);
  qa_vec3 origin = q2mpm_body(game, entity).origin;
  qa_vec3 start = origin;
  start.z += entity->view_height;
  if (qa_q2m_health(game, &entity->enemy) > 0) {
    qa_trace_result trace = q2mpm_trace(game, start, eye, NULL, &entity->actor.id,
                                        (double)(1u | 2u | 0x2000000u | 16u | 8u));
    bool hit_enemy = trace.hit.kind == QA_TRACE_HIT_ACTOR && qa_same_actor(trace.hit.actor, entity->enemy);
    if (!hit_enemy && (!enemy || enemy->solid != QA_Q2F_SOLID_NONE || trace.fraction < 1)) {
      bool blocker_monster = trace.hit.kind == QA_TRACE_HIT_ACTOR
          && game->host->is_monster(game->host->ctx, trace.hit.actor);
      if (!blocker_monster && !qa_q2m_visible_enemy(c) && ((uint32_t)entity->spawnflags & 40u) != 0
          && state->blind_fire_delay <= 10 && q2mpm_now(game) >= state->attack_finished
          && q2mpm_now(game) >= state->trail_time + state->blind_fire_delay) {
        qa_trace_result blind = q2mpm_trace(game, start, state->blind_fire_target, NULL, &entity->actor.id, 0x2000000);
        if (!blind.all_solid && !blind.start_solid
            && (blind.fraction == 1 || (blind.hit.kind == QA_TRACE_HIT_ACTOR
                                         && qa_same_actor(blind.hit.actor, entity->enemy)))) {
          state->attack_state = QA_Q2M_ATTACK_BLIND;
          state->attack_finished = q2mpm_now(game) + 0.5 + 2 * q2mpm_rnd(game);
          return true;
        }
      }
      return false;
    }
  }
  if (q2mpm_now(game) < state->attack_finished) return false;
  if (qa_q2m_target_distance(c) < 80) {
    if (game->options.skill == 0 && floor(q2mpm_rnd(game) * 4) != 0) return false;
    state->attack_state = QA_Q2M_ATTACK_MISSILE;
    return true;
  }
  double chance = ((uint32_t)entity->spawnflags & 32u) != 0 ? 0.1 : ((uint32_t)entity->spawnflags & 8u) != 0 ? 0.35 : 0.5;
  double next = ((uint32_t)entity->spawnflags & 32u) != 0 ? 1.8 - 0.2 * game->options.skill
      : ((uint32_t)entity->spawnflags & 8u) != 0 ? 1.2 - 0.2 * game->options.skill : 0.8 - 0.1 * game->options.skill;
  chance *= game->options.skill == 0 ? 0.5 : game->options.skill > 1 ? 2 : 1;
  if ((q2mpm_rnd(game) < chance && qa_q2m_visible_enemy(c)) || (enemy && enemy->solid == QA_Q2F_SOLID_NONE)) {
    state->attack_state = QA_Q2M_ATTACK_MISSILE;
    state->attack_finished = q2mpm_now(game) + next;
    return true;
  }
  state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
  return false;
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) { (void)ctx; (void)c; (void)reaction; }
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_body_state body = q2mpm_body(game, entity);
  q2mpm_emit_effect(game, "q2:plain-explosion", body.origin, q2mpm_zero(), 1, 0);
  qa_vec3 start = qa_add3_double(body.origin,
                                 qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward);
  static const double speeds[] = {1, 2, 1, 2};
  for (int i = 0; i < 4; i++) (void)qa_q2mpm_throw_debris(entity, game, "models/objects/debris1/tris.md2", speeds[i], start);
  qa_q2f_entity *base = entity->has_team_chain ? game->entity(game->ctx, &entity->team_chain) : NULL;
  if (base) {
    game->solid(game->ctx, base, QA_Q2F_SOLID_BOX);
    if (game->host->combat.read(game->host->combat.ctx, base->actor.id).has_value)
      q2mpm_set_damageable(game, base, false);
    game->motion(game->ctx, base, QA_Q2F_MOTION_STATIONARY);
    game->link(game->ctx, base);
  }
  const char *target = entity->target ? entity->target : "";
  if (target[0] != '\0') {
    qa_actor_id activator = entity->has_enemy && q2mpm_live(game, entity->enemy) ? entity->enemy : entity->actor.id;
    game->use_targets(game->ctx, entity, &activator, false, false);
  }
  game->remove(game->ctx, entity);
}
qa_q2m_definition qa_q2mpm_create_rogue_turret_definition(qa_q2m_monsters *monsters, qa_q2mpm_movers movers,
                                                           qa_q2mpm_monster_state *source) {
  turret_bundle *bundle = malloc(sizeof *bundle);
  if (!bundle) qa_q2f_error("Out of memory creating rogue turret definition");
  bundle->monsters = monsters;
  bundle->movers = movers;
  bundle->source = source;
  qa_q2m_named_handler *callbacks = malloc(4 * sizeof *callbacks);
  qa_q2f_named_think *thinks = malloc(sizeof *thinks);
  qa_q2f_named_use *uses = malloc(sizeof *uses);
  if (!callbacks || !thinks || !uses) qa_q2f_error("Out of memory creating rogue turret definition");
  callbacks[0] = (qa_q2m_named_handler){"turret_run", {NULL, run_fn}};
  callbacks[1] = (qa_q2m_named_handler){"TurretAim", {bundle, aim_fn}};
  callbacks[2] = (qa_q2m_named_handler){"TurretFire", {bundle, fire_fn}};
  callbacks[3] = (qa_q2m_named_handler){"TurretFireBlind", {bundle, fire_blind_fn}};
  thinks[0] = (qa_q2f_named_think){"q2:rogue/turret_wake", {bundle, wake_fn}};
  uses[0] = (qa_q2f_named_use){"q2:rogue/turret_activate", {bundle, activate_fn}};
  qa_q2m_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.classname = "monster_turret";
  definition.kind = "turret";
  definition.model = "models/monsters/turret/tris.md2";
  definition.health = 240;
  definition.gib_health = -100;
  definition.mass = 250;
  definition.bounds.min = q2mpm_v3(-12, -12, -12);
  definition.bounds.max = q2mpm_v3(12, 12, 12);
  definition.scale = 1;
  definition.has_view_height = true;
  definition.view_height = 0;
  definition.has_yaw_speed = true;
  definition.yaw_speed = 45;
  definition.has_locomotion = true;
  definition.locomotion = QA_Q2M_LOCO_STATIONARY;
  definition.initial_move = "turret_move_stand";
  definition.moves = qa_q2mpm_rogue_turret_moves;
  definition.move_count = qa_q2mpm_rogue_turret_move_count;
  definition.callbacks = callbacks;
  definition.callback_count = 4;
  definition.has_source_callbacks = true;
  memset(&definition.source_callbacks, 0, sizeof definition.source_callbacks);
  definition.source_callbacks.think = thinks;
  definition.source_callbacks.think_count = 1;
  definition.source_callbacks.use = uses;
  definition.source_callbacks.use_count = 1;
  definition.stand = qa_q2bm_move_handler("turret_move_stand");
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.walk = (qa_q2m_handler){NULL, walk_fn};
  definition.attack = (qa_q2m_handler){NULL, attack_fn};
  definition.has_blind_fire_flag = true;
  definition.blind_fire = true;
  definition.initialize = (qa_q2m_handler){bundle, init_fn};
  definition.after_spawn = (qa_q2m_handler){bundle, after_spawn_fn};
  definition.start_mode = start_mode_fn;
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.check_attack = check_attack_fn;
  return definition;
}
