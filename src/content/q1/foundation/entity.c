/* Port of src/content/q1/foundation/entity.ts. GPL-2.0-or-later. */
#include "qa/content/q1/foundation/entity.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q1/foundation/text.h"
#include "qa/foundation/js.h"

const qa_q1_monster_species QA_Q1_MONSTER_SPECIES[28] = {
  "army", "dog", "knight", "enforcer", "demon", "ogre", "hellknight", "shambler", "wizard",
  "shalrath", "tarbaby", "fish", "zombie", "boss", "oldone", "gremlin", "scourge", "armagon",
  "spikemine", "decoy", "eel", "sword", "wrath", "super-wrath", "mummy", "lava-man", "morph", "dragon"};

static char *dupstr(const char *text) {
  size_t len = strlen(text) + 1;
  char *copy = malloc(len);
  if (copy) memcpy(copy, text, len);
  return copy;
}
static float froundd(double value) { return (float)value; }

bool qa_q1_actor_field_set(qa_q1_actor *entity, const char *key, const char *value) {
  for (size_t i = 0; i < entity->field_count; i++) if (!strcmp(entity->fields[i].key, key)) {
    char *copy = dupstr(value); if (!copy) return false;
    free(entity->fields[i].value); entity->fields[i].value = copy; return true;
  }
  if (entity->field_count == entity->field_capacity) {
    size_t capacity = entity->field_capacity ? entity->field_capacity * 2 : 8;
    qa_q1_field *grown = realloc(entity->fields, capacity * sizeof *grown);
    if (!grown) return false;
    entity->fields = grown; entity->field_capacity = capacity;
  }
  char *k = dupstr(key), *v = dupstr(value);
  if (!k || !v) { free(k); free(v); return false; }
  entity->fields[entity->field_count].key = k;
  entity->fields[entity->field_count].value = v;
  entity->field_count++;
  return true;
}
const char *qa_q1_actor_field_get(const qa_q1_actor *entity, const char *key) {
  for (size_t i = 0; i < entity->field_count; i++)
    if (!strcmp(entity->fields[i].key, key)) return entity->fields[i].value;
  return NULL;
}
bool qa_q1_actor_field_delete(qa_q1_actor *entity, const char *key) {
  for (size_t i = 0; i < entity->field_count; i++)
    if (!strcmp(entity->fields[i].key, key)) {
      free(entity->fields[i].key);
      free(entity->fields[i].value);
      /* Map deletion keeps the remaining insertion order. */
      memmove(&entity->fields[i], &entity->fields[i + 1], (entity->field_count - i - 1) * sizeof *entity->fields);
      entity->field_count--;
      return true;
    }
  return false;
}
bool qa_q1_actor_reference_set(qa_q1_actor *entity, const char *key, const qa_actor_id *actor) {
  for (size_t i = 0; i < entity->reference_count; i++) if (!strcmp(entity->references[i].key, key)) {
    entity->references[i].has_actor = actor != NULL;
    if (actor) entity->references[i].actor = *actor;
    return true;
  }
  if (entity->reference_count == entity->reference_capacity) {
    size_t capacity = entity->reference_capacity ? entity->reference_capacity * 2 : 4;
    qa_q1_reference *grown = realloc(entity->references, capacity * sizeof *grown);
    if (!grown) return false;
    entity->references = grown; entity->reference_capacity = capacity;
  }
  char *k = dupstr(key); if (!k) return false;
  entity->references[entity->reference_count].key = k;
  entity->references[entity->reference_count].has_actor = actor != NULL;
  if (actor) entity->references[entity->reference_count].actor = *actor;
  entity->reference_count++;
  return true;
}
bool qa_q1_actor_reference_get(const qa_q1_actor *entity, const char *key, qa_actor_id *out) {
  for (size_t i = 0; i < entity->reference_count; i++)
    if (!strcmp(entity->references[i].key, key) && entity->references[i].has_actor) {
      if (out) *out = entity->references[i].actor;
      return true;
    }
  return false;
}
const char *qa_q1_actor_text(const qa_q1_actor *entity, const char *key) {
  const char *value = qa_q1_actor_field_get(entity, key);
  return value ? value : "";
}
double qa_q1_actor_number_or(const qa_q1_actor *entity, const char *key, double fallback) {
  const char *text = qa_q1_actor_text(entity, key);
  if (text[0] == '\0') return fallback;
  double value = qa_js_parse_float(text); /* Number.parseFloat(text) */
  return isfinite(value) ? (double)froundd(value) : fallback;
}
double qa_q1_actor_number(const qa_q1_actor *entity, const char *key) {
  return qa_q1_actor_number_or(entity, key, 0);
}
qa_vec3 qa_q1_parse_vector(const char *text) {
  /* text.trim().split(/\s+/u).map(Number.parseFloat): every token parses on its own;
   * strtod chained tokens, so "1,2 3 4" or a bad first token lost the rest. */
  double numbers[3] = {NAN, NAN, NAN};
  const char *cursor = text;
  for (int i = 0; i < 3; i++) {
    size_t space;
    while ((space = qa_js_white_space_length(cursor)) != 0) cursor += space;
    if (*cursor == '\0') break;
    numbers[i] = qa_js_parse_float(cursor);
    while (*cursor != '\0' && qa_js_white_space_length(cursor) == 0) cursor++;
  }
  qa_vec3 result = {isfinite(numbers[0]) ? (double)froundd(numbers[0]) : 0,
                    isfinite(numbers[1]) ? (double)froundd(numbers[1]) : 0,
                    isfinite(numbers[2]) ? (double)froundd(numbers[2]) : 0};
  return result;
}
qa_vec3 qa_q1_actor_vector(const qa_q1_actor *entity, const char *key) {
  return qa_q1_parse_vector(qa_q1_actor_text(entity, key));
}
qa_vec3 qa_q1_source_angles(const qa_q1_entity *source) {
  const char *angles = qa_q1_entity_value(source, "angles");
  if (angles) return qa_q1_parse_vector(angles);
  const char *angle = qa_q1_entity_value(source, "angle");
  qa_vec3 result = {0, angle ? qa_js_string_to_number(angle) : 0, 0}; /* Number(angle ?? 0) */
  return result;
}
qa_vec3 qa_q1_move_direction(qa_vec3 angles, qa_q1_services *game) {
  if (angles.y == -1 && angles.x == 0 && angles.z == 0) { qa_vec3 up = {0, 0, 1}; return up; }
  if (angles.y == -2 && angles.x == 0 && angles.z == 0) { qa_vec3 down = {0, 0, -1}; return down; }
  return (game == NULL ? qa_q1_vectors(angles) : qa_q1_make_vectors(game, angles)).forward;
}

