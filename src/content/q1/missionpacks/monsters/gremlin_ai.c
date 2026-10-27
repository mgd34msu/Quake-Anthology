/* Port of src/content/q1/missionpacks/monsters/gremlin-ai.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/gremlin_ai.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/gremlin_weapons.h"
#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "monsters_internal.h"

qa_q1mpm_opt_actor qa_q1mpm_gremlin_find_victim(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  monster->state.search_until = game->time + 1;
  qa_q1mpm_opt_actor selected = QA_Q1MPM_NONE;
  double distance = 1000;
  qa_actor_id actors[512];
  size_t count = qa_q1mpm_radius_actors(game, qa_q1mpm_monster_origin(monster), 1000, actors, 512);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = actors[i];
    qa_q1mpm_actor *other = game->entity(game, QA_Q1MPM_SOME(actor));
    int32_t flags = (other == NULL ? 0 : other->movement_flags) |
                    (game->is_player(game, QA_Q1MPM_SOME(actor)) ? 8 : 0);
    qa_q1mpm_body body;
    bool has_body = game->host->bodies_read(game->host->ctx, actor, &body);
    if ((flags & 128) != 0 || (flags & (32 | 8)) == 0 || !qa_q1mpm_monster_visible_actor(monster, actor) ||
        game->health(game, actor) <= 0 || qa_same_actor(actor, entity->actor.id) || !has_body)
      continue;
    double range = qa_q1mpm_length(qa_q1mpm_vsub(body.origin, qa_q1mpm_monster_origin(monster)));
    if (qa_q1mpm_id_eq_opt(actor, qa_q1mpm_references_get(entity, "lastvictim"))) range *= 2;
    if ((flags & 8) != 0) range /= 1.5;
    if (other != NULL && strcmp(other->classname, entity->classname) == 0) range *= 1.5;
    if (range < distance) { distance = range; selected = QA_Q1MPM_SOME(actor); }
  }
  qa_q1mpm_references_set(entity, "lastvictim", selected);
  return selected;
}
bool qa_q1mpm_gremlin_find_target(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (qa_q1mpm_actor_number(entity, "stoleweapon") == 0 && game->time > entity->wait) {
    entity->wait = game->time + 1;
    double distance = 2000;
    qa_q1mpm_opt_actor gorge = QA_Q1MPM_NONE;
    qa_owned_actor observed[1024];
    size_t count = game->host->actors_observations(game->host->ctx, observed, 1024);
    for (size_t i = 0; i < count; i++) {
      qa_actor_id id = observed[i].id;
      qa_q1mpm_actor *other = game->entity(game, QA_Q1MPM_SOME(id));
      qa_q1mpm_body body;
      bool has_body = game->host->bodies_read(game->host->ctx, id, &body);
      int32_t flags = (other == NULL ? 0 : other->movement_flags) |
                      (game->is_player(game, QA_Q1MPM_SOME(id)) ? 8 : 0);
      if (!has_body || game->health(game, id) >= 1 || (flags & (32 | 8)) == 0) continue;
      double vertical = fabs(body.origin.z - qa_q1mpm_monster_origin(monster).z);
      bool visible = qa_q1mpm_monster_visible_actor(monster, id);
      qa_vec3 start, end;
      bool has_start = qa_q1mpm_monster_eye(monster, &start);
      bool has_end = qa_q1mpm_monster_eye_actor(monster, id, &end);
      double range = !has_start || !has_end ? INFINITY : qa_q1mpm_length(qa_q1mpm_vsub(end, start));
      if (visible && vertical < 80 && (other == NULL ? 0 : qa_q1mpm_actor_number(other, "gorging")) == 0 &&
          range < distance) {
        distance = range;
        gorge = QA_Q1MPM_SOME(id);
      }
    }
    if (gorge.has && distance < 700 * game->host->random(game->host->ctx)) {
      monster->state.old_enemy = monster->state.enemy;
      qa_q1mpm_number(monster, "gorging", 1);
      monster->state.enemy = gorge;
      monster->state.search_until = game->time + 4;
      qa_q1mpm_monster_found(monster, gorge.id);
      return true;
    }
  } else if (qa_q1mpm_actor_number(entity, "stoleweapon") != 0) {
    qa_q1mpm_opt_actor victim = qa_q1mpm_gremlin_find_victim(monster);
    if (victim.has) {
      qa_q1mpm_monster_found(monster, victim.id);
      monster->state.attack_finished = game->time;
      monster->state.search_until = game->time + 2;
      return true;
    }
  }
  bool found = qa_q1mpm_monster_find_target(monster);
  monster->state.search_until = game->time + 2;
  return found;
}
void qa_q1mpm_gremlin_walk(qa_q1mpm_mission_monster *monster, double distance) {
  if (qa_q1mpm_gremlin_find_target(monster)) return;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_actor *goal = game->entity(game, qa_q1mpm_references_get(entity, "goalentity"));
  if (goal == NULL) {
    qa_q1mpm_actor *found[1];
    if (game->find(game, monster->state.path, found, 1) > 0) goal = found[0];
  }
  if (goal != NULL) game->host->move_to_goal(game->host->ctx, entity->actor, goal->actor.id, distance, NULL);
}
void qa_q1mpm_gremlin_stand(qa_q1mpm_mission_monster *monster) {
  if (qa_q1mpm_monster_find_target(monster)) return;
  if (monster->game->time > monster->state.pause_until) qa_q1mpm_monster_play(monster, "gremlin_walk1");
}
void qa_q1mpm_gremlin_run(qa_q1mpm_mission_monster *monster, double distance) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (entity->water_type == -5) {
    qa_actor_id world = game->world == NULL ? entity->actor.id : game->world->actor.id;
    game->damage(game, entity->actor.id, world,
                 game->world == NULL ? QA_Q1MPM_NONE : QA_Q1MPM_SOME(game->world->actor.id), 2000, NULL, "direct", "");
  }
  if (qa_q1mpm_actor_number(entity, "stoleweapon") != 0) entity->frame += 164 - 29;
  qa_vec3 target;
  if (!qa_q1mpm_monster_target(monster, &target)) {
    qa_q1mpm_monster_ai(monster, QA_Q1MPM_AI_RUN, distance);
    return;
  }
  if (qa_q1mpm_actor_number(entity, "gorging") != 0) {
    qa_q1mpm_trace_request request = {qa_q1mpm_monster_origin(monster), target, QA_Q1MPM_POINT,
                                      QA_Q1MPM_SOME(entity->actor.id), false, false};
    if (game->host->trace(game->host->ctx, &request).fraction != 1 || !qa_q1mpm_monster_visible(monster)) {
      qa_q1mpm_number(monster, "gorging", 0);
      return;
    }
    double range = qa_q1mpm_monster_distance(monster);
    if (range < 130) {
      qa_q1mpm_monster_face(monster);
      if (range < 45) {
        qa_q1mpm_monster_melee_attack(monster);
        entity->attack_state = "straight";
        return;
      }
      if (!game->host->walk_move(game->host->ctx, entity->actor, game->body(game, entity).angles.y, distance))
        qa_q1mpm_number(monster, "gorging", 0);
      return;
    }
    if (monster->state.enemy.has)
      game->host->move_to_goal(game->host->ctx, entity->actor, monster->state.enemy.id, distance, NULL);
    return;
  }
  if (game->host->random(game->host->ctx) > 0.97 && qa_q1mpm_gremlin_find_target(monster)) return;
  if (qa_q1mpm_actor_number(entity, "stoleweapon") != 0) {
    if (monster->state.enemy.has && game->health(game, monster->state.enemy.id) < 0 &&
        qa_q1mpm_str_eq(game->host->classname(game->host->ctx, monster->state.enemy.id), "player")) {
      qa_q1mpm_monster_play(monster, "gremlin_glook1");
      return;
    }
    qa_q1mpm_actor *goal = game->entity(game, qa_q1mpm_references_get(entity, "trigger_field"));
    if (!qa_q1mpm_gremlin_has_ammo(monster)) {
      if (qa_q1mpm_actor_number(entity, "t_length") == 1) {
        if (goal != NULL) game->remove(game, goal);
        qa_q1mpm_references_set(entity, "goalentity", monster->state.enemy);
        qa_q1mpm_number(monster, "t_length", 0);
      }
      return;
    }
    double range = qa_q1mpm_monster_distance(monster);
    qa_vec3 direction = qa_q1mpm_normalize(qa_q1mpm_vsub(qa_q1mpm_monster_origin(monster), target));
    if (qa_q1mpm_actor_number(entity, "t_length") == 0 && range < 150) {
      goal = game->create(game, "gremlin_goal");
      qa_bounds bounds = {{-1, -1, -1}, {1, 1, 1}};
      game->set_bounds(game, goal, bounds);
      qa_q1mpm_references_set(entity, "trigger_field", QA_Q1MPM_SOME(goal->actor.id));
      qa_q1mpm_number(monster, "t_length", 1);
    }
    if (qa_q1mpm_actor_number(entity, "t_length") == 1 && goal != NULL) {
      if (range > 250) {
        game->remove(game, goal);
        qa_q1mpm_references_set(entity, "goalentity", monster->state.enemy);
        qa_q1mpm_number(monster, "t_length", 0);
      } else {
        if (range < 160) {
          qa_vec3 angles = qa_q1mpm_velocity_angles(direction);
          qa_vec3 end = target;
          for (int count = 0; count < 10; count++) {
            end = qa_q1mpm_vadd(target, qa_q1mpm_vscale(game->make_vectors(game, angles).forward, 350));
            qa_q1mpm_trace_request first = {target, end, QA_Q1MPM_POINT, QA_Q1MPM_SOME(entity->actor.id), true, false};
            qa_q1mpm_trace_request second = {qa_q1mpm_monster_origin(monster), end, QA_Q1MPM_POINT,
                                             QA_Q1MPM_SOME(entity->actor.id), true, false};
            bool clear = game->host->trace(game->host->ctx, &first).fraction == 1 &&
                         game->host->trace(game->host->ctx, &second).fraction == 1;
            angles.y = fmod(angles.y + 36, 360);
            if (clear) break;
          }
          game->set_origin(game, goal, end);
        }
        qa_q1mpm_references_set(entity, "goalentity", QA_Q1MPM_SOME(goal->actor.id));
        entity->ideal_yaw = qa_q1mpm_yaw_for(qa_q1mpm_normalize(
            qa_q1mpm_vsub(game->body(game, goal).origin, qa_q1mpm_monster_origin(monster))));
        qa_q1mpm_monster_change_yaw(monster);
        game->host->move_to_goal(game->host->ctx, entity->actor, goal->actor.id, distance, NULL);
        qa_q1mpm_monster_delay(monster, 0.1);
        return;
      }
    }
  }
  qa_q1mpm_monster_ai(monster, QA_Q1MPM_AI_RUN, distance);
  qa_q1mpm_monster_delay(monster, 0.1);
}
