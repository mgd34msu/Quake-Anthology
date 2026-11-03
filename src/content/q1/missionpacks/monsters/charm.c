/* Port of src/content/q1/missionpacks/monsters/charm.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/charm.h"

#include <math.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "monsters_internal.h"

bool qa_q1mpm_charmer(qa_q1mpm_mission_monster *monster, qa_actor_id *out) {
  return qa_q1_actor_number(monster->base.entity, "charmed") != 0 &&
         qa_q1_actor_reference_get(monster->base.entity, "charmer", out);
}
static qa_q1_actor *update_goal(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_actor_id owner;
  qa_q1_body body;
  if (!qa_q1mpm_charmer(monster, &owner) || !q1mpm_read_body(game, owner, &body)) return NULL;
  qa_actor_id field;
  qa_q1_actor *goal = qa_q1_get_entity(game, q1mpm_reference(entity, "trigger_field", &field));
  if (qa_q1_actor_number(entity, "huntingcharmer") == 1) {
    goal = qa_q1_create(game, "charmed_goal", NULL, NULL);
    (void)qa_q1_actor_reference_set(entity, "trigger_field", &goal->actor.id);
    qa_q1_set_origin(game, goal, body.origin);
    qa_q1mpm_number(monster, "huntingcharmer", 2);
    (void)qa_q1_actor_reference_set(entity, "goalentity", &goal->actor.id);
  }
  if (goal == NULL) return NULL;
  if (qa_q1_actor_number(entity, "huntingcharmer") == 2) {
    if (q1mpm_trace(game, qa_q1_base_monster_origin(&monster->base), body.origin, QA_Q1_POINT, &entity->actor.id, false)
            .fraction == 1)
      qa_q1_set_origin(game, goal, body.origin);
  } else {
    qa_q1_set_origin(game, goal, qa_q1_vadd(body.origin, qa_q1_vscale(qa_q1_normalize(qa_q1_vsub(
                                                               qa_q1_base_monster_origin(&monster->base), body.origin)), 300)));
  }
  return goal;
}
void qa_q1mpm_hunt_charmer(qa_q1mpm_mission_monster *monster, bool flee) {
  qa_vec3 a;
  qa_vec3 b;
  qa_q1mpm_number(monster, "huntingcharmer", 1);
  qa_q1_actor *goal = update_goal(monster);
  if (flee) qa_q1mpm_number(monster, "huntingcharmer", 3);
  else if (goal != NULL)
    monster->base.entity->ideal_yaw = qa_q1_yaw_for((a = qa_q1_get_body(monster->base.game, goal).origin, b = qa_q1_base_monster_origin(&monster->base), qa_q1_vsub(a,
                                                               b)));
  q1mpm_set_next_frame(&monster->base, monster->base.spec->walk);
  qa_q1_base_monster_delay(&monster->base, 0.1);
}
static void stop_hunting(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_actor_id field;
  qa_q1_actor *goal = qa_q1_get_entity(game, q1mpm_reference(entity, "trigger_field", &field));
  if (qa_q1_actor_number(entity, "huntingcharmer") > 1 && goal != NULL) qa_q1_remove(game, goal);
  (void)qa_q1_actor_reference_set(entity, "goalentity", NULL);
  qa_q1mpm_number(monster, "huntingcharmer", 0);
  q1mpm_set_next_frame(&monster->base, monster->base.spec->stand);
  qa_q1_base_monster_delay(&monster->base, 0.1);
}
bool qa_q1mpm_find_charmed_target(qa_q1mpm_mission_monster *monster, bool *out_null) {
  *out_null = false;
  qa_actor_id owner;
  if (!qa_q1mpm_charmer(monster, &owner)) { *out_null = true; return false; }
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_body owner_body;
  if (!q1mpm_read_body(game, owner, &owner_body)) return false;
  entity->effects = qa_js_bit_or(entity->effects, 8);
  if (qa_q1_actor_number(entity, "huntingcharmer") > 0) {
    qa_vec3 a;
    qa_vec3 b;
    qa_q1_actor *goal = update_goal(monster);
    const double distance = goal == NULL ? INFINITY
        : qa_q1_length((a = qa_q1_base_monster_origin(&monster->base), b = qa_q1_get_body(game, goal).origin, qa_q1_vsub(a, b)));
    if (distance < 150) {
      if (qa_q1_actor_number(entity, "huntingcharmer") == 3 && distance > 120) return false;
      stop_hunting(monster);
      return true;
    }
  } else if (qa_q1_length(qa_q1_vsub(qa_q1_base_monster_origin(&monster->base), owner_body.origin)) > 200) {
    qa_q1mpm_hunt_charmer(monster, false);
    return false;
  } else if (qa_q1_length(qa_q1_vsub(qa_q1_base_monster_origin(&monster->base), owner_body.origin)) < 120) {
    qa_q1mpm_hunt_charmer(monster, true);
    return false;
  }
  bool has_selected = false;
  qa_actor_id selected = {0};
  double distance = 1500;
  size_t count = 0;
  qa_actor_id *actors = qa_q1mpm_radius_actors(game, qa_q1_base_monster_origin(&monster->base), 1500, &count);
  for (size_t i = 0; i < count; i++) {
    const qa_actor_id actor = actors[i];
    qa_q1_actor *candidate = qa_q1_get_entity(game, &actor);
    qa_q1_body body;
    const bool has_body = q1mpm_read_body(game, actor, &body);
    qa_actor_id candidate_charmer;
    if (candidate == NULL || !has_body || qa_js_bit_and(candidate->movement_flags, 128) != 0 ||
        qa_js_bit_and(candidate->movement_flags, 32) == 0 || qa_same_actor(actor, entity->actor.id) ||
        qa_same_actor(actor, owner) ||
        (qa_q1_actor_reference_get(candidate, "charmer", &candidate_charmer) && qa_same_actor(candidate_charmer, owner)) ||
        qa_q1_health(game, actor) <= 0 || !qa_q1_base_monster_visible(&monster->base, &actor))
      continue;
    const double range = qa_q1_base_monster_range_distance(&monster->base, &actor);
    if (range < distance) { has_selected = true; selected = actor; distance = range; }
  }
  free(actors);
  if (!has_selected || q1mpm_same_opt(&selected, qa_q1_base_monster_enemy(&monster->base)) || distance >= 1000 ||
      q1mpm_player_powerup(game, selected, "invisibility") > game->time)
    return false;
  qa_q1_base_monster_found(&monster->base, selected);
  return true;
}
bool qa_q1mpm_find_hipnotic_target(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_actor_id candidate;
  bool has_candidate;
  if (game->sight_entity != NULL && game->sight_time >= game->time - 0.1 &&
      qa_js_bit_and(entity->spawnflags, 3) == 0) {
    /* TS `sightEntity.monster?.enemy === monster.enemy`: undefined never equals. */
    if (game->sight_entity->monster != NULL &&
        q1mpm_same_opt(q1mpm_state_enemy(game->sight_entity), qa_q1_base_monster_enemy(&monster->base)))
      return false;
    candidate = game->sight_entity->actor.id;
    has_candidate = true;
  } else {
    has_candidate = q1mpm_check_client(game, entity->actor, &candidate);
  }
  if (!has_candidate || q1mpm_same_opt(&candidate, qa_q1_base_monster_enemy(&monster->base))) return false;
  qa_q1_actor *target = qa_q1_get_entity(game, &candidate);
  qa_q1_body body;
  if (!q1mpm_read_body(game, candidate, &body) || qa_js_bit_and(target == NULL ? 0 : target->movement_flags, 128) != 0 ||
      q1mpm_player_powerup(game, candidate, "invisibility") > game->time)
    return false;
  const double distance = qa_q1_base_monster_range_distance(&monster->base, &candidate);
  if (distance >= 1000 || !qa_q1_base_monster_visible(&monster->base, &candidate)) return false;
  if (distance >= 120) {
    qa_vec3 a;
    qa_vec3 b;
    const qa_q1_player_state *player = qa_q1_player(game, candidate);
    const double hostile = player != NULL ? player->hostile_until
                           : target != NULL ? qa_q1_actor_number(target, "show_hostile") : 0;
    if ((distance >= 500 || hostile < game->time) &&
        (a = qa_q1_normalize(qa_q1_vsub(body.origin, qa_q1_base_monster_origin(&monster->base))), b = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles).forward, qa_q1_dot(a,
                  b)) <= 0.3)
      return false;
  }
  if ((target == NULL ? 0 : qa_q1_actor_number(target, "charmed")) == 0 && !qa_q1_is_player(game, &candidate)) {
    const qa_actor_id *enemy = q1mpm_state_enemy(target);
    if (enemy == NULL) return false;
    candidate = *enemy;
    if (!qa_q1_is_player(game, &candidate)) return false;
  }
  qa_q1_base_monster_found(&monster->base, candidate);
  return true;
}
bool qa_q1mpm_walk_with_charmer(qa_q1mpm_mission_monster *monster, double distance) {
  qa_actor_id owner;
  if (!qa_q1mpm_charmer(monster, &owner)) return false;
  if (qa_q1_base_monster_find_target(&monster->base)) return true;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_actor_id goal_id;
  qa_q1_actor *goal = qa_q1_get_entity(game, q1mpm_reference(entity, "goalentity", &goal_id));
  if (goal == NULL) {
    qa_q1_actor *found[1];
    const char *path = monster->base.state->path == NULL ? "" : monster->base.state->path;
    if (qa_q1_find(game, path, found, 1) > 0) goal = found[0];
  }
  if (goal != NULL) game->host.move_to_goal(game->host.ctx, entity->actor, goal->actor.id, distance, NULL);
  if (qa_q1_actor_number(entity, "huntingcharmer") != 0)
    qa_q1_base_monster_delay(&monster->base, (entity->next_think - game->time) / 2);
  return true;
}
