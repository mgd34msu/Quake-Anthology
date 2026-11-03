/* Port of src/content/q1/missionpacks/rogue-weapons.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/rogue_weapons.h"

#include "missionpacks_internal.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/foundation/weapons.h"

static bool controls_trajectory(qa_q1_services *game, qa_actor_id actor) {
  const qa_weapon_behavior_projectile_port *behavior = game->host.weapon_behavior;
  return behavior != NULL && behavior->controls_trajectory(behavior->ctx, actor);
}
static bool finish(qa_q1_services *game, qa_q1_player_state *player, double delay, const char *model, bool repeating) {
  delay = qa_q1_weapon_attack_delay(game, player, delay);
  player->attack_finished = qa_q1mp_fround(game->time + delay);
  player->next_weapon_frame = qa_q1mp_fround(game->time + (repeating ? qa_q1_weapon_frame_delay(game, player, 0.1) : delay));
  player->continuous_firing = repeating;
  player->weapon_frame = repeating ? fmod(player->weapon_frame, 8) + 1 : qa_q1mp_streq(player->weapon, "rogue:plasma") ? 0 : 1;
  player->weapon_animation_at = repeating || qa_q1mp_streq(player->weapon, "rogue:plasma") ? -1 : game->time;
  player->weapon_animation_base = 1; player->hostile_until = qa_q1mp_fround(game->time + 1);
  qa_q1_weapon_punch(game, player, -2);
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "weapon"; event.has_player = true; event.player = player->actor.id; event.weapon = player->weapon;
  event.view_model = model; event.frame = player->weapon_frame; event.punch = -2;
  qa_q1_emit(game, event);
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
  if (body.has_value) qa_q1_effect(game, "muzzleflash", body.value.origin, &player->actor.id, 1);
  return true;
}
static qa_q1_actor *missile(qa_q1_services *game, const char *classname, qa_actor_id owner, qa_vec3 origin, qa_vec3 velocity, const char *model) {
  qa_q1_actor *entity = qa_q1_create(game, classname, NULL, NULL);
  entity->has_owner = true; entity->owner = owner; entity->solid = "bbox"; entity->movement = "flymissile";
  qa_q1mp_set_string(&entity->model, model);
  qa_q1_set_bounds(game, entity, QA_Q1_POINT);
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true; patch.origin = origin; patch.has_velocity = true; patch.velocity = velocity;
  patch.has_angles = true; patch.angles = qa_q1mp_velocity_angles(velocity);
  qa_q1_set_body(game, entity, &patch);
  qa_q1_link(game, entity);
  return entity;
}
static bool ignore_touch(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other) {
  if (entity->has_owner && qa_same_actor(entity->owner, other)) return true;
  const qa_q1_actor *target = qa_q1_get_entity(game, &other);
  return target != NULL && qa_q1mp_streq(target->solid, "trigger");
}
static void emit_colored_explosion(qa_q1_services *game, qa_vec3 origin, double color_start, double color_length) {
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "colored-explosion"; event.has_origin = true; event.origin = origin;
  event.color_start = color_start; event.color_length = color_length;
  qa_q1_emit(game, event);
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

qa_q1_actor *qa_q1mp_launch_rogue_lava_spike(qa_q1_services *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 direction, bool powered) {
  qa_q1_actor *entity = missile(game, "lava_spike", owner, origin, qa_q1_vscale(direction, 1000), "progs/lspike.mdl");
  entity->count = powered ? 1 : 0; entity->projectile_weapon = powered ? "rogue:lava-supernailgun" : "rogue:lava-nailgun";
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "rogue:lava-touch");
  qa_q1_schedule(game, entity, 6, qa_q1_callback_registry_action(game->named, entity, "SUB_Remove"));
  qa_q1_launch_projectile_behavior(game, entity, owner, entity->projectile_weapon, "nail");
  return entity;
}
bool qa_q1mp_fire_rogue_lava(qa_q1_services *game, qa_q1_player_state *player) {
  const double nails = qa_shared_inventory_table_count(game->host.inventory, player->actor.id, "rogue:ammo/lava-nails");
  const bool powered = qa_q1mp_streq(player->weapon, "rogue:lava-supernailgun") && nails >= 2;
  if (nails < 1) { qa_q1_message(game, &player->actor.id, "$qc_out_lava_nails", false, NULL, 0); return false; }
  if (!qa_shared_inventory_table_consume(game->host.inventory, player->actor, "rogue:ammo/lava-nails", powered ? 2 : 1)) return false;
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
  if (!body.has_value) return false;
  const qa_q1_basis basis = qa_q1_make_vectors(game, player->view_angles);
  const qa_vec3 origin = qa_q1_vadd(qa_q1_vadd(body.value.origin, QA_Q1MP_V3(0, 0, 16)), qa_q1_vscale(basis.right, powered ? 0 : player->nail_side * 4));
  qa_q1_sound(game, player->actor.id, powered ? "weapons/spike2.wav" : "weapons/rocket1i.wav", "weapon", 1, 1);
  const qa_vec3 direction = qa_q1_aim(game, player->actor, basis.forward);
  (void)qa_q1mp_launch_rogue_lava_spike(game, player->actor.id, origin, direction, powered);
  player->nail_side *= -1;
  return finish(game, player, 0.2, qa_q1mp_streq(player->weapon, "rogue:lava-supernailgun") ? "progs/v_lava2.mdl" : "progs/v_lava.mdl", true);
}
static void lava_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS lavaTouch(game, entity, other) */
  if (ignore_touch(game, entity, other)) return;
  const qa_vec3 origin = qa_q1_get_body(game, entity).origin;
  if (qa_q1mp_streq(game->host.contents(game->host.ctx, origin), "sky")) { qa_q1_remove(game, entity); return; }
  const bool powered = entity->count == 1;
  const qa_combat_state_result state = qa_gameplay_authority_read(game->host.combat, other);
  if (state.has_value && state.value.can_take_damage) {
    qa_q1_effect(game, "blood", origin, &other, powered ? 18 : 9);
    if (!qa_q1mp_streq(game->host.classname(game->host.ctx, other), "monster_lava_man")) {
      const bool player = qa_q1_is_player(game, &other);
      const double damage = player ? (powered ? 18 : 9) : (powered ? 30 : 15);
      (void)qa_q1_damage(game, other, entity->actor.id, entity->has_owner ? &entity->owner : NULL, damage, entity->projectile_weapon,
                         "direct", powered ? "rogue:super-lava" : "rogue:lava",
                         player ? (powered ? "half-effectiveness" : "bypass") : NULL);
    }
  } else qa_q1_effect(game, powered ? "superspike" : "spike", origin, NULL, 1);
  qa_q1_remove(game, entity);
}

