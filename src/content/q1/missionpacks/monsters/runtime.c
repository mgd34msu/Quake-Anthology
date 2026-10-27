/* Port of src/content/q1/missionpacks/monsters/runtime.ts, plus the
 * mirrored Q1Actor accessors declared in types.h. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/runtime.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/charm.h"
#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "monsters_internal.h"

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}
static void copy_text(char *dst, size_t size, const char *src) {
  if (size == 0) return;
  size_t length = strlen(src);
  if (length >= size) length = size - 1;
  memcpy(dst, src, length);
  dst[length] = '\0';
}

/* TS String(x): shortest %.*g that round-trips (exponent spelling may
 * differ cosmetically outside [1e-6, 1e21), as in persistence/value). */
void qa_q1mpm_format_number(char *buf, size_t size, double value) {
  if (isnan(value)) { copy_text(buf, size, "NaN"); return; }
  if (isinf(value)) { copy_text(buf, size, value > 0 ? "Infinity" : "-Infinity"); return; }
  for (int precision = 1; precision <= 17; precision++) {
    snprintf(buf, size, "%.*g", precision, value);
    if (strtod(buf, NULL) == value) return;
  }
  snprintf(buf, size, "%.17g", value);
}
void qa_q1mpm_format_fround(char *buf, size_t size, double value) {
  float rounded = (float)value;
  if (isnan(rounded)) { copy_text(buf, size, "NaN"); return; }
  if (isinf(rounded)) { copy_text(buf, size, rounded > 0 ? "Infinity" : "-Infinity"); return; }
  for (int precision = 1; precision <= 17; precision++) {
    snprintf(buf, size, "%.*g", precision, (double)rounded);
    if ((float)strtod(buf, NULL) == rounded) return;
  }
  snprintf(buf, size, "%.17g", (double)rounded);
}

qa_vec3 qa_q1mpm_velocity_angles(qa_vec3 velocity) {
  double yaw = velocity.x == 0 && velocity.y == 0 ? 0 : atan2(velocity.y, velocity.x) * 180 / 3.141592653589793;
  double pitch = velocity.x == 0 && velocity.y == 0 ? velocity.z > 0 ? 90 : 270
                 : atan2(velocity.z, hypot(velocity.x, velocity.y)) * 180 / 3.141592653589793;
  qa_vec3 out = {(float)(pitch < 0 ? pitch + 360 : pitch), (float)(yaw < 0 ? yaw + 360 : yaw), 0};
  return out;
}

void qa_q1mpm_actor_init(qa_q1mpm_actor *entity, qa_owned_actor actor, const char *classname) {
  memset(entity, 0, sizeof(*entity));
  entity->actor = actor;
  copy_text(entity->classname, sizeof(entity->classname), classname);
  entity->solid = "none";
  entity->movement = "none";
  entity->next_think = -1;
  entity->door_state = "bottom";
  entity->yaw_speed = 20;
  entity->attack_state = "straight";
}
void qa_q1mpm_actor_destroy(qa_q1mpm_actor *entity) {
  for (size_t i = 0; i < entity->field_count; i++) { free(entity->fields[i].key); free(entity->fields[i].value); }
  free(entity->fields);
  for (size_t i = 0; i < entity->reference_count; i++) free(entity->references[i].key);
  free(entity->references);
  memset(entity, 0, sizeof(*entity));
}
void qa_q1mpm_fields_set(qa_q1mpm_actor *entity, const char *key, const char *value) {
  for (size_t i = 0; i < entity->field_count; i++) {
    if (strcmp(entity->fields[i].key, key) == 0) {
      char *copy = dup_text(value);
      if (copy == NULL) return;
      free(entity->fields[i].value);
      entity->fields[i].value = copy;
      return;
    }
  }
  if (entity->field_count == entity->field_capacity) {
    size_t capacity = entity->field_capacity == 0 ? 8 : entity->field_capacity * 2;
    qa_q1mpm_field_entry *next = realloc(entity->fields, capacity * sizeof(*next));
    if (next == NULL) return;
    entity->fields = next;
    entity->field_capacity = capacity;
  }
  char *key_copy = dup_text(key), *value_copy = dup_text(value);
  if (key_copy == NULL || value_copy == NULL) { free(key_copy); free(value_copy); return; }
  entity->fields[entity->field_count].key = key_copy;
  entity->fields[entity->field_count].value = value_copy;
  entity->field_count++;
}
const char *qa_q1mpm_fields_get(const qa_q1mpm_actor *entity, const char *key) {
  for (size_t i = 0; i < entity->field_count; i++)
    if (strcmp(entity->fields[i].key, key) == 0) return entity->fields[i].value;
  return NULL;
}
bool qa_q1mpm_fields_has(const qa_q1mpm_actor *entity, const char *key) {
  return qa_q1mpm_fields_get(entity, key) != NULL;
}
void qa_q1mpm_references_set(qa_q1mpm_actor *entity, const char *key, qa_q1mpm_opt_actor value) {
  for (size_t i = 0; i < entity->reference_count; i++) {
    if (strcmp(entity->references[i].key, key) == 0) { entity->references[i].value = value; return; }
  }
  if (entity->reference_count == entity->reference_capacity) {
    size_t capacity = entity->reference_capacity == 0 ? 8 : entity->reference_capacity * 2;
    qa_q1mpm_ref_entry *next = realloc(entity->references, capacity * sizeof(*next));
    if (next == NULL) return;
    entity->references = next;
    entity->reference_capacity = capacity;
  }
  char *key_copy = dup_text(key);
  if (key_copy == NULL) return;
  entity->references[entity->reference_count].key = key_copy;
  entity->references[entity->reference_count].value = value;
  entity->reference_count++;
}
qa_q1mpm_opt_actor qa_q1mpm_references_get(const qa_q1mpm_actor *entity, const char *key) {
  for (size_t i = 0; i < entity->reference_count; i++)
    if (strcmp(entity->references[i].key, key) == 0) return entity->references[i].value;
  return QA_Q1MPM_NONE;
}
const char *qa_q1mpm_actor_text(const qa_q1mpm_actor *entity, const char *key) {
  const char *value = qa_q1mpm_fields_get(entity, key);
  return value == NULL ? "" : value;
}
double qa_q1mpm_actor_number_or(const qa_q1mpm_actor *entity, const char *key, double fallback) {
  const char *text = qa_q1mpm_actor_text(entity, key);
  if (text[0] == '\0') return fallback;
  double value = strtod(text, NULL); /* TS Number.parseFloat prefix parse */
  return isfinite(value) ? (float)value : fallback;
}
double qa_q1mpm_actor_number(const qa_q1mpm_actor *entity, const char *key) {
  return qa_q1mpm_actor_number_or(entity, key, 0);
}
qa_vec3 qa_q1mpm_parse_vector(const char *text) {
  double numbers[3] = {0, 0, 0};
  const char *cursor = text;
  for (int i = 0; i < 3; i++) {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') cursor++;
    if (*cursor == '\0') break;
    char *end = NULL;
    numbers[i] = strtod(cursor, &end);
    if (end == cursor) numbers[i] = NAN;
    cursor = end == cursor ? cursor + strlen(cursor) : end;
  }
  qa_vec3 out = {isfinite(numbers[0]) ? (float)numbers[0] : 0,
                 isfinite(numbers[1]) ? (float)numbers[1] : 0,
                 isfinite(numbers[2]) ? (float)numbers[2] : 0};
  return out;
}
qa_vec3 qa_q1mpm_actor_vector(const qa_q1mpm_actor *entity, const char *key) {
  return qa_q1mpm_parse_vector(qa_q1mpm_actor_text(entity, key));
}
bool qa_q1mpm_actor_damageable(qa_q1mpm_game *game, const qa_q1mpm_actor *entity) {
  qa_q1mpm_combat_read read;
  return game->host->combat_read(game->host->ctx, entity->actor.id, &read) && read.can_take_damage;
}
void qa_q1mpm_actor_set_damageable(qa_q1mpm_game *game, qa_q1mpm_actor *entity, bool value) {
  game->host->combat_set_can_take_damage(game->host->ctx, entity->actor, value);
}
double qa_q1mpm_player_powerup(const qa_q1mpm_player_state *player, const char *name) {
  if (player == NULL) return 0;
  for (size_t i = 0; i < player->powerup_count; i++)
    if (strcmp(player->powerups[i].name, name) == 0) return player->powerups[i].expires;
  return 0;
}
void qa_q1mpm_game_sound(qa_q1mpm_game *game, qa_q1mpm_actor *entity, const char *path) {
  game->sound(game, entity, path, QA_Q1MPM_CH("voice"), 1, 1);
}
void qa_q1mpm_game_sound_ch(qa_q1mpm_game *game, qa_q1mpm_actor *entity, const char *path,
                            qa_q1mpm_channel channel) {
  game->sound(game, entity, path, channel, 1, 1);
}
void qa_q1mpm_game_damage(qa_q1mpm_game *game, qa_actor_id target, qa_actor_id inflictor,
                          qa_q1mpm_opt_actor attacker, double amount) {
  game->damage(game, target, inflictor, attacker, amount, NULL, "direct", "");
}

