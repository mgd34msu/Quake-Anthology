/* Port of src/content/q1/base/monsters.ts. GPL-2.0-or-later.
 * TS class BaseMonster: subclasses override through ops (monsters.h);
 * each public qa_q1_base_monster_<m> is the TS virtual call and
 * qa_q1_base_monster_<m>_super the BaseMonster body. */
#include "qa/content/q1/base/monsters.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/monsters/target.h"
#include "qa/content/q1/base/frames.h"
#include "qa/content/q1/base/monster_actions.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/base/grunt.h"
#include "qa/content/q1/foundation/callbacks.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/core/math.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

#define V3(x, y, z) ((qa_vec3){(x), (y), (z)})
#define FR(x) ((double)(float)(x))
static const qa_vec3 ZERO3 = {0, 0, 0};
static const qa_bounds POINT_BOUNDS = {{0, 0, 0}, {0, 0, 0}};

static void copy_name(char *dst, size_t size, const char *src) {
  size_t length = strlen(src);
  if (length >= size) length = size - 1;
  memmove(dst, src, length);
  dst[length] = '\0';
}
static char *dup_text(const char *text) {
  if (text == NULL) text = "";
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, text, length);
  return copy;
}
static void set_text(char **slot, const char *value) {
  char *copy = dup_text(value);
  free(*slot);
  *slot = copy;
}
static bool same(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static const char *prefix_of(const qa_q1_base_monster *monster) {
  return monster->has_source && monster->source.callback_prefix != NULL
             ? monster->source.callback_prefix : "base";
}
static qa_q1_bound_action named_action(qa_q1_base_monster *monster, const char *suffix) {
  char name[128];
  (void)snprintf(name, sizeof name, "%s:%s", prefix_of(monster), suffix);
  return qa_q1_callback_registry_action(monster->game->named, monster->entity, name);
}
static bool species_is(const qa_q1_base_monster *monster, const char *species) {
  return same(monster->spec->species, species);
}
static bool edition_is(const qa_q1_base_monster *monster, const char *edition) {
  return same(monster->game->options.edition, edition);
}
static double host_random(qa_q1_services *game) { return game->host.random(game->host.ctx); }
/* TS game.host.bodies.read(id): null -> false. */
static bool read_body(qa_q1_services *game, qa_actor_id actor, qa_q1_body *out) {
  const qa_body_state_result result = qa_shared_body_table_read(game->host.bodies, actor);
  if (!result.has_value) return false;
  *out = qa_q1_body_from_state(result.value);
  return true;
}
/* TS game.monsterTarget(actor): null -> false. */
static bool observe(qa_q1_services *game, qa_actor_id actor, qa_monster_target_observation *out) {
  const qa_q1_monster_target_result result = qa_q1_monster_target(game, actor);
  if (!result.has_value) return false;
  *out = result.value;
  return true;
}
/* TS game.host.combat.setHealth(actor, health). */
static void set_health(qa_q1_services *game, qa_owned_actor actor, double health) {
  qa_gameplay_authority_set_health(game->host.combat, actor, health);
}
static bool target_eligible(qa_q1_services *game, qa_actor_id actor) {
  qa_monster_target_observation observation;
  const bool has = observe(game, actor, &observation);
  return qa_monster_target_eligible(qa_q1_health(game, actor), has ? &observation : NULL);
}
static void set_enemy_id(qa_q1_monster *state, const qa_actor_id *enemy) {
  state->has_enemy = enemy != NULL;
  if (enemy != NULL) state->enemy = *enemy;
}
static void set_old_enemy(qa_q1_monster *state, const qa_actor_id *enemy) {
  state->has_old_enemy = enemy != NULL;
  if (enemy != NULL) state->old_enemy = *enemy;
}
static bool is_enemy(const qa_q1_base_monster *monster, qa_actor_id actor) {
  return monster->state->has_enemy && qa_same_actor(monster->state->enemy, actor);
}
static void set_number_field(qa_q1_actor *entity, const char *key, double value) {
  char text[40];
  qa_net_js_number(value, text, sizeof text);
  (void)qa_q1_actor_field_set(entity, key, text);
}

/* V1.01 knight.qc lists attackb1 twice and oldone.qc lists shake12 twice,
 * so id1 progs number the later frames one higher than the rerelease table. */
static double classic_frame_shift(const char *name) {
  const char *digits = NULL;
  bool knight = false;
  if (strncmp(name, "knight_atk", 10) == 0) { digits = name + 10; knight = true; }
  else if (strncmp(name, "old_thrash", 10) == 0) digits = name + 10;
  if (digits == NULL || *digits == '\0') return 0;
  for (const char *c = digits; *c != '\0'; c++)
    if (*c < '0' || *c > '9') return 0;
  const double number = atof(digits);
  if (knight) return number >= 2 && number <= 10 ? 1 : 0;
  return number >= 13 && number <= 20 ? 1 : 0;
}

bool qa_q1_base_monster_init(qa_q1_base_monster *monster, qa_q1_services *game, qa_q1_actor *entity,
                             const qa_q1_base_monster_species *spec,
                             qa_q1_monster_services services,
                             const qa_q1_base_monster_source *source) {
  memset(monster, 0, sizeof *monster);
  monster->game = game;
  monster->entity = entity;
  monster->spec = spec;
  monster->services = services;
  if (source != NULL) { monster->has_source = true; monster->source = *source; }
  copy_name(monster->current_frame, sizeof monster->current_frame, spec->stand);
  copy_name(monster->next_frame, sizeof monster->next_frame, spec->stand);
  if (entity->monster == NULL) {
    qa_q1_monster *state = calloc(1, sizeof *state);
    if (state == NULL) return false;
    state->species = spec->species;
    state->mode = "stand";
    state->path = dup_text(entity->target);
    entity->monster = state; /* owned by the actor (qa_q1_actor_free) */
  }
  monster->state = entity->monster;
  return qa_q1_actor_field_set(entity, "source.monsterCallbackPrefix", prefix_of(monster));
}
qa_q1_base_monster *qa_q1_base_monster_create(qa_q1_services *game, qa_q1_actor *entity,
                                              const qa_q1_base_monster_species *spec,
                                              qa_q1_monster_services services,
                                              const qa_q1_base_monster_source *source) {
  qa_q1_base_monster *monster = malloc(sizeof *monster);
  if (monster == NULL) return NULL;
  if (!qa_q1_base_monster_init(monster, game, entity, spec, services, source)) {
    free(monster);
    return NULL;
  }
  return monster;
}
void qa_q1_base_monster_destroy(qa_q1_base_monster *monster) {
  if (monster == NULL) return;
  if (monster->ops != NULL && monster->ops->destroy != NULL) monster->ops->destroy(monster);
  else free(monster);
}

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
  ok = ok && set_owned(object, "countedDeath", qa_save_value_boolean(monster->counted_death));
  if (!ok) { qa_save_value_free(object); return NULL; }
  return object;
}
static void must_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
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
static void field_name(const qa_save_reader *reader, const char *name, char *dst, size_t size) {
  qa_save_reader scratch;
  must_field(reader, name, &scratch);
  copy_name(dst, size, qa_save_reader_string(&scratch));
  qa_save_reader_destroy(&scratch);
}
static bool source_action(const qa_q1_base_monster *monster, const char *name,
                          const qa_q1_monster_action_entry **out) {
  if (!monster->has_source || monster->source.actions == NULL) return false;
  for (size_t i = 0; i < monster->source.action_count; i++)
    if (strcmp(monster->source.actions[i].name, name) == 0) {
      if (out != NULL) *out = &monster->source.actions[i];
      return true;
    }
  return false;
}
static const qa_q1_monster_frame *source_frame(const qa_q1_base_monster *monster, const char *name) {
  return monster->has_source ? qa_q1_monster_frame_table_get(monster->source.frames, name) : NULL;
}
void qa_q1_base_monster_restore(qa_q1_base_monster *monster, const qa_save_reader *reader) {
  field_name(reader, "currentFrame", monster->current_frame, sizeof monster->current_frame);
  field_name(reader, "nextFrame", monster->next_frame, sizeof monster->next_frame);
  const char *names[2] = {monster->current_frame, monster->next_frame};
  for (int i = 0; i < 2; i++) {
    const bool known = qa_q1_monster_frames_has(names[i]) || source_frame(monster, names[i]) != NULL ||
                       source_action(monster, names[i], NULL);
    if (!known) qa_save_reader_fail(reader, "unknown source monster continuation");
  }
  monster->in_pain = field_number(reader, "inPain");
  monster->counter = field_number(reader, "counter");
  monster->idle_until = field_number(reader, "idleUntil");
  monster->lefty = field_boolean(reader, "lefty");
  monster->sliding = field_boolean(reader, "sliding");
  monster->counted_death = field_boolean(reader, "countedDeath");
}
const qa_actor_id *qa_q1_base_monster_enemy(const qa_q1_base_monster *monster) {
  return monster->state->has_enemy ? &monster->state->enemy : NULL;
}
void qa_q1_base_monster_set_enemy(qa_q1_base_monster *monster, const qa_actor_id *enemy) {
  set_enemy_id(monster->state, enemy);
}
qa_vec3 qa_q1_base_monster_origin(const qa_q1_base_monster *monster) {
  return qa_q1_get_body(monster->game, monster->entity).origin;
}
bool qa_q1_base_monster_target(const qa_q1_base_monster *monster, qa_vec3 *out) {
  if (!monster->state->has_enemy) return false;
  qa_q1_body body;
  if (!read_body(monster->game, monster->state->enemy, &body)) return false;
  *out = body.origin;
  return true;
}
double qa_q1_base_monster_distance(const qa_q1_base_monster *monster) {
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target)) return INFINITY;
  return qa_length3(qa_sub3(target, qa_q1_base_monster_origin(monster)));
}

