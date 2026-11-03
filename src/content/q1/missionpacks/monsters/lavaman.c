/* Port of src/content/q1/missionpacks/monsters/lavaman.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/lavaman.h"

#include <math.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/lavaman.h"
#include "monsters_internal.h"
#include "qa/foundation/js.h"

static bool check_attack(qa_q1mpm_mission_monster *monster) {
  qa_q1_base_monster_face(&monster->base);
  qa_vec3 target;
  if (!qa_q1_base_monster_target(&monster->base, &target)) return false;
  const qa_q1_trace trace = q1mpm_trace(monster->base.game, qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 64}),
                                        target, QA_Q1_POINT, &monster->base.entity->actor.id, true);
  if (!q1mpm_same_opt(trace.has_actor ? &trace.actor : NULL, qa_q1_base_monster_enemy(&monster->base)) ||
      (trace.in_open && trace.in_water) ||
      monster->base.game->time < monster->base.state->attack_finished)
    return false;
  qa_q1_base_monster_play(&monster->base, "lavaman_fire1");
  qa_q1_base_monster_attack_finished(&monster->base, 1 + qa_q1_host_random(monster->base.game));
  return true;
}
static bool check_attack_hook(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  return check_attack(monster);
}
static void hunt(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  if (!monster->base.state->has_enemy || qa_q1_health(game, monster->base.state->enemy) <= 0) {
    size_t count = 0;
    const qa_actor_id *players = game->host.players(game->host.ctx, &count);
    /* TS players.indexOf(monster.enemy): -1 when absent. */
    size_t next = 0;
    if (monster->base.state->has_enemy) {
      for (size_t i = 0; i < count; i++)
        if (qa_same_actor(players[i], monster->base.state->enemy)) { next = i + 1; break; }
    }
    if (next < count) {
      const qa_actor_id candidate = players[next];
      qa_q1_body body;
      if (q1mpm_read_body(game, candidate, &body) &&
          q1mpm_trace(game, qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 96}), body.origin,
                      QA_Q1_POINT, game->world == NULL ? NULL : &game->world->actor.id, false).fraction == 1)
        qa_q1_base_monster_set_enemy(&monster->base, &candidate);
    }
  }
  if (monster->base.state->has_enemy) qa_q1_base_monster_face(&monster->base);
}
static void locomotion(qa_q1mpm_mission_monster *monster, qa_q1_monster_ai mode, double distance) {
  if (monster->base.state->has_enemy) (void)check_attack(monster);
  else hunt(monster);
  if (mode == QA_Q1_AI_WALK && monster->base.state->has_enemy) {
    (void)qa_q1_base_monster_find_target(&monster->base);
    monster->base.game->host.move_to_goal(monster->base.game->host.ctx, monster->base.entity->actor,
                                      monster->base.state->enemy, distance, NULL);
    return;
  }
  qa_q1_base_monster_ai(&monster->base, mode, distance);
}
static void fire_at_side(qa_q1mpm_mission_monster *monster, double side) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(&monster->base, &target)) return;
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_vec3 origin = qa_q1_vadd(qa_q1_vadd(qa_q1_vadd(qa_q1_base_monster_origin(&monster->base),
                                                             qa_q1_vscale(basis.forward, 40)),
                                              qa_q1_vscale(basis.right, side == 1 ? 65 : -75)),
                                 qa_q1_vscale(basis.up, side == 1 ? 130 : 125));
  qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(target, origin));
  double t = qa_js_max(1, qa_js_min(1.75, qa_q1_length(qa_q1_vsub(target, origin)) / 380));
  qa_vec3 velocity = qa_q1_vadd(qa_q1_vscale(direction, 600 * t), (qa_vec3){0, 0, 200 * t});
  qa_q1_actor *ball = qa_q1mpm_missile(game, entity->actor.id, "lavaman_ball", "progs/lavaball.mdl",
                                          origin, velocity, "rogue:lavaman_touch", 6);
  ball->movement = "bounce";
  ball->angular_velocity = (qa_vec3){200, 100, 300};
  qa_q1_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = qa_q1mp_velocity_angles(direction);
  qa_q1_set_body(game, ball, &patch);
  qa_q1_sound(game, entity->actor.id, "boss1/throw.wav", "weapon", 1, 1);
  if (!monster->base.state->has_enemy || qa_q1_health(game, monster->base.state->enemy) <= 0)
    qa_q1_base_monster_play(&monster->base, "lavaman_idle1");
}
static void awake(qa_q1mpm_mission_monster *monster, const qa_actor_id *activator) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  entity->solid = "slidebox";
  entity->movement = "step";
  qa_q1_actor_set_damageable(entity, true);
  entity->aimed_damage = true;
  entity->movement_flags = qa_js_bit_or(entity->movement_flags, 32);
  entity->ideal_yaw = qa_q1_get_body(game, entity).angles.y;
  double yaw_speed = qa_q1_actor_number(entity, "yaw_speed");
  entity->yaw_speed = yaw_speed != 0 ? yaw_speed : 20;
  q1mpm_set_text(&entity->model, "progs/lavaman.mdl");
  (void)qa_q1_actor_field_set(entity, "view_ofs", "0 0 48");
  qa_q1_set_bounds(game, entity, monster->base.spec->bounds);
  entity->max_health = 1250 + 250 * qa_q1_option_skill(game);
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, entity->max_health);
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, "rogue:monster_pain");
  entity->die = qa_q1_callback_registry_die(game->named, entity, "rogue:monster_die");
  qa_q1_effect(game, "lava-splash", qa_q1_base_monster_origin(&monster->base), NULL, 1);
  if (activator != NULL && qa_q1_is_player(game, activator) &&
      q1mpm_player_powerup(game, *activator, "invisibility") <= game->time) {
    qa_q1_actor *activator_entity = qa_q1_get_entity(game, activator);
    if (qa_js_bit_and(activator_entity == NULL ? 0 : activator_entity->movement_flags, 128) == 0)
      qa_q1_base_monster_set_enemy(&monster->base, activator);
  }
  (void)qa_q1mpm_drop_to_floor(monster);
  const qa_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
  if (mission != NULL) mission->started(mission->ctx);
  qa_q1_base_monster_play(&monster->base, "lavaman_rise1");
}
static void use_hook(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *activator) {
  (void)ctx;
  awake(monster, activator);
}
static void lavaman_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if ((entity->has_owner && qa_same_actor(other, entity->owner))) return;
  qa_q1_body body = qa_q1_get_body(game, entity);
  if (q1mpm_str_eq(game->host.contents(game->host.ctx, body.origin), "sky")) {
    qa_q1_remove(game, entity);
    return;
  }
  if (qa_q1_health(game, other) != 0)
    (void)qa_q1_damage(game, other, entity->actor.id, (entity->has_owner ? &entity->owner : NULL), q1mpm_str_eq(game->host.classname(game->host.ctx, other), "monster_shambler") ? 20 : 40, NULL, NULL, NULL, NULL);
  qa_q1_radius_damage(game, entity->actor.id, entity->has_owner ? &entity->owner : NULL, 40, &other, NULL, NULL);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1_vsub(body.origin, qa_q1_vscale(qa_q1_normalize(body.velocity), 8));
  qa_q1_set_body(game, entity, &patch);
  qa_q1_sprite_explosion(game, entity);
}
static void stand_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; locomotion(monster, QA_Q1_AI_STAND, 0); }
static void walk_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; locomotion(monster, QA_Q1_AI_WALK, 2); }
static void run_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; locomotion(monster, QA_Q1_AI_RUN, 2); }
static void missile1(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; fire_at_side(monster, 1); }
static void missile2(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; fire_at_side(monster, 2); }
static void death9(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_sound(monster->base.game, monster->base.entity->actor.id, "boss1/out1.wav", "body", 1, 1);
  qa_q1_effect(monster->base.game, "lava-splash", qa_q1_base_monster_origin(&monster->base), NULL, 1);
}
static void death10(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_remove(monster->base.game, monster->base.entity);
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  const qa_monster_mission *mission = qa_q1_mission(monster->base.game, monster->base.entity->actor.id);
  if (mission == NULL) monster->base.game->total_monsters++;
  else mission->spawned(mission->ctx);
  if (qa_js_bit_and(monster->base.entity->spawnflags, 2) != 0) {
    monster->base.entity->use = qa_q1_callback_registry_use(monster->base.game->named, monster->base.entity, "rogue:monster_use");
    return;
  }
  awake(monster, monster->base.entity->has_activator ? &monster->base.entity->activator : NULL);
}
static void melee_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_play(&monster->base, "lavaman_fire1");
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  if (monster->base.state->pain_finished > monster->base.game->time ||
      qa_q1_host_random(monster->base.game) >= 0.05)
    return;
  monster->base.state->pain_finished = monster->base.game->time + 2;
  qa_q1_base_monster_play(&monster->base, "lavaman_shocka1");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker;
  qa_q1_base_monster_play(&monster->base, "lavaman_death1");
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
    {"lavaman_touch", {.touch = lavaman_touch}},
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