/* Adapted definition action dispatch (BaseMonsterSource actions). */
static void call_action(const qa_q1mpm_named_action *entry, qa_q1mpm_mission_monster *monster) {
  if (entry->action.fn != NULL) entry->action.fn(entry->action.ctx, monster);
}
static const qa_q1mpm_named_action *find_action(const qa_q1mpm_pack_monster_definition *definition,
                                                const char *name) {
  for (size_t i = 0; i < definition->action_count; i++)
    if (strcmp(definition->actions[i].name, name) == 0) return &definition->actions[i];
  return NULL;
}

bool qa_q1mpm_mission_monster_init(qa_q1mpm_mission_monster *monster, qa_q1mpm_game *game,
                                   qa_q1mpm_actor *entity,
                                   const qa_q1mpm_pack_monster_definition *definition,
                                   qa_q1mpm_runtime *runtime) {
  memset(monster, 0, sizeof(*monster));
  monster->game = game;
  monster->entity = entity;
  monster->definition = definition;
  monster->runtime = runtime;
  monster->callback_prefix = runtime->pack;
  copy_text(monster->current_frame, sizeof(monster->current_frame), definition->spec.stand);
  copy_text(monster->next_frame, sizeof(monster->next_frame), definition->spec.stand);
  if (entity->monster != NULL) {
    monster->state = *entity->monster;
  } else {
    monster->state.species = definition->spec.species;
    monster->state.mode = "stand";
    copy_text(monster->state.path, sizeof(monster->state.path), entity->target);
  }
  entity->monster = &monster->state;
  qa_q1mpm_fields_set(entity, "source.monsterCallbackPrefix", runtime->pack);
  qa_q1mpm_base_source source = {runtime->pack, definition->frames, definition->actions,
                                 definition->action_count};
  runtime->base->base_factory.create(runtime->base->base_factory.ctx, monster, &source);
  return true;
}
void qa_q1mpm_mission_monster_destroy(qa_q1mpm_mission_monster *monster) {
  if (monster->entity != NULL && monster->entity->monster == &monster->state)
    monster->entity->monster = NULL;
  memset(monster, 0, sizeof(*monster));
}
void qa_q1mpm_monster_set_next_frame(qa_q1mpm_mission_monster *monster, const char *name) {
  copy_text(monster->next_frame, sizeof(monster->next_frame), name);
}
void qa_q1mpm_monster_set_enemy(qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor enemy) {
  monster->state.enemy = enemy;
}
qa_vec3 qa_q1mpm_monster_origin(qa_q1mpm_mission_monster *monster) {
  return monster->game->body(monster->game, monster->entity).origin;
}
bool qa_q1mpm_monster_target(qa_q1mpm_mission_monster *monster, qa_vec3 *out) {
  if (!monster->state.enemy.has) return false;
  qa_q1mpm_body body;
  if (!monster->game->host->bodies_read(monster->game->host->ctx, monster->state.enemy.id, &body)) return false;
  *out = body.origin;
  return true;
}
double qa_q1mpm_monster_distance(qa_q1mpm_mission_monster *monster) {
  qa_vec3 target;
  return qa_q1mpm_monster_target(monster, &target)
             ? qa_q1mpm_length(qa_q1mpm_vsub(target, qa_q1mpm_monster_origin(monster)))
             : INFINITY;
}
const qa_q1mpm_species_spec *qa_q1mpm_monster_spec(qa_q1mpm_mission_monster *monster) {
  return &monster->definition->spec;
}
void qa_q1mpm_monster_spawn(qa_q1mpm_mission_monster *monster) {
  if (monster->definition->spawn.fn == NULL) qa_q1mpm_monster_spawn_default(monster);
  else monster->definition->spawn.fn(monster->definition->spawn.ctx, monster);
}
void qa_q1mpm_monster_spawn_default(qa_q1mpm_mission_monster *monster) {
  monster->base.spawn(monster->base.ctx, monster);
}
void qa_q1mpm_monster_start(qa_q1mpm_mission_monster *monster) {
  if (monster->definition->start.fn == NULL) qa_q1mpm_monster_start_default(monster);
  else monster->definition->start.fn(monster->definition->start.ctx, monster);
}
void qa_q1mpm_monster_start_default(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_actor *found[1];
  size_t count = monster->game->find(monster->game, monster->state.path, found, 1);
  qa_q1mpm_opt_actor path = count > 0 ? QA_Q1MPM_SOME(found[0]->actor.id) : QA_Q1MPM_NONE;
  qa_q1mpm_references_set(monster->entity, "goalentity", path);
  qa_q1mpm_references_set(monster->entity, "movetarget", path);
  monster->base.start(monster->base.ctx, monster);
}
void qa_q1mpm_monster_play(qa_q1mpm_mission_monster *monster, const char *name) {
  if (!qa_q1mpm_frame_table_has(&monster->definition->frames, name)) {
    const qa_q1mpm_named_action *action = find_action(monster->definition, name);
    if (action != NULL) { call_action(action, monster); return; }
  }
  monster->base.play(monster->base.ctx, monster, name);
}
void qa_q1mpm_monster_pain(qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  if (monster->definition->has_base_behavior && monster->definition->base_behavior) {
    monster->base.pain(monster->base.ctx, monster, attacker, damage);
    return;
  }
  qa_q1mpm_monster_retaliate(monster, attacker);
  monster->definition->pain(monster->definition->pain_ctx, monster, attacker, damage);
}
void qa_q1mpm_monster_pain_default(qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  monster->base.pain(monster->base.ctx, monster, attacker, damage);
}
void qa_q1mpm_monster_die_default(qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  monster->base.die(monster->base.ctx, monster, attacker);
}
void qa_q1mpm_monster_die(qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (qa_q1mpm_actor_number(entity, "charmed") != 0) entity->effects &= ~8;
  if (monster->definition->has_base_behavior && monster->definition->base_behavior) {
    monster->base.die(monster->base.ctx, monster, attacker);
    return;
  }
  if (monster->counted_death) return;
  monster->state.enemy = attacker;
  if (game->health(game, entity->actor.id) < -99)
    game->host->combat_set_health(game->host->ctx, entity->actor, -99);
  qa_q1mpm_actor_set_damageable(game, entity, false);
  entity->touch = (qa_q1mpm_callback){NULL, NULL, QA_Q1MPM_CB_TOUCH};
  qa_q1mpm_monster_count_kill(monster);
  monster->definition->die(monster->definition->die_ctx, monster, attacker);
}
void qa_q1mpm_monster_melee_attack(qa_q1mpm_mission_monster *monster) {
  if (monster->definition->has_base_behavior && monster->definition->base_behavior)
    monster->base.melee_attack(monster->base.ctx, monster);
  else if (monster->definition->melee.fn != NULL)
    monster->definition->melee.fn(monster->definition->melee.ctx, monster);
}
bool qa_q1mpm_monster_try_attack(qa_q1mpm_mission_monster *monster) {
  return monster->definition->check_attack == NULL
             ? monster->base.try_attack(monster->base.ctx, monster)
             : monster->definition->check_attack(monster->definition->check_attack_ctx, monster);
}
bool qa_q1mpm_monster_check_attack_default(qa_q1mpm_mission_monster *monster) {
  return monster->base.try_attack(monster->base.ctx, monster);
}
void qa_q1mpm_monster_found(qa_q1mpm_mission_monster *monster, qa_actor_id target) {
  qa_q1mpm_opt_actor owner = qa_q1mpm_charmer(monster);
  if (owner.has) {
    qa_q1mpm_actor *other = monster->game->entity(monster->game, QA_Q1MPM_SOME(target));
    qa_q1mpm_opt_actor other_charmer = other == NULL ? QA_Q1MPM_NONE
                                       : qa_q1mpm_references_get(other, "charmer");
    if (qa_same_actor(target, owner.id) || qa_q1mpm_opt_eq(other_charmer, owner)) {
      monster->state.enemy = QA_Q1MPM_NONE;
      return;
    }
  }
  if (monster->definition->found == NULL) qa_q1mpm_monster_found_default(monster, target);
  else monster->definition->found(monster->definition->found_ctx, monster, target);
}
void qa_q1mpm_monster_found_default(qa_q1mpm_mission_monster *monster, qa_actor_id target) {
  if (!qa_q1mpm_str_eq(monster->runtime->pack, "hipnotic")) {
    monster->base.found(monster->base.ctx, monster, target);
    return;
  }
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  const qa_q1mpm_species_spec *spec = &monster->definition->spec;
  monster->state.enemy = QA_Q1MPM_SOME(target);
  if (game->is_player(game, QA_Q1MPM_SOME(target))) { game->sight_entity = entity; game->sight_time = game->time; }
  char buf[32];
  qa_q1mpm_format_fround(buf, sizeof(buf), game->time + 1);
  qa_q1mpm_fields_set(entity, "show_hostile", buf);
  const char *sound = spec->sight;
  char enforcer[64];
  if (qa_q1mpm_str_eq(spec->species, "enforcer")) {
    double r = floor(game->host->random(game->host->ctx) * 3 + 0.5);
    snprintf(enforcer, sizeof(enforcer), "enforcer/sight%d.wav", r == 1 ? 1 : r == 2 ? 2 : r == 0 ? 3 : 4);
    sound = enforcer;
  } else if (qa_q1mpm_str_eq(spec->species, "fish") ||
             (qa_q1mpm_str_eq(spec->species, "gremlin") && qa_q1mpm_actor_number(entity, "stoleweapon") != 0)) {
    sound = "";
  }
  if (sound[0] != '\0') qa_q1mpm_game_sound(game, entity, sound);
  monster->state.mode = "run";
  qa_q1mpm_monster_set_next_frame(monster, spec->run);
  qa_q1mpm_references_set(entity, "goalentity", QA_Q1MPM_SOME(target));
  qa_q1mpm_body body;
  qa_vec3 origin = game->host->bodies_read(game->host->ctx, target, &body) ? body.origin : QA_Q1MPM_ZERO;
  entity->ideal_yaw = qa_q1mpm_yaw_for(qa_q1mpm_vsub(origin, qa_q1mpm_monster_origin(monster)));
  qa_q1mpm_monster_delay(monster, 0.1);
  qa_q1mpm_monster_attack_finished(monster, 1);
}
void qa_q1mpm_monster_ai(qa_q1mpm_mission_monster *monster, qa_q1mpm_ai mode, double distance) {
  if (mode == QA_Q1MPM_AI_WALK && qa_q1mpm_walk_with_charmer(monster, distance)) return;
  if (monster->definition->ai == NULL) monster->base.ai(monster->base.ctx, monster, mode, distance);
  else monster->definition->ai(monster->definition->ai_ctx, monster, mode, distance);
}
void qa_q1mpm_monster_ai_default(qa_q1mpm_mission_monster *monster, qa_q1mpm_ai mode, double distance) {
  monster->base.ai(monster->base.ctx, monster, mode, distance);
}
void qa_q1mpm_monster_run(qa_q1mpm_mission_monster *monster, double distance) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_opt_actor owner = qa_q1mpm_charmer(monster);
  if (owner.has && (!monster->state.enemy.has || qa_q1mpm_opt_eq(monster->state.enemy, owner) ||
                    game->health(game, monster->state.enemy.id) <= 0)) {
    monster->state.enemy = QA_Q1MPM_NONE;
    qa_q1mpm_hunt_charmer(monster, false);
    return;
  }
  if (!qa_q1mpm_str_eq(entity->attack_state, "dodging")) {
    monster->base.run(monster->base.ctx, monster, distance);
    return;
  }
  if (!monster->state.enemy.has || game->health(game, monster->state.enemy.id) <= 0) {
    entity->attack_state = "straight";
    monster->base.run(monster->base.ctx, monster, distance);
    return;
  }
  qa_vec3 target;
  if (!qa_q1mpm_monster_target(monster, &target)) return;
  bool seen = qa_q1mpm_monster_visible(monster);
  if (seen) monster->state.search_until = game->time + 5;
  if (qa_q1mpm_monster_search_for_coop_target(monster)) return;
  game->make_vectors(game, game->body(game, entity).angles);
  if (seen && qa_q1mpm_monster_try_attack(monster)) return;
  qa_q1mpm_monster_delay(monster, 0.1);
  double offset = monster->lefty ? 40 : -40;
  if (game->time > qa_q1mpm_actor_number(entity, "ltime")) {
    monster->lefty = !monster->lefty;
    char buf[32];
    qa_q1mpm_format_fround(buf, sizeof(buf), game->time + 0.8);
    qa_q1mpm_fields_set(entity, "ltime", buf);
  }
  entity->ideal_yaw = qa_q1mpm_yaw_for(qa_q1mpm_vsub(target, qa_q1mpm_monster_origin(monster)));
  if (game->host->walk_move(game->host->ctx, entity->actor, entity->ideal_yaw + offset, distance)) {
    qa_q1mpm_monster_change_yaw(monster);
    return;
  }
  monster->lefty = !monster->lefty;
  char buf[32];
  qa_q1mpm_format_fround(buf, sizeof(buf), game->time + 0.8);
  qa_q1mpm_fields_set(entity, "ltime", buf);
  game->host->walk_move(game->host->ctx, entity->actor, entity->ideal_yaw - offset, distance);
  qa_q1mpm_monster_change_yaw(monster);
}
void qa_q1mpm_monster_move_to_enemy(qa_q1mpm_mission_monster *monster, double distance) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (qa_q1mpm_str_eq(monster->runtime->pack, "hipnotic") && game->world != NULL &&
      qa_q1mpm_actor_number(game->world, "RUN_STRAIGHT") != 0 &&
      game->time > qa_q1mpm_actor_number(entity, "endtime")) {
    qa_q1mpm_fields_set(game->world, "RUN_STRAIGHT", "0");
    if (game->host->walk_move(game->host->ctx, entity->actor, game->body(game, entity).angles.y, distance))
      return;
    char buf[32];
    qa_q1mpm_format_fround(buf, sizeof(buf), game->time + 3);
    qa_q1mpm_fields_set(entity, "endtime", buf);
  }
  monster->base.move_to_enemy(monster->base.ctx, monster, distance);
}
bool qa_q1mpm_monster_search_for_coop_target(qa_q1mpm_mission_monster *monster) {
  return qa_q1mpm_actor_number(monster->entity, "charmed") == 0 &&
         monster->base.search_for_coop_target(monster->base.ctx, monster);
}
qa_q1mpm_basis qa_q1mpm_monster_make_vectors(qa_q1mpm_mission_monster *monster) {
  return monster->game->make_vectors(monster->game, monster->game->body(monster->game, monster->entity).angles);
}
bool qa_q1mpm_monster_find_target(qa_q1mpm_mission_monster *monster) {
  if (!qa_q1mpm_str_eq(monster->runtime->pack, "hipnotic"))
    return monster->base.find_target(monster->base.ctx, monster);
  bool is_null = false;
  bool found = qa_q1mpm_find_charmed_target(monster, &is_null);
  return is_null ? qa_q1mpm_find_hipnotic_target(monster) : found;
}
void qa_q1mpm_monster_retaliate(qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  qa_q1mpm_opt_actor owner = qa_q1mpm_charmer(monster);
  if (!(attacker.has && owner.has && qa_same_actor(attacker.id, owner.id)))
    monster->base.retaliate(monster->base.ctx, monster, attacker);
}
void qa_q1mpm_monster_use(qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor activator) {
  if (monster->definition->use == NULL) monster->base.use(monster->base.ctx, monster, activator);
  else monster->definition->use(monster->definition->use_ctx, monster, activator);
}
void qa_q1mpm_monster_delay(qa_q1mpm_mission_monster *monster, double seconds) {
  monster->base.delay(monster->base.ctx, monster, seconds);
}
void qa_q1mpm_monster_face(qa_q1mpm_mission_monster *monster) {
  monster->base.face(monster->base.ctx, monster);
}
void qa_q1mpm_monster_change_yaw(qa_q1mpm_mission_monster *monster) {
  monster->base.change_yaw(monster->base.ctx, monster);
}
void qa_q1mpm_monster_attack_finished(qa_q1mpm_mission_monster *monster, double seconds) {
  monster->base.attack_finished(monster->base.ctx, monster, seconds);
}
bool qa_q1mpm_monster_visible(qa_q1mpm_mission_monster *monster) {
  return monster->base.visible(monster->base.ctx, monster, QA_Q1MPM_NONE, false);
}
bool qa_q1mpm_monster_visible_actor(qa_q1mpm_mission_monster *monster, qa_actor_id target) {
  return monster->base.visible(monster->base.ctx, monster, QA_Q1MPM_SOME(target), true);
}
bool qa_q1mpm_monster_eye(qa_q1mpm_mission_monster *monster, qa_vec3 *out) {
  return monster->base.eye(monster->base.ctx, monster, monster->entity->actor.id, out);
}
bool qa_q1mpm_monster_eye_actor(qa_q1mpm_mission_monster *monster, qa_actor_id target, qa_vec3 *out) {
  return monster->base.eye(monster->base.ctx, monster, target, out);
}
double qa_q1mpm_monster_range_distance(qa_q1mpm_mission_monster *monster) {
  return monster->base.range_distance(monster->base.ctx, monster, QA_Q1MPM_NONE, false);
}
double qa_q1mpm_monster_range_distance_actor(qa_q1mpm_mission_monster *monster, qa_actor_id target) {
  return monster->base.range_distance(monster->base.ctx, monster, QA_Q1MPM_SOME(target), true);
}
void qa_q1mpm_monster_count_kill(qa_q1mpm_mission_monster *monster) {
  monster->base.count_kill(monster->base.ctx, monster);
}
qa_save_value *qa_q1mpm_monster_capture(qa_q1mpm_mission_monster *monster) {
  return monster->base.capture(monster->base.ctx, monster);
}
void qa_q1mpm_monster_restore(qa_q1mpm_mission_monster *monster, const qa_save_reader *reader) {
  monster->base.restore(monster->base.ctx, monster, reader);
}

