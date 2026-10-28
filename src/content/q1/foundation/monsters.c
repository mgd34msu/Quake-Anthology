/*
 * Port of src/content/q1/foundation/monsters.ts. GPL-2.0-or-later.
 * Host body/combat reads use the actor-local mirrors of types.h;
 * throwGib/throwHead (TS content/q1/base/projectiles.ts) are inlined
 * here against qa_q1_services because the landed base projectiles
 * port binds the separate qa_q1b_game mirror.
 */
#include "qa/content/q1/foundation/monsters.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/monsters/target.h"
#include "qa/content/q1/foundation/weapons.h"
#include "qa/foundation/support.h"

static double fr(double value) { return (float)value; }
static qa_vec3 v3(double x, double y, double z) {
  const qa_vec3 result = {x, y, z};
  return result;
}
static void set_string(char **slot, const char *value) {
  if (value == NULL) value = "";
  if (*slot != NULL && strcmp(*slot, value) == 0) return;
  const size_t len = strlen(value) + 1;
  char *copy = malloc(len);
  if (!copy) return;
  memcpy(copy, value, len);
  free(*slot);
  *slot = copy;
}
static bool body_of(qa_q1_services *game, qa_actor_id actor, qa_q1_body *out) {
  qa_q1_actor *entity = qa_q1_get_entity(game, &actor);
  if (entity == NULL) return false;
  *out = entity->body;
  return true;
}
static bool can_take_damage(qa_q1_services *game, qa_actor_id actor) {
  qa_q1_actor *entity = qa_q1_get_entity(game, &actor);
  if (entity != NULL) return entity->damageable;
  return qa_q1_is_player(game, &actor);
}
static qa_q1_bound_action named_action(qa_q1_services *game, qa_q1_actor *entity, const char *name) {
  return qa_q1_callback_registry_action(game->named, entity, name);
}
static bool target_eligible(qa_q1_services *game, qa_actor_id actor) {
  const qa_q1_monster_target_result observed = qa_q1_monster_target(game, actor);
  if (!observed.has_value) return qa_monster_target_eligible(qa_q1_health(game, actor), NULL);
  const qa_monster_target_observation converted = {
      observed.value.view_height, observed.value.notarget, observed.value.invisible,
      observed.value.light_level != NULL, observed.value.light_level ? *observed.value.light_level : 0,
      observed.value.hostile_until != NULL, observed.value.hostile_until ? *observed.value.hostile_until : 0};
  return qa_monster_target_eligible(qa_q1_health(game, actor), &converted);
}

static const double ARMY_WALK[24] = {1, 1, 1, 1, 2, 3, 4, 4, 2, 2, 2, 1,
                                     0, 1, 1, 1, 3, 3, 3, 3, 2, 1, 1, 1};
static const double ARMY_RUN[8] = {11, 15, 10, 10, 8, 15, 10, 8};
static const double DOG_RUN[12] = {16, 32, 32, 20, 64, 32, 16, 32, 32, 20, 64, 32};

static void set_sequence(qa_q1_monster *monster, const char *mode, double first_frame,
                         const double *sequence, size_t count) {
  monster->mode = mode;
  monster->first_frame = first_frame;
  if (count > 32) count = 32;
  if (sequence != NULL) memcpy(monster->sequence, sequence, count * sizeof *sequence);
  else memset(monster->sequence, 0, count * sizeof *sequence);
  monster->sequence_count = count;
  monster->frame_index = 0;
}
static bool is_army(const qa_q1_monster *monster) {
  return monster->species != NULL && !strcmp(monster->species, "army");
}
static void stand(qa_q1_monster *monster) {
  set_sequence(monster, "stand", is_army(monster) ? 0 : 69, NULL, is_army(monster) ? 8 : 9);
}
static void walk(qa_q1_monster *monster) {
  if (is_army(monster)) {
    set_sequence(monster, "walk", 90, ARMY_WALK, 24);
  } else {
    static const double DOG_WALK[8] = {8, 8, 8, 8, 8, 8, 8, 8};
    set_sequence(monster, "walk", 78, DOG_WALK, 8);
  }
}
static void run_monster(qa_q1_monster *monster) {
  if (is_army(monster)) set_sequence(monster, "run", 73, ARMY_RUN, 8);
  else set_sequence(monster, "run", 48, DOG_RUN, 12);
}

static qa_q1_monster *require_monster(qa_q1_actor *entity) {
  if (entity->monster == NULL)
    qa_range_error("Missing saved monster state: %s", entity->classname ? entity->classname : "");
  return entity->monster;
}

double qa_q1_path_end_time(double time) { return fr(fr(time) + 999999); }

