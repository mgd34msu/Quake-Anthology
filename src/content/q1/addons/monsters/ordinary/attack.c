/* Port of src/content/q1/addons/monsters/ordinary/attack.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ordinary/attack.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/ordinary/army.h"

static void set_attack_state(qa_q1am_actor *entity, const char *state) {
  (void)snprintf(entity->attack_state, sizeof entity->attack_state, "%s", state);
}
static void straight(qa_q1am_monster *monster) {
  qa_q1am_actor *entity = monster->entity;
  if (!monster->sliding && strcmp(entity->attack_state, "straight") == 0) return;
  monster->sliding = false;
  set_attack_state(entity, "straight");
  monster->ops.play(monster, "wiz_run1");
}
bool qa_q1am_mg3_ordinary_attack(qa_q1am_monster *monster) {
  qa_q1am_game *game = monster->game;
  qa_q1am_actor *entity = monster->entity;
  if ((game->world ? qa_q1am_actor_number(game->world, "enemy_visible") : 0) == 0 ||
      !monster->state->has_enemy)
    return false;
  qa_actor_id enemy = monster->state->enemy;
  double range = game->world ? qa_q1am_actor_number(game->world, "enemy_range") : 0;
  if (strcmp(entity->classname, "monster_army") == 0) return qa_q1am_army_attack(monster);
  if (strcmp(entity->classname, "monster_demon1") == 0 || strcmp(entity->classname, "monster_dog") == 0) {
    if (range == 0) {
      set_attack_state(entity, "melee");
      return true;
    }
    qa_q1am_body body = game->body(game->ctx, entity);
    qa_q1am_body target;
    if (!game->body_read(game->ctx, enemy, &target)) return false;
    double height = target.bounds.max.z - target.bounds.min.z;
    if (body.origin.z + body.bounds.min.z > target.origin.z + target.bounds.min.z + height * 0.75 ||
        body.origin.z + body.bounds.max.z < target.origin.z + target.bounds.min.z + height * 0.25)
      return false;
    qa_vec3 delta = qa_sub3(target.origin, body.origin);
    double distance = hypot(delta.x, delta.y);
    bool demon = strcmp(entity->classname, "monster_demon1") == 0;
    if (demon ? (distance < 100 || (distance > 200 && game->random(game->ctx) < 0.9))
              : (distance < 80 || distance > 150))
      return false;
    set_attack_state(entity, "missile");
    if (demon) game->sound(game->ctx, entity, "demon/djump.wav", "voice", 1);
    return true;
  }
  bool shambler = strcmp(entity->classname, "monster_shambler") == 0;
  bool ogre = strcmp(entity->classname, "monster_ogre") == 0;
  bool wizard = strcmp(entity->classname, "monster_wizard") == 0;
  if (!shambler && !ogre && !wizard) return monster->ops.check_attack(monster);
  if (!wizard && range == 0 && game->can_damage(game->ctx, enemy, entity->actor.id)) {
    set_attack_state(entity, "melee");
    return true;
  }
  if (game->time < monster->state->attack_finished) return false;
  if (wizard && range == 3) {
    straight(monster);
    return false;
  }
  qa_vec3 start, end;
  if (!qa_q1am_monster_eye(monster, entity->actor.id, &start) ||
      !qa_q1am_monster_eye(monster, enemy, &end))
    return false;
  if (shambler && qa_length3(qa_sub3(start, end)) > 600) return false;
  qa_q1am_trace_request request = {start, end, QA_Q1AM_POINT, true, entity->actor.id, true};
  qa_q1am_trace trace = game->trace(game->ctx, &request);
  if (!trace.has_actor || !qa_same_actor(trace.actor, enemy) ||
      (!wizard && trace.in_open && trace.in_water)) {
    if (wizard) straight(monster);
    return false;
  }
  if (!wizard) {
    if (range == 3) return false;
    set_attack_state(entity, "missile");
    monster->ops.attack_finished(monster, (shambler ? 2 : 1) + 2 * game->random(game->ctx));
    return true;
  }
  double chance = range == 0 ? 0.9 : range == 1 ? 0.6 : range == 2 ? 0.2 : 0;
  if (game->random(game->ctx) < chance) {
    monster->sliding = false;
    set_attack_state(entity, "missile");
    return true;
  }
  if (range == 2) straight(monster);
  else if (!monster->sliding) {
    monster->sliding = true;
    set_attack_state(entity, "straight");
    monster->ops.play(monster, "wiz_side1");
  }
  return false;
}
