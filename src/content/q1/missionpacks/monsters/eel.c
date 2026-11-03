/* Port of src/content/q1/missionpacks/monsters/eel.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/eel.h"

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/eel.h"
#include "monsters_internal.h"

static void pitch(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (strcmp(game->host.contents(game->host.ctx, qa_q1_base_monster_origin(&monster->base)), "water") != 0) {
    (void)qa_q1mpm_drop_to_floor(monster);
    const qa_actor_id inflictor = game->world == NULL ? entity->actor.id : game->world->actor.id;
    (void)qa_q1_damage(game, entity->actor.id, inflictor, game->world == NULL ? NULL : &game->world->actor.id, 6,
                       NULL, NULL, NULL, NULL);
    return;
  }
  if (game->time < entity->delay) return;
  double weapon = qa_q1_actor_number(entity, "weapon");
  if (weapon > 10) weapon = -10;
  qa_q1_body body = qa_q1_get_body(game, entity);
  if (weapon != 0) {
    qa_q1_body_patch patch = {0};
    patch.has_angles = true;
    patch.angles = body.angles;
    patch.angles.x = body.angles.x + (weapon < 0 ? -1.5 : 1.5);
    qa_q1_set_body(game, entity, &patch);
  }
  qa_q1mpm_number(monster, "weapon", weapon + 1);
}
static void charge(void *ctx, qa_q1mpm_mission_monster *monster) {
  qa_q1_base_monster_ai(&monster->base, QA_Q1_AI_CHARGE, 8);
  pitch(ctx, monster);
}
static void attack8(void *ctx, qa_q1mpm_mission_monster *monster) {
  monster->base.entity->effects = 8;
  monster->base.entity->skin = 1;
  charge(ctx, monster);
  qa_q1_sound(monster->base.game, monster->base.entity->actor.id, "eel/eatt1.wav", "weapon", 1, 1);
}
static void attack9(void *ctx, qa_q1mpm_mission_monster *monster) {
  monster->base.entity->skin = 2;
  charge(ctx, monster);
}
static void attack10(void *ctx, qa_q1mpm_mission_monster *monster) {
  monster->base.entity->skin = 3;
  charge(ctx, monster);
}
static void attack11(void *ctx, qa_q1mpm_mission_monster *monster) {
  monster->base.entity->effects = 4;
  monster->base.entity->skin = 4;
  charge(ctx, monster);
}
static void attack12(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->base.entity->skin = 5;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_vec3 target;
  if (monster->base.state->has_enemy && qa_q1_base_monster_target(&monster->base, &target)) {
    qa_q1_trace trace = q1mpm_trace(game, qa_q1_base_monster_origin(&monster->base), target, QA_Q1_POINT,
                                    &entity->actor.id, true);
    if (trace.has_actor && qa_same_actor(trace.actor, monster->base.state->enemy) &&
        !(trace.in_open && trace.in_water))
      qa_q1mpm_eel_zap(monster);
  }
  entity->skin = 0;
  entity->effects = 0;
}
static void death1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->base.entity->skin = 0;
  monster->base.entity->effects = 0;
  qa_q1_sound_defaults(monster->base.game, monster->base.entity->actor.id, "eel/edie3r.wav");
}
static void death11(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->base.entity->movement_flags -= 2;
}
static void droptofloor_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  (void)qa_q1mpm_drop_to_floor(monster);
}
static void pain1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (monster->base.state->pain_finished > monster->base.game->time) return;
  monster->base.state->pain_finished = monster->base.game->time + 1;
  qa_q1_sound_defaults(monster->base.game, monster->base.entity->actor.id, "eel/epain3.wav");
  monster->base.entity->skin = 0;
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->base.entity->delay = monster->base.game->time + qa_q1_host_random(monster->base.game) * 6;
  qa_q1mpm_number(monster, "weapon", 0);
  qa_q1mpm_monster_spawn_default(monster);
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  qa_q1_base_monster_play(&monster->base, "eel_pain1");
}
static void melee_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_play(&monster->base, "eel_attack1");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker;
  monster->base.entity->movement_flags += 2;
  qa_q1_set_bounds(monster->base.game, monster->base.entity, QA_Q1_POINT);
  if (qa_q1_health(monster->base.game, monster->base.entity->actor.id) < -12) {
    monster->base.entity->skin = 0;
    monster->base.entity->effects = 0;
    static const char *const gibs[] = {"gib1", "gib1", "gib1"};
    qa_q1mpm_gib(monster, "eelgib", gibs, 3, "");
    return;
  }
  qa_q1_base_monster_play(&monster->base, "eel_death1");
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
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};
