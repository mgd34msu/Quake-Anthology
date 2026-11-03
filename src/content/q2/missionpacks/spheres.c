/* Port of src/content/q2/missionpacks/spheres.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/spheres.h"
#include "qa/foundation/js.h"

#include <string.h>

#include "missionpacks_internal.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/missionpacks/projectiles/common.h"

const char *qa_q2mp_sphere_kind_name(qa_q2mp_sphere_kind kind) {
  return kind == QA_Q2MP_SPHERE_DEFENDER ? "defender" : kind == QA_Q2MP_SPHERE_HUNTER ? "hunter" : "vengeance";
}
/* Math.max: NaN when either side is NaN. */
static uint32_t dopple_flag(qa_q2_game_services *game) { return q2mp_rr(game) ? 0x10000u : 0x100u; }
static bool is_dopple(qa_q2_entity *entity, qa_q2_game_services *game) {
  return ((q2mp_u32(entity->spawnflags) & dopple_flag(game)) != 0);
}
static void player_effect_camera(qa_q2mp_spheres *s, qa_actor_id actor, bool has_sphere, qa_actor_id sphere,
                                 qa_vec3 origin, qa_vec3 angles) {
  qa_q2mp_player_effect fx;
  memset(&fx, 0, sizeof fx);
  fx.kind = QA_Q2MP_EFFECT_SPHERE_CAMERA; fx.actor = actor; fx.has_sphere = has_sphere; fx.sphere = sphere;
  fx.origin = origin; fx.angles = angles;
  s->hooks.player_effect(s->hooks.ctx, fx);
}

qa_q2_entity *qa_q2mp_spheres_owned_sphere(qa_q2mp_spheres *s, qa_actor_id actor, qa_q2_game_services *game) {
  (void)s;
  for (size_t i = 0; i < qa_q2_game_entity_count(game); i++) {
    qa_q2_entity *entity = qa_q2_game_entity_at(game, i);
    if (strcmp(entity->classname, "sphere") == 0 && entity->has_owner && qa_same_actor(entity->owner, actor) &&
        !is_dopple(entity, game)) return entity;
  }
  return NULL;
}
void qa_q2mp_spheres_owner_damaged(qa_q2mp_spheres *s, qa_actor_id actor, const qa_attack_provenance *attack,
                                   qa_q2_game_services *game) {
  qa_q2_entity *sphere = qa_q2mp_spheres_owned_sphere(s, actor, game);
  if (sphere != NULL && sphere->pain.fn != NULL) {
    qa_pain_reaction reaction;
    memset(&reaction, 0, sizeof reaction);
    reaction.attack = attack; reaction.self = sphere->actor;
    reaction.attacker = attack != NULL ? attack->attacker : NULL; reaction.damage = 0; reaction.kick = 0;
    sphere->pain.fn(sphere->pain.ctx, sphere, game, reaction);
  }
}
void qa_q2mp_spheres_owner_died(qa_q2mp_spheres *s, qa_actor_id actor, qa_q2_game_services *game,
                                const qa_attack_provenance *attack) {
  qa_q2_entity *sphere = qa_q2mp_spheres_owned_sphere(s, actor, game);
  if (sphere != NULL && sphere->die.fn != NULL) {
    qa_death_reaction reaction;
    memset(&reaction, 0, sizeof reaction);
    reaction.pain.attack = attack; reaction.pain.self = sphere->actor; reaction.pain.attacker = &actor;
    reaction.pain.damage = 0; reaction.pain.kick = 0; reaction.inflictor = &actor; reaction.point = q2mp_zero();
    sphere->die.fn(sphere->die.ctx, sphere, game, reaction);
  }
}
void qa_q2mp_spheres_disconnect(qa_q2mp_spheres *s, qa_actor_id actor, qa_q2_game_services *game) {
  qa_q2_entity *sphere = qa_q2mp_spheres_owned_sphere(s, actor, game);
  if (sphere != NULL) game->remove(game->ctx, sphere);
}

