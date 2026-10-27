/* Port of src/content/q1/missionpacks/monsters/overlord.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/overlord.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/s_wrath.h"
#include "qa/content/q1/missionpacks/monsters/wrath.h"
#include "monsters_internal.h"

bool qa_q1mpm_is_spawn_point_empty(qa_q1mpm_game *game, qa_q1mpm_actor *point) {
  qa_actor_id actors[512];
  size_t count = qa_q1mpm_radius_actors(game, game->body(game, point).origin, 64, actors, 512);
  for (size_t i = 0; i < count; i++) {
    if (qa_same_actor(actors[i], point->actor.id)) continue;
    qa_q1mpm_actor *other = game->entity(game, QA_Q1MPM_SOME(actors[i]));
    if (((other == NULL ? 0 : other->movement_flags) & 32) != 0 ||
        qa_q1mpm_str_eq(game->host->classname(game->host->ctx, actors[i]), "player") ||
        (other != NULL && other->think.name != NULL))
      return false;
  }
  return true;
}
qa_q1mpm_actor *qa_q1mpm_overlord_destination(qa_q1mpm_game *game) {
  qa_actor_id players[64];
  size_t player_count = game->host->players(game->host->ctx, players, 64);
  qa_q1mpm_opt_actor player = player_count > 0 ? QA_Q1MPM_SOME(players[0])
                              : game->world == NULL ? QA_Q1MPM_NONE
                                                    : QA_Q1MPM_SOME(game->world->actor.id);
  qa_q1mpm_body body_value;
  bool has_body = player.has && game->host->bodies_read(game->host->ctx, player.id, &body_value);
  qa_q1mpm_basis basis = game->make_vectors(game, has_body ? body_value.angles : QA_Q1MPM_ZERO);
  qa_vec3 origin = has_body ? body_value.origin : QA_Q1MPM_ZERO;
  qa_q1mpm_actor *best = NULL, *furthest = NULL;
  double distance = 0;
  qa_q1mpm_actor *entities[1024];
  size_t count = game->entities(game, entities, 1024);
  for (size_t i = 0; i < count; i++) {
    qa_q1mpm_actor *entity = entities[i];
    if (strcmp(entity->classname, "info_overlord_destination") != 0 ||
        !qa_q1mpm_is_spawn_point_empty(game, entity))
      continue;
    qa_vec3 delta = qa_q1mpm_vsub(game->body(game, entity).origin, origin);
    double current = qa_q1mpm_length(delta);
    if (qa_q1mpm_dot(qa_q1mpm_normalize(delta), basis.forward) > 0.6 && current > 150) best = entity;
    if (current > distance) { furthest = entity; distance = current; }
  }
  return best != NULL ? best : furthest;
}
static void destination_spawn(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity) {
  (void)ctx;
  qa_q1mpm_body body = game->body(game, entity);
  entity->mangle = body.angles;
  entity->model[0] = '\0';
  qa_q1mpm_body_patch patch = {0};
  patch.has_angles = true; patch.angles = QA_Q1MPM_ZERO;
  patch.has_origin = true; patch.origin = qa_q1mpm_vadd(body.origin, QA_Q1MPM_V3(0, 0, 27));
  game->set_body(game, entity, &patch);
}
void qa_q1mpm_register_overlord_destination(qa_q1mpm_game *game) {
  game->register_spawn(game, "info_overlord_destination", destination_spawn, game);
}
static void teleport(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if ((entity->spawnflags & 2) == 0 || game->host->random(game->host->ctx) > 0.75) return;
  qa_q1mpm_actor *destination = qa_q1mpm_overlord_destination(game);
  if (destination == NULL) return;
  qa_q1mpm_ext_spawn_teleport_fog(game, qa_q1mpm_monster_origin(monster));
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  qa_vec3 origin = game->body(game, destination).origin;
  qa_q1mpm_ext_spawn_teleport_fog(game, qa_q1mpm_vadd(origin, qa_q1mpm_vscale(basis.forward, 32)));
  qa_q1mpm_ext_spawn_teledeath(game, origin, entity->actor.id);
  game->set_origin(game, entity, origin);
  entity->movement_flags &= ~512;
}
static void toss(qa_q1mpm_mission_monster *monster, const char *model) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, monster->entity).angles);
  qa_vec3 velocity = qa_q1mpm_vadd(
      qa_q1mpm_vadd(qa_q1mpm_vadd(qa_q1mpm_vscale(basis.forward, 250), qa_q1mpm_vscale(basis.up, 300)),
                    qa_q1mpm_vscale(basis.up, game->host->random(game->host->ctx) * 100 - 50)),
      qa_q1mpm_vscale(basis.right, game->host->random(game->host->ctx) * 200 - 100));
  qa_q1mpm_actor *gib = game->create(game, "gib");
  snprintf(gib->model, sizeof(gib->model), "progs/%s.mdl", model);
  gib->movement = "bounce";
  char buf[32];
  qa_q1mpm_format_number(buf, sizeof(buf), game->time);
  qa_q1mpm_fields_set(gib, "ltime", buf);
  qa_q1mpm_body_patch patch = {0};
  patch.has_origin = true; patch.origin = qa_q1mpm_monster_origin(monster);
  patch.has_bounds = true; patch.bounds = QA_Q1MPM_POINT;
  game->set_body(game, gib, &patch);
  game->schedule(game, gib, 10 + game->host->random(game->host->ctx) * 10,
                 game->named.action(game->named.ctx, gib, "SUB_Remove"));
  qa_q1mpm_body_patch velocity_patch = {0};
  velocity_patch.has_velocity = true;
  velocity_patch.velocity = velocity;
  game->set_body(game, gib, &velocity_patch);
  game->link(game, gib);
}
static void burst(qa_q1mpm_mission_monster *monster, const char *const *models, size_t count) {
  qa_q1mpm_event event = {0};
  event.kind = "colored-explosion";
  event.has_origin = true;
  event.origin = qa_q1mpm_monster_origin(monster);
  event.color_start = 0;
  event.color_length = 4;
  monster->game->host->emit(monster->game->host->ctx, &event);
  for (size_t i = 0; i < count; i++) toss(monster, models[i]);
}
static void smash(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (!monster->state.enemy.has || !game->can_damage(game, monster->state.enemy.id, entity->actor.id))
    return;
  qa_q1mpm_monster_ai(monster, QA_Q1MPM_AI_CHARGE, 10);
  qa_vec3 target;
  if (!qa_q1mpm_monster_target(monster, &target) || qa_q1mpm_monster_distance(monster) > 100) return;
  double damage = 20 + game->host->random(game->host->ctx) * 10;
  game->sound(game, entity, "s_wrath/smash.wav", QA_Q1MPM_CH("weapon"), 1, 1);
  qa_q1mpm_game_damage(game, monster->state.enemy.id, entity->actor.id, QA_Q1MPM_SOME(entity->actor.id), damage);
  qa_vec3 eye_target;
  qa_vec3 aim = qa_q1mpm_eye(game, monster->state.enemy.id, &eye_target) ? eye_target : target;
  qa_vec3 direction = qa_q1mpm_normalize(qa_q1mpm_vsub(aim, qa_q1mpm_monster_origin(monster)));
  qa_q1mpm_event event = {0};
  event.kind = "particles";
  event.has_origin = true;
  event.origin = qa_q1mpm_vsub(target, qa_q1mpm_vscale(direction, 30));
  event.direction = qa_q1mpm_vscale(direction, -100);
  event.color = 73;
  event.count_value = damage;
  game->host->emit(game->host->ctx, &event);
}
static void melee_impl(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  double r = monster->game->host->random(monster->game->host->ctx);
  qa_q1mpm_monster_play(monster, r < 0.33 ? "overlord_at_a01" : r < 0.66 ? "overlord_at_b01" : "overlord_at_c01");
}
static void missile_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->game->host->random(monster->game->host->ctx);
  qa_q1mpm_monster_play(monster, "overlord_msl_a01");
}
static void missile4(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_wrath_missile(monster, 4);
}
static void die01(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; qa_q1mpm_monster_delay(monster, 0.05); }
static void die02(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->entity->movement_flags |= 1;
  qa_q1mpm_monster_delay(monster, 0.05);
}
static void die17(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->entity->model[0] = '\0';
  static const char *const models[] = {"s_wrtgb2", "s_wrtgb3", "wrthgib1", "wrthgib2", "wrthgib3"};
  burst(monster, models, 5);
  qa_q1mpm_monster_delay(monster, 0.1);
}
static void die18(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  static const char *const models[] = {"gib1", "gib2", "gib3", "gib1", "gib2", "gib3"};
  burst(monster, models, 6);
  qa_q1mpm_monster_delay(monster, 0.1);
}
static void die19(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  static const char *const models[] = {"gib1", "gib2", "gib3", "gib1", "gib2", "gib3"};
  burst(monster, models, 6);
  monster->game->remove(monster->game, monster->entity);
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  if (monster->state.pain_finished > monster->game->time) return;
  double r = monster->game->host->random(monster->game->host->ctx);
  if (r > 0.2) return;
  qa_q1mpm_monster_play(monster, r < 0.15 ? "overlord_pn_a01" : "overlord_pn_b01");
  monster->state.pain_finished = monster->game->time + 2;
  qa_q1mpm_game_sound(monster->game, monster->entity, "wrath/wpain.wav");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  (void)ctx; (void)attacker;
  qa_q1mpm_monster_play(monster, "overlord_die02");
}
static const char *const classnames[] = {"monster_super_wrath"};
static const qa_q1mpm_named_action actions[] = {
    {"overlord_smash", {smash, NULL}},
    {"overlord_melee", {melee_impl, NULL}},
    {"overlord_teleport", {teleport, NULL}},
    {"overlord_missile", {missile_action, NULL}},
    {"WrathMissile(4)", {missile4, NULL}},
    {"s_wrath:overlord_die01", {die01, NULL}},
    {"s_wrath:overlord_die02", {die02, NULL}},
    {"s_wrath:overlord_die17", {die17, NULL}},
    {"s_wrath:overlord_die18", {die18, NULL}},
    {"s_wrath:overlord_die19", {die19, NULL}},
};
const qa_q1mpm_pack_monster_definition qa_q1mpm_overlord_definition = {
    .spec = {"super-wrath", NULL, classnames, 1, "s_wrath", NULL, 1000, -INFINITY, NULL, 0,
             {{-16, -16, -24}, {16, 16, 32}}, "overlord_stand1", "overlord_walk01", "overlord_run01",
             "", "overlord_missile", true, "fly"},
    .frames = {qa_q1mpm_tables_s_wrath_entries, QA_Q1MPM_TABLES_S_WRATH_COUNT},
    .actions = actions,
    .action_count = sizeof(actions) / sizeof(actions[0]),
    .pain = pain,
    .die = die,
    .melee = {melee_impl, NULL},
};
