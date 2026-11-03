/* Port of src/content/q1/addons/monsters/ordinary/army.ts.
 * quakec_{mg1,mg3}/monsters/soldier.qc and fight.qc SoldierCheckAttack. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ordinary/army.h"

#include <math.h>
#include <string.h>

#include "qa/content/q1/foundation/weapons.h"
#include "../support.h"

static const char *const army_classnames[] = {"monster_army"};
static const char *const army_gibs[] = {"gib1", "gib2", "gib3"};
const qa_q1_base_monster_species qa_q1am_army_species = {
  .species = "army", .classnames = army_classnames, .classname_count = 1, .model = "soldier", .head = "h_guard",
  .health = 30, .gib_health = -35, .gibs = army_gibs, .gib_count = 3, .bounds = {{-16, -16, -24}, {16, 16, 32}},
  .stand = "army_stand1", .walk = "army_walk1", .run = "army_run1", .sight = "soldier/sight1.wav",
  .missile = "army_atk1", .melee = false, .movement = "walk",
};
void qa_q1am_army_fire(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_body target;
  if (!monster->state->has_enemy || !qa_q1am_read_body(game, monster->state->enemy, &target)) return;
  qa_q1_base_monster_face(monster);
  qa_q1_sound(game, entity->actor.id, "soldier/sattck1.wav", "weapon", 1, 1);
  const qa_vec3 direction = qa_q1_normalize(qa_sub3(qa_sub3(target.origin, qa_scale3(target.velocity, 0.2)), qa_q1_base_monster_origin(monster)));
  qa_q1_fire_bullets(game, entity->actor, direction, qa_q1_get_body(game, entity).angles, 4, 0.1, 0.1, NULL);
  entity->effects = QA_Q1AM_FLAGS(entity->effects) | 2;
}
void qa_q1am_army_refire(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx;
  qa_actor_id enemy;
  const qa_actor_id *current = NULL;
  if (monster->state->has_enemy) { enemy = monster->state->enemy; current = &enemy; }
  if (qa_q1_option_skill(monster->game) == 3 && !monster->state->refired && qa_q1_base_monster_visible(monster, current)) {
    monster->state->refired = true;
    qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, "army_atk1");
  }
}
const qa_q1_monster_action_entry qa_q1am_army_actions[] = {
  {"army_fire", qa_q1am_army_fire, NULL},
  {"army_refire", qa_q1am_army_refire, NULL},
};
const size_t qa_q1am_army_action_count = sizeof qa_q1am_army_actions / sizeof qa_q1am_army_actions[0];

bool qa_q1am_army_attack(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const double range = game->world == NULL ? 0 : qa_q1_actor_number(game->world, "enemy_range");
  if (!monster->state->has_enemy) return false;
  const qa_actor_id enemy = monster->state->enemy;
  qa_vec3 start, end;
  if (!qa_q1_base_monster_eye(monster, entity->actor.id, &start) || !qa_q1_base_monster_eye(monster, enemy, &end)) return false;
  const qa_q1_trace trace = qa_q1am_trace(game, start, end, QA_Q1_POINT, &entity->actor.id, true);
  if (!trace.has_actor || !qa_same_actor(trace.actor, enemy) || (trace.in_open && trace.in_water) || range == 3 ||
      game->time < monster->state->attack_finished ||
      qa_q1am_random(game) >= (range == 0 ? 0.9 : range == 1 ? 0.4 : range == 2 ? 0.05 : 0))
    return false;
  qa_q1_base_monster_play(monster, "army_atk1");
  qa_q1_base_monster_attack_finished(monster, 1 + qa_q1am_random(game));
  if (qa_q1am_random(game) < 0.3) {
    monster->lefty = !monster->lefty;
    qa_q1am_field_text(entity, "lefty", monster->lefty ? "1" : "0");
  }
  return true;
}
void qa_q1am_army_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage, bool nightmare_resistance) {
  qa_q1_base_monster_retaliate(monster, attacker);
  qa_q1_services *game = monster->game;
  qa_q1_monster *state = monster->state;
  if (state->pain_finished > game->time ||
      (nightmare_resistance && qa_q1_option_skill(game) > 2 && qa_q1am_random(game) * 100 > damage))
    return;
  const double r = qa_q1am_random(game);
  state->pain_finished = game->time + (r < 0.2 ? 0.6 : 1.1);
  qa_q1_base_monster_play(monster, r < 0.2 ? "army_pain1" : r < 0.6 ? "army_painb1" : "army_painc1");
  qa_q1_sound(game, monster->entity->actor.id, r < 0.2 ? "soldier/pain1.wav" : "soldier/pain2.wav", "voice", 1, 1);
}
