/* Port of src/content/q1/base/monsters.ts. GPL-2.0-or-later. */
#include "qa/content/q1/base/monsters.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/monsters/target.h"
#include "qa/content/q1/base/frames.h"
#include "qa/content/q1/base/monster_actions.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

#define V3(x, y, z) ((qa_vec3){(x), (y), (z)})
#define FR(x) ((double)(float)(x))
static const qa_vec3 ZERO3 = {0, 0, 0};
static const qa_bounds POINT_BOUNDS = {{0, 0, 0}, {0, 0, 0}};

static void copy_name(char *dst, size_t size, const char *src) {
  size_t length = strlen(src);
  if (length >= size) length = size - 1;
  memcpy(dst, src, length);
  dst[length] = '\0';
}
static const char *prefix_of(const qa_q1_base_monster *monster) {
  return monster->has_source && monster->source.callback_prefix != NULL
             ? monster->source.callback_prefix : "base";
}
static qa_q1b_action named_action(qa_q1_base_monster *monster, qa_q1b_actor *entity, const char *suffix) {
  char name[128];
  (void)snprintf(name, sizeof name, "%s:%s", prefix_of(monster), suffix);
  return monster->game->named.action(monster->game->named.ctx, entity, name);
}
static bool species_is(const qa_q1_base_monster *monster, const char *species) {
  return strcmp(monster->spec->species, species) == 0;
}
static bool edition_is(const qa_q1_base_monster *monster, const char *edition) {
  return strcmp(monster->game->options.edition, edition) == 0;
}
/* game.monsterTarget(actor) with TS null as a NULL out pointer use. */
static bool target_observation(qa_q1_base_monster *monster, qa_actor_id actor,
                               qa_monster_target_observation *out) {
  return monster->game->monster_target(monster->game, actor, out);
}
static bool target_eligible(qa_q1_base_monster *monster, qa_actor_id actor) {
  qa_monster_target_observation observation;
  bool has = target_observation(monster, actor, &observation);
  return qa_monster_target_eligible(monster->game->health(monster->game, actor),
                                    has ? &observation : NULL);
}

qa_q1_base_monster *qa_q1_base_monster_create(qa_q1b_game *game, qa_q1b_actor *entity,
                                              const qa_q1_base_monster_species *spec,
                                              qa_q1_monster_services services,
                                              const qa_q1_base_monster_source *source) {
  qa_q1_base_monster *monster = malloc(sizeof *monster);
  if (monster == NULL) return NULL;
  memset(monster, 0, sizeof *monster);
  monster->game = game;
  monster->entity = entity;
  monster->spec = spec;
  monster->services = services;
  if (source != NULL) { monster->has_source = true; monster->source = *source; }
  copy_name(monster->current_frame, sizeof monster->current_frame, spec->stand);
  copy_name(monster->next_frame, sizeof monster->next_frame, spec->stand);
  if (entity->monster != NULL) {
    monster->state = *entity->monster;
  } else {
    monster->state.species = spec->species;
    monster->state.mode = "stand";
    copy_name(monster->state.path, sizeof monster->state.path, entity->target);
  }
  entity->monster = &monster->state;
  qa_q1b_fields_set(entity, "source.monsterCallbackPrefix", prefix_of(monster));
  return monster;
}
void qa_q1_base_monster_destroy(qa_q1_base_monster *monster) { free(monster); }

static bool set_owned(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) { qa_save_value_free(child); return false; }
  return true;
}
qa_save_value *qa_q1_base_monster_capture(const qa_q1_base_monster *monster) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  bool ok = set_owned(object, "currentFrame", qa_save_value_string(monster->current_frame));
  ok = ok && set_owned(object, "nextFrame", qa_save_value_string(monster->next_frame));
  ok = ok && set_owned(object, "inPain", qa_save_value_number(monster->in_pain));
  ok = ok && set_owned(object, "counter", qa_save_value_number(monster->counter));
  ok = ok && set_owned(object, "idleUntil", qa_save_value_number(monster->idle_until));
  ok = ok && set_owned(object, "lefty", qa_save_value_boolean(monster->lefty));
  ok = ok && set_owned(object, "sliding", qa_save_value_boolean(monster->sliding));
  ok = ok && set_owned(object, "lightningCount", qa_save_value_number(monster->lightning_count));
  ok = ok && set_owned(object, "countedDeath", qa_save_value_boolean(monster->counted_death));
  if (!ok) { qa_save_value_free(object); return NULL; }
  return object;
}
static void must_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
}
static const char *field_string(const qa_save_reader *reader, const char *name, qa_save_reader *scratch) {
  must_field(reader, name, scratch);
  return qa_save_reader_string(scratch);
}
static double field_number(const qa_save_reader *reader, const char *name) {
  qa_save_reader scratch;
  must_field(reader, name, &scratch);
  double value = qa_save_reader_number(&scratch);
  qa_save_reader_destroy(&scratch);
  return value;
}
static bool field_boolean(const qa_save_reader *reader, const char *name) {
  qa_save_reader scratch;
  must_field(reader, name, &scratch);
  bool value = qa_save_reader_boolean(&scratch);
  qa_save_reader_destroy(&scratch);
  return value;
}
void qa_q1_base_monster_restore(qa_q1_base_monster *monster, const qa_save_reader *reader) {
  qa_save_reader scratch;
  copy_name(monster->current_frame, sizeof monster->current_frame, field_string(reader, "currentFrame", &scratch));
  qa_save_reader_destroy(&scratch);
  copy_name(monster->next_frame, sizeof monster->next_frame, field_string(reader, "nextFrame", &scratch));
  qa_save_reader_destroy(&scratch);
  const qa_q1_monster_frame_table *source_frames =
      monster->has_source ? monster->source.frames : NULL;
  const qa_q1_monster_action_entry *actions = monster->has_source ? monster->source.actions : NULL;
  size_t action_count = monster->has_source ? monster->source.action_count : 0;
  const char *names[2] = {monster->current_frame, monster->next_frame};
  for (int i = 0; i < 2; i++) {
    bool known = qa_q1_monster_frames_has(names[i]) ||
                 qa_q1_monster_frame_table_has(source_frames, names[i]);
    if (!known && actions != NULL) {
      for (size_t a = 0; a < action_count; a++)
        if (strcmp(actions[a].name, names[i]) == 0) { known = true; break; }
    }
    if (!known) qa_save_reader_fail(reader, "unknown source monster continuation");
  }
  monster->in_pain = field_number(reader, "inPain");
  monster->counter = field_number(reader, "counter");
  monster->idle_until = field_number(reader, "idleUntil");
  monster->lefty = field_boolean(reader, "lefty");
  monster->sliding = field_boolean(reader, "sliding");
  monster->lightning_count = field_number(reader, "lightningCount");
  monster->counted_death = field_boolean(reader, "countedDeath");
}
qa_q1b_opt_actor qa_q1_base_monster_enemy(const qa_q1_base_monster *monster) {
  return monster->state.enemy;
}
void qa_q1_base_monster_set_enemy(qa_q1_base_monster *monster, qa_q1b_opt_actor enemy) {
  monster->state.enemy = enemy;
}
qa_vec3 qa_q1_base_monster_origin(const qa_q1_base_monster *monster) {
  return monster->game->body(monster->game, monster->entity).origin;
}
bool qa_q1_base_monster_target(const qa_q1_base_monster *monster, qa_vec3 *out) {
  if (!monster->state.enemy.has) return false;
  qa_q1b_body body;
  if (!monster->game->host->bodies_read(monster->game->host->ctx, monster->state.enemy.id, &body))
    return false;
  *out = body.origin;
  return true;
}
double qa_q1_base_monster_distance(const qa_q1_base_monster *monster) {
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target)) return INFINITY;
  return qa_length3(qa_sub3(target, qa_q1_base_monster_origin(monster)));
}

