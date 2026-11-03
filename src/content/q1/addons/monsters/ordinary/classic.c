/* Port of src/content/q1/addons/monsters/ordinary/classic.ts.
 * quakec_{mg1,mg3}/monsters/{soldier,dog}.qc: the ordinary grunt and dog under InitMonster.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ordinary/classic.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/ordinary/army.h"
#include "qa/content/q1/base/monster_actions.h"
#include "qa/content/q1/base/projectiles.h"
#include "../support.h"

static const char *const dog_classnames[] = {"monster_dog"};
static const char *const dog_gibs[] = {"gib3", "gib3", "gib3"};
const qa_q1_base_monster_species qa_q1am_dog_species = {
  .species = "dog", .classnames = dog_classnames, .classname_count = 1, .model = "dog", .head = "h_dog",
  .health = 25, .gib_health = -35, .gibs = dog_gibs, .gib_count = 3, .bounds = {{-32, -32, -24}, {32, 32, 64}},
  .stand = "dog_stand1", .walk = "dog_walk1", .run = "dog_run1", .sight = "dog/dsight.wav", .missile = "dog_leap1",
  .melee = true, .movement = "walk",
};

/* MG1 items.qc DropBackpack returns when a horde manager exists. */
static bool horde_active(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  for (size_t i = 0; i < game->entity_count; i++)
    if (strcmp(game->entities[i]->classname, "horde_manager") == 0) return true;
  return false;
}
static void army_die3(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx;
  if (horde_active(monster)) return;
  qa_q1_backpack_contents contents;
  memset(&contents, 0, sizeof contents);
  contents.weapon = NULL;
  contents.shells = 5;
  (void)qa_q1_drop_backpack(monster->game, qa_q1_base_monster_origin(monster), &contents, NULL);
}
static void dog_bite(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx;
  if (!monster->state->has_enemy) return;
  qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 10);
  (void)qa_q1_base_monster_melee(monster, 100, 8, 3, true);
}
static void dog_jump(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_body body = qa_q1_get_body(game, entity);
  char name[96];
  (void)snprintf(name, sizeof name, "%s:dog_jump_touch",
                 monster->has_source && monster->source.callback_prefix != NULL ? monster->source.callback_prefix : "base");
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, name);
  entity->movement_flags = QA_Q1AM_FLAGS(entity->movement_flags) & ~512;
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = qa_add3(body.origin, (qa_vec3){0, 0, 1});
  patch.has_velocity = true;
  patch.velocity = qa_add3(qa_scale3(qa_q1_make_vectors(game, body.angles).forward, 300), (qa_vec3){0, 0, 200});
  patch.has_ground = true;
  patch.ground = NULL;
  qa_q1_set_body(game, entity, &patch);
}
const qa_q1_monster_action_entry qa_q1am_classic_actions[] = {
  {"army_fire", qa_q1am_army_fire, NULL}, {"army_refire", qa_q1am_army_refire, NULL}, /* ...armyActions */
  {"army_die3", army_die3, NULL}, {"dog_bite", dog_bite, NULL}, {"dog_jump", dog_jump, NULL},
};
const size_t qa_q1am_classic_action_count = sizeof qa_q1am_classic_actions / sizeof qa_q1am_classic_actions[0];

/* entry ctx = the action name the TS closure captures. */
static void horde_no_backpack(void *ctx, qa_q1_base_monster *monster) {
  if (!horde_active(monster)) qa_q1_monster_action(monster, ctx);
}
static void no_backpack(void *ctx, qa_q1_base_monster *monster) { (void)ctx; (void)monster; /* TS () => undefined */ }
#define NAMED(N) (void *)(uintptr_t)(N)
const qa_q1_monster_action_entry qa_q1am_mg1_horde_no_backpack_actions[] = {
  {"enf_die3", horde_no_backpack, NAMED("enf_die3")}, {"enf_fdie3", horde_no_backpack, NAMED("enf_fdie3")},
  {"ogre_die3", horde_no_backpack, NAMED("ogre_die3")}, {"ogre_bdie3", horde_no_backpack, NAMED("ogre_bdie3")},
};
const size_t qa_q1am_mg1_horde_no_backpack_action_count =
    sizeof qa_q1am_mg1_horde_no_backpack_actions / sizeof qa_q1am_mg1_horde_no_backpack_actions[0];
const qa_q1_monster_action_entry qa_q1am_mg3_no_backpack_actions[] = {
  {"enf_die3", no_backpack, NULL}, {"enf_fdie3", no_backpack, NULL}, {"ogre_die3", no_backpack, NULL},
  {"ogre_bdie3", no_backpack, NULL},
};
const size_t qa_q1am_mg3_no_backpack_action_count =
    sizeof qa_q1am_mg3_no_backpack_actions / sizeof qa_q1am_mg3_no_backpack_actions[0];
#undef NAMED

void qa_q1am_dog_jump_touch(qa_q1_base_monster *monster, qa_actor_id other) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (qa_q1_health(game, entity->actor.id) <= 0) return;
  if (qa_q1am_can_take_damage(game, other) && monster->state->attack_finished < game->time &&
      qa_length3(qa_q1_get_body(game, entity).velocity) > 300) {
    const qa_actor_id self = entity->actor.id;
    (void)qa_q1_damage(game, other, self, &self, 10 + 10 * qa_q1am_random(game), NULL, NULL, NULL, NULL);
    monster->state->attack_finished = game->time + 0.5;
  }
  if (!game->host.check_bottom(game->host.ctx, entity->actor.id)) {
    if ((QA_Q1AM_FLAGS(entity->movement_flags) & 512) != 0) {
      memset(&entity->touch, 0, sizeof entity->touch);
      qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, "dog_leap1");
      qa_q1_base_monster_delay(monster, 0.1);
    }
    return;
  }
  memset(&entity->touch, 0, sizeof entity->touch);
  qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, "dog_run1");
  qa_q1_base_monster_delay(monster, 0.1);
}
void qa_q1am_dog_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker) {
  qa_q1_base_monster_retaliate(monster, attacker);
  qa_q1_sound(monster->game, monster->entity->actor.id, "dog/dpain1.wav", "voice", 1, 1);
  qa_q1_base_monster_play(monster, qa_q1am_random(monster->game) > 0.5 ? "dog_pain1" : "dog_painb1");
}
bool qa_q1am_dog_attack(qa_q1_base_monster *monster, double range) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (!monster->state->has_enemy) return false;
  const qa_actor_id enemy = monster->state->enemy;
  if (range == 0) { entity->attack_state = "melee"; return true; }
  const qa_q1_body body = qa_q1_get_body(game, entity);
  qa_q1_body target;
  if (!qa_q1am_read_body(game, enemy, &target)) return false;
  const double height = target.bounds.max.z - target.bounds.min.z;
  if (body.origin.z + body.bounds.min.z > target.origin.z + target.bounds.min.z + height * 0.75 ||
      body.origin.z + body.bounds.max.z < target.origin.z + target.bounds.min.z + height * 0.25)
    return false;
  const qa_vec3 delta = qa_sub3(target.origin, body.origin);
  const double distance = qa_js_hypot2(delta.x, delta.y);
  if (distance < 80 || distance > 150) return false;
  entity->attack_state = "missile";
  return true;
}