void qa_q1_set_monster_route(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *goal,
                             double pause_until) {
  qa_q1_monster *monster = require_monster(entity);
  monster->pause_until = pause_until;
  const char *prefix = qa_q1_actor_field_get(entity, "source.monsterCallbackPrefix");
  if (prefix != NULL) {
    char name[256];
    (void)snprintf(name, sizeof name, "%s:monster_route", prefix);
    qa_q1_call_action(game, entity, named_action(game, entity, name));
  } else if (goal == NULL || pause_until > game->time) {
    stand(monster);
  } else if (monster->mode != NULL && !strcmp(monster->mode, "stand")) {
    walk(monster);
  }
  qa_q1_body target;
  if (goal != NULL && body_of(game, *goal, &target))
    entity->ideal_yaw = qa_q1_yaw_for(qa_q1_vsub(target.origin, qa_q1_get_body(game, entity).origin));
}

static double face(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 destination) {
  const qa_q1_body body = qa_q1_get_body(game, entity);
  const double ideal = qa_q1_yaw_for(qa_q1_vsub(destination, body.origin));
  double move = ideal - body.angles.y;
  if (move > 180) move -= 360;
  if (move < -180) move += 360;
  const double speed = qa_q1_actor_number(entity, "yaw_speed") ? qa_q1_actor_number(entity, "yaw_speed") : 20;
  entity->ideal_yaw = ideal;
  const double clamped = fmax(-speed, fmin(speed, move));
  double yaw = fmod(body.angles.y + clamped + 360, 360);
  qa_q1_body patch = body;
  patch.angles.y = yaw;
  qa_q1_set_body_patch(game, entity, &patch, false, true, false, false, false);
  return yaw;
}

static bool visible(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id target) {
  qa_q1_body body;
  if (!body_of(game, target, &body)) return false;
  const qa_q1_monster_target_result observed = qa_q1_monster_target(game, target);
  if (!observed.has_value) return false;
  qa_q1_trace_request request = {
      qa_q1_vadd(qa_q1_get_body(game, entity).origin, v3(0, 0, 25)),
      qa_q1_vadd(body.origin, v3(0, 0, observed.value.view_height)),
      QA_Q1_POINT, true, entity->actor.id, false, false, false};
  const qa_q1_trace trace = qa_q1_host_trace(game, request);
  return trace.fraction == 1 && !(trace.in_open && trace.in_water);
}

static void found_target(qa_q1_services *game, qa_q1_actor *entity, qa_q1_monster *monster, qa_actor_id target) {
  monster->has_enemy = true;
  monster->enemy = target;
  monster->search_until = game->time + 5;
  monster->attack_finished = game->time + 1;
  run_monster(monster);
  const qa_q1_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
  if (mission != NULL && mission->found_target) mission->found_target(mission->ctx);
  game->sight_entity = entity;
  game->sight_time = game->time;
  qa_q1_sound_defaults(game, entity->actor.id, is_army(monster) ? "soldier/sight1.wav" : "dog/dsight.wav");
}

static bool mission_route(qa_q1_services *game, qa_actor_id actor, qa_actor_id *out) {
  const qa_q1_monster_mission *mission = qa_q1_mission(game, actor);
  if (mission == NULL || mission->route == NULL) return false;
  return mission->route(mission->ctx, out);
}

static bool find_target(qa_q1_services *game, qa_q1_actor *entity, qa_q1_monster *monster) {
  bool has_candidate = false;
  qa_actor_id candidate = entity->actor.id;
  if (game->sight_entity != NULL && game->sight_time >= game->time - 0.1 &&
      ((long)entity->spawnflags & 3) == 0) {
    if (game->sight_entity->monster != NULL && game->sight_entity->monster->has_enemy) {
      candidate = game->sight_entity->monster->enemy;
      has_candidate = true;
    }
  } else if (game->host.check_client != NULL) {
    qa_actor_id out = entity->actor.id;
    /* TS checkClient returns ActorId | null; the C mirror's has_value
     * is the non-null signal and the out id is the candidate. */
    const qa_q1_monster_target_result result = game->host.check_client(game->host.ctx, entity->actor, &out);
    candidate = out;
    has_candidate = result.has_value;
  }
  if (!has_candidate || qa_q1_health(game, candidate) <= 0) return false;
  const qa_q1_monster_target_result observed = qa_q1_monster_target(game, candidate);
  if (!observed.has_value || observed.value.invisible || observed.value.notarget) return false;
  qa_q1_body target;
  if (!body_of(game, candidate, &target)) return false;
  const qa_vec3 delta = qa_q1_vsub(target.origin, qa_q1_get_body(game, entity).origin);
  const double distance = qa_q1_length(delta);
  if (distance >= 1000 || !visible(game, entity, candidate)) return false;
  const bool in_front =
      qa_q1_dot(qa_q1_normalize(delta), qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles).forward) > 0.3;
  const bool hostile = observed.value.hostile_until != NULL && *observed.value.hostile_until >= game->time;
  if ((distance >= 500 && !in_front) || (distance >= 120 && distance < 500 && !hostile && !in_front))
    return false;
  found_target(game, entity, monster, candidate);
  return true;
}