void qa_q1_base_monster_play(qa_q1_base_monster *monster, const char *name) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  if (!game->live(game, entity)) return;
  if (game->health(game, entity->actor.id) > 0 && monster->state.enemy.has &&
      !target_eligible(monster, monster->state.enemy.id)) {
    qa_q1b_opt_actor previous = monster->state.old_enemy;
    monster->state.enemy = previous.has && target_eligible(monster, previous.id)
                               ? previous : QA_Q1B_NO_ACTOR;
    monster->state.old_enemy = QA_Q1B_NO_ACTOR;
    entity->attack_state = "straight";
    const char *resume = !monster->state.enemy.has
                             ? (qa_q1_base_monster_route(monster).has ? monster->spec->walk : monster->spec->stand)
                             : monster->spec->run;
    /* TS tests route() === null first; order is unobservable here. */
    qa_q1_base_monster_play(monster, resume);
    return;
  }
  const qa_q1_monster_frame *frame = NULL;
  if (monster->has_source && monster->source.frames != NULL)
    frame = qa_q1_monster_frame_table_get(monster->source.frames, name);
  if (frame == NULL) frame = qa_q1_monster_frames_get(name);
  if (frame == NULL) qa_range_error("Missing Q1 source animation %s", name);
  copy_name(monster->current_frame, sizeof monster->current_frame, name);
  copy_name(monster->next_frame, sizeof monster->next_frame, frame->next);
  entity->frame = frame->frame;
  game->schedule(game, entity, 0.1, named_action(monster, entity, "monster_frame"));
  if (edition_is(monster, "classic")) {
    if (strcmp(name, "boss_idle1") == 0 || strcmp(name, "f_death2") == 0) return;
    if (strcmp(name, "f_death21") == 0) { entity->solid = "none"; game->link(game, entity); return; }
    if (strcmp(name, "sham_magic11") == 0 && game->options.skill == 3) { qa_q1_cast_lightning(monster); return; }
  }
  for (size_t i = 0; i < frame->operation_count; i++) {
    if (!game->live(game, entity)) return;
    const qa_q1_monster_operation *op = &frame->operations[i];
    switch (op->kind) {
      case QA_Q1_OP_AI: qa_q1_base_monster_ai(monster, op->mode, op->distance); break;
      case QA_Q1_OP_SOLID: entity->solid = op->solid; game->link(game, entity); break;
      case QA_Q1_OP_LIGHTSTYLE: {
        qa_q1b_event event = {0};
        event.kind = "lightstyle"; event.lightstyle = 0; event.pattern = op->pattern;
        game->host->emit(game->host->ctx, &event);
        break;
      }
      case QA_Q1_OP_ACTION: {
        bool done = false;
        if (monster->has_source && monster->source.actions != NULL) {
          for (size_t a = 0; a < monster->source.action_count; a++) {
            if (strcmp(monster->source.actions[a].name, op->name) == 0) {
              monster->source.actions[a].fn(monster->source.actions_ctx, monster);
              done = true; break;
            }
          }
        }
        if (!done) qa_q1_monster_action(monster, op->name);
        break;
      }
      case QA_Q1_OP_SOUND: {
        double r = op->has_chance ? game->host->random(game->host->ctx) : 0;
        if (!op->has_chance || (strcmp(op->comparison, "greater") == 0 ? r > op->chance : r < op->chance))
          game->sound(game, entity, op->path, op->channel, op->attenuation, 1);
        break;
      }
    }
  }
}
void qa_q1_base_monster_delay(qa_q1_base_monster *monster, double seconds) {
  monster->game->schedule(monster->game, monster->entity, seconds,
                          named_action(monster, monster->entity, "monster_frame"));
}
void qa_q1_base_monster_change_yaw(qa_q1_base_monster *monster) {
  monster->game->host->change_yaw(monster->game->host->ctx, monster->entity->actor);
}
void qa_q1_base_monster_face(qa_q1_base_monster *monster) {
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target)) return;
  qa_q1b_body body = monster->game->body(monster->game, monster->entity);
  monster->entity->ideal_yaw = qa_q1b_yaw_for(qa_sub3(target, body.origin));
  qa_q1_base_monster_change_yaw(monster);
}
qa_q1b_basis qa_q1_base_monster_make_vectors(qa_q1_base_monster *monster) {
  qa_vec3 angles = monster->game->body(monster->game, monster->entity).angles;
  if (edition_is(monster, "rerelease")) angles.x = -angles.x;
  return monster->game->make_vectors(monster->game, angles);
}
bool qa_q1_base_monster_eye(qa_q1_base_monster *monster, qa_actor_id target, qa_vec3 *out) {
  qa_q1b_body body;
  if (!monster->game->host->bodies_read(monster->game->host->ctx, target, &body)) return false;
  qa_q1b_actor *entity = monster->game->entity(monster->game, qa_q1b_some_actor(target));
  qa_vec3 offset;
  if (entity != NULL && qa_q1b_fields_has(entity, "view_ofs")) {
    offset = qa_q1b_actor_vector(entity, "view_ofs");
  } else {
    double z = 25;
    if (entity != NULL && entity->monster != NULL && strcmp(entity->monster->species, "fish") == 0) {
      z = 10;
    } else {
      qa_monster_target_observation observation;
      if (target_observation(monster, target, &observation)) z = observation.view_height;
    }
    offset = V3(0, 0, z);
  }
  *out = qa_add3(body.origin, offset);
  return true;
}
double qa_q1_base_monster_range_distance(qa_q1_base_monster *monster, qa_q1b_opt_actor target) {
  qa_vec3 start, end;
  if (!qa_q1_base_monster_eye(monster, monster->entity->actor.id, &start)) return INFINITY;
  if (!target.has) target = monster->state.enemy;
  if (!target.has) return INFINITY;
  if (!qa_q1_base_monster_eye(monster, target.id, &end)) return INFINITY;
  return qa_length3(qa_sub3(end, start));
}
bool qa_q1_base_monster_visible(qa_q1_base_monster *monster, qa_q1b_opt_actor target) {
  if (!target.has) target = monster->state.enemy;
  if (!target.has) return false;
  qa_vec3 start, end;
  if (!qa_q1_base_monster_eye(monster, monster->entity->actor.id, &start)) return false;
  if (!qa_q1_base_monster_eye(monster, target.id, &end)) return false;
  qa_q1b_trace_request request = {start, end, POINT_BOUNDS,
                                  qa_q1b_some_actor(monster->entity->actor.id), false, false};
  qa_q1b_trace trace = monster->game->host->trace(monster->game->host->ctx, &request);
  return trace.fraction == 1 && !(trace.in_open && trace.in_water);
}