static bool owned_eq(qa_owned_actor a, qa_owned_actor b) { return qa_same_actor(a.id, b.id); }
qa_q1mpm_mission_monster *qa_q1mpm_runtime_get(qa_q1mpm_runtime *runtime, qa_owned_actor actor) {
  for (size_t i = 0; i < runtime->monster_count; i++)
    if (owned_eq(runtime->monsters[i].actor, actor)) return runtime->monsters[i].monster;
  return NULL;
}
static void runtime_delete_monster(qa_q1mpm_runtime *runtime, qa_owned_actor actor) {
  for (size_t i = 0; i < runtime->monster_count; i++) {
    if (!owned_eq(runtime->monsters[i].actor, actor)) continue;
    qa_q1mpm_mission_monster_destroy(runtime->monsters[i].monster);
    free(runtime->monsters[i].monster);
    runtime->monsters[i] = runtime->monsters[runtime->monster_count - 1];
    runtime->monster_count--;
    return;
  }
}
bool qa_q1mpm_runtime_set_monster(qa_q1mpm_runtime *runtime, qa_owned_actor actor,
                                  qa_q1mpm_mission_monster *monster) {
  for (size_t i = 0; i < runtime->monster_count; i++) {
    if (!owned_eq(runtime->monsters[i].actor, actor)) continue;
    if (runtime->monsters[i].monster != monster) {
      qa_q1mpm_mission_monster_destroy(runtime->monsters[i].monster);
      free(runtime->monsters[i].monster);
      runtime->monsters[i].monster = monster;
    }
    return true;
  }
  if (runtime->monster_count == runtime->monster_capacity) {
    size_t capacity = runtime->monster_capacity == 0 ? 16 : runtime->monster_capacity * 2;
    qa_q1mpm_monster_entry *next = realloc(runtime->monsters, capacity * sizeof(*next));
    if (next == NULL) return false;
    runtime->monsters = next;
    runtime->monster_capacity = capacity;
  }
  runtime->monsters[runtime->monster_count].actor = actor;
  runtime->monsters[runtime->monster_count].monster = monster;
  runtime->monster_count++;
  return true;
}
static const qa_q1mpm_pack_monster_definition *find_definition(qa_q1mpm_runtime *runtime,
                                                               const char *classname) {
  for (size_t i = 0; i < runtime->definition_count; i++)
    if (strcmp(runtime->definitions[i].classname, classname) == 0) return runtime->definitions[i].definition;
  return NULL;
}
static bool add_definition(qa_q1mpm_runtime *runtime, const char *classname,
                           const qa_q1mpm_pack_monster_definition *definition) {
  if (runtime->definition_count == runtime->definition_capacity) {
    size_t capacity = runtime->definition_capacity == 0 ? 16 : runtime->definition_capacity * 2;
    qa_q1mpm_definition_entry *next = realloc(runtime->definitions, capacity * sizeof(*next));
    if (next == NULL) return false;
    runtime->definitions = next;
    runtime->definition_capacity = capacity;
  }
  char *copy = dup_text(classname);
  if (copy == NULL) return false;
  runtime->definitions[runtime->definition_count].classname = copy;
  runtime->definitions[runtime->definition_count].definition = definition;
  runtime->definition_count++;
  return true;
}
static void on_release(void *ctx, qa_owned_actor actor) {
  runtime_delete_monster((qa_q1mpm_runtime *)ctx, actor);
}
static void spawn_trampoline(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity) {
  (void)game;
  qa_q1mpm_runtime_spawn((qa_q1mpm_runtime *)ctx, entity);
}
static qa_q1mpm_mission_monster *require_lookup(void *ctx, qa_q1mpm_actor *entity) {
  return qa_q1mpm_runtime_require((qa_q1mpm_runtime *)ctx, entity);
}
static void synthetic_pain(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker,
                           double damage) {
  (void)ctx;
  qa_q1mpm_monster_pain_default(monster, attacker, damage);
}
static void synthetic_die(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  (void)ctx;
  qa_q1mpm_monster_die_default(monster, attacker);
}
static uint8_t *runtime_capture(void *ctx, size_t *out_length);
static void runtime_restore(void *ctx, const uint8_t *bytes, size_t length);
static void runtime_clone(void *ctx, qa_q1mpm_actor *source, qa_q1mpm_actor *target);

