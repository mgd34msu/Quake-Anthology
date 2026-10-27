/* Port of src/content/q2/missionpacks/projectiles/mines.ts. GPL-2.0-or-later.
 * TS game.host.actors.observations() has no qa_q2f mirror; iteration
 * uses the game entities snapshot (actor ids of spawned entities). */
#include "qa/content/q2/missionpacks/projectiles/mines.h"

#include <string.h>

#include "../missionpacks_internal.h"
#include "qa/content/q2/missionpacks/projectiles/common.h"
#include "qa/foundation/support.h"

static double mine_life(double multiplier) { return multiplier == 2 ? 30 : multiplier == 4 ? 15 : multiplier == 8 ? 10 : 45; }
static bool player_start(qa_q2f_entity *entity, qa_q2f_game *game) {
  if (q2mp_rr(game))
    return strncmp(entity->classname, "info_player_", 12) == 0 || strcmp(entity->classname, "misc_teleporter_dest") == 0 ||
           strncmp(entity->classname, "item_flag_", 10) == 0;
  return strcmp(entity->classname, "info_player_deathmatch") == 0 || strcmp(entity->classname, "info_player_start") == 0 ||
         strcmp(entity->classname, "info_player_coop") == 0 || strcmp(entity->classname, "misc_teleporter_dest") == 0;
}
static void bad_area_touch_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_touch_contact contact) {
  (void)ctx; (void)entity; (void)game; (void)contact;
}

qa_q2f_entity *qa_q2mp_mines_spawn_bad_area(qa_q2mp_mines *m, qa_q2f_game *game, qa_vec3 min, qa_vec3 max,
                                            double lifespan, bool has_owner, qa_actor_id owner) {
  game->source_callbacks.register_defs(game->source_callbacks.ctx, qa_q2mp_mines_callbacks(m));
  qa_q2f_entity *area = game->create(game->ctx, "bad_area", NULL);
  qa_vec3 origin = q2mp_scale(q2mp_add(min, max), 0.5);
  area->touch = m->bad_area_touch;
  area->owner = owner; area->has_owner = has_owner;
  qa_q2f_body_changes c = q2mp_changes();
  c.has_origin = true; c.origin = origin;
  c.has_bounds = true; c.bounds = (qa_bounds){q2mp_sub(min, origin), q2mp_sub(max, origin)};
  q2mp_move(game, area, c, false, false);
  game->solid(game->ctx, area, QA_Q2F_SOLID_TRIGGER);
  game->motion(game->ctx, area, QA_Q2F_MOTION_STATIONARY);
  if (lifespan != 0) game->schedule(game->ctx, area, lifespan, qa_q2f_free_entity_think());
  return area;
}
qa_q2f_entity *qa_q2mp_mines_bad_area_entity(qa_q2mp_mines *m, qa_actor_id actor, qa_q2f_game *game, const qa_vec3 *origin) {
  qa_body_state_result body = q2mp_bread(game, actor);
  if (!body.has_value) return NULL;
  qa_vec3 base = origin != NULL ? *origin : body.value.origin;
  qa_vec3 min = q2mp_add(base, body.value.bounds.min), max = q2mp_add(base, body.value.bounds.max);
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q2f_entity *area = game->entities[i];
    if (!q2mp_touch_eq(area->touch, m->bad_area_touch) || area->solid != QA_Q2F_SOLID_TRIGGER) continue;
    qa_body_state other = q2mp_body(game, area);
    qa_vec3 area_min = q2mp_add(other.origin, other.bounds.min), area_max = q2mp_add(other.origin, other.bounds.max);
    if (min.x <= area_max.x && max.x >= area_min.x && min.y <= area_max.y && max.y >= area_min.y &&
        min.z <= area_max.z && max.z >= area_min.z) return area;
  }
  return NULL;
}
bool qa_q2mp_mines_bad_area(qa_q2mp_mines *m, qa_actor_id actor, qa_q2f_game *game) {
  return qa_q2mp_mines_bad_area_entity(m, actor, game, NULL) != NULL;
}
bool qa_q2mp_mines_mark_tesla_area(qa_q2mp_mines *m, qa_owned_actor self, qa_q2f_game *game, qa_q2f_entity *tesla) {
  if (!q2mp_live(game, self.id) || !q2mp_live(game, tesla->actor.id)) return false;
  qa_q2f_entity *tail = tesla;
  qa_q2f_entity *next = q2mp_ent(game, tesla->has_team_chain, tesla->team_chain);
  while (next != NULL) {
    if (strcmp(next->classname, "bad_area") == 0) return false;
    tail = next;
    next = q2mp_ent(game, next->has_team_chain, next->team_chain);
  }
  qa_q2f_entity *trigger = q2mp_ent(game, tesla->has_team_chain, tesla->team_chain);
  qa_bounds bounds = trigger == NULL
      ? (qa_bounds){q2mp_v3(-128, -128, q2mp_body(game, tesla).bounds.min.z), q2mp_v3(128, 128, 128)}
      : q2mp_body(game, trigger).bounds;
  qa_vec3 origin = trigger == NULL ? q2mp_zero() : q2mp_body(game, trigger).origin;
  double lifespan = trigger == NULL ? 30 : tesla->timestamp != 0 ? tesla->timestamp : tesla->has_next_think ? tesla->next_think : 0;
  qa_q2f_entity *area = qa_q2mp_mines_spawn_bad_area(m, game, q2mp_add(origin, bounds.min), q2mp_add(origin, bounds.max),
                                                     lifespan, true, tesla->actor.id);
  tail->team_chain = area->actor.id; tail->has_team_chain = true;
  return true;
}

