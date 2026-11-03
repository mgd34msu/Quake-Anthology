/* Port of src/content/q1/missionpacks/monsters/wrath.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/wrath.h"

#include <string.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/wrath.h"
#include "monsters_internal.h"

void qa_q1mpm_wrath_missile(qa_q1mpm_mission_monster *monster, double attack) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(&monster->base, &target)) return;
  qa_vec3 origin0 = qa_q1_base_monster_origin(&monster->base);
  qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(qa_q1_vadd(target, (qa_vec3){0, 0, 10}), origin0));
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  entity->effects = qa_js_bit_or(entity->effects, 2);
  double forward = attack == 1 || attack == 4 ? 20 : attack == 2 ? 18 : 12;
  double up = attack == 4 ? 16 : attack == 2 ? 10 : 12;
  qa_vec3 origin = qa_q1_vadd(qa_q1_vadd(qa_q1_vadd(origin0, qa_q1_vscale(basis.forward, forward)),
                                              qa_q1_vscale(basis.up, up)),
                                 qa_q1_vscale(basis.right, attack == 3 ? 20 : 0));
  qa_q1_actor *shot = qa_q1mpm_missile(game, entity->actor.id, "wrath_missile", "progs/w_ball.mdl",
                                          origin, qa_q1_vscale(direction, 400), "rogue:WrathMissileTouch", 5);
  (void)qa_q1_actor_reference_set(shot, "enemy", qa_q1_base_monster_enemy(&monster->base));
  shot->angular_velocity = (qa_vec3){300, 300, 300};
  qa_q1_schedule(game, shot, 0.1, qa_q1_callback_registry_action(game->named, shot, "rogue:WrathHome"));
  monster->base.state->attack_finished = game->time + 2;
}
static void wrath_home(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_actor_id enemy;
  if (!qa_q1_actor_reference_get(entity, "enemy", &enemy) || qa_q1_health(game, enemy) < 1) { qa_q1_remove(game, entity); return; }
  qa_vec3 target;
  if (qa_q1mpm_eye(game, enemy, &target)) {
    qa_q1_body_patch patch = {0};
    patch.has_velocity = true;
    patch.velocity = qa_q1_vscale(
        qa_q1_normalize(qa_q1_vsub(target, qa_q1_get_body(game, entity).origin)),
        qa_q1_option_skill(game) == 3 ? 550 : 400);
    qa_q1_set_body(game, entity, &patch);
  }
  qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, "rogue:WrathHome"));
}
static void wrath_missile_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  const char *classname = game->host.classname(game->host.ctx, other);
  if ((entity->has_owner && qa_same_actor(other, entity->owner)) || q1mpm_str_eq(classname, "monster_wrath") ||
      q1mpm_str_eq(classname, "monster_super_wrath")) {
    qa_q1_remove(game, entity);
    return;
  }
  if (q1mpm_str_eq(classname, "monster_zombie"))
    (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, 110, NULL, NULL, NULL, NULL);
  qa_q1_radius_damage(game, entity->actor.id, entity->has_owner ? &entity->owner : NULL, 20,
                      game->world == NULL ? NULL : &game->world->actor.id, NULL, NULL);
  qa_q1_sound(game, entity->actor.id, "weapons/r_exp3.wav", "weapon", 1, 1);
  qa_q1_effect(game, "explosion", qa_q1_get_body(game, entity).origin, NULL, 1);
  qa_q1_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = QA_Q1_ZERO;
  qa_q1_set_body(game, entity, &patch);
  entity->touch = (qa_q1_bound_touch){0};
  q1mpm_set_text(&entity->model, "progs/s_explod.spr");
  entity->solid = "none";
  entity->frame = 0;
  qa_q1_link(game, entity);
  qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, "rogue:wrath_explode"));
}
static void wrath_explode(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->frame++;
  if (entity->frame > 5) qa_q1_remove(game, entity);
  else qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, "rogue:wrath_explode"));
}
const qa_q1mpm_named_handlers qa_q1mpm_wrath_callbacks[] = {
    {"WrathHome", {.action = wrath_home}},
    {"WrathMissileTouch", {.touch = wrath_missile_touch}},
    {"wrath_explode", {.action = wrath_explode}},
};
const size_t qa_q1mpm_wrath_callback_count = sizeof(qa_q1mpm_wrath_callbacks) / sizeof(qa_q1mpm_wrath_callbacks[0]);

static void wrath_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  double r = qa_q1_host_random(monster->base.game);
  qa_q1_base_monster_play(&monster->base, r < 0.25 ? "wrath_at_a01" : r < 0.65 ? "wrath_at_b01" : "wrath_at_c01");
  qa_q1_sound_defaults(monster->base.game, monster->base.entity->actor.id, "wrath/watt.wav");
}
static void missile1(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; qa_q1mpm_wrath_missile(monster, 1); }
static void missile2(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; qa_q1mpm_wrath_missile(monster, 2); }
static void missile3(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; qa_q1mpm_wrath_missile(monster, 3); }
static void die15(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  static const char *const models[] = {"wrthgib1", "wrthgib2", "wrthgib3"};
  for (size_t i = 0; i < 3; i++) {
    const qa_vec3 origin = qa_q1_base_monster_origin(&monster->base);
    (void)qa_q1_throw_gib(game, origin, models[i], qa_q1_health(game, entity->actor.id));
  }
  qa_q1_radius_damage(game, entity->actor.id, &entity->actor.id, 80, game->world == NULL ? NULL : &game->world->actor.id,
                      NULL, NULL);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 24});
  qa_q1_set_body(game, entity, &patch);
  /* TS emits at monster.origin read after the setBody above. */
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "colored-explosion";
  event.has_origin = true;
  event.origin = qa_q1_base_monster_origin(&monster->base);
  event.color_start = 0;
  event.color_length = 4;
  qa_q1_emit(game, event);
  qa_q1_remove(game, entity);
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  (void)qa_q1_actor_field_set(monster->base.entity, "yaw_speed", "35");
  qa_q1mpm_monster_spawn_default(monster);
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  if (monster->base.state->pain_finished > monster->base.game->time) return;
  double r = qa_q1_host_random(monster->base.game);
  if (r > 0.1) { monster->base.state->pain_finished = monster->base.game->time + 0.5; return; }
  qa_q1_base_monster_play(&monster->base, r < 0.07 ? "wrath_pn_a01" : "wrath_pn_b01");
  monster->base.state->pain_finished = monster->base.game->time + 3;
  qa_q1_sound_defaults(monster->base.game, monster->base.entity->actor.id, "wrath/wpain.wav");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker;
  qa_q1_base_monster_play(&monster->base, "wrath_die02");
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