void qa_q1_base_monster_play(qa_q1_base_monster *monster, const char *name) {
  if (monster->ops != NULL && monster->ops->play != NULL) monster->ops->play(monster, name);
  else qa_q1_base_monster_play_super(monster, name);
}
void qa_q1_base_monster_play_super(qa_q1_base_monster *monster, const char *requested) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (!qa_q1_live(game, entity)) return;
  char name[64];
  copy_name(name, sizeof name, requested); /* requested may alias next_frame */
  const qa_q1_monster_frame *frame = source_frame(monster, name);
  if (frame == NULL) frame = qa_q1_monster_frames_get(name);
  if (frame == NULL) qa_range_error("Missing Q1 source animation %s", name);
  copy_name(monster->current_frame, sizeof monster->current_frame, name);
  copy_name(monster->next_frame, sizeof monster->next_frame, frame->next);
  entity->frame = frame->frame + (edition_is(monster, "classic") ? classic_frame_shift(name) : 0);
  qa_q1_schedule(game, entity, 0.1, named_action(monster, "monster_frame"));
  if (edition_is(monster, "classic")) {
    if (strcmp(name, "boss_idle1") == 0 || strcmp(name, "f_death2") == 0) return;
    if (strcmp(name, "f_death21") == 0) { entity->solid = "none"; qa_q1_link(game, entity); return; }
    if (strcmp(name, "sham_magic11") == 0 && qa_q1_option_skill(game) == 3) { qa_q1_cast_lightning(monster); return; }
  }
  for (size_t i = 0; i < frame->operation_count; i++) {
    const qa_q1_monster_operation *op = &frame->operations[i];
    if (!qa_q1_live(game, entity)) return;
    switch (op->kind) {
      case QA_Q1_OP_AI: qa_q1_base_monster_ai(monster, op->mode, op->distance); break;
      case QA_Q1_OP_SOLID: entity->solid = op->solid; qa_q1_link(game, entity); break;
      case QA_Q1_OP_LIGHTSTYLE: {
        qa_q1_event event;
        memset(&event, 0, sizeof event);
        event.kind = "lightstyle";
        event.style = 0;
        event.pattern = op->pattern;
        qa_q1_emit(game, event);
        break;
      }
      case QA_Q1_OP_ACTION: {
        const qa_q1_monster_action_entry *action = NULL;
        if (source_action(monster, op->name, &action))
          action->fn(action->ctx != NULL ? action->ctx : monster->source.actions_ctx, monster);
        else qa_q1_monster_action(monster, op->name);
        break;
      }
      case QA_Q1_OP_SOUND: {
        const char *path = qa_q1_base_monster_sfx(monster, op->path);
        if (strcmp(op->path, "fish/idle.wav") == 0 && game->mg3_pickups) {
          /* MG3 fish.qc: a five to ten second quiet period follows each idle sound. */
          if (qa_q1_actor_number(entity, "waitmin") > game->time) break;
          if (host_random(game) < 0.25) {
            qa_q1_sound(game, entity->actor.id, path, op->channel, op->attenuation, 1);
            const double wait = host_random(game);
            set_number_field(entity, "waitmin", FR(game->time + 5 + wait * 5));
          }
          break;
        }
        const double r = !op->has_chance ? 0 : host_random(game);
        if (!op->has_chance || (same(op->comparison, "greater") ? r > op->chance : r < op->chance))
          qa_q1_sound(game, entity->actor.id, path, op->channel, op->attenuation, 1);
        break;
      }
    }
  }
}
void qa_q1_base_monster_delay(qa_q1_base_monster *monster, double seconds) {
  qa_q1_schedule(monster->game, monster->entity, seconds, named_action(monster, "monster_frame"));
}
void qa_q1_base_monster_change_yaw(qa_q1_base_monster *monster) {
  monster->game->host.change_yaw(monster->game->host.ctx, monster->entity->actor);
}
void qa_q1_base_monster_face(qa_q1_base_monster *monster) {
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target)) return;
  const qa_q1_body body = qa_q1_get_body(monster->game, monster->entity);
  monster->entity->ideal_yaw = qa_q1_yaw_for(qa_sub3(target, body.origin));
  qa_q1_base_monster_change_yaw(monster);
}
qa_q1_basis qa_q1_base_monster_make_vectors(qa_q1_base_monster *monster) {
  if (monster->ops != NULL && monster->ops->make_vectors != NULL) return monster->ops->make_vectors(monster);
  return qa_q1_base_monster_make_vectors_super(monster);
}
qa_q1_basis qa_q1_base_monster_make_vectors_super(qa_q1_base_monster *monster) {
  qa_vec3 angles = qa_q1_get_body(monster->game, monster->entity).angles;
  if (edition_is(monster, "rerelease")) angles.x = -angles.x;
  return qa_q1_make_vectors(monster->game, angles);
}
bool qa_q1_base_monster_eye(qa_q1_base_monster *monster, qa_actor_id target, qa_vec3 *out) {
  qa_q1_services *game = monster->game;
  qa_q1_body body;
  if (!read_body(game, target, &body)) return false;
  qa_q1_actor *entity = qa_q1_get_entity(game, &target);
  qa_vec3 offset;
  if (entity != NULL && qa_q1_actor_field_get(entity, "view_ofs") != NULL) {
    offset = qa_q1_actor_vector(entity, "view_ofs");
  } else {
    double height;
    if (entity != NULL && entity->monster != NULL && same(entity->monster->species, "fish")) {
      height = 10;
    } else {
      qa_monster_target_observation observation;
      height = observe(game, target, &observation) ? observation.view_height : 25;
    }
    offset = V3(0, 0, height);
  }
  *out = qa_add3(body.origin, offset);
  return true;
}
static double range_to(qa_q1_base_monster *monster, const qa_actor_id *target) {
  qa_vec3 start, end;
  if (!qa_q1_base_monster_eye(monster, monster->entity->actor.id, &start)) return INFINITY;
  if (target == NULL || !qa_q1_base_monster_eye(monster, *target, &end)) return INFINITY;
  return qa_length3(qa_sub3(end, start));
}
double qa_q1_base_monster_range_distance(qa_q1_base_monster *monster, const qa_actor_id *target) {
  return range_to(monster, target);
}
bool qa_q1_base_monster_visible(qa_q1_base_monster *monster, const qa_actor_id *target) {
  if (target == NULL) return false;
  const qa_actor_id goal = *target;
  qa_vec3 start, end;
  if (!qa_q1_base_monster_eye(monster, monster->entity->actor.id, &start) ||
      !qa_q1_base_monster_eye(monster, goal, &end)) return false;
  qa_q1_trace_request request;
  memset(&request, 0, sizeof request);
  request.start = start;
  request.end = end;
  request.bounds = POINT_BOUNDS;
  request.has_ignore = true;
  request.ignore = monster->entity->actor.id;
  request.monsters = false;
  const qa_q1_trace trace = qa_q1_host_trace(monster->game, request);
  return trace.fraction == 1 && !(trace.in_open && trace.in_water);
}
/* TS default parameter target = this.enemy. */
static double range_enemy(qa_q1_base_monster *monster) {
  if (!monster->state->has_enemy) return range_to(monster, NULL);
  const qa_actor_id copy = monster->state->enemy;
  return range_to(monster, &copy);
}
static bool visible_enemy(qa_q1_base_monster *monster) {
  if (!monster->state->has_enemy) return false;
  const qa_actor_id copy = monster->state->enemy;
  return qa_q1_base_monster_visible(monster, &copy);
}