static bool try_attack(qa_q1_services *game, qa_q1_actor *entity, qa_q1_monster *monster) {
  if (!monster->has_enemy) return false;
  const qa_actor_id enemy = monster->enemy;
  qa_q1_body target;
  if (!body_of(game, enemy, &target)) return false;
  const qa_q1_body body = qa_q1_get_body(game, entity);
  const qa_vec3 delta = qa_q1_vsub(target.origin, body.origin);
  const double distance = qa_q1_length(delta);
  if (!is_army(monster)) {
    if (distance < 120) {
      entity->attack_state = "melee";
      return true;
    }
    const double height = target.bounds.max.z - target.bounds.min.z;
    const bool vertical = body.origin.z + body.bounds.min.z <= target.origin.z + target.bounds.min.z + height * 0.75 &&
                          body.origin.z + body.bounds.max.z >= target.origin.z + target.bounds.min.z + height * 0.25;
    const double horizontal = hypot(delta.x, delta.y);
    if (vertical && horizontal >= 80 && horizontal <= 150) {
      entity->attack_state = "missile";
      return true;
    }
    return false;
  }
  if (game->time < monster->attack_finished || distance >= 1000) return false;
  const qa_q1_monster_target_result observed = qa_q1_monster_target(game, enemy);
  if (!observed.has_value) return false;
  qa_q1_trace_request request = {qa_q1_vadd(body.origin, v3(0, 0, 25)),
                                 qa_q1_vadd(target.origin, v3(0, 0, observed.value.view_height)),
                                 QA_Q1_POINT, true, entity->actor.id, true, false, false};
  const qa_q1_trace trace = qa_q1_host_trace(game, request);
  if (!trace.has_actor || !qa_same_actor(trace.actor, enemy) || (trace.in_open && trace.in_water))
    return false;
  if (qa_q1_host_random(game) >= (distance < 120 ? 0.9 : distance < 500 ? 0.4 : 0.05)) return false;
  set_sequence(monster, "attack", 81, NULL, 9);
  monster->attack_finished = game->time + 1 + qa_q1_host_random(game);
  monster->refired = false;
  (void)qa_q1_host_random(game);
  return true;
}

static void schedule_frame(qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1_schedule(game, entity, 0.1, named_action(game, entity, "monster_frame"));
}
static void walk_move(qa_q1_services *game, qa_q1_actor *entity, double yaw, double distance) {
  if (game->host.walk_move) (void)game->host.walk_move(game->host.ctx, entity->actor, yaw, distance);
}
static void move_to_goal(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id goal, double distance,
                         const char *mode) {
  if (game->host.move_to_goal) game->host.move_to_goal(game->host.ctx, entity->actor, goal, distance, mode);
}

