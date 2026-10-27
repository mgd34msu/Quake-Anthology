/* Port of src/content/q1/missionpacks/monsters/charmed-base.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/charmed_base.h"

#include <math.h>
#include <stdlib.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/grunt.h"
#include "qa/content/q1/missionpacks/monsters/tables/rottweiler.h"
#include "monsters_internal.h"

static void army_fire_impl(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_monster_face(monster);
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  game->sound(game, entity, "soldier/sattck1.wav", QA_Q1MPM_CH("weapon"), 1, 1);
  if (!monster->state.enemy.has) return;
  qa_q1mpm_body enemy;
  if (!game->host->bodies_read(game->host->ctx, monster->state.enemy.id, &enemy)) return;
  qa_vec3 direction = qa_q1mpm_normalize(qa_q1mpm_vsub(
      qa_q1mpm_vsub(enemy.origin, qa_q1mpm_vscale(enemy.velocity, 0.2)), qa_q1mpm_monster_origin(monster)));
  qa_q1mpm_ext_fire_bullets(game, entity->actor, direction, qa_q1mpm_actor_vector(entity, "v_angle"),
                            4, 0.1, 0.1, NULL);
}
static void army_fire(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; army_fire_impl(monster); }
static void drop_shells(qa_q1mpm_mission_monster *monster) {
  monster->entity->solid = "none";
  monster->game->link(monster->game, monster->entity);
  qa_q1mpm_ext_backpack_contents contents = {NULL, 5, 0, 0, 0};
  qa_q1mpm_ext_drop_backpack(monster->game, qa_q1mpm_monster_origin(monster), &contents);
}
static void army_atk5(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_face(monster);
  army_fire_impl(monster);
  monster->entity->effects |= 2;
}
static void army_atk7(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_face(monster);
  if (monster->game->options.skill == 3 && !monster->state.refired && qa_q1mpm_monster_visible(monster)) {
    monster->state.refired = true;
    qa_q1mpm_monster_set_next_frame(monster, "army_atk1");
  }
}
static void army_die3(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; drop_shells(monster); }
static void army_cdie3(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  drop_shells(monster);
  monster->game->host->walk_move(monster->game->host->ctx, monster->entity->actor,
                                  monster->game->body(monster->game, monster->entity).angles.y + 180, 4);
}
static double back3 = 3, back4 = 4, back5 = 5, back13 = 13;
static void ai_back(void *ctx, qa_q1mpm_mission_monster *monster) {
  double distance = *(const double *)ctx;
  monster->game->host->walk_move(monster->game->host->ctx, monster->entity->actor,
                                  monster->game->body(monster->game, monster->entity).angles.y + 180, distance);
}
static bool army_check_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_vec3 start, end;
  bool has_start = qa_q1mpm_monster_eye(monster, &start);
  bool has_end = monster->state.enemy.has && qa_q1mpm_monster_eye_actor(monster, monster->state.enemy.id, &end);
  if (!has_start || !has_end) return false;
  qa_q1mpm_trace_request request = {start, end, QA_Q1MPM_POINT,
                                    QA_Q1MPM_SOME(monster->entity->actor.id), true, false};
  qa_q1mpm_trace trace = monster->game->host->trace(monster->game->host->ctx, &request);
  if ((trace.in_open && trace.in_water) || !qa_q1mpm_opt_eq(trace.actor, monster->state.enemy) ||
      monster->game->time < monster->state.attack_finished || qa_q1mpm_monster_range_distance(monster) >= 1000)
    return false;
  double range = qa_q1mpm_monster_range_distance(monster);
  if (monster->game->host->random(monster->game->host->ctx) >= (range < 120 ? 0.9 : range < 500 ? 0.4 : 0.05))
    return false;
  qa_q1mpm_monster_play(monster, "army_atk1");
  qa_q1mpm_monster_attack_finished(monster, 1 + monster->game->host->random(monster->game->host->ctx));
  if (monster->game->host->random(monster->game->host->ctx) < 0.3) monster->lefty = !monster->lefty;
  return true;
}
static void army_pain(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  qa_q1mpm_game *game = monster->game;
  if (monster->state.pain_finished > game->time) return;
  double r = game->host->random(game->host->ctx);
  monster->state.pain_finished = game->time + (r < 0.2 ? 0.6 : 1.1);
  qa_q1mpm_monster_play(monster, r < 0.2 ? "army_pain1" : r < 0.6 ? "army_painb1" : "army_painc1");
  qa_q1mpm_game_sound(game, monster->entity, r < 0.2 ? "soldier/pain1.wav" : "soldier/pain2.wav");
}
static void army_die(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  (void)ctx; (void)attacker;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  double health = game->health(game, entity->actor.id);
  if (health < -35) {
    qa_q1mpm_game_sound(game, entity, "player/udeath.wav");
    qa_q1mpm_ext_throw_head_damage(game, entity, "h_guard", health);
    static const char *const models[] = {"gib1", "gib2", "gib3"};
    for (size_t i = 0; i < 3; i++)
      qa_q1mpm_ext_throw_gib(game, qa_q1mpm_monster_origin(monster), models[i], health);
    return;
  }
  qa_q1mpm_game_sound(game, entity, "soldier/death1.wav");
  qa_q1mpm_monster_play(monster, game->host->random(game->host->ctx) < 0.5 ? "army_die1" : "army_cdie1");
}
static const char *const army_classnames[] = {"monster_army"};
static const char *const army_gibs[] = {"gib1", "gib2", "gib3"};
static const qa_q1mpm_named_action army_actions[] = {
    {"army_fire", {army_fire, NULL}},
    {"grunt:army_atk5", {army_atk5, NULL}},
    {"grunt:army_atk7", {army_atk7, NULL}},
    {"grunt:army_die3", {army_die3, NULL}},
    {"grunt:army_cdie3", {army_cdie3, NULL}},
    {"ai_back(3)", {ai_back, &back3}},
    {"ai_back(4)", {ai_back, &back4}},
    {"ai_back(5)", {ai_back, &back5}},
    {"ai_back(13)", {ai_back, &back13}},
};
const qa_q1mpm_pack_monster_definition qa_q1mpm_hipnotic_army_definition = {
    .spec = {"army", NULL, army_classnames, 1, "soldier", "h_guard", 30, -35, army_gibs, 3,
             {{-16, -16, -24}, {16, 16, 40}}, "army_stand1", "army_walk1", "army_run1",
             "soldier/sight1.wav", "army_atk1", false, "walk"},
    .frames = {qa_q1mpm_tables_grunt_entries, QA_Q1MPM_TABLES_GRUNT_COUNT},
    .actions = army_actions,
    .action_count = sizeof(army_actions) / sizeof(army_actions[0]),
    .pain = army_pain,
    .die = army_die,
    .check_attack = army_check_attack,
};

static void dog_jump_touch(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity, qa_actor_id other,
                           bool has_normal, qa_vec3 normal, const void *surface) {
  (void)has_normal; (void)normal; (void)surface;
  qa_q1mpm_mission_monster *monster = qa_q1mpm_runtime_require((qa_q1mpm_runtime *)ctx, entity);
  if (game->health(game, entity->actor.id) <= 0) return;
  qa_q1mpm_combat_read combat;
  if (game->host->combat_read(game->host->ctx, other, &combat) && combat.can_take_damage &&
      qa_q1mpm_length(game->body(game, entity).velocity) > 300)
    qa_q1mpm_game_damage(game, other, entity->actor.id, QA_Q1MPM_SOME(entity->actor.id),
                         10 + 10 * game->host->random(game->host->ctx));
  if (!game->host->check_bottom(game->host->ctx, entity->actor.id)) {
    if ((entity->movement_flags & 512) != 0) {
      entity->touch = (qa_q1mpm_callback){NULL, NULL, QA_Q1MPM_CB_TOUCH};
      qa_q1mpm_monster_set_next_frame(monster, "dog_leap1");
      qa_q1mpm_monster_delay(monster, 0.1);
    }
    return;
  }
  entity->touch = (qa_q1mpm_callback){NULL, NULL, QA_Q1MPM_CB_TOUCH};
  qa_q1mpm_monster_set_next_frame(monster, "dog_run1");
  qa_q1mpm_monster_delay(monster, 0.1);
}
static void dog_bite(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (!monster->state.enemy.has) return;
  qa_q1mpm_monster_ai(monster, QA_Q1MPM_AI_CHARGE, 10);
  if (!monster->game->can_damage(monster->game, monster->state.enemy.id, monster->entity->actor.id) ||
      qa_q1mpm_monster_distance(monster) > 100)
    return;
  const qa_q1mpm_host *host = monster->game->host;
  double damage = (host->random(host->ctx) + host->random(host->ctx) + host->random(host->ctx)) * 8;
  qa_q1mpm_game_damage(monster->game, monster->state.enemy.id, monster->entity->actor.id,
                       QA_Q1MPM_SOME(monster->entity->actor.id), damage);
}
static void dog_leap2(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_face(monster);
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  entity->touch = game->named.touch(game->named.ctx, entity, "hipnotic:Dog_JumpTouch");
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  qa_q1mpm_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, 1));
  patch.has_velocity = true;
  patch.velocity = qa_q1mpm_vadd(qa_q1mpm_vscale(basis.forward, 300), QA_Q1MPM_V3(0, 0, 200));
  game->set_body(game, entity, &patch);
  entity->movement_flags &= ~512;
}
static bool dog_check_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (qa_q1mpm_monster_range_distance(monster) < 120) {
    monster->entity->attack_state = "melee";
    return true;
  }
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_body body = game->body(game, entity);
  if (!monster->state.enemy.has) return false;
  qa_q1mpm_body enemy;
  if (!game->host->bodies_read(game->host->ctx, monster->state.enemy.id, &enemy)) return false;
  if (body.origin.z + body.bounds.min.z > enemy.origin.z + enemy.bounds.min.z + 0.75 * (enemy.bounds.max.z - enemy.bounds.min.z) ||
      body.origin.z + body.bounds.max.z < enemy.origin.z + enemy.bounds.min.z + 0.25 * (enemy.bounds.max.z - enemy.bounds.min.z))
    return false;
  double distance = hypot(enemy.origin.x - body.origin.x, enemy.origin.y - body.origin.y);
  if (distance < 80 || distance > 150) return false;
  entity->attack_state = "missile";
  return true;
}
static void dog_melee(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_play(monster, "dog_atta1");
}
static void dog_pain(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  qa_q1mpm_game_sound(monster->game, monster->entity, "dog/dpain1.wav");
  qa_q1mpm_monster_play(monster, monster->game->host->random(monster->game->host->ctx) > 0.5
                                     ? "dog_pain1" : "dog_painb1");
}
static void dog_die(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  (void)ctx; (void)attacker;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  double health = game->health(game, entity->actor.id);
  if (health < -35) {
    qa_q1mpm_game_sound(game, entity, "player/udeath.wav");
    for (int i = 0; i < 3; i++)
      qa_q1mpm_ext_throw_gib(game, qa_q1mpm_monster_origin(monster), "gib3", health);
    qa_q1mpm_ext_throw_head_damage(game, entity, "h_dog", health);
    return;
  }
  qa_q1mpm_game_sound(game, entity, "dog/ddeath.wav");
  entity->solid = "none";
  game->link(game, entity);
  qa_q1mpm_monster_play(monster, game->host->random(game->host->ctx) > 0.5 ? "dog_die1" : "dog_dieb1");
}
static const char *const dog_classnames[] = {"monster_dog"};
static const char *const dog_gibs[] = {"gib3", "gib3", "gib3"};
static const qa_q1mpm_named_action dog_actions[] = {
    {"dog_bite", {dog_bite, NULL}},
    {"rottweiler:dog_leap2", {dog_leap2, NULL}},
};
void qa_q1mpm_hipnotic_dog_definition(qa_q1mpm_runtime *runtime, qa_q1mpm_pack_monster_definition *out) {
  static const qa_q1mpm_species_spec spec = {
      "dog", NULL, dog_classnames, 1, "dog", "h_dog", 25, -35, dog_gibs, 3,
      {{-32, -32, -24}, {32, 32, 40}}, "dog_stand1", "dog_walk1", "dog_run1",
      "dog/dsight.wav", "dog_leap1", true, "walk"};
  *out = (qa_q1mpm_pack_monster_definition){0};
  out->spec = spec;
  out->frames = (qa_q1mpm_frame_table){qa_q1mpm_tables_rottweiler_entries, QA_Q1MPM_TABLES_ROTTWEILER_COUNT};
  out->actions = dog_actions;
  out->action_count = sizeof(dog_actions) / sizeof(dog_actions[0]);
  /* Callback storage lives as long as the registered definition
   * (allocated per factory call, never freed separately). */
  qa_q1mpm_named_handlers *callbacks = malloc(sizeof(*callbacks));
  if (callbacks != NULL) {
    callbacks[0].name = "Dog_JumpTouch";
    callbacks[0].handlers = (qa_q1mpm_handlers){runtime, NULL, dog_jump_touch, NULL, NULL, NULL};
    out->callbacks = callbacks;
    out->callback_count = 1;
  }
  out->pain = dog_pain;
  out->die = dog_die;
  out->melee = (qa_q1mpm_action){dog_melee, NULL};
  out->check_attack = dog_check_attack;
}
