/* Port of src/content/q1/missionpacks/hipnotic-weapons.ts. The TS
 * WeakSet<Q1EntityServices> registration guards become a lookup of the
 * guarded callback name in the game's own registry, which is per game.
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/hipnotic_weapons.h"

#include "missionpacks_internal.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/foundation/weapons.h"

const qa_q1mp_hipnotic_laser_profile qa_q1mp_hipnotic_laser_default = {"hipnotic:laser", 18, 25};

static void emit_weapon(qa_q1_services *game, qa_q1_player_state *player, const char *model, double frame, double punch) {
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "weapon"; event.has_player = true; event.player = player->actor.id; event.weapon = player->weapon;
  event.view_model = model; event.frame = frame; event.punch = punch;
  qa_q1_emit(game, event);
}
static void emit_beam(qa_q1_services *game, const char *style, qa_actor_id actor, qa_vec3 start, qa_vec3 end) {
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "beam"; event.beam_style = style; event.has_actor = true; event.actor = actor; event.start = start; event.end = end;
  qa_q1_emit(game, event);
}
static bool finish(qa_q1_services *game, qa_q1_player_state *player, double delay, double frame, const char *model, double punch) {
  player->attack_finished = qa_q1mp_fround(game->time + qa_q1_weapon_attack_delay(game, player, delay));
  player->next_weapon_frame = player->attack_finished;
  player->weapon_frame = frame; player->hostile_until = qa_q1mp_fround(game->time + 1);
  qa_q1_weapon_punch(game, player, punch);
  emit_weapon(game, player, model, frame, punch);
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
  if (body.has_value) qa_q1_effect(game, "muzzleflash", body.value.origin, &player->actor.id, 1);
  return true;
}

qa_q1_actor *qa_q1mp_launch_hipnotic_laser(qa_q1_services *game, qa_actor_id shooter, qa_vec3 origin, qa_vec3 direction,
                                           bool light, const qa_q1mp_hipnotic_laser_profile *profile) {
  if (profile == NULL) profile = &qa_q1mp_hipnotic_laser_default;
  qa_q1_actor *laser = qa_q1_create(game, "hiplaser", NULL, NULL);
  const qa_vec3 velocity = qa_q1_vscale(qa_q1_normalize(direction), 1000);
  laser->has_owner = true; laser->owner = shooter; laser->has_activator = true; laser->activator = shooter;
  laser->movement = "flymissile"; laser->solid = "bbox";
  qa_q1mp_set_string(&laser->model, "progs/lasrspik.mdl"); laser->effects = light ? 8 : 0; laser->speed = 1000;
  laser->damage = light ? profile->light_damage : profile->damage;
  laser->angular_velocity = QA_Q1MP_V3(0, 0, 400); laser->movedir = velocity;
  laser->attack_finished = qa_q1mp_fround(game->time + 5);
  laser->projectile_weapon = profile->weapon;
  laser->touch = qa_q1_callback_registry_touch(game->named, laser, "hipnotic:laser-touch");
  qa_q1_set_bounds(game, laser, QA_Q1_POINT);
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true; patch.origin = origin; patch.has_velocity = true; patch.velocity = velocity;
  patch.has_angles = true; patch.angles = qa_q1mp_velocity_angles(velocity);
  qa_q1_set_body(game, laser, &patch);
  qa_q1_link(game, laser);
  qa_q1_schedule(game, laser, 0, qa_q1_callback_registry_action(game->named, laser, "hipnotic:laser-think"));
  qa_owned_actor owner;
  if (qa_session_actor_registry_resolve_owned(game->host.actors, shooter, &owner))
    qa_q1_sound(game, owner.id, "hipweap/laserg.wav", "weapon", 1, 1);
  qa_q1_launch_projectile_behavior(game, laser, shooter, profile->weapon, "bolt");
  return laser;
}
bool qa_q1mp_fire_hipnotic_laser(qa_q1_services *game, qa_q1_player_state *player) {
  if (!qa_shared_inventory_table_consume(game->host.inventory, player->actor, "q1:ammo/cells", 1)) return false;
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
  if (!body.has_value) return false;
  const qa_q1_basis basis = qa_q1_make_vectors(game, player->view_angles);
  const qa_vec3 outward = qa_q1_normalize(QA_Q1MP_V3(basis.forward.x, basis.forward.y, 0));
  qa_vec3 origin = qa_q1_vadd(qa_q1_vadd(body.value.origin, qa_q1_vscale(basis.up, 6)), qa_q1_vscale(outward, 12));
  const qa_vec3 direction = qa_q1_aim(game, player->actor, basis.forward);
  const bool paired = !player->continuous_firing || player->weapon_frame == 4;
  if (paired) {
    const double offset = 6 * 0.707;
    origin = qa_q1_vsub(qa_q1_vadd(origin, qa_q1_vscale(basis.right, offset)), qa_q1_vscale(basis.up, offset));
    (void)qa_q1mp_launch_hipnotic_laser(game, player->actor.id, origin, direction, false, NULL);
    (void)qa_q1mp_launch_hipnotic_laser(game, player->actor.id, qa_q1_vsub(origin, qa_q1_vscale(basis.right, offset * 2)), direction, false, NULL);
  } else {
    const qa_vec3 start = qa_q1_vadd(origin, qa_q1_vscale(basis.up, 6));
    const bool light = game->host.random(game->host.ctx) < 0.1;
    (void)qa_q1mp_launch_hipnotic_laser(game, player->actor.id, start, direction, light, NULL);
  }
  player->continuous_firing = true; player->weapon_animation_at = -1;
  return finish(game, player, 0.1, paired ? 1 : 4, "progs/v_laserg.mdl", -1);
}
static void laser_touch(void *ctx, qa_q1_services *game, qa_q1_actor *laser, qa_actor_id other,
                        const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)surface; /* TS laserTouch(game, laser, other, normal) */
  laser->has_owner = false; laser->count++;
  if (qa_q1mp_streq(game->host.contents(game->host.ctx, qa_q1_get_body(game, laser).origin), "sky")) { qa_q1_remove(game, laser); return; }
  const qa_vec3 old_direction = qa_q1_normalize(laser->movedir), origin = qa_q1_get_body(game, laser).origin;
  const qa_actor_id self = laser->actor.id;
  const qa_q1_trace trace = qa_q1mp_trace(game, qa_q1_vsub(origin, qa_q1_vscale(old_direction, 16)),
                                          qa_q1_vadd(origin, qa_q1_vscale(old_direction, 16)), &self, true);
  qa_q1_set_origin(game, laser, trace.end);
  if (qa_q1_health(game, other) != 0) {
    if (laser->has_activator && qa_same_actor(laser->activator, other)) laser->damage = qa_q1mp_fround(laser->damage / 2);
    qa_q1_effect(game, "blood", trace.end, &other, laser->damage);
    (void)qa_q1_damage(game, other, laser->actor.id, laser->has_activator ? &laser->activator : NULL, laser->damage,
                       laser->projectile_weapon, NULL, NULL, NULL);
  } else if (laser->count == 3 || game->host.random(game->host.ctx) < 0.15) qa_q1_effect(game, "gunshot", trace.end, NULL, 1);
  else {
    laser->damage = qa_q1mp_fround(laser->damage * 0.9);
    const qa_vec3 bounce = trace.fraction < 1 ? trace.normal : normal != NULL ? *normal : QA_Q1_ZERO;
    laser->movedir = qa_q1_vscale(qa_q1_normalize(qa_q1_vadd(old_direction, qa_q1_vscale(bounce, 2))), laser->speed);
    qa_q1mp_move_missile(game, laser, laser->movedir);
    (void)game->host.random(game->host.ctx);
    qa_q1_sound(game, laser->actor.id, "hipweap/laserric.wav", "weapon", 3, 1);
    return;
  }
  qa_q1_sound(game, laser->actor.id, "enforcer/enfstop.wav", "weapon", 3, 1);
  qa_q1_remove(game, laser);
}
static void laser_trajectory(void *ctx, qa_q1_services *runtime, qa_q1_actor *laser, qa_weapon_trajectory_update update) {
  (void)ctx; (void)runtime;
  laser->movedir = update.velocity; laser->speed = qa_q1_length(update.velocity);
}
static void laser_think(void *ctx, qa_q1_services *runtime, qa_q1_actor *laser) {
  (void)ctx;
  if (runtime->time > laser->attack_finished) { qa_q1_remove(runtime, laser); return; }
  const qa_weapon_behavior_projectile_port *behavior = runtime->host.weapon_behavior;
  if (!(behavior != NULL && behavior->controls_trajectory(behavior->ctx, laser->actor.id))) qa_q1mp_move_missile(runtime, laser, laser->movedir);
  qa_q1_schedule(runtime, laser, 0.1, qa_q1_callback_registry_action(runtime->named, laser, "hipnotic:laser-think"));
}