static void monster_frame(qa_q1_services *game, qa_q1_actor *entity, qa_q1_monster *monster) {
  const char *mode = monster->mode ? monster->mode : "";
  const size_t index = monster->frame_index >= 0 ? (size_t)monster->frame_index : 0;
  const double distance = index < monster->sequence_count ? monster->sequence[index] : 0;
  entity->frame = monster->first_frame + (double)index;
  if ((!strcmp(mode, "walk") || !strcmp(mode, "run")) && index == 0 && qa_q1_host_random(game) < 0.2)
    qa_q1_sound(game, entity->actor.id, is_army(monster) ? "soldier/idle.wav" : "dog/idle.wav", "voice", 2, 1);
  if (!strcmp(mode, "death")) {
    if (is_army(monster) && index == 2 && !monster->death_drop) {
      entity->solid = "none";
      qa_q1_link(game, entity);
      qa_q1_drop_shells(game, qa_q1_get_body(game, entity).origin);
      monster->death_drop = true;
    }
    if (distance != 0) walk_move(game, entity, qa_q1_get_body(game, entity).angles.y, distance);
  } else if (!strcmp(mode, "pain")) {
    if (distance != 0) walk_move(game, entity, qa_q1_get_body(game, entity).angles.y, distance);
  } else if (!strcmp(mode, "stand") || !strcmp(mode, "walk")) {
    if (find_target(game, entity, monster)) {
      schedule_frame(game, entity);
      return;
    }
    qa_actor_id route_goal = entity->actor.id;
    if (!strcmp(mode, "stand") && game->time >= monster->pause_until &&
        mission_route(game, entity->actor.id, &route_goal))
      walk(monster);
    if (!strcmp(mode, "walk") && game->time >= monster->pause_until) {
      bool has_target = false;
      qa_actor_id target = entity->actor.id;
      if (qa_q1_mission(game, entity->actor.id) == NULL) {
        qa_q1_actor *found = NULL;
        if (monster->path != NULL && qa_q1_find(game, monster->path, &found, 1) > 0 && found != NULL) {
          target = found->actor.id;
          has_target = true;
        }
      } else {
        has_target = mission_route(game, entity->actor.id, &target);
      }
      if (!has_target) stand(monster);
      else move_to_goal(game, entity, target, distance, NULL);
    }
  } else {
    if (!monster->has_enemy || !target_eligible(game, monster->enemy)) {
      entity->attack_state = "straight";
      if (monster->has_old_enemy && target_eligible(game, monster->old_enemy)) {
        monster->enemy = monster->old_enemy;
        monster->has_enemy = true;
        monster->has_old_enemy = false;
        run_monster(monster);
        monster_frame(game, entity, monster);
        return;
      }
      monster->has_enemy = false;
      monster->has_old_enemy = false;
      qa_actor_id route_goal = entity->actor.id;
      if (mission_route(game, entity->actor.id, &route_goal) ||
          (monster->path != NULL && strcmp(monster->path, "") != 0))
        walk(monster);
      else
        stand(monster);
      schedule_frame(game, entity);
      return;
    }
    const qa_actor_id enemy = monster->enemy;
    qa_q1_body target;
    if (!body_of(game, enemy, &target)) return;
    if (!strcmp(mode, "run")) {
      const qa_q1_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
      qa_actor_id combat_goal = entity->actor.id;
      bool stand_ground = false;
      const bool has_combat_goal = mission != NULL && mission->combat_route != NULL &&
                                   mission->combat_route(mission->ctx, &combat_goal, &stand_ground);
      if (has_combat_goal) {
        move_to_goal(game, entity, combat_goal, distance, "contact");
        if (monster->sequence_count > 0)
          monster->frame_index = fmod(monster->frame_index + 1, (double)monster->sequence_count);
        schedule_frame(game, entity);
        return;
      }
      const bool seen = visible(game, entity, enemy);
      if (seen) monster->search_until = game->time + 5;
      if (game->options.coop && monster->search_until < game->time && find_target(game, entity, monster)) {
        schedule_frame(game, entity);
        return;
      }
      if (entity->attack_state == NULL || strcmp(entity->attack_state, "straight") != 0) {
        const double yaw = face(game, entity, target.origin);
        const double ideal = qa_q1_yaw_for(qa_q1_vsub(target.origin, qa_q1_get_body(game, entity).origin));
        const double delta = fmod(yaw - ideal + 360, 360);
        if (delta <= 45 || delta >= 315) {
          if (entity->attack_state != NULL && !strcmp(entity->attack_state, "melee")) {
            static const double MELEE[8] = {10, 10, 10, 10, 10, 10, 10, 10};
            set_sequence(monster, "attack", 0, MELEE, 8);
          } else {
            set_sequence(monster, "leap", 60, NULL, 9);
          }
          entity->attack_state = "straight";
          monster_frame(game, entity, monster);
          return;
        }
        if (monster->sequence_count > 0)
          monster->frame_index = fmod(monster->frame_index + 1, (double)monster->sequence_count);
        schedule_frame(game, entity);
        return;
      }
      if (seen && try_attack(game, entity, monster)) {
        if (monster->mode == NULL || strcmp(monster->mode, mode) != 0) {
          monster_frame(game, entity, monster);
          return;
        }
        if (monster->sequence_count > 0)
          monster->frame_index = fmod(monster->frame_index + 1, (double)monster->sequence_count);
        schedule_frame(game, entity);
        return;
      }
      if (!stand_ground) move_to_goal(game, entity, enemy, distance, NULL);
    } else if (!strcmp(mode, "attack")) {
      (void)face(game, entity, target.origin);
      if (is_army(monster)) {
        if (index == 4) {
          qa_q1_sound(game, entity->actor.id, "soldier/sattck1.wav", "weapon", 1, 1);
          const qa_vec3 aim = qa_q1_normalize(qa_q1_vsub(
              qa_q1_vsub(target.origin, qa_q1_vscale(target.velocity, 0.2)),
              qa_q1_get_body(game, entity).origin));
          qa_q1_fire_bullets(game, entity->actor, aim, qa_q1_actor_vector(entity, "v_angle"), 4, 0.1, 0.1, NULL);
          qa_q1_effect(game, "muzzleflash", qa_q1_get_body(game, entity).origin, &entity->actor.id, 1);
        }
        if (index == 6 && game->options.skill == 3 && !monster->refired && visible(game, entity, enemy)) {
          monster->refired = true;
          monster->frame_index = 0;
          schedule_frame(game, entity);
          return;
        }
      } else {
        move_to_goal(game, entity, enemy, distance, NULL);
        if (index == 3) {
          qa_q1_sound_defaults(game, entity->actor.id, "dog/dattack1.wav");
          if (qa_q1_can_damage(game, enemy, entity->actor.id) &&
              qa_q1_length(qa_q1_vsub(target.origin, qa_q1_get_body(game, entity).origin)) <= 100)
            (void)qa_q1_damage(game, enemy, entity->actor.id, &entity->actor.id,
                               (qa_q1_host_random(game) + qa_q1_host_random(game) + qa_q1_host_random(game)) * 8,
                               NULL, "direct", "");
        }
      }
    } else if (!strcmp(mode, "leap")) {
      if (index < 2) (void)face(game, entity, target.origin);
      if (index == 1) {
        const qa_q1_body body = qa_q1_get_body(game, entity);
        entity->movement = "toss";
        qa_q1_body patch = body;
        patch.origin = qa_q1_vadd(body.origin, v3(0, 0, 1));
        patch.velocity = qa_q1_vadd(qa_q1_vscale(qa_q1_make_vectors(game, body.angles).forward, 300), v3(0, 0, 200));
        patch.has_ground = false;
        qa_q1_set_body_patch(game, entity, &patch, true, false, true, false, true);
        entity->touch = qa_q1_callback_registry_touch(game->named, entity, "Dog_JumpTouch");
      }
    }
  }
  if (monster->mode != NULL && !strcmp(monster->mode, mode)) {
    if (index + 1 < monster->sequence_count) monster->frame_index += 1;
    else if (!strcmp(mode, "death")) return;
    else if (!strcmp(mode, "leap")) monster->frame_index = (double)monster->sequence_count - 1;
    else if (!strcmp(mode, "attack") || !strcmp(mode, "pain")) run_monster(monster);
    else monster->frame_index = 0;
  }
  schedule_frame(game, entity);
}