void qa_q1_base_monster_found(qa_q1_base_monster *monster, qa_actor_id target) {
  qa_q1b_game *game = monster->game;
  monster->state.enemy = qa_q1b_some_actor(target);
  monster->state.mode = "run";
  monster->state.search_until = game->time + 5;
  qa_q1b_monster_mission *mission = game->monster_mission(game, monster->entity->actor.id);
  if (mission != NULL && mission->found_target != NULL) mission->found_target(mission->ctx);
  qa_q1_base_monster_attack_finished(monster, 1);
  if (game->is_player(game, qa_q1b_some_actor(target))) {
    game->sight_entity = monster->entity;
    game->sight_time = game->time;
  }
  qa_q1b_body target_body;
  if (game->host->bodies_read(game->host->ctx, target, &target_body))
    monster->entity->ideal_yaw = qa_q1b_yaw_for(qa_sub3(target_body.origin, qa_q1_base_monster_origin(monster)));
  const char *sound = monster->spec->sight;
  char enforcer_sound[64];
  if (species_is(monster, "enforcer") && edition_is(monster, "classic")) {
    double r = floor(game->host->random(game->host->ctx) * 3 + 0.5);
    int n = r == 1 ? 1 : r == 2 ? 2 : r == 0 ? 3 : 4;
    (void)snprintf(enforcer_sound, sizeof enforcer_sound, "enforcer/sight%d.wav", n);
    sound = enforcer_sound;
  }
  if (strcmp(sound, "") != 0) qa_q1b_game_sound(game, monster->entity, sound);
  copy_name(monster->next_frame, sizeof monster->next_frame, monster->spec->run);
  qa_q1_base_monster_delay(monster, 0.1);
}
bool qa_q1_base_monster_find_target(qa_q1_base_monster *monster) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  qa_q1b_monster_mission *mission = game->monster_mission(game, entity->actor.id);
  bool ambush = mission != NULL ? mission->ambush : ((entity->spawnflags & 3) != 0);
  qa_q1b_opt_actor candidate = QA_Q1B_NO_ACTOR;
  if (game->sight_entity != NULL && game->sight_time >= game->time - 0.1 && !ambush) {
    if (game->sight_entity->monster != NULL) candidate = game->sight_entity->monster->enemy;
  } else {
    candidate = game->host->check_client(game->host->ctx, entity->actor);
  }
  if (!candidate.has || game->health(game, candidate.id) <= 0) return false;
  qa_monster_target_observation observed;
  if (!target_observation(monster, candidate.id, &observed)) return false;
  if (observed.invisible || observed.notarget) return false;
  qa_q1b_body body;
  if (!game->host->bodies_read(game->host->ctx, candidate.id, &body)) return false;
  qa_vec3 delta = qa_sub3(body.origin, qa_q1_base_monster_origin(monster));
  double distance = qa_q1_base_monster_range_distance(monster, candidate);
  if (distance >= 1000 || !qa_q1_base_monster_visible(monster, candidate)) return false;
  bool front = qa_dot3(qa_q1b_normalize(delta), qa_q1_base_monster_make_vectors(monster).forward) > 0.3;
  if (distance >= 500 && !front) return false;
  if (distance >= 120 && distance < 500 &&
      (!observed.has_hostile_until || observed.hostile_until < game->time) && !front) return false;
  qa_q1_base_monster_found(monster, candidate.id);
  return true;
}
qa_q1b_opt_actor qa_q1_base_monster_route(qa_q1_base_monster *monster) {
  qa_q1b_monster_mission *mission = monster->game->monster_mission(monster->game, monster->entity->actor.id);
  if (mission == NULL) {
    if (monster->state.path[0] == '\0') return QA_Q1B_NO_ACTOR;
    qa_q1b_actor *found[1];
    size_t count = monster->game->find(monster->game, monster->state.path, found, 1);
    if (count == 0) return QA_Q1B_NO_ACTOR;
    return qa_q1b_some_actor(found[0]->actor.id);
  }
  return mission->route != NULL ? mission->route(mission->ctx) : QA_Q1B_NO_ACTOR;
}
void qa_q1_base_monster_update_route(qa_q1_base_monster *monster) {
  qa_q1b_opt_actor goal = qa_q1_base_monster_route(monster);
  if (!goal.has || monster->state.pause_until > monster->game->time) {
    monster->state.mode = "stand";
    qa_q1_base_monster_play(monster, monster->spec->stand);
    return;
  }
  if (strcmp(monster->state.mode, "stand") == 0) {
    monster->state.mode = "walk";
    qa_q1_base_monster_play(monster, monster->spec->walk);
  }
}
void qa_q1_base_monster_ai(qa_q1_base_monster *monster, qa_q1_monster_ai mode, double distance) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  if (mode == QA_Q1_AI_STAND || mode == QA_Q1_AI_WALK || mode == QA_Q1_AI_RUN)
    monster->state.mode = qa_q1_monster_ai_name(mode);
  switch (mode) {
    case QA_Q1_AI_STAND:
      if (!qa_q1_base_monster_find_target(monster) && game->time > monster->state.pause_until &&
          qa_q1_base_monster_route(monster).has && strcmp(monster->spec->walk, monster->spec->stand) != 0)
        qa_q1_base_monster_play(monster, monster->spec->walk);
      return;
    case QA_Q1_AI_TURN:
      if (!qa_q1_base_monster_find_target(monster)) qa_q1_base_monster_face(monster);
      return;
    case QA_Q1_AI_WALK: {
      if (qa_q1_base_monster_find_target(monster) || game->time < monster->state.pause_until) return;
      qa_q1b_opt_actor path = qa_q1_base_monster_route(monster);
      if (!path.has) {
        monster->state.pause_until = qa_q1b_path_end_time(game->time);
        qa_q1_base_monster_play(monster, monster->spec->stand);
        return;
      }
      game->host->move_to_goal(game->host->ctx, entity->actor, path.id, distance, NULL);
      return;
    }
    case QA_Q1_AI_RUN: qa_q1_base_monster_run(monster, distance); return;
    case QA_Q1_AI_FACE: qa_q1_base_monster_face(monster); return;
    case QA_Q1_AI_CHARGE:
      qa_q1_base_monster_face(monster);
      if (monster->state.enemy.has)
        game->host->move_to_goal(game->host->ctx, entity->actor, monster->state.enemy.id, distance, NULL);
      return;
    case QA_Q1_AI_CHARGE_SIDE: {
      qa_q1_base_monster_face(monster);
      qa_vec3 target;
      if (qa_q1_base_monster_target(monster, &target)) {
        qa_vec3 side = qa_sub3(target, qa_scale3(qa_q1_base_monster_make_vectors(monster).right, 30));
        game->host->walk_move(game->host->ctx, entity->actor,
                              qa_q1b_yaw_for(qa_sub3(side, qa_q1_base_monster_origin(monster))), 20);
      }
      return;
    }
    case QA_Q1_AI_MELEE_SIDE:
      qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE_SIDE, 0);
      (void)qa_q1_base_monster_melee(monster, 60, 3, 3, true);
      return;
    case QA_Q1_AI_MELEE:
      (void)qa_q1_base_monster_melee(monster, 60, 3, 3, false);
      return;
    case QA_Q1_AI_PAIN:
      game->host->walk_move(game->host->ctx, entity->actor,
                            game->body(game, entity).angles.y + 180, distance);
      return;
    case QA_Q1_AI_PAINFORWARD:
    case QA_Q1_AI_FORWARD:
      game->host->walk_move(game->host->ctx, entity->actor,
                            game->body(game, entity).angles.y, distance);
      return;
  }
}

