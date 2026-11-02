/* Implementations for include/qa/content/q1/base/foundation_types.h
 * (mirrors of src/content/q1/foundation entity/types/movers/monsters
 * helpers). GPL-2.0-or-later. */
#include "qa/content/q1/base/foundation_types.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/foundation/support.h"

qa_q1b_opt_actor qa_q1b_some_actor(qa_actor_id id) {
  qa_q1b_opt_actor value = {true, id};
  return value;
}
qa_q1b_channel qa_q1b_channel_named(const char *name) {
  qa_q1b_channel value = {name, 0};
  return value;
}
qa_q1b_channel qa_q1b_channel_numeric(double numeric) {
  qa_q1b_channel value = {NULL, numeric};
  return value;
}

bool qa_q1b_owned_equal(qa_owned_actor a, qa_owned_actor b) {
  if (!qa_same_actor(a.id, b.id)) return false;
  if (a.token != b.token) return false;
  if (a.owner == b.owner) return true;
  if (a.owner == NULL || b.owner == NULL) return false;
  return strcmp(a.owner, b.owner) == 0;
}

void qa_q1b_action_call(const qa_q1b_action *cb) {
  if (cb->has && cb->fn != NULL) cb->fn(cb->ctx, cb->game, cb->entity);
}
void qa_q1b_use_call(const qa_q1b_use *cb, qa_q1b_opt_actor other, qa_q1b_opt_actor activator) {
  if (cb->has && cb->fn != NULL) cb->fn(cb->ctx, cb->game, cb->entity, other, activator);
}
void qa_q1b_touch_call(const qa_q1b_touch *cb, qa_actor_id other, bool has_normal, qa_vec3 normal, const void *surface) {
  if (cb->has && cb->fn != NULL) cb->fn(cb->ctx, cb->game, cb->entity, other, has_normal, normal, surface);
}
void qa_q1b_pain_call(const qa_q1b_pain *cb, qa_q1b_opt_actor attacker, double damage) {
  if (cb->has && cb->fn != NULL) cb->fn(cb->ctx, cb->game, cb->entity, attacker, damage);
}
void qa_q1b_die_call(const qa_q1b_die *cb, qa_q1b_opt_actor attacker) {
  if (cb->has && cb->fn != NULL) cb->fn(cb->ctx, cb->game, cb->entity, attacker);
}
void qa_q1b_blocked_call(const qa_q1b_blocked *cb, qa_actor_id other) {
  if (cb->has && cb->fn != NULL) cb->fn(cb->ctx, cb->game, cb->entity, other);
}

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, text, length);
  return copy;
}
static void copy_fixed(char *dst, size_t size, const char *src) {
  size_t length = strlen(src);
  if (length >= size) length = size - 1;
  memcpy(dst, src, length);
  dst[length] = '\0';
}