static void expire_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_spheres *s = ctx;
  qa_q2_entity *owner = q2mp_ent(game, entity->has_owner, entity->owner);
  if (owner != NULL && ((q2mp_u32(owner->flags) & 0x4000u) != 0)) {
    qa_q2_body_changes c = q2mp_changes();
    c.has_velocity = true; c.velocity = q2mp_zero();
    q2mp_move(game, owner, c, false, false);
    game->motion(game->ctx, owner, "stationary");
    qa_actor_id none = owner->actor.id;
    qa_actor_id actor2 = owner->actor.id;
    qa_actor_id sphere = none;
    qa_vec3 origin2 = q2mp_body(game, owner).origin;
    qa_vec3 angles2 = q2mp_body(game, owner).angles;
    player_effect_camera(s, actor2, false, sphere, origin2, angles2);
  }
  qa_q2mp_explode_default(entity, game);
}
static void die_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction) {
  (void)reaction;
  expire_fn(ctx, entity, game);
}
static void idle_die_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction) {
  (void)reaction;
  if (!entity->has_enemy) expire_fn(ctx, entity, game);
}
static void loop_sound(qa_q2_entity *entity, qa_q2_game_services *game, const char *sound) {
  if (strcmp(entity->sound, sound) == 0) return;
  if (strcmp(entity->sound, "") != 0) {
    qa_q2_presentation_event e;
    memset(&e, 0, sizeof e);
    e.kind = "sound"; e.has_actor = true; e.actor = entity->actor.id; e.origin = q2mp_body(game, entity).origin;
    e.path = entity->sound; e.channel = 0; e.volume = 1; e.attenuation = 1; e.reliable = false; e.loop = "stop";
    game->host->emit(game->host->ctx, e);
  }
  entity->sound = sound;
  qa_q2_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = "sound"; e.has_actor = true; e.actor = entity->actor.id; e.origin = q2mp_body(game, entity).origin;
  e.path = sound; e.channel = 0; e.volume = 1; e.attenuation = 1; e.reliable = false; e.loop = "start";
  game->host->emit(game->host->ctx, e);
}
static void fly(qa_q2mp_spheres *s, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (q2mp_now(game) >= entity->wait) { expire_fn(s, entity, game); return; }
  qa_body_state_result owner = entity->has_owner ? q2mp_bread(game, entity->owner)
      : (qa_body_state_result){false, {q2mp_zero(), q2mp_zero(), q2mp_zero(), {q2mp_zero(), q2mp_zero()}, NULL}};
  if (!owner.has_value) { game->remove(game->ctx, entity); return; }
  qa_vec3 destination = q2mp_v3(owner.value.origin.x, owner.value.origin.y, owner.value.origin.z + owner.value.bounds.max.z + 4);
  double left = q2mp_now(game);
  double right = trunc(q2mp_now(game));
  if (left == right && entity->has_owner && !qa_q2mp_sight(game, entity, entity->owner)) {
    qa_q2_body_changes c = q2mp_changes();
    c.has_origin = true; c.origin = destination;
    q2mp_move(game, entity, c, false, false);
    return;
  }
  qa_q2_body_changes c = q2mp_changes();
  c.has_velocity = true; c.velocity = q2mp_scale(q2mp_sub(destination, q2mp_body(game, entity).origin), 5);
  q2mp_move(game, entity, c, false, false);
  game->motion(game->ctx, entity, entity->motion);
}
static void chase(qa_q2mp_spheres *s, qa_q2_entity *entity, qa_q2_game_services *game, bool direct) {
  qa_body_state_result body = entity->has_enemy ? q2mp_bread(game, entity->enemy)
      : (qa_body_state_result){false, {q2mp_zero(), q2mp_zero(), q2mp_zero(), {q2mp_zero(), q2mp_zero()}, NULL}};
  if (q2mp_now(game) >= entity->wait || !entity->has_enemy || !body.has_value || q2mp_health(game, entity->enemy) < 1) {
    expire_fn(s, entity, game);
    return;
  }
  qa_actor_id enemy = entity->enemy;
  qa_q2_entity *enemy_entity = q2mp_ent(game, true, enemy);
  qa_vec3 destination = game->host->is_player(game->host->ctx, enemy)
      ? q2mp_add(body.value.origin, q2mp_v3(0, 0, enemy_entity != NULL ? enemy_entity->view_height : 22))
      : body.value.origin;
  qa_vec3 direction;
  double speed;
  if (direct || qa_q2mp_sight(game, entity, enemy)) {
    if (!direct) loop_sound(entity, game, "spheres/h_active.wav");
    direction = q2mp_norm(q2mp_sub(destination, q2mp_body(game, entity).origin));
    speed = 500;
    entity->pos1 = destination;
  } else if (q2mp_len(entity->pos1) == 0) {
    direction = q2mp_norm(q2mp_sub(body.value.origin, q2mp_body(game, entity).origin));
    speed = 0;
    loop_sound(entity, game, "spheres/h_lurk.wav");
  } else {
    direction = q2mp_sub(entity->pos1, q2mp_body(game, entity).origin);
    double distance = q2mp_len(direction);
    direction = q2mp_norm(direction);
    if (distance > 1) {
      speed = distance > 500 ? 500 : distance < 20 ? distance / q2mp_frame(game) : distance;
      if (!direct) loop_sound(entity, game, "spheres/h_active.wav");
    } else {
      direction = q2mp_norm(q2mp_sub(body.value.origin, q2mp_body(game, entity).origin));
      speed = 0;
      if (!direct) loop_sound(entity, game, "spheres/h_lurk.wav");
    }
  }
  qa_q2_body_changes c = q2mp_changes();
  c.has_angles = true; c.angles = q2mp_vta(direction);
  c.has_velocity = true; c.velocity = q2mp_scale(direction, speed);
  q2mp_move(game, entity, c, false, false);
  game->motion(game->ctx, entity, entity->motion);
}
static void touch_sphere(qa_q2mp_spheres *s, qa_q2_entity *entity, qa_q2_game_services *game, qa_touch_contact contact, double means) {
  if (is_dopple(entity, game)) {
    if (entity->has_team_master && qa_same_actor(contact.other, entity->team_master)) return;
    entity->owner = entity->team_master; entity->has_owner = entity->has_team_master;
    entity->has_team_master = false;
  } else {
    qa_q2_entity *other = q2mp_ent(game, true, contact.other);
    if ((entity->has_owner && qa_same_actor(contact.other, entity->owner)) ||
        (other != NULL && strcmp(other->classname, "bodyque") == 0)) return;
  }
  if ((q2mp_u32(q2mp_surf(&contact)) & 4) != 0) { game->remove(game->ctx, entity); return; }
  qa_body_state body = q2mp_body(game, entity);
  if (q2mp_can_hurt(game, contact.other))
    q2mp_damage(game, contact.other, entity, entity->actor.id, entity->has_owner ? &entity->owner : NULL, 10000, 1,
                body.velocity, body.origin, q2mp_cnormal(&contact), means, true, 64, NULL);
  else q2mp_radius(game, entity, entity->has_owner ? &entity->owner : NULL, 512,
                   entity->has_owner ? &entity->owner : NULL, 256, means, false, 0, NULL);
  expire_fn(s, entity, game);
}
static void vengeance_touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_touch_contact contact) {
  touch_sphere(ctx, entity, game, contact, is_dopple(entity, game) ? QA_Q2MP_MOD_DOPPLE_VENGEANCE : QA_Q2MP_MOD_VENGEANCE_SPHERE);
}
static void hunter_touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_touch_contact contact) {
  if (qa_same_actor(contact.other, game->host->world_actor(game->host->ctx))) return;
  qa_q2_entity *owner = q2mp_ent(game, entity->has_owner, entity->owner);
  if (owner != NULL && ((q2mp_u32(owner->flags) & 0x4000u) != 0)) {
    qa_q2_body_changes c = q2mp_changes();
    c.has_velocity = true; c.velocity = q2mp_zero();
    q2mp_move(game, owner, c, false, false);
    game->motion(game->ctx, owner, "stationary");
  }
  touch_sphere(ctx, entity, game, contact, is_dopple(entity, game) ? QA_Q2MP_MOD_DOPPLE_HUNTER : QA_Q2MP_MOD_HUNTER_SPHERE);
}

