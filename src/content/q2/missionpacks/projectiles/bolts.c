/* Port of src/content/q2/missionpacks/projectiles/bolts.ts. GPL-2.0-or-later.
 * game.host.weaponBehavior.controlsTrajectory has no qa_q2f mirror (the
 * mirror port carries launch only); steering-skip checks read as false. */
#include "qa/content/q2/missionpacks/projectiles/bolts.h"

#include <math.h>
#include <string.h>

#include "../missionpacks_internal.h"
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/content/q2/missionpacks/projectiles/common.h"

static void project_steering(void *ctx, qa_q2f_entity *entity, qa_weapon_trajectory_update update) {
  (void)ctx;
  entity->movedir = q2mp_norm(update.velocity);
  entity->speed = q2mp_len(update.velocity);
}
static void register_callbacks(qa_q2mp_bolts *b, qa_q2f_game *game) {
  game->source_callbacks.register_defs(game->source_callbacks.ctx, qa_q2mp_bolts_callbacks(b));
}
void qa_q2mp_bolts_player_collision(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2f_game *game, qa_q2f_entity *p) {
  if (q2mp_rr(game) && game->host->is_player(game->host->ctx, self.id)) {
    const qa_q2w_input *input = qa_q2w_ballistics_input(b->hooks.base, self.id);
    if (input != NULL && !input->players_collide) p->clip_mask = (double)(q2mp_u32(p->clip_mask) & ~0x40000000u);
  }
}
static void impact_noise(qa_q2mp_bolts *b, qa_q2f_entity *entity, qa_q2f_game *game) {
  if (entity->has_owner)
    qa_q2w_ballistics_player_noise_for_actor(b->hooks.base, entity->owner, game, q2mp_body(game, entity).origin, "impact");
}
static void initial_touch(qa_owned_actor self, qa_q2f_game *game, qa_q2f_entity *bolt) {
  qa_vec3 start = game->body(game->ctx, self).origin;
  qa_trace_result trace = q2mp_trace(game, start, q2mp_body(game, bolt).origin, NULL, &bolt->actor.id, bolt->clip_mask);
  if (trace.fraction == 1 || bolt->touch.fn == NULL) return;
  bool rr = q2mp_rr(game);
  qa_plane plane = trace.contact.plane;
  qa_touch_surface surface;
  memset(&surface, 0, sizeof surface);
  if (rr && strcmp(trace.kind, "q2") == 0 && trace.surface != NULL) {
    surface.name = trace.surface->name; surface.native_flags = trace.surface->flags; surface.native_value = trace.surface->value;
  }
  qa_q2f_body_changes c = q2mp_changes();
  c.has_origin = true;
  c.origin = rr ? q2mp_add(trace.end, trace.contact.has_plane ? plane.normal : q2mp_zero())
                : q2mp_add(q2mp_body(game, bolt).origin, q2mp_scale(bolt->movedir, -10));
  q2mp_move(game, bolt, c, false, false);
  qa_touch_contact contact;
  memset(&contact, 0, sizeof contact);
  contact.self = bolt->actor;
  contact.other = trace.hit.kind == QA_TRACE_HIT_ACTOR ? trace.hit.actor : game->host->world_actor(game->host->ctx);
  contact.plane = rr && trace.contact.has_plane ? &plane : NULL;
  contact.surface = rr && strcmp(trace.kind, "q2") == 0 && trace.surface != NULL ? &surface : NULL;
  bolt->touch.fn(bolt->touch.ctx, bolt, game, contact);
}