static qa_vec3 damage_velocity(qa_q1_services *game, double damage) {
  return qa_q1_vscale(v3(100 * (qa_q1_host_random(game) * 2 - 1), 100 * (qa_q1_host_random(game) * 2 - 1),
                         200 + 100 * qa_q1_host_random(game)),
                      damage > -50 ? 0.7 : damage > -200 ? 2 : 10);
}
static void throw_gib(qa_q1_services *game, qa_vec3 origin, const char *model, double damage) {
  qa_q1_actor *gib = qa_q1_create(game, "gib", NULL, NULL);
  if (gib == NULL) return;
  char path[128];
  (void)snprintf(path, sizeof path, "progs/%s.mdl", model);
  set_string(&gib->model, path);
  gib->movement = "bounce";
  qa_q1_body patch = qa_q1_get_body(game, gib);
  patch.origin = origin;
  patch.velocity = damage_velocity(game, damage);
  patch.bounds = QA_Q1_POINT;
  qa_q1_set_body_patch(game, gib, &patch, true, false, true, true, false);
  gib->angular_velocity = v3(qa_q1_host_random(game) * 600, qa_q1_host_random(game) * 600,
                             qa_q1_host_random(game) * 600);
  qa_q1_schedule(game, gib, 10 + qa_q1_host_random(game) * 10, named_action(game, gib, "SUB_Remove"));
  qa_q1_link(game, gib);
}
static void throw_head(qa_q1_services *game, qa_q1_actor *entity, const char *model, double damage) {
  qa_q1_cancel(game, entity);
  char path[128];
  (void)snprintf(path, sizeof path, "progs/%s.mdl", model);
  set_string(&entity->model, path);
  entity->frame = 0;
  entity->movement = "bounce";
  entity->damageable = false;
  entity->solid = "none";
  const qa_vec3 origin = qa_q1_get_body(game, entity).origin;
  qa_q1_body patch = qa_q1_get_body(game, entity);
  patch.origin = qa_q1_vadd(origin, v3(0, 0, -24));
  patch.velocity = damage_velocity(game, damage);
  patch.bounds.min = v3(-16, -16, 0);
  patch.bounds.max = v3(16, 16, 56);
  patch.has_ground = false;
  qa_q1_set_body_patch(game, entity, &patch, true, false, true, true, true);
  entity->angular_velocity = v3(0, (qa_q1_host_random(game) * 2 - 1) * 600, 0);
  qa_q1_link(game, entity);
}