static void defender_pain_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_pain_reaction reaction) {
  (void)ctx; (void)game;
  bool same = (reaction.attacker == NULL && !entity->has_owner) ||
              (reaction.attacker != NULL && entity->has_owner && qa_same_actor(*reaction.attacker, entity->owner));
  if (!same) {
    if (reaction.attacker != NULL) entity->enemy = *reaction.attacker;
    entity->has_enemy = reaction.attacker != NULL;
  }
}
static void vengeance_pain_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_pain_reaction reaction) {
  qa_q2mp_spheres *s = ctx;
  if (entity->has_enemy || reaction.attacker == NULL) return;
  if (!is_dopple(entity, game) &&
      ((entity->has_owner && q2mp_health(game, entity->owner) >= 25) ||
       (entity->has_owner && qa_same_actor(*reaction.attacker, entity->owner)))) return;
  entity->wait = is_dopple(entity, game) ? q2mp_now(game) + 15 : qa_js_max(entity->wait, q2mp_now(game) + 15);
  entity->effects = (double)(qa_js_to_int32(q2mp_u32(entity->effects) | 16u));
  entity->touch = s->vengeance_touch;
  entity->enemy = *reaction.attacker; entity->has_enemy = true;
  game->show(game->ctx, entity);
}
static void hunter_pain_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_pain_reaction reaction) {
  qa_q2mp_spheres *s = ctx;
  if (entity->has_enemy || reaction.attacker == NULL) return;
  qa_q2_entity *owner = q2mp_ent(game, entity->has_owner, entity->owner);
  bool dopple = is_dopple(entity, game);
  if (!dopple && ((owner != NULL && q2mp_health(game, owner->actor.id) > 0) ||
      (entity->has_owner && qa_same_actor(*reaction.attacker, entity->owner)))) return;
  entity->wait = dopple ? q2mp_now(game) + 15 : qa_js_max(entity->wait, q2mp_now(game) + 15);
  entity->effects = (double)(qa_js_to_int32(q2mp_u32(entity->effects) | (8u | 0x4000000u)));
  entity->touch = s->hunter_touch;
  entity->enemy = *reaction.attacker; entity->has_enemy = true;
  game->show(game->ctx, entity);
  if (dopple || owner == NULL || !game->host->is_player(game->host->ctx, owner->actor.id) || !s->hooks.hunter_camera ||
      ((q2mp_u32(qa_q2_options_deathmatch_flags(&game->options)) & 1024u) != 0)) return;
  qa_body_state_result target = q2mp_bread(game, *reaction.attacker);
  if (!target.has_value || q2mp_len(q2mp_sub(target.value.origin, q2mp_body(game, entity).origin)) < 192) return;
  q2mp_sound(game, owner, "misc/udeath.wav", true, 4, false, 0, false, 0);
  qa_q2m_gib_options gib = {false, false, false, false};
  for (int index = 0; index < 4; index++) (void)qa_q2m_throw_gib(owner, game, "models/objects/gibs/sm_meat/tris.md2", 50, gib);
  (void)qa_q2m_throw_gib(owner, game, "models/objects/gibs/skull/tris.md2", 50, gib);
  qa_vec3 origin = q2mp_add(q2mp_body(game, owner).origin, q2mp_v3(0, 0, owner->view_height));
  qa_q2_body_changes c = q2mp_changes();
  c.has_origin = true; c.origin = origin;
  q2mp_move(game, entity, c, false, false);
  owner->model = ""; owner->model2 = ""; owner->view_height = 8;
  owner->flags = q2mp_js_or(owner->flags, 0x4000u);
  qa_q2_body_changes oc = q2mp_changes();
  oc.has_origin = true; oc.origin = origin;
  oc.has_angles = true; oc.angles = q2mp_body(game, entity).angles;
  oc.has_bounds = true; oc.bounds = (qa_bounds){q2mp_v3(-5, -5, -5), q2mp_v3(5, 5, 5)};
  q2mp_move(game, owner, oc, false, false);
  game->solid(game->ctx, owner, "none");
  game->motion(game->ctx, owner, "fly-missile");
  game->show(game->ctx, owner);
  game->solid(game->ctx, entity, "box");
  player_effect_camera(s, owner->actor.id, true, entity->actor.id, origin, q2mp_body(game, entity).angles);
}