qa_q1_actor *qa_q1mp_launch_rogue_multi_grenade(qa_q1_services *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 velocity, qa_vec3 angles) {
  qa_q1_actor *grenade = missile(game, "MultiGrenade", owner, origin, velocity, "progs/mervup.mdl");
  grenade->movement = "bounce"; grenade->mangle = angles; grenade->angular_velocity = QA_Q1MP_V3(300, 300, 300);
  grenade->projectile_weapon = "rogue:multi-grenade";
  grenade->touch = qa_q1_callback_registry_touch(game->named, grenade, "rogue:multi-grenade-touch");
  qa_q1_schedule(game, grenade, 1, qa_q1_callback_registry_action(game->named, grenade, "rogue:multi-grenade-split"));
  qa_q1_launch_projectile_behavior(game, grenade, owner, "rogue:multi-grenade", "grenade");
  return grenade;
}
bool qa_q1mp_fire_rogue_multi_grenade(qa_q1_services *game, qa_q1_player_state *player) {
  if (!qa_shared_inventory_table_consume(game->host.inventory, player->actor, "rogue:ammo/multi-rockets", 1)) return false;
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
  if (!body.has_value) return false;
  const qa_vec3 aimed = qa_q1_aim(game, player->actor, qa_q1_make_vectors(game, player->view_angles).forward);
  const qa_vec3 velocity = qa_q1mp_grenade_velocity(game, player->view_angles, aimed);
  (void)qa_q1mp_launch_rogue_multi_grenade(game, player->actor.id, body.value.origin, velocity, QA_Q1_ZERO);
  qa_q1_sound(game, player->actor.id, "weapons/grenade.wav", "weapon", 1, 1);
  return finish(game, player, 0.6, "progs/v_multi.mdl", false);
}
static void grenade_explode(qa_q1_services *game, qa_q1_actor *grenade, bool mini) {
  const bool player = grenade->has_owner && qa_q1_is_player(game, &grenade->owner);
  qa_q1_radius_damage(game, grenade->actor.id, grenade->has_owner ? &grenade->owner : NULL, mini ? (player ? 90 : 60) : 120, NULL,
                      "rogue:multi-grenade", NULL);
  if (!mini) {
    qa_q1_effect(game, "explosion", qa_q1_get_body(game, grenade).origin, NULL, 1);
    qa_q1_remove(game, grenade);
    return;
  }
  emit_colored_explosion(game, qa_q1_get_body(game, grenade).origin, 230, 5);
  qa_q1_sprite_explosion_plain_effect(game, grenade, false);
}
static void split_grenade(void *ctx, qa_q1_services *game, qa_q1_actor *grenade) {
  (void)ctx;
  if (!grenade->has_owner) { qa_q1_remove(game, grenade); return; }
  static const double OFFSETS[5] = {0, 72, 144, 216, 288};
  for (size_t i = 0; i < 5; i++) {
    const qa_vec3 angles = QA_Q1MP_V3(grenade->mangle.x, grenade->mangle.y + OFFSETS[i], grenade->mangle.z);
    const qa_q1_basis basis = qa_q1_make_vectors(game, angles);
    qa_vec3 velocity = qa_q1_vadd(qa_q1_vscale(basis.forward, 100), qa_q1_vscale(basis.up, 400));
    velocity = qa_q1_vadd(velocity, qa_q1_vscale(basis.forward, (game->host.random(game->host.ctx) * 2 - 1) * 60 - 30));
    velocity = qa_q1_vadd(velocity, qa_q1_vscale(basis.right, (game->host.random(game->host.ctx) * 2 - 1) * 40 - 20));
    velocity = qa_q1_vadd(velocity, qa_q1_vscale(basis.up, (game->host.random(game->host.ctx) * 2 - 1) * 60 - 30));
    qa_q1_actor *mini = missile(game, "MiniGrenade", grenade->owner, qa_q1_get_body(game, grenade).origin, velocity, "progs/mervup.mdl");
    mini->movement = "bounce"; mini->mangle = angles; mini->angular_velocity = QA_Q1MP_V3(300, 300, 300);
    mini->projectile_weapon = "rogue:multi-grenade";
    mini->touch = qa_q1_callback_registry_touch(game->named, mini, "rogue:multi-grenade-touch");
    qa_q1_launch_projectile_behavior(game, mini, grenade->owner, "rogue:multi-grenade", "grenade");
    const double delay = 1 + (game->host.random(game->host.ctx) * 2 - 1) * 0.5;
    qa_q1_schedule(game, mini, delay, qa_q1_callback_registry_action(game->named, mini, "rogue:mini-grenade-explode"));
  }
  qa_q1_remove(game, grenade);
}
static void multi_grenade_touch(void *ctx, qa_q1_services *game, qa_q1_actor *grenade, qa_actor_id other,
                                const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS multiGrenadeTouch(game, grenade, other) */
  if (grenade->has_owner && qa_same_actor(grenade->owner, other)) return;
  const qa_q1_actor *target = qa_q1_get_entity(game, &other);
  if ((target != NULL && target->aimed_damage) || qa_q1_is_player(game, &other)) {
    grenade_explode(game, grenade, qa_q1mp_streq(grenade->classname, "MiniGrenade") || !grenade->has_owner ||
                                   !qa_q1_is_player(game, &grenade->owner));
    return;
  }
  qa_q1_sound(game, grenade->actor.id, "weapons/bounce.wav", "weapon", 1, 1);
  if (qa_q1_length(qa_q1_get_body(game, grenade).velocity) == 0) grenade->angular_velocity = QA_Q1_ZERO;
}
static void mini_grenade_explode(void *ctx, qa_q1_services *runtime, qa_q1_actor *grenade) { (void)ctx; grenade_explode(runtime, grenade, true); }