void qa_q1_base_monster_found(qa_q1_base_monster *monster, qa_actor_id target) {
  if (monster->ops != NULL && monster->ops->found != NULL) monster->ops->found(monster, target);
  else qa_q1_base_monster_found_super(monster, target);
}
void qa_q1_base_monster_found_super(qa_q1_base_monster *monster, qa_actor_id target) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  set_enemy_id(monster->state, &target);
  monster->state->mode = "run";
  monster->state->search_until = game->time + 5;
  const qa_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
  if (mission != NULL) mission->found_target(mission->ctx);
  qa_q1_base_monster_attack_finished(monster, 1);
  if (qa_q1_is_player(game, &target)) { game->sight_entity = entity; game->sight_time = game->time; }
  qa_q1_body target_body;
  if (read_body(game, target, &target_body))
    entity->ideal_yaw = qa_q1_yaw_for(qa_sub3(target_body.origin, qa_q1_base_monster_origin(monster)));
  const char *sound = monster->spec->sight;
  char chosen[32];
  if (species_is(monster, "enforcer") && edition_is(monster, "classic")) {
    const double r = floor(host_random(game) * 3 + 0.5);
    (void)snprintf(chosen, sizeof chosen, "enforcer/sight%d.wav", r == 1 ? 1 : r == 2 ? 2 : r == 0 ? 3 : 4);
    sound = chosen;
  }
  if (sound[0] != '\0') qa_q1_sound_defaults(game, entity->actor.id, sound);
  copy_name(monster->next_frame, sizeof monster->next_frame, monster->spec->run);
  qa_q1_base_monster_delay(monster, 0.1);
}
bool qa_q1_base_monster_find_target(qa_q1_base_monster *monster) {
  if (monster->ops != NULL && monster->ops->find_target != NULL) return monster->ops->find_target(monster);
  return qa_q1_base_monster_find_target_super(monster);
}
bool qa_q1_base_monster_find_target_super(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  bool has_candidate = false;
  qa_actor_id candidate = {0};
  const qa_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
  const bool ambush = mission != NULL ? mission->ambush : (qa_js_to_int32(entity->spawnflags) & 3) != 0;
  if (game->sight_entity != NULL && game->sight_time >= game->time - 0.1 && !ambush) {
    const qa_q1_monster *sighted = game->sight_entity->monster;
    const bool has_sighted = sighted != NULL && sighted->has_enemy;
    if (!has_sighted ? !monster->state->has_enemy
                     : monster->state->has_enemy && qa_same_actor(sighted->enemy, monster->state->enemy))
      return true;
    candidate = game->sight_entity->actor.id;
    has_candidate = true;
  } else if (game->host.check_client != NULL) {
    has_candidate = game->host.check_client(game->host.ctx, entity->actor, &candidate);
  }
  if (!has_candidate || is_enemy(monster, candidate) || qa_q1_health(game, candidate) <= 0) return false;
  qa_monster_target_observation observed;
  if (!observe(game, candidate, &observed) || observed.invisible || observed.notarget) return false;
  qa_q1_body body;
  if (!read_body(game, candidate, &body)) return false;
  const qa_vec3 delta = qa_sub3(body.origin, qa_q1_base_monster_origin(monster));
  const double distance = range_to(monster, &candidate);
  if (distance >= 1000 || !qa_q1_base_monster_visible(monster, &candidate)) return false;
  const bool front = qa_dot3(qa_q1_normalize(delta), qa_q1_base_monster_make_vectors(monster).forward) > 0.3;
  if ((distance >= 500 && !front) ||
      (distance >= 120 && distance < 500 &&
       (!observed.has_hostile_until || observed.hostile_until < game->time) && !front))
    return false;
  if (!qa_q1_is_player(game, &candidate)) {
    qa_q1_actor *other = qa_q1_get_entity(game, &candidate);
    if (other == NULL || other->monster == NULL || !other->monster->has_enemy) return false;
    candidate = other->monster->enemy;
    if (!qa_q1_is_player(game, &candidate)) return false;
  }
  qa_q1_base_monster_found(monster, candidate);
  return true;
}
const qa_actor_id *qa_q1_base_monster_route(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  const qa_monster_mission *mission = qa_q1_mission(game, monster->entity->actor.id);
  if (mission != NULL) return mission->route(mission->ctx);
  if (monster->state->path == NULL || monster->state->path[0] == '\0') return NULL;
  qa_q1_actor *found[1];
  if (qa_q1_find(game, monster->state->path, found, 1) == 0) return NULL;
  return &found[0]->actor.id;
}
void qa_q1_base_monster_update_route(qa_q1_base_monster *monster) {
  const qa_actor_id *goal = qa_q1_base_monster_route(monster);
  if (goal == NULL || monster->state->pause_until > monster->game->time) {
    monster->state->mode = "stand";
    qa_q1_base_monster_play(monster, monster->spec->stand);
    return;
  }
  if (same(monster->state->mode, "stand")) {
    monster->state->mode = "walk";
    qa_q1_base_monster_play(monster, monster->spec->walk);
  }
}
void qa_q1_base_monster_ai(qa_q1_base_monster *monster, qa_q1_monster_ai mode, double distance) {
  if (monster->ops != NULL && monster->ops->ai != NULL) monster->ops->ai(monster, mode, distance);
  else qa_q1_base_monster_ai_super(monster, mode, distance);
}
void qa_q1_base_monster_ai_super(qa_q1_base_monster *monster, qa_q1_monster_ai mode, double distance) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (mode == QA_Q1_AI_STAND || mode == QA_Q1_AI_WALK || mode == QA_Q1_AI_RUN)
    monster->state->mode = qa_q1_monster_ai_name(mode);
  switch (mode) {
    case QA_Q1_AI_STAND:
      if (!qa_q1_base_monster_find_target(monster) && game->time > monster->state->pause_until &&
          qa_q1_base_monster_route(monster) != NULL && !same(monster->spec->walk, monster->spec->stand))
        qa_q1_base_monster_play(monster, monster->spec->walk);
      return;
    case QA_Q1_AI_TURN:
      if (!qa_q1_base_monster_find_target(monster)) qa_q1_base_monster_face(monster);
      return;
    case QA_Q1_AI_WALK: {
      if (qa_q1_base_monster_find_target(monster) || game->time < monster->state->pause_until) return;
      const qa_actor_id *path = qa_q1_base_monster_route(monster);
      if (path == NULL) {
        monster->state->pause_until = qa_q1_path_end_time(game->time);
        qa_q1_base_monster_play(monster, monster->spec->stand);
        return;
      }
      const qa_actor_id goal = *path;
      game->host.move_to_goal(game->host.ctx, entity->actor, goal, distance, NULL);
      return;
    }
    case QA_Q1_AI_RUN: qa_q1_base_monster_run(monster, distance); return;
    case QA_Q1_AI_FACE: qa_q1_base_monster_face(monster); return;
    case QA_Q1_AI_CHARGE:
      qa_q1_base_monster_face(monster);
      if (monster->state->has_enemy)
        game->host.move_to_goal(game->host.ctx, entity->actor, monster->state->enemy, distance, NULL);
      return;
    case QA_Q1_AI_CHARGE_SIDE: {
      qa_q1_base_monster_face(monster);
      qa_vec3 target;
      if (qa_q1_base_monster_target(monster, &target)) {
        const qa_vec3 right = qa_q1_base_monster_make_vectors(monster).right;
        const double yaw = qa_q1_yaw_for(qa_sub3(qa_sub3(target, qa_scale3(right, 30)), qa_q1_base_monster_origin(monster)));
        (void)game->host.walk_move(game->host.ctx, entity->actor, yaw, 20);
      }
      return;
    }
    case QA_Q1_AI_MELEE_SIDE:
      qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE_SIDE, 0);
      (void)qa_q1_base_monster_melee(monster, 60, 3, 3, true);
      return;
    case QA_Q1_AI_MELEE: (void)qa_q1_base_monster_melee(monster, 60, 3, 3, false); return;
    case QA_Q1_AI_PAIN:
      (void)game->host.walk_move(game->host.ctx, entity->actor, qa_q1_get_body(game, entity).angles.y + 180, distance);
      return;
    case QA_Q1_AI_PAINFORWARD:
      (void)game->host.walk_move(game->host.ctx, entity->actor, entity->ideal_yaw, distance);
      return;
    case QA_Q1_AI_FORWARD:
      (void)game->host.walk_move(game->host.ctx, entity->actor, qa_q1_get_body(game, entity).angles.y, distance);
      return;
  }
}
static void world_field(qa_q1_services *game, const char *key, const char *value) {
  if (game->world != NULL) (void)qa_q1_actor_field_set(game->world, key, value);
}
void qa_q1_base_monster_run(qa_q1_base_monster *monster, double distance) {
  if (monster->ops != NULL && monster->ops->run != NULL) monster->ops->run(monster, distance);
  else qa_q1_base_monster_run_super(monster, distance);
}
void qa_q1_base_monster_run_super(qa_q1_base_monster *monster, double distance) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_monster *state = monster->state;
  if (!state->has_enemy || !target_eligible(game, state->enemy)) {
    entity->attack_state = "straight";
    if (state->has_old_enemy && target_eligible(game, state->old_enemy)) {
      const qa_actor_id old = state->old_enemy;
      set_enemy_id(state, &old);
      set_old_enemy(state, NULL);
      qa_q1_base_monster_hunt_target(monster);
    } else {
      set_enemy_id(state, NULL);
      set_old_enemy(state, NULL);
      qa_q1_base_monster_play(monster, qa_q1_base_monster_route(monster) == NULL ? monster->spec->stand : monster->spec->walk);
      return;
    }
  }
  if (!state->has_enemy) return;
  const qa_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
  bool has_route = false;
  qa_monster_combat_route route = {0};
  if (mission != NULL) { route = mission->combat_route(mission->ctx); has_route = true; }
  if (has_route && route.goal != NULL) {
    const qa_actor_id goal = *route.goal;
    game->host.move_to_goal(game->host.ctx, entity->actor, goal, distance, "contact");
    return;
  }
  const bool seen = visible_enemy(monster);
  world_field(game, "enemy_visible", seen ? "1" : "0");
  if (qa_q1_base_monster_search_for_coop_target(monster)) return;
  qa_q1_base_monster_update_run_knowledge(monster);
  if (!same(entity->attack_state, "straight")) {
    qa_q1_base_monster_face(monster);
    const double delta = fmod(qa_q1_get_body(game, entity).angles.y - entity->ideal_yaw + 360, 360);
    if (delta <= 45 || delta >= 315) {
      const char *attack = entity->attack_state;
      if (same(attack, "melee")) qa_q1_base_monster_melee_attack(monster);
      else if (monster->spec->missile != NULL) qa_q1_base_monster_play(monster, monster->spec->missile);
      entity->attack_state = "straight";
    }
    return;
  }
  if (seen && qa_q1_base_monster_try_attack(monster)) return;
  if (has_route && route.stand_ground) return;
  if (monster->sliding) {
    qa_q1_base_monster_face(monster);
    const double direction = entity->ideal_yaw + (monster->lefty ? 90 : -90);
    if (!game->host.walk_move(game->host.ctx, entity->actor, direction, distance)) {
      monster->lefty = !monster->lefty;
      (void)game->host.walk_move(game->host.ctx, entity->actor, direction + 180, distance);
    }
  } else {
    qa_q1_base_monster_move_to_enemy(monster, distance);
  }
}
void qa_q1_base_monster_update_run_knowledge(qa_q1_base_monster *monster) {
  if (monster->ops != NULL && monster->ops->update_run_knowledge != NULL) monster->ops->update_run_knowledge(monster);
  else qa_q1_base_monster_update_run_knowledge_super(monster);
}
void qa_q1_base_monster_update_run_knowledge_super(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_vec3 target = ZERO3;
  (void)qa_q1_base_monster_target(monster, &target);
  const qa_vec3 delta = qa_sub3(target, qa_q1_base_monster_origin(monster));
  const bool front = qa_dot3(qa_q1_normalize(delta), qa_q1_base_monster_make_vectors(monster).forward) > 0.3;
  world_field(game, "enemy_infront", front ? "1" : "0");
  const double distance = range_enemy(monster);
  world_field(game, "enemy_range", distance < 120 ? "0" : distance < 500 ? "1" : distance < 1000 ? "2" : "3");
  if (game->world != NULL) set_number_field(game->world, "enemy_yaw", FR(qa_q1_yaw_for(delta)));
}
void qa_q1_base_monster_move_to_enemy(qa_q1_base_monster *monster, double distance) {
  if (monster->ops != NULL && monster->ops->move_to_enemy != NULL) monster->ops->move_to_enemy(monster, distance);
  else qa_q1_base_monster_move_to_enemy_super(monster, distance);
}
void qa_q1_base_monster_move_to_enemy_super(qa_q1_base_monster *monster, double distance) {
  qa_q1_services *game = monster->game;
  if (monster->state->has_enemy)
    game->host.move_to_goal(game->host.ctx, monster->entity->actor, monster->state->enemy, distance, NULL);
}
void qa_q1_base_monster_hunt_target(qa_q1_base_monster *monster) {
  if (monster->ops != NULL && monster->ops->hunt_target != NULL) monster->ops->hunt_target(monster);
  else qa_q1_base_monster_hunt_target_super(monster);
}
void qa_q1_base_monster_hunt_target_super(qa_q1_base_monster *monster) {
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target)) return;
  monster->state->mode = "run";
  monster->entity->ideal_yaw = qa_q1_yaw_for(qa_sub3(target, qa_q1_base_monster_origin(monster)));
  copy_name(monster->next_frame, sizeof monster->next_frame, monster->spec->run);
  qa_q1_base_monster_delay(monster, 0.1);
  qa_q1_base_monster_attack_finished(monster, 1);
}
bool qa_q1_base_monster_search_for_coop_target(qa_q1_base_monster *monster) {
  if (monster->ops != NULL && monster->ops->search_for_coop_target != NULL)
    return monster->ops->search_for_coop_target(monster);
  return qa_q1_base_monster_search_for_coop_target_super(monster);
}
bool qa_q1_base_monster_search_for_coop_target_super(qa_q1_base_monster *monster) {
  return monster->game->options.coop && monster->state->search_until < monster->game->time &&
         qa_q1_base_monster_find_target(monster);
}
void qa_q1_base_monster_attack_finished(qa_q1_base_monster *monster, double seconds) {
  if (monster->ops != NULL && monster->ops->attack_finished != NULL) monster->ops->attack_finished(monster, seconds);
  else qa_q1_base_monster_attack_finished_super(monster, seconds);
}
void qa_q1_base_monster_attack_finished_super(qa_q1_base_monster *monster, double seconds) {
  monster->state->refired = false;
  if (edition_is(monster, "rerelease") || qa_q1_option_skill(monster->game) != 3)
    monster->state->attack_finished = monster->game->time + seconds;
}
/* Hipnotic CheckAttack lets a charmed monster fire when another entity is in the line. */
static bool fires_through_blockers(qa_q1_base_monster *monster) {
  return monster->ops != NULL && monster->ops->fires_through_blockers != NULL &&
         monster->ops->fires_through_blockers(monster);
}
static bool clear_shot(qa_q1_base_monster *monster, qa_actor_id enemy) {
  qa_vec3 start, end;
  if (!qa_q1_base_monster_eye(monster, monster->entity->actor.id, &start) ||
      !qa_q1_base_monster_eye(monster, enemy, &end)) return false;
  qa_q1_trace_request request;
  memset(&request, 0, sizeof request);
  request.start = start;
  request.end = end;
  request.bounds = POINT_BOUNDS;
  request.has_ignore = true;
  request.ignore = monster->entity->actor.id;
  request.monsters = true;
  const qa_q1_trace trace = qa_q1_host_trace(monster->game, request);
  return ((trace.has_actor && qa_same_actor(trace.actor, enemy)) || fires_through_blockers(monster)) &&
         (species_is(monster, "wizard") || !(trace.in_open && trace.in_water));
}
static void wizard_move(qa_q1_base_monster *monster, bool sliding) {
  if (monster->sliding == sliding) return;
  monster->sliding = sliding;
  qa_q1_base_monster_play(monster, sliding ? "wiz_side1" : "wiz_run1");
}
bool qa_q1_base_monster_try_attack(qa_q1_base_monster *monster) {
  if (monster->ops != NULL && monster->ops->try_attack != NULL) return monster->ops->try_attack(monster);
  return qa_q1_base_monster_try_attack_super(monster);
}
bool qa_q1_base_monster_try_attack_super(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_base_monster_species *spec = monster->spec;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target) || !monster->state->has_enemy) return false;
  const qa_actor_id enemy = monster->state->enemy;
  if (species_is(monster, "army")) return qa_q1_army_check_attack(monster);
  if (species_is(monster, "dog")) return qa_q1_dog_check_attack(monster);
  const double distance = range_enemy(monster);
  if (species_is(monster, "demon")) {
    if (distance < 120) { entity->attack_state = "melee"; return true; }
    const qa_q1_body body = qa_q1_get_body(game, entity);
    qa_q1_body other;
    if (!read_body(game, enemy, &other)) return false;
    const qa_vec3 delta = qa_sub3(target, body.origin);
    const double horizontal = hypot(delta.x, delta.y);
    const double other_height = other.bounds.max.z - other.bounds.min.z;
    if (body.origin.z + body.bounds.min.z > target.z + other.bounds.min.z + other_height * 0.75 ||
        body.origin.z + body.bounds.max.z < target.z + other.bounds.min.z + other_height * 0.25 ||
        horizontal < 100 || (horizontal > 200 && host_random(game) < 0.9))
      return false;
    qa_q1_sound_defaults(game, entity->actor.id, "demon/djump.wav");
    entity->attack_state = "missile";
    return true;
  }
  const bool specialized = same(entity->classname, "monster_ogre") || species_is(monster, "shambler");
  const bool wizard = species_is(monster, "wizard");
  if (!specialized && !wizard && !clear_shot(monster, enemy)) return false;
  if (distance < 120 && spec->melee && (!specialized || qa_q1_can_damage(game, enemy, entity->actor.id))) {
    if (specialized) entity->attack_state = "melee";
    else qa_q1_base_monster_melee_attack(monster);
    return true;
  }
  if (spec->missile == NULL || game->time < monster->state->attack_finished) return false;
  if (distance >= 1000 || (species_is(monster, "shambler") && distance > 600)) {
    if (wizard) wizard_move(monster, false);
    return false;
  }
  if ((specialized || wizard) && !clear_shot(monster, enemy)) {
    if (wizard) wizard_move(monster, false);
    return false;
  }
  if (specialized) {
    const double base = species_is(monster, "shambler") ? 2 : 1;
    qa_q1_base_monster_attack_finished(monster, base + 2 * host_random(game));
    entity->attack_state = "missile";
    return true;
  }
  if (distance < 120 && !wizard) monster->state->attack_finished = 0;
  const double chance = distance < 120 ? 0.9
                        : distance < 500 ? (wizard ? 0.6 : spec->melee ? 0.2 : 0.4)
                                         : (wizard ? 0.2 : spec->melee ? 0.05 : 0.1);
  if (host_random(game) >= chance) {
    if (wizard) wizard_move(monster, distance < 500);
    return false;
  }
  if (wizard) { monster->sliding = false; entity->attack_state = "missile"; return true; }
  if (species_is(monster, "zombie")) {
    const double r = host_random(game);
    qa_q1_base_monster_play(monster, r < 0.3 ? "zombie_atta1" : r < 0.6 ? "zombie_attb1" : "zombie_attc1");
  } else {
    qa_q1_base_monster_play(monster, spec->missile);
  }
  qa_q1_base_monster_attack_finished(monster, 2 * host_random(game));
  return true;
}
void qa_q1_base_monster_melee_attack(qa_q1_base_monster *monster) {
  if (monster->ops != NULL && monster->ops->melee_attack != NULL) monster->ops->melee_attack(monster);
  else qa_q1_base_monster_melee_attack_super(monster);
}
void qa_q1_base_monster_melee_attack_super(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  if (species_is(monster, "knight")) {
    qa_q1_base_monster_play(monster, range_enemy(monster) < 80 ? "knight_atk1" : "knight_runatk1");
  } else if (species_is(monster, "demon")) {
    qa_q1_base_monster_play(monster, "demon1_atta1");
  } else if (species_is(monster, "ogre")) {
    qa_q1_base_monster_play(monster, host_random(game) > 0.5 ? "ogre_smash1" : "ogre_swing1");
  } else if (species_is(monster, "hellknight")) {
    qa_q1_sound(game, monster->entity->actor.id, "hknight/slash1.wav", "weapon", 1, 1);
    qa_q1_base_monster_play(monster, monster->services.next_hell_knight_melee(monster->services.ctx));
  } else if (species_is(monster, "shambler")) {
    const double chance = host_random(game);
    qa_q1_base_monster_play(monster, chance > 0.6 || qa_q1_health(game, monster->entity->actor.id) == 600
                                         ? "sham_smash1" : chance > 0.3 ? "sham_swingr1" : "sham_swingl1");
  } else if (species_is(monster, "tarbaby")) {
    qa_q1_base_monster_play(monster, "tbaby_jump1");
  } else if (species_is(monster, "fish")) {
    qa_q1_base_monster_play(monster, "f_attack1");
  } else if (species_is(monster, "dog")) {
    qa_q1_base_monster_play(monster, "dog_atta1");
  }
}
double qa_q1_base_monster_melee(qa_q1_base_monster *monster, double range, double scale, double rolls, bool sight) {
  qa_q1_services *game = monster->game;
  if (!monster->state->has_enemy) return 0;
  const qa_actor_id enemy = monster->state->enemy;
  if (qa_q1_base_monster_distance(monster) > range ||
      (sight && !qa_q1_can_damage(game, enemy, monster->entity->actor.id))) return 0;
  double damage = 0;
  for (double i = 0; i < rolls; i++) damage = FR(damage + host_random(game));
  damage = FR(damage * scale);
  const qa_actor_id self = monster->entity->actor.id;
  (void)qa_q1_damage(game, enemy, self, &self, damage, NULL, "direct", "", NULL);
  return damage;
}
void qa_q1_base_monster_retaliate(qa_q1_base_monster *monster, const qa_actor_id *attacker) {
  if (monster->ops != NULL && monster->ops->retaliate != NULL) monster->ops->retaliate(monster, attacker);
  else qa_q1_base_monster_retaliate_super(monster, attacker);
}
void qa_q1_base_monster_retaliate_super(qa_q1_base_monster *monster, const qa_actor_id *attacker) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (attacker == NULL || qa_same_actor(*attacker, entity->actor.id)) return;
  const qa_actor_id other = *attacker;
  if (same(game->host.classname(game->host.ctx, other), entity->classname) && !species_is(monster, "army")) return;
  if (game->world != NULL && qa_same_actor(other, game->world->actor.id)) return;
  if (is_enemy(monster, other)) return;
  if (monster->state->has_enemy && qa_q1_is_player(game, &monster->state->enemy)) {
    const qa_actor_id enemy = monster->state->enemy;
    set_old_enemy(monster->state, &enemy);
  }
  qa_q1_base_monster_found(monster, other);
}
void qa_q1_base_monster_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage) {
  if (monster->ops != NULL && monster->ops->pain != NULL) monster->ops->pain(monster, attacker, damage);
  else qa_q1_base_monster_pain_super(monster, attacker, damage);
}
static void sound_at(qa_q1_base_monster *monster, const char *path) {
  qa_q1_sound_defaults(monster->game, monster->entity->actor.id, path);
}
void qa_q1_base_monster_pain_super(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_monster *state = monster->state;
  if ((qa_js_to_int32(entity->movement_flags) & 32) != 0) qa_q1_base_monster_retaliate(monster, attacker);
  const char *species = monster->spec->species;
  if (same(species, "zombie")) {
    set_health(game, entity->actor, 60);
    if (damage < 9 || monster->in_pain == 2) return;
    if (damage >= 25) { monster->in_pain = 2; qa_q1_base_monster_play(monster, "zombie_paine1"); return; }
    if (monster->in_pain != 0) { state->pain_finished = game->time + 3; return; }
    if (state->pain_finished > game->time) { monster->in_pain = 2; qa_q1_base_monster_play(monster, "zombie_paine1"); return; }
    monster->in_pain = 1;
    const double r = host_random(game);
    qa_q1_base_monster_play(monster, r < 0.25 ? "zombie_paina1" : r < 0.5 ? "zombie_painb1" : r < 0.75 ? "zombie_painc1" : "zombie_paind1");
  } else if (same(species, "fish")) {
    qa_q1_base_monster_play(monster, "f_pain1");
  } else if (same(species, "wizard")) {
    sound_at(monster, "wizard/wpain.wav");
    if (host_random(game) * 70 > damage) return;
    qa_q1_base_monster_play(monster, "wiz_pain1");
  } else if (same(species, "shambler")) {
    sound_at(monster, "shambler/shurt2.wav");
    if (qa_q1_health(game, entity->actor.id) <= 0 || host_random(game) * 400 > damage || state->pain_finished > game->time) return;
    state->pain_finished = game->time + 2;
    qa_q1_base_monster_play(monster, "sham_pain1");
  } else if (same(species, "demon")) {
    if (entity->touch.fn != NULL || state->pain_finished > game->time) return;
    state->pain_finished = game->time + 1;
    sound_at(monster, "demon/dpain1.wav");
    if (host_random(game) * 200 > damage) return;
    qa_q1_base_monster_play(monster, "demon1_pain1");
  } else if (same(species, "knight")) {
    if (state->pain_finished > game->time) return;
    const double r = host_random(game);
    sound_at(monster, qa_q1_base_monster_sfx(monster, "knight/khurt.wav"));
    state->pain_finished = game->time + 1;
    qa_q1_base_monster_play(monster, r < 0.85 ? "knight_pain1" : "knight_painb1");
  } else if (same(species, "enforcer")) {
    const double r = host_random(game);
    if (state->pain_finished > game->time) return;
    sound_at(monster, r < 0.5 ? "enforcer/pain1.wav" : "enforcer/pain2.wav");
    state->pain_finished = game->time + (r < 0.7 ? 1 : 2);
    qa_q1_base_monster_play(monster, r < 0.2 ? "enf_paina1" : r < 0.4 ? "enf_painb1" : r < 0.7 ? "enf_painc1" : "enf_paind1");
  } else if (same(species, "ogre")) {
    if (state->pain_finished > game->time) return;
    sound_at(monster, "ogre/ogpain1.wav");
    const double r = host_random(game);
    state->pain_finished = game->time + (r < 0.75 ? 1 : 2);
    qa_q1_base_monster_play(monster, r < 0.25 ? "ogre_pain1" : r < 0.5 ? "ogre_painb1" : r < 0.75 ? "ogre_painc1"
                                     : r < 0.88 ? "ogre_paind1" : "ogre_paine1");
  } else if (same(species, "hellknight")) {
    if (state->pain_finished > game->time) return;
    sound_at(monster, qa_q1_base_monster_sfx(monster, "hknight/pain1.wav"));
    if (game->time - state->pain_finished <= 5 && host_random(game) * 30 > damage) return;
    state->pain_finished = game->time + 1;
    qa_q1_base_monster_play(monster, "hknight_pain1");
  } else if (same(species, "shalrath")) {
    if (state->pain_finished > game->time) return;
    sound_at(monster, "shalrath/pain.wav");
    state->pain_finished = game->time + 3;
    qa_q1_base_monster_play(monster, "shal_pain1");
  } else if (same(species, "army")) {
    qa_q1_army_pain(monster);
  } else if (same(species, "dog")) {
    qa_q1_dog_pain(monster);
  }
}
void qa_q1_base_monster_count_kill(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (monster->counted_death) return;
  monster->counted_death = true;
  const qa_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
  if (mission != NULL) {
    mission->killed(mission->ctx, qa_q1_base_monster_enemy(monster));
  } else if (monster->services.count_monster_kill(monster->services.ctx, monster)) {
    game->killed_monsters++;
    qa_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = "monster-killed";
    event.has_actor = true;
    event.actor = entity->actor.id;
    event.total = game->total_monsters;
    event.found = game->killed_monsters;
    qa_q1_emit(game, event);
  }
  if (edition_is(monster, "rerelease") && (qa_js_to_int32(entity->movement_flags) & 32) != 0 &&
      monster->state->has_enemy && !qa_same_actor(monster->state->enemy, entity->actor.id)) {
    qa_q1_actor *enemy = qa_q1_get_entity(game, &monster->state->enemy);
    if ((qa_js_to_int32(enemy != NULL ? enemy->movement_flags : 0) & 32) != 0) {
      qa_q1_event event;
      memset(&event, 0, sizeof event);
      event.kind = "achievement";
      event.id = "ACH_FRIENDLY_FIRE";
      qa_q1_emit(game, event);
    }
  }
  entity->movement_flags = qa_js_to_int32(entity->movement_flags) & ~3;
  if (mission == NULL) {
    qa_actor_id copy;
    const bool has_enemy = monster->state->has_enemy;
    if (has_enemy) copy = monster->state->enemy;
    qa_q1_use_targets(game, entity, has_enemy ? &copy : NULL);
  }
}
/* Source variants (Rogue statues) swap individual sound paths. */
const char *qa_q1_base_monster_sfx(qa_q1_base_monster *monster, const char *path) {
  if (monster->ops != NULL && monster->ops->sfx != NULL) return monster->ops->sfx(monster, path);
  return path;
}
bool qa_q1_base_monster_gib_death(qa_q1_base_monster *monster) {
  if (monster->ops != NULL && monster->ops->gib_death != NULL) return monster->ops->gib_death(monster);
  return qa_q1_base_monster_gib_death_super(monster);
}
void qa_q1_base_monster_die(qa_q1_base_monster *monster, const qa_actor_id *attacker) {
  if (monster->ops != NULL && monster->ops->die != NULL) monster->ops->die(monster, attacker);
  else qa_q1_base_monster_die_super(monster, attacker);
}
static void throw_gibs(qa_q1_base_monster *monster) {
  qa_vec3 origin;
  const char * model;
  double damage;
  qa_q1_services *game = monster->game;
  for (size_t i = 0; i < monster->spec->gib_count; i++)
    (void)(origin = qa_q1_base_monster_origin(monster), model = monster->spec->gibs[i], damage = qa_q1_health(game, monster->entity->actor.id), qa_q1_throw_gib(game, origin, model,
                          damage));
}
/* Plays the gibbing death when health is below the species threshold;
 * returns whether it did. */