static void defender_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_spheres *s = ctx;
  qa_q2_entity *owner = q2mp_ent(game, entity->has_owner, entity->owner);
  if (owner == NULL) { game->remove(game->ctx, entity); return; }
  if (s->hooks.intermission(s->hooks.ctx) || q2mp_health(game, owner->actor.id) <= 0) { expire_fn(s, entity, game); return; }
  if (++entity->frame > 19) entity->frame = 0;
  if (entity->has_enemy) {
    qa_body_state_result target = q2mp_bread(game, entity->enemy);
    if (q2mp_health(game, entity->enemy) <= 0 || !target.has_value) entity->has_enemy = false;
    else if (!(entity->has_owner && qa_same_actor(entity->enemy, entity->owner)) && entity->delay <= q2mp_now(game) &&
             qa_q2mp_sight(game, entity, entity->enemy)) {
      qa_vec3 origin = q2mp_body(game, entity).origin;
      (void)qa_q2mp_bolts_fire_blaster2(&s->projectiles->base.base.base, owner->actor, game,
                                        q2mp_add(origin, q2mp_v3(0, 0, 2)),
                                        q2mp_norm(q2mp_sub(target.value.origin, origin)), 10, 1000, 8);
      entity->delay = q2mp_now(game) + 0.4;
    }
  }
  fly(s, entity, game);
  if (q2mp_live(game, entity->actor.id)) {
    game->show(game->ctx, entity);
    game->schedule(game->ctx, entity, 0.1, s->defender_think);
  }
}
static void hunter_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_spheres *s = ctx;
  if (s->hooks.intermission(s->hooks.ctx)) { expire_fn(s, entity, game); return; }
  qa_q2_entity *owner = q2mp_ent(game, entity->has_owner, entity->owner);
  qa_body_state_result enemy = entity->has_enemy ? q2mp_bread(game, entity->enemy)
      : (qa_body_state_result){false, {q2mp_zero(), q2mp_zero(), q2mp_zero(), {q2mp_zero(), q2mp_zero()}, NULL}};
  if (owner == NULL && !is_dopple(entity, game)) { game->remove(game->ctx, entity); return; }
  qa_body_state body = q2mp_body(game, entity);
  double ideal = owner != NULL ? q2mp_body(game, owner).angles.y
      : enemy.has_value ? q2mp_vta(q2mp_sub(enemy.value.origin, body.origin)).y : body.angles.y;
  double wrapped = fmod(fmod(body.angles.y, 360) + 360, 360);
  double delta = ideal - wrapped;
  if (delta > 180) delta -= 360;
  if (delta < -180) delta += 360;
  double clamped = delta < -40 ? -40 : delta > 40 ? 40 : delta;
  qa_q2_body_changes c = q2mp_changes();
  c.has_angles = true;
  c.angles = q2mp_v3(body.angles.x, fmod(body.angles.y + clamped + 360, 360), body.angles.z);
  q2mp_move(game, entity, c, false, false);
  if (!entity->has_enemy) fly(s, entity, game);
  else chase(s, entity, game, false);
  if (owner != NULL && ((q2mp_u32(owner->flags) & 0x4000u) != 0) && q2mp_live(game, entity->actor.id)) {
    qa_body_state current = q2mp_body(game, entity);
    qa_body_state current_owner = q2mp_body(game, owner);
    owner->view_height = current.origin.z - current_owner.origin.z;
    qa_vec3 angles = !enemy.has_value ? current.angles
        : q2mp_v3(0, q2mp_vta(q2mp_sub(enemy.value.origin, current.origin)).y, 0);
    qa_q2_body_changes oc = q2mp_changes();
    oc.has_origin = true; oc.origin = current.origin;
    oc.has_velocity = true; oc.velocity = current.velocity;
    oc.has_bounds = true; oc.bounds = (qa_bounds){q2mp_zero(), q2mp_zero()};
    q2mp_move(game, owner, oc, false, false);
    game->motion(game->ctx, owner, "fly-missile");
    player_effect_camera(s, owner->actor.id, true, entity->actor.id, current.origin, angles);
  }
  if (q2mp_live(game, entity->actor.id)) game->schedule(game->ctx, entity, 0.1, s->hunter_think);
}
static void vengeance_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_spheres *s = ctx;
  if (s->hooks.intermission(s->hooks.ctx)) { expire_fn(s, entity, game); return; }
  if (q2mp_ent(game, entity->has_owner, entity->owner) == NULL && !is_dopple(entity, game)) {
    game->remove(game->ctx, entity);
    return;
  }
  if (!entity->has_enemy) fly(s, entity, game);
  else chase(s, entity, game, true);
  if (q2mp_live(game, entity->actor.id)) game->schedule(game->ctx, entity, 0.1, s->vengeance_think);
}

