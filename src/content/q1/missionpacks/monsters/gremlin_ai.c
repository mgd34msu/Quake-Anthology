/* Port of src/content/q1/missionpacks/monsters/gremlin-ai.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/gremlin_ai.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/gremlin_weapons.h"
#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "monsters_internal.h"

bool qa_q1mpm_gremlin_find_victim(qa_q1mpm_mission_monster *monster, qa_actor_id *out) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  monster->base.state->search_until = game->time + 1;
  bool has_selected = false;
  qa_actor_id selected = {0};
  double distance = 1000;
  size_t count = 0;
  qa_actor_id *actors = qa_q1mpm_radius_actors(game, qa_q1_base_monster_origin(&monster->base), 1000, &count);
  for (size_t i = 0; i < count; i++) {
    const qa_actor_id actor = actors[i];
    qa_q1_actor *other = qa_q1_get_entity(game, &actor);
    const double flags = qa_js_bit_or(other == NULL ? 0 : other->movement_flags, qa_q1_is_player(game, &actor) ? 8 : 0);
    qa_q1_body body;
    const bool has_body = q1mpm_read_body(game, actor, &body);
    if (qa_js_bit_and(flags, 128) != 0 || qa_js_bit_and(flags, 32 | 8) == 0 ||
        !qa_q1_base_monster_visible(&monster->base, &actor) || qa_q1_health(game, actor) <= 0 ||
        qa_same_actor(actor, entity->actor.id) || !has_body)
      continue;
    double range = qa_q1_length(qa_q1_vsub(body.origin, qa_q1_base_monster_origin(&monster->base)));
    qa_actor_id last;
    if (qa_q1_actor_reference_get(entity, "lastvictim", &last) && qa_same_actor(actor, last)) range *= 2;
    if (qa_js_bit_and(flags, 8) != 0) range /= 1.5;
    if (other != NULL && strcmp(other->classname, entity->classname) == 0) range *= 1.5;
    if (range < distance) { distance = range; has_selected = true; selected = actor; }
  }
  free(actors);
  (void)qa_q1_actor_reference_set(entity, "lastvictim", has_selected ? &selected : NULL);
  if (has_selected) *out = selected;
  return has_selected;
}
bool qa_q1mpm_gremlin_find_target(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (qa_q1_actor_number(entity, "stoleweapon") == 0 && game->time > entity->wait) {
    entity->wait = game->time + 1;
    double distance = 2000;
    bool has_gorge = false;
    qa_actor_id gorge = {0};
    size_t count = 0;
    const qa_actor_observation *scratch = qa_session_actor_registry_observations(game->host.actors, &count);
    qa_actor_id *observed = malloc((count == 0 ? 1 : count) * sizeof *observed);
    if (observed == NULL) qa_range_error("out of memory");
    for (size_t i = 0; i < count; i++) observed[i] = scratch[i].id; /* the registry list is scratch */
    for (size_t i = 0; i < count; i++) {
      const qa_actor_id id = observed[i];
      qa_q1_actor *other = qa_q1_get_entity(game, &id);
      qa_q1_body body;
      const bool has_body = q1mpm_read_body(game, id, &body);
      const double flags = qa_js_bit_or(other == NULL ? 0 : other->movement_flags, qa_q1_is_player(game, &id) ? 8 : 0);
      if (!has_body || qa_q1_health(game, id) >= 1 || qa_js_bit_and(flags, 32 | 8) == 0) continue;
      double vertical = fabs(body.origin.z - qa_q1_base_monster_origin(&monster->base).z);
      bool visible = qa_q1_base_monster_visible(&monster->base, &id);
      qa_vec3 start, end;
      bool has_start = qa_q1_base_monster_eye(&monster->base, monster->base.entity->actor.id, &start);
      bool has_end = qa_q1_base_monster_eye(&monster->base, id, &end);
      double range = !has_start || !has_end ? INFINITY : qa_q1_length(qa_q1_vsub(end, start));
      if (visible && vertical < 80 && (other == NULL ? 0 : qa_q1_actor_number(other, "gorging")) == 0 &&
          range < distance) {
        distance = range;
        has_gorge = true;
        gorge = id;
      }
    }
    free(observed);
    if (has_gorge && distance < 700 * qa_q1_host_random(game)) {
      monster->base.state->has_old_enemy = monster->base.state->has_enemy;
      monster->base.state->old_enemy = monster->base.state->enemy;
      qa_q1mpm_number(monster, "gorging", 1);
      qa_q1_base_monster_set_enemy(&monster->base, &gorge);
      monster->base.state->search_until = game->time + 4;
      qa_q1_base_monster_found(&monster->base, gorge);
      return true;
    }
  } else if (qa_q1_actor_number(entity, "stoleweapon") != 0) {
    qa_actor_id victim;
    if (qa_q1mpm_gremlin_find_victim(monster, &victim)) {
      qa_q1_base_monster_found(&monster->base, victim);
      monster->base.state->attack_finished = game->time;
      monster->base.state->search_until = game->time + 2;
      return true;
    }
  }
  bool found = qa_q1_base_monster_find_target(&monster->base);
  monster->base.state->search_until = game->time + 2;
  return found;
}
void qa_q1mpm_gremlin_walk(qa_q1mpm_mission_monster *monster, double distance) {
  if (qa_q1mpm_gremlin_find_target(monster)) return;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_actor_id goal_id;
  qa_q1_actor *goal = qa_q1_get_entity(game, q1mpm_reference(entity, "goalentity", &goal_id));
  if (goal == NULL) {
    qa_q1_actor *found[1];
    if (qa_q1_find(game, monster->base.state->path, found, 1) > 0) goal = found[0];
  }
  if (goal != NULL) game->host.move_to_goal(game->host.ctx, entity->actor, goal->actor.id, distance, NULL);
}
void qa_q1mpm_gremlin_stand(qa_q1mpm_mission_monster *monster) {
  if (qa_q1_base_monster_find_target(&monster->base)) return;
  if (monster->base.game->time > monster->base.state->pause_until) qa_q1_base_monster_play(&monster->base, "gremlin_walk1");
}
void qa_q1mpm_gremlin_run(qa_q1mpm_mission_monster *monster, double distance) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (entity->water_type == -5) {
    const qa_actor_id inflictor = game->world == NULL ? entity->actor.id : game->world->actor.id;
    (void)qa_q1_damage(game, entity->actor.id, inflictor, game->world == NULL ? NULL : &game->world->actor.id, 2000,
                       NULL, NULL, NULL, NULL);
  }
  if (qa_q1_actor_number(entity, "stoleweapon") != 0) entity->frame += 164 - 29;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(&monster->base, &target)) {
    qa_q1_base_monster_ai(&monster->base, QA_Q1_AI_RUN, distance);
    return;
  }
  if (qa_q1_actor_number(entity, "gorging") != 0) {
    if (q1mpm_trace(game, qa_q1_base_monster_origin(&monster->base), target, QA_Q1_POINT, &entity->actor.id, false)
                .fraction != 1 ||
        !qa_q1_base_monster_visible(&monster->base, qa_q1_base_monster_enemy(&monster->base))) {
      qa_q1mpm_number(monster, "gorging", 0);
      return;
    }
    double range = qa_q1_base_monster_distance(&monster->base);
    if (range < 130) {
      qa_q1_base_monster_face(&monster->base);
      if (range < 45) {
        qa_q1_base_monster_melee_attack(&monster->base);
        entity->attack_state = "straight";
        return;
      }
      if (!game->host.walk_move(game->host.ctx, entity->actor, qa_q1_get_body(game, entity).angles.y, distance))
        qa_q1mpm_number(monster, "gorging", 0);
      return;
    }
    if (monster->base.state->has_enemy)
      game->host.move_to_goal(game->host.ctx, entity->actor, monster->base.state->enemy, distance, NULL);
    return;
  }
  if (qa_q1_host_random(game) > 0.97 && qa_q1mpm_gremlin_find_target(monster)) return;
  if (qa_q1_actor_number(entity, "stoleweapon") != 0) {
    if (monster->base.state->has_enemy && qa_q1_health(game, monster->base.state->enemy) < 0 &&
        q1mpm_str_eq(game->host.classname(game->host.ctx, monster->base.state->enemy), "player")) {
      qa_q1_base_monster_play(&monster->base, "gremlin_glook1");
      return;
    }
    qa_actor_id field;
    qa_q1_actor *goal = qa_q1_get_entity(game, q1mpm_reference(entity, "trigger_field", &field));
    if (!qa_q1mpm_gremlin_has_ammo(monster)) {
      if (qa_q1_actor_number(entity, "t_length") == 1) {
        if (goal != NULL) qa_q1_remove(game, goal);
        (void)qa_q1_actor_reference_set(entity, "goalentity", qa_q1_base_monster_enemy(&monster->base));
        qa_q1mpm_number(monster, "t_length", 0);
      }
      return;
    }
    double range = qa_q1_base_monster_distance(&monster->base);
    qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(qa_q1_base_monster_origin(&monster->base), target));
    if (qa_q1_actor_number(entity, "t_length") == 0 && range < 150) {
      goal = qa_q1_create(game, "gremlin_goal", NULL, NULL);
      qa_bounds bounds = {{-1, -1, -1}, {1, 1, 1}};
      qa_q1_set_bounds(game, goal, bounds);
      (void)qa_q1_actor_reference_set(entity, "trigger_field", &goal->actor.id);
      qa_q1mpm_number(monster, "t_length", 1);
    }
    if (qa_q1_actor_number(entity, "t_length") == 1 && goal != NULL) {
      if (range > 250) {
        qa_q1_remove(game, goal);
        (void)qa_q1_actor_reference_set(entity, "goalentity", qa_q1_base_monster_enemy(&monster->base));
        qa_q1mpm_number(monster, "t_length", 0);
      } else {
        if (range < 160) {
          qa_vec3 angles = qa_q1mp_velocity_angles(direction);
          qa_vec3 end = target;
          for (int count = 0; count < 10; count++) {
            end = qa_q1_vadd(target, qa_q1_vscale(qa_q1_make_vectors(game, angles).forward, 350));
            const bool clear =
                q1mpm_trace(game, target, end, QA_Q1_POINT, &entity->actor.id, true).fraction == 1 &&
                q1mpm_trace(game, qa_q1_base_monster_origin(&monster->base), end, QA_Q1_POINT, &entity->actor.id, true)
                        .fraction == 1;
            angles.y = fmod(angles.y + 36, 360);
            if (clear) break;
          }
          qa_q1_set_origin(game, goal, end);
        }
        (void)qa_q1_actor_reference_set(entity, "goalentity", &goal->actor.id);
        const qa_vec3 goal_origin = qa_q1_get_body(game, goal).origin;
        entity->ideal_yaw = qa_q1_yaw_for(qa_q1_normalize(qa_q1_vsub(goal_origin, qa_q1_base_monster_origin(&monster->base))));
        qa_q1_base_monster_change_yaw(&monster->base);
        game->host.move_to_goal(game->host.ctx, entity->actor, goal->actor.id, distance, NULL);
        qa_q1_base_monster_delay(&monster->base, 0.1);
        return;
      }
    }
  }
  qa_q1_base_monster_ai(&monster->base, QA_Q1_AI_RUN, distance);
  qa_q1_base_monster_delay(&monster->base, 0.1);
}