qa_q2f_entity *qa_q2mp_mines_throw_mine(qa_q2mp_mines *m, qa_owned_actor self, qa_q2f_game *game, const char *classname,
                                        qa_vec3 start, qa_vec3 direction, double speed) {
  game->source_callbacks.register_defs(game->source_callbacks.ctx, qa_q2mp_mines_callbacks(m));
  const char *model = strcmp(classname, "prox") == 0 ? "models/weapons/g_prox/tris.md2"
      : strcmp(classname, "tesla") == 0 ? "models/weapons/g_tesla/tris.md2"
      : strcmp(classname, "nuke") == 0 ? "models/weapons/g_nuke/tris.md2" : "models/weapons/g_trap/tris.md2";
  qa_q2f_entity *mine = qa_q2mp_projectile(self, game, classname, start, direction, speed, model, QA_Q2F_MOTION_BOUNCE, 32);
  qa_angle_vectors axes = q2mp_axes(q2mp_vta(direction));
  double gravity = 1;
  if (q2mp_rr(game) && strcmp(classname, "nuke") != 0) {
    double g = 800;
    if (m->base.hooks.gravity != NULL) g = m->base.hooks.gravity(m->base.hooks.gravity_ctx);
    else {
      const qa_q2w_input *input = qa_q2w_ballistics_input(m->base.hooks.base, self.id);
      if (input != NULL) g = input->gravity;
    }
    gravity = g / 800;
  }
  qa_q2f_body_changes c = q2mp_changes();
  c.has_velocity = true;
  c.velocity = q2mp_add(q2mp_add(q2mp_scale(direction, speed),
                                 q2mp_scale(axes.up, (200 + (q2mp_rnd(game) * 2 - 1) * 10) * gravity)),
                        q2mp_scale(axes.right, (q2mp_rnd(game) * 2 - 1) * 10));
  q2mp_move(game, mine, c, true, false);
  if (strcmp(classname, "nuke") != 0) qa_q2mp_bolts_player_collision(&m->base, self, game, mine);
  mine->render_flags = 0x8000; mine->team_master = self.id; mine->has_team_master = true; mine->damageable_target = true;
  return mine;
}
void qa_q2mp_mines_grenade_effect(qa_q2mp_mines *m, qa_q2f_entity *entity, qa_q2f_game *game) {
  (void)m;
  qa_body_state body = q2mp_body(game, entity);
  bool wet = ((q2mp_u32(game->host->point_contents(game->host->ctx, body.origin)) & 56) != 0);
  qa_q2f_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = QA_Q2F_EVENT_EFFECT;
  e.effect = body.ground == NULL ? (wet ? "q2:rocket_explosion_water" : "q2:rocket_explosion")
                                  : (wet ? "q2:grenade_explosion_water" : "q2:grenade_explosion");
  e.origin = q2mp_add(body.origin, q2mp_scale(body.velocity, -0.02));
  e.direction = q2mp_zero(); e.count = 0; e.color = 0;
  game->host->emit(game->host->ctx, e);
}

