/* Port of src/content/q1/missionpacks/monsters/charmed-base.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/charmed_base.h"

#include <math.h>

#include "qa/content/q1/base/grunt.h"
#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/grunt.h"
#include "qa/content/q1/missionpacks/monsters/tables/rottweiler.h"
#include "monsters_internal.h"

static void army_fire_impl(qa_q1mpm_mission_monster *monster) {
  qa_q1_base_monster_face(&monster->base);
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_sound(game, entity->actor.id, "soldier/sattck1.wav", "weapon", 1, 1);
  if (!monster->base.state->has_enemy) return;
  qa_q1_body enemy;
  if (!q1mpm_read_body(game, monster->base.state->enemy, &enemy)) return;
  qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(
      qa_q1_vsub(enemy.origin, qa_q1_vscale(enemy.velocity, 0.2)), qa_q1_base_monster_origin(&monster->base)));
  qa_q1_fire_bullets(game, entity->actor, direction, qa_q1_actor_vector(entity, "v_angle"), 4, 0.1, 0.1, NULL);
}
static void army_fire(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; army_fire_impl(monster); }
static void army_atk5(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_face(&monster->base);
  army_fire_impl(monster);
  monster->base.entity->effects = qa_js_bit_or(monster->base.entity->effects, 2);
}
static void army_atk7(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_face(&monster->base);
  if (qa_q1_option_skill(monster->base.game) == 3 && !monster->base.state->refired && qa_q1_base_monster_visible(&monster->base, qa_q1_base_monster_enemy(&monster->base))) {
    monster->base.state->refired = true;
    q1mpm_set_next_frame(&monster->base, "army_atk1");
  }
}
/* TS dropShells / walkBack from base/grunt.ts. */
static void army_die3(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; qa_q1_grunt_drop_shells(&monster->base); }
static void army_cdie3(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_grunt_drop_shells(&monster->base);
  qa_q1_grunt_walk_back(&monster->base, 4);
}
static const double BACK3 = 3, BACK4 = 4, BACK5 = 5, BACK13 = 13;
static void ai_back(void *ctx, qa_q1mpm_mission_monster *monster) {
  qa_q1_grunt_walk_back(&monster->base, *(const double *)ctx);
}
/* TS armyCheckAttack / armyPain from base/grunt.ts. */
static bool army_check_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  return qa_q1_army_check_attack(&monster->base);
}
static void army_pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage; /* TS pain: armyPain(monster) */
  qa_q1_army_pain(&monster->base);
}
static void army_die(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  double health = qa_q1_health(game, entity->actor.id);
  if (health < -35) {
    qa_q1_sound_defaults(game, entity->actor.id, "player/udeath.wav");
    qa_q1_throw_head_damage(game, entity, "h_guard", health);
    static const char *const models[] = {"gib1", "gib2", "gib3"};
    for (size_t i = 0; i < 3; i++)
      (void)qa_q1_throw_gib(game, qa_q1_base_monster_origin(&monster->base), models[i], health);
    return;
  }
  qa_q1_sound_defaults(game, entity->actor.id, "soldier/death1.wav");
  qa_q1_base_monster_play(&monster->base, qa_q1_host_random(game) < 0.5 ? "army_die1" : "army_cdie1");
}
static const char *const army_classnames[] = {"monster_army"};
static const char *const army_gibs[] = {"gib1", "gib2", "gib3"};
static const qa_q1mpm_named_action army_actions[] = {
    {"army_fire", {army_fire, NULL}},
    {"grunt:army_atk5", {army_atk5, NULL}},
    {"grunt:army_atk7", {army_atk7, NULL}},
    {"grunt:army_die3", {army_die3, NULL}},
    {"grunt:army_cdie3", {army_cdie3, NULL}},
    {"ai_back(3)", {ai_back, (void *)&BACK3}},
    {"ai_back(4)", {ai_back, (void *)&BACK4}},
    {"ai_back(5)", {ai_back, (void *)&BACK5}},
    {"ai_back(13)", {ai_back, (void *)&BACK13}},
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

static void dog_jump_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1mpm_mission_monster *monster = qa_q1mpm_runtime_require((qa_q1mpm_runtime *)ctx, entity);
  if (qa_q1_health(game, entity->actor.id) <= 0) return;
  if (q1mpm_can_take_damage(game, other) && qa_q1_length(qa_q1_get_body(game, entity).velocity) > 300)
    (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, 10 + 10 * qa_q1_host_random(game), NULL, NULL, NULL, NULL);
  if (!game->host.check_bottom(game->host.ctx, entity->actor.id)) {
    if (qa_js_bit_and(entity->movement_flags, 512) != 0) {
      entity->touch = (qa_q1_bound_touch){0};
      q1mpm_set_next_frame(&monster->base, "dog_leap1");
      qa_q1_base_monster_delay(&monster->base, 0.1);
    }
    return;
  }
  entity->touch = (qa_q1_bound_touch){0};
  q1mpm_set_next_frame(&monster->base, "dog_run1");
  qa_q1_base_monster_delay(&monster->base, 0.1);
}
/* TS dogBite / dogLeap / dogCheckAttack / dogPain from base/grunt.ts. */
static void dog_bite(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_dog_bite(&monster->base);
}
static void dog_leap2(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_dog_leap(&monster->base, "hipnotic:Dog_JumpTouch");
}
static bool dog_check_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  return qa_q1_dog_check_attack(&monster->base);
}
static void dog_melee(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_play(&monster->base, "dog_atta1");
}
static void dog_pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage; /* TS pain: dogPain(monster) */
  qa_q1_dog_pain(&monster->base);
}
static void dog_die(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  double health = qa_q1_health(game, entity->actor.id);
  if (health < -35) {
    qa_q1_sound_defaults(game, entity->actor.id, "player/udeath.wav");
    for (int i = 0; i < 3; i++)
      (void)qa_q1_throw_gib(game, qa_q1_base_monster_origin(&monster->base), "gib3", health);
    qa_q1_throw_head_damage(game, entity, "h_dog", health);
    return;
  }
  qa_q1_sound_defaults(game, entity->actor.id, "dog/ddeath.wav");
  entity->solid = "none";
  qa_q1_link(game, entity);
  qa_q1_base_monster_play(&monster->base, qa_q1_host_random(game) > 0.5 ? "dog_die1" : "dog_dieb1");
}
static const char *const dog_classnames[] = {"monster_dog"};
static const char *const dog_gibs[] = {"gib3", "gib3", "gib3"};
static const qa_q1mpm_named_action dog_actions[] = {
    {"dog_bite", {dog_bite, NULL}},
    {"rottweiler:dog_leap2", {dog_leap2, NULL}},
};
void qa_q1mpm_hipnotic_dog_definition(qa_q1mpm_runtime *runtime, qa_q1mpm_pack_monster_definition *out) {
  static const qa_q1_base_monster_species spec = {
      "dog", NULL, dog_classnames, 1, "dog", "h_dog", 25, -35, dog_gibs, 3,
      {{-32, -32, -24}, {32, 32, 40}}, "dog_stand1", "dog_walk1", "dog_run1",
      "dog/dsight.wav", "dog_leap1", true, "walk"};
  *out = (qa_q1mpm_pack_monster_definition){0};
  out->spec = spec;
  out->frames = (qa_q1_monster_frame_table){qa_q1mpm_tables_rottweiler_entries, QA_Q1MPM_TABLES_ROTTWEILER_COUNT};
  out->actions = dog_actions;
  out->action_count = sizeof(dog_actions) / sizeof(dog_actions[0]);
  /* The closure captures runtime: the record lives as long as the runtime. */
  qa_q1mpm_named_handlers *callbacks = qa_q1mpm_runtime_calloc(runtime, 1, sizeof *callbacks);
  callbacks[0].name = "Dog_JumpTouch";
  callbacks[0].handlers = (qa_q1_callback_handlers){.ctx = runtime, .touch = dog_jump_touch};
  out->callbacks = callbacks;
  out->callback_count = 1;
  out->pain = dog_pain;
  out->die = dog_die;
  out->melee = (qa_q1mpm_action){dog_melee, NULL};
  out->check_attack = dog_check_attack;
}