/* direct NULL = TS null. */
static void explode_rocket(qa_q1_services *game, qa_q1_actor *rocket, const qa_actor_id *direct) {
  if (direct != NULL && qa_q1_health(game, *direct) != 0) {
    double damage = 60 + game->host.random(game->host.ctx) * 15;
    if (qa_q1mp_streq(game->host.classname(game->host.ctx, *direct), "monster_shambler") ||
        qa_q1mp_streq(game->host.classname(game->host.ctx, *direct), "monster_dragon")) damage *= 0.5;
    (void)qa_q1_damage(game, *direct, rocket->actor.id, rocket->has_owner ? &rocket->owner : NULL, damage, "rogue:multi-rocket", NULL, NULL, NULL);
  }
  qa_q1_radius_damage(game, rocket->actor.id, rocket->has_owner ? &rocket->owner : NULL, 75, direct, "rogue:multi-rocket", NULL);
  const qa_vec3 origin = qa_q1_get_body(game, rocket).origin, velocity = qa_q1_get_body(game, rocket).velocity;
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true; patch.origin = qa_q1_vsub(origin, qa_q1_vscale(qa_q1_normalize(velocity), 8));
  qa_q1_set_body(game, rocket, &patch);
  qa_q1_sprite_explosion(game, rocket);
}
static void home_rocket(void *ctx, qa_q1_services *game, qa_q1_actor *rocket);
static void acquire_rocket(void *ctx, qa_q1_services *game, qa_q1_actor *rocket) {
  if (rocket->delay < game->time) { explode_rocket(game, rocket, NULL); return; }
  const qa_q1_body body = qa_q1_get_body(game, rocket);
  qa_owned_actor owner;
  const bool has_owner = rocket->has_owner && qa_session_actor_registry_resolve_owned(game->host.actors, rocket->owner, &owner);
  const qa_vec3 direction = !has_owner ? qa_q1_normalize(body.velocity)
                                       : qa_q1_aim(game, rocket->actor, qa_q1_make_vectors(game, rocket->mangle).forward);
  const qa_actor_id self = rocket->actor.id;
  const qa_q1_trace trace = qa_q1mp_trace(game, body.origin, qa_q1_vadd(body.origin, qa_q1_vscale(direction, 1000)), &self, true);
  const qa_q1_actor *target = trace.has_actor ? qa_q1_get_entity(game, &trace.actor) : NULL;
  if (trace.has_actor && (qa_js_to_int32(target != NULL ? target->movement_flags : 0) & 32) != 0) {
    qa_q1mp_set_mission_reference(rocket, "rogue:enemy", &trace.actor);
    home_rocket(ctx, game, rocket);
    return;
  }
  rocket->mangle = qa_q1mp_velocity_angles(body.velocity);
  qa_q1_schedule(game, rocket, 0.2, qa_q1_callback_registry_action(game->named, rocket, "rogue:rocket-acquire"));
}
static void home_rocket(void *ctx, qa_q1_services *game, qa_q1_actor *rocket) {
  (void)ctx;
  qa_actor_id target;
  const bool has_target = qa_q1mp_mission_reference(game, rocket, "rogue:enemy", &target);
  const qa_body_state_result body = has_target ? qa_shared_body_table_read(game->host.bodies, target) : (qa_body_state_result){0};
  if (!body.has_value || !has_target || qa_q1_health(game, target) < 1) { qa_q1_remove(game, rocket); return; }
  if (!controls_trajectory(game, rocket->actor.id))
    qa_q1mp_move_missile(game, rocket, qa_q1_vscale(qa_q1_normalize(qa_q1_vsub(body.value.origin, qa_q1_get_body(game, rocket).origin)), 1000));
  qa_q1_schedule(game, rocket, 0.1, qa_q1_callback_registry_action(game->named, rocket, "rogue:rocket-home"));
}
bool qa_q1mp_fire_rogue_multi_rocket(qa_q1_services *game, qa_q1_player_state *player) {
  if (!qa_shared_inventory_table_consume(game->host.inventory, player->actor, "rogue:ammo/multi-rockets", 1)) return false;
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
  if (!body.has_value) return false;
  (void)qa_q1_make_vectors(game, player->view_angles);
  const bool multiplayer = game->options.coop || game->options.deathmatch != 0;
  static const struct { double offset, frame; } SHOTS[4] = {{-10, 2}, {-5, 3}, {5, 0}, {10, 1}};
  for (size_t i = 0; i < 4; i++) {
    const qa_vec3 origin = qa_q1_vadd(qa_q1_vadd(body.value.origin, qa_q1_vscale(game->basis.forward, 8)), QA_Q1MP_V3(0, 0, 16));
    const qa_vec3 angles = QA_Q1MP_V3(player->view_angles.x, player->view_angles.y + SHOTS[i].offset * 0.66, player->view_angles.z);
    const qa_q1_basis basis = qa_q1_make_vectors(game, multiplayer ? angles : player->view_angles);
    const qa_vec3 velocity = multiplayer ? qa_q1_vscale(qa_q1_aim(game, player->actor, basis.forward), 1000)
                                         : qa_q1_vsub(qa_q1_vscale(basis.forward, 1000), qa_q1_vscale(basis.right, SHOTS[i].offset * 8));
    qa_q1_actor *rocket = missile(game, "MultiRocket", player->actor.id, origin, velocity, multiplayer ? "progs/rockup_d.mdl" : "progs/rockup.mdl");
    rocket->projectile_weapon = "rogue:multi-rocket"; rocket->frame = SHOTS[i].frame;
    rocket->delay = qa_q1mp_fround(game->time + 4); rocket->mangle = player->view_angles;
    rocket->touch = qa_q1_callback_registry_touch(game->named, rocket, "rogue:rocket-touch");
    qa_q1_launch_projectile_behavior(game, rocket, player->actor.id, "rogue:multi-rocket", "rocket");
    if (multiplayer) qa_q1_schedule(game, rocket, 4, qa_q1_callback_registry_action(game->named, rocket, "rogue:rocket-explode"));
    else {
      const qa_actor_id shooter = player->actor.id;
      const qa_q1_trace trace = qa_q1mp_trace(game, origin, qa_q1_vadd(origin, velocity), &shooter, true);
      const qa_q1_actor *target = trace.has_actor ? qa_q1_get_entity(game, &trace.actor) : NULL;
      if (trace.has_actor && (qa_js_to_int32(target != NULL ? target->movement_flags : 0) & 32) != 0) {
        /* Both source editions return here without assigning nextthink. */
        qa_q1mp_set_mission_reference(rocket, "rogue:enemy", &trace.actor);
        rocket->think = qa_q1_callback_registry_action(game->named, rocket, "rogue:rocket-home");
      } else qa_q1_schedule(game, rocket, 0.1, qa_q1_callback_registry_action(game->named, rocket, "rogue:rocket-acquire"));
    }
  }
  qa_q1_sound(game, player->actor.id, "weapons/sgun1.wav", "weapon", 1, 1);
  return finish(game, player, 0.8, "progs/v_multi2.mdl", false);
}