static void prox_explode_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_mines *m = ctx;
  qa_q2f_entity *field = q2mp_ent(game, entity->has_team_chain, entity->team_chain);
  if (field != NULL && field->has_owner && qa_same_actor(field->owner, entity->actor.id)) game->remove(game->ctx, field);
  qa_actor_id owner = entity->has_team_master && q2mp_live(game, entity->team_master) ? entity->team_master : entity->actor.id;
  if (entity->has_team_master)
    qa_q2w_ballistics_player_noise_for_actor(m->base.hooks.base, entity->team_master, game, q2mp_body(game, entity).origin, "impact");
  if (entity->damage > 90) q2mp_sound(game, entity, "items/damage3.wav", true, 3, false, 0, false, 0);
  if (q2mp_cread(game, entity->actor.id).has_value) q2mp_traits_hurt(game, entity->actor, false);
  q2mp_radius(game, entity, &owner, entity->damage, &entity->actor.id, 192, QA_Q2MP_MOD_PROX, true, 0, "q2:weapon_proxlauncher");
  qa_q2mp_mines_grenade_effect(m, entity, game);
  game->remove(game->ctx, entity);
}
static void prox_die_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_death_reaction reaction) {
  qa_q2mp_mines *m = ctx;
  q2mp_traits_hurt(game, entity->actor, false);
  qa_q2f_entity *inflictor = q2mp_ent(game, reaction.inflictor != NULL, reaction.inflictor ? *reaction.inflictor : entity->actor.id);
  if (inflictor != NULL && strcmp(inflictor->classname, q2mp_rr(game) ? "prox_mine" : "prox") == 0)
    game->schedule(game->ctx, entity, q2mp_frame(game), m->prox_explode);
  else prox_explode_fn(ctx, entity, game);
}
static void prox_field_fn(void *ctx, qa_q2f_entity *field, qa_q2f_game *game, qa_touch_contact contact) {
  qa_q2mp_mines *m = ctx;
  if (!game->host->is_monster(game->host->ctx, contact.other) && !game->host->is_player(game->host->ctx, contact.other)) return;
  qa_q2f_entity *mine = q2mp_ent(game, field->has_owner, field->owner);
  if (mine == NULL) { game->remove(game->ctx, field); return; }
  if (q2mp_rr(game) && (!m->base.hooks.base->hooks.can_target(m->base.hooks.base->hooks.ctx,
          mine->has_team_master ? &mine->team_master : NULL, contact.other) ||
        (!q2mp_dm(game) && game->host->is_player(game->host->ctx, contact.other)))) return;
  if (qa_same_actor(contact.other, mine->actor.id) || q2mp_think_is(mine->think, m->prox_explode)) return;
  if (!mine->has_team_chain || !qa_same_actor(mine->team_chain, field->actor.id)) { game->remove(game->ctx, field); return; }
  q2mp_sound(game, field, "weapons/proxwarn.wav", true, 2, false, 0, false, 0);
  game->schedule(game->ctx, mine, 0.5, m->prox_explode);
}
static void prox_seek_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_mines *m = ctx;
  if (q2mp_now(game) > entity->wait) { prox_explode_fn(ctx, entity, game); return; }
  if (++entity->frame > 13) entity->frame = 9;
  game->show(game->ctx, entity);
  game->schedule(game->ctx, entity, 0.1, m->prox_seek);
}
static void prox_open_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_mines *m = ctx;
  if (entity->frame != 9) {
    if (entity->frame == 0) q2mp_sound(game, entity, "weapons/proxopen.wav", true, 2, false, 0, false, 0);
    entity->frame++;
    game->show(game->ctx, entity);
    game->schedule(game->ctx, entity, q2mp_rr(game) ? 0.1 : 0.05, m->prox_open);
    return;
  }
  if (!q2mp_rr(game) || q2mp_dm(game)) entity->has_owner = false;
  game->motion(game->ctx, entity, entity->motion);
  qa_q2f_entity *field = q2mp_ent(game, entity->has_team_chain, entity->team_chain);
  if (field != NULL) field->touch = m->prox_field;
  size_t n = 0;
  const qa_actor_id *actors = game->host->nearby(game->host->ctx, q2mp_body(game, entity).origin, 202, &n);
  for (size_t i = 0; i < n; i++) {
    qa_actor_id actor = actors[i];
    qa_q2f_entity *target = q2mp_ent(game, true, actor);
    bool rr = q2mp_rr(game);
    if (rr && (qa_same_actor(actor, entity->actor.id) ||
        !m->base.hooks.base->hooks.can_target(m->base.hooks.base->hooks.ctx,
            entity->has_team_master ? &entity->team_master : NULL, actor))) continue;
    bool living = (game->host->is_monster(game->host->ctx, actor) ||
        ((game->host->is_player(game->host->ctx, actor) || (rr && target != NULL && strcmp(target->classname, "prox_mine") == 0)) &&
        (!rr || q2mp_dm(game)))) && q2mp_health(game, actor) > 0;
    if (!living && !(q2mp_dm(game) && target != NULL && player_start(target, game))) continue;
    bool seen = target != NULL ? qa_q2mp_sight(game, target, entity->actor.id) : qa_q2mp_sight(game, entity, actor);
    if (!seen) continue;
    q2mp_sound(game, entity, "weapons/proxwarn.wav", true, 2, false, 0, false, 0);
    prox_explode_fn(ctx, entity, game);
    return;
  }
  entity->wait = q2mp_now(game) + (m->base.hooks.strong_mines ? 45 : mine_life(entity->damage / 90));
  game->schedule(game->ctx, entity, 0.2, m->prox_seek);
}
static void prox_land_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_touch_contact contact) {
  qa_q2mp_mines *m = ctx;
  if ((q2mp_u32(q2mp_surf(&contact)) & 4) != 0) { game->remove(game->ctx, entity); return; }
  bool has_normal = contact.plane != NULL;
  qa_vec3 normal = q2mp_cnormal(&contact);
  if (has_normal && ((q2mp_u32(game->host->point_contents(game->host->ctx,
        q2mp_add(q2mp_body(game, entity).origin, q2mp_scale(normal, -10)))) & 24) != 0)) {
    prox_explode_fn(ctx, entity, game);
    return;
  }
  qa_q2f_entity *other = q2mp_ent(game, true, contact.other);
  if (game->host->is_monster(game->host->ctx, contact.other) || game->host->is_player(game->host->ctx, contact.other) ||
      (other != NULL && other->damageable_target)) {
    if (entity->has_team_master && qa_same_actor(contact.other, entity->team_master)) return;
    prox_explode_fn(ctx, entity, game);
    return;
  }
  qa_q2f_motion_kind motion = QA_Q2F_MOTION_STATIONARY;
  if (!qa_same_actor(contact.other, game->host->world_actor(game->host->ctx))) {
    if (!has_normal) { prox_explode_fn(ctx, entity, game); return; }
    qa_vec3 v = q2mp_body(game, entity).velocity;
    qa_vec3 out = q2mp_sub(v, q2mp_scale(normal, q2mp_dot(v, normal) * 1.5));
    if (out.z > 60) return;
    if (other == NULL || other->motion != QA_Q2F_MOTION_PUSH || normal.z <= 0.7) {
      if (normal.z > 0.7) prox_explode_fn(ctx, entity, game);
      return;
    }
    motion = QA_Q2F_MOTION_BOUNCE;
  }
  if (!has_normal) { prox_explode_fn(ctx, entity, game); return; }
  if (((q2mp_u32(game->host->point_contents(game->host->ctx, q2mp_body(game, entity).origin)) & 24) != 0)) {
    prox_explode_fn(ctx, entity, game);
    return;
  }
  qa_q2f_entity *field = game->create(game->ctx, "prox_field", NULL);
  field->owner = entity->actor.id; field->has_owner = true;
  field->team_master = entity->actor.id; field->has_team_master = true;
  qa_q2f_body_changes fc = q2mp_changes();
  fc.has_origin = true; fc.origin = q2mp_body(game, entity).origin;
  fc.has_bounds = true; fc.bounds = (qa_bounds){q2mp_v3(-96, -96, -96), q2mp_v3(96, 96, 96)};
  q2mp_move(game, field, fc, false, false);
  game->solid(game->ctx, field, QA_Q2F_SOLID_TRIGGER);
  game->motion(game->ctx, field, QA_Q2F_MOTION_STATIONARY);
  qa_vec3 angles = q2mp_vta(normal);
  entity->angular_velocity = q2mp_zero();
  qa_q2f_body_changes c = q2mp_changes();
  c.has_velocity = true; c.velocity = q2mp_zero();
  c.has_angles = true; c.angles = q2mp_v3(angles.x + 90, angles.y, angles.z);
  q2mp_move(game, entity, c, false, false);
  entity->die = m->prox_die;
  entity->team_chain = field->actor.id; entity->has_team_chain = true;
  entity->touch = (qa_q2f_touch){NULL, NULL};
  q2mp_combat_create(game, entity->actor, 20, 0, true);
  if (q2mp_rr(game)) entity->projectile = false;
  game->motion(game->ctx, entity, motion);
  game->schedule(game->ctx, entity, q2mp_rr(game) ? 0 : 0.05, m->prox_open);
}
static void prox_flight_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_mines *m = ctx;
  if (entity->timestamp <= q2mp_now(game)) { prox_explode_fn(ctx, entity, game); return; }
  qa_vec3 angles = q2mp_vta(q2mp_body(game, entity).velocity);
  qa_q2f_body_changes c = q2mp_changes();
  c.has_angles = true; c.angles = q2mp_v3(angles.x - 90, angles.y, angles.z);
  q2mp_move(game, entity, c, false, false);
  game->schedule(game->ctx, entity, 0, m->prox_flight);
}
qa_q2f_entity *qa_q2mp_mines_fire_prox(qa_q2mp_mines *m, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start,
                                       qa_vec3 direction, double multiplier, double speed) {
  qa_q2f_entity *mine = qa_q2mp_mines_throw_mine(m, self, game, "prox", start, direction, speed);
  qa_vec3 angles = q2mp_body(game, mine).angles;
  mine->clip_mask = (double)(q2mp_u32(mine->clip_mask) | 24u);
  mine->flags = (double)(q2mp_u32(mine->flags) | 0x2000u) + (mine->flags >= 4294967296.0 ? 4294967296.0 : 0);
  mine->damage = 90 * multiplier;
  mine->touch = m->prox_land;
  qa_q2f_body_changes c = q2mp_changes();
  c.has_angles = true; c.angles = q2mp_v3(angles.x - 90, angles.y, angles.z);
  c.has_bounds = true; c.bounds = (qa_bounds){q2mp_v3(-6, -6, -6), q2mp_v3(6, 6, 6)};
  q2mp_move(game, mine, c, true, false);
  if (q2mp_rr(game)) {
    mine->classname = "prox_mine";
    mine->flags = (double)(q2mp_u32(mine->flags) | 0x20000u) + 4294967296.0;
    mine->timestamp = q2mp_now(game) + mine_life(multiplier);
    game->schedule(game->ctx, mine, 0, m->prox_flight);
  } else game->schedule(game->ctx, mine, mine_life(multiplier), m->prox_explode);
  qa_q2mp_publish_projectile(mine, game, "", true, (qa_q2mp_projectile_behavior){"q2:weapon_proxlauncher", "grenade"});
  return mine;
}