qa_q1_actor *qa_q1mp_launch_hipnotic_proximity(qa_q1_services *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 velocity) {
  qa_q1_actor *mine = qa_q1_create(game, "proximity_grenade", NULL, NULL);
  mine->has_owner = true; mine->owner = owner; mine->has_activator = true; mine->activator = owner;
  mine->movement = "toss"; mine->solid = "bbox";
  qa_q1mp_set_string(&mine->model, "progs/proxbomb.mdl"); mine->angular_velocity = QA_Q1MP_V3(100, 600, 100);
  mine->projectile_weapon = "hipnotic:proximity";
  mine->delay = qa_q1mp_fround(game->time + 15 + 10 * game->host.random(game->host.ctx));
  qa_q1_actor_set_damageable(mine, false);
  qa_gameplay_authority_set_health(game->host.combat, mine->actor, 5);
  mine->touch = qa_q1_callback_registry_touch(game->named, mine, "hipnotic:proximity-touch");
  mine->die = qa_q1_callback_registry_die(game->named, mine, "hipnotic:proximity-arm-explosion");
  qa_q1_set_bounds(game, mine, (qa_bounds){{-1, -1, -1}, {1, 1, 1}});
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true; patch.origin = origin; patch.has_velocity = true; patch.velocity = velocity;
  patch.has_angles = true; patch.angles = qa_q1mp_velocity_angles(velocity);
  qa_q1_set_body(game, mine, &patch);
  qa_q1_link(game, mine);
  qa_q1_schedule(game, mine, 2, qa_q1_callback_registry_action(game->named, mine, "hipnotic:proximity-watch"));
  qa_q1_launch_projectile_behavior(game, mine, owner, "hipnotic:proximity", "grenade");
  return mine;
}
bool qa_q1mp_fire_hipnotic_proximity(qa_q1_services *game, qa_q1_player_state *player) {
  if (!qa_shared_inventory_table_consume(game->host.inventory, player->actor, "q1:ammo/rockets", 1)) return false;
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
  if (!body.has_value) return false;
  const qa_vec3 aimed = qa_q1_aim(game, player->actor, qa_q1_make_vectors(game, player->view_angles).forward);
  const qa_vec3 velocity = qa_q1mp_grenade_velocity(game, player->view_angles, aimed);
  (void)qa_q1mp_launch_hipnotic_proximity(game, player->actor.id, body.value.origin, velocity);
  qa_q1_sound(game, player->actor.id, "hipweap/proxbomb.wav", "weapon", 1, 1);
  player->continuous_firing = false; player->weapon_animation_at = game->time; player->weapon_animation_base = 1;
  return finish(game, player, 0.6, 1, "progs/v_prox.mdl", -2);
}
static void proximity_explode(qa_q1_services *game, qa_q1_actor *mine) {
  qa_q1_radius_damage(game, mine->actor.id, mine->has_activator ? &mine->activator : NULL, 95, NULL, "hipnotic:proximity", NULL);
  qa_q1_sprite_explosion(game, mine);
}
static void arm_proximity_explosion(qa_q1_services *game, qa_q1_actor *mine, double delay) {
  qa_q1_actor_set_damageable(mine, false);
  qa_q1mp_set_mission_number(mine, "hipnotic:detonating", 1);
  mine->has_owner = mine->has_activator; mine->owner = mine->activator;
  qa_q1_schedule(game, mine, delay, qa_q1_callback_registry_action(game->named, mine, "hipnotic:proximity-explode"));
}
static void proximity_touch(void *ctx, qa_q1_services *game, qa_q1_actor *mine, qa_actor_id other,
                            const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS proximityTouch(game, mine, other) */
  if (qa_same_actor(other, mine->actor.id) || qa_q1mp_streq(game->host.classname(game->host.ctx, other), mine->classname)) return;
  mine->movement = "toss";
  if (mine->count == 1) return;
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, other);
  const qa_q1_actor *entity = NULL;
  bool moving = body.has_value && qa_q1_length(body.value.velocity) > 0;
  if (!moving) entity = qa_q1_get_entity(game, &other);
  if (moving || (entity != NULL && entity->aimed_damage) || qa_q1_is_player(game, &other)) {
    arm_proximity_explosion(game, mine, 0.1);
    proximity_explode(game, mine);
    return;
  }
  qa_q1_sound(game, mine->actor.id, "weapons/bounce.wav", "weapon", 1, 1);
  mine->movement = "none"; mine->count = 1;
  qa_q1mp_set_mission_reference(mine, "hipnotic:surface", &other);
  qa_q1_set_bounds(game, mine, (qa_bounds){{-8, -8, -8}, {8, 8, 8}});
  qa_q1_link(game, mine);
}
/* [...game.host.actors.observations()].reverse() */
static qa_actor_id *observations_reversed(qa_q1_services *game, size_t *out_count) {
  size_t count = 0;
  const qa_actor_observation *observations = qa_session_actor_registry_observations(game->host.actors, &count);
  qa_actor_id *ids = malloc((count != 0 ? count : 1) * sizeof *ids);
  if (ids == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) ids[i] = observations[count - 1 - i].id;
  *out_count = count;
  return ids;
}
static void proximity_watch(void *ctx, qa_q1_services *game, qa_q1_actor *mine) {
  (void)ctx;
  size_t mines = 0;
  for (size_t i = 0; i < game->entity_count; i++) {
    const qa_q1_actor *entity = game->entities[i];
    if (qa_q1mp_streq(entity->classname, mine->classname) && qa_q1_actor_number(entity, "hipnotic:detonating") == 0) mines++;
  }
  qa_actor_id surface;
  const bool has_surface = qa_q1mp_mission_reference(game, mine, "hipnotic:surface", &surface);
  const qa_body_state_result surface_body = has_surface ? qa_shared_body_table_read(game->host.bodies, surface) : (qa_body_state_result){0};
  if (game->time > mine->delay || mines > 15 || (surface_body.has_value && qa_q1_length(surface_body.value.velocity) > 0)) {
    proximity_explode(game, mine);
    return;
  }
  mine->has_owner = false;
  qa_q1_actor_set_damageable(mine, true);
  const qa_vec3 origin = qa_q1_get_body(game, mine).origin;
  size_t count = 0;
  qa_actor_id *others = observations_reversed(game, &count);
  for (size_t i = 0; i < count; i++) {
    const qa_actor_id other = others[i];
    const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, other);
    if (!body.has_value) continue;
    const qa_vec3 center = qa_q1_vadd(body.value.origin, qa_q1_vscale(qa_q1_vadd(body.value.bounds.min, body.value.bounds.max), 0.5));
    if (qa_q1_length(qa_q1_vsub(origin, center)) > 140) continue;
    const qa_q1_actor *target = qa_q1_get_entity(game, &other);
    const bool live_enemy = !qa_same_actor(other, mine->actor.id) && qa_q1_health(game, other) > 0 &&
                            (qa_q1_is_player(game, &other) || (qa_js_to_int32(target != NULL ? target->movement_flags : 0) & 32) != 0) &&
                            !(target != NULL && qa_q1mp_streq(target->classname, mine->classname));
    const bool loose_mine = target != NULL && qa_q1mp_streq(target->classname, mine->classname) && target->count == 0;
    if (!live_enemy && !loose_mine) continue;
    const qa_actor_id self = mine->actor.id;
    const qa_q1_trace trace = qa_q1mp_trace(game, origin, body.value.origin, &self, false);
    if (trace.fraction != 1) continue;
    free(others);
    qa_q1_sound(game, mine->actor.id, "hipweap/proxwarn.wav", "weapon", 1, 1);
    arm_proximity_explosion(game, mine, 0.5);
    return;
  }
  free(others);
  qa_q1_schedule(game, mine, 0.25, qa_q1_callback_registry_action(game->named, mine, "hipnotic:proximity-watch"));
}
static void proximity_explode_action(void *ctx, qa_q1_services *game, qa_q1_actor *mine) { (void)ctx; proximity_explode(game, mine); }
static void proximity_arm_die(void *ctx, qa_q1_services *runtime, qa_q1_actor *mine, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker; /* TS (runtime, mine) */
  arm_proximity_explosion(runtime, mine, 0.1);
}