qa_q1_actor *qa_q1mp_launch_rogue_plasma(qa_q1_services *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 direction) {
  qa_q1_actor *plasma = missile(game, "plasma", owner, origin, qa_q1_vscale(direction, 0.01), "progs/plasma.mdl");
  plasma->angular_velocity = QA_Q1MP_V3(300, 300, 300); plasma->projectile_weapon = "rogue:plasma";
  if (!game->options.coop && game->options.deathmatch == 0) plasma->effects = 4;
  plasma->touch = qa_q1_callback_registry_touch(game->named, plasma, "rogue:plasma-touch");
  qa_q1_sound(game, plasma->actor.id, "plasma/flight.wav", "weapon", 1, 1);
  qa_q1_schedule(game, plasma, 0.1, qa_q1_callback_registry_action(game->named, plasma, "rogue:plasma-launch"));
  qa_q1_launch_projectile_behavior(game, plasma, owner, "rogue:plasma", "plasma");
  return plasma;
}
bool qa_q1mp_fire_rogue_plasma(qa_q1_services *game, qa_q1_player_state *player) {
  const double ammo = qa_shared_inventory_table_count(game->host.inventory, player->actor.id, "rogue:ammo/plasma");
  if (ammo < 1) return false;
  if (player->water_level > 1) {
    (void)qa_shared_inventory_table_consume(game->host.inventory, player->actor, "rogue:ammo/plasma", ammo);
    qa_q1_radius_damage(game, player->actor.id, &player->actor.id, 35 * ammo, NULL, "rogue:plasma", NULL);
  } else {
    const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
    if (!body.has_value) return false;
    (void)qa_shared_inventory_table_consume(game->host.inventory, player->actor, "rogue:ammo/plasma", 1);
    const qa_q1_basis basis = qa_q1_make_vectors(game, player->view_angles);
    const qa_vec3 origin = qa_q1_vadd(qa_q1_vadd(body.value.origin, qa_q1_vscale(basis.forward, 24)), QA_Q1MP_V3(0, 0, 16));
    const qa_vec3 direction = qa_q1_aim(game, player->actor, basis.forward);
    (void)qa_q1mp_launch_rogue_plasma(game, player->actor.id, origin, direction);
    qa_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = "sound"; event.has_actor = true; event.actor = player->actor.id; event.path = "plasma/fire.wav";
    event.sound_channel = "weapon"; event.attenuation = 1; event.volume = 0.5;
    qa_q1_emit(game, event);
  }
  return finish(game, player, 1, "progs/v_plasma.mdl", false);
}
static void plasma_damage(qa_q1_services *game, qa_q1_actor *plasma, qa_vec3 end) {
  const qa_vec3 origin = qa_q1_get_body(game, plasma).origin, delta = qa_q1_vsub(end, origin);
  const qa_vec3 side = QA_Q1MP_V3(-delta.y * 16, -delta.y * 16, 0);
  const qa_vec3 offsets[3] = {QA_Q1_ZERO, side, qa_q1_vscale(side, -1)};
  qa_actor_id hit[3];
  size_t hit_count = 0;
  const qa_actor_id self = plasma->actor.id;
  for (size_t i = 0; i < 3; i++) {
    const qa_q1_trace trace = qa_q1mp_trace(game, qa_q1_vadd(origin, offsets[i]), qa_q1_vadd(end, offsets[i]), &self, true);
    if (!trace.has_actor) continue;
    const qa_actor_id other = trace.actor;
    bool seen = false;
    for (size_t h = 0; h < hit_count; h++) if (qa_same_actor(hit[h], other)) seen = true;
    if (seen) continue;
    hit[hit_count++] = other;
    const qa_combat_state_result state = qa_gameplay_authority_read(game->host.combat, other);
    if (state.has_value && state.value.can_take_damage) {
      qa_q1_event event;
      memset(&event, 0, sizeof event);
      event.kind = "particles"; event.has_origin = true; event.origin = trace.end; event.direction = QA_Q1MP_V3(0, 0, 100);
      event.color = 225; event.count = 200;
      qa_q1_emit(game, event);
      (void)qa_q1_damage(game, other, self, plasma->has_owner ? &plasma->owner : NULL, 50, "rogue:plasma", NULL, NULL, NULL);
    }
  }
}
static void plasma_touch(void *ctx, qa_q1_services *game, qa_q1_actor *plasma, qa_actor_id other,
                         const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS plasmaTouch(game, plasma, other) */
  if (plasma->has_owner && qa_same_actor(plasma->owner, other)) return;
  const qa_vec3 origin = qa_q1_get_body(game, plasma).origin;
  if (qa_q1mp_streq(game->host.contents(game->host.ctx, origin), "sky")) { qa_q1_remove(game, plasma); return; }
  double damage = 80 + game->host.random(game->host.ctx) * 20;
  qa_q1_sound(game, plasma->actor.id, "plasma/explode.wav", "weapon", 1, 1);
  if (qa_q1_health(game, other) != 0) {
    if (qa_q1mp_streq(game->host.classname(game->host.ctx, other), "monster_shambler")) damage *= 0.5;
    (void)qa_q1_damage(game, other, plasma->actor.id, plasma->has_owner ? &plasma->owner : NULL, damage, "rogue:plasma", NULL, NULL, NULL);
  }
  qa_q1_radius_damage(game, plasma->actor.id, plasma->has_owner ? &plasma->owner : NULL, 70, &other, "rogue:plasma", NULL);
  emit_colored_explosion(game, origin, 244, 3);
  int count = 0;
  size_t target_count = 0;
  qa_actor_id *targets = observations_reversed(game, &target_count);
  for (size_t i = 0; i < target_count; i++) {
    const qa_actor_id target = targets[i];
    const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, target);
    const qa_q1_actor *entity = qa_q1_get_entity(game, &target);
    if (!body.has_value || (plasma->has_owner && qa_same_actor(plasma->owner, target)) ||
        !(qa_q1_is_player(game, &target) || (qa_js_to_int32(entity != NULL ? entity->movement_flags : 0) & 32) != 0)) continue;
    const qa_vec3 center = qa_q1_vadd(body.value.origin, qa_q1_vscale(qa_q1_vadd(body.value.bounds.min, body.value.bounds.max), 0.5));
    if (qa_q1_length(qa_q1_vsub(center, origin)) > 320) continue;
    const qa_q1_trace trace = qa_q1mp_trace(game, origin, body.value.origin, NULL, false);
    if (trace.fraction != 1) continue;
    qa_q1_event beam;
    memset(&beam, 0, sizeof beam);
    beam.kind = "beam"; beam.beam_style = "lightning2"; beam.has_actor = true; beam.actor = target;
    beam.start = body.value.origin; beam.end = origin;
    game->host.emit(game->host.ctx, beam);
    qa_q1_sound(game, plasma->actor.id, "weapons/lhit.wav", "voice", 1, 1);
    plasma_damage(game, plasma, body.value.origin);
    if (++count == 5) break;
  }
  free(targets);
  qa_q1_remove(game, plasma);
}
static void rocket_explode(void *ctx, qa_q1_services *runtime, qa_q1_actor *rocket) { (void)ctx; explode_rocket(runtime, rocket, NULL); }
static void rocket_touch(void *ctx, qa_q1_services *runtime, qa_q1_actor *rocket, qa_actor_id other,
                         const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS (runtime, rocket, other) */
  if (rocket->has_owner && qa_same_actor(rocket->owner, other)) return;
  if (qa_q1mp_streq(runtime->host.contents(runtime->host.ctx, qa_q1_get_body(runtime, rocket).origin), "sky")) { qa_q1_remove(runtime, rocket); return; }
  explode_rocket(runtime, rocket, &other);
}
static void plasma_launch(void *ctx, qa_q1_services *runtime, qa_q1_actor *plasma) {
  (void)ctx;
  if (!controls_trajectory(runtime, plasma->actor.id))
    qa_q1mp_move_missile(runtime, plasma, qa_q1_vscale(qa_q1_normalize(qa_q1_get_body(runtime, plasma).velocity), 1250));
  qa_q1_schedule(runtime, plasma, 5, qa_q1_callback_registry_action(runtime->named, plasma, "SUB_Remove"));
}
void qa_q1mp_register_rogue_weapon_callbacks(qa_q1_services *game) {
  qa_q1_callback_registry *named = game->named;
  qa_q1_callback_registry_register(named, "rogue:lava-touch", (qa_q1_callback_handlers){.touch = lava_touch});
  qa_q1_callback_registry_register(named, "rogue:multi-grenade-touch", (qa_q1_callback_handlers){.touch = multi_grenade_touch});
  qa_q1_callback_registry_register(named, "rogue:multi-grenade-split", (qa_q1_callback_handlers){.action = split_grenade});
  qa_q1_callback_registry_register(named, "rogue:mini-grenade-explode", (qa_q1_callback_handlers){.action = mini_grenade_explode});
  qa_q1_callback_registry_register(named, "rogue:rocket-explode", (qa_q1_callback_handlers){.action = rocket_explode});
  qa_q1_callback_registry_register(named, "rogue:rocket-acquire", (qa_q1_callback_handlers){.action = acquire_rocket});
  qa_q1_callback_registry_register(named, "rogue:rocket-home", (qa_q1_callback_handlers){.action = home_rocket});
  qa_q1_callback_registry_register(named, "rogue:rocket-touch", (qa_q1_callback_handlers){.touch = rocket_touch});
  qa_q1_callback_registry_register(named, "rogue:plasma-touch", (qa_q1_callback_handlers){.touch = plasma_touch});
  qa_q1_callback_registry_register(named, "rogue:plasma-launch", (qa_q1_callback_handlers){.action = plasma_launch});
}