static void remove_tesla(qa_q2mp_mines *m, qa_q2f_entity *entity, qa_q2f_game *game, bool blow) {
  q2mp_traits_hurt(game, entity->actor, false);
  qa_q2f_entity *child = q2mp_ent(game, entity->has_team_chain, entity->team_chain);
  while (child != NULL) {
    qa_q2f_entity *next = q2mp_ent(game, child->has_team_chain, child->team_chain);
    game->remove(game->ctx, child);
    child = next;
  }
  entity->owner = entity->team_master; entity->has_owner = entity->has_team_master;
  entity->has_enemy = false;
  if (blow) { entity->damage *= 50; entity->damage_radius = 200; }
  if (entity->damage_radius != 0 && entity->damage > 150) q2mp_sound(game, entity, "items/damage3.wav", true, 3, false, 0, false, 0);
  if (entity->has_owner)
    qa_q2w_ballistics_player_noise_for_actor(m->base.hooks.base, entity->owner, game, q2mp_body(game, entity).origin, "impact");
  q2mp_radius(game, entity, entity->has_owner ? &entity->owner : NULL, entity->damage, NULL, entity->damage_radius, 7,
              false, 0, NULL);
  qa_q2mp_mines_grenade_effect(m, entity, game);
  game->remove(game->ctx, entity);
}
static void tesla_die_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_death_reaction reaction) {
  (void)reaction;
  remove_tesla(ctx, entity, game, false);
}
static void tesla_lava_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_touch_contact contact) {
  if (contact.plane != NULL && ((q2mp_u32(game->host->point_contents(game->host->ctx,
        q2mp_add(q2mp_body(game, entity).origin, q2mp_scale(contact.plane->normal, -20)))) & 24) != 0)) {
    remove_tesla(ctx, entity, game, true);
    return;
  }
  q2mp_sound(game, entity, q2mp_rnd(game) > 0.5 ? "weapons/hgrenb1a.wav" : "weapons/hgrenb2a.wav", true, 2, false, 0, false, 0);
}
static void tesla_open_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_mines *m = ctx;
  if (((q2mp_u32(game->host->point_contents(game->host->ctx, q2mp_body(game, entity).origin)) & 24) != 0)) {
    remove_tesla(m, entity, game, false);
    return;
  }
  qa_q2f_body_changes c = q2mp_changes();
  c.has_angles = true; c.angles = q2mp_zero();
  q2mp_move(game, entity, c, false, false);
  if (entity->frame == 0) q2mp_sound(game, entity, "weapons/teslaopen.wav", true, 2, false, 0, false, 0);
  entity->frame++;
  if (entity->frame > 14) {
    entity->frame = 14;
    game->schedule(game->ctx, entity, 0.1, m->tesla_activate);
    return;
  }
  if (entity->frame == 10) {
    if (entity->has_owner)
      qa_q2w_ballistics_player_noise_for_actor(m->base.hooks.base, entity->owner, game, q2mp_body(game, entity).origin, "weapon");
    entity->skin = 1;
  } else if (entity->frame == 12) entity->skin = 2;
  else if (entity->frame == 14) entity->skin = 3;
  game->show(game->ctx, entity);
  game->schedule(game->ctx, entity, 0.1, m->tesla_open);
}
static void tesla_activate_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_mines *m = ctx;
  if (((q2mp_u32(game->host->point_contents(game->host->ctx, q2mp_body(game, entity).origin)) & 56) != 0)) {
    remove_tesla(m, entity, game, true);
    return;
  }
  if (q2mp_dm(game)) {
    size_t n = 0;
    const qa_actor_id *actors = game->host->nearby(game->host->ctx, q2mp_body(game, entity).origin, 192, &n);
    for (size_t i = 0; i < n; i++) {
      qa_q2f_entity *other = q2mp_ent(game, true, actors[i]);
      if (other != NULL && player_start(other, game) && qa_q2mp_sight(game, other, entity->actor.id)) {
        remove_tesla(m, entity, game, false);
        return;
      }
    }
  }
  qa_q2f_entity *field = game->create(game->ctx, "tesla trigger", NULL);
  field->owner = entity->actor.id; field->has_owner = true;
  qa_q2f_body_changes fc = q2mp_changes();
  fc.has_origin = true; fc.origin = q2mp_body(game, entity).origin;
  fc.has_bounds = true;
  fc.bounds = (qa_bounds){q2mp_v3(-128, -128, q2mp_body(game, entity).bounds.min.z), q2mp_v3(128, 128, 128)};
  q2mp_move(game, field, fc, false, false);
  game->solid(game->ctx, field, QA_Q2F_SOLID_TRIGGER);
  game->motion(game->ctx, field, QA_Q2F_MOTION_STATIONARY);
  qa_q2f_body_changes c = q2mp_changes();
  c.has_angles = true; c.angles = q2mp_zero();
  q2mp_move(game, entity, c, false, false);
  if (q2mp_dm(game)) { entity->has_owner = false; game->motion(game->ctx, entity, entity->motion); }
  entity->team_chain = field->actor.id; entity->has_team_chain = true;
  entity->timestamp = q2mp_now(game) + 30;
  game->schedule(game->ctx, entity, q2mp_rr(game) ? 0.1 : q2mp_frame(game), m->tesla_active);
}
static void tesla_active_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_mines *m = ctx;
  if (q2mp_now(game) > entity->timestamp) { remove_tesla(m, entity, game, false); return; }
  qa_q2f_entity *field = q2mp_ent(game, entity->has_team_chain, entity->team_chain);
  if (field == NULL) qa_q2f_error("Active source Tesla has no trigger field");
  qa_body_state box = q2mp_body(game, field);
  qa_vec3 min = q2mp_add(box.origin, box.bounds.min), max = q2mp_add(box.origin, box.bounds.max);
  qa_vec3 start = q2mp_add(q2mp_body(game, entity).origin, q2mp_v3(0, 0, 16));
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_actor_id actor = game->entities[i]->actor.id;
    qa_body_state_result body = q2mp_bread(game, actor);
    qa_q2f_entity *target = q2mp_ent(game, true, actor);
    if (!q2mp_live(game, entity->actor.id)) return;
    if (!body.has_value || qa_same_actor(actor, entity->actor.id) || q2mp_health(game, actor) < 1) continue;
    if (game->host->is_player(game->host->ctx, actor) && !q2mp_dm(game)) continue;
    if (q2mp_rr(game)) {
      if (game->host->is_player(game->host->ctx, actor) && entity->has_team_master &&
          !m->base.hooks.base->hooks.can_target(m->base.hooks.base->hooks.ctx, &entity->team_master, actor)) continue;
      if (!q2mp_dm(game) && target != NULL && (int64_t)trunc(target->flags / 4294967296.0) % 2 != 0) continue;
    }
    if (!game->host->is_player(game->host->ctx, actor) && !game->host->is_monster(game->host->ctx, actor) &&
        !(target != NULL && target->damageable_target)) continue;
    if (body.value.origin.x + body.value.bounds.max.x < min.x || body.value.origin.x + body.value.bounds.min.x > max.x ||
        body.value.origin.y + body.value.bounds.max.y < min.y || body.value.origin.y + body.value.bounds.min.y > max.y ||
        body.value.origin.z + body.value.bounds.max.z < min.z || body.value.origin.z + body.value.bounds.min.z > max.z) continue;
    qa_trace_result trace = q2mp_trace(game, start, body.value.origin, NULL, &entity->actor.id, qa_q2mp_projectile_mask(game));
    if (trace.fraction != 1 && !(trace.hit.kind == QA_TRACE_HIT_ACTOR && qa_same_actor(trace.hit.actor, actor))) continue;
    if (entity->damage > 3) q2mp_sound(game, entity, "items/damage3.wav", true, 3, false, 0, false, 0);
    q2mp_damage(game, actor, entity, entity->actor.id, entity->has_team_master ? &entity->team_master : NULL, entity->damage,
                game->host->is_monster(game->host->ctx, actor) && ((q2mp_u32(target ? target->flags : 0) & 3) == 0) ? 0 : 8,
                q2mp_sub(body.value.origin, start), trace.end, q2mp_normal(trace), QA_Q2MP_MOD_TESLA, false, 0, NULL);
    qa_q2w_event ev;
    memset(&ev, 0, sizeof ev);
    ev.kind = QA_Q2W_EVENT_BEAM; ev.effect = "bfg-lightning"; ev.actor = &entity->actor.id;
    ev.start = start; ev.end = trace.end; ev.duration = q2mp_frame(game);
    m->base.hooks.base->hooks.emit(m->base.hooks.base->hooks.ctx, ev);
  }
  game->schedule(game->ctx, entity, q2mp_rr(game) ? 0.1 : q2mp_frame(game), m->tesla_active);
}
qa_q2f_entity *qa_q2mp_mines_fire_tesla(qa_q2mp_mines *m, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start,
                                        qa_vec3 direction, double multiplier, double speed) {
  qa_q2f_entity *mine = qa_q2mp_mines_throw_mine(m, self, game, "tesla", start, direction, speed);
  mine->damage = 3 * multiplier;
  mine->clip_mask = (double)(q2mp_u32(mine->clip_mask) | 24u);
  mine->flags = (double)(q2mp_u32(mine->flags) | 0x2000u) + (mine->flags >= 4294967296.0 ? 4294967296.0 : 0);
  mine->touch = m->tesla_lava;
  mine->die = m->tesla_die;
  if (q2mp_rr(game)) {
    mine->classname = "tesla_mine";
    mine->clip_mask = (double)(q2mp_u32(mine->clip_mask) & ~0x4000000u);
    mine->flags = (double)(q2mp_u32(mine->flags) | 0x20000u) + 4294967296.0;
  }
  qa_q2f_body_changes c = q2mp_changes();
  c.has_angles = true; c.angles = q2mp_zero();
  c.has_bounds = true; c.bounds = (qa_bounds){q2mp_v3(-12, -12, 0), q2mp_v3(12, 12, 20)};
  q2mp_move(game, mine, c, true, false);
  q2mp_combat_create(game, mine->actor, q2mp_dm(game) ? 20 : q2mp_rr(game) ? 50 : 30, 0, true);
  mine->wait = q2mp_now(game) + 30;
  game->schedule(game->ctx, mine, 3, m->tesla_open);
  qa_q2mp_publish_projectile(mine, game, "", true, (qa_q2mp_projectile_behavior){"q2:ammo_tesla", "grenade"});
  return mine;
}