bool qa_q1_base_monster_gib_death_super(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_base_monster_species *spec = monster->spec;
  if (qa_q1_health(game, entity->actor.id) >= spec->gib_health || spec->head == NULL) return false;
  sound_at(monster, species_is(monster, "zombie") ? "zombie/z_gib.wav" : "player/udeath.wav");
  /* The grunt throws its head first, so the gibs leave from the lowered body. */
  if (species_is(monster, "army")) qa_q1_throw_head(game, entity, spec->head);
  throw_gibs(monster);
  if (!species_is(monster, "army")) qa_q1_throw_head(game, entity, spec->head);
  return true;
}
void qa_q1_base_monster_die_super(qa_q1_base_monster *monster, const qa_actor_id *attacker) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_base_monster_species *spec = monster->spec;
  if (monster->counted_death) return;
  set_enemy_id(monster->state, attacker);
  if (qa_q1_health(game, entity->actor.id) < -99) set_health(game, entity->actor, -99);
  qa_q1_actor_set_damageable(entity, false);
  memset(&entity->touch, 0, sizeof entity->touch);
  if (species_is(monster, "oldone")) { monster->services.finale(monster->services.ctx, monster); return; }
  qa_q1_base_monster_count_kill(monster);
  if (qa_q1_base_monster_gib_death(monster)) return;
  const char *species = spec->species;
  if (same(species, "knight")) {
    sound_at(monster, qa_q1_base_monster_sfx(monster, "knight/kdeath.wav"));
    qa_q1_base_monster_play(monster, host_random(game) < 0.5 ? "knight_die1" : "knight_dieb1");
  } else if (same(species, "enforcer")) {
    sound_at(monster, "enforcer/death1.wav");
    qa_q1_base_monster_play(monster, host_random(game) > 0.5 ? "enf_die1" : "enf_fdie1");
  } else if (same(species, "demon")) {
    qa_q1_base_monster_play(monster, "demon1_die1");
  } else if (same(species, "ogre")) {
    sound_at(monster, "ogre/ogdth.wav");
    qa_q1_base_monster_play(monster, host_random(game) < 0.5 ? "ogre_die1" : "ogre_bdie1");
  } else if (same(species, "hellknight")) {
    sound_at(monster, qa_q1_base_monster_sfx(monster, "hknight/death1.wav"));
    qa_q1_base_monster_play(monster, host_random(game) > 0.5 ? "hknight_die1" : "hknight_dieb1");
  } else if (same(species, "shambler")) {
    sound_at(monster, "shambler/sdeath.wav");
    qa_q1_base_monster_play(monster, "sham_death1");
  } else if (same(species, "wizard")) {
    entity->movement = "toss";
    entity->movement_flags = qa_js_to_int32(entity->movement_flags) & ~1;
    qa_q1_base_monster_play(monster, "wiz_death1");
  } else if (same(species, "shalrath")) {
    sound_at(monster, "shalrath/death.wav");
    entity->solid = "none";
    qa_q1_link(game, entity);
    qa_q1_base_monster_play(monster, "shal_death1");
  } else if (same(species, "tarbaby")) {
    qa_q1_base_monster_play(monster, "tbaby_die1");
  } else if (same(species, "fish")) {
    qa_q1_base_monster_play(monster, "f_death1");
  } else if (same(species, "zombie")) {
    sound_at(monster, "zombie/z_gib.wav");
    throw_gibs(monster);
    qa_q1_throw_head(game, entity, "h_zombie");
  } else if (same(species, "boss")) {
    qa_q1_base_monster_play(monster, "boss_death1");
  } else if (same(species, "army")) {
    sound_at(monster, "soldier/death1.wav");
    qa_q1_base_monster_play(monster, host_random(game) < 0.5 ? "army_die1" : "army_cdie1");
  } else if (same(species, "dog")) {
    sound_at(monster, "dog/ddeath.wav");
    entity->solid = "none";
    qa_q1_link(game, entity);
    qa_q1_base_monster_play(monster, host_random(game) > 0.5 ? "dog_die1" : "dog_dieb1");
  }
}
static qa_q1_bound_pain named_pain(qa_q1_base_monster *monster, const char *suffix) {
  char name[128];
  (void)snprintf(name, sizeof name, "%s:%s", prefix_of(monster), suffix);
  return qa_q1_callback_registry_pain(monster->game->named, monster->entity, name);
}
static qa_q1_bound_die named_die(qa_q1_base_monster *monster, const char *suffix) {
  char name[128];
  (void)snprintf(name, sizeof name, "%s:%s", prefix_of(monster), suffix);
  return qa_q1_callback_registry_die(monster->game->named, monster->entity, name);
}
static qa_q1_bound_use named_use(qa_q1_base_monster *monster, const char *suffix) {
  char name[128];
  (void)snprintf(name, sizeof name, "%s:%s", prefix_of(monster), suffix);
  return qa_q1_callback_registry_use(monster->game->named, monster->entity, name);
}
void qa_q1_base_monster_spawn(qa_q1_base_monster *monster) {
  if (monster->ops != NULL && monster->ops->spawn != NULL) monster->ops->spawn(monster);
  else qa_q1_base_monster_spawn_super(monster);
}
void qa_q1_base_monster_spawn_super(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_base_monster_species *spec = monster->spec;
  if (!monster->has_source && qa_q1_services_uses_id1(game)) {
    if (game->options.deathmatch != 0) { qa_q1_remove(game, entity); return; }
    qa_q1_precache_id1_monster(game, spec->species);
  }
  const bool crucified = species_is(monster, "zombie") && (qa_js_to_int32(entity->spawnflags) & 1) != 0;
  if (!crucified) {
    const qa_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
    if (mission == NULL) game->total_monsters++;
    else mission->spawned(mission->ctx);
  }
  entity->max_health = spec->health;
  set_health(game, entity->actor, spec->health);
  char model[128];
  (void)snprintf(model, sizeof model, "progs/%s.mdl", spec->model);
  set_text(&entity->model, model);
  entity->solid = "slidebox";
  entity->movement = "step";
  entity->aimed_damage = true;
  if (spec->kill_string != NULL) (void)qa_q1_actor_field_set(entity, "killstring", spec->kill_string);
  /* TS `entity.number("yaw_speed") || default`: 0 and NaN fall through. */
  const double yaw_speed = qa_q1_actor_number(entity, "yaw_speed");
  entity->yaw_speed = yaw_speed != 0 && !isnan(yaw_speed) ? yaw_speed
                      : (same(spec->movement, "fly") || same(spec->movement, "swim") ? 10 : 20);
  entity->ideal_yaw = qa_q1_get_body(game, entity).angles.y;
  if (!crucified && !same(spec->movement, "boss"))
    entity->movement_flags = qa_js_to_int32(entity->movement_flags) |
                             (same(spec->movement, "fly") ? 1 : same(spec->movement, "swim") ? 2 : 0);
  qa_q1_set_bounds(game, entity, spec->bounds);
  entity->pain = named_pain(monster, "monster_pain");
  entity->die = named_die(monster, "monster_die");
  entity->path_end = named_action(monster, "monster_stand");
  entity->use = named_use(monster, "monster_use");
  if (species_is(monster, "boss")) {
    set_text(&entity->model, "");
    entity->solid = "none";
    entity->use = named_use(monster, "boss_awake");
    return;
  }
  if (species_is(monster, "oldone")) {
    qa_q1_actor_set_damageable(entity, true);
    copy_name(monster->next_frame, sizeof monster->next_frame, "old_idle1");
    qa_q1_base_monster_delay(monster, 0.1);
    return;
  }
  if (crucified) { entity->movement = "none"; qa_q1_base_monster_play(monster, "zombie_cruc1"); return; }
  const double jitter = host_random(game) * 0.5;
  qa_q1_schedule(game, entity, qa_js_max(0, entity->next_think) + jitter - game->time,
                 named_action(monster, "monster_start"));
}
void qa_q1_base_monster_start(qa_q1_base_monster *monster) {
  if (monster->ops != NULL && monster->ops->start != NULL) monster->ops->start(monster);
  else qa_q1_base_monster_start_super(monster);
}
void qa_q1_base_monster_start_super(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_base_monster_species *spec = monster->spec;
  if (same(spec->movement, "walk")) {
    /* droptofloor: the monster is raised a unit first and keeps that origin when there is no floor to land on. */
    const qa_vec3 start = qa_add3(qa_q1_base_monster_origin(monster), V3(0, 0, 1));
    qa_q1_body_patch raised;
    memset(&raised, 0, sizeof raised);
    raised.has_origin = true; raised.origin = start;
    qa_q1_set_body(game, entity, &raised);
    qa_q1_trace_request request;
    memset(&request, 0, sizeof request);
    request.start = start;
    request.end = qa_add3(start, V3(0, 0, -256));
    request.bounds = qa_q1_get_body(game, entity).bounds;
    request.has_ignore = true;
    request.ignore = entity->actor.id;
    request.monsters = true;
    const qa_q1_trace trace = qa_q1_host_trace(game, request);
    qa_q1_body_patch patch;
    memset(&patch, 0, sizeof patch);
    patch.has_origin = true;
    if (trace.fraction < 1 && !trace.all_solid) {
      patch.origin = trace.end;
      patch.has_ground = true;
      patch.ground = trace.has_actor ? &trace.actor : NULL;
      qa_q1_set_body(game, entity, &patch);
      entity->movement_flags = qa_js_to_int32(entity->movement_flags) | 512;
    }
    (void)game->host.walk_move(game->host.ctx, entity->actor, 0, 0);
  }
  if (species_is(monster, "fish") && edition_is(monster, "classic") && qa_q1_mission(game, entity->actor.id) == NULL)
    game->total_monsters++;
  entity->movement_flags = qa_js_to_int32(entity->movement_flags) | 32;
  qa_q1_actor_set_damageable(entity, true);
  entity->ideal_yaw = qa_q1_get_body(game, entity).angles.y;
  qa_q1_link(game, entity);
  if (same(spec->movement, "fly")) (void)game->host.walk_move(game->host.ctx, entity->actor, 0, 0);
  const qa_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
  if (mission != NULL) {
    mission->started(mission->ctx);
    qa_q1_base_monster_update_route(monster);
  } else {
    const char *path_name = monster->state->path != NULL ? monster->state->path : "";
    qa_q1_actor *found[1];
    qa_q1_actor *path = qa_q1_find(game, path_name, found, 1) > 0 ? found[0] : NULL;
    if (path_name[0] != '\0') {
      if (!same(spec->movement, "fly")) {
        const qa_vec3 path_origin = path == NULL ? ZERO3 : qa_q1_get_body(game, path).origin;
        entity->ideal_yaw = qa_q1_yaw_for(qa_sub3(path_origin, qa_q1_base_monster_origin(monster)));
      }
      if (same(spec->movement, "swim")) {
        qa_q1_base_monster_play(monster, spec->walk);
      } else {
        if (path != NULL && same(path->classname, "path_corner")) qa_q1_base_monster_play(monster, spec->walk);
        else monster->state->pause_until = FR(99999999);
        qa_q1_base_monster_play(monster, spec->stand);
      }
    } else {
      monster->state->pause_until = FR(99999999);
      qa_q1_base_monster_play(monster, spec->stand);
    }
  }
  if (!same(spec->movement, "fly") && entity->think.fn != NULL)
    qa_q1_schedule(game, entity, entity->next_think - game->time + host_random(game) * 0.5, entity->think);
}
void qa_q1_base_monster_use(qa_q1_base_monster *monster, const qa_actor_id *activator) {
  if (monster->ops != NULL && monster->ops->use != NULL) monster->ops->use(monster, activator);
  else qa_q1_base_monster_use_super(monster, activator);
}
void qa_q1_base_monster_use_super(qa_q1_base_monster *monster, const qa_actor_id *activator) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_monster_mission *mission = qa_q1_mission(game, entity->actor.id);
  if (mission != NULL && mission->use(mission->ctx, activator)) return;
  if (monster->state->has_enemy || qa_q1_health(game, entity->actor.id) <= 0 || activator == NULL ||
      !qa_q1_is_player(game, activator)) return;
  qa_monster_target_observation observed;
  if (!observe(game, *activator, &observed) || observed.invisible || observed.notarget) return;
  set_enemy_id(monster->state, activator);
  qa_q1_schedule(game, entity, 0.1, named_action(monster, "monster_found"));
}
void qa_q1_base_monster_awake(qa_q1_base_monster *monster, const qa_actor_id *activator) {
  if (monster->ops != NULL && monster->ops->awake != NULL) monster->ops->awake(monster, activator);
  else qa_q1_base_monster_awake_super(monster, activator);
}
void qa_q1_base_monster_awake_super(qa_q1_base_monster *monster, const qa_actor_id *activator) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  set_text(&entity->model, "progs/boss.mdl");
  entity->solid = "slidebox";
  qa_q1_actor_set_damageable(entity, false);
  set_health(game, entity->actor, qa_q1_option_skill(game) == 0 ? 1 : 3);
  set_enemy_id(monster->state, activator);
  qa_q1_effect(game, "lava-splash", qa_q1_base_monster_origin(monster), NULL, 1);
  qa_q1_link(game, entity);
  qa_q1_base_monster_play(monster, "boss_rise1");
}