void qa_q1b_actor_init(qa_q1b_actor *entity, qa_owned_actor actor, const char *classname) {
  memset(entity, 0, sizeof *entity);
  entity->actor = actor;
  copy_fixed(entity->classname, sizeof entity->classname, classname);
  entity->solid = "none";
  entity->movement = "none";
  entity->next_think = -1;
  entity->state = "bottom";
  entity->yaw_speed = 20;
  entity->attack_state = "straight";
  /* TS constructor seeds the named scalars from the fields map; the
   * foundation owner re-applies them after loading source fields. */
  copy_fixed(entity->model, sizeof entity->model, qa_q1b_actor_text(entity, "model"));
  copy_fixed(entity->original_model, sizeof entity->original_model, entity->model);
  copy_fixed(entity->target, sizeof entity->target, qa_q1b_actor_text(entity, "target"));
  copy_fixed(entity->targetname, sizeof entity->targetname, qa_q1b_actor_text(entity, "targetname"));
  copy_fixed(entity->killtarget, sizeof entity->killtarget, qa_q1b_actor_text(entity, "killtarget"));
  copy_fixed(entity->message, sizeof entity->message, qa_q1b_actor_text(entity, "message"));
  entity->delay = qa_q1b_actor_number(entity, "delay");
  entity->spawnflags = (int32_t)qa_q1b_actor_number(entity, "spawnflags");
  entity->sounds = qa_q1b_actor_number(entity, "sounds");
  entity->wait = qa_q1b_actor_number(entity, "wait");
  entity->speed = qa_q1b_actor_number(entity, "speed");
  entity->damage = qa_q1b_actor_number(entity, "dmg");
  entity->max_health = qa_q1b_actor_number(entity, "health");
  entity->count = qa_q1b_actor_number(entity, "count");
}
void qa_q1b_actor_destroy(qa_q1b_actor *entity) {
  for (size_t i = 0; i < entity->field_count; i++) { free(entity->fields[i].key); free(entity->fields[i].value); }
  free(entity->fields);
  for (size_t i = 0; i < entity->reference_count; i++) free(entity->references[i].key);
  free(entity->references);
  free(entity->door_group);
  memset(entity, 0, sizeof *entity);
}
void qa_q1b_fields_set(qa_q1b_actor *entity, const char *key, const char *value) {
  for (size_t i = 0; i < entity->field_count; i++) {
    if (strcmp(entity->fields[i].key, key) == 0) {
      free(entity->fields[i].value);
      entity->fields[i].value = dup_text(value);
      return;
    }
  }
  if (entity->field_count == entity->field_capacity) {
    size_t capacity = entity->field_capacity == 0 ? 8 : entity->field_capacity * 2;
    qa_q1b_field_entry *grown = realloc(entity->fields, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    entity->fields = grown;
    entity->field_capacity = capacity;
  }
  entity->fields[entity->field_count].key = dup_text(key);
  entity->fields[entity->field_count].value = dup_text(value);
  entity->field_count++;
}
const char *qa_q1b_fields_get(const qa_q1b_actor *entity, const char *key) {
  for (size_t i = 0; i < entity->field_count; i++)
    if (strcmp(entity->fields[i].key, key) == 0) return entity->fields[i].value;
  return NULL;
}
bool qa_q1b_fields_has(const qa_q1b_actor *entity, const char *key) {
  return qa_q1b_fields_get(entity, key) != NULL;
}
void qa_q1b_references_set(qa_q1b_actor *entity, const char *key, qa_q1b_opt_actor value) {
  for (size_t i = 0; i < entity->reference_count; i++) {
    if (strcmp(entity->references[i].key, key) == 0) { entity->references[i].value = value; return; }
  }
  if (entity->reference_count == entity->reference_capacity) {
    size_t capacity = entity->reference_capacity == 0 ? 4 : entity->reference_capacity * 2;
    qa_q1b_ref_entry *grown = realloc(entity->references, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    entity->references = grown;
    entity->reference_capacity = capacity;
  }
  entity->references[entity->reference_count].key = dup_text(key);
  entity->references[entity->reference_count].value = value;
  entity->reference_count++;
}
qa_q1b_opt_actor qa_q1b_references_get(const qa_q1b_actor *entity, const char *key) {
  for (size_t i = 0; i < entity->reference_count; i++)
    if (strcmp(entity->references[i].key, key) == 0) return entity->references[i].value;
  return QA_Q1B_NO_ACTOR;
}
const char *qa_q1b_actor_text(const qa_q1b_actor *entity, const char *key) {
  const char *value = qa_q1b_fields_get(entity, key);
  return value == NULL ? "" : value;
}
double qa_q1b_actor_number_or(const qa_q1b_actor *entity, const char *key, double fallback) {
  const char *text = qa_q1b_actor_text(entity, key);
  if (text[0] == '\0') return fallback;
  char *end = NULL;
  double value = strtod(text, &end);
  if (end == text) value = NAN;
  return isfinite(value) ? (double)(float)value : fallback;
}
double qa_q1b_actor_number(const qa_q1b_actor *entity, const char *key) {
  return qa_q1b_actor_number_or(entity, key, 0);
}
qa_vec3 qa_q1b_parse_vector(const char *text) {
  double numbers[3] = {0, 0, 0};
  const char *cursor = text;
  for (int i = 0; i < 3; i++) {
    while ((*cursor >= '\t' && *cursor <= '\r') || *cursor == ' ') cursor++;
    if (*cursor == '\0') break;
    char *end = NULL;
    double value = strtod(cursor, &end);
    if (end == cursor) value = NAN;
    numbers[i] = isfinite(value) ? (double)(float)value : 0;
    if (end == cursor) break;
    cursor = end;
  }
  qa_vec3 result = {numbers[0], numbers[1], numbers[2]};
  return result;
}
qa_vec3 qa_q1b_actor_vector(const qa_q1b_actor *entity, const char *key) {
  return qa_q1b_parse_vector(qa_q1b_actor_text(entity, key));
}
bool qa_q1b_actor_damageable(qa_q1b_game *game, const qa_q1b_actor *entity) {
  qa_combat_state combat;
  return game->host->combat_read(game->host->ctx, entity->actor.id, &combat) && combat.can_take_damage;
}
void qa_q1b_actor_set_damageable(qa_q1b_game *game, qa_q1b_actor *entity, bool value) {
  game->host->combat_set_can_take_damage(game->host->ctx, entity->actor, value);
}

qa_vec3 qa_q1b_move_direction(qa_q1b_game *game, qa_vec3 angles) {
  if (angles.y == -1 && angles.x == 0 && angles.z == 0) { qa_vec3 up = {0, 0, 1}; return up; }
  if (angles.y == -2 && angles.x == 0 && angles.z == 0) { qa_vec3 down = {0, 0, -1}; return down; }
  if (game != NULL) return game->make_vectors(game, angles).forward;
  return qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_Q1).forward;
}

double qa_q1b_powerups_get(const qa_q1b_player_state *player, const char *name) {
  for (size_t i = 0; i < player->powerup_count; i++)
    if (strcmp(player->powerups[i].name, name) == 0) return player->powerups[i].expires;
  return 0;
}
bool qa_q1b_powerups_has(const qa_q1b_player_state *player, const char *name) {
  for (size_t i = 0; i < player->powerup_count; i++)
    if (strcmp(player->powerups[i].name, name) == 0) return true;
  return false;
}
void qa_q1b_powerups_set(qa_q1b_player_state *player, const char *name, double expires) {
  for (size_t i = 0; i < player->powerup_count; i++) {
    if (strcmp(player->powerups[i].name, name) == 0) { player->powerups[i].expires = expires; return; }
  }
  if (player->powerup_count == player->powerup_capacity) {
    size_t capacity = player->powerup_capacity == 0 ? 4 : player->powerup_capacity * 2;
    qa_q1b_powerup_entry *grown = realloc(player->powerups, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    player->powerups = grown;
    player->powerup_capacity = capacity;
  }
  player->powerups[player->powerup_count].name = name;
  player->powerups[player->powerup_count].expires = expires;
  player->powerup_count++;
}
void qa_q1b_powerups_clear(qa_q1b_player_state *player) { player->powerup_count = 0; }

void qa_q1b_game_set_time(qa_q1b_game *game, double value) { game->time = (double)(float)value; }
void qa_q1b_game_sound(qa_q1b_game *game, qa_q1b_actor *entity, const char *path) {
  game->sound(game, entity, path, qa_q1b_channel_named("voice"), 1, 1);
}
void qa_q1b_game_sound_ch(qa_q1b_game *game, qa_q1b_actor *entity, const char *path, qa_q1b_channel channel) {
  game->sound(game, entity, path, channel, 1, 1);
}
void qa_q1b_game_sound_ch_att(qa_q1b_game *game, qa_q1b_actor *entity, const char *path, qa_q1b_channel channel, double attenuation) {
  game->sound(game, entity, path, channel, attenuation, 1);
}
void qa_q1b_game_sound_owned(qa_q1b_game *game, qa_owned_actor actor, const char *path) {
  game->sound_owned(game, actor, path, qa_q1b_channel_named("voice"), 1, 1);
}
void qa_q1b_game_sound_owned_ch(qa_q1b_game *game, qa_owned_actor actor, const char *path, qa_q1b_channel channel) {
  game->sound_owned(game, actor, path, channel, 1, 1);
}
void qa_q1b_game_sound_owned_ch_att(qa_q1b_game *game, qa_owned_actor actor, const char *path, qa_q1b_channel channel, double attenuation) {
  game->sound_owned(game, actor, path, channel, attenuation, 1);
}
void qa_q1b_game_damage(qa_q1b_game *game, qa_actor_id target, qa_actor_id inflictor,
                        qa_q1b_opt_actor attacker, double amount) {
  (void)game->damage(game, target, inflictor, attacker, amount, NULL, "direct", "", NULL);
}
void qa_q1b_game_message(qa_q1b_game *game, qa_actor_id player, const char *text) {
  game->message(game, qa_q1b_some_actor(player), text, true);
}
void qa_q1b_game_effect(qa_q1b_game *game, const char *effect, qa_vec3 origin) {
  game->effect(game, effect, origin, QA_Q1B_NO_ACTOR, 1);
}
void qa_q1b_game_effect_actor(qa_q1b_game *game, const char *effect, qa_vec3 origin, qa_actor_id actor) {
  game->effect(game, effect, origin, qa_q1b_some_actor(actor), 1);
}
void qa_q1b_game_effect_full(qa_q1b_game *game, const char *effect, qa_vec3 origin, qa_q1b_opt_actor actor, double amount) {
  game->effect(game, effect, origin, actor, amount);
}

const char *const QA_Q1B_WEAPONS[8] = {
  "axe", "shotgun", "supershotgun", "nailgun",
  "supernailgun", "grenadelauncher", "rocketlauncher", "lightning"
};
const char *qa_q1b_weapon_item(const char *weapon, char *buf, size_t size) {
  (void)snprintf(buf, size, "q1:weapon/%s", weapon);
  return buf;
}
qa_vec3 qa_q1b_normalize(qa_vec3 a) {
  double magnitude = qa_length3(a);
  if (magnitude == 0) { qa_vec3 zero = {0, 0, 0}; return zero; }
  return qa_scale3(a, 1 / magnitude);
}
double qa_q1b_yaw_for(qa_vec3 direction) {
  double yaw = atan2(direction.y, direction.x) * 180 / 3.141592653589793;
  return yaw < 0 ? yaw + 360 : yaw;
}
double qa_q1b_path_end_time(double time) {
  return (double)(float)((double)(float)time + 999999);
}
static const char *door_sound(const qa_q1b_actor *entity, bool moving) {
  if (entity->sounds == 1) return moving ? "doors/doormv1.wav" : "doors/drclos4.wav";
  if (entity->sounds == 2) return moving ? "doors/hydro1.wav" : "doors/hydro2.wav";
  if (entity->sounds == 3) return moving ? "doors/stndr1.wav" : "doors/stndr2.wav";
  if (entity->sounds == 4) return moving ? "doors/ddoor1.wav" : "doors/ddoor2.wav";
  return "misc/null.wav";
}
void qa_q1b_door_down(qa_q1b_game *game, qa_q1b_actor *entity) {
  qa_q1b_game_sound(game, entity, door_sound(entity, true));
  entity->state = "down";
  if (entity->max_health > 0) {
    game->host->combat_set_health(game->host->ctx, entity->actor, entity->max_health);
    qa_q1b_actor_set_damageable(game, entity, true);
  }
  game->calc_move(game, entity, entity->pos1, entity->speed,
                  game->named.action(game->named.ctx, entity, "door_hit_bottom"));
}
void qa_q1b_format_number(char *buf, size_t size, double value) {
  if (isfinite(value) && value == trunc(value) && fabs(value) < 1e15) {
    (void)snprintf(buf, size, "%lld", (long long)value);
  } else {
    (void)snprintf(buf, size, "%.17g", value);
  }
}
void qa_q1b_format_fround(char *buf, size_t size, double value) {
  qa_q1b_format_number(buf, size, (double)(float)value);
}