bool qa_q1_actor_init(qa_q1_actor *entity, qa_owned_actor actor, const char *classname,
                      const double *source_ordinal, qa_gameplay_authority *combat,
                      const qa_q1_entity *source) {
  memset(entity, 0, sizeof *entity);
  entity->actor = actor;
  entity->has_source_ordinal = source_ordinal != NULL;
  if (source_ordinal) entity->source_ordinal = *source_ordinal;
  entity->solid = "none"; entity->movement = "none";
  entity->next_think = -1;
  entity->state = "bottom"; entity->attack_state = "straight";
  entity->yaw_speed = 20;
  entity->combat = combat;
  entity->classname = dupstr(classname);
  if (!entity->classname) return false;
  if (source) {
    for (size_t i = 0; i < source->property_count; i++) {
      /* TS q1EntityString has no length limit; unescaping never lengthens the
       * value, so a buffer of the source length holds it. */
      const char *value = source->properties[i].value;
      char *buffer = malloc(strlen(value) + 1);
      if (!buffer) { qa_q1_actor_free(entity); return false; }
      qa_q1_entity_string(value, buffer, strlen(value) + 1);
      bool stored = qa_q1_actor_field_set(entity, source->properties[i].key, buffer);
      free(buffer);
      if (!stored) { qa_q1_actor_free(entity); return false; }
    }
  }
  entity->model = dupstr(qa_q1_actor_text(entity, "model"));
  entity->original_model = dupstr(entity->model ? entity->model : "");
  entity->target = dupstr(qa_q1_actor_text(entity, "target"));
  entity->targetname = dupstr(qa_q1_actor_text(entity, "targetname"));
  entity->killtarget = dupstr(qa_q1_actor_text(entity, "killtarget"));
  entity->message = dupstr(qa_q1_actor_text(entity, "message"));
  if (!entity->model || !entity->original_model || !entity->target || !entity->targetname ||
      !entity->killtarget || !entity->message) { qa_q1_actor_free(entity); return false; }
  entity->delay = qa_q1_actor_number(entity, "delay");
  entity->spawnflags = qa_q1_actor_number(entity, "spawnflags");
  entity->sounds = qa_q1_actor_number(entity, "sounds");
  entity->wait = qa_q1_actor_number(entity, "wait");
  entity->speed = qa_q1_actor_number(entity, "speed");
  entity->damage = qa_q1_actor_number(entity, "dmg");
  entity->max_health = qa_q1_actor_number(entity, "health");
  entity->count = qa_q1_actor_number(entity, "count");
  return true;
}
qa_q1_body qa_q1_body_from_state(qa_body_state state) {
  qa_q1_body body;
  memset(&body, 0, sizeof body);
  body.origin = state.origin; body.angles = state.angles; body.velocity = state.velocity;
  body.bounds = state.bounds;
  body.has_ground = state.ground != NULL;
  if (state.ground) body.ground = *state.ground;
  return body;
}
qa_body_state qa_q1_body_to_state(const qa_q1_body *body) {
  qa_body_state state;
  state.origin = body->origin; state.angles = body->angles; state.velocity = body->velocity;
  state.bounds = body->bounds;
  state.ground = body->has_ground ? &body->ground : NULL;
  return state;
}
bool qa_q1_actor_damageable(const qa_q1_actor *entity) {
  const qa_combat_state_result state = qa_gameplay_authority_read(entity->combat, entity->actor.id);
  return state.has_value ? state.value.can_take_damage : false;
}
void qa_q1_actor_set_damageable(qa_q1_actor *entity, bool value) {
  qa_combat_trait_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_can_take_damage = true;
  changes.can_take_damage = value;
  qa_gameplay_authority_set_traits(entity->combat, entity->actor, changes);
}
void qa_q1_actor_free(qa_q1_actor *entity) {
  if (!entity) return;
  free(entity->classname); free(entity->model); free(entity->original_model);
  free(entity->target); free(entity->targetname); free(entity->killtarget); free(entity->message);
  for (size_t i = 0; i < entity->field_count; i++) { free(entity->fields[i].key); free(entity->fields[i].value); }
  free(entity->fields);
  for (size_t i = 0; i < entity->reference_count; i++) free(entity->references[i].key);
  free(entity->references);
  free(entity->door_group);
  if (entity->monster) { free(entity->monster->path); free(entity->monster); }
  memset(entity, 0, sizeof *entity);
}