/* TS registerMonsterCallbacks: the closures capture `monster` (lookup). */
typedef struct callback_ctx { qa_q1_monster_lookup_fn lookup; void *lookup_ctx; } callback_ctx;
static qa_q1_base_monster *lookup(void *ctx, qa_q1_actor *entity) {
  const callback_ctx *c = ctx;
  return c->lookup(c->lookup_ctx, entity);
}
static void cb_jump_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                          const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)game; (void)normal; (void)surface;
  qa_q1_monster_jump_touch(lookup(ctx, entity), other);
}
static void cb_frame(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1_base_monster *monster = lookup(ctx, entity);
  qa_q1_base_monster_play(monster, monster->next_frame);
}
static void cb_route(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1_base_monster_update_route(lookup(ctx, entity));
}
static void cb_start(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1_base_monster_start(lookup(ctx, entity));
}
static void cb_stand(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1_base_monster *monster = lookup(ctx, entity);
  qa_q1_base_monster_play(monster, monster->spec->stand);
}
static void cb_found(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1_base_monster *monster = lookup(ctx, entity);
  if (monster->state->has_enemy) qa_q1_base_monster_found(monster, monster->state->enemy);
}
static void cb_pain(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker, double damage) {
  qa_q1_base_monster_pain(lookup(ctx, entity), attacker, damage);
  /* T_Damage extends the cooldown after every th_pain call on nightmare,
   * including one that returns early. */
  if (qa_q1_option_skill(game) == 3) lookup(ctx, entity)->state->pain_finished = game->time + 5;
}
static void cb_die(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  (void)game;
  qa_q1_base_monster_die(lookup(ctx, entity), attacker);
}
static void cb_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                   const qa_actor_id *activator) {
  (void)game; (void)other;
  qa_q1_base_monster_use(lookup(ctx, entity), activator);
}
static void cb_awake(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                     const qa_actor_id *activator) {
  (void)game; (void)other;
  qa_q1_base_monster_awake(lookup(ctx, entity), activator);
}
static void register_named(qa_q1_services *game, const char *prefix, const char *suffix,
                           qa_q1_callback_handlers handlers) {
  char name[128];
  (void)snprintf(name, sizeof name, "%s:%s", prefix, suffix);
  qa_q1_callback_registry_register(game->named, name, handlers);
}
void *qa_q1_register_monster_callbacks(qa_q1_services *game, const char *prefix,
                                       qa_q1_monster_lookup_fn lookup_fn, void *lookup_ctx) {
  /* The registry keeps the closure for the game's lifetime, as in TS; the
   * caller owns it. */
  callback_ctx *c = malloc(sizeof *c);
  if (c == NULL) qa_range_error("out of memory");
  c->lookup = lookup_fn;
  c->lookup_ctx = lookup_ctx;
  register_named(game, prefix, "monster_jump_touch", (qa_q1_callback_handlers){.ctx = c, .touch = cb_jump_touch});
  register_named(game, prefix, "monster_frame", (qa_q1_callback_handlers){.ctx = c, .action = cb_frame});
  register_named(game, prefix, "monster_route", (qa_q1_callback_handlers){.ctx = c, .action = cb_route});
  register_named(game, prefix, "monster_start", (qa_q1_callback_handlers){.ctx = c, .action = cb_start});
  register_named(game, prefix, "monster_stand", (qa_q1_callback_handlers){.ctx = c, .action = cb_stand});
  register_named(game, prefix, "monster_found", (qa_q1_callback_handlers){.ctx = c, .action = cb_found});
  register_named(game, prefix, "monster_pain", (qa_q1_callback_handlers){.ctx = c, .pain = cb_pain});
  register_named(game, prefix, "monster_die", (qa_q1_callback_handlers){.ctx = c, .die = cb_die});
  register_named(game, prefix, "monster_use", (qa_q1_callback_handlers){.ctx = c, .use = cb_use});
  register_named(game, prefix, "boss_awake", (qa_q1_callback_handlers){.ctx = c, .use = cb_awake});
  return c;
}