static void ion_sparks_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  (void)ctx;
  qa_q2mp_effect(entity, game, "welding_sparks", q2mp_zero(), 0, 0xe4 + floor(q2mp_rnd(game) * 4));
  game->remove(game->ctx, entity);
}
static void ion_touch_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_touch_contact contact) {
  qa_q2mp_bolts *b = ctx;
  if (entity->has_owner && qa_same_actor(contact.other, entity->owner)) return;
  if ((q2mp_u32(q2mp_surf(&contact)) & 4) != 0) { game->remove(game->ctx, entity); return; }
  impact_noise(b, entity, game);
  if (!q2mp_can_hurt(game, contact.other)) return;
  qa_body_state body = q2mp_body(game, entity);
  q2mp_damage(game, contact.other, entity, entity->actor.id, entity->has_owner ? &entity->owner : NULL, entity->damage,
              1, body.velocity, body.origin, q2mp_cnormal(&contact), QA_Q2MP_MOD_RIPPER, true, 4, "q2:weapon_boomer");
  game->remove(game->ctx, entity);
}
qa_q2f_entity *qa_q2mp_bolts_fire_ion_ripper(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start,
                                             qa_vec3 direction, double damage, double speed, double effects) {
  register_callbacks(b, game);
  qa_q2f_entity *bolt = qa_q2mp_projectile(self, game, "ion", start, q2mp_norm(direction), speed,
                                           "models/objects/boomrang/tris.md2", QA_Q2F_MOTION_WALL_BOUNCE, effects);
  qa_q2mp_bolts_player_collision(b, self, game, bolt);
  bolt->damage = damage; bolt->damage_radius = 100; bolt->render_flags = 8; bolt->touch = b->ion_touch;
  game->schedule(game->ctx, bolt, 3, b->ion_sparks);
  qa_q2mp_publish_projectile(bolt, game, "misc/lasfly.wav", true, (qa_q2mp_projectile_behavior){"q2:weapon_boomer", "bolt"});
  qa_q2w_ballistics_check_dodge(b->hooks.base, self, game, start, bolt->movedir, speed);
  initial_touch(self, game, bolt);
  return bolt;
}
qa_q2f_entity *qa_q2mp_bolts_fire_blue_blaster(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start,
                                               qa_vec3 direction, double damage, double speed, double effects) {
  qa_q2f_entity *bolt = qa_q2w_ballistics_fire_blaster(b->hooks.base, self, game, start, direction, damage, speed,
                                                       effects, false, q2mp_rr(game) ? 58 : 1);
  if (q2mp_live(game, bolt->actor.id)) { bolt->model = "models/objects/blaser/tris.md2"; game->show(game->ctx, bolt); }
  return bolt;
}

static void plasma_touch_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_touch_contact contact) {
  qa_q2mp_bolts *b = ctx;
  if (entity->has_owner && qa_same_actor(contact.other, entity->owner)) return;
  if ((q2mp_u32(q2mp_surf(&contact)) & 4) != 0) { game->remove(game->ctx, entity); return; }
  impact_noise(b, entity, game);
  qa_body_state body = q2mp_body(game, entity);
  if (q2mp_can_hurt(game, contact.other))
    q2mp_damage(game, contact.other, entity, entity->actor.id, entity->has_owner ? &entity->owner : NULL, entity->damage, 0,
                body.velocity, body.origin, q2mp_cnormal(&contact), QA_Q2MP_MOD_PHALANX, true, 0, "q2:weapon_phalanx");
  q2mp_radius(game, entity, entity->has_owner ? &entity->owner : NULL, entity->radius_damage, &contact.other,
              entity->damage_radius, QA_Q2MP_MOD_PHALANX, true, 0, "q2:weapon_phalanx");
  qa_q2f_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = QA_Q2F_EVENT_EFFECT; e.effect = "q2:plasma_explosion";
  e.origin = q2mp_add(body.origin, q2mp_scale(body.velocity, -0.02)); e.direction = q2mp_zero(); e.count = 0; e.color = 0;
  game->host->emit(game->host->ctx, e);
  game->remove(game->ctx, entity);
}
qa_q2f_entity *qa_q2mp_bolts_fire_plasma(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start,
                                         qa_vec3 direction, double damage, double speed, double radius,
                                         double radius_damage) {
  register_callbacks(b, game);
  qa_q2f_entity *bolt = qa_q2mp_projectile(self, game, "plasma", start, direction, speed, "sprites/s_photon.sp2",
                                           QA_Q2F_MOTION_FLY_MISSILE, 0x1000000 | 0x2000);
  qa_q2mp_bolts_player_collision(b, self, game, bolt);
  bolt->damage = damage; bolt->damage_radius = radius; bolt->radius_damage = radius_damage; bolt->touch = b->plasma_touch;
  game->schedule(game->ctx, bolt, 8000 / speed, qa_q2mp_free_projectile_think());
  qa_q2mp_publish_projectile(bolt, game, "weapons/rockfly.wav", true, (qa_q2mp_projectile_behavior){"q2:weapon_phalanx", "plasma"});
  qa_q2w_ballistics_check_dodge(b->hooks.base, self, game, start, direction, speed);
  return bolt;
}

