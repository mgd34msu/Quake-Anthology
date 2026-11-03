/* Port of src/content/q1/addons/monsters/ai/controller.ts (Mg3Monster).
 * quakec_mg3/ai.qc: source targeting, liquid damage and Horde movement policy.
 * Copyright (C) 1996-2026 id Software LLC. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ai/controller.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/ai/path.h"
#include "../support.h"

#define FR QA_Q1AM_FR
#define FLAGS QA_Q1AM_FLAGS

static qa_q1am_mg3_monster *mg3_of(qa_q1_base_monster *monster) { return (qa_q1am_mg3_monster *)monster; }
static bool classname_is(const qa_q1_actor *entity, const char *classname) {
  return entity->classname != NULL && strcmp(entity->classname, classname) == 0;
}

const qa_q1_base_monster_ops QA_Q1AM_MG3_BASE_OPS = {QA_Q1AM_MG3_MONSTER_OPS};

bool qa_q1am_mg3_monster_init(qa_q1am_mg3_monster *monster, qa_q1_services *game, qa_q1_actor *entity,
                              const qa_q1_base_monster_species *spec, qa_q1_monster_services services,
                              const qa_q1_base_monster_source *source, const qa_q1_base_monster_ops *ops,
                              const qa_q1am_mg3_ops *mg3_ops) {
  if (!qa_q1_base_monster_init(&monster->base, game, entity, spec, services, source)) return false;
  monster->base.ops = ops != NULL ? ops : &QA_Q1AM_MG3_BASE_OPS;
  monster->mg3 = mg3_ops;
  return true;
}

qa_q1_basis qa_q1am_mg3_make_vectors(qa_q1_base_monster *monster) {
  return qa_q1_make_vectors(monster->game, qa_q1_get_body(monster->game, monster->entity).angles);
}
void qa_q1am_mg3_attack_finished(qa_q1_base_monster *monster, double seconds) {
  monster->state->refired = false;
  monster->state->attack_finished = FR(monster->game->time + seconds);
}
void qa_q1am_mg3_sight_sound(qa_q1_base_monster *monster) {
  qa_q1am_mg3_monster *mg3 = mg3_of(monster);
  if (mg3->mg3 != NULL && mg3->mg3->sight_sound != NULL) mg3->mg3->sight_sound(mg3);
  else qa_q1am_mg3_sight_sound_super(monster);
}
void qa_q1am_mg3_sight_sound_super(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const char *sound = monster->spec->sight;
  char computed[32];
  if (classname_is(entity, "monster_ogre") && qa_q1_actor_number(entity, "aflag") != 0) {
    sound = "armagon/sight.wav";
  } else if (classname_is(entity, "monster_hell_knight") && strcmp(entity->solid, "none") == 0 &&
             (FLAGS(entity->spawnflags) & (65536 | 8388608)) != 0) {
    return;
  } else if (classname_is(entity, "monster_enforcer")) {
    const double choice = floor(qa_q1am_random(game) * 3 + 0.5);
    (void)snprintf(computed, sizeof computed, "enforcer/sight%d.wav",
                   choice == 1 ? 1 : choice == 2 ? 2 : choice == 0 ? 3 : 4);
    sound = computed;
  }
  if (sound != NULL && sound[0] != '\0') qa_q1_sound_defaults(game, entity->actor.id, sound);
}
void qa_q1am_mg3_found(qa_q1_base_monster *monster, qa_actor_id target) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_base_monster_set_enemy(monster, &target);
  if (qa_q1_is_player(game, &target)) { game->sight_entity = entity; game->sight_time = game->time; }
  if (classname_is(entity, "monster_hell_knight") && (FLAGS(entity->spawnflags) & (65536 | 8388608)) != 0 &&
      monster->state->pain_finished > game->time)
    return;
  qa_q1am_field_fround(entity, "show_hostile", game->time + 1);
  qa_q1am_mg3_sight_sound(monster);
  qa_q1_base_monster_hunt_target(monster);
}
bool qa_q1am_mg3_check_attack(qa_q1_base_monster *monster) {
  qa_q1am_mg3_monster *mg3 = mg3_of(monster);
  if (mg3->mg3 != NULL && mg3->mg3->check_attack != NULL) return mg3->mg3->check_attack(mg3);
  return qa_q1am_mg3_check_attack_super(monster);
}
bool qa_q1am_mg3_check_attack_super(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_base_monster_species *spec = monster->spec;
  const bool has_enemy = monster->state->has_enemy;
  const qa_actor_id enemy = monster->state->enemy;
  qa_vec3 start, end;
  const bool has_start = qa_q1_base_monster_eye(monster, entity->actor.id, &start);
  const bool has_end = has_enemy && qa_q1_base_monster_eye(monster, enemy, &end);
  if (!has_enemy || !has_start || !has_end) return false;
  const qa_q1_trace trace = qa_q1am_trace(game, start, end, QA_Q1_POINT, &entity->actor.id, true);
  if (!trace.has_actor || !qa_same_actor(trace.actor, enemy) || (trace.in_open && trace.in_water)) return false;
  const double range = qa_q1am_mg3_word(monster, "enemy_range");
  if (range == 0 && spec->melee) {
    qa_q1_base_monster_melee_attack(monster);
    return true;
  }
  if (spec->missile == NULL || game->time < monster->state->attack_finished || range == 3) return false;
  if (range == 0) monster->state->attack_finished = 0;
  const double chance = range == 0 ? 0.9 : range == 1 ? (spec->melee ? 0.2 : 0.4)
                      : range == 2 ? (spec->melee ? 0.05 : 0.1) : 0;
  if (qa_q1am_random(game) >= chance) return false;
  qa_q1_base_monster_play(monster, spec->missile);
  qa_q1_base_monster_attack_finished(monster, 2 * qa_q1am_random(game));
  return true;
}
/* QC keeps these AI globals on world; a foreign world has no Q1 world actor,
 * so they live in the game's saved sourceWorldWords. */