static void gib(qa_q1_services *game, qa_q1_actor *entity, qa_q1_monster *monster) {
  const double health = qa_q1_health(game, entity->actor.id);
  qa_q1_sound_defaults(game, entity->actor.id, "player/udeath.wav");
  if (is_army(monster)) throw_head(game, entity, "h_guard", health);
  static const char *const ARMY_GIBS[3] = {"gib1", "gib2", "gib3"};
  static const char *const DOG_GIBS[3] = {"gib3", "gib3", "gib3"};
  const char *const *models = is_army(monster) ? ARMY_GIBS : DOG_GIBS;
  for (size_t i = 0; i < 3; i++)
    throw_gib(game, qa_q1_get_body(game, entity).origin, models[i], health);
  if (!is_army(monster)) throw_head(game, entity, "h_dog", health);
}

void qa_q1_spawn_monster(qa_q1_services *game, qa_q1_actor *entity) {
  const bool army = entity->classname != NULL && !strcmp(entity->classname, "monster_army");
  if (qa_q1_services_uses_id1(game)) {
    if (game->options.deathmatch != 0) {
      qa_q1_remove(game, entity);
      return;
    }
    if (army) {
      static const char *const MODELS[5] = {"progs/soldier.mdl", "progs/h_guard.mdl", "progs/gib1.mdl",
                                            "progs/gib2.mdl", "progs/gib3.mdl"};
      static const char *const SOUNDS[7] = {"soldier/death1.wav", "soldier/idle.wav", "soldier/pain1.wav",
                                            "soldier/pain2.wav", "soldier/sattck1.wav", "soldier/sight1.wav",
                                            "player/udeath.wav"};
      for (size_t i = 0; i < 5; i++) (void)qa_q1_services_precache_model(game, MODELS[i]);
      for (size_t i = 0; i < 7; i++) (void)qa_q1_services_precache_sound(game, SOUNDS[i]);
    } else {
      static const char *const MODELS[2] = {"progs/h_dog.mdl", "progs/dog.mdl"};
      static const char *const SOUNDS[5] = {"dog/dattack1.wav", "dog/ddeath.wav", "dog/dpain1.wav",
                                            "dog/dsight.wav", "dog/idle.wav"};
      for (size_t i = 0; i < 2; i++) (void)qa_q1_services_precache_model(game, MODELS[i]);
      for (size_t i = 0; i < 5; i++) (void)qa_q1_services_precache_sound(game, SOUNDS[i]);
    }
  }
  qa_q1_monster *monster = calloc(1, sizeof *monster);
  if (monster == NULL) return;
  monster->species = army ? "army" : "dog";
  monster->mode = "stand";
  set_string(&monster->path, entity->target ? entity->target : "");
  free(entity->monster ? entity->monster->path : NULL);
  free(entity->monster);
  entity->monster = monster;
  stand(monster);
  entity->solid = "slidebox";
  entity->movement = "step";
  entity->aimed_damage = true;
  entity->path_end = named_action(game, entity, "monster_path_end");
  set_string(&entity->model, army ? "progs/soldier.mdl" : "progs/dog.mdl");
  entity->max_health = army ? 30 : 25;
  qa_q1_set_health(game, entity->actor, entity->max_health);
  const qa_bounds bounds = {v3(army ? -16 : -32, army ? -16 : -32, -24), v3(army ? 16 : 32, army ? 16 : 32, 40)};
  qa_q1_set_bounds(game, entity, bounds);
  const qa_q1_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
  if (mission == NULL) game->total_monsters += 1;
  else if (mission->spawned) mission->spawned(mission->ctx);
  entity->use = qa_q1_callback_registry_use(game->named, entity, "monster_use");
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, "monster_pain");
  entity->die = qa_q1_callback_registry_die(game->named, entity, "monster_die");
  qa_q1_schedule(game, entity, 0.1 + qa_q1_host_random(game) * 0.5,
                 named_action(game, entity, "walkmonster_start_go"));
}

static void dog_leap_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                           const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  qa_q1_monster *monster = require_monster(entity);
  if (qa_q1_health(game, entity->actor.id) <= 0) return;
  if (can_take_damage(game, other) && qa_q1_length(qa_q1_get_body(game, entity).velocity) > 300)
    (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, 10 + 10 * qa_q1_host_random(game),
                       NULL, "direct", "");
  const bool grounded = game->host.check_bottom ? game->host.check_bottom(game->host.ctx, entity->actor.id) : false;
  if (grounded) {
    memset(&entity->touch, 0, sizeof entity->touch);
    entity->movement = "step";
    run_monster(monster);
    schedule_frame(game, entity);
  }
}

