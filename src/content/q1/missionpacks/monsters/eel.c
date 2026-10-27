/* Port of src/content/q1/missionpacks/monsters/eel.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/eel.h"

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/eel.h"
#include "monsters_internal.h"

static void pitch(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (!qa_q1mpm_str_eq(game->host->contents(game->host->ctx, qa_q1mpm_monster_origin(monster)), "water")) {
    qa_q1mpm_drop_to_floor(monster);
    qa_actor_id world = game->world == NULL ? entity->actor.id : game->world->actor.id;
    game->damage(game, entity->actor.id, world,
                 game->world == NULL ? QA_Q1MPM_NONE : QA_Q1MPM_SOME(game->world->actor.id), 6, NULL, "direct", "");
    return;
  }
  if (game->time < entity->delay) return;
  double weapon = qa_q1mpm_actor_number(entity, "weapon");
  if (weapon > 10) weapon = -10;
  qa_q1mpm_body body = game->body(game, entity);
  if (weapon != 0) {
    qa_q1mpm_body_patch patch = {0};
    patch.has_angles = true;
    patch.angles = body.angles;
    patch.angles.x = body.angles.x + (weapon < 0 ? -1.5 : 1.5);
    game->set_body(game, entity, &patch);
  }
  qa_q1mpm_number(monster, "weapon", weapon + 1);
}
static void charge(void *ctx, qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_monster_ai(monster, QA_Q1MPM_AI_CHARGE, 8);
  pitch(ctx, monster);
}
static void attack8(void *ctx, qa_q1mpm_mission_monster *monster) {
  monster->entity->effects = 8;
  monster->entity->skin = 1;
  charge(ctx, monster);
  monster->game->sound(monster->game, monster->entity, "eel/eatt1.wav", QA_Q1MPM_CH("weapon"), 1, 1);
}
static void attack9(void *ctx, qa_q1mpm_mission_monster *monster) {
  monster->entity->skin = 2;
  charge(ctx, monster);
}
static void attack10(void *ctx, qa_q1mpm_mission_monster *monster) {
  monster->entity->skin = 3;
  charge(ctx, monster);
}
static void attack11(void *ctx, qa_q1mpm_mission_monster *monster) {
  monster->entity->effects = 4;
  monster->entity->skin = 4;
  charge(ctx, monster);
}
static void attack12(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->entity->skin = 5;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_vec3 target;
  if (monster->state.enemy.has && qa_q1mpm_monster_target(monster, &target)) {
    qa_q1mpm_trace_request request = {qa_q1mpm_monster_origin(monster), target, QA_Q1MPM_POINT,
                                      QA_Q1MPM_SOME(entity->actor.id), true, false};
    qa_q1mpm_trace trace = game->host->trace(game->host->ctx, &request);
    if (trace.actor.has && qa_same_actor(trace.actor.id, monster->state.enemy.id) &&
        !(trace.in_open && trace.in_water))
      qa_q1mpm_eel_zap(monster);
  }
  entity->skin = 0;
  entity->effects = 0;
}
static void death1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->entity->skin = 0;
  monster->entity->effects = 0;
  qa_q1mpm_game_sound(monster->game, monster->entity, "eel/edie3r.wav");
}
static void death11(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->entity->movement_flags -= 2;
}
static void droptofloor_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_drop_to_floor(monster);
}
static void pain1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (monster->state.pain_finished > monster->game->time) return;
  monster->state.pain_finished = monster->game->time + 1;
  qa_q1mpm_game_sound(monster->game, monster->entity, "eel/epain3.wav");
  monster->entity->skin = 0;
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->entity->delay = monster->game->time + monster->game->host->random(monster->game->host->ctx) * 6;
  qa_q1mpm_number(monster, "weapon", 0);
  qa_q1mpm_monster_spawn_default(monster);
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  qa_q1mpm_monster_play(monster, "eel_pain1");
}
static void melee_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_play(monster, "eel_attack1");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  (void)ctx; (void)attacker;
  monster->entity->movement_flags += 2;
  monster->game->set_bounds(monster->game, monster->entity, QA_Q1MPM_POINT);
  if (monster->game->health(monster->game, monster->entity->actor.id) < -12) {
    monster->entity->skin = 0;
    monster->entity->effects = 0;
    static const char *const gibs[] = {"gib1", "gib1", "gib1"};
    qa_q1mpm_gib(monster, "eelgib", gibs, 3, "");
    return;
  }
  qa_q1mpm_monster_play(monster, "eel_death1");
}
static const char *const classnames[] = {"monster_eel"};
static const char *const spec_gibs[] = {"gib1", "gib1", "gib1"};
static const qa_q1mpm_named_action actions[] = {
    {"eel_pitch_change", {pitch, NULL}},
    {"eel:eel_attack8", {attack8, NULL}},
    {"eel:eel_attack9", {attack9, NULL}},
    {"eel:eel_attack10", {attack10, NULL}},
    {"eel:eel_attack11", {attack11, NULL}},
    {"eel:eel_attack12", {attack12, NULL}},
    {"eel:eel_death1", {death1, NULL}},
    {"eel:eel_death11", {death11, NULL}},
    {"droptofloor", {droptofloor_action, NULL}},
    {"eel:eel_pain1", {pain1, NULL}},
};
const qa_q1mpm_pack_monster_definition qa_q1mpm_eel_definition = {
    {"eel", NULL, classnames, 1, "eel2", "eelgib", 60, -12, spec_gibs, 3,
     {{-16, -16, -24}, {16, 16, 32}}, "eel_stand1", "eel_walk1", "eel_run1",
     "eel/eelc5.wav", NULL, true, "swim"},
    false, false,
    {qa_q1mpm_tables_eel_entries, QA_Q1MPM_TABLES_EEL_COUNT},
    actions, sizeof(actions) / sizeof(actions[0]),
    NULL, 0,
    {spawn, NULL}, {NULL, NULL},
    pain, NULL, die, NULL,
    {melee_action, NULL},
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};