void qa_q1am_mg3_source_word(qa_q1_base_monster *monster, const char *name, double value) {
  qa_q1_actor *world = monster->game->world;
  if (world != NULL) qa_q1am_field_fround(world, name, value);
  else qa_q1_source_world_word_set(monster->game, name, (double)(float)value);
}
double qa_q1am_mg3_word(qa_q1_base_monster *monster, const char *name) {
  qa_q1_actor *world = monster->game->world;
  if (world != NULL) return qa_q1_actor_number(world, name);
  double value = 0;
  return qa_q1_source_world_word_get(monster->game, name, &value) ? value : 0;
}
double qa_q1am_mg3_range_category(qa_q1_base_monster *monster, const qa_actor_id *target) {
  /* TS default parameter target = this.enemy. */
  qa_actor_id enemy;
  if (target == NULL && monster->state->has_enemy) { enemy = monster->state->enemy; target = &enemy; }
  const double distance = qa_q1_base_monster_range_distance(monster, target);
  const bool near_sighted = (FLAGS(monster->entity->spawnflags) & 8192) != 0;
  double melee, near, mid;
  /* TS branches on this.enemy, not on the target parameter. */
  if (near_sighted && !monster->state->has_enemy) { melee = 120; near = 300; mid = 340; }
  else if (near_sighted) { melee = 96; near = 400; mid = 800; }
  else { melee = 120; near = 500; mid = 1000; }
  return distance < melee ? 0 : distance < near ? 1 : distance < mid ? 2 : 3;
}
bool qa_q1am_mg3_in_front(qa_q1_base_monster *monster, qa_actor_id target) {
  qa_q1_services *game = monster->game;
  qa_q1_body body;
  if (!qa_q1am_read_body(game, target, &body)) return false;
  /* MG3 infront passes self.angles directly, unlike the id1 rerelease pitch adjustment. */
  const qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, monster->entity).angles);
  const double facing = qa_dot3(qa_q1_normalize(qa_sub3(body.origin, qa_q1_base_monster_origin(monster))), basis.forward);
  const double threshold = (FLAGS(monster->entity->spawnflags) & 8192) != 0 ? FR(0.866) : FR(0.3);
  return facing > threshold || (classname_is(monster->entity, "monster_orb") && -facing > threshold);
}
bool qa_q1am_mg3_find_target(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if ((FLAGS(entity->spawnflags) & 32) != 0) {
    size_t count = 0, damageable = 0;
    qa_q1_actor **found = qa_q1am_find(game, entity->target, &count);
    for (size_t i = 0; i < count; i++)
      if (qa_q1_actor_damageable(found[i])) found[damageable++] = found[i];
    if (damageable > 0) {
      const double index = floor((double)damageable * qa_q1am_random(game));
      if (!(index < (double)damageable)) {
        free(found);
        qa_range_error("Source random target index exceeds candidates");
      }
      const qa_actor_id id = found[(size_t)index]->actor.id;
      free(found);
      qa_q1_base_monster_set_enemy(monster, &id);
      qa_q1_base_monster_found(monster, id);
      return true;
    }
    free(found);
  }
  bool has_candidate;
  qa_actor_id candidate = {0};
  if (game->sight_time >= game->time - 0.1 && (FLAGS(entity->spawnflags) & 3) == 0) {
    qa_q1_actor *sight = game->sight_entity;
    const qa_actor_id *sight_enemy =
        sight != NULL && sight->monster != NULL && sight->monster->has_enemy ? &sight->monster->enemy : NULL;
    if (qa_q1am_same(sight_enemy, qa_q1_base_monster_enemy(monster))) return false;
    has_candidate = sight != NULL;
    if (sight != NULL) candidate = sight->actor.id;
  } else {
    has_candidate = game->host.check_client(game->host.ctx, entity->actor, &candidate);
  }
  if (!has_candidate || qa_q1am_same(&candidate, qa_q1_base_monster_enemy(monster))) return false;
  qa_q1_actor *actor = qa_q1_get_entity(game, &candidate);
  qa_q1_player_state *player = qa_q1_player(game, candidate);
  if (((actor != NULL ? FLAGS(actor->movement_flags) : 0) & 128) != 0 ||
      (player != NULL ? qa_q1_player_powerup_get(player, "invisibility") : 0) > game->time)
    return false;
  const double range = qa_q1am_mg3_range_category(monster, &candidate);
  if (range == 3 || !qa_q1_base_monster_visible(monster, &candidate)) return false;
  const double hostile = player != NULL ? player->hostile_until
                       : actor != NULL ? qa_q1_actor_number(actor, "show_hostile") : 0;
  if ((range == 1 && hostile < game->time && !qa_q1am_mg3_in_front(monster, candidate)) ||
      (range == 2 && !qa_q1am_mg3_in_front(monster, candidate)))
    return false;
  qa_q1_base_monster_set_enemy(monster, &candidate);
  if (!qa_q1_is_player(game, &candidate)) {
    const qa_actor_id *other = actor != NULL && actor->monster != NULL && actor->monster->has_enemy
                                   ? &actor->monster->enemy : NULL;
    qa_q1_base_monster_set_enemy(monster, other);
    if (other == NULL || !qa_q1_is_player(game, other)) {
      qa_q1_base_monster_set_enemy(monster, NULL);
      return false;
    }
  }
  const qa_actor_id enemy = monster->state->enemy;
  qa_q1_base_monster_found(monster, enemy);
  return true;
}
bool qa_q1am_mg3_check_contents_damage(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (entity->water_level == 0 || qa_q1_health(game, entity->actor.id) <= 0 ||
      game->time < qa_q1_actor_number(entity, "dmgtime") || (FLAGS(entity->spawnflags) & 16384) != 0)
    return false;
  if (entity->water_type != -4 && entity->water_type != -5) return false;
  /* A foreign world has no Q1 world actor; the monster is then the source of its own contents damage. */
  const qa_actor_id source = game->world != NULL ? game->world->actor.id : entity->actor.id;
  const bool lava = entity->water_type == -5;
  qa_q1am_field_fround(entity, "dmgtime", game->time + (lava ? 0.2 : 1));
  (void)qa_q1_damage(game, entity->actor.id, source, &source,
                     lava ? (classname_is(entity, "monster_zombie") ? 120 : 30 * entity->water_level)
                          : 4 * entity->water_level,
                     NULL, NULL, NULL, NULL);
  return (lava && classname_is(entity, "monster_zombie")) || qa_q1_health(game, entity->actor.id) <= 0;
}
void qa_q1am_mg3_ai(qa_q1_base_monster *monster, qa_q1_monster_ai mode, double distance) {
  qa_q1_services *game = monster->game;
  switch (mode) {
    case QA_Q1_AI_STAND:
      if (qa_q1am_mg3_check_contents_damage(monster) || qa_q1_base_monster_find_target(monster)) return;
      if (game->time > monster->state->pause_until) qa_q1_base_monster_play(monster, monster->spec->walk);
      return;
    case QA_Q1_AI_WALK: {
      if (qa_q1am_mg3_check_contents_damage(monster)) return;
      qa_q1am_mg3_source_word(monster, "movedist", distance);
      if (qa_q1_base_monster_find_target(monster)) return;
      qa_q1_actor *goal = qa_q1am_find_first(game, monster->state->path != NULL ? monster->state->path : "");
      if (goal == NULL) goal = game->world;
      if (goal != NULL)
        game->host.move_to_goal(game->host.ctx, monster->entity->actor, goal->actor.id, distance, NULL);
      return;
    }
    case QA_Q1_AI_TURN:
      if (!qa_q1_base_monster_find_target(monster)) qa_q1_base_monster_change_yaw(monster);
      return;
    case QA_Q1_AI_PAINFORWARD:
      (void)qa_q1am_walk_move(game, monster->entity->actor, monster->entity->ideal_yaw, distance);
      return;
    default:
      qa_q1_base_monster_ai_super(monster, mode, distance);
      return;
  }
}
void qa_q1am_mg3_hunt_target(qa_q1_base_monster *monster) {
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target)) return;
  monster->state->mode = "run";
  monster->entity->ideal_yaw = qa_q1_yaw_for(qa_sub3(target, qa_q1_base_monster_origin(monster)));
  qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, monster->spec->run);
  qa_q1_base_monster_delay(monster, 0.1);
  qa_q1_base_monster_attack_finished(monster, 1);
}
void qa_q1am_mg3_update_run_knowledge(qa_q1_base_monster *monster) {
  qa_q1am_mg3_source_word(monster, "enemy_range", qa_q1am_mg3_range_category(monster, NULL));
  qa_vec3 target;
  if (qa_q1_base_monster_target(monster, &target))
    qa_q1am_mg3_source_word(monster, "enemy_yaw", qa_q1_yaw_for(qa_sub3(target, qa_q1_base_monster_origin(monster))));
}
void qa_q1am_mg3_run(qa_q1_base_monster *monster, double distance) {
  if (qa_q1am_mg3_check_contents_damage(monster)) return;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_monster *state = monster->state;
  qa_q1am_mg3_source_word(monster, "movedist", distance);
  if (!state->has_enemy || qa_q1_health(game, state->enemy) <= 0) {
    qa_q1_base_monster_set_enemy(monster, NULL);
    if (state->has_old_enemy && qa_q1_health(game, state->old_enemy) > 0) {
      const qa_actor_id old = state->old_enemy;
      qa_q1_base_monster_set_enemy(monster, &old);
      qa_q1_base_monster_hunt_target(monster);
    } else {
      qa_q1_base_monster_play(monster, state->path == NULL || state->path[0] == '\0' ? monster->spec->stand
                                                                                      : monster->spec->walk);
      return;
    }
  }
  qa_q1am_field_fround(entity, "show_hostile", game->time + 1);
  qa_actor_id enemy;
  const qa_actor_id *current = NULL;
  if (state->has_enemy) { enemy = state->enemy; current = &enemy; }
  const bool seen = qa_q1_base_monster_visible(monster, current);
  qa_q1am_mg3_source_word(monster, "enemy_visible", seen ? 1 : 0);
  if (seen) state->search_until = FR(game->time + 5);
  if (qa_q1_base_monster_search_for_coop_target(monster)) return;
  qa_q1_base_monster_update_run_knowledge(monster);
  if (strcmp(entity->attack_state, "missile") == 0 || strcmp(entity->attack_state, "melee") == 0) {
    entity->ideal_yaw = qa_q1am_mg3_word(monster, "enemy_yaw");
    qa_q1_base_monster_change_yaw(monster);
    const double delta = fmod(fmod(qa_q1_get_body(game, entity).angles.y - entity->ideal_yaw, 360) + 360, 360);
    if (delta <= 45 || delta >= 315) {
      if (strcmp(entity->attack_state, "melee") == 0) qa_q1_base_monster_melee_attack(monster);
      else if (monster->spec->missile != NULL) qa_q1_base_monster_play(monster, monster->spec->missile);
      entity->attack_state = "straight";
    }
    return;
  }
  if (seen && qa_q1_base_monster_try_attack(monster)) return;
  if (monster->sliding) {
    entity->ideal_yaw = qa_q1am_mg3_word(monster, "enemy_yaw");
    qa_q1_base_monster_change_yaw(monster);
    const double offset = monster->lefty ? 90 : -90;
    if (qa_q1am_walk_move(game, entity->actor, entity->ideal_yaw + offset, distance)) return;
    monster->lefty = !monster->lefty;
    if (classname_is(entity, "monster_orb")) {
      monster->sliding = false;
      entity->attack_state = "straight";
      qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, monster->spec->run);
      return;
    }
    (void)qa_q1am_walk_move(game, entity->actor, entity->ideal_yaw - offset, distance);
    return;
  }
  if (qa_q1am_mg3_word(monster, "isHordeMode") != 0) qa_q1am_mg3_path_to_goal(monster, distance);
  else qa_q1_base_monster_move_to_enemy(monster, distance);
}
void qa_q1am_mg3_path_to_goal(qa_q1_base_monster *monster, double distance) {
  qa_q1_actor *entity = monster->entity;
  qa_vec3 target;
  const bool has_target = qa_q1_base_monster_target(monster, &target);
  if (qa_q1_actor_number(entity, "allowPathFind") == 0 || !has_target) {
    qa_q1_base_monster_move_to_enemy(monster, distance);
    return;
  }
  const bool seen = qa_q1am_mg3_word(monster, "enemy_visible") != 0;
  const double range = qa_q1am_mg3_word(monster, "enemy_range"), style = qa_q1_actor_number(entity, "combat_style");
  if (!seen || (style == 2 && range > 1) || (style == 3 && range > 2)) {
    if (qa_q1am_walk_mg3_path_to_goal(monster, distance, target) == QA_Q1AM_PATH_IN_PROGRESS) return;
  }
  qa_q1_base_monster_move_to_enemy(monster, distance);
}
