/* Port of src/content/q1/missionpacks/monsters/dragon.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/dragon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/dragon.h"
#include "monsters_internal.h"

qa_q1mpm_actor *qa_q1mpm_launch_dragon_fireball(qa_q1mpm_game *game, qa_actor_id owner,
                                                qa_vec3 origin, qa_vec3 direction) {
  qa_q1mpm_actor *source = game->entity(game, QA_Q1MPM_SOME(owner));
  if (source != NULL) source->effects |= 2;
  qa_q1mpm_actor *shot = qa_q1mpm_missile(game, owner, "fireball", "progs/fireball.mdl", origin,
                                          QA_Q1MPM_ZERO, "rogue:FireballTouch", 6);
  qa_q1mpm_body_patch velocity_patch = {0};
  velocity_patch.has_velocity = true;
  velocity_patch.velocity = qa_q1mpm_vscale(direction, game->host->random(game->host->ctx) * 300 + 900);
  game->set_body(game, shot, &velocity_patch);
  shot->angular_velocity = QA_Q1MPM_V3(0, 0, 300);
  qa_q1mpm_body_patch angles_patch = {0};
  angles_patch.has_angles = true;
  angles_patch.angles = qa_q1mpm_velocity_angles(game->body(game, shot).velocity);
  game->set_body(game, shot, &angles_patch);
  qa_q1mpm_references_set(shot, "enemy",
                          source == NULL || source->monster == NULL ? QA_Q1MPM_NONE : source->monster->enemy);
  return shot;
}
static qa_q1mpm_actor *goal(qa_q1mpm_mission_monster *monster) {
  return monster->game->entity(monster->game, qa_q1mpm_references_get(monster->entity, "movetarget"));
}
static void stop_attack_impl(qa_q1mpm_mission_monster *monster) {
  if (qa_q1mpm_actor_number(monster->entity, "dragonAttacking") == 0) return;
  qa_q1mpm_game *game = monster->game;
  monster->state.attack_finished =
      game->time + game->host->random(game->host->ctx) * 2 + 4 - game->options.skill;
  qa_q1mpm_number(monster, "dragonAttacking", 0);
  qa_q1mpm_actor *destination = goal(monster);
  qa_vec3 end = destination == NULL ? QA_Q1MPM_ZERO : game->body(game, destination).origin;
  qa_q1mpm_opt_actor ignore = game->world == NULL ? QA_Q1MPM_NONE : QA_Q1MPM_SOME(game->world->actor.id);
  qa_q1mpm_trace_request request = {qa_q1mpm_monster_origin(monster), end, QA_Q1MPM_POINT, ignore, false, false};
  if (game->host->trace(game->host->ctx, &request).fraction != 1) {
    qa_actor_id players[64];
    size_t count = game->host->players(game->host->ctx, players, 64);
    for (size_t i = 0; i < count; i++)
      game->message(game, QA_Q1MPM_SOME(players[i]), "Error: Dragon cannot get to next target!\n", false);
  }
}
static void check_attack_impl(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  const char *attack = qa_q1mpm_fields_get(entity, "dragon:missile");
  if (attack == NULL) attack = "";
  if (qa_q1mpm_actor_number(entity, "dragonAttacking") == 1 || attack[0] == '\0' ||
      monster->state.attack_finished > game->time)
    return;
  if (monster->state.enemy.has && game->health(game, monster->state.enemy.id) < 0)
    monster->state.enemy = QA_Q1MPM_NONE;
  qa_q1mpm_actor *enemy_entity = game->entity(game, monster->state.enemy);
  if (((enemy_entity == NULL ? 0 : enemy_entity->movement_flags) & 128) != 0) return;
  if (!monster->state.enemy.has) { qa_q1mpm_monster_find_target(monster); return; }
  qa_vec3 target;
  if (!qa_q1mpm_monster_target(monster, &target)) return;
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  qa_vec3 direction = qa_q1mpm_normalize(qa_q1mpm_vsub(target, qa_q1mpm_monster_origin(monster)));
  qa_q1mpm_opt_actor ignore = game->world == NULL ? QA_Q1MPM_NONE : QA_Q1MPM_SOME(game->world->actor.id);
  qa_q1mpm_trace_request request = {qa_q1mpm_monster_origin(monster), target, QA_Q1MPM_POINT, ignore, false, false};
  if (qa_q1mpm_dot(direction, basis.forward) <= 0.3 || game->host->trace(game->host->ctx, &request).fraction != 1)
    return;
  qa_q1mpm_number(monster, "dragonAttacking", 1);
  qa_q1mpm_monster_set_next_frame(monster, qa_q1mpm_monster_distance(monster) < 350 ? "dragon_melee1" : attack);
}
static double anglemod(double value) { return fmod(fmod(value, 360) + 360, 360); }
static void move_impl(qa_q1mpm_mission_monster *monster, double distance) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (game->health(game, entity->actor.id) < 1) { game->remove(game, entity); return; }
  if (qa_q1mpm_actor_number(entity, "dragonAttacking") == 0) check_attack_impl(monster);
  qa_q1mpm_opt_actor previous_enemy = monster->state.enemy;
  qa_q1mpm_actor *destination = goal(monster);
  qa_vec3 target;
  if (qa_q1mpm_actor_number(entity, "dragonAttacking") == 0) {
    target = destination == NULL ? QA_Q1MPM_ZERO : game->body(game, destination).origin;
    monster->state.enemy = destination == NULL ? QA_Q1MPM_NONE : QA_Q1MPM_SOME(destination->actor.id);
  } else if (!qa_q1mpm_monster_target(monster, &target)) {
    target = QA_Q1MPM_ZERO;
  }
  qa_vec3 origin = qa_q1mpm_monster_origin(monster);
  qa_vec3 direction = qa_q1mpm_vsub(target, origin);
  qa_vec3 desired = qa_q1mpm_velocity_angles(direction);
  qa_q1mpm_body original_body = game->body(game, entity);
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
      if (right > 5) roll = fmin(30, roll + 5);
    } else {
      yaw = entity->yaw_speed < right ? yaw + entity->yaw_speed : desired.y;
      if (left > 5) roll = fmax(-30, roll - 5);
    }
  } else if (roll != 0) {
    if (roll < -5) roll += 5;
    else if (roll < 5) roll = 0;
    else if (roll > 5) roll -= 5;
  }
  qa_q1mpm_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = original;
  patch.angles.y = yaw;
  patch.angles.z = roll;
  game->set_body(game, entity, &patch);
  if (direction.z > 5) game->set_origin(game, entity, qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, 5)));
  else if (direction.z < -5) game->set_origin(game, entity, qa_q1mpm_vsub(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, 5)));
  qa_vec3 before = qa_q1mpm_monster_origin(monster);
  game->host->walk_move(game->host->ctx, entity->actor, yaw, distance);
  qa_vec3 after = qa_q1mpm_monster_origin(monster);
  if (before.x == after.x && before.y == after.y && before.z == after.z) {
    qa_q1mpm_actor *movement_goal = game->entity(game, qa_q1mpm_references_get(entity, "goalentity"));
    if (movement_goal != NULL)
      game->host->move_to_goal(game->host->ctx, entity->actor, movement_goal->actor.id, distance, NULL);
  }
  monster->state.enemy = previous_enemy;
}
static void fire_impl(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_game_sound(game, entity, "dragon/attack.wav");
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  qa_vec3 origin = qa_q1mpm_vadd(qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), qa_q1mpm_vscale(basis.forward, 112)),
                                 qa_q1mpm_vscale(basis.up, 32));
  bool plasma = game->host->random(game->host->ctx) > 0.66;
  double count = plasma ? (game->options.skill > 1 ? 2 : 1)
                        : floor(game->host->random(game->host->ctx) * game->options.skill + 0.5) + 1;
  while (count-- > 0) {
    double distortion = (game->host->random(game->host->ctx) - 0.5) * 0.25;
    qa_vec3 target;
    qa_vec3 target_or_zero = qa_q1mpm_monster_target(monster, &target) ? target : QA_Q1MPM_ZERO;
    qa_vec3 direction = qa_q1mpm_normalize(qa_q1mpm_vsub(target_or_zero, origin));
    qa_q1mpm_basis spray = game->make_vectors(game, direction);
    qa_vec3 aim = qa_q1mpm_vadd(direction, qa_q1mpm_vscale(spray.right, distortion));
    if (plasma) qa_q1mpm_ext_launch_rogue_plasma(game, entity->actor.id, origin, aim);
    else qa_q1mpm_launch_dragon_fireball(game, entity->actor.id, origin, aim);
  }
}
static void tail_impl(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (!monster->state.enemy.has || !game->can_damage(game, monster->state.enemy.id, entity->actor.id)) return;
  move_impl(monster, 10);
  qa_vec3 target;
  if (!qa_q1mpm_monster_target(monster, &target)) return;
  qa_vec3 delta = qa_q1mpm_vsub(target, qa_q1mpm_monster_origin(monster));
  if (qa_q1mpm_length(delta) < 250) {
    qa_q1mpm_game_damage(game, monster->state.enemy.id, entity->actor.id, QA_Q1MPM_SOME(entity->actor.id), 30);
    qa_owned_actor actor;
    qa_q1mpm_body body;
    if (game->host->actors_resolve_owned(game->host->ctx, monster->state.enemy.id, &actor) &&
        game->host->bodies_read(game->host->ctx, monster->state.enemy.id, &body)) {
      body.velocity = qa_q1mpm_vscale(qa_q1mpm_normalize(delta), 500);
      body.velocity.z = 350;
      game->host->bodies_write(game->host->ctx, actor, &body);
    }
  }
  stop_attack_impl(monster);
}
static void violent_death(qa_q1mpm_mission_monster *monster, double count) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  static const char *const models[] = {"gib1", "gib2", "gib3"};
  for (; count > 0; count -= 3) {
    for (size_t i = 0; i < 3; i++) {
      qa_q1mpm_actor *gib = game->create(game, "gib");
      snprintf(gib->model, sizeof(gib->model), "progs/%s.mdl", models[i]);
      qa_vec3 velocity = qa_q1mpm_vscale(game->body(game, entity).velocity, -1.25);
      qa_q1mpm_basis basis = game->make_vectors(game, velocity);
      qa_q1mpm_body_patch patch = {0};
      patch.has_origin = true;
      patch.origin = qa_q1mpm_monster_origin(monster);
      patch.has_bounds = true;
      patch.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
      patch.has_velocity = true;
      const qa_q1mpm_host *host = game->host;
      patch.velocity = qa_q1mpm_vadd(qa_q1mpm_vadd(velocity, qa_q1mpm_vscale(basis.right, host->random(host->ctx) * 300 - 150)),
                                     qa_q1mpm_vscale(game->basis.up, host->random(host->ctx) * 300 - 150));
      game->set_body(game, gib, &patch);
      gib->movement = "bounce";
      gib->angular_velocity = QA_Q1MPM_V3(host->random(host->ctx) * 600, host->random(host->ctx) * 600,
                                          host->random(host->ctx) * 600);
      char buf[32];
      qa_q1mpm_format_number(buf, sizeof(buf), game->time);
      qa_q1mpm_fields_set(gib, "ltime", buf);
      game->schedule(game, gib, 10 + host->random(host->ctx) * 10,
                     game->named.action(game->named.ctx, gib, "SUB_Remove"));
      game->link(game, gib);
    }
  }
}
static void finish_death(qa_q1mpm_mission_monster *monster, double count) {
  violent_death(monster, count);
  snprintf(monster->entity->target, sizeof(monster->entity->target), "%s", "dragondoor");
  monster->game->use_targets(monster->game, monster->entity, monster->entity->activator);
  monster->game->remove(monster->game, monster->entity);
}
static void boom(qa_q1mpm_mission_monster *monster) {
  if (qa_q1mpm_actor_number(monster->entity, "dragonDeathState") > 2) return;
  qa_q1mpm_number(monster, "dragonDeathState", 3);
  static const char *const models[] = {"drggib01", "drggib02", "drggib03"};
  for (size_t i = 0; i < 3; i++)
    qa_q1mpm_ext_throw_gib(monster->game, qa_q1mpm_monster_origin(monster), models[i], -100);
  monster->game->sound(monster->game, monster->entity, "player/tornoff2.wav", QA_Q1MPM_CH("body"), 0, 1);
  qa_q1mpm_monster_set_next_frame(monster, "dragon_boom2");
  qa_q1mpm_monster_delay(monster, 0.1);
}
static void explode_impl(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (qa_q1mpm_actor_number(entity, "dragonDeathState") > 1) return;
  if (qa_q1mpm_length(game->body(game, entity).velocity) < 100 || (entity->movement_flags & 16) != 0) {
    qa_q1mpm_number(monster, "dragonDeathState", 2);
    boom(monster);
    return;
  }
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  qa_vec3 velocity = qa_q1mpm_vsub(game->body(game, entity).velocity, qa_q1mpm_vscale(basis.up, 40));
  qa_q1mpm_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = velocity;
  game->set_body(game, entity, &patch);
  char buf[128];
  char fx[32], fy[32], fz[32];
  qa_q1mpm_format_number(fx, sizeof(fx), velocity.x);
  qa_q1mpm_format_number(fy, sizeof(fy), velocity.y);
  qa_q1mpm_format_number(fz, sizeof(fz), velocity.z);
  snprintf(buf, sizeof(buf), "%s %s %s", fx, fy, fz);
  qa_q1mpm_fields_set(entity, "dragonLastVelocity", buf);
}
static void explode_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; explode_impl(monster); }
static void use_impl(qa_q1mpm_mission_monster *monster) {
  if (monster->game->health(monster->game, monster->entity->actor.id) < 1) {
    monster->entity->use = (qa_q1mpm_callback){NULL, NULL, QA_Q1MPM_CB_USE};
    return;
  }
  qa_q1mpm_monster_set_next_frame(monster, "dragon_walk1");
  qa_q1mpm_monster_delay(monster, 0.1);
}
static void use_hook(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor activator) {
  (void)ctx; (void)activator;
  use_impl(monster);
}
static void stop_attack_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; stop_attack_impl(monster); }
static void tail_touch(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (!monster->state.enemy.has || !game->can_damage(game, monster->state.enemy.id, entity->actor.id)) return;
  qa_q1mpm_monster_ai(monster, QA_Q1MPM_AI_CHARGE, 10);
  qa_vec3 target;
  if (qa_q1mpm_monster_target(monster, &target) &&
      qa_q1mpm_length(qa_q1mpm_vsub(target, qa_q1mpm_monster_origin(monster))) <= 150)
    qa_q1mpm_game_damage(game, monster->state.enemy.id, entity->actor.id, QA_Q1MPM_SOME(entity->actor.id),
                         game->host->random(game->host->ctx) * 30 + 30);
}
static void boom2(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = qa_q1mpm_actor_vector(monster->entity, "dragonLastVelocity");
  monster->game->set_body(monster->game, monster->entity, &patch);
  finish_death(monster, 15);
}
static void activate(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_actor_set_damageable(game, entity, true);
  entity->aimed_damage = true;
  entity->ideal_yaw = game->body(game, entity).angles.y;
  if (entity->yaw_speed == 0) entity->yaw_speed = 10;
  qa_q1mpm_fields_set(entity, "view_ofs", "0 0 25");
  entity->movement_flags |= 1 | 32;
  game->host->walk_move(game->host->ctx, entity->actor, 0, 0);
  if (entity->target[0] != '\0') {
    qa_q1mpm_actor *found[1];
    size_t count = game->find(game, entity->target, found, 1);
    qa_q1mpm_opt_actor target = count > 0 ? QA_Q1MPM_SOME(found[0]->actor.id) : QA_Q1MPM_NONE;
    qa_q1mpm_references_set(entity, "movetarget", target);
    qa_q1mpm_references_set(entity, "goalentity", target);
  }
  if (entity->targetname[0] != '\0') {
    entity->use = game->named.use(game->named.ctx, entity, "rogue:monster_use");
    return;
  }
  use_impl(monster);
}
static void walk1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (qa_q1mpm_actor_number(monster->entity, "dragonAttacking") != 0) stop_attack_impl(monster);
  qa_q1mpm_fields_set(monster->entity, "dragon:missile", "dragon_atk_a1");
  qa_q1mpm_number(monster, "dragonPainSequence", 1);
  move_impl(monster, 17);
  if (monster->game->host->random(monster->game->host->ctx) < 0.2) {
    qa_q1mpm_event event = {0};
    event.kind = "sound";
    event.actor = QA_Q1MPM_SOME(monster->entity->actor.id);
    event.path = "dragon/active.wav";
    event.channel = QA_Q1MPM_CH("voice");
    event.volume = 0.6;
    event.attenuation = 2;
    monster->game->host->emit(monster->game->host->ctx, &event);
  }
}
static void walk2(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_fields_set(monster->entity, "dragon:missile", "");
  move_impl(monster, 17);
}
static void walk13(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_fields_set(monster->entity, "dragon:missile", "");
  move_impl(monster, 17);
  qa_q1mpm_number(monster, "dragonPainSequence", 1);
}
static void death1_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (qa_q1mpm_actor_number(entity, "dragonDeathState") > 0) return;
  qa_q1mpm_number(monster, "dragonDeathState", 1);
  entity->use = (qa_q1mpm_callback){NULL, NULL, QA_Q1MPM_CB_USE};
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  qa_q1mpm_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = qa_q1mpm_vsub(qa_q1mpm_vscale(basis.forward, 300), qa_q1mpm_vscale(basis.up, 40));
  patch.has_ground = true;
  patch.ground = QA_Q1MPM_NONE;
  game->set_body(game, entity, &patch);
  entity->movement_flags &= ~512;
  game->set_bounds(game, entity, qa_q1mpm_hull_bounds);
  entity->touch = game->named.touch(game->named.ctx, entity, "rogue:dragon_squish");
  game->sound(game, entity, "dragon/death.wav", QA_Q1MPM_CH("voice"), 0, 1);
  qa_q1mpm_number(monster, "dragonAttacking", 0);
}
static void death21(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; finish_death(monster, 39); }
static double dist10 = 10, dist12 = 12, dist17 = 17;
static void move_dist(void *ctx, qa_q1mpm_mission_monster *monster) { move_impl(monster, *(const double *)ctx); }
typedef struct walk_entry { const char *attack; double pain; } walk_entry;
static walk_entry walk_b = {"b", 2}, walk_c = {"c", 3}, walk_d = {"d", 4}, walk_e = {"e", 5}, walk_f = {"f", 6};
static void walk_entry_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  const walk_entry *entry = ctx;
  char name[32];
  snprintf(name, sizeof(name), "dragon_atk_%s1", entry->attack);
  qa_q1mpm_fields_set(monster->entity, "dragon:missile", name);
  move_impl(monster, 17);
  qa_q1mpm_number(monster, "dragonPainSequence", entry->pain);
}
static void fireball_touch(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity, qa_actor_id other,
                           bool has_normal, qa_vec3 normal, const void *surface) {
  (void)ctx; (void)has_normal; (void)normal; (void)surface;
  if (entity->owner.has && qa_same_actor(other, entity->owner.id)) return;
  bool dragon = entity->owner.has &&
                qa_q1mpm_str_eq(game->host->classname(game->host->ctx, entity->owner.id), "monster_dragon");
  qa_q1mpm_opt_actor ignore = dragon ? entity->owner
                                     : game->world == NULL ? QA_Q1MPM_NONE
                                                           : QA_Q1MPM_SOME(game->world->actor.id);
  game->radius_damage(game, entity->actor.id, entity->owner, dragon ? 90 : 30, ignore, NULL, "");
  game->sound(game, entity, "weapons/r_exp3.wav", QA_Q1MPM_CH("weapon"), 1, 1);
  qa_q1mpm_event event = {0};
  event.kind = "colored-explosion";
  event.has_origin = true;
  event.origin = game->body(game, entity).origin;
  event.color_start = 228;
  event.color_length = 5;
  game->host->emit(game->host->ctx, &event);
  game->remove(game, entity);
}
static void squish_touch(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity, qa_actor_id other,
                         bool has_normal, qa_vec3 normal, const void *surface) {
  (void)has_normal; (void)normal; (void)surface;
  qa_q1mpm_mission_monster *monster = qa_q1mpm_runtime_require((qa_q1mpm_runtime *)ctx, entity);
  if (qa_q1mpm_str_eq(game->host->classname(game->host->ctx, other), "player")) {
    snprintf(entity->classname, sizeof(entity->classname), "%s", "monster_dragon_dead");
    qa_q1mpm_game_damage(game, other, entity->actor.id, QA_Q1MPM_SOME(entity->actor.id), 200);
  }
  if (game->world != NULL && qa_same_actor(other, game->world->actor.id)) {
    qa_q1mpm_body_patch patch = {0};
    patch.has_velocity = true;
    patch.velocity = QA_Q1MPM_ZERO;
    game->set_body(game, entity, &patch);
    explode_impl(monster);
  }
}
static void corner_touch(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *corner, qa_actor_id other,
                         bool has_normal, qa_vec3 normal, const void *surface) {
  (void)ctx; (void)has_normal; (void)normal; (void)surface;
  qa_q1mpm_actor *entity = game->entity(game, QA_Q1MPM_SOME(other));
  if (entity == NULL) return;
  /* TS also stops when the reference is absent (undefined); the
   * mirrored references_get returns "none" for both cases. */
  qa_q1mpm_opt_actor current = qa_q1mpm_references_get(entity, "movetarget");
  if (!current.has || !qa_same_actor(current.id, corner->actor.id) ||
      strcmp(entity->classname, "monster_dragon") != 0)
    return;
  qa_q1mpm_actor *found[1];
  size_t count = game->find(game, corner->target, found, 1);
  qa_q1mpm_actor *target = count > 0 ? found[0] : NULL;
  qa_q1mpm_opt_actor target_id = target == NULL ? QA_Q1MPM_NONE : QA_Q1MPM_SOME(target->actor.id);
  qa_q1mpm_references_set(entity, "movetarget", target_id);
  qa_q1mpm_references_set(entity, "goalentity", target_id);
  snprintf(entity->target, sizeof(entity->target), "%s", corner->target);
  if (target == NULL) qa_range_error("dragon_corner: no target found");
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_number(monster, "dragonInRoom", 1);
  qa_q1mpm_number(monster, "dragonInTransit", 0);
  qa_q1mpm_number(monster, "dragonAttacking", 0);
  qa_q1mpm_number(monster, "playerInRoom", 1);
  qa_q1mpm_number(monster, "playerInTransit", 0);
  entity->solid = "slidebox";
  entity->movement = "step";
  entity->yaw_speed = qa_q1mpm_actor_number(entity, "yaw_speed");
  snprintf(entity->model, sizeof(entity->model), "%s", "progs/dragon.mdl");
  game->set_bounds(game, entity, qa_q1mpm_monster_spec(monster)->bounds);
  entity->max_health = 3000 + 1000 * game->options.skill;
  game->host->combat_set_health(game->host->ctx, entity->actor, entity->max_health);
  entity->pain = game->named.pain(game->named.ctx, entity, "rogue:monster_pain");
  entity->die = game->named.die(game->named.ctx, entity, "rogue:monster_die");
  qa_q1mpm_number(monster, "dragonPainSequence", 1);
  game->total_monsters++;
  qa_q1mpm_monster_set_next_frame(monster, "dragon_activate");
  qa_q1mpm_monster_delay(monster, 0.1 - game->time);
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (monster->state.pain_finished > game->time || game->host->random(game->host->ctx) >= 0.25) return;
  stop_attack_impl(monster);
  qa_q1mpm_game_sound(game, entity, "dragon/pain.wav");
  monster->state.pain_finished = game->time + 2;
  double sequence = qa_q1mpm_actor_number(entity, "dragonPainSequence");
  const char *name = sequence == 1 ? "A" : sequence == 2 ? "F" : sequence == 3 ? "E"
                     : sequence == 4 ? "D" : sequence == 5 ? "C" : sequence == 6 ? "B" : NULL;
  if (name != NULL) {
    char frame[32];
    snprintf(frame, sizeof(frame), "dragon_pain%s1", name);
    qa_q1mpm_monster_set_next_frame(monster, frame);
  }
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  (void)ctx; (void)attacker;
  qa_q1mpm_monster_play(monster, "dragon_death1");
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
    {"dragon_move(10)", {move_dist, &dist10}},
    {"dragon_move(12)", {move_dist, &dist12}},
    {"dragon_move(17)", {move_dist, &dist17}},
    {"dragon:dragon_walk3", {walk_entry_action, &walk_b}},
    {"dragon:dragon_walk5", {walk_entry_action, &walk_c}},
    {"dragon:dragon_walk7", {walk_entry_action, &walk_d}},
    {"dragon:dragon_walk9", {walk_entry_action, &walk_e}},
    {"dragon:dragon_walk11", {walk_entry_action, &walk_f}},
};
void qa_q1mpm_dragon_definition(qa_q1mpm_runtime *runtime, qa_q1mpm_pack_monster_definition *out) {
  *out = (qa_q1mpm_pack_monster_definition){0};
  out->spec = (qa_q1mpm_species_spec){
      "dragon", NULL, classnames, 1, "dragon", NULL, 4000, -INFINITY, NULL, 0,
      {{-32, -32, -24}, {32, 32, 64}}, "dragon_walk1", "dragon_walk1", "dragon_walk1",
      "dragon/see.wav", NULL, false, "fly"};
  out->frames = (qa_q1mpm_frame_table){qa_q1mpm_tables_dragon_entries, QA_Q1MPM_TABLES_DRAGON_COUNT};
  out->actions = actions;
  out->action_count = sizeof(actions) / sizeof(actions[0]);
  qa_q1mpm_named_handlers *callbacks = calloc(3, sizeof(*callbacks));
  if (callbacks != NULL) {
    callbacks[0].name = "FireballTouch";
    callbacks[0].handlers = (qa_q1mpm_handlers){NULL, NULL, fireball_touch, NULL, NULL, NULL};
    callbacks[1].name = "dragon_squish";
    callbacks[1].handlers = (qa_q1mpm_handlers){runtime, NULL, squish_touch, NULL, NULL, NULL};
    callbacks[2].name = "dragon_corner_touch";
    callbacks[2].handlers = (qa_q1mpm_handlers){NULL, NULL, corner_touch, NULL, NULL, NULL};
    out->callbacks = callbacks;
    out->callback_count = 3;
  }
  out->spawn = (qa_q1mpm_action){spawn, NULL};
  out->pain = pain;
  out->die = die;
  out->use = use_hook;
}
static void trigger_dragon_spawn(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity) {
  (void)ctx;
  game->remove(game, entity);
}
static void corner_spawn(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity) {
  (void)ctx;
  if (entity->targetname[0] == '\0') qa_range_error("dragon_corner: no targetname");
  entity->solid = "trigger";
  entity->movement = "none";
  entity->touch = game->named.touch(game->named.ctx, entity, "rogue:dragon_corner_touch");
  entity->model[0] = '\0';
  qa_bounds bounds = {{-16, -16, -16}, {16, 16, 16}};
  game->set_bounds(game, entity, bounds);
  game->link(game, entity);
}
void qa_q1mpm_register_dragon_corners(qa_q1mpm_game *game) {
  game->register_spawn(game, "trigger_dragon", trigger_dragon_spawn, game);
  game->register_spawn(game, "dragon_corner", corner_spawn, game);
}
