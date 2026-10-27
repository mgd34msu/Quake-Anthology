/* Port of src/content/q1/missionpacks/monsters/wrath.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/wrath.h"

#include <stdio.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/wrath.h"
#include "monsters_internal.h"

void qa_q1mpm_wrath_missile(qa_q1mpm_mission_monster *monster, double attack) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1mpm_monster_target(monster, &target)) return;
  qa_vec3 origin0 = qa_q1mpm_monster_origin(monster);
  qa_vec3 direction = qa_q1mpm_normalize(qa_q1mpm_vsub(qa_q1mpm_vadd(target, QA_Q1MPM_V3(0, 0, 10)), origin0));
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  entity->effects |= 2;
  double forward = attack == 1 || attack == 4 ? 20 : attack == 2 ? 18 : 12;
  double up = attack == 4 ? 16 : attack == 2 ? 10 : 12;
  qa_vec3 origin = qa_q1mpm_vadd(qa_q1mpm_vadd(qa_q1mpm_vadd(origin0, qa_q1mpm_vscale(basis.forward, forward)),
                                              qa_q1mpm_vscale(basis.up, up)),
                                 qa_q1mpm_vscale(basis.right, attack == 3 ? 20 : 0));
  qa_q1mpm_actor *shot = qa_q1mpm_missile(game, entity->actor.id, "wrath_missile", "progs/w_ball.mdl",
                                          origin, qa_q1mpm_vscale(direction, 400), "rogue:WrathMissileTouch", 5);
  qa_q1mpm_references_set(shot, "enemy", monster->state.enemy);
  shot->angular_velocity = QA_Q1MPM_V3(300, 300, 300);
  game->schedule(game, shot, 0.1, game->named.action(game->named.ctx, shot, "rogue:WrathHome"));
  monster->state.attack_finished = game->time + 2;
}
static void wrath_home(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity) {
  (void)ctx;
  qa_q1mpm_opt_actor enemy = qa_q1mpm_references_get(entity, "enemy");
  if (!enemy.has || game->health(game, enemy.id) < 1) { game->remove(game, entity); return; }
  qa_vec3 target;
  if (qa_q1mpm_eye(game, enemy.id, &target)) {
    qa_q1mpm_body_patch patch = {0};
    patch.has_velocity = true;
    patch.velocity = qa_q1mpm_vscale(
        qa_q1mpm_normalize(qa_q1mpm_vsub(target, game->body(game, entity).origin)),
        game->options.skill == 3 ? 550 : 400);
    game->set_body(game, entity, &patch);
  }
  game->schedule(game, entity, 0.1, game->named.action(game->named.ctx, entity, "rogue:WrathHome"));
}
static void wrath_missile_touch(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity, qa_actor_id other,
                                 bool has_normal, qa_vec3 normal, const void *surface) {
  (void)ctx; (void)has_normal; (void)normal; (void)surface;
  const char *classname = game->host->classname(game->host->ctx, other);
  if (qa_q1mpm_id_eq_opt(other, entity->owner) || qa_q1mpm_str_eq(classname, "monster_wrath") ||
      qa_q1mpm_str_eq(classname, "monster_super_wrath")) {
    game->remove(game, entity);
    return;
  }
  if (qa_q1mpm_str_eq(classname, "monster_zombie"))
    qa_q1mpm_game_damage(game, other, entity->actor.id, QA_Q1MPM_SOME(entity->actor.id), 110);
  qa_q1mpm_opt_actor ignore = game->world == NULL ? QA_Q1MPM_NONE : QA_Q1MPM_SOME(game->world->actor.id);
  game->radius_damage(game, entity->actor.id, entity->owner, 20, ignore, NULL, "");
  game->sound(game, entity, "weapons/r_exp3.wav", QA_Q1MPM_CH("weapon"), 1, 1);
  qa_q1mpm_opt_actor none = QA_Q1MPM_NONE;
  game->effect(game, "explosion", game->body(game, entity).origin, none, 1);
  qa_q1mpm_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = QA_Q1MPM_ZERO;
  game->set_body(game, entity, &patch);
  entity->touch = (qa_q1mpm_callback){NULL, NULL, QA_Q1MPM_CB_TOUCH};
  snprintf(entity->model, sizeof(entity->model), "%s", "progs/s_explod.spr");
  entity->solid = "none";
  entity->frame = 0;
  game->link(game, entity);
  game->schedule(game, entity, 0.1, game->named.action(game->named.ctx, entity, "rogue:wrath_explode"));
}
static void wrath_explode(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity) {
  (void)ctx;
  entity->frame++;
  if (entity->frame > 5) game->remove(game, entity);
  else game->schedule(game, entity, 0.1, game->named.action(game->named.ctx, entity, "rogue:wrath_explode"));
}
const qa_q1mpm_named_handlers qa_q1mpm_wrath_callbacks[] = {
    {"WrathHome", {NULL, wrath_home, NULL, NULL, NULL, NULL}},
    {"WrathMissileTouch", {NULL, NULL, wrath_missile_touch, NULL, NULL, NULL}},
    {"wrath_explode", {NULL, wrath_explode, NULL, NULL, NULL, NULL}},
};
const size_t qa_q1mpm_wrath_callback_count = sizeof(qa_q1mpm_wrath_callbacks) / sizeof(qa_q1mpm_wrath_callbacks[0]);

static void wrath_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  double r = monster->game->host->random(monster->game->host->ctx);
  qa_q1mpm_monster_play(monster, r < 0.25 ? "wrath_at_a01" : r < 0.65 ? "wrath_at_b01" : "wrath_at_c01");
  qa_q1mpm_game_sound(monster->game, monster->entity, "wrath/watt.wav");
}
static void missile1(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; qa_q1mpm_wrath_missile(monster, 1); }
static void missile2(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; qa_q1mpm_wrath_missile(monster, 2); }
static void missile3(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; qa_q1mpm_wrath_missile(monster, 3); }
static void die15(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_vec3 origin = qa_q1mpm_monster_origin(monster);
  static const char *const models[] = {"wrthgib1", "wrthgib2", "wrthgib3"};
  for (size_t i = 0; i < 3; i++)
    qa_q1mpm_ext_throw_gib(game, origin, models[i], game->health(game, entity->actor.id));
  qa_q1mpm_opt_actor ignore = game->world == NULL ? QA_Q1MPM_NONE : QA_Q1MPM_SOME(game->world->actor.id);
  game->radius_damage(game, entity->actor.id, QA_Q1MPM_SOME(entity->actor.id), 80, ignore, NULL, "");
  qa_q1mpm_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1mpm_vadd(origin, QA_Q1MPM_V3(0, 0, 24));
  game->set_body(game, entity, &patch);
  qa_q1mpm_event event = {0};
  event.kind = "colored-explosion";
  event.has_origin = true;
  event.origin = origin;
  event.color_start = 0;
  event.color_length = 4;
  game->host->emit(game->host->ctx, &event);
  game->remove(game, entity);
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_fields_set(monster->entity, "yaw_speed", "35");
  qa_q1mpm_monster_spawn_default(monster);
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  if (monster->state.pain_finished > monster->game->time) return;
  double r = monster->game->host->random(monster->game->host->ctx);
  if (r > 0.1) { monster->state.pain_finished = monster->game->time + 0.5; return; }
  qa_q1mpm_monster_play(monster, r < 0.07 ? "wrath_pn_a01" : "wrath_pn_b01");
  monster->state.pain_finished = monster->game->time + 3;
  qa_q1mpm_game_sound(monster->game, monster->entity, "wrath/wpain.wav");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  (void)ctx; (void)attacker;
  qa_q1mpm_monster_play(monster, "wrath_die02");
}
static const char *const classnames[] = {"monster_wrath"};
static const char *const spec_gibs[] = {"wrthgib1", "wrthgib2", "wrthgib3"};
static const qa_q1mpm_named_action actions[] = {
    {"wrath_attack", {wrath_attack, NULL}},
    {"WrathMissile(1)", {missile1, NULL}},
    {"WrathMissile(2)", {missile2, NULL}},
    {"WrathMissile(3)", {missile3, NULL}},
    {"wrath:wrath_die15", {die15, NULL}},
};
const qa_q1mpm_pack_monster_definition qa_q1mpm_wrath_definition = {
    .spec = {"wrath", NULL, classnames, 1, "wrath", NULL, 400, -INFINITY, spec_gibs, 3,
             {{-16, -16, -24}, {16, 16, 32}}, "wrath_stand1", "wrath_walk01", "wrath_run01",
             "wrath/wsee.wav", "wrath_attack", false, "fly"},
    .frames = {qa_q1mpm_tables_wrath_entries, QA_Q1MPM_TABLES_WRATH_COUNT},
    .actions = actions,
    .action_count = sizeof(actions) / sizeof(actions[0]),
    .callbacks = qa_q1mpm_wrath_callbacks,
    .callback_count = sizeof(qa_q1mpm_wrath_callbacks) / sizeof(qa_q1mpm_wrath_callbacks[0]),
    .spawn = {spawn, NULL},
    .pain = pain,
    .die = die,
};