void qa_q2mp_mines_init(qa_q2mp_mines *m, qa_q2mp_projectile_hooks hooks) {
  /* bolts_init memsets only its own struct; fields after base are set here. */
  qa_q2mp_bolts_init(&m->base, hooks);
  m->prox_explode = (qa_q2f_think){m, prox_explode_fn};
  m->prox_open = (qa_q2f_think){m, prox_open_fn};
  m->prox_seek = (qa_q2f_think){m, prox_seek_fn};
  m->prox_flight = (qa_q2f_think){m, prox_flight_fn};
  m->tesla_open = (qa_q2f_think){m, tesla_open_fn};
  m->tesla_activate = (qa_q2f_think){m, tesla_activate_fn};
  m->tesla_active = (qa_q2f_think){m, tesla_active_fn};
  m->prox_land = (qa_q2f_touch){m, prox_land_fn};
  m->prox_field = (qa_q2f_touch){m, prox_field_fn};
  m->tesla_lava = (qa_q2f_touch){m, tesla_lava_fn};
  m->bad_area_touch = (qa_q2f_touch){m, bad_area_touch_fn};
  m->prox_die = (qa_q2f_die){m, prox_die_fn};
  m->tesla_die = (qa_q2f_die){m, tesla_die_fn};
  memcpy(m->cb_think, m->base.cb_think, sizeof m->base.cb_think);
  m->cb_think[6] = (qa_q2f_named_think){"Prox_Explode", m->prox_explode};
  m->cb_think[7] = (qa_q2f_named_think){"prox_open", m->prox_open};
  m->cb_think[8] = (qa_q2f_named_think){"prox_seek", m->prox_seek};
  m->cb_think[9] = (qa_q2f_named_think){"Prox_Think", m->prox_flight};
  m->cb_think[10] = (qa_q2f_named_think){"tesla_think", m->tesla_open};
  m->cb_think[11] = (qa_q2f_named_think){"tesla_activate", m->tesla_activate};
  m->cb_think[12] = (qa_q2f_named_think){"tesla_think_active", m->tesla_active};
  memcpy(m->cb_touch, m->base.cb_touch, sizeof m->base.cb_touch);
  m->cb_touch[5] = (qa_q2f_named_touch){"prox_land", m->prox_land};
  m->cb_touch[6] = (qa_q2f_named_touch){"Prox_Field_Touch", m->prox_field};
  m->cb_touch[7] = (qa_q2f_named_touch){"tesla_lava", m->tesla_lava};
  m->cb_touch[8] = (qa_q2f_named_touch){"badarea_touch", m->bad_area_touch};
  m->cb_die[0] = (qa_q2f_named_die){"prox_die", m->prox_die};
  m->cb_die[1] = (qa_q2f_named_die){"tesla_die", m->tesla_die};
}
qa_q2f_callback_definitions qa_q2mp_mines_callbacks(qa_q2mp_mines *m) {
  qa_q2f_callback_definitions defs;
  memset(&defs, 0, sizeof defs);
  defs.trajectory = m->base.cb_traj; defs.trajectory_count = 4;
  defs.think = m->cb_think; defs.think_count = 13;
  defs.touch = m->cb_touch; defs.touch_count = 9;
  defs.die = m->cb_die; defs.die_count = 2;
  return defs;
}