qa_q2_entity *qa_q2mp_spheres_launch(qa_q2mp_spheres *s, qa_q2_entity *owner, qa_q2_game_services *game,
                                      qa_q2mp_sphere_kind kind, bool dopple) {
  qa_q2_source_callbacks_register(game->source_callbacks, qa_q2mp_spheres_callbacks(s));
  if (!dopple) {
    qa_q2_entity *old = qa_q2mp_spheres_owned_sphere(s, owner->actor.id, game);
    if (old != NULL) game->remove(game->ctx, old);
  }
  qa_q2_entity *sphere = game->create(game->ctx, "sphere", NULL, 0);
  qa_body_state body = q2mp_body(game, owner);
  sphere->owner = owner->actor.id; sphere->has_owner = !dopple;
  sphere->team_master = owner->team_master; sphere->has_team_master = dopple && owner->has_team_master;
  sphere->spawnflags = (double)(qa_js_to_int32((kind == QA_Q2MP_SPHERE_DEFENDER ? 1u : kind == QA_Q2MP_SPHERE_HUNTER ? 2u : 4u) |
                                 (dopple ? dopple_flag(game) : 0u)));
  sphere->clip_mask = qa_q2mp_projectile_mask(game);
  sphere->render_flags = 8 | 0x8000;
  sphere->motion = "fly-missile";
  sphere->wait = q2mp_now(game) + 30;
  sphere->model = kind == QA_Q2MP_SPHERE_DEFENDER ? "models/items/defender/tris.md2"
      : kind == QA_Q2MP_SPHERE_HUNTER ? "models/items/hunter/tris.md2" : "models/items/vengnce/tris.md2";
  if (kind == QA_Q2MP_SPHERE_DEFENDER) {
    sphere->model2 = "models/items/shell/tris.md2"; sphere->pain = s->defender_pain; sphere->die = s->die;
  } else if (kind == QA_Q2MP_SPHERE_HUNTER) {
    sphere->pain = s->hunter_pain; sphere->die = s->idle_die;
  } else {
    sphere->pain = s->vengeance_pain; sphere->die = s->idle_die; sphere->angular_velocity = q2mp_v3(30, 30, 0);
  }
  qa_q2_body_changes c = q2mp_changes();
  c.has_origin = true; c.origin = q2mp_v3(body.origin.x, body.origin.y, body.origin.z + body.bounds.max.z);
  c.has_angles = true; c.angles = q2mp_v3(0, body.angles.y, 0);
  c.has_bounds = true; c.bounds = (qa_bounds){q2mp_zero(), q2mp_zero()};
  q2mp_move(game, sphere, c, true, false);
  game->schedule(game->ctx, sphere, 0.1, kind == QA_Q2MP_SPHERE_DEFENDER ? s->defender_think
                 : kind == QA_Q2MP_SPHERE_HUNTER ? s->hunter_think : s->vengeance_think);
  qa_q2mp_publish_projectile(sphere, game, "", false, (qa_q2mp_projectile_behavior){NULL, NULL});
  loop_sound(sphere, game, kind == QA_Q2MP_SPHERE_DEFENDER ? "spheres/d_idle.wav"
             : kind == QA_Q2MP_SPHERE_HUNTER ? "spheres/h_idle.wav" : "spheres/v_idle.wav");
  return sphere;
}

