/* Port of src/content/q2/missionpacks/projectiles/trap.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/projectiles/trap.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../missionpacks_internal.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/missionpacks/projectiles/common.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void trap_die_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2mp_explode_default(entity, game);
}
static qa_q2f_entity *spawn_food(qa_q2f_game *game, qa_q2f_entity *entity, qa_vec3 origin) {
  qa_combat_state_result combat = q2mp_cread(game, entity->actor.id);
  char count[64], origin_text[160];
  (void)snprintf(count, sizeof count, "%g", combat.has_value ? combat.value.mass : 0);
  (void)snprintf(origin_text, sizeof origin_text, "%g %g %g", origin.x, origin.y, origin.z);
  const char *keys[3] = {"count", "spawnflags", "origin"};
  const char *values[3] = {count, "65536", origin_text};
  qa_q2f_spawn_fields fields;
  memset(&fields, 0, sizeof fields);
  fields.ordinal = -1; fields.classname = "item_foodcube"; fields.keys = keys; fields.values = values; fields.count = 3;
  return game->spawn(game->ctx, fields);
}

static void trap_gib_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_projectiles *p = ctx;
  qa_q2f_entity *trap = q2mp_ent(game, entity->has_owner, entity->owner);
  if (trap == NULL || trap->frame != 5) { game->remove(game->ctx, entity); return; }
  qa_body_state body = q2mp_body(game, entity);
  qa_vec3 origin = q2mp_body(game, trap).origin;
  qa_angle_vectors axes = q2mp_axes(q2mp_body(game, trap).angles);
  double degrees = 150 * q2mp_frame(game) + trap->delay, radians = degrees * M_PI / 180;
  qa_vec3 delta = q2mp_sub(origin, body.origin);
  qa_vec3 rotated = q2mp_add(q2mp_add(q2mp_scale(delta, cos(radians)),
                                      q2mp_scale(qa_cross3_double(axes.up, delta), sin(radians))),
                             q2mp_scale(axes.up, q2mp_dot(axes.up, delta) * (1 - cos(radians))));
  qa_trace_result trace = q2mp_trace(game, body.origin, q2mp_sub(origin, rotated), NULL, &entity->actor.id, 3);
  qa_q2f_body_changes c = q2mp_changes();
  c.has_origin = true;
  c.origin = q2mp_add(trace.end, q2mp_scale(q2mp_norm(delta), 15 * q2mp_frame(game)));
  c.has_angles = true; c.angles = q2mp_v3(body.angles.x, body.angles.y + degrees, body.angles.z);
  q2mp_move(game, entity, c, false, false);
  game->schedule(game->ctx, entity, q2mp_frame(game), p->trap_gib);
}

static void trap_rerelease_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_projectiles *p = ctx;
  double now = q2mp_now(game);
  qa_body_state body = q2mp_body(game, entity);
  if (entity->timestamp < now) { qa_q2mp_explode_default(entity, game); return; }
  game->schedule(game->ctx, entity, 0.1, p->trap_rerelease);
  if (body.ground == NULL) return;
  if (entity->frame > 4) {
    if (entity->frame == 5) {
      if (entity->wait == 64) q2mp_sound(game, entity, "weapons/trapdown.wav", true, 2, true, 1, true, 2);
      entity->wait -= 2; entity->delay += 2;
      if (entity->wait < 19) entity->frame++;
    } else if (++entity->frame == 8) {
      game->schedule(game->ctx, entity, 1, qa_q2mp_free_projectile_think());
      entity->effects = (double)(q2mp_u32(entity->effects) & ~0x2000000u);
      double size = 1 + (entity->accel - 100) / 300;
      qa_q2f_entity *food = spawn_food(game, entity, q2mp_v3(body.origin.x, body.origin.y, body.origin.z + 24 * size));
      food->scale = size;
      qa_q2f_body_changes c = q2mp_changes();
      c.has_angles = true; c.angles = q2mp_v3(0, q2mp_rnd(game) * 360, 0);
      c.has_velocity = true; c.velocity = q2mp_v3(0, 0, 400);
      q2mp_move(game, food, c, false, false);
      if (food->think.fn != NULL) food->think.fn(food->think.ctx, food, game);
      game->cancel(game->ctx, food);
      game->show(game->ctx, food);
      q2mp_sound(game, food, "misc/fhit3.wav", false, 0, false, 0, false, 0);
    }
    game->show(game->ctx, entity);
    return;
  }
  entity->effects = (double)(q2mp_u32(entity->effects) & ~0x2000000u);
  if (entity->frame >= 4) {
    entity->effects = (double)(q2mp_u32(entity->effects) | 0x2000000u);
    if (q2mp_dm(game)) { entity->has_owner = false; game->motion(game->ctx, entity, entity->motion); }
  } else {
    entity->frame++;
    game->show(game->ctx, entity);
    return;
  }
  bool has_best = false;
  qa_actor_id best = entity->actor.id;
  double nearest = 8000;
  size_t count = 0;
  const qa_actor_id *actors = game->host->nearby(game->host->ctx, body.origin, 256, &count);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = actors[i];
    if (qa_same_actor(actor, entity->actor.id)) continue;
    qa_q2f_entity *target = q2mp_ent(game, true, actor);
    qa_body_state_result target_body = q2mp_bread(game, actor);
    if (q2mp_dm(game) && target != NULL &&
        (strncmp(target->classname, "info_player_", 12) == 0 || strcmp(target->classname, "misc_teleporter_dest") == 0 ||
         strncmp(target->classname, "item_flag_", 10) == 0) && qa_q2mp_sight(game, target, entity->actor.id)) {
      qa_q2mp_explode_default(entity, game);
      return;
    }
    if ((!game->host->is_player(game->host->ctx, actor) && !game->host->is_monster(game->host->ctx, actor)) ||
        (!q2mp_dm(game) && game->host->is_player(game->host->ctx, actor)) ||
        ((!entity->has_team_master || !qa_same_actor(actor, entity->team_master)) &&
            !p->base.base.base.hooks.base->hooks.can_target(p->base.base.base.hooks.base->hooks.ctx,
                entity->has_team_master ? &entity->team_master : NULL, actor)) ||
        q2mp_health(game, actor) <= 0 || !target_body.has_value || !qa_q2mp_sight(game, entity, actor)) continue;
    double distance = q2mp_len(q2mp_sub(body.origin, target_body.value.origin));
    if (!has_best || distance < nearest) { has_best = true; best = actor; nearest = distance; }
  }
  qa_q2f_owned_result owned = has_best ? game->host->actors.resolve_owned(game->host->actors.ctx, best)
                                       : (qa_q2f_owned_result){false, entity->actor};
  qa_body_state_result target_body = has_best ? q2mp_bread(game, best)
      : (qa_body_state_result){false, {q2mp_zero(), q2mp_zero(), q2mp_zero(), {q2mp_zero(), q2mp_zero()}, NULL}};
  if (owned.has_value && target_body.has_value) {
    qa_owned_actor target = owned.value;
    qa_vec3 origin = target_body.value.ground == NULL ? target_body.value.origin
        : q2mp_add(target_body.value.origin, q2mp_v3(0, 0, 1));
    qa_vec3 delta = q2mp_sub(body.origin, origin);
    double distance = q2mp_len(delta);
    qa_body_state next = target_body.value;
    next.origin = origin; next.ground = NULL;
    game->host->bodies.write(game->host->bodies.ctx, target, next);
    double maximum = game->host->is_player(game->host->ctx, target.id) ? 290 : 150;
    double pull = maximum - distance < 64 ? 64 : maximum - distance > maximum ? maximum : maximum - distance;
    qa_q2mp_velocity(game, target.id, q2mp_add(target_body.value.velocity, q2mp_scale(q2mp_norm(delta), pull)), true);
    if (strcmp(entity->sound_text, "weapons/trapsuck.wav") != 0) {
      entity->sound_text = "weapons/trapsuck.wav";
      qa_q2f_presentation_event e;
      memset(&e, 0, sizeof e);
      e.kind = QA_Q2F_EVENT_SOUND; e.actor = &entity->actor.id; e.origin = body.origin; e.path = entity->sound_text;
      e.channel = 0; e.volume = 1; e.attenuation = 1; e.reliable = false; e.loop = "start";
      game->host->emit(game->host->ctx, e);
    }
    if (distance < 48) {
      qa_combat_state_result combat = q2mp_cread(game, target.id);
      double mass = combat.has_value ? combat.value.mass : 0;
      if (mass >= 400) { qa_q2mp_explode_default(entity, game); return; }
      q2mp_traits_hurt(game, entity->actor, false);
      game->solid(game->ctx, entity, QA_Q2F_SOLID_NONE);
      entity->die = (qa_q2f_die){NULL, NULL};
      q2mp_damage(game, target.id, entity, entity->actor.id, entity->has_team_master ? &entity->team_master : NULL,
                  100000, 1, q2mp_zero(), origin, q2mp_zero(), QA_Q2MP_MOD_TRAP, true, 0, "q2:ammo_trap");
      entity->enemy = target.id; entity->has_enemy = true;
      entity->wait = 64; entity->timestamp = now + 30; entity->accel = mass; entity->frame = 5;
      q2mp_traits_mass(game, entity->actor, trunc(mass / (q2mp_dm(game) ? 4 : 10)));
      for (size_t i = 0; i < game->entity_count; i++) {
        qa_q2f_entity *gib = game->entities[i];
        if (strcmp(gib->classname, "gib") == 0 && q2mp_len(q2mp_sub(q2mp_body(game, gib).origin, body.origin)) <= 128) {
          game->motion(game->ctx, gib, QA_Q2F_MOTION_STATIONARY);
          gib->owner = entity->actor.id; gib->has_owner = true;
          trap_gib_fn(p, gib, game);
        }
      }
    }
  }
  game->show(game->ctx, entity);
}

static void trap_think_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_projectiles *p = ctx;
  double now = q2mp_now(game);
  qa_body_state body = q2mp_body(game, entity);
  if (entity->timestamp < now) { qa_q2mp_explode_default(entity, game); return; }
  game->schedule(game->ctx, entity, 0.1, p->trap_think);
  if (body.ground == NULL) return;
  if (entity->frame > 4) {
    if (entity->frame == 5) {
      if (entity->wait == 64) q2mp_sound(game, entity, "weapons/trapdown.wav", true, 2, true, 1, true, 2);
      entity->wait -= 2; entity->delay += now;
      qa_angle_vectors axes = q2mp_axes(body.angles);
      for (int index = 0; index < 3; index++) {
        qa_q2f_entity *gib = game->create(game->ctx, "trap_gib", NULL);
        double radians = (120 * index + entity->delay) * M_PI / 180;
        qa_vec3 rotated = q2mp_add(q2mp_add(q2mp_scale(axes.right, cos(radians)),
                                            q2mp_scale(qa_cross3_double(axes.up, axes.right), sin(radians))),
                                   q2mp_scale(axes.up, q2mp_dot(axes.up, axes.right) * (1 - cos(radians))));
        qa_vec3 point = q2mp_add(q2mp_add(body.origin, q2mp_scale(rotated, 1 + entity->wait / 2)), axes.forward);
        qa_combat_state_result combat = q2mp_cread(game, entity->actor.id);
        gib->model = entity->style == 1 ? "models/objects/gekkgib/torso/tris.md2"
            : (combat.has_value ? combat.value.mass : 0) > 200 ? "models/objects/gibs/chest/tris.md2"
            : "models/objects/gibs/sm_meat/tris.md2";
        gib->effects = (entity->style == 1 ? 26 : 1) | 2;
        gib->server_flags = (double)(q2mp_u32(gib->server_flags) | 4u);
        qa_q2f_body_changes c = q2mp_changes();
        c.has_origin = true; c.origin = q2mp_v3(point.x, point.y, body.origin.z + entity->wait);
        c.has_angles = true; c.angles = body.angles;
        c.has_bounds = true; c.bounds = (qa_bounds){q2mp_zero(), q2mp_zero()};
        q2mp_move(game, gib, c, false, false);
        q2mp_combat_create(game, gib->actor, 0, 0, true);
        game->solid(game->ctx, gib, QA_Q2F_SOLID_NONE);
        game->motion(game->ctx, gib, QA_Q2F_MOTION_TOSS);
        game->show(game->ctx, gib);
        game->schedule(game->ctx, gib, 0.1, qa_q2mp_free_projectile_think());
      }
      if (entity->wait < 19) entity->frame++;
    } else {
      entity->frame++;
      if (entity->frame == 8) {
        game->schedule(game->ctx, entity, 1, qa_q2mp_free_projectile_think());
        qa_q2f_entity *food = spawn_food(game, entity, q2mp_v3(body.origin.x, body.origin.y, body.origin.z + 16));
        food->classname = "foodcube";
        qa_q2f_body_changes c = q2mp_changes();
        c.has_velocity = true; c.velocity = q2mp_v3(0, 0, 400);
        q2mp_move(game, food, c, false, false);
        game->motion(game->ctx, food, QA_Q2F_MOTION_TOSS);
      }
    }
    game->show(game->ctx, entity);
    return;
  }
  entity->effects = (double)(q2mp_u32(entity->effects) & ~0x2000000u);
  if (entity->frame >= 4) {
    entity->effects = (double)(q2mp_u32(entity->effects) | 0x2000000u);
    qa_q2f_body_changes c = q2mp_changes();
    c.has_bounds = true; c.bounds = (qa_bounds){q2mp_zero(), q2mp_zero()};
    q2mp_move(game, entity, c, false, false);
  }
  if (entity->frame < 4) entity->frame++;
  bool has_best = false;
  qa_actor_id best = entity->actor.id;
  double old_length = 8000;
  size_t count = 0;
  const qa_actor_id *actors = game->host->nearby(game->host->ctx, body.origin, 256, &count);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = actors[i];
    if (qa_same_actor(actor, entity->actor.id) ||
        (!game->host->is_player(game->host->ctx, actor) && !game->host->is_monster(game->host->ctx, actor)) ||
        q2mp_health(game, actor) <= 0 || !qa_q2mp_sight(game, entity, actor)) continue;
    if (!has_best) { has_best = true; best = actor; continue; }
    qa_body_state_result target = q2mp_bread(game, actor);
    if (!target.has_value) continue;
    double distance = trunc(q2mp_len(q2mp_sub(body.origin, target.value.origin)));
    if (distance < old_length) { old_length = distance; best = actor; }
  }
  if (has_best) {
    qa_q2f_owned_result owned = game->host->actors.resolve_owned(game->host->actors.ctx, best);
    qa_body_state_result target_body = q2mp_bread(game, best);
    if (owned.has_value && target_body.has_value) {
      qa_vec3 origin = target_body.value.ground == NULL ? target_body.value.origin
          : q2mp_add(target_body.value.origin, q2mp_v3(0, 0, 1));
      qa_body_state next = target_body.value;
      next.origin = origin; next.ground = NULL;
      game->host->bodies.write(game->host->bodies.ctx, owned.value, next);
      qa_vec3 delta = q2mp_sub(body.origin, origin);
      double distance = trunc(q2mp_len(delta));
      if (game->host->is_player(game->host->ctx, best))
        qa_q2mp_velocity(game, best, q2mp_add(target_body.value.velocity, q2mp_scale(q2mp_norm(delta), 250)), true);
      else {
        qa_q2m_context *monster = p->base.base.base.hooks.monster(p->base.base.base.hooks.ctx, best);
        if (monster != NULL) {
          monster->state->ideal_yaw = q2mp_vta(delta).y;
          qa_q2m_change_yaw(monster);
        }
        qa_body_state_result current = q2mp_bread(game, best);
        qa_vec3 angles = current.has_value ? current.value.angles : target_body.value.angles;
        qa_q2mp_velocity(game, best, q2mp_scale(q2mp_axes(angles).forward, 256), true);
      }
      q2mp_sound(game, entity, "weapons/trapsuck.wav", true, 2, true, 1, true, 2);
      if (distance < 32) {
        qa_combat_state_result combat = q2mp_cread(game, best);
        double mass = combat.has_value ? combat.value.mass : 0;
        if (mass >= 400) { qa_q2mp_explode_default(entity, game); return; }
        qa_q2f_entity *best_entity = q2mp_ent(game, true, best);
        entity->style = best_entity != NULL && strcmp(best_entity->classname, "monster_gekk") == 0 ? 1 : 0;
        q2mp_damage(game, best, entity, entity->actor.id, entity->has_owner ? &entity->owner : NULL, 100000, 1,
                    q2mp_zero(), origin, q2mp_zero(), QA_Q2MP_MOD_TRAP, true, 0, "q2:ammo_trap");
        entity->enemy = best; entity->has_enemy = true;
        entity->wait = 64; entity->timestamp = now + 30;
        q2mp_traits_mass(game, entity->actor, trunc(mass / (q2mp_dm(game) ? 4 : 10)));
        entity->frame = 5;
      }
    }
  }
  game->show(game->ctx, entity);
}

qa_q2f_entity *qa_q2mp_projectiles_fire_trap(qa_q2mp_projectiles *p, qa_owned_actor self, qa_q2f_game *game,
                                             qa_vec3 start, qa_vec3 direction, double damage, double speed,
                                             double timer, double radius, bool held) {
  /* TS throwMine registers the subclass (virtual) callback set. */
  game->source_callbacks.register_defs(game->source_callbacks.ctx, qa_q2mp_projectiles_callbacks(p));
  qa_q2f_entity *trap = qa_q2mp_mines_throw_mine(&p->base.base, self, game, "trap", start, direction, speed);
  if (q2mp_rr(game)) {
    qa_angle_vectors axes = q2mp_axes(q2mp_vta(direction));
    qa_body_state body = q2mp_body(game, trap);
    double base_up = q2mp_dot(q2mp_sub(body.velocity, q2mp_scale(direction, speed)), axes.up);
    double gravity = 800;
    if (p->base.base.base.hooks.gravity != NULL) gravity = p->base.base.base.hooks.gravity(p->base.base.base.hooks.gravity_ctx);
    else {
      const qa_q2w_input *input = qa_q2w_ballistics_input(p->base.base.base.hooks.base, self.id);
      if (input != NULL) gravity = input->gravity;
    }
    trap->classname = "food_cube_trap"; trap->model = "models/weapons/z_trap/tris.md2";
    trap->effects = 0; trap->render_flags = 0;
    trap->flags = (double)(0x20000u | 0x2000u) + 4294967296.0;
    trap->die = p->trap_die;
    trap->angular_velocity = q2mp_v3(0, 300, 0);
    trap->clip_mask = (double)(int64_t)0x42004003;
    if (game->host->is_player(game->host->ctx, self.id)) {
      const qa_q2w_input *input = qa_q2w_ballistics_input(p->base.base.base.hooks.base, self.id);
      if (input != NULL && !input->players_collide) trap->clip_mask = (double)(q2mp_u32(trap->clip_mask) & ~0x40000000u);
    }
    trap->timestamp = q2mp_now(game) + 30;
    trap->sound_text = "weapons/traploop.wav";
    q2mp_combat_create(game, trap->actor, 20, 0, true);
    qa_q2f_body_changes c = q2mp_changes();
    c.has_angles = true; c.angles = q2mp_zero();
    c.has_velocity = true; c.velocity = q2mp_add(body.velocity, q2mp_scale(axes.up, base_up * (gravity / 800 - 1)));
    c.has_bounds = true; c.bounds = (qa_bounds){q2mp_v3(-4, -4, 0), q2mp_v3(4, 4, 8)};
    q2mp_move(game, trap, c, true, false);
    game->schedule(game->ctx, trap, 1, p->trap_rerelease);
    qa_q2mp_publish_projectile(trap, game, trap->sound_text, true, (qa_q2mp_projectile_behavior){"q2:ammo_trap", "grenade"});
    return trap;
  }
  trap->classname = "htrap"; trap->model = "models/weapons/z_trap/tris.md2";
  trap->effects = 0; trap->render_flags = 0; trap->damageable_target = false;
  trap->has_team_master = false; trap->damage = damage; trap->damage_radius = radius; trap->spawnflags = held ? 3 : 1;
  trap->angular_velocity = q2mp_v3(0, 300, 0);
  trap->timestamp = q2mp_now(game) + 30;
  qa_q2f_body_changes c = q2mp_changes();
  c.has_angles = true; c.angles = q2mp_zero();
  c.has_bounds = true; c.bounds = (qa_bounds){q2mp_v3(-4, -4, 0), q2mp_v3(4, 4, 8)};
  q2mp_move(game, trap, c, true, false);
  q2mp_combat_create(game, trap->actor, 0, 0, false);
  if (timer <= 0) {
    qa_q2w_ballistics_player_noise(p->base.base.base.hooks.base, self, game, start, "impact");
    q2mp_radius(game, trap, trap->has_owner ? &trap->owner : NULL, trap->damage, NULL, radius, held ? 24 : 16, true, 0,
                "q2:ammo_trap");
    qa_q2mp_mines_grenade_effect(&p->base.base, trap, game);
    game->remove(game->ctx, trap);
  } else {
    game->schedule(game->ctx, trap, 1, p->trap_think);
    qa_q2mp_publish_projectile(trap, game, "weapons/traploop.wav", true, (qa_q2mp_projectile_behavior){"q2:ammo_trap", "grenade"});
  }
  return trap;
}

