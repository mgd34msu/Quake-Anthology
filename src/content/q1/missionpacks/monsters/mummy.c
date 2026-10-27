/* Port of src/content/q1/missionpacks/monsters/mummy.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/mummy.h"

#include <stdio.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/mummy.h"
#include "monsters_internal.h"

static void fire_at(qa_q1mpm_mission_monster *monster, qa_vec3 offset) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_monster_face(monster);
  qa_vec3 target;
  if (!qa_q1mpm_monster_target(monster, &target)) return;
  game->sound(game, entity, "zombie/z_shot1.wav", QA_Q1MPM_CH("weapon"), 1, 1);
  qa_q1mpm_basis basis = game->basis;
  qa_vec3 origin0 = qa_q1mpm_monster_origin(monster);
  qa_vec3 origin = qa_q1mpm_vadd(qa_q1mpm_vadd(qa_q1mpm_vadd(origin0, qa_q1mpm_vscale(basis.forward, offset.x)),
                                              qa_q1mpm_vscale(basis.right, offset.y)),
                                 qa_q1mpm_vscale(basis.up, offset.z - 24));
  qa_q1mpm_actor *grenade = game->create(game, "mummy_grenade");
  grenade->owner = QA_Q1MPM_SOME(entity->actor.id);
  grenade->movement = "bounce";
  grenade->solid = "bbox";
  snprintf(grenade->model, sizeof(grenade->model), "%s", "progs/zom_gib.mdl");
  game->make_vectors(game, game->body(game, entity).angles);
  qa_vec3 velocity = qa_q1mpm_vscale(qa_q1mpm_normalize(qa_q1mpm_vsub(target, origin)), 600);
  velocity.z = 200;
  qa_q1mpm_body_patch patch = {0};
  patch.has_origin = true; patch.origin = origin;
  patch.has_velocity = true; patch.velocity = velocity;
  patch.has_bounds = true; patch.bounds = QA_Q1MPM_POINT;
  game->set_body(game, grenade, &patch);
  grenade->angular_velocity = QA_Q1MPM_V3(3000, 1000, 2000);
  grenade->touch = game->named.touch(game->named.ctx, grenade, "rogue:mummyGrenadeTouch");
  game->schedule(game, grenade, 2.5, game->named.action(game->named.ctx, grenade, "SUB_Remove"));
  game->link(game, grenade);
}
static void wake(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_number(monster, "mummy:asleep", 0);
  qa_q1mpm_monster_play(monster, "mummy_paine12");
}
static void grenade_touch(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity, qa_actor_id other,
                          bool has_normal, qa_vec3 normal, const void *surface) {
  (void)ctx; (void)has_normal; (void)normal; (void)surface;
  if (qa_q1mpm_id_eq_opt(other, entity->owner)) return;
  qa_q1mpm_combat_read combat;
  if (game->host->combat_read(game->host->ctx, other, &combat) && combat.can_take_damage) {
    qa_q1mpm_game_damage(game, other, entity->actor.id, entity->owner,
                         15 + game->host->random(game->host->ctx) * 15);
    game->sound(game, entity, "zombie/z_hit.wav", QA_Q1MPM_CH("weapon"), 1, 1);
    game->remove(game, entity);
    return;
  }
  game->sound(game, entity, "zombie/z_miss.wav", QA_Q1MPM_CH("weapon"), 1, 1);
  qa_q1mpm_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = QA_Q1MPM_ZERO;
  game->set_body(game, entity, &patch);
  entity->angular_velocity = QA_Q1MPM_ZERO;
  entity->touch = game->named.touch(game->named.ctx, entity, "rogue:mummyGrenadeRemove");
}
static void grenade_remove(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity, qa_actor_id other,
                           bool has_normal, qa_vec3 normal, const void *surface) {
  (void)ctx; (void)other; (void)has_normal; (void)normal; (void)surface;
  game->remove(game, entity);
}
static void run1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_ai(monster, QA_Q1MPM_AI_RUN, 2);
  monster->in_pain = 0;
}
static void atta13(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; fire_at(monster, QA_Q1MPM_V3(-10, -22, 30)); }
static void attb14(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; fire_at(monster, QA_Q1MPM_V3(-10, -24, 29)); }
static void attc12(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; fire_at(monster, QA_Q1MPM_V3(-12, -19, 29)); }
static void paine11(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_delay(monster, monster->entity->next_think - monster->game->time + 5);
}
static void paine12(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->game->sound(monster->game, monster->entity, "zombie/z_idle.wav", QA_Q1MPM_CH("voice"), 2, 1);
  monster->game->set_bounds(monster->game, monster->entity, qa_q1mpm_hull_bounds);
  monster->entity->solid = "slidebox";
  if (!monster->game->host->walk_move(monster->game->host->ctx, monster->entity->actor, 0, 0)) {
    qa_q1mpm_monster_set_next_frame(monster, "mummy_paine11");
    monster->entity->solid = "none";
  }
  monster->game->link(monster->game, monster->entity);
}
static void mummy_missile(void *ctx, qa_q1mpm_mission_monster *monster) {
  if (qa_q1mpm_actor_number(monster->entity, "mummy:asleep") != 0) { wake(ctx, monster); return; }
  double r = monster->game->host->random(monster->game->host->ctx);
  qa_q1mpm_monster_play(monster, r < 0.3 ? "mummy_atta1" : r < 0.6 ? "mummy_attb1" : "mummy_attc1");
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_spawn_default(monster);
  if ((monster->entity->spawnflags & 4) != 0) {
    monster->entity->max_health = 1000;
    monster->game->host->combat_set_health(monster->game->host->ctx, monster->entity->actor, 1000);
  }
  if ((monster->entity->spawnflags & 2) != 0) {
    qa_q1mpm_number(monster, "mummy:asleep", 1);
    qa_bounds bounds = {qa_q1mpm_hull_bounds.min, {16, 16, -16}};
    monster->game->set_bounds(monster->game, monster->entity, bounds);
    monster->entity->solid = "none";
  }
}
static void start(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_start_default(monster);
  if (qa_q1mpm_actor_number(monster->entity, "mummy:asleep") != 0)
    qa_q1mpm_monster_set_next_frame(monster, monster->state.path[0] == '\0' ? "mummy_sleep" : "mummy_wake");
}
static void found(void *ctx, qa_q1mpm_mission_monster *monster, qa_actor_id target) {
  (void)ctx;
  qa_q1mpm_monster_found_default(monster, target);
  if (qa_q1mpm_actor_number(monster->entity, "mummy:asleep") != 0)
    qa_q1mpm_monster_set_next_frame(monster, "mummy_wake");
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  (void)attacker; (void)damage;
  if (qa_q1mpm_actor_number(monster->entity, "mummy:asleep") != 0) { wake(ctx, monster); return; }
  if (monster->state.pain_finished > monster->game->time) return;
  double r = monster->game->host->random(monster->game->host->ctx);
  if (r > 0.24) return;
  monster->state.pain_finished = monster->game->time + 2.5;
  qa_q1mpm_monster_play(monster, r < 0.06 ? "mummy_paina1" : r < 0.12 ? "mummy_painb1"
                                 : r < 0.18 ? "mummy_painc1" : "mummy_paind1");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  (void)ctx; (void)attacker;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  game->host->combat_set_health(game->host->ctx, entity->actor, -35);
  qa_q1mpm_game_sound(game, entity, "zombie/z_gib.wav");
  qa_q1mpm_ext_throw_head(game, entity, "h_zombie");
  static const char *const models[] = {"gib1", "gib2", "gib3"};
  for (size_t i = 0; i < 3; i++)
    qa_q1mpm_ext_throw_gib(game, qa_q1mpm_monster_origin(monster), models[i], -35);
}
static const char *const classnames[] = {"monster_mummy"};
static const char *const spec_gibs[] = {"gib1", "gib2", "gib3"};
static const qa_q1mpm_named_action actions[] = {
    {"mummy:mummy_run1", {run1, NULL}},
    {"mummy:mummy_atta13", {atta13, NULL}},
    {"mummy:mummy_attb14", {attb14, NULL}},
    {"mummy:mummy_attc12", {attc12, NULL}},
    {"mummy:mummy_paine11", {paine11, NULL}},
    {"mummy:mummy_paine12", {paine12, NULL}},
    {"mummy_wake", {wake, NULL}},
    {"mummy_missile", {mummy_missile, NULL}},
};
static const qa_q1mpm_named_handlers callbacks[] = {
    {"mummyGrenadeTouch", {NULL, NULL, grenade_touch, NULL, NULL, NULL}},
    {"mummyGrenadeRemove", {NULL, NULL, grenade_remove, NULL, NULL, NULL}},
};
const qa_q1mpm_pack_monster_definition qa_q1mpm_mummy_definition = {
    .spec = {"mummy", NULL, classnames, 1, "mummy", "h_zombie", 500, 0, spec_gibs, 3,
             {{-16, -16, -24}, {16, 16, 32}}, "mummy_stand1", "mummy_walk1", "mummy_run1",
             "zombie/z_idle.wav", "mummy_missile", false, "walk"},
    .frames = {qa_q1mpm_tables_mummy_entries, QA_Q1MPM_TABLES_MUMMY_COUNT},
    .actions = actions,
    .action_count = sizeof(actions) / sizeof(actions[0]),
    .callbacks = callbacks,
    .callback_count = sizeof(callbacks) / sizeof(callbacks[0]),
    .spawn = {spawn, NULL},
    .start = {start, NULL},
    .pain = pain,
    .die = die,
    .found = found,
};
