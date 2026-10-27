/* Port of src/content/q1/foundation/entity.ts. GPL-2.0-or-later. */
#include "qa/content/q1/foundation/entity.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q1/foundation/text.h"

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
  double value = strtod(text, NULL); /* parseFloat prefix semantics */
  return isfinite(value) ? (double)froundd(value) : fallback;
}
double qa_q1_actor_number(const qa_q1_actor *entity, const char *key) {
  return qa_q1_actor_number_or(entity, key, 0);
}
qa_vec3 qa_q1_parse_vector(const char *text) {
  double numbers[3] = {0, 0, 0};
  const char *cursor = text;
  for (int i = 0; i < 3; i++) {
    while (*cursor && isspace((unsigned char)*cursor)) cursor++;
    if (!*cursor) break;
    char *end = NULL;
    numbers[i] = strtod(cursor, &end);
    if (end == cursor) { numbers[i] = NAN; break; }
    cursor = end;
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
  qa_vec3 result = {0, angle ? strtod(angle, NULL) : 0, 0}; /* TS Number(...) */
  return result;
}
qa_vec3 qa_q1_move_direction(qa_vec3 angles, qa_q1_services *game) {
  if (angles.y == -1 && angles.x == 0 && angles.z == 0) { qa_vec3 up = {0, 0, 1}; return up; }
  if (angles.y == -2 && angles.x == 0 && angles.z == 0) { qa_vec3 down = {0, 0, -1}; return down; }
  return (game == NULL ? qa_q1_vectors(angles) : qa_q1_make_vectors(game, angles)).forward;
}

bool qa_q1_actor_init(qa_q1_actor *entity, qa_owned_actor actor, const char *classname,
                      const double *source_ordinal, const qa_q1_entity *source) {
  memset(entity, 0, sizeof *entity);
  entity->actor = actor;
  entity->has_source_ordinal = source_ordinal != NULL;
  if (source_ordinal) entity->source_ordinal = *source_ordinal;
  entity->solid = "none"; entity->movement = "none";
  entity->next_think = -1;
  entity->state = "bottom"; entity->attack_state = "straight";
  entity->yaw_speed = 20; entity->live = true;
  entity->body.bounds = QA_Q1_POINT;
  entity->classname = dupstr(classname);
  if (!entity->classname) return false;
  if (source) {
    for (size_t i = 0; i < source->property_count; i++) {
      char buffer[1024];
      qa_q1_entity_string(source->properties[i].value, buffer, sizeof buffer);
      if (!qa_q1_actor_field_set(entity, source->properties[i].key, buffer)) { qa_q1_actor_free(entity); return false; }
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
  entity->health = entity->max_health;
  return true;
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