bool qa_q1mpm_runtime_init(qa_q1mpm_runtime *runtime, qa_q1mpm_game *game, qa_q1mpm_q1base *base,
                           const char *pack, const qa_q1mpm_mission_monster_hooks *hooks) {
  memset(runtime, 0, sizeof(*runtime));
  runtime->game = game;
  runtime->base = base;
  runtime->pack = pack;
  if (hooks != NULL) runtime->hooks = *hooks;
  qa_q1mpm_ext_register_monster_callbacks(game, pack, require_lookup, runtime);
  if (qa_q1mpm_str_eq(pack, "hipnotic") && qa_q1mpm_base_species != NULL) {
    runtime->synthetic = calloc(qa_q1mpm_base_species_count, sizeof(*runtime->synthetic));
    if (runtime->synthetic == NULL && qa_q1mpm_base_species_count > 0) return false;
    runtime->synthetic_count = qa_q1mpm_base_species_count;
    for (size_t i = 0; i < qa_q1mpm_base_species_count; i++) {
      const qa_q1mpm_species_spec *spec = &qa_q1mpm_base_species[i];
      qa_q1mpm_pack_monster_definition *definition = &runtime->synthetic[i];
      definition->spec = *spec;
      definition->has_base_behavior = true;
      definition->base_behavior = true;
      definition->pain = synthetic_pain;
      definition->die = synthetic_die;
      for (size_t j = 0; j < spec->classname_count; j++) {
        if (!add_definition(runtime, spec->classnames[j], definition)) return false;
        if (!qa_q1mpm_str_eq(spec->movement, "boss"))
          game->replace_spawn(game, spec->classnames[j], spawn_trampoline, runtime);
      }
    }
  }
  game->host->actors_on_release(game->host->ctx, on_release, runtime);
  char id[96];
  snprintf(id, sizeof(id), "q1:%s:monsters", pack);
  /* TS extension id string is built per pack; the registry copies it. */
  char *id_copy = dup_text(id);
  if (id_copy == NULL) return false;
  qa_q1mpm_state_extension extension = {id_copy, runtime, runtime_capture, runtime_restore, runtime_clone};
  game->register_state_extension(game, &extension);
  free(id_copy);
  return true;
}
void qa_q1mpm_runtime_destroy(qa_q1mpm_runtime *runtime) {
  for (size_t i = 0; i < runtime->monster_count; i++) {
    qa_q1mpm_mission_monster_destroy(runtime->monsters[i].monster);
    free(runtime->monsters[i].monster);
  }
  free(runtime->monsters);
  for (size_t i = 0; i < runtime->definition_count; i++) free(runtime->definitions[i].classname);
  free(runtime->definitions);
  free(runtime->synthetic);
  free(runtime->owned_defs);
  memset(runtime, 0, sizeof(*runtime));
}
qa_q1mpm_mission_monster *qa_q1mpm_runtime_require(qa_q1mpm_runtime *runtime, qa_q1mpm_actor *entity) {
  qa_q1mpm_mission_monster *monster = qa_q1mpm_runtime_get(runtime, entity->actor);
  if (monster == NULL)
    qa_range_error("Missing %s monster controller for %s", runtime->pack, entity->classname);
  return monster;
}
qa_q1mpm_mission_monster *qa_q1mpm_runtime_context(qa_q1mpm_runtime *runtime, qa_actor_id actor) {
  qa_owned_actor owner;
  if (!runtime->game->host->actors_resolve_owned(runtime->game->host->ctx, actor, &owner)) return NULL;
  return qa_q1mpm_runtime_get(runtime, owner);
}
bool qa_q1mpm_runtime_register(qa_q1mpm_runtime *runtime,
                               const qa_q1mpm_pack_monster_definition *definition) {
  qa_q1mpm_game *game = runtime->game;
  for (size_t i = 0; i < definition->callback_count; i++) {
    char name[192];
    snprintf(name, sizeof(name), "%s:%s", runtime->pack, definition->callbacks[i].name);
    game->named.register_named(game->named.ctx, name, &definition->callbacks[i].handlers);
  }
  for (size_t i = 0; i < definition->spec.classname_count; i++) {
    const char *classname = definition->spec.classnames[i];
    if (find_definition(runtime, classname) != NULL)
      qa_range_error("Duplicate %s monster %s", runtime->pack, classname);
    if (!add_definition(runtime, classname, definition)) return false;
    game->register_spawn(game, classname, spawn_trampoline, runtime);
  }
  return true;
}
void qa_q1mpm_runtime_spawn(qa_q1mpm_runtime *runtime, qa_q1mpm_actor *entity) {
  qa_q1mpm_game *game = runtime->game;
  if (game->options.deathmatch != 0) { game->remove(game, entity); return; }
  const qa_q1mpm_pack_monster_definition *definition = find_definition(runtime, entity->classname);
  if (definition == NULL) qa_range_error("Unknown %s monster %s", runtime->pack, entity->classname);
  qa_q1mpm_mission_monster *monster = calloc(1, sizeof(*monster));
  if (monster == NULL) return;
  qa_q1mpm_mission_monster_init(monster, game, entity, definition, runtime);
  if (!qa_q1mpm_runtime_set_monster(runtime, entity->actor, monster)) {
    qa_q1mpm_mission_monster_destroy(monster);
    free(monster);
    return;
  }
  qa_q1mpm_monster_spawn(monster);
}
static const char *charm_name(qa_q1mpm_runtime *runtime, qa_q1mpm_callback callback, char *buf, size_t size) {
  const char *source = qa_q1mpm_callback_name(callback);
  if (source == NULL) return NULL;
  if (strncmp(source, "base:", 5) == 0) {
    snprintf(buf, size, "%s:%s", runtime->pack, source + 5);
    return buf;
  }
  return source;
}
void qa_q1mpm_runtime_charm(qa_q1mpm_runtime *runtime, qa_q1mpm_actor *entity, qa_actor_id owner) {
  qa_q1mpm_game *game = runtime->game;
  qa_q1mpm_references_set(entity, "charmer", QA_Q1MPM_SOME(owner));
  qa_q1mpm_fields_set(entity, "charmed", "1");
  if (qa_q1mpm_runtime_get(runtime, entity->actor) != NULL) return;
  qa_save_value *previous = runtime->base->monsters_capture(runtime->base->ctx, entity->actor);
  const qa_q1mpm_pack_monster_definition *definition = find_definition(runtime, entity->classname);
  if (previous == NULL || definition == NULL) {
    qa_save_value_free(previous);
    qa_range_error("Missing source charm controller for %s", entity->classname);
  }
  qa_q1mpm_mission_monster *monster = calloc(1, sizeof(*monster));
  if (monster == NULL) { qa_save_value_free(previous); return; }
  qa_q1mpm_mission_monster_init(monster, game, entity, definition, runtime);
  qa_save_reader reader;
  if (qa_save_reader_init(&reader, previous, "hipnotic:charm")) {
    qa_q1mpm_monster_restore(monster, &reader);
    qa_save_reader_destroy(&reader);
  }
  qa_save_value_free(previous);
  runtime->base->monsters_delete(runtime->base->ctx, entity->actor);
  qa_q1mpm_runtime_set_monster(runtime, entity->actor, monster);
  char b1[192], b2[192], b3[192], b4[192], b5[192], b6[192];
  const char *think = charm_name(runtime, entity->think, b1, sizeof(b1));
  const char *use = charm_name(runtime, entity->use, b2, sizeof(b2));
  const char *pain = charm_name(runtime, entity->pain, b3, sizeof(b3));
  const char *die = charm_name(runtime, entity->die, b4, sizeof(b4));
  const char *touch = charm_name(runtime, entity->touch, b5, sizeof(b5));
  const char *path_end = charm_name(runtime, entity->path_end, b6, sizeof(b6));
  qa_q1mpm_callback none_action = {NULL, NULL, QA_Q1MPM_CB_ACTION};
  qa_q1mpm_callback none_use = {NULL, NULL, QA_Q1MPM_CB_USE};
  qa_q1mpm_callback none_pain = {NULL, NULL, QA_Q1MPM_CB_PAIN};
  qa_q1mpm_callback none_die = {NULL, NULL, QA_Q1MPM_CB_DIE};
  qa_q1mpm_callback none_touch = {NULL, NULL, QA_Q1MPM_CB_TOUCH};
  entity->think = think == NULL ? none_action : game->named.action(game->named.ctx, entity, think);
  entity->use = use == NULL ? none_use : game->named.use(game->named.ctx, entity, use);
  entity->pain = pain == NULL ? none_pain : game->named.pain(game->named.ctx, entity, pain);
  entity->die = die == NULL ? none_die : game->named.die(game->named.ctx, entity, die);
  entity->touch = touch == NULL ? none_touch : game->named.touch(game->named.ctx, entity, touch);
  entity->path_end = path_end == NULL ? none_action : game->named.action(game->named.ctx, entity, path_end);
}
static void runtime_clone(void *ctx, qa_q1mpm_actor *source, qa_q1mpm_actor *target) {
  qa_q1mpm_runtime *runtime = ctx;
  qa_q1mpm_mission_monster *original = qa_q1mpm_runtime_get(runtime, source->actor);
  if (original == NULL) return;
  qa_q1mpm_mission_monster *monster = calloc(1, sizeof(*monster));
  if (monster == NULL) return;
  qa_q1mpm_mission_monster_init(monster, runtime->game, target, original->definition, runtime);
  qa_save_value *state = qa_q1mpm_monster_capture(original);
  char path[128];
  snprintf(path, sizeof(path), "%s:monster-clone", runtime->pack);
  qa_save_reader reader;
  if (qa_save_reader_init(&reader, state, path)) {
    qa_q1mpm_monster_restore(monster, &reader);
    qa_save_reader_destroy(&reader);
  }
  qa_save_value_free(state);
  qa_q1mpm_runtime_set_monster(runtime, target->actor, monster);
}
static uint8_t *runtime_capture(void *ctx, size_t *out_length) {
  qa_q1mpm_runtime *runtime = ctx;
  qa_save_value *root = qa_save_value_object();
  qa_save_value *list = qa_save_value_array();
  if (root == NULL || list == NULL) { qa_save_value_free(root); qa_save_value_free(list); return NULL; }
  for (size_t i = 0; i < runtime->monster_count; i++) {
    qa_q1mpm_mission_monster *monster = runtime->monsters[i].monster;
    qa_save_value *entry = qa_save_value_object();
    qa_save_value *actor = qa_save_value_object();
    if (entry == NULL || actor == NULL) { qa_save_value_free(entry); qa_save_value_free(actor); continue;
    }
    qa_save_value_set(actor, "slot", qa_save_value_number((double)monster->entity->actor.id.slot));
    qa_save_value_set(actor, "generation", qa_save_value_number((double)monster->entity->actor.id.generation));
    qa_save_value_set(entry, "actor", actor);
    const char *definition = monster->definition->spec.classname_count > 0
                                 ? monster->definition->spec.classnames[0]
                                 : monster->entity->classname;
    qa_save_value_set(entry, "definition", qa_save_value_string(definition));
    qa_save_value_set(entry, "state", qa_q1mpm_monster_capture(monster));
    qa_save_value_push(list, entry);
  }
  qa_save_value_set(root, "version", qa_save_value_number(1));
  qa_save_value_set(root, "authoredGremlins", qa_save_value_number(runtime->authored_gremlins));
  qa_save_value_set(root, "spawnedGremlins", qa_save_value_number(runtime->spawned_gremlins));
  qa_save_value_set(root, "monsters", list);
  uint8_t *bytes = qa_encode_checkpoint_value(root, out_length);
  qa_save_value_free(root);
  return bytes;
}
static double reader_number_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) return 0;
  double value = qa_save_reader_number(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static void runtime_restore(void *ctx, const uint8_t *bytes, size_t length) {
  qa_q1mpm_runtime *runtime = ctx;
  qa_q1mpm_game *game = runtime->game;
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  char path[128];
  snprintf(path, sizeof(path), "q1:%s:monsters", runtime->pack);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, decoded, path)) { qa_save_value_free(decoded); return; }
  qa_save_reader version;
  if (qa_save_reader_field(&reader, "version", &version)) {
    qa_save_reader_literal_number(&version, 1);
    qa_save_reader_destroy(&version);
  }
  runtime->authored_gremlins = reader_number_field(&reader, "authoredGremlins");
  runtime->spawned_gremlins = reader_number_field(&reader, "spawnedGremlins");
  while (runtime->monster_count > 0) runtime_delete_monster(runtime, runtime->monsters[0].actor);
  qa_save_reader monsters;
  if (qa_save_reader_field(&reader, "monsters", &monsters)) {
    size_t count = qa_save_reader_list_count(&monsters);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader saved;
      if (!qa_save_reader_item(&monsters, i, &saved)) continue;
      qa_save_reader id;
      int64_t slot = 0, generation = 0;
      if (qa_save_reader_field(&saved, "actor", &id)) {
        qa_save_reader slot_reader, generation_reader;
        if (qa_save_reader_field(&id, "slot", &slot_reader)) {
          slot = (int64_t)qa_save_reader_integer_min(&slot_reader, 0);
          qa_save_reader_destroy(&slot_reader);
        }
        if (qa_save_reader_field(&id, "generation", &generation_reader)) {
          generation = (int64_t)qa_save_reader_integer_min(&generation_reader, 0);
          qa_save_reader_destroy(&generation_reader);
        }
        qa_save_reader_destroy(&id);
      }
      qa_owned_actor actor;
      if (!game->host->actors_resolve_saved(game->host->ctx, slot, generation, &actor))
        qa_save_reader_fail(&saved, "missing saved monster actor");
      qa_q1mpm_actor *entity = game->entity(game, QA_Q1MPM_SOME(actor.id));
      if (entity == NULL) qa_save_reader_fail(&saved, "missing saved monster source entity");
      qa_save_reader definition_reader;
      const char *definition_name = NULL;
      if (qa_save_reader_field(&saved, "definition", &definition_reader)) {
        definition_name = qa_save_reader_string(&definition_reader);
        qa_save_reader_destroy(&definition_reader);
      }
      const qa_q1mpm_pack_monster_definition *definition =
          definition_name == NULL ? NULL : find_definition(runtime, definition_name);
      if (definition == NULL) qa_save_reader_fail(&saved, "unknown saved mission pack monster");
      qa_q1mpm_mission_monster *monster = calloc(1, sizeof(*monster));
      if (monster != NULL) {
        qa_q1mpm_mission_monster_init(monster, game, entity, definition, runtime);
        qa_save_reader state;
        if (qa_save_reader_field(&saved, "state", &state)) {
          qa_q1mpm_monster_restore(monster, &state);
          qa_save_reader_destroy(&state);
        }
        qa_q1mpm_runtime_set_monster(runtime, actor, monster);
      }
      qa_save_reader_destroy(&saved);
    }
    qa_save_reader_destroy(&monsters);
  }
  qa_save_reader_destroy(&reader);
  qa_save_value_free(decoded);
}