bool qa_q1mp_fire_hipnotic_mjolnir(qa_q1_services *game, qa_q1_player_state *player) {
  qa_q1_actor *strike = qa_q1_create(game, "hipnotic_hammer_strike", NULL, NULL);
  strike->has_owner = true; strike->owner = player->actor.id;
  qa_q1_schedule(game, strike, 0.3, qa_q1_callback_registry_action(game->named, strike, "hipnotic:hammer-strike"));
  player->continuous_firing = false; player->weapon_animation_at = game->time;
  player->weapon_animation_base = qa_shared_inventory_table_count(game->host.inventory, player->actor.id, "q1:ammo/cells") < 30 ? 32 : 38;
  return finish(game, player, 0.8, 1, "progs/v_hammer.mdl", 0);
}
static void hammer_damage(qa_q1_services *game, qa_actor_id from, qa_vec3 start, qa_vec3 end, double damage, qa_q1_weapon weapon) {
  const qa_vec3 delta = qa_q1_vsub(end, start), side = QA_Q1MP_V3(-delta.y * 16, -delta.y * 16, 0);
  const qa_vec3 offsets[3] = {QA_Q1_ZERO, side, qa_q1_vscale(side, -1)};
  qa_actor_id hit[3];
  size_t hit_count = 0;
  for (size_t i = 0; i < 3; i++) {
    const qa_q1_trace trace = qa_q1mp_trace(game, qa_q1_vadd(start, offsets[i]), qa_q1_vadd(end, offsets[i]), &from, true);
    /* MG3 HIP_HammerDamage returns when the first trace hits no entity, because trace_ent is then the player's owner, world. */
    if (!trace.has_actor && i == 0 && game->mg3_pickups) return;
    if (!trace.has_actor) continue;
    const qa_actor_id target = trace.actor;
    bool seen = false;
    for (size_t h = 0; h < hit_count; h++) if (qa_same_actor(hit[h], target)) seen = true;
    if (seen) continue;
    hit[hit_count++] = target;
    const qa_combat_state_result state = qa_gameplay_authority_read(game->host.combat, target);
    const qa_q1_player_state *player = qa_q1_player(game, target);
    if (state.has_value && state.value.can_take_damage && (player != NULL ? qa_q1_player_powerup_get(player, "hipnotic:wetsuit") : 0) == 0) {
      qa_q1_event event;
      memset(&event, 0, sizeof event);
      event.kind = "particles"; event.has_origin = true; event.origin = trace.end; event.direction = QA_Q1MP_V3(0, 0, 100);
      event.color = 225; event.count = damage * 4;
      qa_q1_emit(game, event);
      (void)qa_q1_damage(game, target, from, &from, damage, weapon, "direct", "electric", NULL);
    }
  }
}
static void hammer_strike(void *ctx, qa_q1_services *game, qa_q1_actor *strike) {
  (void)ctx;
  qa_q1_player_state *player = strike->has_owner ? qa_q1_player(game, strike->owner) : NULL;
  const qa_body_state_result body = player != NULL ? qa_shared_body_table_read(game->host.bodies, player->actor.id) : (qa_body_state_result){0};
  if (player == NULL || !body.has_value || qa_q1_health(game, player->actor.id) <= 0) { qa_q1_remove(game, strike); return; }
  const qa_q1_basis basis = qa_q1_make_vectors(game, player->view_angles);
  const qa_vec3 source = qa_q1_vadd(body.value.origin, QA_Q1MP_V3(0, 0, 16));
  const qa_actor_id self = player->actor.id;
  qa_q1_trace trace = qa_q1mp_trace(game, source, qa_q1_vadd(source, qa_q1_vscale(basis.forward, 32)), &self, true);
  const double cells = qa_shared_inventory_table_count(game->host.inventory, player->actor.id, "q1:ammo/cells");
  player->attack_finished = qa_q1mp_fround(game->time + qa_q1_weapon_attack_delay(game, player, 0.4));
  if (trace.fraction == 1 && cells >= 15) {
    const qa_vec3 start = qa_q1_vadd(source, qa_q1_vscale(basis.forward, 32));
    trace = qa_q1mp_trace(game, start, qa_q1_vsub(start, qa_q1_vscale(basis.up, 50)), &self, true);
    if (trace.fraction > 0.3 && trace.fraction < 1) {
      if (player->water_level > 1) {
        (void)qa_shared_inventory_table_consume(game->host.inventory, player->actor, "q1:ammo/cells", cells);
        qa_q1_radius_damage(game, self, &self, 35 * cells, NULL, "hipnotic:mjolnir", "discharge");
      } else {
        (void)qa_shared_inventory_table_consume(game->host.inventory, player->actor, "q1:ammo/cells", 15);
        qa_q1mp_spawn_hipnotic_hammer_base(game, player, trace.end, NULL);
      }
      player->attack_finished = qa_q1mp_fround(game->time + qa_q1_weapon_attack_delay(game, player, 1.5));
      qa_q1_remove(game, strike);
      return;
    }
  }
  const qa_vec3 origin = qa_q1_vsub(trace.end, qa_q1_vscale(basis.forward, 4));
  const qa_combat_state_result state = trace.has_actor ? qa_gameplay_authority_read(game->host.combat, trace.actor) : (qa_combat_state_result){0};
  if (trace.has_actor && state.has_value && state.value.can_take_damage) {
    const double damage = qa_q1mp_streq(game->host.classname(game->host.ctx, trace.actor), "monster_zombie") ? 70 : 50;
    qa_q1_effect(game, "blood", origin, &trace.actor, damage);
    (void)qa_q1_damage(game, trace.actor, self, &self, damage, "hipnotic:mjolnir", NULL, NULL, NULL);
  } else if (trace.fraction != 1) {
    qa_q1_sound(game, self, "hipweap/mjoltink.wav", "weapon", 1, 1);
    qa_q1_effect(game, "gunshot", origin, NULL, 1);
  } else qa_q1_sound(game, self, "knight/sword1.wav", "weapon", 1, 1);
  qa_q1_remove(game, strike);
}
void qa_q1mp_spawn_hipnotic_hammer_base(qa_q1_services *game, qa_q1_player_state *player, qa_vec3 origin, qa_q1_weapon weapon) {
  if (weapon == NULL) weapon = "hipnotic:mjolnir";
  qa_q1_actor *base = qa_q1_create(game, "hipnotic_mjolnir_base", NULL, NULL);
  base->has_owner = true; base->owner = player->actor.id;
  base->movedir = qa_q1_make_vectors(game, player->view_angles).forward; base->projectile_weapon = weapon;
  qa_q1_set_origin(game, base, origin);
  qa_q1_schedule(game, base, 1, qa_q1_callback_registry_action(game->named, base, "SUB_Remove"));
  qa_q1_sound(game, base->actor.id, "hipweap/mjolslap.wav", "auto", 1, 1);
  qa_q1_sound(game, base->actor.id, "hipweap/mjolhit.wav", "weapon", 1, 1);
  for (int i = 0; i < 4; i++) {
    qa_q1_actor *bolt = qa_q1_create(game, "hipnotic_mjolnir_lightning", NULL, NULL);
    bolt->has_owner = true; bolt->owner = base->actor.id; bolt->has_activator = true; bolt->activator = player->actor.id;
    bolt->projectile_weapon = weapon;
    bolt->delay = qa_q1mp_fround(game->time + 0.8); bolt->mangle = QA_Q1MP_V3(0, player->view_angles.y, 0);
    qa_q1_set_origin(game, bolt, origin);
    qa_q1_schedule(game, bolt, 0, qa_q1_callback_registry_action(game->named, bolt, "hipnotic:hammer-lightning"));
  }
}
static bool already_struck(qa_q1_services *game, qa_actor_id actor) {
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *other = game->entities[i];
    if (!(qa_q1mp_streq(other->classname, "hipnotic_mjolnir_lightning") || qa_q1mp_streq(other->classname, "hipnotic_tesla_lightning")) ||
        other->count != 1) continue;
    qa_actor_id struck;
    if (qa_q1mp_mission_reference(game, other, "hipnotic:enemy", &struck) && qa_same_actor(struck, actor)) return true;
  }
  return false;
}
static void hammer_lightning(void *ctx, qa_q1_services *game, qa_q1_actor *bolt) {
  (void)ctx;
  qa_q1_actor *base = bolt->has_owner ? qa_q1_get_entity(game, &bolt->owner) : NULL;
  if (game->time > bolt->delay || base == NULL || !bolt->has_activator) { qa_q1_remove(game, bolt); return; }
  const qa_vec3 origin = qa_q1_get_body(game, base).origin;
  const double old_state = bolt->count;
  const qa_actor_id self = bolt->actor.id;
  qa_actor_id target;
  bool has_target = qa_q1mp_mission_reference(game, bolt, "hipnotic:enemy", &target);
  if (bolt->count == 0) {
    has_target = false;
    double best = 350;
    size_t count = 0;
    qa_actor_id *actors = observations_reversed(game, &count);
    for (size_t i = 0; i < count; i++) {
      const qa_actor_id actor = actors[i];
      const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor);
      const qa_q1_actor *entity = qa_q1_get_entity(game, &actor);
      const int32_t flags = qa_js_to_int32(entity != NULL ? entity->movement_flags : 0);
      if (!body.has_value || qa_same_actor(actor, bolt->activator) || qa_q1_health(game, actor) <= 0 || (flags & 128) != 0 ||
          !(qa_q1_is_player(game, &actor) || (flags & 32) != 0)) continue;
      if (already_struck(game, actor)) continue;
      const double distance = qa_q1_length(qa_q1_vsub(body.value.origin, origin));
      if (distance >= best) continue;
      const double eye = qa_q1_is_player(game, &actor) ? 22 : entity != NULL ? qa_q1_actor_vector(entity, "view_ofs").z : 0;
      const qa_q1_trace visible = qa_q1mp_trace(game, origin, qa_q1_vadd(body.value.origin, QA_Q1MP_V3(0, 0, eye)), &self, false);
      if (visible.fraction != 1 || (visible.in_open && visible.in_water)) continue;
      best = distance; target = actor; has_target = true;
    }
    free(actors);
    qa_q1mp_set_mission_reference(bolt, "hipnotic:enemy", has_target ? &target : NULL);
    if (!has_target) {
      const qa_q1_basis basis = qa_q1_make_vectors(game, bolt->mangle);
      const double spread = 400 * game->host.random(game->host.ctx) - 200;
      const qa_vec3 end = qa_q1_vadd(qa_q1_vadd(origin, qa_q1_vscale(basis.forward, 200)), qa_q1_vscale(basis.right, spread));
      const qa_q1_trace trace = qa_q1mp_trace(game, origin, end, &self, false);
      emit_beam(game, "lightning2", self, origin, trace.end);
      qa_q1_schedule(game, bolt, 0.1, qa_q1_callback_registry_action(game->named, bolt, "hipnotic:hammer-lightning"));
      return;
    }
    bolt->count = 1;
  }
  const qa_body_state_result body = has_target ? qa_shared_body_table_read(game->host.bodies, target) : (qa_body_state_result){0};
  if (!body.has_value || !has_target) {
    bolt->count = 0;
    qa_q1_schedule(game, bolt, 0.1, qa_q1_callback_registry_action(game->named, bolt, "hipnotic:hammer-lightning"));
    return;
  }
  const qa_vec3 size = qa_q1_vsub(body.value.bounds.max, body.value.bounds.min);
  const double along = 0.25 + game->host.random(game->host.ctx) * 0.5;
  const qa_vec3 end = qa_q1_vadd(qa_q1_vadd(body.value.origin, body.value.bounds.min), qa_q1_vscale(size, along));
  const qa_q1_trace trace = qa_q1mp_trace(game, origin, end, &bolt->activator, false);
  if (trace.fraction != 1 || qa_q1_health(game, target) <= 0) {
    bolt->count = 0;
    qa_q1_schedule(game, bolt, 0.1, qa_q1_callback_registry_action(game->named, bolt, "hipnotic:hammer-lightning"));
    return;
  }
  emit_beam(game, "lightning2", self, origin, trace.end);
  const double damage = old_state == 0 ? 80 : 30;
  const double facing = qa_q1_dot(qa_q1_normalize(qa_q1_vsub(body.value.origin, origin)), base->movedir);
  hammer_damage(game, bolt->activator, origin, trace.end, facing > 0.3 ? damage : damage * 0.5,
                bolt->projectile_weapon != NULL ? bolt->projectile_weapon : "hipnotic:mjolnir");
  qa_q1_schedule(game, bolt, 0.2, qa_q1_callback_registry_action(game->named, bolt, "hipnotic:hammer-lightning"));
}