void qa_q2mp_spheres_init(qa_q2mp_spheres *s, qa_q2mp_projectiles *projectiles, qa_q2mp_sphere_hooks hooks) {
  memset(s, 0, sizeof *s);
  s->projectiles = projectiles;
  s->hooks = hooks;
  s->expire = (qa_q2_think){s, expire_fn};
  s->defender_think = (qa_q2_think){s, defender_think_fn};
  s->hunter_think = (qa_q2_think){s, hunter_think_fn};
  s->vengeance_think = (qa_q2_think){s, vengeance_think_fn};
  s->defender_pain = (qa_q2_pain){s, defender_pain_fn};
  s->hunter_pain = (qa_q2_pain){s, hunter_pain_fn};
  s->vengeance_pain = (qa_q2_pain){s, vengeance_pain_fn};
  s->die = (qa_q2_die){s, die_fn};
  s->idle_die = (qa_q2_die){s, idle_die_fn};
  s->hunter_touch = (qa_q2_touch){s, hunter_touch_fn};
  s->vengeance_touch = (qa_q2_touch){s, vengeance_touch_fn};
  s->cb_think[0] = (qa_q2_named_think){"sphere_think_explode", s->expire};
  s->cb_think[1] = (qa_q2_named_think){"defender_think", s->defender_think};
  s->cb_think[2] = (qa_q2_named_think){"hunter_think", s->hunter_think};
  s->cb_think[3] = (qa_q2_named_think){"vengeance_think", s->vengeance_think};
  s->cb_pain[0] = (qa_q2_named_pain){"defender_pain", s->defender_pain};
  s->cb_pain[1] = (qa_q2_named_pain){"hunter_pain", s->hunter_pain};
  s->cb_pain[2] = (qa_q2_named_pain){"vengeance_pain", s->vengeance_pain};
  s->cb_die[0] = (qa_q2_named_die){"sphere_explode", s->die};
  s->cb_die[1] = (qa_q2_named_die){"sphere_if_idle_die", s->idle_die};
  s->cb_touch[0] = (qa_q2_named_touch){"hunter_touch", s->hunter_touch};
  s->cb_touch[1] = (qa_q2_named_touch){"vengeance_touch", s->vengeance_touch};
}
qa_q2_callback_definitions qa_q2mp_spheres_callbacks(qa_q2mp_spheres *s) {
  qa_q2_callback_definitions defs;
  memset(&defs, 0, sizeof defs);
  defs.think = s->cb_think; defs.think_count = 4;
  defs.pain = s->cb_pain; defs.pain_count = 3;
  defs.die = s->cb_die; defs.die_count = 2;
  defs.touch = s->cb_touch; defs.touch_count = 2;
  return defs;
}