static void flechette_touch_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_touch_contact contact) {
  (void)ctx;
  if (entity->has_owner && qa_same_actor(contact.other, entity->owner)) return;
  if ((q2mp_u32(q2mp_surf(&contact)) & 4) != 0) { game->remove(game->ctx, entity); return; }
  qa_body_state body = q2mp_body(game, entity);
  if (q2mp_can_hurt(game, contact.other))
    q2mp_damage(game, contact.other, entity, entity->actor.id, entity->has_owner ? &entity->owner : NULL, entity->damage,
                entity->damage_radius, body.velocity, body.origin, q2mp_cnormal(&contact), QA_Q2MP_MOD_FLECHETTE, true, 128,
                "q2:weapon_etf_rifle");
  else qa_q2mp_effect(entity, game, "flechette", q2mp_cnormal(&contact), 1, 0);
  game->remove(game->ctx, entity);
}
qa_q2f_entity *qa_q2mp_bolts_fire_flechette(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start,
                                            qa_vec3 direction, double damage, double speed, double kick) {
  register_callbacks(b, game);
  qa_q2f_entity *bolt = qa_q2mp_projectile(self, game, "flechette", start, q2mp_norm(direction), speed,
                                           "models/proj/flechette/tris.md2", QA_Q2F_MOTION_FLY_MISSILE, 0);
  qa_q2mp_bolts_player_collision(b, self, game, bolt);
  bolt->damage = damage; bolt->damage_radius = kick; bolt->render_flags = 8; bolt->touch = b->flechette_touch;
  game->schedule(game->ctx, bolt, 8000 / speed, qa_q2mp_free_projectile_think());
  qa_q2mp_publish_projectile(bolt, game, "", true, (qa_q2mp_projectile_behavior){"q2:weapon_etf_rifle", "nail"});
  if (q2mp_rr(game)) initial_touch(self, game, bolt);
  else qa_q2w_ballistics_check_dodge(b->hooks.base, self, game, start, bolt->movedir, speed);
  return bolt;
}