void qa_q1mp_register_hipnotic_laser_callbacks(qa_q1_services *game) {
  if (qa_q1_callback_registry_has(game->named, "hipnotic:laser-touch")) return;
  qa_q1_callback_registry_register(game->named, "hipnotic:laser-touch",
                                   (qa_q1_callback_handlers){.touch = laser_touch, .trajectory = laser_trajectory});
  qa_q1_callback_registry_register(game->named, "hipnotic:laser-think", (qa_q1_callback_handlers){.action = laser_think});
}
void qa_q1mp_register_hipnotic_weapon_callbacks(qa_q1_services *game) {
  qa_q1mp_register_hipnotic_laser_callbacks(game);
  qa_q1_callback_registry_register(game->named, "hipnotic:proximity-touch", (qa_q1_callback_handlers){.touch = proximity_touch});
  qa_q1_callback_registry_register(game->named, "hipnotic:proximity-watch", (qa_q1_callback_handlers){.action = proximity_watch});
  qa_q1_callback_registry_register(game->named, "hipnotic:proximity-explode", (qa_q1_callback_handlers){.action = proximity_explode_action});
  qa_q1_callback_registry_register(game->named, "hipnotic:proximity-arm-explosion", (qa_q1_callback_handlers){.die = proximity_arm_die});
  qa_q1mp_register_hipnotic_hammer_callbacks(game);
}
void qa_q1mp_register_hipnotic_hammer_callbacks(qa_q1_services *game) {
  if (qa_q1_callback_registry_has(game->named, "hipnotic:hammer-strike")) return;
  qa_q1_callback_registry_register(game->named, "hipnotic:hammer-strike", (qa_q1_callback_handlers){.action = hammer_strike});
  qa_q1_callback_registry_register(game->named, "hipnotic:hammer-lightning", (qa_q1_callback_handlers){.action = hammer_lightning});
}