static void retaliate(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  qa_q1_monster *monster = require_monster(entity);
  if (attacker == NULL || qa_same_actor(*attacker, entity->actor.id)) return;
  qa_q1_actor *attacker_entity = qa_q1_get_entity(game, attacker);
  if (attacker_entity != NULL && attacker_entity->classname != NULL && entity->classname != NULL &&
      !strcmp(attacker_entity->classname, entity->classname) && !is_army(monster))
    return;
  if (monster->has_enemy && qa_q1_is_player(game, &monster->enemy)) {
    monster->old_enemy = monster->enemy;
    monster->has_old_enemy = true;
  }
  if (!monster->has_enemy || !qa_same_actor(monster->enemy, *attacker))
    found_target(game, entity, monster, *attacker);
}

static void monster_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                        const qa_actor_id *activator) {
  (void)ctx; (void)other;
  const qa_q1_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
  if (mission != NULL && mission->use && mission->use(mission->ctx, activator)) return;
  qa_q1_monster *monster = require_monster(entity);
  if (monster->has_enemy || qa_q1_health(game, entity->actor.id) <= 0 ||
      activator == NULL || !qa_q1_is_player(game, activator))
    return;
  const qa_q1_monster_target_result observed = qa_q1_monster_target(game, *activator);
  if (!observed.has_value || observed.value.invisible || observed.value.notarget) return;
  monster->enemy = *activator;
  monster->has_enemy = true;
  qa_q1_schedule(game, entity, 0.1, named_action(game, entity, "monster_found_target"));
}

static void monster_pain(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker,
                         double damage) {
  (void)ctx; (void)damage;
  qa_q1_monster *monster = require_monster(entity);
  retaliate(game, entity, attacker);
  if (is_army(monster)) {
    if (monster->pain_finished > game->time) return;
    const double random = qa_q1_host_random(game);
    monster->pain_finished = game->time + (random < 0.2 ? 0.6 : 1.1);
    if (random < 0.2) {
      static const double SEQ[6] = {0, 0, 0, 0, 0, -1};
      set_sequence(monster, "pain", 40, SEQ, 6);
    } else if (random < 0.6) {
      static const double SEQ[14] = {0, 13, 9, 0, 0, 0, 0, 0, 0, 0, 0, -2, 0, 0};
      set_sequence(monster, "pain", 46, SEQ, 14);
    } else {
      static const double SEQ[13] = {0, -1, 0, 0, 1, 1, 0, -1, 4, 3, 6, 8, 0};
      set_sequence(monster, "pain", 60, SEQ, 13);
    }
    qa_q1_sound_defaults(game, entity->actor.id, random < 0.2 ? "soldier/pain1.wav" : "soldier/pain2.wav");
    if (game->options.skill == 3) monster->pain_finished = game->time + 5;
  } else {
    qa_q1_sound_defaults(game, entity->actor.id, "dog/dpain1.wav");
    if (qa_q1_host_random(game) > 0.5) {
      set_sequence(monster, "pain", 26, NULL, 6);
    } else {
      static const double SEQ[16] = {0, 0, -4, -12, -12, -2, 0, -4, 0, -10, 0, 0, 0, 0, 0, 0};
      set_sequence(monster, "pain", 32, SEQ, 16);
    }
  }
  monster_frame(game, entity, monster);
}

static void monster_die(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  (void)ctx;
  qa_q1_monster *monster = require_monster(entity);
  entity->damageable = false;
  memset(&entity->touch, 0, sizeof entity->touch);
  const qa_q1_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
  const qa_actor_id fallback = attacker ? *attacker : entity->actor.id;
  const qa_actor_id *by = monster->has_enemy ? &monster->enemy : (attacker ? attacker : NULL);
  (void)fallback;
  if (mission == NULL) {
    game->killed_monsters += 1;
    qa_q1_event event = {0};
    event.kind = "monster-killed";
    event.has_actor = true;
    event.actor = entity->actor.id;
    event.total = game->total_monsters;
    event.found = game->killed_monsters;
    qa_q1_emit(game, event);
    qa_q1_use_targets(game, entity, by);
  } else if (mission->killed) {
    mission->killed(mission->ctx, by);
  }
  if (qa_q1_health(game, entity->actor.id) < -35) {
    gib(game, entity, monster);
    return;
  }
  if (!is_army(monster)) {
    entity->solid = "none";
    qa_q1_sound_defaults(game, entity->actor.id, "dog/ddeath.wav");
    set_sequence(monster, "death", qa_q1_host_random(game) > 0.5 ? 8 : 17, NULL, 9);
  } else {
    qa_q1_sound_defaults(game, entity->actor.id, "soldier/death1.wav");
    if (qa_q1_host_random(game) < 0.5) {
      set_sequence(monster, "death", 8, NULL, 10);
    } else {
      static const double SEQ[11] = {0, -5, -4, -13, -3, -4, 0, 0, 0, 0, 0};
      set_sequence(monster, "death", 18, SEQ, 11);
    }
  }
  qa_q1_link(game, entity);
  monster_frame(game, entity, monster);
}