void qa_q1_base_monster_run(qa_q1_base_monster *monster, double distance) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  if (!monster->state.enemy.has || !target_eligible(monster, monster->state.enemy.id)) {
    entity->attack_state = "straight";
    if (monster->state.old_enemy.has && target_eligible(monster, monster->state.old_enemy.id)) {
      monster->state.enemy = monster->state.old_enemy;
      monster->state.old_enemy = QA_Q1B_NO_ACTOR;
    } else {
      monster->state.enemy = QA_Q1B_NO_ACTOR;
      monster->state.old_enemy = QA_Q1B_NO_ACTOR;
      qa_q1_base_monster_play(monster, qa_q1_base_monster_route(monster).has ? monster->spec->walk : monster->spec->stand);
      return;
    }
  }
  if (!monster->state.enemy.has) return;
  qa_q1b_monster_mission *mission = game->monster_mission(game, entity->actor.id);
  qa_q1b_combat_route combat_route = {0};
  if (mission != NULL && mission->combat_route != NULL) combat_route = mission->combat_route(mission->ctx);
  if (combat_route.has && combat_route.goal.has) {
    game->host->move_to_goal(game->host->ctx, entity->actor, combat_route.goal.id, distance, "contact");
    return;
  }
  bool seen = qa_q1_base_monster_visible(monster, monster->state.enemy);
  if (game->world != NULL) qa_q1b_fields_set(game->world, "enemy_visible", seen ? "1" : "0");
  if (seen) monster->state.search_until = game->time + 5;
  if (qa_q1_base_monster_search_for_coop_target(monster)) return;
  qa_q1_base_monster_update_run_knowledge(monster);
  if (strcmp(entity->attack_state, "straight") != 0) {
    qa_q1_base_monster_face(monster);
    double delta = fmod(game->body(game, entity).angles.y - entity->ideal_yaw + 360, 360);
    if (delta <= 45 || delta >= 315) {
      const char *attack = entity->attack_state;
      if (strcmp(attack, "melee") == 0) qa_q1_base_monster_melee_attack(monster);
      else if (monster->spec->missile != NULL) qa_q1_base_monster_play(monster, monster->spec->missile);
      entity->attack_state = "straight";
    }
    return;
  }
  if (seen && qa_q1_base_monster_try_attack(monster)) return;
  if (combat_route.has && combat_route.stand_ground) return;
  if (monster->sliding) {
    qa_q1_base_monster_face(monster);
    double direction = entity->ideal_yaw + (monster->lefty ? 90 : -90);
    if (!game->host->walk_move(game->host->ctx, entity->actor, direction, distance)) {
      monster->lefty = !monster->lefty;
      game->host->walk_move(game->host->ctx, entity->actor, direction + 180, distance);
    }
  } else {
    qa_q1_base_monster_move_to_enemy(monster, distance);
  }
}
void qa_q1_base_monster_update_run_knowledge(qa_q1_base_monster *monster) {
  qa_vec3 target = ZERO3;
  (void)qa_q1_base_monster_target(monster, &target);
  qa_vec3 delta = qa_sub3(target, qa_q1_base_monster_origin(monster));
  bool front = qa_dot3(qa_q1b_normalize(delta), qa_q1_base_monster_make_vectors(monster).forward) > 0.3;
  if (monster->game->world != NULL)
    qa_q1b_fields_set(monster->game->world, "enemy_infront", front ? "1" : "0");
  double distance = qa_q1_base_monster_range_distance(monster, QA_Q1B_NO_ACTOR);
  char buffer[64];
  qa_q1b_format_number(buffer, sizeof buffer, distance < 120 ? 0 : distance < 500 ? 1 : distance < 1000 ? 2 : 3);
  if (monster->game->world != NULL) qa_q1b_fields_set(monster->game->world, "enemy_range", buffer);
  qa_q1b_format_fround(buffer, sizeof buffer, qa_q1b_yaw_for(delta));
  if (monster->game->world != NULL) qa_q1b_fields_set(monster->game->world, "enemy_yaw", buffer);
}
void qa_q1_base_monster_move_to_enemy(qa_q1_base_monster *monster, double distance) {
  if (monster->state.enemy.has)
    monster->game->host->move_to_goal(monster->game->host->ctx, monster->entity->actor,
                                       monster->state.enemy.id, distance, NULL);
}
bool qa_q1_base_monster_search_for_coop_target(qa_q1_base_monster *monster) {
  return monster->game->options.coop && monster->state.search_until < monster->game->time &&
         qa_q1_base_monster_find_target(monster);
}
void qa_q1_base_monster_attack_finished(qa_q1_base_monster *monster, double seconds) {
  monster->state.refired = false;
  if (edition_is(monster, "rerelease") || monster->game->options.skill != 3)
    monster->state.attack_finished = monster->game->time + seconds;
}
static bool clear_shot(qa_q1_base_monster *monster, qa_actor_id enemy) {
  qa_vec3 start, end;
  if (!qa_q1_base_monster_eye(monster, monster->entity->actor.id, &start)) return false;
  if (!qa_q1_base_monster_eye(monster, enemy, &end)) return false;
  qa_q1b_trace_request request = {start, end, POINT_BOUNDS,
                                  qa_q1b_some_actor(monster->entity->actor.id), true, false};
  qa_q1b_trace trace = monster->game->host->trace(monster->game->host->ctx, &request);
  return trace.actor.has && qa_same_actor(trace.actor.id, enemy) &&
         (species_is(monster, "wizard") || !(trace.in_open && trace.in_water));
}
static void wizard_move(qa_q1_base_monster *monster, bool sliding) {
  if (monster->sliding == sliding) return;
  monster->sliding = sliding;
  qa_q1_base_monster_play(monster, sliding ? "wiz_side1" : "wiz_run1");
}
bool qa_q1_base_monster_try_attack(qa_q1_base_monster *monster) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  const qa_q1_base_monster_species *spec = monster->spec;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target) || !monster->state.enemy.has) return false;
  qa_actor_id enemy = monster->state.enemy.id;
  double distance = qa_q1_base_monster_range_distance(monster, QA_Q1B_NO_ACTOR);
  if (species_is(monster, "demon")) {
    if (distance < 120) { entity->attack_state = "melee"; return true; }
    qa_q1b_body body = game->body(game, entity);
    qa_q1b_body other;
    if (!game->host->bodies_read(game->host->ctx, enemy, &other)) return false;
    qa_vec3 delta = qa_sub3(target, body.origin);
    double horizontal = hypot(delta.x, delta.y);
    double height = other.bounds.max.z - other.bounds.min.z;
    if (body.origin.z + body.bounds.min.z > target.z + other.bounds.min.z + height * 0.75 ||
        body.origin.z + body.bounds.max.z < target.z + other.bounds.min.z + height * 0.25 ||
        horizontal < 100 || (horizontal > 200 && game->host->random(game->host->ctx) < 0.9)) return false;
    qa_q1b_game_sound(game, entity, "demon/djump.wav");
    entity->attack_state = "missile";
    return true;
  }
  bool specialized = strcmp(entity->classname, "monster_ogre") == 0 || species_is(monster, "shambler");
  if (!specialized && !species_is(monster, "wizard") && !clear_shot(monster, enemy)) return false;
  if (distance < 120 && spec->melee && (!specialized || game->can_damage(game, enemy, entity->actor.id))) {
    if (specialized) entity->attack_state = "melee";
    else qa_q1_base_monster_melee_attack(monster);
    return true;
  }
  if (spec->missile == NULL || game->time < monster->state.attack_finished) return false;
  if (distance >= 1000 || (species_is(monster, "shambler") && distance > 600)) {
    if (species_is(monster, "wizard")) wizard_move(monster, false);
    return false;
  }
  if ((specialized || species_is(monster, "wizard")) && !clear_shot(monster, enemy)) {
    if (species_is(monster, "wizard")) wizard_move(monster, false);
    return false;
  }
  if (specialized) {
    qa_q1_base_monster_attack_finished(monster, (species_is(monster, "shambler") ? 2 : 1) + 2 * game->host->random(game->host->ctx));
    entity->attack_state = "missile";
    return true;
  }
  if (distance < 120 && !species_is(monster, "wizard")) monster->state.attack_finished = 0;
  double chance = distance < 120 ? 0.9
                  : distance < 500 ? (species_is(monster, "wizard") ? 0.6 : spec->melee ? 0.2 : 0.4)
                                   : (species_is(monster, "wizard") ? 0.2 : spec->melee ? 0.05 : 0.1);
  if (game->host->random(game->host->ctx) >= chance) {
    if (species_is(monster, "wizard")) wizard_move(monster, distance < 500);
    return false;
  }
  if (species_is(monster, "wizard")) {
    monster->sliding = false;
    entity->attack_state = "missile";
    return true;
  }
  if (species_is(monster, "zombie")) {
    double r = game->host->random(game->host->ctx);
    qa_q1_base_monster_play(monster, r < 0.3 ? "zombie_atta1" : r < 0.6 ? "zombie_attb1" : "zombie_attc1");
  } else {
    qa_q1_base_monster_play(monster, spec->missile);
  }
  qa_q1_base_monster_attack_finished(monster, 2 * game->host->random(game->host->ctx));
  return true;
}
void qa_q1_base_monster_melee_attack(qa_q1_base_monster *monster) {
  qa_q1b_game *game = monster->game;
  if (species_is(monster, "knight")) {
    qa_q1_base_monster_play(monster, qa_q1_base_monster_range_distance(monster, QA_Q1B_NO_ACTOR) < 80 ? "knight_atk1" : "knight_runatk1");
  } else if (species_is(monster, "demon")) {
    qa_q1_base_monster_play(monster, "demon1_atta1");
  } else if (species_is(monster, "ogre")) {
    qa_q1_base_monster_play(monster, game->host->random(game->host->ctx) > 0.5 ? "ogre_smash1" : "ogre_swing1");
  } else if (species_is(monster, "hellknight")) {
    qa_q1b_game_sound_ch(game, monster->entity, "hknight/slash1.wav", qa_q1b_channel_named("weapon"));
    qa_q1_base_monster_play(monster, monster->services.next_hell_knight_melee(monster->services.ctx));
  } else if (species_is(monster, "shambler")) {
    double chance = game->host->random(game->host->ctx);
    qa_q1_base_monster_play(monster, chance > 0.6 || game->health(game, monster->entity->actor.id) == 600
                                         ? "sham_smash1" : chance > 0.3 ? "sham_swingr1" : "sham_swingl1");
  } else if (species_is(monster, "tarbaby")) {
    qa_q1_base_monster_play(monster, "tbaby_jump1");
  } else if (species_is(monster, "fish")) {
    qa_q1_base_monster_play(monster, "f_attack1");
  }
}
double qa_q1_base_monster_melee(qa_q1_base_monster *monster, double range, double scale, double rolls, bool sight) {
  if (!monster->state.enemy.has || qa_q1_base_monster_distance(monster) > range ||
      (sight && !monster->game->can_damage(monster->game, monster->state.enemy.id, monster->entity->actor.id)))
    return 0;
  double damage = 0;
  for (double i = 0; i < rolls; i++)
    damage = FR(damage + monster->game->host->random(monster->game->host->ctx));
  damage = FR(damage * scale);
  qa_q1b_game_damage(monster->game, monster->state.enemy.id, monster->entity->actor.id,
                     qa_q1b_some_actor(monster->entity->actor.id), damage);
  return damage;
}
void qa_q1_base_monster_retaliate(qa_q1_base_monster *monster, qa_q1b_opt_actor attacker) {
  qa_q1b_game *game = monster->game;
  if (!attacker.has || qa_same_actor(attacker.id, monster->entity->actor.id) ||
      strcmp(game->host->classname(game->host->ctx, attacker.id), monster->entity->classname) == 0) return;
  if (game->world != NULL && qa_same_actor(attacker.id, game->world->actor.id)) return;
  if (monster->state.enemy.has && qa_same_actor(attacker.id, monster->state.enemy.id)) return;
  if (monster->state.enemy.has && game->is_player(game, monster->state.enemy))
    monster->state.old_enemy = monster->state.enemy;
  qa_q1_base_monster_found(monster, attacker.id);
}