static void heat_think_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_bolts *b = ctx;
  bool has_nearest = false;
  qa_actor_id nearest = entity->actor.id;
  double nearest_distance = 0;
  qa_vec3 origin = q2mp_body(game, entity).origin;
  qa_vec3 forward = q2mp_axes(q2mp_body(game, entity).angles).forward;
  size_t n = 0;
  const qa_actor_id *actors = game->host->nearby(game->host->ctx, origin, 1024, &n);
  for (size_t i = 0; i < n; i++) {
    qa_actor_id actor = actors[i];
    if (entity->has_owner && qa_same_actor(actor, entity->owner)) continue;
    if (!game->host->is_player(game->host->ctx, actor) || q2mp_health(game, actor) <= 0 || !qa_q2mp_sight(game, entity, actor)) continue;
    qa_body_state_result body = q2mp_bread(game, actor);
    if (!body.has_value) continue;
    qa_vec3 delta = q2mp_sub(body.value.origin, origin);
    double distance = trunc(q2mp_len(delta));
    if (q2mp_dot(q2mp_norm(delta), forward) <= 0.3) continue;
    if (!has_nearest || distance < nearest_distance) { has_nearest = true; nearest = actor; nearest_distance = distance; }
  }
  qa_body_state_result target = has_nearest ? q2mp_bread(game, nearest) : (qa_body_state_result){false, {q2mp_zero(), q2mp_zero(), q2mp_zero(), {q2mp_zero(), q2mp_zero()}, NULL}};
  if (target.has_value) {
    entity->enemy = nearest; entity->has_enemy = true;
    entity->movedir = q2mp_norm(q2mp_sub(target.value.origin, origin));
    qa_q2f_body_changes c = q2mp_changes();
    c.has_angles = true; c.angles = q2mp_vta(entity->movedir);
    c.has_velocity = true; c.velocity = q2mp_scale(entity->movedir, 500);
    q2mp_move(game, entity, c, false, false);
    game->motion(game->ctx, entity, entity->motion);
  }
  game->schedule(game->ctx, entity, 0.1, b->heat_think);
}
static void heat_think_rr_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_bolts *b = ctx;
  bool has_acquire = false;
  qa_actor_id acquire = entity->actor.id;
  double old_dot = 1, old_distance = 0;
  qa_vec3 origin = q2mp_body(game, entity).origin;
  qa_vec3 forward = q2mp_axes(q2mp_body(game, entity).angles).forward;
  size_t n = 0;
  const qa_actor_id *actors = game->host->nearby(game->host->ctx, origin, 1024, &n);
  for (size_t i = 0; i < n; i++) {
    qa_actor_id actor = actors[i];
    if (entity->has_owner && qa_same_actor(actor, entity->owner)) continue;
    if (!game->host->is_player(game->host->ctx, actor) || q2mp_health(game, actor) <= 0 || !qa_q2mp_sight(game, entity, actor)) continue;
    qa_body_state_result body = q2mp_bread(game, actor);
    if (!body.has_value) continue;
    qa_vec3 delta = q2mp_sub(origin, body.value.origin);
    double distance = q2mp_len(delta), alignment = q2mp_dot(q2mp_norm(delta), forward);
    if (alignment >= old_dot) continue;
    if (!has_acquire || alignment < old_dot || distance < old_distance) {
      has_acquire = true; acquire = actor; old_dot = alignment; old_distance = distance;
    }
  }
  qa_body_state_result target = has_acquire ? q2mp_bread(game, acquire) : (qa_body_state_result){false, {q2mp_zero(), q2mp_zero(), q2mp_zero(), {q2mp_zero(), q2mp_zero()}, NULL}};
  if (!target.has_value) entity->has_enemy = false;
  else {
    qa_vec3 desired = q2mp_norm(q2mp_sub(target.value.origin, origin));
    double alignment = q2mp_dot(entity->movedir, desired);
    if (alignment < 0.45 && alignment > -0.45) desired = q2mp_scale(desired, -1);
    double cosine = q2mp_dot(entity->movedir, desired), angle = acos(cosine), sine = sin(angle);
    double from = fabs(cosine) > 0.9995 ? 1 - entity->accel : sin((1 - entity->accel) * angle) / sine;
    double to = fabs(cosine) > 0.9995 ? entity->accel : sin(entity->accel * angle) / sine;
    entity->movedir = q2mp_norm(q2mp_add(q2mp_scale(entity->movedir, from), q2mp_scale(desired, to)));
    qa_q2f_body_changes c = q2mp_changes();
    c.has_angles = true; c.angles = q2mp_vta(entity->movedir);
    q2mp_move(game, entity, c, false, false);
    if (!entity->has_enemy) {
      q2mp_sound(game, entity, "weapons/railgr1a.wav", true, 1, true, 1, true, 0.25);
      entity->enemy = acquire; entity->has_enemy = true;
    }
  }
  qa_q2f_body_changes c = q2mp_changes();
  c.has_velocity = true; c.velocity = q2mp_scale(entity->movedir, entity->speed);
  q2mp_move(game, entity, c, false, false);
  game->motion(game->ctx, entity, entity->motion);
  game->schedule(game->ctx, entity, q2mp_frame(game), b->heat_think_rerelease);
}
qa_q2f_entity *qa_q2mp_bolts_fire_heat_rocket(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start,
                                              qa_vec3 direction, double damage, double speed, double radius,
                                              double radius_damage, double turn_fraction) {
  register_callbacks(b, game);
  qa_q2f_entity *rocket = qa_q2w_ballistics_fire_rocket(b->hooks.base, self, game, start, direction, damage, speed, radius, radius_damage);
  if (q2mp_live(game, rocket->actor.id)) {
    rocket->accel = turn_fraction; rocket->speed = speed; rocket->movedir = direction;
    game->schedule(game->ctx, rocket, q2mp_rr(game) ? q2mp_frame(game) : 0.1, q2mp_rr(game) ? b->heat_think_rerelease : b->heat_think);
  }
  return rocket;
}
qa_q2f_entity *qa_q2mp_bolts_fire_heat_rocket_default(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2f_game *game,
                                                       qa_vec3 start, qa_vec3 direction, double damage, double speed,
                                                       double radius, double radius_damage) {
  return qa_q2mp_bolts_fire_heat_rocket(b, self, game, start, direction, damage, speed, radius, radius_damage, 0.075);
}

