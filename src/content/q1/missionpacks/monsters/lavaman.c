/* Port of src/content/q1/missionpacks/monsters/lavaman.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/lavaman.h"

#include <math.h>
#include <stdio.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/lavaman.h"
#include "monsters_internal.h"

static bool check_attack(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_monster_face(monster);
  qa_vec3 target;
  if (!qa_q1mpm_monster_target(monster, &target)) return false;
  qa_q1mpm_trace_request request = {
      qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, 64)), target, QA_Q1MPM_POINT,
      QA_Q1MPM_SOME(monster->entity->actor.id), true, false};
  qa_q1mpm_trace trace = monster->game->host->trace(monster->game->host->ctx, &request);
  if (!qa_q1mpm_opt_eq(trace.actor, monster->state.enemy) || (trace.in_open && trace.in_water) ||
      monster->game->time < monster->state.attack_finished)
    return false;
  qa_q1mpm_monster_play(monster, "lavaman_fire1");
  qa_q1mpm_monster_attack_finished(monster, 1 + monster->game->host->random(monster->game->host->ctx));
  return true;
}
static bool check_attack_hook(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  return check_attack(monster);
}
static void hunt(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  if (!monster->state.enemy.has || game->health(game, monster->state.enemy.id) <= 0) {
    qa_actor_id players[64];
    size_t count = game->host->players(game->host->ctx, players, 64);
    long index = -1;
    if (monster->state.enemy.has) {
      for (size_t i = 0; i < count; i++)
        if (qa_same_actor(players[i], monster->state.enemy.id)) { index = (long)i; break;
        }
    }
    qa_q1mpm_opt_actor candidate = index + 1 < (long)count ? QA_Q1MPM_SOME(players[index + 1]) : QA_Q1MPM_NONE;
    qa_q1mpm_body body;
    if (candidate.has && game->host->bodies_read(game->host->ctx, candidate.id, &body)) {
      qa_q1mpm_opt_actor ignore = game->world == NULL ? QA_Q1MPM_NONE : QA_Q1MPM_SOME(game->world->actor.id);
      qa_q1mpm_trace_request request = {
          qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, 96)), body.origin,
          QA_Q1MPM_POINT, ignore, false, false};
      if (game->host->trace(game->host->ctx, &request).fraction == 1) monster->state.enemy = candidate;
    }
  }
  if (monster->state.enemy.has) qa_q1mpm_monster_face(monster);
}
static void locomotion(qa_q1mpm_mission_monster *monster, qa_q1mpm_ai mode, double distance) {
  if (monster->state.enemy.has) check_attack(monster);
  else hunt(monster);
  if (mode == QA_Q1MPM_AI_WALK && monster->state.enemy.has) {
    qa_q1mpm_monster_find_target(monster);
    monster->game->host->move_to_goal(monster->game->host->ctx, monster->entity->actor,
                                      monster->state.enemy.id, distance, NULL);
    return;
  }
  qa_q1mpm_monster_ai(monster, mode, distance);
}
static void fire_at_side(qa_q1mpm_mission_monster *monster, double side) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1mpm_monster_target(monster, &target)) return;
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  qa_vec3 origin = qa_q1mpm_vadd(qa_q1mpm_vadd(qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster),
                                                             qa_q1mpm_vscale(basis.forward, 40)),
                                              qa_q1mpm_vscale(basis.right, side == 1 ? 65 : -75)),
                                 qa_q1mpm_vscale(basis.up, side == 1 ? 130 : 125));
  qa_vec3 direction = qa_q1mpm_normalize(qa_q1mpm_vsub(target, origin));
  double t = fmax(1, fmin(1.75, qa_q1mpm_length(qa_q1mpm_vsub(target, origin)) / 380));
  qa_vec3 velocity = qa_q1mpm_vadd(qa_q1mpm_vscale(direction, 600 * t), QA_Q1MPM_V3(0, 0, 200 * t));
  qa_q1mpm_actor *ball = qa_q1mpm_missile(game, entity->actor.id, "lavaman_ball", "progs/lavaball.mdl",
                                          origin, velocity, "rogue:lavaman_touch", 6);
  ball->movement = "bounce";
  ball->angular_velocity = QA_Q1MPM_V3(200, 100, 300);
  qa_q1mpm_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = qa_q1mpm_velocity_angles(direction);
  game->set_body(game, ball, &patch);
  game->sound(game, entity, "boss1/throw.wav", QA_Q1MPM_CH("weapon"), 1, 1);
  if (!monster->state.enemy.has || game->health(game, monster->state.enemy.id) <= 0)
    qa_q1mpm_monster_play(monster, "lavaman_idle1");
}
static void awake(qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor activator) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  entity->solid = "slidebox";
  entity->movement = "step";
  qa_q1mpm_actor_set_damageable(game, entity, true);
  entity->aimed_damage = true;
  entity->movement_flags |= 32;
  entity->ideal_yaw = game->body(game, entity).angles.y;
  double yaw_speed = qa_q1mpm_actor_number(entity, "yaw_speed");
  entity->yaw_speed = yaw_speed != 0 ? yaw_speed : 20;
  snprintf(entity->model, sizeof(entity->model), "%s", "progs/lavaman.mdl");
  qa_q1mpm_fields_set(entity, "view_ofs", "0 0 48");
  game->set_bounds(game, entity, qa_q1mpm_monster_spec(monster)->bounds);
  entity->max_health = 1250 + 250 * game->options.skill;
  game->host->combat_set_health(game->host->ctx, entity->actor, entity->max_health);
  entity->pain = game->named.pain(game->named.ctx, entity, "rogue:monster_pain");
  entity->die = game->named.die(game->named.ctx, entity, "rogue:monster_die");
  qa_q1mpm_opt_actor none = QA_Q1MPM_NONE;
  game->effect(game, "lava-splash", qa_q1mpm_monster_origin(monster), none, 1);
  if (activator.has && game->is_player(game, activator) &&
      qa_q1mpm_player_powerup(game->player(game, activator.id), "invisibility") <= game->time) {
    qa_q1mpm_actor *activator_entity = game->entity(game, activator);
    if (((activator_entity == NULL ? 0 : activator_entity->movement_flags) & 128) == 0)
      monster->state.enemy = activator;
  }
  qa_q1mpm_drop_to_floor(monster);
  game->monster_mission_spawned_started(game, entity->actor.id, 1);
  qa_q1mpm_monster_play(monster, "lavaman_rise1");
}
static void use_hook(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor activator) {
  (void)ctx;
  awake(monster, activator);
}
static void lavaman_touch(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity, qa_actor_id other,
                          bool has_normal, qa_vec3 normal, const void *surface) {
  (void)ctx; (void)has_normal; (void)normal; (void)surface;
  if (qa_q1mpm_id_eq_opt(other, entity->owner)) return;
  qa_q1mpm_body body = game->body(game, entity);
  if (qa_q1mpm_str_eq(game->host->contents(game->host->ctx, body.origin), "sky")) {
    game->remove(game, entity);
    return;
  }
  if (game->health(game, other) != 0)
    qa_q1mpm_game_damage(game, other, entity->actor.id, entity->owner,
                         qa_q1mpm_str_eq(game->host->classname(game->host->ctx, other), "monster_shambler") ? 20 : 40);
  game->radius_damage(game, entity->actor.id, entity->owner, 40, QA_Q1MPM_SOME(other), NULL, "");
  qa_q1mpm_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1mpm_vsub(body.origin, qa_q1mpm_vscale(qa_q1mpm_normalize(body.velocity), 8));
  game->set_body(game, entity, &patch);
  qa_q1mpm_opt_actor none = QA_Q1MPM_NONE;
  game->effect(game, "explosion", game->body(game, entity).origin, none, 1);
  game->effect(game, "explosion", game->body(game, entity).origin, none, 1);
  game->remove(game, entity);
}
static void stand_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; locomotion(monster, QA_Q1MPM_AI_STAND, 0); }
static void walk_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; locomotion(monster, QA_Q1MPM_AI_WALK, 2); }
static void run_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; locomotion(monster, QA_Q1MPM_AI_RUN, 2); }
static void missile1(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; fire_at_side(monster, 1); }
static void missile2(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; fire_at_side(monster, 2); }
static void death9(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->game->sound(monster->game, monster->entity, "boss1/out1.wav", QA_Q1MPM_CH("body"), 1, 1);
  qa_q1mpm_opt_actor none = QA_Q1MPM_NONE;
  monster->game->effect(monster->game, "lava-splash", qa_q1mpm_monster_origin(monster), none, 1);
}
static void death10(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->game->remove(monster->game, monster->entity);
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (!monster->game->monster_mission_spawned_started(monster->game, monster->entity->actor.id, 0))
    monster->game->total_monsters++;
  if ((monster->entity->spawnflags & 2) != 0) {
    monster->entity->use = monster->game->named.use(monster->game->named.ctx, monster->entity, "rogue:monster_use");
    return;
  }
  awake(monster, monster->entity->activator);
}
static void melee_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_play(monster, "lavaman_fire1");
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  if (monster->state.pain_finished > monster->game->time ||
      monster->game->host->random(monster->game->host->ctx) >= 0.05)
    return;
  monster->state.pain_finished = monster->game->time + 2;
  qa_q1mpm_monster_play(monster, "lavaman_shocka1");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  (void)ctx; (void)attacker;
  qa_q1mpm_monster_play(monster, "lavaman_death1");
}
static const char *const classnames[] = {"monster_lava_man"};
static const qa_q1mpm_named_action actions[] = {
    {"lavaman_stand", {stand_action, NULL}},
    {"lavaman_walk", {walk_action, NULL}},
    {"lavaman_run", {run_action, NULL}},
    {"lavaman_missile(1)", {missile1, NULL}},
    {"lavaman_missile(2)", {missile2, NULL}},
    {"lavaman:lavaman_death9", {death9, NULL}},
    {"lavaman:lavaman_death10", {death10, NULL}},
};
static const qa_q1mpm_named_handlers callbacks[] = {
    {"lavaman_touch", {NULL, NULL, lavaman_touch, NULL, NULL, NULL}},
};
const qa_q1mpm_pack_monster_definition qa_q1mpm_lavaman_definition = {
    .spec = {"lava-man", NULL, classnames, 1, "lavaman", NULL, 1500, -INFINITY, NULL, 0,
             {{-32, -32, -24}, {32, 32, 64}}, "lavaman_idle1", "lavaman_walk1", "lavaman_walk1",
             "", "lavaman_fire1", true, "walk"},
    .frames = {qa_q1mpm_tables_lavaman_entries, QA_Q1MPM_TABLES_LAVAMAN_COUNT},
    .actions = actions,
    .action_count = sizeof(actions) / sizeof(actions[0]),
    .callbacks = callbacks,
    .callback_count = sizeof(callbacks) / sizeof(callbacks[0]),
    .spawn = {spawn, NULL},
    .pain = pain,
    .die = die,
    .melee = {melee_action, NULL},
    .check_attack = check_attack_hook,
    .use = use_hook,
};
