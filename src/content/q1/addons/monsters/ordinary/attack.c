/* Port of src/content/q1/addons/monsters/ordinary/attack.ts.
 * quakec_mg3/ai.qc CheckAnyAttack and species attack policies. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ordinary/attack.h"

#include <string.h>

#include "qa/content/q1/addons/monsters/ordinary/army.h"
#include "qa/content/q1/addons/monsters/ordinary/classic.h"
#include "../support.h"

static bool classname_is(const qa_q1_actor *entity, const char *classname) { return strcmp(entity->classname, classname) == 0; }
/* TS straight(): drop out of a slide back to the run. */
static void straight(qa_q1_base_monster *monster) {
  if (!monster->sliding && strcmp(monster->entity->attack_state, "straight") == 0) return;
  monster->sliding = false;
  monster->entity->attack_state = "straight";
  qa_q1_base_monster_play(monster, "wiz_run1");
}
bool qa_q1am_mg3_ordinary_attack(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if ((game->world == NULL ? 0 : qa_q1_actor_number(game->world, "enemy_visible")) == 0 || !monster->state->has_enemy) return false;
  const qa_actor_id enemy = monster->state->enemy;
  const double range = game->world == NULL ? 0 : qa_q1_actor_number(game->world, "enemy_range");
  if (classname_is(entity, "monster_army")) return qa_q1am_army_attack(monster);
  if (classname_is(entity, "monster_dog")) return qa_q1am_dog_attack(monster, range);
  if (classname_is(entity, "monster_demon1")) {
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
    if (distance < 100 || (distance > 200 && qa_q1am_random(game) < 0.9)) return false;
    entity->attack_state = "missile";
    qa_q1_sound(game, entity->actor.id, "demon/djump.wav", "voice", 1, 1);
    return true;
  }
  const bool shambler = classname_is(entity, "monster_shambler"), ogre = classname_is(entity, "monster_ogre"),
             wizard = classname_is(entity, "monster_wizard");
  if (!shambler && !ogre && !wizard) return qa_q1am_mg3_check_attack(monster);
  if (!wizard && range == 0 && qa_q1_can_damage(game, enemy, entity->actor.id)) { entity->attack_state = "melee"; return true; }
  if (game->time < monster->state->attack_finished) return false;
  if (wizard && range == 3) { straight(monster); return false; }
  qa_vec3 start, end;
  if (!qa_q1_base_monster_eye(monster, entity->actor.id, &start) || !qa_q1_base_monster_eye(monster, enemy, &end)) return false;
  if (shambler && qa_length3(qa_sub3(start, end)) > 600) return false;
  const qa_q1_trace trace = qa_q1am_trace(game, start, end, QA_Q1_POINT, &entity->actor.id, true);
  if (!trace.has_actor || !qa_same_actor(trace.actor, enemy) || (!wizard && trace.in_open && trace.in_water)) {
    if (wizard) straight(monster);
    return false;
  }
  if (!wizard) {
    if (range == 3) return false;
    entity->attack_state = "missile";
    qa_q1_base_monster_attack_finished(monster, (shambler ? 2 : 1) + 2 * qa_q1am_random(game));
    return true;
  }
  const double chance = range == 0 ? 0.9 : range == 1 ? 0.6 : range == 2 ? 0.2 : 0;
  if (qa_q1am_random(game) < chance) {
    monster->sliding = false;
    entity->attack_state = "missile";
    return true;
  }
  if (range == 2) {
    straight(monster);
  } else if (!monster->sliding) {
    monster->sliding = true;
    entity->attack_state = "straight";
    qa_q1_base_monster_play(monster, "wiz_side1");
  }
  return false;
}