void qa_q1_base_monster_pain(qa_q1_base_monster *monster, qa_q1b_opt_actor attacker, double damage) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  qa_q1b_monster_state *state = &monster->state;
  if ((entity->movement_flags & 32) != 0) qa_q1_base_monster_retaliate(monster, attacker);
  const char *species = monster->spec->species;
  if (strcmp(species, "zombie") == 0) {
    game->host->combat_set_health(game->host->ctx, entity->actor, 60);
    if (damage < 9 || monster->in_pain == 2) return;
    if (damage >= 25) { monster->in_pain = 2; qa_q1_base_monster_play(monster, "zombie_paine1"); return; }
    if (monster->in_pain != 0) { state->pain_finished = game->time + 3; return; }
    if (state->pain_finished > game->time) { monster->in_pain = 2; qa_q1_base_monster_play(monster, "zombie_paine1"); return; }
    monster->in_pain = 1;
    double r = game->host->random(game->host->ctx);
    qa_q1_base_monster_play(monster, r < 0.25 ? "zombie_paina1" : r < 0.5 ? "zombie_painb1" : r < 0.75 ? "zombie_painc1" : "zombie_paind1");
    return;
  }
  if (strcmp(species, "fish") == 0) { qa_q1_base_monster_play(monster, "f_pain1"); return; }
  if (strcmp(species, "wizard") == 0) {
    qa_q1b_game_sound(game, entity, "wizard/wpain.wav");
    if (!(game->host->random(game->host->ctx) * 70 > damage)) qa_q1_base_monster_play(monster, "wiz_pain1");
    return;
  }
  if (strcmp(species, "shambler") == 0) {
    qa_q1b_game_sound(game, entity, "shambler/shurt2.wav");
    if (game->health(game, entity->actor.id) <= 0 || game->host->random(game->host->ctx) * 400 > damage ||
        state->pain_finished > game->time) return;
    state->pain_finished = game->time + 2;
    qa_q1_base_monster_play(monster, "sham_pain1");
    return;
  }
  if (strcmp(species, "demon") == 0) {
    if (entity->touch.has || state->pain_finished > game->time) return;
    state->pain_finished = game->time + 1;
    qa_q1b_game_sound(game, entity, "demon/dpain1.wav");
    if (!(game->host->random(game->host->ctx) * 200 > damage)) qa_q1_base_monster_play(monster, "demon1_pain1");
    return;
  }
  if (strcmp(species, "knight") == 0) {
    if (state->pain_finished > game->time) return;
    double r = game->host->random(game->host->ctx);
    qa_q1b_game_sound(game, entity, "knight/khurt.wav");
    state->pain_finished = game->time + 1;
    qa_q1_base_monster_play(monster, r < 0.85 ? "knight_pain1" : "knight_painb1");
    return;
  }
  if (strcmp(species, "enforcer") == 0) {
    double r = game->host->random(game->host->ctx);
    if (state->pain_finished > game->time) return;
    qa_q1b_game_sound(game, entity, r < 0.5 ? "enforcer/pain1.wav" : "enforcer/pain2.wav");
    state->pain_finished = game->time + (r < 0.7 ? 1 : 2);
    qa_q1_base_monster_play(monster, r < 0.2 ? "enf_paina1" : r < 0.4 ? "enf_painb1" : r < 0.7 ? "enf_painc1" : "enf_paind1");
    return;
  }
  if (strcmp(species, "ogre") == 0) {
    if (state->pain_finished > game->time) return;
    qa_q1b_game_sound(game, entity, "ogre/ogpain1.wav");
    double r = game->host->random(game->host->ctx);
    state->pain_finished = game->time + (r < 0.75 ? 1 : 2);
    qa_q1_base_monster_play(monster, r < 0.25 ? "ogre_pain1" : r < 0.5 ? "ogre_painb1" : r < 0.75 ? "ogre_painc1" : r < 0.88 ? "ogre_paind1" : "ogre_paine1");
    return;
  }
  if (strcmp(species, "hellknight") == 0) {
    if (state->pain_finished > game->time) return;
    qa_q1b_game_sound(game, entity, "hknight/pain1.wav");
    if (game->time - state->pain_finished <= 5 && game->host->random(game->host->ctx) * 30 > damage) return;
    state->pain_finished = game->time + 1;
    qa_q1_base_monster_play(monster, "hknight_pain1");
    return;
  }
  if (strcmp(species, "shalrath") == 0) {
    if (state->pain_finished > game->time) return;
    qa_q1b_game_sound(game, entity, "shalrath/pain.wav");
    state->pain_finished = game->time + 3;
    qa_q1_base_monster_play(monster, "shal_pain1");
    return;
  }
  /* tarbaby / boss / oldone: no pain. */
}
void qa_q1_base_monster_count_kill(qa_q1_base_monster *monster) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  if (monster->counted_death) return;
  monster->counted_death = true;
  qa_q1b_monster_mission *mission = game->monster_mission(game, entity->actor.id);
  if (mission != NULL) {
    if (mission->killed != NULL) mission->killed(mission->ctx, monster->state.enemy);
  } else if (monster->services.count_monster_kill(monster->services.ctx, monster)) {
    game->killed_monsters++;
    qa_q1b_event event = {0};
    event.kind = "monster-killed";
    event.actor = qa_q1b_some_actor(entity->actor.id);
    event.total = game->total_monsters;
    event.found = game->killed_monsters;
    game->host->emit(game->host->ctx, &event);
  }
  if (edition_is(monster, "rerelease") && (entity->movement_flags & 32) != 0 && monster->state.enemy.has &&
      !qa_same_actor(monster->state.enemy.id, entity->actor.id)) {
    qa_q1b_actor *enemy_entity = game->entity(game, monster->state.enemy);
    int32_t enemy_flags = enemy_entity != NULL ? enemy_entity->movement_flags : 0;
    if ((enemy_flags & 32) != 0) {
      qa_q1b_event event = {0};
      event.kind = "achievement"; event.id = "ACH_FRIENDLY_FIRE";
      game->host->emit(game->host->ctx, &event);
    }
  }
  entity->movement_flags &= ~3;
  if (mission == NULL) game->use_targets(game, entity, monster->state.enemy);
}
void qa_q1_base_monster_die(qa_q1_base_monster *monster, qa_q1b_opt_actor attacker) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  const qa_q1_base_monster_species *spec = monster->spec;
  if (monster->counted_death) return;
  monster->state.enemy = attacker;
  if (game->health(game, entity->actor.id) < -99) game->host->combat_set_health(game->host->ctx, entity->actor, -99);
  qa_q1b_actor_set_damageable(game, entity, false);
  entity->touch = (qa_q1b_touch){0};
  if (species_is(monster, "oldone")) { monster->services.finale(monster->services.ctx, monster); return; }
  qa_q1_base_monster_count_kill(monster);
  if (game->health(game, entity->actor.id) < spec->gib_health && spec->head != NULL) {
    qa_q1b_game_sound(game, entity, species_is(monster, "zombie") ? "zombie/z_gib.wav" : "player/udeath.wav");
    qa_vec3 origin = qa_q1_base_monster_origin(monster);
    double health = game->health(game, entity->actor.id);
    for (size_t i = 0; i < spec->gib_count; i++) (void)qa_q1_throw_gib(game, origin, spec->gibs[i], health);
    qa_q1_throw_head(game, entity, spec->head);
    return;
  }
  const char *species = spec->species;
  if (strcmp(species, "knight") == 0) {
    qa_q1b_game_sound(game, entity, "knight/kdeath.wav");
    qa_q1_base_monster_play(monster, game->host->random(game->host->ctx) < 0.5 ? "knight_die1" : "knight_dieb1");
  } else if (strcmp(species, "enforcer") == 0) {
    qa_q1b_game_sound(game, entity, "enforcer/death1.wav");
    qa_q1_base_monster_play(monster, game->host->random(game->host->ctx) > 0.5 ? "enf_die1" : "enf_fdie1");
  } else if (strcmp(species, "demon") == 0) {
    qa_q1_base_monster_play(monster, "demon1_die1");
  } else if (strcmp(species, "ogre") == 0) {
    qa_q1b_game_sound(game, entity, "ogre/ogdth.wav");
    qa_q1_base_monster_play(monster, game->host->random(game->host->ctx) < 0.5 ? "ogre_die1" : "ogre_bdie1");
  } else if (strcmp(species, "hellknight") == 0) {
    qa_q1b_game_sound(game, entity, "hknight/death1.wav");
    qa_q1_base_monster_play(monster, game->host->random(game->host->ctx) > 0.5 ? "hknight_die1" : "hknight_dieb1");
  } else if (strcmp(species, "shambler") == 0) {
    qa_q1b_game_sound(game, entity, "shambler/sdeath.wav");
    qa_q1_base_monster_play(monster, "sham_death1");
  } else if (strcmp(species, "wizard") == 0) {
    entity->movement = "toss"; entity->movement_flags &= ~1;
    qa_q1_base_monster_play(monster, "wiz_death1");
  } else if (strcmp(species, "shalrath") == 0) {
    qa_q1b_game_sound(game, entity, "shalrath/death.wav");
    entity->solid = "none"; game->link(game, entity);
    qa_q1_base_monster_play(monster, "shal_death1");
  } else if (strcmp(species, "tarbaby") == 0) {
    qa_q1_base_monster_play(monster, "tbaby_die1");
  } else if (strcmp(species, "fish") == 0) {
    qa_q1_base_monster_play(monster, "f_death1");
  } else if (strcmp(species, "zombie") == 0) {
    qa_q1b_game_sound(game, entity, "zombie/z_gib.wav");
    qa_vec3 origin = qa_q1_base_monster_origin(monster);
    double health = game->health(game, entity->actor.id);
    for (size_t i = 0; i < spec->gib_count; i++) (void)qa_q1_throw_gib(game, origin, spec->gibs[i], health);
    qa_q1_throw_head(game, entity, "h_zombie");
  } else if (strcmp(species, "boss") == 0) {
    qa_q1_base_monster_play(monster, "boss_death1");
  }
}