void qa_q2mp_projectiles_init(qa_q2mp_projectiles *p, qa_q2mp_projectile_hooks hooks) {
  qa_q2mp_nuke_init(&p->base, hooks);
  p->trap_think = (qa_q2f_think){p, trap_think_fn};
  p->trap_rerelease = (qa_q2f_think){p, trap_rerelease_fn};
  p->trap_gib = (qa_q2f_think){p, trap_gib_fn};
  p->trap_die = (qa_q2f_die){p, trap_die_fn};
  memcpy(p->cb_think, p->base.cb_think, sizeof p->base.cb_think);
  p->cb_think[15] = (qa_q2f_named_think){"Trap_Think", p->trap_think};
  p->cb_think[16] = (qa_q2f_named_think){"rerelease/Trap_Think", p->trap_rerelease};
  p->cb_think[17] = (qa_q2f_named_think){"Trap_Gib_Think", p->trap_gib};
  memcpy(p->cb_die, p->base.cb_die, sizeof p->base.cb_die);
  p->cb_die[3] = (qa_q2f_named_die){"trap_die", p->trap_die};
}
qa_q2f_callback_definitions qa_q2mp_projectiles_callbacks(qa_q2mp_projectiles *p) {
  qa_q2f_callback_definitions defs;
  memset(&defs, 0, sizeof defs);
  defs.trajectory = p->base.base.base.cb_traj; defs.trajectory_count = 4;
  defs.think = p->cb_think; defs.think_count = 18;
  defs.touch = p->base.cb_touch; defs.touch_count = 10;
  defs.die = p->cb_die; defs.die_count = 4;
  return defs;
}