static void green_touch_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_touch_contact contact) {
  qa_q2mp_bolts *b = ctx;
  if (entity->has_owner && qa_same_actor(contact.other, entity->owner)) return;
  if ((q2mp_u32(q2mp_surf(&contact)) & 4) != 0) { game->remove(game->ctx, entity); return; }
  impact_noise(b, entity, game);
  qa_vec3 origin = q2mp_body(game, entity).origin;
  bool hurt = q2mp_can_hurt(game, contact.other);
  if (entity->damage >= 5) {
    size_t n = 0;
    const qa_actor_id *actors = game->host->nearby(game->host->ctx, origin, entity->damage_radius, &n);
    for (size_t i = 0; i < n; i++) {
      qa_actor_id actor = actors[i];
      if (entity->has_owner && qa_same_actor(actor, entity->owner)) continue;
      if (hurt && qa_same_actor(actor, contact.other)) continue;
      if (!q2mp_can_hurt(game, actor)) continue;
      qa_body_state_result body = q2mp_bread(game, actor);
      if (!body.has_value || !game->can_damage(game->ctx, actor, entity->actor)) continue;
      qa_vec3 center = q2mp_add(body.value.origin, q2mp_scale(q2mp_add(body.value.bounds.min, body.value.bounds.max), 0.5));
      double points = entity->damage * (q2mp_rr(game) ? 2 : 3) - 0.5 * q2mp_len(q2mp_sub(center, origin));
      if (points > 0)
        q2mp_damage(game, actor, entity, entity->actor.id, entity->has_owner ? &entity->owner : NULL, trunc(points),
                    trunc(points), q2mp_sub(body.value.origin, origin), origin, q2mp_zero(), 0, true, q2mp_rr(game) ? 5 : 1, NULL);
    }
  }
  if (hurt)
    q2mp_damage(game, contact.other, entity, entity->actor.id, entity->has_owner ? &entity->owner : NULL, entity->damage, 1,
                q2mp_body(game, entity).velocity, origin, q2mp_cnormal(&contact),
                entity->has_owner && game->host->is_player(game->host->ctx, entity->owner) ? QA_Q2MP_MOD_DEFENDER_SPHERE : QA_Q2MP_MOD_BLASTER2,
                true, 4, NULL);
  else qa_q2mp_effect(entity, game, "blaster2", q2mp_cnormal(&contact), 1, 0);
  game->remove(game->ctx, entity);
}
qa_q2f_entity *qa_q2mp_bolts_fire_blaster2(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start,
                                           qa_vec3 direction, double damage, double speed, double effects) {
  register_callbacks(b, game);
  qa_q2f_entity *bolt = qa_q2mp_projectile(self, game, "bolt", start, q2mp_norm(direction), speed,
                                           "models/proj/laser2/tris.md2", QA_Q2F_MOTION_FLY_MISSILE,
                                           (double)(q2mp_u32(effects) | (effects == 0 ? 0u : 0x4000000u)));
  qa_q2mp_bolts_player_collision(b, self, game, bolt);
  if (q2mp_rr(game)) { bolt->model = "models/objects/laser/tris.md2"; bolt->skin = 2; bolt->scale = 2.5; }
  bolt->damage = damage; bolt->damage_radius = 128; bolt->touch = b->green_touch;
  game->schedule(game->ctx, bolt, 2, qa_q2mp_free_projectile_think());
  qa_q2mp_publish_projectile(bolt, game, "", false, (qa_q2mp_projectile_behavior){NULL, NULL});
  qa_q2w_ballistics_check_dodge(b->hooks.base, self, game, start, bolt->movedir, speed);
  initial_touch(self, game, bolt);
  return bolt;
}

