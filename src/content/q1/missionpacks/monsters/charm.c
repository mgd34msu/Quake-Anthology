/* Port of src/content/q1/missionpacks/monsters/charm.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/charm.h"

#include <math.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "monsters_internal.h"

qa_q1mpm_opt_actor qa_q1mpm_charmer(qa_q1mpm_mission_monster *monster) {
  return qa_q1mpm_actor_number(monster->entity, "charmed") == 0
             ? QA_Q1MPM_NONE
             : qa_q1mpm_references_get(monster->entity, "charmer");
}
static qa_q1mpm_actor *update_goal(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_opt_actor owner = qa_q1mpm_charmer(monster);
  qa_q1mpm_body body;
  if (!owner.has || !game->host->bodies_read(game->host->ctx, owner.id, &body)) return NULL;
  qa_q1mpm_actor *goal = game->entity(game, qa_q1mpm_references_get(entity, "trigger_field"));
  if (qa_q1mpm_actor_number(entity, "huntingcharmer") == 1) {
    goal = game->create(game, "charmed_goal");
    qa_q1mpm_references_set(entity, "trigger_field", QA_Q1MPM_SOME(goal->actor.id));
    game->set_origin(game, goal, body.origin);
    qa_q1mpm_number(monster, "huntingcharmer", 2);
    qa_q1mpm_references_set(entity, "goalentity", QA_Q1MPM_SOME(goal->actor.id));
  }
  if (goal == NULL) return NULL;
  if (qa_q1mpm_actor_number(entity, "huntingcharmer") == 2) {
    qa_q1mpm_trace_request request = {qa_q1mpm_monster_origin(monster), body.origin, QA_Q1MPM_POINT,
                                      QA_Q1MPM_SOME(entity->actor.id), false, false};
    if (game->host->trace(game->host->ctx, &request).fraction == 1) game->set_origin(game, goal, body.origin);
  } else {
    qa_vec3 away = qa_q1mpm_vadd(body.origin, qa_q1mpm_vscale(qa_q1mpm_normalize(qa_q1mpm_vsub(
                                                  qa_q1mpm_monster_origin(monster), body.origin)), 300));
    game->set_origin(game, goal, away);
  }
  return goal;
}
void qa_q1mpm_hunt_charmer(qa_q1mpm_mission_monster *monster, bool flee) {
  qa_q1mpm_number(monster, "huntingcharmer", 1);
  qa_q1mpm_actor *goal = update_goal(monster);
  if (flee) qa_q1mpm_number(monster, "huntingcharmer", 3);
  else if (goal != NULL)
    monster->entity->ideal_yaw = qa_q1mpm_yaw_for(qa_q1mpm_vsub(
        monster->game->body(monster->game, goal).origin, qa_q1mpm_monster_origin(monster)));
  qa_q1mpm_monster_set_next_frame(monster, qa_q1mpm_monster_spec(monster)->walk);
  qa_q1mpm_monster_delay(monster, 0.1);
}
static void stop_hunting(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_actor *goal = monster->game->entity(
      monster->game, qa_q1mpm_references_get(monster->entity, "trigger_field"));
  if (qa_q1mpm_actor_number(monster->entity, "huntingcharmer") > 1 && goal != NULL)
    monster->game->remove(monster->game, goal);
  qa_q1mpm_references_set(monster->entity, "goalentity", QA_Q1MPM_NONE);
  qa_q1mpm_number(monster, "huntingcharmer", 0);
  qa_q1mpm_monster_set_next_frame(monster, qa_q1mpm_monster_spec(monster)->stand);
  qa_q1mpm_monster_delay(monster, 0.1);
}
bool qa_q1mpm_find_charmed_target(qa_q1mpm_mission_monster *monster, bool *out_null) {
  *out_null = false;
  qa_q1mpm_opt_actor owner = qa_q1mpm_charmer(monster);
  if (!owner.has) { *out_null = true; return false; }
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_body owner_body;
  if (!game->host->bodies_read(game->host->ctx, owner.id, &owner_body)) return false;
  entity->effects |= 8;
  qa_vec3 origin = qa_q1mpm_monster_origin(monster);
  if (qa_q1mpm_actor_number(entity, "huntingcharmer") > 0) {
    qa_q1mpm_actor *goal = update_goal(monster);
    double distance = goal == NULL ? INFINITY
                                   : qa_q1mpm_length(qa_q1mpm_vsub(origin, game->body(game, goal).origin));
    if (distance < 150) {
      if (qa_q1mpm_actor_number(entity, "huntingcharmer") == 3 && distance > 120) return false;
      stop_hunting(monster);
      return true;
    }
  } else if (qa_q1mpm_length(qa_q1mpm_vsub(origin, owner_body.origin)) > 200) {
    qa_q1mpm_hunt_charmer(monster, false);
    return false;
  } else if (qa_q1mpm_length(qa_q1mpm_vsub(origin, owner_body.origin)) < 120) {
    qa_q1mpm_hunt_charmer(monster, true);
    return false;
  }
  qa_actor_id actors[512];
  size_t count = qa_q1mpm_radius_actors(game, origin, 1500, actors, 512);
  qa_q1mpm_opt_actor selected = QA_Q1MPM_NONE;
  double distance = 1500;
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = actors[i];
    qa_q1mpm_actor *candidate = game->entity(game, QA_Q1MPM_SOME(actor));
    qa_q1mpm_body body;
    if (candidate == NULL || !game->host->bodies_read(game->host->ctx, actor, &body)) continue;
    if ((candidate->movement_flags & 128) != 0 || (candidate->movement_flags & 32) == 0) continue;
    if (qa_same_actor(actor, entity->actor.id) || qa_same_actor(actor, owner.id)) continue;
    if (qa_q1mpm_opt_eq(qa_q1mpm_references_get(candidate, "charmer"), owner)) continue;
    if (game->health(game, actor) <= 0 || !qa_q1mpm_monster_visible_actor(monster, actor)) continue;
    double range = qa_q1mpm_monster_range_distance_actor(monster, actor);
    if (range < distance) { selected = QA_Q1MPM_SOME(actor); distance = range; }
  }
  if (!selected.has || qa_q1mpm_opt_eq(selected, monster->state.enemy) || distance >= 1000 ||
      qa_q1mpm_player_powerup(game->player(game, selected.id), "invisibility") > game->time)
    return false;
  qa_q1mpm_monster_found(monster, selected.id);
  return true;
}
bool qa_q1mpm_find_hipnotic_target(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_opt_actor candidate;
  if (game->sight_entity != NULL && game->sight_time >= game->time - 0.1 &&
      (entity->spawnflags & 3) == 0) {
    qa_q1mpm_opt_actor sight_enemy = game->sight_entity->monster == NULL
                                         ? QA_Q1MPM_NONE
                                         : game->sight_entity->monster->enemy;
    if (qa_q1mpm_opt_eq(sight_enemy, monster->state.enemy) && game->sight_entity->monster != NULL)
      return false;
    candidate = QA_Q1MPM_SOME(game->sight_entity->actor.id);
  } else {
    candidate = game->host->check_client(game->host->ctx, entity->actor);
  }
  if (!candidate.has || qa_q1mpm_opt_eq(candidate, monster->state.enemy)) return false;
  qa_q1mpm_actor *target = game->entity(game, candidate);
  qa_q1mpm_body body;
  if (!game->host->bodies_read(game->host->ctx, candidate.id, &body)) return false;
  if (((target == NULL ? 0 : target->movement_flags) & 128) != 0) return false;
  if (qa_q1mpm_player_powerup(game->player(game, candidate.id), "invisibility") > game->time) return false;
  double distance = qa_q1mpm_monster_range_distance_actor(monster, candidate.id);
  if (distance >= 1000 || !qa_q1mpm_monster_visible_actor(monster, candidate.id)) return false;
  if (distance >= 120) {
    qa_q1mpm_player_state *player = game->player(game, candidate.id);
    double hostile = player != NULL ? player->hostile_until
                                    : target == NULL ? 0 : qa_q1mpm_actor_number(target, "show_hostile");
    qa_vec3 forward = game->make_vectors(game, game->body(game, entity).angles).forward;
    if ((distance >= 500 || hostile < game->time) &&
        qa_q1mpm_dot(qa_q1mpm_normalize(qa_q1mpm_vsub(body.origin, qa_q1mpm_monster_origin(monster))), forward) <= 0.3)
      return false;
  }
  if ((target == NULL ? 0 : qa_q1mpm_actor_number(target, "charmed")) == 0 &&
      !game->is_player(game, candidate)) {
    candidate = target == NULL || target->monster == NULL ? QA_Q1MPM_NONE : target->monster->enemy;
    if (!candidate.has || !game->is_player(game, candidate)) return false;
  }
  qa_q1mpm_monster_found(monster, candidate.id);
  return true;
}
bool qa_q1mpm_walk_with_charmer(qa_q1mpm_mission_monster *monster, double distance) {
  if (!qa_q1mpm_charmer(monster).has) return false;
  if (qa_q1mpm_monster_find_target(monster)) return true;
  qa_q1mpm_actor *goal = monster->game->entity(
      monster->game, qa_q1mpm_references_get(monster->entity, "goalentity"));
  if (goal == NULL) {
    qa_q1mpm_actor *found[1];
    if (monster->game->find(monster->game, monster->state.path, found, 1) > 0) goal = found[0];
  }
  if (goal != NULL)
    monster->game->host->move_to_goal(monster->game->host->ctx, monster->entity->actor, goal->actor.id,
                                      distance, NULL);
  if (qa_q1mpm_actor_number(monster->entity, "huntingcharmer") != 0)
    qa_q1mpm_monster_delay(monster, (monster->entity->next_think - monster->game->time) / 2);
  return true;
}
