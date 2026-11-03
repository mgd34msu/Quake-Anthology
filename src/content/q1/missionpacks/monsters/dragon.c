/* Port of src/content/q1/missionpacks/monsters/dragon.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/dragon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/dragon.h"
#include "monsters_internal.h"
#include "qa/foundation/js.h"

qa_q1_actor *qa_q1mpm_launch_dragon_fireball(qa_q1_services *game, qa_actor_id owner,
                                             qa_vec3 origin, qa_vec3 direction) {
  qa_q1_actor *source = qa_q1_get_entity(game, &owner);
  if (source != NULL) source->effects = qa_js_bit_or(source->effects, 2);
  qa_q1_actor *shot = qa_q1mpm_missile(game, owner, "fireball", "progs/fireball.mdl", origin,
                                          QA_Q1_ZERO, "rogue:FireballTouch", 6);
  qa_q1_body_patch velocity_patch = {0};
  velocity_patch.has_velocity = true;
  velocity_patch.velocity = qa_q1_vscale(direction, qa_q1_host_random(game) * 300 + 900);
  qa_q1_set_body(game, shot, &velocity_patch);
  shot->angular_velocity = (qa_vec3){0, 0, 300};
  qa_q1_body_patch angles_patch = {0};
  angles_patch.has_angles = true;
  angles_patch.angles = qa_q1mp_velocity_angles(qa_q1_get_body(game, shot).velocity);
  qa_q1_set_body(game, shot, &angles_patch);
  (void)qa_q1_actor_reference_set(shot, "enemy", q1mpm_state_enemy(source));
  return shot;
}
static qa_q1_actor *goal(qa_q1mpm_mission_monster *monster) {
  qa_actor_id movetarget;
  return qa_q1_get_entity(monster->base.game, q1mpm_reference(monster->base.entity, "movetarget", &movetarget));
}
static void stop_attack_impl(qa_q1mpm_mission_monster *monster) {
  if (qa_q1_actor_number(monster->base.entity, "dragonAttacking") == 0) return;
  qa_q1_services *game = monster->base.game;
  monster->base.state->attack_finished =
      game->time + qa_q1_host_random(game) * 2 + 4 - qa_q1_option_skill(game);
  qa_q1mpm_number(monster, "dragonAttacking", 0);
  qa_q1_actor *destination = goal(monster);
  qa_vec3 end = destination == NULL ? QA_Q1_ZERO : qa_q1_get_body(game, destination).origin;
  if (q1mpm_trace(game, qa_q1_base_monster_origin(&monster->base), end, QA_Q1_POINT,
                  game->world == NULL ? NULL : &game->world->actor.id, false).fraction != 1) {
    size_t count = 0;
    const qa_actor_id *players = game->host.players(game->host.ctx, &count);
    for (size_t i = 0; i < count; i++)
      qa_q1_message(game, &players[i], "Error: Dragon cannot get to next target!\n", false, NULL, 0);
  }
}
static void check_attack_impl(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  const char *attack = qa_q1_actor_field_get(entity, "dragon:missile");
  if (attack == NULL) attack = "";
  if (qa_q1_actor_number(entity, "dragonAttacking") == 1 || attack[0] == '\0' ||
      monster->base.state->attack_finished > game->time)
    return;
  if (monster->base.state->has_enemy && qa_q1_health(game, monster->base.state->enemy) < 0)
    qa_q1_base_monster_set_enemy(&monster->base, NULL);
  qa_q1_actor *enemy_entity = qa_q1_get_entity(game, qa_q1_base_monster_enemy(&monster->base));
  if (qa_js_bit_and(enemy_entity == NULL ? 0 : enemy_entity->movement_flags, 128) != 0) return;
  if (!monster->base.state->has_enemy) { (void)qa_q1_base_monster_find_target(&monster->base); return; }
  qa_vec3 target;
  if (!qa_q1_base_monster_target(&monster->base, &target)) return;
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(target, qa_q1_base_monster_origin(&monster->base)));
  if (qa_q1_dot(direction, basis.forward) <= 0.3 ||
      q1mpm_trace(game, qa_q1_base_monster_origin(&monster->base), target, QA_Q1_POINT,
                  game->world == NULL ? NULL : &game->world->actor.id, false).fraction != 1)
    return;
  qa_q1mpm_number(monster, "dragonAttacking", 1);
  q1mpm_set_next_frame(&monster->base, qa_q1_base_monster_distance(&monster->base) < 350 ? "dragon_melee1" : attack);
}
static double anglemod(double value) { return fmod(fmod(value, 360) + 360, 360); }
static void move_impl(qa_q1mpm_mission_monster *monster, double distance) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (qa_q1_health(game, entity->actor.id) < 1) { qa_q1_remove(game, entity); return; }
  if (qa_q1_actor_number(entity, "dragonAttacking") == 0) check_attack_impl(monster);
  const bool had_enemy = monster->base.state->has_enemy;
  const qa_actor_id previous_enemy = monster->base.state->enemy;
  qa_q1_actor *destination = goal(monster);
  qa_vec3 target;
  if (qa_q1_actor_number(entity, "dragonAttacking") == 0) {
    target = destination == NULL ? QA_Q1_ZERO : qa_q1_get_body(game, destination).origin;
    qa_q1_base_monster_set_enemy(&monster->base, destination == NULL ? NULL : &destination->actor.id);
  } else if (!qa_q1_base_monster_target(&monster->base, &target)) {
    target = QA_Q1_ZERO;
  }
  qa_vec3 origin = qa_q1_base_monster_origin(&monster->base);
  qa_vec3 direction = qa_q1_vsub(target, origin);
  qa_vec3 desired = qa_q1mp_velocity_angles(direction);
  qa_q1_body original_body = qa_q1_get_body(game, entity);
  qa_vec3 original = original_body.angles;
  double yaw = original.y, roll = original.z, offset = yaw - desired.y;
  if (offset != 0) {
    offset = 180 - yaw;
    double left = anglemod(desired.y + offset) - 180;
    double right = 180 - anglemod(desired.y + offset);
    if (left < 0) left = 360;
    else if (right < 0) right = 360;
    entity->yaw_speed = 10;
    if (right < 180) {
      yaw = entity->yaw_speed < right ? yaw - entity->yaw_speed : desired.y;
      if (right > 5) roll = qa_js_min(30, roll + 5);
    } else {
      yaw = entity->yaw_speed < right ? yaw + entity->yaw_speed : desired.y;
      if (left > 5) roll = qa_js_max(-30, roll - 5);
    }
  } else if (roll != 0) {
    if (roll < -5) roll += 5;
    else if (roll < 5) roll = 0;
    else if (roll > 5) roll -= 5;
  }
  qa_q1_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = original;
  patch.angles.y = yaw;
  patch.angles.z = roll;
  qa_q1_set_body(game, entity, &patch);
  if (direction.z > 5) qa_q1_set_origin(game, entity, qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 5}));
  else if (direction.z < -5) qa_q1_set_origin(game, entity, qa_q1_vsub(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 5}));
  qa_vec3 before = qa_q1_base_monster_origin(&monster->base);
  (void)game->host.walk_move(game->host.ctx, entity->actor, yaw, distance);
  qa_vec3 after = qa_q1_base_monster_origin(&monster->base);
  if (before.x == after.x && before.y == after.y && before.z == after.z) {
    qa_actor_id goal_id;
    qa_q1_actor *movement_goal = qa_q1_get_entity(game, q1mpm_reference(entity, "goalentity", &goal_id));
    if (movement_goal != NULL)
      game->host.move_to_goal(game->host.ctx, entity->actor, movement_goal->actor.id, distance, NULL);
  }
  qa_q1_base_monster_set_enemy(&monster->base, had_enemy ? &previous_enemy : NULL);
}
static void fire_impl(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_sound_defaults(game, entity->actor.id, "dragon/attack.wav");
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_vec3 origin = qa_q1_vadd(qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), qa_q1_vscale(basis.forward, 112)),
                                 qa_q1_vscale(basis.up, 32));
  bool plasma = qa_q1_host_random(game) > 0.66;
  double count = plasma ? (qa_q1_option_skill(game) > 1 ? 2 : 1)
                        : floor(qa_q1_host_random(game) * qa_q1_option_skill(game) + 0.5) + 1;
  while (count-- > 0) {
    double distortion = (qa_q1_host_random(game) - 0.5) * 0.25;
    qa_vec3 target;
    qa_vec3 target_or_zero = qa_q1_base_monster_target(&monster->base, &target) ? target : QA_Q1_ZERO;
    qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(target_or_zero, origin));
    qa_q1_basis spray = qa_q1_make_vectors(game, direction);
    qa_vec3 aim = qa_q1_vadd(direction, qa_q1_vscale(spray.right, distortion));
    if (plasma) (void)qa_q1mp_launch_rogue_plasma(game, entity->actor.id, origin, aim);
    else (void)qa_q1mpm_launch_dragon_fireball(game, entity->actor.id, origin, aim);
  }
}
static void tail_impl(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (!monster->base.state->has_enemy || !qa_q1_can_damage(game, monster->base.state->enemy, entity->actor.id)) return;
  move_impl(monster, 10);
  qa_vec3 target;
  if (!qa_q1_base_monster_target(&monster->base, &target)) return;
  qa_vec3 delta = qa_q1_vsub(target, qa_q1_base_monster_origin(&monster->base));
  if (qa_q1_length(delta) < 250) {
    (void)qa_q1_damage(game, monster->base.state->enemy, entity->actor.id, &entity->actor.id, 30, NULL, NULL, NULL, NULL);
    qa_owned_actor actor;
    const bool has_actor = qa_session_actor_registry_resolve_owned(game->host.actors, monster->base.state->enemy, &actor);
    const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, monster->base.state->enemy);
    if (has_actor && body.has_value) {
      qa_body_state next = body.value; /* TS { ...body, velocity: { ...vscale(normalize(delta), 500), z: 350 } } */
      next.velocity = qa_q1_vscale(qa_q1_normalize(delta), 500);
      next.velocity.z = 350;
      qa_shared_body_table_write(game->host.bodies, actor, next);
    }
  }
  stop_attack_impl(monster);
}
static void violent_death(qa_q1mpm_mission_monster *monster, double count) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  static const char *const models[] = {"gib1", "gib2", "gib3"};
  for (; count > 0; count -= 3) {
    for (size_t i = 0; i < 3; i++) {
      qa_q1_actor *gib = qa_q1_create(game, "gib", NULL, NULL);
      char path[64];
      snprintf(path, sizeof path, "progs/%s.mdl", models[i]);
      q1mpm_set_text(&gib->model, path);
      qa_vec3 velocity = qa_q1_vscale(qa_q1_get_body(game, entity).velocity, -1.25);
      qa_q1_basis basis = qa_q1_make_vectors(game, velocity);
      qa_q1_body_patch patch = {0};
      patch.has_origin = true;
      patch.origin = qa_q1_base_monster_origin(&monster->base);
      patch.has_bounds = true;
      patch.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
      patch.has_velocity = true;
      const qa_vec3 right = qa_q1_vadd(velocity, qa_q1_vscale(basis.right, qa_q1_host_random(game) * 300 - 150));
      patch.velocity = qa_q1_vadd(right, qa_q1_vscale(game->basis.up, qa_q1_host_random(game) * 300 - 150));
      qa_q1_set_body(game, gib, &patch);
      gib->movement = "bounce";
      const double spin_x = qa_q1_host_random(game) * 600;
      const double spin_y = qa_q1_host_random(game) * 600;
      gib->angular_velocity = (qa_vec3){spin_x, spin_y, qa_q1_host_random(game) * 600};
      q1mpm_set_number_text(gib, "ltime", game->time);
      const double delay = 10 + qa_q1_host_random(game) * 10;
      qa_q1_schedule(game, gib, delay, qa_q1_callback_registry_action(game->named, gib, "SUB_Remove"));
      qa_q1_link(game, gib);
    }
  }
}
static void finish_death(qa_q1mpm_mission_monster *monster, double count) {
  violent_death(monster, count);
  q1mpm_set_text(&monster->base.entity->target, "dragondoor");
  qa_q1_use_targets(monster->base.game, monster->base.entity,
                    monster->base.entity->has_activator ? &monster->base.entity->activator : NULL);
  qa_q1_remove(monster->base.game, monster->base.entity);
}
static void boom(qa_q1mpm_mission_monster *monster) {
  if (qa_q1_actor_number(monster->base.entity, "dragonDeathState") > 2) return;
  qa_q1mpm_number(monster, "dragonDeathState", 3);
  static const char *const models[] = {"drggib01", "drggib02", "drggib03"};
  for (size_t i = 0; i < 3; i++)
    (void)qa_q1_throw_gib(monster->base.game, qa_q1_base_monster_origin(&monster->base), models[i], -100);
  qa_q1_sound(monster->base.game, monster->base.entity->actor.id, "player/tornoff2.wav", "body", 0, 1);
  q1mpm_set_next_frame(&monster->base, "dragon_boom2");
  qa_q1_base_monster_delay(&monster->base, 0.1);
}
static void explode_impl(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (qa_q1_actor_number(entity, "dragonDeathState") > 1) return;
  if (qa_q1_length(qa_q1_get_body(game, entity).velocity) < 100 || qa_js_bit_and(entity->movement_flags, 16) != 0) {
    qa_q1mpm_number(monster, "dragonDeathState", 2);
    boom(monster);
    return;
  }
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_vec3 velocity = qa_q1_vsub(qa_q1_get_body(game, entity).velocity, qa_q1_vscale(basis.up, 40));
  qa_q1_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = velocity;
  qa_q1_set_body(game, entity, &patch);
  char x[40], y[40], z[40], text[128];
  qa_net_js_number(velocity.x, x, sizeof x);
  qa_net_js_number(velocity.y, y, sizeof y);
  qa_net_js_number(velocity.z, z, sizeof z);
  snprintf(text, sizeof text, "%s %s %s", x, y, z);
  (void)qa_q1_actor_field_set(entity, "dragonLastVelocity", text);
}
static void explode_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; explode_impl(monster); }
static void use_impl(qa_q1mpm_mission_monster *monster) {
  if (qa_q1_health(monster->base.game, monster->base.entity->actor.id) < 1) {
    monster->base.entity->use = (qa_q1_bound_use){0};
    return;
  }
  q1mpm_set_next_frame(&monster->base, "dragon_walk1");
  qa_q1_base_monster_delay(&monster->base, 0.1);
}
static void use_hook(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *activator) {
  (void)ctx; (void)activator;
  use_impl(monster);
}
static void stop_attack_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; stop_attack_impl(monster); }
static void tail_touch(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (!monster->base.state->has_enemy || !qa_q1_can_damage(game, monster->base.state->enemy, entity->actor.id)) return;
  qa_q1_base_monster_ai(&monster->base, QA_Q1_AI_CHARGE, 10);
  qa_vec3 target;
  if (qa_q1_base_monster_target(&monster->base, &target) &&
      qa_q1_length(qa_q1_vsub(target, qa_q1_base_monster_origin(&monster->base))) <= 150)
    (void)qa_q1_damage(game, monster->base.state->enemy, entity->actor.id, &entity->actor.id, qa_q1_host_random(game) * 30 + 30, NULL, NULL, NULL, NULL);
}
static void boom2(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = qa_q1_actor_vector(monster->base.entity, "dragonLastVelocity");
  qa_q1_set_body(monster->base.game, monster->base.entity, &patch);
  finish_death(monster, 15);
}
static void activate(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_actor_set_damageable(entity, true);
  entity->aimed_damage = true;
  entity->ideal_yaw = qa_q1_get_body(game, entity).angles.y;
  if (entity->yaw_speed == 0) entity->yaw_speed = 10;
  (void)qa_q1_actor_field_set(entity, "view_ofs", "0 0 25");
  entity->movement_flags = qa_js_bit_or(entity->movement_flags, 1 | 32);
  (void)game->host.walk_move(game->host.ctx, entity->actor, 0, 0);
  if (entity->target[0] != '\0') {
    qa_q1_actor *found[1];
    const qa_actor_id *target = qa_q1_find(game, entity->target, found, 1) > 0 ? &found[0]->actor.id : NULL;
    (void)qa_q1_actor_reference_set(entity, "movetarget", target);
    (void)qa_q1_actor_reference_set(entity, "goalentity", target);
  }
  if (entity->targetname[0] != '\0') {
    entity->use = qa_q1_callback_registry_use(game->named, entity, "rogue:monster_use");
    return;
  }
  use_impl(monster);
}
static void walk1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (qa_q1_actor_number(monster->base.entity, "dragonAttacking") != 0) stop_attack_impl(monster);
  (void)qa_q1_actor_field_set(monster->base.entity, "dragon:missile", "dragon_atk_a1");
  qa_q1mpm_number(monster, "dragonPainSequence", 1);
  move_impl(monster, 17);
  if (qa_q1_host_random(monster->base.game) < 0.2) {
    q1mpm_emit_sound(monster->base.game, monster->base.entity->actor.id, "dragon/active.wav", "voice", 0.6, 2);
  }
}
static void walk2(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  (void)qa_q1_actor_field_set(monster->base.entity, "dragon:missile", "");
  move_impl(monster, 17);
}
static void walk13(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  (void)qa_q1_actor_field_set(monster->base.entity, "dragon:missile", "");
  move_impl(monster, 17);
  qa_q1mpm_number(monster, "dragonPainSequence", 1);
}
static void death1_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (qa_q1_actor_number(entity, "dragonDeathState") > 0) return;
  qa_q1mpm_number(monster, "dragonDeathState", 1);
  entity->use = (qa_q1_bound_use){0};
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_q1_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = qa_q1_vsub(qa_q1_vscale(basis.forward, 300), qa_q1_vscale(basis.up, 40));
  patch.has_ground = true;
  patch.ground = NULL;
  qa_q1_set_body(game, entity, &patch);
  entity->movement_flags = qa_js_bit_and(entity->movement_flags, qa_js_bit_not(512));
  qa_q1_set_bounds(game, entity, qa_q1mpm_hull_bounds);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "rogue:dragon_squish");
  qa_q1_sound(game, entity->actor.id, "dragon/death.wav", "voice", 0, 1);
  qa_q1mpm_number(monster, "dragonAttacking", 0);
}
static void death21(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; finish_death(monster, 39); }
static const double DIST10 = 10, DIST12 = 12, DIST17 = 17;
static void move_dist(void *ctx, qa_q1mpm_mission_monster *monster) { move_impl(monster, *(const double *)ctx); }
typedef struct walk_entry { const char *attack; double pain; } walk_entry;
static const walk_entry WALK_B = {"b", 2}, WALK_C = {"c", 3}, WALK_D = {"d", 4}, WALK_E = {"e", 5}, WALK_F = {"f", 6};
static void walk_entry_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  const walk_entry *entry = ctx;
  char name[32];
  snprintf(name, sizeof(name), "dragon_atk_%s1", entry->attack);
  (void)qa_q1_actor_field_set(monster->base.entity, "dragon:missile", name);
  move_impl(monster, 17);
  qa_q1mpm_number(monster, "dragonPainSequence", entry->pain);
}
static void fireball_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (entity->has_owner && qa_same_actor(other, entity->owner)) return;
  const bool dragon = entity->has_owner &&
                      q1mpm_str_eq(game->host.classname(game->host.ctx, entity->owner), "monster_dragon");
  const qa_actor_id *ignore = dragon ? &entity->owner : game->world == NULL ? NULL : &game->world->actor.id;
  qa_q1_radius_damage(game, entity->actor.id, entity->has_owner ? &entity->owner : NULL, dragon ? 90 : 30, ignore, NULL, NULL);
  qa_q1_sound(game, entity->actor.id, "weapons/r_exp3.wav", "weapon", 1, 1);
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "colored-explosion";
  event.has_origin = true;
  event.origin = qa_q1_get_body(game, entity).origin;
  event.color_start = 228;
  event.color_length = 5;
  qa_q1_emit(game, event);
  qa_q1_remove(game, entity);
}
static void squish_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1mpm_mission_monster *monster = qa_q1mpm_runtime_require((qa_q1mpm_runtime *)ctx, entity);
  if (q1mpm_str_eq(game->host.classname(game->host.ctx, other), "player")) {
    q1mpm_set_text(&entity->classname, "monster_dragon_dead");
    (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, 200, NULL, NULL, NULL, NULL);
  }
  if (game->world != NULL && qa_same_actor(other, game->world->actor.id)) {
    qa_q1_body_patch patch = {0};
    patch.has_velocity = true;
    patch.velocity = QA_Q1_ZERO;
    qa_q1_set_body(game, entity, &patch);
    explode_impl(monster);
  }
}
static void corner_touch(void *ctx, qa_q1_services *game, qa_q1_actor *corner, qa_actor_id other,
                         const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  qa_q1_actor *entity = qa_q1_get_entity(game, &other);
  qa_actor_id current;
  /* TS stops for an absent (undefined) or null reference alike. */
  if (entity == NULL || !qa_q1_actor_reference_get(entity, "movetarget", &current) ||
      !qa_same_actor(current, corner->actor.id) || strcmp(entity->classname, "monster_dragon") != 0)
    return;
  qa_q1_actor *found[1];
  qa_q1_actor *target = qa_q1_find(game, corner->target, found, 1) > 0 ? found[0] : NULL;
  const qa_actor_id *target_id = target == NULL ? NULL : &target->actor.id;
  (void)qa_q1_actor_reference_set(entity, "movetarget", target_id);
  (void)qa_q1_actor_reference_set(entity, "goalentity", target_id);
  q1mpm_set_text(&entity->target, corner->target);
  if (target == NULL) qa_range_error("dragon_corner: no target found");
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1mpm_number(monster, "dragonInRoom", 1);
  qa_q1mpm_number(monster, "dragonInTransit", 0);
  qa_q1mpm_number(monster, "dragonAttacking", 0);
  qa_q1mpm_number(monster, "playerInRoom", 1);
  qa_q1mpm_number(monster, "playerInTransit", 0);
  entity->solid = "slidebox";
  entity->movement = "step";
  entity->yaw_speed = qa_q1_actor_number(entity, "yaw_speed");
  q1mpm_set_text(&entity->model, "progs/dragon.mdl");
  qa_q1_set_bounds(game, entity, monster->base.spec->bounds);
  entity->max_health = 3000 + 1000 * qa_q1_option_skill(game);
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, entity->max_health);
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, "rogue:monster_pain");
  entity->die = qa_q1_callback_registry_die(game->named, entity, "rogue:monster_die");
  qa_q1mpm_number(monster, "dragonPainSequence", 1);
  game->total_monsters++;
  q1mpm_set_next_frame(&monster->base, "dragon_activate");
  qa_q1_base_monster_delay(&monster->base, 0.1 - game->time);
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (monster->base.state->pain_finished > game->time || qa_q1_host_random(game) >= 0.25) return;
  stop_attack_impl(monster);
  qa_q1_sound_defaults(game, entity->actor.id, "dragon/pain.wav");
  monster->base.state->pain_finished = game->time + 2;
  double sequence = qa_q1_actor_number(entity, "dragonPainSequence");
  const char *name = sequence == 1 ? "A" : sequence == 2 ? "F" : sequence == 3 ? "E"
                     : sequence == 4 ? "D" : sequence == 5 ? "C" : sequence == 6 ? "B" : NULL;
  if (name != NULL) {
    char frame[32];
    snprintf(frame, sizeof(frame), "dragon_pain%s1", name);
    q1mpm_set_next_frame(&monster->base, frame);
  }
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker;
  qa_q1_base_monster_play(&monster->base, "dragon_death1");
}
static const char *const classnames[] = {"monster_dragon"};
static const qa_q1mpm_named_action actions[] = {
    {"dragon_stop_attack", {stop_attack_action, NULL}},
    {"dragon_fireball", {fire_impl, NULL}},
    {"dragon_tail", {tail_impl, NULL}},
    {"dragon_explode", {explode_action, NULL}},
    {"dragon_tail_touch", {tail_touch, NULL}},
    {"dragon_boom2", {boom2, NULL}},
    {"dragon_activate", {activate, NULL}},
    {"dragon:dragon_walk1", {walk1, NULL}},
    {"dragon:dragon_walk2", {walk2, NULL}},
    {"dragon:dragon_walk13", {walk13, NULL}},
    {"dragon:dragon_death1", {death1_action, NULL}},
    {"dragon:dragon_death21", {death21, NULL}},
    {"dragon_move(10)", {move_dist, (void *)&DIST10}},
    {"dragon_move(12)", {move_dist, (void *)&DIST12}},
    {"dragon_move(17)", {move_dist, (void *)&DIST17}},
    {"dragon:dragon_walk3", {walk_entry_action, (void *)&WALK_B}},
    {"dragon:dragon_walk5", {walk_entry_action, (void *)&WALK_C}},
    {"dragon:dragon_walk7", {walk_entry_action, (void *)&WALK_D}},
    {"dragon:dragon_walk9", {walk_entry_action, (void *)&WALK_E}},
    {"dragon:dragon_walk11", {walk_entry_action, (void *)&WALK_F}},
};
void qa_q1mpm_dragon_definition(qa_q1mpm_runtime *runtime, qa_q1mpm_pack_monster_definition *out) {
  *out = (qa_q1mpm_pack_monster_definition){0};
  out->spec = (qa_q1_base_monster_species){
      "dragon", NULL, classnames, 1, "dragon", NULL, 4000, -INFINITY, NULL, 0,
      {{-32, -32, -24}, {32, 32, 64}}, "dragon_walk1", "dragon_walk1", "dragon_walk1",
      "dragon/see.wav", NULL, false, "fly"};
  out->frames = (qa_q1_monster_frame_table){qa_q1mpm_tables_dragon_entries, QA_Q1MPM_TABLES_DRAGON_COUNT};
  out->actions = actions;
  out->action_count = sizeof(actions) / sizeof(actions[0]);
  qa_q1mpm_named_handlers *callbacks = qa_q1mpm_runtime_calloc(runtime, 3, sizeof *callbacks);
  callbacks[0].name = "FireballTouch";
  callbacks[0].handlers = (qa_q1_callback_handlers){.touch = fireball_touch};
  callbacks[1].name = "dragon_squish";
  callbacks[1].handlers = (qa_q1_callback_handlers){.ctx = runtime, .touch = squish_touch};
  callbacks[2].name = "dragon_corner_touch";
  callbacks[2].handlers = (qa_q1_callback_handlers){.touch = corner_touch};
  out->callbacks = callbacks;
  out->callback_count = 3;
  out->spawn = (qa_q1mpm_action){spawn, NULL};
  out->pain = pain;
  out->die = die;
  out->use = use_hook;
}
/* TS `(_game, entity) => ...` over the registering game (ctx). */
static void trigger_dragon_spawn(void *ctx, qa_q1_services *source, qa_q1_actor *entity) {
  (void)source;
  qa_q1_remove(ctx, entity);
}
static void corner_spawn(void *ctx, qa_q1_services *source, qa_q1_actor *entity) {
  (void)source;
  qa_q1_services *game = ctx;
  if (entity->targetname[0] == '\0') qa_range_error("dragon_corner: no targetname");
  entity->solid = "trigger";
  entity->movement = "none";
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "rogue:dragon_corner_touch");
  q1mpm_set_text(&entity->model, "");
  qa_bounds bounds = {{-16, -16, -16}, {16, 16, 16}};
  qa_q1_set_bounds(game, entity, bounds);
  qa_q1_link(game, entity);
}
void qa_q1mpm_register_dragon_corners(qa_q1_services *game) {
  qa_q1_register_spawn(game, "trigger_dragon", trigger_dragon_spawn, game);
  qa_q1_register_spawn(game, "dragon_corner", corner_spawn, game);
}