void qa_q1_base_monster_spawn(qa_q1_base_monster *monster) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  const qa_q1_base_monster_species *spec = monster->spec;
  if (!monster->has_source && game->uses_id1_precaches) {
    if (game->options.deathmatch != 0) { game->remove(game, entity); return; }
    qa_q1_precache_id1_monster(game, spec->species);
  }
  bool crucified = species_is(monster, "zombie") && (entity->spawnflags & 1) != 0;
  if (!crucified) {
    qa_q1b_monster_mission *mission = game->monster_mission(game, entity->actor.id);
    if (mission == NULL) game->total_monsters++;
    else if (mission->spawned != NULL) mission->spawned(mission->ctx);
  }
  entity->max_health = spec->health;
  game->host->combat_set_health(game->host->ctx, entity->actor, spec->health);
  (void)snprintf(entity->model, sizeof entity->model, "progs/%s.mdl", spec->model);
  entity->solid = "slidebox"; entity->movement = "step"; entity->aimed_damage = true;
  if (spec->kill_string != NULL) qa_q1b_fields_set(entity, "killstring", spec->kill_string);
  double yaw_speed = qa_q1b_actor_number(entity, "yaw_speed");
  entity->yaw_speed = yaw_speed != 0 ? yaw_speed
                      : (strcmp(spec->movement, "fly") == 0 || strcmp(spec->movement, "swim") == 0 ? 10 : 20);
  entity->ideal_yaw = game->body(game, entity).angles.y;
  if (!crucified && strcmp(spec->movement, "boss") != 0)
    entity->movement_flags |= strcmp(spec->movement, "fly") == 0 ? 1 : strcmp(spec->movement, "swim") == 0 ? 2 : 0;
  game->set_bounds(game, entity, spec->bounds);
  char name[128];
  (void)snprintf(name, sizeof name, "%s:monster_pain", prefix_of(monster));
  entity->pain = game->named.pain(game->named.ctx, entity, name);
  (void)snprintf(name, sizeof name, "%s:monster_die", prefix_of(monster));
  entity->die = game->named.die(game->named.ctx, entity, name);
  (void)snprintf(name, sizeof name, "%s:monster_stand", prefix_of(monster));
  entity->path_end = game->named.action(game->named.ctx, entity, name);
  (void)snprintf(name, sizeof name, "%s:monster_use", prefix_of(monster));
  entity->use = game->named.use(game->named.ctx, entity, name);
  if (species_is(monster, "boss")) {
    entity->model[0] = '\0'; entity->solid = "none";
    (void)snprintf(name, sizeof name, "%s:boss_awake", prefix_of(monster));
    entity->use = game->named.use(game->named.ctx, entity, name);
    return;
  }
  if (species_is(monster, "oldone")) {
    qa_q1b_actor_set_damageable(game, entity, true);
    copy_name(monster->next_frame, sizeof monster->next_frame, "old_idle1");
    qa_q1_base_monster_delay(monster, 0.1);
    return;
  }
  if (crucified) { entity->movement = "none"; qa_q1_base_monster_play(monster, "zombie_cruc1"); return; }
  double delay = fmax(0, entity->next_think) + game->host->random(game->host->ctx) * 0.5 - game->time;
  game->schedule(game, entity, delay, named_action(monster, entity, "monster_start"));
}
void qa_q1_base_monster_start(qa_q1_base_monster *monster) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  const qa_q1_base_monster_species *spec = monster->spec;
  if (strcmp(spec->movement, "walk") == 0) {
    qa_vec3 start = qa_add3(qa_q1_base_monster_origin(monster), V3(0, 0, 1));
    qa_q1b_trace_request request = {start, qa_add3(start, V3(0, 0, -256)),
                                    game->body(game, entity).bounds,
                                    qa_q1b_some_actor(entity->actor.id), true, false};
    qa_q1b_trace trace = game->host->trace(game->host->ctx, &request);
    if (trace.fraction < 1 && !trace.all_solid) {
      qa_q1b_body_patch patch = {0};
      patch.has_origin = true; patch.origin = trace.end;
      patch.has_ground = true; patch.ground = trace.actor;
      game->set_body(game, entity, &patch);
      entity->movement_flags |= 512;
    } else {
      qa_q1b_body_patch patch = {0};
      patch.has_origin = true; patch.origin = start;
      game->set_body(game, entity, &patch);
    }
    game->host->walk_move(game->host->ctx, entity->actor, 0, 0);
  }
  qa_q1b_monster_mission *mission = game->monster_mission(game, entity->actor.id);
  if (species_is(monster, "fish") && edition_is(monster, "classic") && mission == NULL) game->total_monsters++;
  entity->movement_flags |= 32;
  qa_q1b_actor_set_damageable(game, entity, true);
  entity->ideal_yaw = game->body(game, entity).angles.y;
  game->link(game, entity);
  if (strcmp(spec->movement, "fly") == 0) game->host->walk_move(game->host->ctx, entity->actor, 0, 0);
  if (mission != NULL) {
    if (mission->started != NULL) mission->started(mission->ctx);
    qa_q1_base_monster_update_route(monster);
  } else {
    qa_q1b_actor *found[1];
    size_t count = game->find(game, monster->state.path, found, 1);
    qa_q1b_actor *path = count > 0 ? found[0] : NULL;
    if (monster->state.path[0] != '\0') {
      if (strcmp(spec->movement, "fly") != 0) {
        qa_vec3 path_origin = path == NULL ? ZERO3 : game->body(game, path).origin;
        entity->ideal_yaw = qa_q1b_yaw_for(qa_sub3(path_origin, qa_q1_base_monster_origin(monster)));
      }
      if (strcmp(spec->movement, "swim") == 0) {
        qa_q1_base_monster_play(monster, spec->walk);
      } else if (path != NULL && strcmp(path->classname, "path_corner") == 0) {
        qa_q1_base_monster_play(monster, spec->walk);
      } else {
        monster->state.pause_until = FR(99999999);
        qa_q1_base_monster_play(monster, spec->stand);
      }
    } else {
      monster->state.pause_until = FR(99999999);
      qa_q1_base_monster_play(monster, spec->stand);
    }
  }
  if (strcmp(spec->movement, "fly") != 0 && entity->think.has) {
    game->schedule(game, entity, entity->next_think - game->time + game->host->random(game->host->ctx) * 0.5, entity->think);
  }
}
void qa_q1_base_monster_use(qa_q1_base_monster *monster, qa_q1b_opt_actor activator) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  qa_q1b_monster_mission *mission = game->monster_mission(game, entity->actor.id);
  if (mission != NULL && mission->use != NULL && mission->use(mission->ctx, activator)) return;
  if (monster->state.enemy.has || game->health(game, entity->actor.id) <= 0 || !activator.has ||
      !game->is_player(game, activator)) return;
  qa_monster_target_observation observed;
  if (!target_observation(monster, activator.id, &observed)) return;
  if (observed.invisible || observed.notarget) return;
  monster->state.enemy = activator;
  game->schedule(game, entity, 0.1, named_action(monster, entity, "monster_found"));
}
void qa_q1_base_monster_awake(qa_q1_base_monster *monster, qa_q1b_opt_actor activator) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  copy_name(entity->model, sizeof entity->model, "progs/boss.mdl");
  entity->solid = "slidebox";
  qa_q1b_actor_set_damageable(game, entity, false);
  game->host->combat_set_health(game->host->ctx, entity->actor, game->options.skill == 0 ? 1 : 3);
  monster->state.enemy = activator;
  qa_q1b_game_effect(game, "lava-splash", qa_q1_base_monster_origin(monster));
  game->link(game, entity);
  qa_q1_base_monster_play(monster, "boss_rise1");
}