static void tracker_fly_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_bolts *b = ctx;
  qa_body_state_result target = entity->has_enemy ? q2mp_bread(game, entity->enemy)
      : (qa_body_state_result){false, {q2mp_zero(), q2mp_zero(), q2mp_zero(), {q2mp_zero(), q2mp_zero()}, NULL}};
  if (!entity->has_enemy || !target.has_value || !q2mp_live(game, entity->enemy) || q2mp_health(game, entity->enemy) < 1) {
    qa_q2mp_explode(entity, game, "tracker_explosion");
    return;
  }
  qa_vec3 min = q2mp_add(target.value.origin, target.value.bounds.min);
  qa_vec3 max = q2mp_add(target.value.origin, target.value.bounds.max);
  qa_q2f_entity *enemy_entity = q2mp_ent(game, true, entity->enemy);
  qa_vec3 destination = game->host->is_player(game->host->ctx, entity->enemy)
      ? q2mp_add(target.value.origin, q2mp_v3(0, 0, enemy_entity != NULL ? enemy_entity->view_height : 22))
      : q2mp_len(min) == 0 || q2mp_len(max) == 0 ? target.value.origin : q2mp_scale(q2mp_add(min, max), 0.5);
  entity->movedir = q2mp_norm(q2mp_sub(destination, q2mp_body(game, entity).origin));
  qa_q2f_body_changes c = q2mp_changes();
  c.has_velocity = true; c.velocity = q2mp_scale(entity->movedir, entity->speed);
  c.has_angles = true; c.angles = q2mp_vta(entity->movedir);
  q2mp_move(game, entity, c, false, false);
  game->motion(game->ctx, entity, entity->motion);
  game->schedule(game->ctx, entity, 0.1, b->tracker_fly);
}
static void tracker_pain_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_bolts *b = ctx;
  qa_q2f_entity *target = q2mp_ent(game, entity->has_enemy, entity->enemy);
  qa_body_state_result body = entity->has_enemy ? q2mp_bread(game, entity->enemy)
      : (qa_body_state_result){false, {q2mp_zero(), q2mp_zero(), q2mp_zero(), {q2mp_zero(), q2mp_zero()}, NULL}};
  if (!entity->has_enemy || !body.has_value || q2mp_now(game) - entity->timestamp > 0.5 || q2mp_health(game, entity->enemy) <= 0) {
    if (target != NULL && !game->host->is_player(game->host->ctx, target->actor.id)) {
      target->effects = (double)(q2mp_u32(target->effects) & ~0x80000000u);
      game->show(game->ctx, target);
    }
    game->remove(game->ctx, entity);
    return;
  }
  qa_actor_id enemy = entity->enemy;
  qa_vec3 point = q2mp_rr(game)
      ? q2mp_add(body.value.origin, q2mp_scale(q2mp_add(body.value.bounds.min, body.value.bounds.max), 0.5))
      : body.value.origin;
  double interval = q2mp_rr(game) ? 0.1 : q2mp_frame(game);
  q2mp_damage(game, enemy, entity, entity->actor.id, entity->has_owner ? &entity->owner : NULL, entity->damage, 0,
              q2mp_zero(), point, q2mp_v3(0, 0, 1), QA_Q2MP_MOD_TRACKER, true, 268, "q2:weapon_disintegrator");
  if (!q2mp_live(game, entity->actor.id)) return;
  if (q2mp_health(game, enemy) < 1) {
    qa_q2m_context *monster = b->hooks.monster(b->hooks.ctx, enemy);
    double gib = monster != NULL ? monster->state->gib_health : 0;
    q2mp_damage(game, enemy, entity, entity->actor.id, entity->has_owner ? &entity->owner : NULL, gib == 0 ? 500 : -gib, 0,
                q2mp_zero(), point, q2mp_v3(0, 0, 1), QA_Q2MP_MOD_TRACKER, true, 268, "q2:weapon_disintegrator");
  }
  if (game->host->is_player(game->host->ctx, enemy)) {
    qa_q2mp_player_effect fx;
    memset(&fx, 0, sizeof fx);
    fx.kind = QA_Q2MP_EFFECT_TRACKER_PAIN; fx.actor = enemy; fx.until = q2mp_now(game) + interval;
    b->hooks.player_effect(b->hooks.ctx, fx);
  } else if (target != NULL) {
    target->effects = (double)(q2mp_u32(target->effects) | 0x80000000u);
    game->show(game->ctx, target);
  }
  game->schedule(game->ctx, entity, interval, b->tracker_pain);
}
static void tracker_touch_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_touch_contact contact) {
  qa_q2mp_bolts *b = ctx;
  if (entity->has_owner && qa_same_actor(contact.other, entity->owner)) return;
  if ((q2mp_u32(q2mp_surf(&contact)) & 4) != 0) { game->remove(game->ctx, entity); return; }
  qa_combat_state_result target = q2mp_cread(game, contact.other);
  qa_body_state body = q2mp_body(game, entity);
  if (target.has_value && target.value.can_take_damage) {
    bool creature = game->host->is_monster(game->host->ctx, contact.other) || game->host->is_player(game->host->ctx, contact.other);
    bool live = creature && target.value.health > 0;
    q2mp_damage(game, contact.other, entity, entity->actor.id, entity->has_owner ? &entity->owner : NULL,
                live ? 0 : creature ? entity->damage * 4 : entity->damage, entity->damage * 3, body.velocity, body.origin,
                q2mp_cnormal(&contact), QA_Q2MP_MOD_TRACKER, true, 260, "q2:weapon_disintegrator");
    if (live) {
      qa_body_state_result target_body = q2mp_bread(game, contact.other);
      qa_q2f_entity *target_entity = q2mp_ent(game, true, contact.other);
      double flags = target_entity != NULL ? target_entity->flags : 0;
      if (target_body.has_value && (q2mp_u32(flags) & 3) == 0)
        qa_q2mp_velocity(game, contact.other, q2mp_add(target_body.value.velocity, q2mp_v3(0, 0, 140)), false);
      qa_q2f_entity *daemon = game->create(game->ctx, "pain daemon", NULL);
      daemon->owner = entity->owner; daemon->has_owner = entity->has_owner;
      daemon->enemy = contact.other; daemon->has_enemy = true;
      daemon->damage = trunc(entity->damage * (q2mp_rr(game) ? 0.1 : q2mp_frame(game)) / 0.5);
      daemon->timestamp = q2mp_now(game);
      game->schedule(game->ctx, daemon, q2mp_rr(game) ? 0 : q2mp_frame(game), b->tracker_pain);
    }
  }
  qa_q2mp_explode(entity, game, "tracker_explosion");
}
qa_q2f_entity *qa_q2mp_bolts_fire_tracker(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start,
                                          qa_vec3 direction, double damage, double speed, bool has_enemy, qa_actor_id enemy) {
  register_callbacks(b, game);
  qa_q2f_entity *bolt = qa_q2mp_projectile(self, game, "tracker", start, q2mp_norm(direction), speed,
                                           "models/proj/disintegrator/tris.md2", QA_Q2F_MOTION_FLY_MISSILE, 0x4000000);
  qa_q2mp_bolts_player_collision(b, self, game, bolt);
  bolt->damage = damage; bolt->enemy = enemy; bolt->has_enemy = has_enemy; bolt->touch = b->tracker_touch;
  game->schedule(game->ctx, bolt, !has_enemy ? 10 : 0.1, !has_enemy ? qa_q2mp_free_projectile_think() : b->tracker_fly);
  qa_q2mp_publish_projectile(bolt, game, "weapons/disrupt.wav", true, (qa_q2mp_projectile_behavior){"q2:weapon_disintegrator", "energy"});
  qa_q2w_ballistics_check_dodge(b->hooks.base, self, game, start, bolt->movedir, speed);
  initial_touch(self, game, bolt);
  return bolt;
}