/* creatures.ts registerLegacyCallbacks: version 5 saves name the grunt and
 * dog callbacks the foundation used to own. */
static void cb_legacy_pain(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker, double damage) {
  qa_q1_base_monster_pain(lookup(ctx, entity), attacker, damage);
  if (qa_q1_option_skill(game) == 3) lookup(ctx, entity)->state->pain_finished = game->time + 5;
}
void *qa_q1_register_legacy_monster_callbacks(qa_q1_services *game, qa_q1_monster_lookup_fn lookup_fn, void *lookup_ctx) {
  callback_ctx *c = malloc(sizeof *c);
  if (c == NULL) qa_range_error("out of memory");
  c->lookup = lookup_fn;
  c->lookup_ctx = lookup_ctx;
  qa_q1_callback_registry_register(game->named, "monster_frame", (qa_q1_callback_handlers){.ctx = c, .action = cb_frame});
  qa_q1_callback_registry_register(game->named, "monster_path_end", (qa_q1_callback_handlers){.ctx = c, .action = cb_stand});
  qa_q1_callback_registry_register(game->named, "monster_found_target", (qa_q1_callback_handlers){.ctx = c, .action = cb_found});
  qa_q1_callback_registry_register(game->named, "walkmonster_start_go", (qa_q1_callback_handlers){.ctx = c, .action = cb_start});
  qa_q1_callback_registry_register(game->named, "monster_use", (qa_q1_callback_handlers){.ctx = c, .use = cb_use});
  qa_q1_callback_registry_register(game->named, "monster_pain", (qa_q1_callback_handlers){.ctx = c, .pain = cb_legacy_pain});
  qa_q1_callback_registry_register(game->named, "monster_die", (qa_q1_callback_handlers){.ctx = c, .die = cb_die});
  qa_q1_callback_registry_register(game->named, "Dog_JumpTouch", (qa_q1_callback_handlers){.ctx = c, .touch = cb_jump_touch});
  return c;
}

/* Points a monster at a route goal (or back to standing) outside its own
 * think, as the monster-mission host does. */
void qa_q1_set_monster_route(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *goal, double pause_until) {
  qa_q1_monster *monster = entity->monster;
  if (monster == NULL) qa_range_error("Missing saved monster state: %s", entity->classname);
  monster->pause_until = pause_until;
  const char *prefix = qa_q1_actor_field_get(entity, "source.monsterCallbackPrefix");
  if (prefix == NULL) qa_range_error("Monster %s has no source controller", entity->classname);
  char name[256];
  (void)snprintf(name, sizeof name, "%s:monster_route", prefix);
  qa_q1_call_action(game, entity, qa_q1_callback_registry_action(game->named, entity, name));
  qa_q1_body target;
  if (goal != NULL && read_body(game, *goal, &target))
    entity->ideal_yaw = qa_q1_yaw_for(qa_q1_vsub(target.origin, qa_q1_get_body(game, entity).origin));
}