typedef struct callback_ctx { qa_q1_monster_lookup_fn lookup; void *lookup_ctx; } callback_ctx;
static void cb_jump_touch(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity, qa_actor_id other,
                          bool has_normal, qa_vec3 normal, const void *surface) {
  (void)game; (void)has_normal; (void)normal; (void)surface;
  callback_ctx *c = ctx;
  qa_q1_monster_jump_touch(c->lookup(c->lookup_ctx, entity), other);
}
static void cb_frame(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity) {
  (void)game;
  callback_ctx *c = ctx;
  qa_q1_base_monster *monster = c->lookup(c->lookup_ctx, entity);
  qa_q1_base_monster_play(monster, monster->next_frame);
}
static void cb_route(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity) {
  (void)game;
  callback_ctx *c = ctx;
  qa_q1_base_monster_update_route(c->lookup(c->lookup_ctx, entity));
}
static void cb_start(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity) {
  (void)game;
  callback_ctx *c = ctx;
  qa_q1_base_monster_start(c->lookup(c->lookup_ctx, entity));
}
static void cb_stand(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity) {
  (void)game;
  callback_ctx *c = ctx;
  qa_q1_base_monster *monster = c->lookup(c->lookup_ctx, entity);
  qa_q1_base_monster_play(monster, monster->spec->stand);
}
static void cb_found(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity) {
  (void)game;
  callback_ctx *c = ctx;
  qa_q1_base_monster *monster = c->lookup(c->lookup_ctx, entity);
  if (monster->state.enemy.has) qa_q1_base_monster_found(monster, monster->state.enemy.id);
}
static void cb_pain(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity, qa_q1b_opt_actor attacker, double damage) {
  (void)game;
  callback_ctx *c = ctx;
  qa_q1_base_monster_pain(c->lookup(c->lookup_ctx, entity), attacker, damage);
}
static void cb_die(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity, qa_q1b_opt_actor attacker) {
  (void)game;
  callback_ctx *c = ctx;
  qa_q1_base_monster_die(c->lookup(c->lookup_ctx, entity), attacker);
}
static void cb_use(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity, qa_q1b_opt_actor other, qa_q1b_opt_actor activator) {
  (void)game; (void)other;
  callback_ctx *c = ctx;
  qa_q1_base_monster_use(c->lookup(c->lookup_ctx, entity), activator);
}
static void cb_awake(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity, qa_q1b_opt_actor other, qa_q1b_opt_actor activator) {
  (void)game; (void)other;
  callback_ctx *c = ctx;
  qa_q1_base_monster_awake(c->lookup(c->lookup_ctx, entity), activator);
}
void qa_q1_register_monster_callbacks(qa_q1b_game *game, const char *prefix,
                                      qa_q1_monster_lookup_fn lookup, void *lookup_ctx) {
  /* Leaked by design: registry outlives this call like the TS closure. */
  callback_ctx *c = malloc(sizeof *c);
  if (c == NULL) qa_range_error("out of memory");
  c->lookup = lookup; c->lookup_ctx = lookup_ctx;
  char name[128];
  qa_q1b_handlers handlers;
  (void)snprintf(name, sizeof name, "%s:monster_jump_touch", prefix);
  handlers = (qa_q1b_handlers){.ctx = c, .touch = cb_jump_touch};
  game->named.register_named(game->named.ctx, name, &handlers);
  (void)snprintf(name, sizeof name, "%s:monster_frame", prefix);
  handlers = (qa_q1b_handlers){.ctx = c, .action = cb_frame};
  game->named.register_named(game->named.ctx, name, &handlers);
  (void)snprintf(name, sizeof name, "%s:monster_route", prefix);
  handlers = (qa_q1b_handlers){.ctx = c, .action = cb_route};
  game->named.register_named(game->named.ctx, name, &handlers);
  (void)snprintf(name, sizeof name, "%s:monster_start", prefix);
  handlers = (qa_q1b_handlers){.ctx = c, .action = cb_start};
  game->named.register_named(game->named.ctx, name, &handlers);
  (void)snprintf(name, sizeof name, "%s:monster_stand", prefix);
  handlers = (qa_q1b_handlers){.ctx = c, .action = cb_stand};
  game->named.register_named(game->named.ctx, name, &handlers);
  (void)snprintf(name, sizeof name, "%s:monster_found", prefix);
  handlers = (qa_q1b_handlers){.ctx = c, .action = cb_found};
  game->named.register_named(game->named.ctx, name, &handlers);
  (void)snprintf(name, sizeof name, "%s:monster_pain", prefix);
  handlers = (qa_q1b_handlers){.ctx = c, .pain = cb_pain};
  game->named.register_named(game->named.ctx, name, &handlers);
  (void)snprintf(name, sizeof name, "%s:monster_die", prefix);
  handlers = (qa_q1b_handlers){.ctx = c, .die = cb_die};
  game->named.register_named(game->named.ctx, name, &handlers);
  (void)snprintf(name, sizeof name, "%s:monster_use", prefix);
  handlers = (qa_q1b_handlers){.ctx = c, .use = cb_use};
  game->named.register_named(game->named.ctx, name, &handlers);
  (void)snprintf(name, sizeof name, "%s:boss_awake", prefix);
  handlers = (qa_q1b_handlers){.ctx = c, .use = cb_awake};
  game->named.register_named(game->named.ctx, name, &handlers);
}