void qa_q2mp_bolts_fire_heat_beam(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start,
                                  qa_vec3 direction, qa_vec3 offset, double damage, double kick) {
  (void)offset;
  const double water_mask = 56;
  qa_vec3 end = q2mp_add(start, q2mp_scale(q2mp_norm(direction), 8192));
  bool underwater = ((q2mp_u32(game->host->point_contents(game->host->ctx, start)) & 56) != 0);
  double mask = qa_q2mp_projectile_mask(game);
  if (q2mp_rr(game) && game->host->is_player(game->host->ctx, self.id)) {
    const qa_q2w_input *input = qa_q2w_ballistics_input(b->hooks.base, self.id);
    if (input != NULL && !input->players_collide) mask = (double)(q2mp_u32(mask) & ~0x40000000u);
  }
  qa_vec3 water_start = start;
  bool water = false;
  qa_trace_result trace = q2mp_trace(game, start, end, NULL, &self.id, mask + (underwater ? 0 : water_mask));
  if (strcmp(trace.kind, "q1") != 0 && ((q2mp_u32(trace.contents)) & 56) != 0) {
    water = true; water_start = trace.end;
    if (q2mp_len(q2mp_sub(start, water_start)) != 0) {
      qa_q2f_presentation_event e;
      memset(&e, 0, sizeof e);
      e.kind = QA_Q2F_EVENT_EFFECT; e.effect = "q2:heatbeam_sparks"; e.origin = water_start;
      e.direction = q2mp_normal(trace); e.count = 0; e.color = 0;
      game->host->emit(game->host->ctx, e);
    }
    trace = q2mp_trace(game, water_start, end, NULL, &self.id, mask);
  }
  qa_vec3 normal = q2mp_normal(trace);
  bool sky = strcmp(trace.kind, "q2") == 0 ? ((q2mp_u32(trace.surface ? trace.surface->flags : 0) & 4) != 0)
             : strcmp(trace.kind, "q3") == 0 && trace.has_surface_flags && ((q2mp_u32(trace.surface_flags) & 4) != 0);
  bool has_actor = trace.hit.kind == QA_TRACE_HIT_ACTOR;
  qa_actor_id actor = has_actor ? trace.hit.actor : self.id;
  if (!sky && trace.fraction < 1) {
    if (has_actor && q2mp_can_hurt(game, actor))
      q2mp_damage(game, actor, NULL, self.id, &self.id, water ? trunc(damage / 2) : damage, kick, direction, trace.end,
                  normal, QA_Q2MP_MOD_HEATBEAM, true, 4, "q2:weapon_plasmabeam");
    else if (!water && !(strcmp(trace.kind, "q2") == 0 && trace.surface != NULL && strncmp(trace.surface->name, "sky", 3) == 0)) {
      qa_q2f_presentation_event e;
      memset(&e, 0, sizeof e);
      e.kind = QA_Q2F_EVENT_EFFECT; e.effect = "q2:heatbeam_steam"; e.origin = trace.end;
      e.direction = normal; e.count = 0; e.color = 0;
      game->host->emit(game->host->ctx, e);
      qa_q2w_ballistics_player_noise(b->hooks.base, self, game, trace.end, "impact");
    }
  }
  if (water || underwater) {
    qa_vec3 pos = q2mp_add(trace.end, q2mp_scale(q2mp_norm(q2mp_sub(trace.end, water_start)), -2));
    qa_vec3 water_end = ((q2mp_u32(game->host->point_contents(game->host->ctx, pos)) & 56) != 0)
        ? pos : q2mp_trace(game, pos, water_start, NULL, has_actor ? &actor : NULL, water_mask).end;
    qa_q2w_event ev;
    memset(&ev, 0, sizeof ev);
    ev.kind = QA_Q2W_EVENT_BEAM; ev.effect = "bubble-trail"; ev.actor = &self.id;
    ev.start = water_start; ev.end = water_end; ev.duration = 0;
    b->hooks.base->hooks.emit(b->hooks.base->hooks.ctx, ev);
  }
  qa_q2w_event ev;
  memset(&ev, 0, sizeof ev);
  ev.kind = QA_Q2W_EVENT_BEAM;
  ev.effect = game->host->is_player(game->host->ctx, self.id) ? "heatbeam" : "monster-heatbeam";
  ev.actor = &self.id; ev.start = start; ev.end = trace.end; ev.duration = 0;
  b->hooks.base->hooks.emit(b->hooks.base->hooks.ctx, ev);
}