static void monster_start(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_monster *monster = require_monster(entity);
  const qa_q1_body body = qa_q1_get_body(game, entity);
  const qa_vec3 start = qa_q1_vadd(body.origin, v3(0, 0, 1));
  qa_q1_body patch = body;
  patch.origin = start;
  qa_q1_set_body_patch(game, entity, &patch, true, false, false, false, false);
  qa_q1_trace_request request = {start, qa_q1_vadd(start, v3(0, 0, -256)), body.bounds,
                                 true, entity->actor.id, true, false, false};
  const qa_q1_trace floor_trace = qa_q1_host_trace(game, request);
  if (floor_trace.fraction < 1 && !floor_trace.all_solid) {
    patch = qa_q1_get_body(game, entity);
    patch.origin = floor_trace.end;
    patch.has_ground = floor_trace.has_actor;
    if (floor_trace.has_actor) patch.ground = floor_trace.actor;
    qa_q1_set_body_patch(game, entity, &patch, true, false, false, false, true);
    entity->movement_flags = (double)((long)entity->movement_flags | 512);
  }
  walk_move(game, entity, 0, 0);
  entity->movement_flags = (double)((long)entity->movement_flags | 32);
  entity->ideal_yaw = body.angles.y;
  entity->yaw_speed = qa_q1_actor_number(entity, "yaw_speed") ? qa_q1_actor_number(entity, "yaw_speed") : 20;
  entity->damageable = true;
  qa_q1_link(game, entity);
  const qa_q1_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
  bool should_walk;
  if (mission == NULL) {
    qa_q1_actor *found = NULL;
    should_walk = monster->path != NULL && strcmp(monster->path, "") != 0 &&
                  qa_q1_find(game, monster->path, &found, 1) > 0 && found != NULL &&
                  found->classname != NULL && !strcmp(found->classname, "path_corner");
  } else {
    qa_actor_id goal = entity->actor.id;
    should_walk = mission_route(game, entity->actor.id, &goal);
  }
  if (should_walk) walk(monster);
  if (mission != NULL && mission->started) mission->started(mission->ctx);
  qa_q1_schedule(game, entity, 0.1 + qa_q1_host_random(game) * 0.5,
                 named_action(game, entity, "monster_frame"));
}

static void frame_cb(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  monster_frame(game, entity, require_monster(entity));
}
static void path_end_cb(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_monster *monster = require_monster(entity);
  monster->pause_until = qa_q1_path_end_time(game->time);
  stand(monster);
  entity->frame = monster->first_frame;
}
static void found_target_cb(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_monster *monster = require_monster(entity);
  if (!monster->has_enemy) return;
  found_target(game, entity, monster, monster->enemy);
  monster_frame(game, entity, monster);
}

void qa_q1_foundation_register_monster_callbacks(qa_q1_services *game) {
  qa_q1_callback_handlers h = {0};
  h.action = frame_cb;
  qa_q1_callback_registry_register(game->named, "monster_frame", h);
  h = (qa_q1_callback_handlers){0}; h.action = path_end_cb;
  qa_q1_callback_registry_register(game->named, "monster_path_end", h);
  h = (qa_q1_callback_handlers){0}; h.action = found_target_cb;
  qa_q1_callback_registry_register(game->named, "monster_found_target", h);
  h = (qa_q1_callback_handlers){0}; h.use = monster_use;
  qa_q1_callback_registry_register(game->named, "monster_use", h);
  h = (qa_q1_callback_handlers){0}; h.pain = monster_pain;
  qa_q1_callback_registry_register(game->named, "monster_pain", h);
  h = (qa_q1_callback_handlers){0}; h.die = monster_die;
  qa_q1_callback_registry_register(game->named, "monster_die", h);
  h = (qa_q1_callback_handlers){0}; h.touch = dog_leap_touch;
  qa_q1_callback_registry_register(game->named, "Dog_JumpTouch", h);
  h = (qa_q1_callback_handlers){0}; h.action = monster_start;
  qa_q1_callback_registry_register(game->named, "walkmonster_start_go", h);
}