void qa_q2mp_bolts_init(qa_q2mp_bolts *b, qa_q2mp_projectile_hooks hooks) {
  memset(b, 0, sizeof *b);
  b->hooks = hooks;
  b->ion_sparks = (qa_q2f_think){b, ion_sparks_fn};
  b->heat_think = (qa_q2f_think){b, heat_think_fn};
  b->heat_think_rerelease = (qa_q2f_think){b, heat_think_rr_fn};
  b->tracker_fly = (qa_q2f_think){b, tracker_fly_fn};
  b->tracker_pain = (qa_q2f_think){b, tracker_pain_fn};
  b->ion_touch = (qa_q2f_touch){b, ion_touch_fn};
  b->plasma_touch = (qa_q2f_touch){b, plasma_touch_fn};
  b->flechette_touch = (qa_q2f_touch){b, flechette_touch_fn};
  b->green_touch = (qa_q2f_touch){b, green_touch_fn};
  b->tracker_touch = (qa_q2f_touch){b, tracker_touch_fn};
  qa_q2f_think free_think = qa_q2mp_free_projectile_think();
  b->cb_think[0] = (qa_q2f_named_think){"missionpack.free", free_think};
  b->cb_think[1] = (qa_q2f_named_think){"ionripper_sparks", b->ion_sparks};
  b->cb_think[2] = (qa_q2f_named_think){"heat_think", b->heat_think};
  b->cb_think[3] = (qa_q2f_named_think){"rerelease/heat_think", b->heat_think_rerelease};
  b->cb_think[4] = (qa_q2f_named_think){"tracker_fly", b->tracker_fly};
  b->cb_think[5] = (qa_q2f_named_think){"tracker_pain_daemon_think", b->tracker_pain};
  b->cb_touch[0] = (qa_q2f_named_touch){"ionripper_touch", b->ion_touch};
  b->cb_touch[1] = (qa_q2f_named_touch){"plasma_touch", b->plasma_touch};
  b->cb_touch[2] = (qa_q2f_named_touch){"flechette_touch", b->flechette_touch};
  b->cb_touch[3] = (qa_q2f_named_touch){"blaster2_touch", b->green_touch};
  b->cb_touch[4] = (qa_q2f_named_touch){"tracker_touch", b->tracker_touch};
  b->cb_traj[0] = (qa_q2f_trajectory_def){b->ion_touch, b, project_steering};
  b->cb_traj[1] = (qa_q2f_trajectory_def){b->plasma_touch, b, project_steering};
  b->cb_traj[2] = (qa_q2f_trajectory_def){b->flechette_touch, b, project_steering};
  b->cb_traj[3] = (qa_q2f_trajectory_def){b->tracker_touch, b, project_steering};
}
qa_q2f_callback_definitions qa_q2mp_bolts_callbacks(qa_q2mp_bolts *b) {
  qa_q2f_callback_definitions defs;
  memset(&defs, 0, sizeof defs);
  defs.trajectory = b->cb_traj; defs.trajectory_count = 4;
  defs.think = b->cb_think; defs.think_count = 6;
  defs.touch = b->cb_touch; defs.touch_count = 5;
  return defs;
}

