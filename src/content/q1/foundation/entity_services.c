/*
 * Port of src/content/q1/foundation/entity-services.ts. The getters
 * entity()/player()/isPlayer()/body()/sourceTarget() are the renamed
 * qa_q1_get_* (a qa_q1_entity() function would collide with the
 * qa_q1_entity typedef). Bodies, combat, inventory, callbacks and actor
 * liveness are the shared world tables on game->host, called directly
 * as TS does. GPL-2.0-or-later.
 */
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q1/foundation/shambler_damage.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/checkpoint.h"
#include "qa/content/q1/foundation/movers.h"
#include "qa/content/q1/foundation/pickups.h"
#include "qa/content/q1/foundation/spawns.h"
#include "qa/content/q1/foundation/weapons.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/movement/q1/pusher.h"
#include "qa/movement/q1/water_transition.h"
#include "qa/world/gameplay/damage_modifier.h"

/* TS entities.get(owner) / players.get(owner): Maps keyed by OwnedActor. */
static qa_q1_actor *entity_of(qa_q1_services *game, qa_owned_actor owner) {
  for (size_t i = 0; i < game->entity_count; i++)
    if (qa_same_owned_actor(game->entities[i]->actor, owner)) return game->entities[i];
  return NULL;
}
static qa_q1_player_state *player_of(qa_q1_services *game, qa_owned_actor owner) {
  for (size_t i = 0; i < game->player_count; i++)
    if (qa_same_owned_actor(game->players[i]->actor, owner)) return game->players[i];
  return NULL;
}
qa_q1_actor *qa_q1_get_entity(qa_q1_services *game, const qa_actor_id *actor) {
  if (actor == NULL) return NULL;
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(game->host.actors, *actor, &owner)) return NULL;
  return entity_of(game, owner);
}
qa_q1_player_state *qa_q1_player(qa_q1_services *game, qa_actor_id actor) {
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(game->host.actors, actor, &owner)) return NULL;
  return player_of(game, owner);
}
bool qa_q1_is_player(qa_q1_services *game, const qa_actor_id *actor) {
  if (actor == NULL) return false;
  if (qa_q1_player(game, *actor) != NULL) return true;
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  for (size_t i = 0; i < count; i++)
    if (qa_same_actor(players[i], *actor)) return true;
  return false;
}
qa_q1_body qa_q1_get_body(qa_q1_services *game, qa_q1_actor *entity) {
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, entity->actor.id);
  if (!body.has_value) qa_range_error("Missing Q1 body"); /* TS Error throw */
  return qa_q1_body_from_state(body.value);
}
qa_q1_source_target qa_q1_get_source_target(qa_q1_services *game, qa_actor_id actor) {
  if (game->host.source_target != NULL) {
    qa_q1_source_target observed;
    if (game->host.source_target(game->host.ctx, actor, &observed)) return observed;
  }
  qa_q1_actor *entity = qa_q1_get_entity(game, &actor);
  bool aimed_damage2 = entity != NULL && entity->aimed_damage;
  bool push2 = entity != NULL && entity->movement != NULL && strcmp(entity->movement, "push") == 0;
  bool player = qa_q1_is_player(game, &actor);
  bool slidebox2 = entity != NULL && entity->solid != NULL && strcmp(entity->solid, "slidebox") == 0;
  qa_q1_source_target result = {
      aimed_damage2, push2, player, slidebox2};
  return result;
}

/* --- helpers --- */
static void *xmalloc(size_t size) {
  void *memory = malloc(size);
  if (memory == NULL) qa_range_error("Out of memory");
  return memory;
}
static char *dup_string(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = xmalloc(length);
  memcpy(copy, text, length);
  return copy;
}
static bool same(const char *a, const char *b) {
  return a != NULL && b != NULL && strcmp(a, b) == 0;
}
/* Grow an insertion-ordered array. A zero capacity with a nonzero
 * count means caller-borrowed storage: copy out, never realloc it. */
static void grow(void **array, size_t *count, size_t *capacity,
                 size_t element_size) {
  if (*count < *capacity) return;
  size_t next = *capacity > 0 ? *capacity * 2 : (*count > 0 ? *count * 2 : 8);
  if (*capacity == 0 && *count > 0) {
    void *grown = xmalloc(next * element_size);
    memcpy(grown, *array, *count * element_size);
    *array = grown;
  } else {
    void *grown = realloc(*array, next * element_size);
    if (grown == NULL) qa_range_error("Out of memory");
    *array = grown;
  }
  *capacity = next;
}
static const qa_q1_weapon_definition *registered(qa_q1_services *game,
                                                  qa_q1_weapon weapon) {
  for (size_t i = 0; i < game->registered_weapon_count; i++)
    if (same(game->registered_weapons[i].def->id, weapon))
      return game->registered_weapons[i].def;
  return NULL;
}

/* --- dropQ1Punch / ammoItem --- */
qa_vec3 qa_q1_drop_punch(qa_vec3 angles, double elapsed,
                         const qa_numeric_operations *numeric) {
  qa_mutable_vector_math math;
  qa_create_mutable_vector_math(&math, numeric, QA_ZERO_LENGTH_PRESERVE);
  qa_vec3 direction = angles;
  double magnitude =
      qa_mvm_vector_normalize(&math, qa_vec3_access_plain(&direction));
  /* Math.max(0, x): keeps NaN and turns -0 into +0. */
  const double remaining = qa_js_max(0, numeric->subtract(
      numeric->ctx, magnitude, numeric->multiply(numeric->ctx, 10, elapsed)));
  qa_vec3 punch = QA_Q1_ZERO;
  qa_mvm_vector_scale(&math, qa_vec3_access_plain(&direction), remaining,
                      qa_vec3_access_plain(&punch));
  return punch;
}
/* Ammunition of the id1 weapons; weapons that other programs add register
 * their own through registerWeapon. */
qa_item_id qa_q1_ammo_item(qa_q1_weapon weapon) {
  if (same(weapon, "shotgun") || same(weapon, "supershotgun"))
    return "q1:ammo/shells";
  if (same(weapon, "nailgun") || same(weapon, "supernailgun"))
    return "q1:ammo/nails";
  if (same(weapon, "grenadelauncher") || same(weapon, "rocketlauncher"))
    return "q1:ammo/rockets";
  if (same(weapon, "lightning")) return "q1:ammo/cells";
  return NULL; /* TS default: null */
}

/* --- services accessors --- */
qa_provider_id qa_q1_services_provider(const qa_q1_services *game) {
  return game->options.provider != NULL ? game->options.provider : QA_Q1_PROVIDER;
}
bool qa_q1_services_uses_id1(const qa_q1_services *game) {
  return game->options.precache_id1;
}
void qa_q1_services_set_time(qa_q1_services *game, double value) {
  game->time = (float)value; /* TS setter: Math.fround */
}
void qa_q1_set_gravity(qa_q1_services *game, qa_actor_id actor, double scale) {
  if (game->host.set_gravity == NULL)
    qa_range_error("Q1 source gravity mutation requires the selected movement host");
  game->host.set_gravity(game->host.ctx, actor, scale);
}
void qa_q1_control_player(qa_q1_services *game, qa_actor_id actor,
                          qa_vec3 origin, qa_vec3 angles, qa_vec3 view_offset) {
  if (game->host.control_player == NULL)
    qa_range_error("Q1 cinematic requires the selected player control host");
  game->host.control_player(game->host.ctx, actor, origin, angles, view_offset);
}
void qa_q1_set_base_team_health(qa_q1_services *game, bool enabled) {
  game->base_team_health = enabled;
}
double qa_q1_host_random(qa_q1_services *game) {
  return game->host.random(game->host.ctx);
}
qa_q1_trace qa_q1_host_trace(qa_q1_services *game, qa_q1_trace_request request) {
  return game->host.trace(game->host.ctx, request);
}
/* Content-addressed game-lifetime storage for qa_q1_emit (open addressing). */
typedef struct emit_blob { uint64_t hash; size_t length; unsigned char *bytes; } emit_blob;
struct qa_q1_emit_intern { emit_blob *slots; size_t count, capacity; };
static uint64_t blob_hash(const void *bytes, size_t length) {
  const unsigned char *p = bytes;
  uint64_t hash = 1469598103934665603ULL;
  for (size_t i = 0; i < length; i++) { hash ^= p[i]; hash *= 1099511628211ULL; }
  return hash;
}
static const void *intern_blob(qa_q1_services *game, const void *bytes, size_t length) {
  struct qa_q1_emit_intern *table = game->emit_intern;
  if (table == NULL) {
    table = calloc(1, sizeof *table);
    if (table == NULL) qa_range_error("out of memory");
    game->emit_intern = table;
  }
  if ((table->count + 1) * 4 >= table->capacity * 3) {
    size_t capacity = table->capacity == 0 ? 64 : table->capacity * 2;
    emit_blob *slots = calloc(capacity, sizeof *slots);
    if (slots == NULL) qa_range_error("out of memory");
    for (size_t i = 0; i < table->capacity; i++) {
      if (table->slots[i].bytes == NULL) continue;
      size_t at = (size_t)(table->slots[i].hash & (capacity - 1));
      while (slots[at].bytes != NULL) at = (at + 1) & (capacity - 1);
      slots[at] = table->slots[i];
    }
    free(table->slots);
    table->slots = slots;
    table->capacity = capacity;
  }
  uint64_t hash = blob_hash(bytes, length);
  size_t at = (size_t)(hash & (table->capacity - 1));
  while (table->slots[at].bytes != NULL) {
    emit_blob *slot = &table->slots[at];
    if (slot->hash == hash && slot->length == length && memcmp(slot->bytes, bytes, length) == 0) return slot->bytes;
    at = (at + 1) & (table->capacity - 1);
  }
  unsigned char *copy = malloc(length == 0 ? 1 : length);
  if (copy == NULL) qa_range_error("out of memory");
  if (length != 0) memcpy(copy, bytes, length);
  table->slots[at] = (emit_blob){hash, length, copy};
  table->count++;
  return copy;
}
static void emit_intern_free(struct qa_q1_emit_intern *table) {
  if (table == NULL) return;
  for (size_t i = 0; i < table->capacity; i++) free(table->slots[i].bytes);
  free(table->slots);
  free(table);
}
static const char *intern_text(qa_q1_services *game, const char *text) {
  return text == NULL ? NULL : intern_blob(game, text, strlen(text) + 1);
}
/* Zeroed scratch so struct padding hashes deterministically. */
static const qa_q1_msg_arg *intern_args(qa_q1_services *game, const qa_q1_msg_arg *args, size_t count) {
  if (args == NULL || count == 0) return args == NULL ? NULL : intern_blob(game, "", 0);
  qa_q1_msg_arg *scratch = calloc(count, sizeof *scratch);
  if (scratch == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    scratch[i].is_number = args[i].is_number;
    scratch[i].number = args[i].number;
    scratch[i].text = intern_text(game, args[i].text);
  }
  const qa_q1_msg_arg *interned = intern_blob(game, scratch, count * sizeof *scratch);
  free(scratch);
  return interned;
}
static const qa_q1_message_part *intern_parts(qa_q1_services *game, const qa_q1_message_part *parts, size_t count) {
  if (parts == NULL || count == 0) return parts == NULL ? NULL : intern_blob(game, "", 0);
  qa_q1_message_part *scratch = calloc(count, sizeof *scratch);
  if (scratch == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    scratch[i].text = intern_text(game, parts[i].text);
    scratch[i].args = intern_args(game, parts[i].args, parts[i].arg_count);
    scratch[i].arg_count = parts[i].arg_count;
  }
  const qa_q1_message_part *interned = intern_blob(game, scratch, count * sizeof *scratch);
  free(scratch);
  return interned;
}
void qa_q1_emit(qa_q1_services *game, qa_q1_event event) {
  event.kind = intern_text(game, event.kind);
  event.sound_channel = intern_text(game, event.sound_channel);
  event.path = intern_text(game, event.path);
  event.text = intern_text(game, event.text);
  event.effect = intern_text(game, event.effect);
  event.view_model = intern_text(game, event.view_model);
  event.pattern = intern_text(game, event.pattern);
  event.map = intern_text(game, event.map);
  event.id = intern_text(game, event.id);
  event.args = intern_args(game, event.args, event.arg_count);
  event.parts = intern_parts(game, event.parts, event.part_count);
  event.beam_style = intern_text(game, event.beam_style);
  event.weapon = intern_text(game, event.weapon);
  event.powerup = intern_text(game, event.powerup);
  event.attack.kind = intern_text(game, event.attack.kind);
  game->host.emit(game->host.ctx, event);
}

/* --- registration family --- */
void qa_q1_register_path_touch(qa_q1_services *game, const char *id,
                               qa_q1_path_touch_fn fn, void *ctx) {
  for (size_t i = 0; i < game->path_touch_count; i++)
    if (same(game->path_touches[i].id, id))
      qa_range_error("Duplicate Q1 source path touch %s", id);
  grow((void **)&game->path_touches, &game->path_touch_count,
       &game->path_touch_capacity, sizeof *game->path_touches);
  game->path_touches[game->path_touch_count].id = dup_string(id);
  game->path_touches[game->path_touch_count].fn = fn;
  game->path_touches[game->path_touch_count].ctx = ctx;
  game->path_touch_count++;
}
bool qa_q1_source_path_touch(qa_q1_services *game, qa_q1_actor *corner,
                             qa_q1_actor *mover) {
  for (size_t i = 0; i < game->path_touch_count; i++)
    if (game->path_touches[i].fn(game->path_touches[i].ctx, corner, mover))
      return true;
  return false;
}
void qa_q1_register_weapon_rules(qa_q1_services *game,
                                 const qa_q1_weapon_rules *rules) {
  /* TS checks the id over every rule before the consumption owner. */
  for (size_t i = 0; i < game->weapon_rule_count; i++)
    if (same(game->weapon_rules[i]->id, rules->id))
      qa_range_error("Duplicate Q1 weapon rules: %s", rules->id);
  for (size_t i = 0; i < game->weapon_rule_count; i++)
    if (rules->consume_ammo != NULL && game->weapon_rules[i]->consume_ammo != NULL)
      qa_range_error("Q1 source ammunition consumption already registered");
  grow((void **)&game->weapon_rules, &game->weapon_rule_count,
       &game->weapon_rule_capacity, sizeof *game->weapon_rules);
  game->weapon_rules[game->weapon_rule_count++] = rules;
}
bool qa_q1_consume_weapon_ammo(qa_q1_services *game,
                               qa_q1_player_state *player, qa_item_id item,
                               double amount) {
  for (size_t i = 0; i < game->weapon_rule_count; i++)
    if (game->weapon_rules[i]->consume_ammo != NULL)
      return game->weapon_rules[i]->consume_ammo(game->weapon_rules[i]->ctx,
                                                 game, player, item, amount);
  return qa_shared_inventory_table_consume(game->host.inventory, player->actor, item, amount);
}
void qa_q1_weapon_before_fire(qa_q1_services *game, qa_q1_player_state *player) {
  for (size_t i = 0; i < game->weapon_rule_count; i++)
    if (game->weapon_rules[i]->before_fire != NULL)
      game->weapon_rules[i]->before_fire(game->weapon_rules[i]->ctx, game, player);
}
double qa_q1_weapon_attack_delay(qa_q1_services *game,
                                 qa_q1_player_state *player, double initial) {
  double delay = initial;
  for (size_t i = 0; i < game->weapon_rule_count; i++)
    if (game->weapon_rules[i]->attack_delay != NULL)
      delay = game->weapon_rules[i]->attack_delay(game->weapon_rules[i]->ctx,
                                                  game, player, delay);
  return delay;
}
double qa_q1_weapon_frame_delay(qa_q1_services *game,
                                qa_q1_player_state *player, double initial) {
  double delay = initial;
  for (size_t i = 0; i < game->weapon_rule_count; i++)
    if (game->weapon_rules[i]->frame_delay != NULL)
      delay = game->weapon_rules[i]->frame_delay(game->weapon_rules[i]->ctx,
                                                 game, player, delay);
  return delay;
}
double qa_q1_nail_speed(qa_q1_services *game, qa_q1_player_state *player,
                        double initial) {
  double speed = initial;
  for (size_t i = 0; i < game->weapon_rule_count; i++)
    if (game->weapon_rules[i]->nail_speed != NULL)
      speed = game->weapon_rules[i]->nail_speed(game->weapon_rules[i]->ctx,
                                                game, player, speed);
  return speed;
}
void qa_q1_register_pickup_rules(qa_q1_services *game,
                                 const qa_q1_pickup_rules *rules) {
  if (game->pickup_rules != NULL)
    qa_range_error("Q1 pickup rules already owned by %s", game->pickup_rules->id);
  game->pickup_rules = rules;
}
static void weapon_set(qa_q1_services *game,
                       const qa_q1_weapon_definition *definition) {
  for (size_t i = 0; i < game->registered_weapon_count; i++)
    if (same(game->registered_weapons[i].def->id, definition->id)) {
      game->registered_weapons[i].def = definition;
      return;
    }
  grow((void **)&game->registered_weapons, &game->registered_weapon_count,
       &game->registered_weapon_capacity, sizeof *game->registered_weapons);
  game->registered_weapons[game->registered_weapon_count++].def = definition;
}
void qa_q1_register_weapon(qa_q1_services *game,
                           const qa_q1_weapon_definition *definition) {
  if (qa_q1_is_base_weapon(definition->id) ||
      registered(game, definition->id) != NULL)
    qa_range_error("Q1 weapon already registered: %s", definition->id);
  weapon_set(game, definition);
}
void qa_q1_replace_weapon(qa_q1_services *game,
                          const qa_q1_weapon_definition *definition) {
  if (!qa_q1_is_base_weapon(definition->id) &&
      registered(game, definition->id) == NULL)
    qa_range_error("No Q1 weapon to replace: %s", definition->id);
  weapon_set(game, definition);
}
void qa_q1_register_weapon_order(qa_q1_services *game, const char *id,
                                 const qa_q1_weapon *order, size_t count) {
  if (game->weapon_order_owner != NULL)
    qa_range_error("Q1 weapon fallback already owned by %s",
                   game->weapon_order_owner);
  if (count > 24) qa_range_error("Q1 weapon order exceeds mirror capacity");
  game->weapon_order_owner = dup_string(id);
  for (size_t i = 0; i < count; i++) game->weapon_order[i] = order[i];
  game->weapon_order_count = count;
}
void qa_q1_register_player_extension(qa_q1_services *game,
                                     const qa_q1_player_extension *extension) {
  for (size_t i = 0; i < game->player_extension_count; i++)
    if (same(game->player_extensions[i]->id, extension->id))
      qa_range_error("Duplicate Q1 player extension: %s", extension->id);
  grow((void **)&game->player_extensions, &game->player_extension_count,
       &game->player_extension_capacity, sizeof *game->player_extensions);
  game->player_extensions[game->player_extension_count++] = extension;
}
bool qa_q1_inventory_capacity(qa_q1_services *game, qa_actor_id actor,
                              qa_item_id item, double *out) {
  qa_q1_player_state *player = qa_q1_player(game, actor);
  if (player == NULL) return false;
  bool found = false;
  double capacity = 0;
  for (size_t i = 0; i < game->player_extension_count; i++) {
    const qa_q1_player_extension *extension = game->player_extensions[i];
    if (extension->inventory_capacity == NULL) continue;
    double value;
    if (!extension->inventory_capacity(extension->ctx, game, player, item,
                                       &value))
      continue;
    if (found) qa_range_error("Multiple Q1 capacity owners for %s", item);
    found = true;
    capacity = value;
  }
  if (found && out != NULL) *out = capacity;
  return found;
}
const char *qa_q1_services_weapon_model(qa_q1_services *game,
                                        qa_q1_weapon weapon,
                                        qa_q1_player_state *player) {
  const qa_q1_weapon_definition *definition = registered(game, weapon);
  if (player != NULL && definition != NULL && definition->model_for != NULL)
    return definition->model_for(definition->ctx, game, player);
  return definition != NULL && definition->model != NULL
             ? definition->model
             : qa_q1_weapon_model(weapon);
}
qa_item_id qa_q1_services_weapon_item(qa_q1_services *game, qa_q1_weapon weapon) {
  const qa_q1_weapon_definition *definition = registered(game, weapon);
  return definition != NULL && definition->item != NULL
             ? definition->item
             : qa_q1_weapon_item(weapon);
}
qa_item_id qa_q1_services_weapon_ammo(qa_q1_services *game, qa_q1_weapon weapon) {
  const qa_q1_weapon_definition *definition = registered(game, weapon);
  return definition == NULL ? qa_q1_ammo_item(weapon) : definition->ammo;
}
bool qa_q1_weapon_available(qa_q1_services *game, qa_q1_player_state *player,
                            qa_q1_weapon weapon, const char *purpose) {
  return qa_q1_weapon_available_counted(game, player, weapon, purpose, NULL, NULL);
}
/* TS weaponAvailable(player, weapon, purpose, ammoCount); count NULL is the
 * TS default `item => host.inventory.count(player.actor.id, item)`. */
bool qa_q1_weapon_available_counted(qa_q1_services *game, qa_q1_player_state *player,
                                    qa_q1_weapon weapon, const char *purpose,
                                    qa_q1_ammo_count_fn count, void *count_ctx) {
  bool best = purpose == NULL || strcmp(purpose, "best") == 0;
  if (qa_shared_inventory_table_count(game->host.inventory, player->actor.id,
                                      qa_q1_services_weapon_item(game, weapon)) == 0)
    return false;
  const qa_q1_weapon_definition *extension = registered(game, weapon);
  if (!qa_q1_is_base_weapon(weapon) && extension == NULL) return false;
  if (extension != NULL && extension->available != NULL &&
      !extension->available(extension->ctx, game, player))
    return false;
  if (best && extension != NULL && extension->best_available != NULL &&
      !extension->best_available(extension->ctx, game, player))
    return false;
  if (best && same(weapon, "lightning") && player->water_level > 1) return false;
  qa_item_id ammo = qa_q1_services_weapon_ammo(game, weapon);
  double needed =
      extension != NULL && extension->has_ammo_per_shot
          ? extension->ammo_per_shot
          : (best && (same(weapon, "supernailgun") ||
                      same(weapon, "supershotgun"))
                 ? 2
                 : 1);
  if (ammo == NULL) return true;
  const double have = count != NULL ? count(count_ctx, ammo)
                                    : qa_shared_inventory_table_count(game->host.inventory, player->actor.id, ammo);
  return have >= needed;
}
bool qa_q1_fire_registered_weapon(qa_q1_services *game,
                                  qa_q1_player_state *player) {
  if (player->primary_holstered) return false;
  const qa_q1_weapon_definition *definition = registered(game, player->weapon);
  if (definition == NULL)
    qa_range_error("Q1 weapon source not registered: %s", player->weapon);
  if (qa_q1_health(game, player->actor.id) <= 0 ||
      game->time < (player->continuous_firing ? player->next_weapon_frame
                                              : player->attack_finished))
    return false;
  if (!qa_q1_weapon_available(game, player, player->weapon, "fire")) {
    qa_q1_select_weapon(game, player->actor,
                        qa_q1_choose_best(game, player->actor));
    return false;
  }
  qa_q1_weapon_before_fire(game, player);
  return definition->fire(definition->ctx, game, player);
}
void qa_q1_register_state_extension(qa_q1_services *game,
                                    const qa_q1_state_extension *extension) {
  for (size_t i = 0; i < game->state_extension_count; i++)
    if (same(game->state_extensions[i]->id, extension->id))
      qa_range_error("Duplicate Q1 state extension: %s", extension->id);
  grow((void **)&game->state_extensions, &game->state_extension_count,
       &game->state_extension_capacity, sizeof *game->state_extensions);
  game->state_extensions[game->state_extension_count++] = extension;
}
void qa_q1_register_spawn(qa_q1_services *game, const char *classname,
                          qa_q1_spawn_fn handler, void *ctx) {
  for (size_t i = 0; i < game->spawner_count; i++)
    if (same(game->spawners[i].classname, classname)) {
      if (game->keeping_owned_spawns) return;
      qa_range_error("Q1 spawn handler already registered: %s", classname);
    }
  grow((void **)&game->spawners, &game->spawner_count, &game->spawner_capacity,
       sizeof *game->spawners);
  game->spawners[game->spawner_count].classname = dup_string(classname);
  game->spawners[game->spawner_count].fn = handler;
  game->spawners[game->spawner_count].ctx = ctx;
  game->spawner_count++;
}
typedef struct keeping_restore { qa_q1_services *game; bool previous; } keeping_restore;
static void restore_keeping(void *arg) {
  const keeping_restore *restore = arg;
  restore->game->keeping_owned_spawns = restore->previous;
}
void qa_q1_adjoin(qa_q1_services *game, void (*register_fn)(void *ctx, qa_q1_services *game), void *ctx) {
  keeping_restore restore = {game, game->keeping_owned_spawns};
  qa_cleanup cleanup; /* TS finally: a raise out of register_fn restores the flag */
  qa_cleanup_push(&cleanup, restore_keeping, &restore);
  game->keeping_owned_spawns = true;
  register_fn(ctx, game);
  qa_cleanup_pop(&cleanup);
  game->keeping_owned_spawns = restore.previous;
}
void qa_q1_add_world_precache(qa_q1_services *game, void (*fn)(void *ctx, qa_q1_services *game), void *ctx) {
  grow((void **)&game->world_precaches, &game->world_precache_count, &game->world_precache_capacity,
       sizeof *game->world_precaches);
  game->world_precaches[game->world_precache_count].fn = fn;
  game->world_precaches[game->world_precache_count].ctx = ctx;
  game->world_precache_count++;
}
bool qa_q1_has_spawn(const qa_q1_services *game, const char *classname) {
  for (size_t i = 0; i < game->spawner_count; i++)
    if (same(game->spawners[i].classname, classname)) return true;
  return false;
}
void qa_q1_set_state_extension_restore_absent(qa_q1_services *game, const qa_q1_state_extension *extension,
                                              void (*restore_absent)(void *ctx)) {
  for (size_t i = 0; i < game->restore_absent_count; i++)
    if (game->restore_absents[i].extension == extension) {
      game->restore_absents[i].restore_absent = restore_absent;
      return;
    }
  grow((void **)&game->restore_absents, &game->restore_absent_count, &game->restore_absent_capacity,
       sizeof *game->restore_absents);
  game->restore_absents[game->restore_absent_count].extension = extension;
  game->restore_absents[game->restore_absent_count].restore_absent = restore_absent;
  game->restore_absent_count++;
}
void (*qa_q1_state_extension_restore_absent(const qa_q1_services *game, const qa_q1_state_extension *extension))(void *ctx) {
  for (size_t i = 0; i < game->restore_absent_count; i++)
    if (game->restore_absents[i].extension == extension) return game->restore_absents[i].restore_absent;
  return NULL;
}
bool qa_q1_source_world_word_get(const qa_q1_services *game, const char *name, double *out) {
  for (size_t i = 0; i < game->source_world_word_count; i++)
    if (same(game->source_world_words[i].name, name)) {
      *out = game->source_world_words[i].value;
      return true;
    }
  return false;
}
void qa_q1_source_world_word_set(qa_q1_services *game, const char *name, double value) {
  for (size_t i = 0; i < game->source_world_word_count; i++)
    if (same(game->source_world_words[i].name, name)) {
      game->source_world_words[i].value = value; /* Map.set keeps the position */
      return;
    }
  grow((void **)&game->source_world_words, &game->source_world_word_count, &game->source_world_word_capacity,
       sizeof *game->source_world_words);
  game->source_world_words[game->source_world_word_count].name = dup_string(name);
  game->source_world_words[game->source_world_word_count].value = value;
  game->source_world_word_count++;
}
void qa_q1_source_world_words_clear(qa_q1_services *game) {
  for (size_t i = 0; i < game->source_world_word_count; i++) free(game->source_world_words[i].name);
  game->source_world_word_count = 0;
}
void qa_q1_replace_spawn(qa_q1_services *game, const char *classname,
                         qa_q1_spawn_fn handler, void *ctx) {
  for (size_t i = 0; i < game->spawner_count; i++)
    if (same(game->spawners[i].classname, classname)) {
      game->spawners[i].fn = handler;
      game->spawners[i].ctx = ctx;
      return;
    }
  qa_range_error("No registered Q1 spawn to replace: %s", classname);
}

/* --- inventory --- */
void qa_q1_initialize_weapon_inventory(qa_q1_services *game,
                                       qa_owned_actor actor) {
  qa_inventory_entry entries[12];
  memset(entries, 0, sizeof entries);
  for (size_t i = 0; i < 8; i++) {
    entries[i].item = qa_q1_weapon_item(QA_Q1_WEAPONS[i]);
    entries[i].count = same(QA_Q1_WEAPONS[i], "axe") || same(QA_Q1_WEAPONS[i], "shotgun") ? 1 : 0;
    entries[i].capacity = 1;
  }
  static const struct { const char *item; double count, capacity; } ammo[4] = {
      {"q1:ammo/shells", 25, 100}, {"q1:ammo/nails", 0, 200},
      {"q1:ammo/rockets", 0, 100}, {"q1:ammo/cells", 0, 100}};
  for (size_t i = 0; i < 4; i++) {
    entries[8 + i].item = ammo[i].item;
    entries[8 + i].count = ammo[i].count;
    entries[8 + i].capacity = ammo[i].capacity;
  }
  if (!qa_shared_inventory_table_has(game->host.inventory, actor.id))
    qa_shared_inventory_table_create_entries(game->host.inventory, actor, entries, 12);
  else
    for (size_t i = 0; i < 12; i++)
      qa_shared_inventory_table_configure(game->host.inventory, actor, entries[i], NULL, NULL);
}

/* --- attach --- */
static qa_damage_adjustment_result foreign_shambler_adjust(void *ctx, qa_damage_request request) {
  const qa_q1_actor *entity = ctx;
  qa_damage_adjustment_result result;
  memset(&result, 0, sizeof result);
  result.has_value = qa_q1_foreign_shambler_damage(entity->monster != NULL ? entity->monster->species : NULL, &request,
                                                   &result.value.amount, &result.value.knockback);
  return result;
}
/* TS admitEntity. A failed admission frees the unadmitted entity first
 * (TS drops the unreferenced object), then throws. */
static qa_q1_actor *admit_entity(qa_q1_services *game, qa_q1_actor *entity) {
  qa_session_actor_registry_assert_owned(game->host.actors, entity->actor);
  const char *failure = NULL;
  if (entity_of(game, entity->actor) != NULL) failure = "Q1 actor already attached";
  else if (!qa_shared_body_table_read(game->host.bodies, entity->actor.id).has_value ||
           !qa_gameplay_authority_read(game->host.combat, entity->actor.id).has_value)
    failure = "Attach Q1 behavior after shared body and combat admission";
  if (failure != NULL) {
    qa_q1_actor_free(entity);
    free(entity);
    qa_range_error("%s", failure);
  }
  grow((void **)&game->entities, &game->entity_count, &game->entity_capacity,
       sizeof *game->entities);
  game->entities[game->entity_count++] = entity;
  qa_gameplay_authority_bind_damage_adjustment(game->host.combat, entity->actor, foreign_shambler_adjust, entity);
  qa_q1_bind_actor_callbacks(game, entity);
  if (game->host.register_entity != NULL) game->host.register_entity(game->host.ctx, entity, game);
  return entity;
}
qa_q1_actor *qa_q1_attach_existing(qa_q1_services *game, qa_owned_actor actor,
                                   const char *classname,
                                   const qa_q1_entity *source,
                                   const double *source_ordinal) {
  qa_q1_actor *entity = xmalloc(sizeof *entity);
  if (!qa_q1_actor_init(entity, actor, classname, source_ordinal, game->host.combat, source))
    qa_range_error("Out of memory");
  return admit_entity(game, entity);
}
qa_q1_player_state *qa_q1_attach_player(qa_q1_services *game,
                                        qa_owned_actor actor, qa_q1_weapon weapon,
                                        bool initialize_inventory,
                                        const double *max_health) {
  qa_session_actor_registry_assert_owned(game->host.actors, actor);
  qa_q1_player_state *existing = player_of(game, actor);
  if (existing != NULL) return existing;
  if (initialize_inventory) {
    qa_q1_initialize_weapon_inventory(game, actor);
    static const char *const keys[2] = {"q1:key/silver", "q1:key/gold"};
    for (size_t i = 0; i < 2; i++) {
      qa_inventory_entry entry;
      memset(&entry, 0, sizeof entry);
      entry.item = keys[i];
      entry.count = 0;
      entry.capacity = 1;
      qa_shared_inventory_table_configure(game->host.inventory, actor, entry, NULL, NULL);
    }
  }
  qa_q1_player_state *state = xmalloc(sizeof *state);
  memset(state, 0, sizeof *state);
  state->actor = actor;
  state->weapon = weapon != NULL ? weapon : "shotgun";
  state->weapon_animation_at = -1;
  state->weapon_animation_base = 1;
  state->nail_side = 1;
  state->max_health =
      max_health != NULL
          ? *max_health
          : (same(game->options.edition, "rerelease") &&
                     qa_q1_option_skill(game) == 3 && game->options.deathmatch == 0
                 ? 50
                 : 100);
  state->mega_rot_at = -1;
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor.id);
  state->view_angles = body.has_value ? body.value.angles : QA_Q1_ZERO;
  state->air_finished = game->time + 12;
  state->drown_damage = 2;
  state->auto_switch = "always";
  grow((void **)&game->players, &game->player_count, &game->player_capacity,
       sizeof *game->players);
  game->players[game->player_count++] = state;
  for (size_t i = 0; i < game->player_extension_count; i++)
    if (game->player_extensions[i]->attach != NULL)
      game->player_extensions[i]->attach(game->player_extensions[i]->ctx, game,
                                         state);
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "weapon";
  event.has_player = true;
  event.player = actor.id;
  event.weapon = state->weapon;
  event.view_model = qa_q1_services_weapon_model(game, state->weapon, state);
  event.frame = state->weapon_frame;
  event.punch = 0;
  qa_q1_emit(game, event);
  return state;
}

/* --- body / health / lifecycle --- */
bool qa_q1_live(qa_q1_services *game, const qa_q1_actor *entity) {
  return qa_session_actor_registry_is_live(game->host.actors, entity->actor.id);
}
double qa_q1_health(qa_q1_services *game, qa_actor_id actor) {
  const qa_combat_state_result state = qa_gameplay_authority_read(game->host.combat, actor);
  return state.has_value ? state.value.health : 0;
}
void qa_q1_set_body_patch(qa_q1_services *game, qa_q1_actor *entity,
                          const qa_q1_body *patch, bool origin, bool angles,
                          bool velocity, bool bounds, bool ground) {
  qa_q1_body body = qa_q1_get_body(game, entity); /* TS body() throw */
  if (origin) body.origin = patch->origin;
  if (angles) body.angles = patch->angles;
  if (velocity) body.velocity = patch->velocity;
  if (bounds) body.bounds = patch->bounds;
  if (ground) {
    body.has_ground = patch->has_ground;
    body.ground = patch->ground;
  }
  qa_shared_body_table_write(game->host.bodies, entity->actor, qa_q1_body_to_state(&body));
}
void qa_q1_set_body(qa_q1_services *game, qa_q1_actor *entity, const qa_q1_body_patch *patch) {
  qa_q1_body body;
  memset(&body, 0, sizeof body);
  body.origin = patch->origin;
  body.angles = patch->angles;
  body.velocity = patch->velocity;
  body.bounds = patch->bounds;
  body.has_ground = patch->has_ground && patch->ground != NULL;
  if (body.has_ground) body.ground = *patch->ground;
  qa_q1_set_body_patch(game, entity, &body, patch->has_origin, patch->has_angles,
                       patch->has_velocity, patch->has_bounds, patch->has_ground);
}
void qa_q1_launch_projectile_behavior(qa_q1_services *game, qa_q1_actor *entity,
    qa_actor_id shooter, qa_q1_weapon weapon, const char *role) {
  if (!qa_q1_is_player(game, &shooter)) return;
  const qa_weapon_behavior_projectile_port *port = game->host.weapon_behavior;
  if (port == NULL || port->launch == NULL) return; /* TS ?. : argument not evaluated */
  qa_q1_body body = qa_q1_get_body(game, entity);
  qa_actor_id ground = body.ground;
  qa_weapon_behavior_launch input = {0};
  input.projectile = entity->actor;
  input.shooter = shooter;
  input.weapon = qa_q1_services_weapon_item(game, weapon);
  input.role = role;
  input.time_seconds = game->time;
  input.body.origin = body.origin;
  input.body.angles = body.angles;
  input.body.velocity = body.velocity;
  input.body.bounds = body.bounds;
  input.body.ground = body.has_ground ? &ground : NULL;
  qa_weapon_trajectory_update_result update = port->launch(port->ctx, input);
  if (!update.has_value) return;
  qa_q1_callback_registry_project_trajectory(game->named, entity, update.value);
  qa_q1_body patch;
  memset(&patch, 0, sizeof patch);
  patch.origin = update.value.origin;
  patch.angles = update.value.angles;
  patch.velocity = update.value.velocity;
  qa_q1_set_body_patch(game, entity, &patch, true, true, true, false, false);
  qa_q1_link(game, entity);
}
/* TS applyProjectileBehavior: weaponBehavior?.step(actor, body, seconds);
 * the optional chain does not evaluate body() when the port is absent. */
static void apply_projectile_behavior(qa_q1_services *game, qa_owned_actor actor,
                                      double seconds) {
  qa_q1_actor *entity = entity_of(game, actor);
  if (entity == NULL || !qa_q1_live(game, entity)) return;
  const qa_weapon_behavior_projectile_port *port = game->host.weapon_behavior;
  if (port == NULL || port->step == NULL) return;
  qa_q1_body body = qa_q1_get_body(game, entity);
  qa_actor_id ground = body.ground;
  qa_body_state state;
  memset(&state, 0, sizeof state);
  state.origin = body.origin;
  state.angles = body.angles;
  state.velocity = body.velocity;
  state.bounds = body.bounds;
  state.ground = body.has_ground ? &ground : NULL;
  qa_weapon_trajectory_update_result update = port->step(port->ctx, actor, state, seconds);
  if (!update.has_value) return;
  qa_q1_callback_registry_project_trajectory(game->named, entity, update.value);
  qa_q1_body patch;
  memset(&patch, 0, sizeof patch);
  patch.origin = update.value.origin;
  patch.angles = update.value.angles;
  patch.velocity = update.value.velocity;
  qa_q1_set_body_patch(game, entity, &patch, true, true, true, false, false);
  qa_q1_link(game, entity);
}
/* TS private projectilePhysics(entity, elapsed). */
static void projectile_physics(qa_q1_services *game, qa_q1_actor *entity, double elapsed) {
  bool gib = same(entity->movement, "gib");
  const char *physics_edition = game->options.physics_edition != NULL
                                    ? game->options.physics_edition
                                    : game->options.edition;
  if (gib && !same(physics_edition, "rerelease"))
    qa_range_error("Gib movement requires the rerelease physics profile");
  double gravity;
  if (gib) {
    double scale = qa_q1_actor_number(entity, "gravity");
    if (scale == 0 || scale != scale) scale = 1; /* TS || 1 (0/NaN falsy) */
    gravity = scale * qa_q1_option_gravity(game);
  } else {
    gravity = qa_q1_option_gravity(game);
  }
  qa_q1_body body = qa_q1_get_body(game, entity);
  if (body.has_ground) return;
  qa_vec3 velocity = body.velocity;
  if (!same(entity->movement, "flymissile") && !same(entity->movement, "fly"))
    velocity.z = (float)(velocity.z - gravity * elapsed); /* TS Math.fround */
  qa_q1_trace_request request;
  memset(&request, 0, sizeof request);
  request.start = body.origin;
  request.end = qa_q1_vadd(body.origin, qa_q1_vscale(velocity, elapsed));
  request.bounds = body.bounds;
  request.has_ignore = true;
  request.ignore = entity->actor.id;
  request.monsters = !same(entity->solid, "none") && !same(entity->solid, "trigger");
  request.has_missile = true;
  request.missile = same(entity->movement, "flymissile");
  qa_q1_trace trace = qa_q1_host_trace(game, request);
  qa_q1_body patch;
  memset(&patch, 0, sizeof patch);
  patch.origin = trace.end;
  patch.velocity = velocity;
  patch.angles = qa_q1_vadd(body.angles, qa_q1_vscale(entity->angular_velocity, elapsed));
  qa_q1_set_body_patch(game, entity, &patch, true, true, true, false, false);
  qa_q1_link(game, entity);
  if (trace.fraction == 1) return;
  if (trace.sky && entity->projectile != NULL && !same(entity->projectile, "grenade")) {
    qa_q1_remove(game, entity);
    return;
  }
  if (entity->projectile != NULL) {
    qa_q1_projectile_touch(game, entity, trace.has_actor ? &trace.actor : NULL,
                           trace.normal, NULL);
  } else {
    bool has_hit = trace.has_actor || game->world != NULL;
    qa_actor_id hit = trace.has_actor ? trace.actor
                      : game->world != NULL ? game->world->actor.id : trace.actor;
    if (has_hit && entity->touch.fn != NULL)
      qa_q1_call_touch(game, entity, entity->touch, hit, &trace.normal, NULL);
  }
  if (!qa_q1_live(game, entity)) return;
  body = qa_q1_get_body(game, entity);
  bool bounces = same(entity->movement, "bounce") ||
                 (gib && same(physics_edition, "rerelease"));
  double overbounce = bounces ? 1.5 : 1;
  velocity = qa_q1_vsub(body.velocity,
                        qa_q1_vscale(trace.normal,
                                     qa_q1_dot(body.velocity, trace.normal) * overbounce));
  /* TS ground is set only when velocity is the shared ZERO object,
   * i.e. exactly when the stop branch above assigns it. */
  bool stopped = false;
  if (trace.normal.z > 0.7 && (velocity.z < 60 || !bounces)) {
    velocity = QA_Q1_ZERO;
    entity->angular_velocity = QA_Q1_ZERO;
    stopped = true;
  }
  memset(&patch, 0, sizeof patch);
  patch.velocity = velocity;
  patch.has_ground = stopped && trace.has_actor;
  patch.ground = trace.actor;
  qa_q1_set_body_patch(game, entity, &patch, false, false, true, false, true);
  qa_q1_check_water_transition(game, entity);
}
/* TS public applyProjectileBehavior (actor-execution calls it directly). */
void qa_q1_apply_projectile_behavior(qa_q1_services *game, qa_owned_actor actor, double seconds) {
  apply_projectile_behavior(game, actor, seconds);
}
void qa_q1_services_physics_entity(qa_q1_services *game, qa_owned_actor actor, double seconds,
                          double elapsed_seconds) {
  qa_q1_services_set_time(game, seconds);
  game->frame_seconds = elapsed_seconds;
  qa_q1_actor *entity = entity_of(game, actor);
  if (entity == NULL || !qa_q1_live(game, entity)) return;
  apply_projectile_behavior(game, actor, seconds);
  if (same(entity->movement, "push")) {
    const qa_q1_pusher_input input = {actor.id, elapsed_seconds, "translate"};
    qa_q1_pusher_result result = qa_step_q1_pusher(input, game->host.pusher_services(game->host.ctx, game));
    free(result.moved);
  } else if (same(entity->movement, "toss") || same(entity->movement, "bounce") ||
             same(entity->movement, "gib") || same(entity->movement, "fly") ||
             same(entity->movement, "flymissile")) {
    projectile_physics(game, entity, elapsed_seconds);
  }
}
void qa_q1_physics_step(qa_q1_services *game, double seconds, double elapsed_seconds) {
  /* TS iterates a spread copy of the entities map. */
  size_t count = game->entity_count;
  qa_q1_actor **snapshot = NULL;
  if (count > 0) {
    snapshot = xmalloc(count * sizeof *snapshot);
    memcpy(snapshot, game->entities, count * sizeof *snapshot);
  }
  for (size_t i = 0; i < count; i++)
    if (snapshot[i] != NULL) qa_q1_services_physics_entity(game, snapshot[i]->actor, seconds, elapsed_seconds);
  free(snapshot);
  for (size_t i = 0; i < game->player_count; i++) {
    qa_q1_player_state *player = game->players[i];
    if (player == NULL) continue;
    qa_q1_player_frame(game, player->actor, seconds, NULL);
    /* playerFrame may splice this player out; do not skip the next. */
    if (i < game->player_count && game->players[i] != player) i--;
  }
}
void qa_q1_link(qa_q1_services *game, qa_q1_actor *entity) {
  qa_shared_body_table_link(game->host.bodies, entity->actor, NULL);
}
void qa_q1_set_origin(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 origin) {
  qa_q1_body patch;
  memset(&patch, 0, sizeof patch);
  patch.origin = origin;
  qa_q1_set_body_patch(game, entity, &patch, true, false, false, false, false);
  qa_q1_link(game, entity);
}
void qa_q1_set_bounds(qa_q1_services *game, qa_q1_actor *entity, qa_bounds bounds) {
  qa_q1_body patch;
  memset(&patch, 0, sizeof patch);
  patch.bounds = bounds;
  qa_q1_set_body_patch(game, entity, &patch, false, false, false, true, false);
  qa_q1_link(game, entity);
}
static void splice_actor(void **array, size_t *count, size_t element_size,
                         size_t at) {
  memmove((char *)*array + at * element_size,
          (char *)*array + (at + 1) * element_size,
          (*count - at - 1) * element_size);
  (*count)--;
}
void qa_q1_remove(qa_q1_services *game, qa_q1_actor *entity) {
  if (qa_q1_live(game, entity)) qa_session_actor_registry_release(game->host.actors, entity->actor);
}
/* TS host.actors.onRelease listener installed by the constructor. */
static void on_release(void *ctx, qa_owned_actor actor) {
  qa_q1_services *game = ctx;
  for (size_t i = 0; i < game->entity_count; i++)
    if (qa_same_owned_actor(game->entities[i]->actor, actor)) {
      qa_q1_actor *entity = game->entities[i];
      splice_actor((void **)&game->entities, &game->entity_count, sizeof *game->entities, i);
      grow((void **)&game->released_entities, &game->released_entity_count,
           &game->released_entity_capacity, sizeof *game->released_entities);
      game->released_entities[game->released_entity_count++] = entity;
      break;
    }
  qa_q1_set_authored_target(game, actor.id, NULL);
  qa_q1_set_mission(game, actor.id, NULL);
  for (size_t i = 0; i < game->player_count; i++)
    if (qa_same_owned_actor(game->players[i]->actor, actor)) {
      qa_q1_player_state *player = game->players[i];
      splice_actor((void **)&game->players, &game->player_count, sizeof *game->players, i);
      grow((void **)&game->released_players, &game->released_player_count,
           &game->released_player_capacity, sizeof *game->released_players);
      game->released_players[game->released_player_count++] = player;
      break;
    }
  game->host.cancel_think(game->host.ctx, actor);
}
void qa_q1_schedule(qa_q1_services *game, qa_q1_actor *entity, double delay,
                    qa_q1_bound_action callback) {
  qa_q1_schedule_at(game, entity, (float)(game->time + delay), callback);
}
void qa_q1_schedule_at(qa_q1_services *game, qa_q1_actor *entity,
                       double due_seconds, qa_q1_bound_action callback) {
  entity->next_think = (float)due_seconds; /* TS Math.fround */
  entity->think = callback;
  if (same(entity->movement, "push")) {
    game->host.cancel_think(game->host.ctx, entity->actor);
    return;
  }
  game->host.schedule_think(game->host.ctx, entity->actor, entity->next_think);
}
void qa_q1_cancel(qa_q1_services *game, qa_q1_actor *entity) {
  entity->next_think = -1;
  memset(&entity->think, 0, sizeof entity->think);
  game->host.cancel_think(game->host.ctx, entity->actor);
}
void qa_q1_register_damage_source_effects(qa_q1_services *game, const char *id, qa_q1_damage_source_effects effects) {
  for (size_t i = 0; i < game->source_damage_effect_count; i++)
    if (same(game->source_damage_effects[i].id, id))
      qa_range_error("Duplicate Q1 damage source effects: %s", id);
  grow((void **)&game->source_damage_effects, &game->source_damage_effect_count,
       &game->source_damage_effect_capacity, sizeof *game->source_damage_effects);
  const size_t length = strlen(id) + 1;
  char *copy = xmalloc(length);
  memcpy(copy, id, length);
  game->source_damage_effects[game->source_damage_effect_count].id = copy;
  game->source_damage_effects[game->source_damage_effect_count].effects = effects;
  game->source_damage_effect_count++;
}
/* TS prepareSourceDamage. */
static bool prepare_source_damage(qa_q1_services *game, bool after_quad, qa_damage_request request, double initial,
                                  qa_combat_state target, const qa_combat_state *attacker, qa_damage_preparation *out) {
  double amount = initial;
  for (size_t i = 0; i < game->source_damage_effect_count; i++) {
    const qa_q1_damage_source_effects *effects = &game->source_damage_effects[i].effects;
    bool (*phase)(void *, qa_damage_request, double, qa_combat_state, const qa_combat_state *, qa_damage_preparation *) =
        after_quad ? effects->after_quad : effects->before_quad;
    qa_damage_preparation result;
    if (phase == NULL || !phase(effects->ctx, request, amount, target, attacker, &result)) continue;
    if (!result.is_continue) { *out = result; return true; }
    amount = result.amount;
  }
  out->is_continue = true;
  out->amount = amount;
  return true;
}
static bool effects_before_quad(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                                const qa_combat_state *attacker, qa_damage_preparation *out) {
  return prepare_source_damage(ctx, false, request, damage, target, attacker, out);
}
static bool effects_after_quad(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                               const qa_combat_state *attacker, qa_damage_preparation *out) {
  return prepare_source_damage(ctx, true, request, damage, target, attacker, out);
}
static bool effects_armor_allowed(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                                  const qa_combat_state *attacker) {
  qa_q1_services *game = ctx;
  for (size_t i = 0; i < game->source_damage_effect_count; i++) {
    const qa_q1_damage_source_effects *effects = &game->source_damage_effects[i].effects;
    if (effects->armor_allowed != NULL && !effects->armor_allowed(effects->ctx, request, damage, target, attacker)) return false;
  }
  return true;
}
static bool effects_protection_applies(void *ctx, qa_damage_request request, qa_combat_state target,
                                       const qa_combat_state *attacker) {
  qa_q1_services *game = ctx;
  for (size_t i = 0; i < game->source_damage_effect_count; i++) {
    const qa_q1_damage_source_effects *effects = &game->source_damage_effects[i].effects;
    if (effects->protection_applies != NULL && !effects->protection_applies(effects->ctx, request, target, attacker)) return false;
  }
  return true;
}
static bool effects_before_health(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                                  const qa_combat_state *attacker) {
  qa_q1_services *game = ctx;
  for (size_t i = 0; i < game->source_damage_effect_count; i++) {
    const qa_q1_damage_source_effects *effects = &game->source_damage_effects[i].effects;
    if (effects->before_health != NULL && !effects->before_health(effects->ctx, request, damage, target, attacker)) return false;
  }
  return true;
}
static double effects_after_armor(void *ctx, qa_damage_request request, double take, qa_combat_state target,
                                  const qa_combat_state *attacker) {
  qa_q1_services *game = ctx;
  double amount = take;
  for (size_t i = 0; i < game->source_damage_effect_count; i++) {
    const qa_q1_damage_source_effects *effects = &game->source_damage_effects[i].effects;
    if (effects->after_armor != NULL) amount = effects->after_armor(effects->ctx, request, amount, target, attacker);
  }
  return amount;
}
static qa_q1_lethal_health effects_lethal_health(void *ctx, qa_damage_request request, double proposed, qa_combat_state target,
                                                 const qa_combat_state *attacker) {
  qa_q1_services *game = ctx;
  qa_q1_lethal_health result = {true, proposed, "death"};
  for (size_t i = 0; i < game->source_damage_effect_count; i++) {
    const qa_q1_damage_source_effects *effects = &game->source_damage_effects[i].effects;
    if (effects->lethal_health != NULL) {
      const qa_q1_lethal_health next = effects->lethal_health(effects->ctx, request, result.health, target, attacker);
      if (next.has_value) result = next;
    }
    if (same(result.reaction, "none")) break;
  }
  return result;
}
qa_q1_damage_source_effects qa_q1_damage_source_effects_of(qa_q1_services *game) {
  const qa_q1_damage_source_effects effects = {game, effects_before_quad, effects_after_quad, effects_armor_allowed,
      effects_protection_applies, effects_before_health, effects_after_armor, effects_lethal_health};
  return effects;
}
typedef struct actor_binding { qa_q1_services *game; qa_q1_actor *entity; } actor_binding;
static double seconds_of(qa_source_time time) {
  return time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000;
}
static void binding_think(void *ctx, qa_owned_actor self, qa_frame_context frame) {
  (void)self;
  actor_binding *binding = ctx;
  qa_q1_services_set_time(binding->game, seconds_of(frame.time));
  binding->game->frame_seconds = seconds_of(frame.elapsed);
  qa_q1_run_think(binding->game, binding->entity);
}
static void binding_touch(void *ctx, qa_touch_contact contact) {
  actor_binding *binding = ctx;
  const qa_q1_bound_touch touch = binding->entity->touch;
  if (touch.fn != NULL)
    touch.fn(touch.ctx, binding->game, binding->entity, contact.other,
             contact.plane != NULL ? &contact.plane->normal : NULL, contact.surface);
}
static void binding_use(void *ctx, qa_owned_actor self, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)self;
  actor_binding *binding = ctx;
  const qa_q1_bound_use use = binding->entity->use;
  if (use.fn != NULL) use.fn(use.ctx, binding->game, binding->entity, other, activator);
}
static void binding_pain(void *ctx, qa_pain_reaction reaction) {
  actor_binding *binding = ctx;
  const qa_q1_bound_pain pain = binding->entity->pain;
  if (pain.fn != NULL) pain.fn(pain.ctx, binding->game, binding->entity, reaction.attacker, reaction.damage);
}
static void binding_die(void *ctx, qa_death_reaction reaction) {
  actor_binding *binding = ctx;
  qa_q1_services *game = binding->game;
  qa_q1_actor *entity = binding->entity;
  /* Killed runs in the victim source even when another game supplied the attack policy. */
  if (qa_q1_health(game, entity->actor.id) < -99) qa_gameplay_authority_set_health(game->host.combat, entity->actor, -99);
  if (entity->monster != NULL && !same(entity->movement, "push") && !same(entity->movement, "none")) {
    entity->monster->has_enemy = reaction.pain.attacker != NULL;
    if (reaction.pain.attacker != NULL) entity->monster->enemy = *reaction.pain.attacker;
  }
  const qa_q1_bound_die die = entity->die;
  if (die.fn != NULL) die.fn(die.ctx, game, entity, reaction.pain.attacker);
}
void qa_q1_bind_actor_callbacks(qa_q1_services *game, qa_q1_actor *entity) {
  actor_binding *binding = xmalloc(sizeof *binding);
  binding->game = game;
  binding->entity = entity;
  grow((void **)&game->actor_bindings, &game->actor_binding_count, &game->actor_binding_capacity,
       sizeof *game->actor_bindings);
  game->actor_bindings[game->actor_binding_count++] = binding;
  const qa_actor_callbacks callbacks = {binding, binding_think, binding_touch, binding_use, binding_pain, binding_die};
  qa_actor_callback_table_bind(game->host.callbacks, entity->actor, callbacks);
}
void qa_q1_run_think(qa_q1_services *game, qa_q1_actor *entity) {
  /* TS bindActorCallbacks think dispatch. */
  qa_q1_bound_action callback = entity->think;
  memset(&entity->think, 0, sizeof entity->think);
  entity->next_think = -1;
  if (callback.fn != NULL) callback.fn(callback.ctx, game, entity);
}
void qa_q1_resume_thinks(qa_q1_services *game) {
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (!same(entity->movement, "push") && entity->think.fn != NULL && entity->next_think >= 0)
      game->host.schedule_think(game->host.ctx, entity->actor,
                                entity->next_think);
  }
}

/* --- events --- */
void qa_q1_sound(qa_q1_services *game, qa_actor_id actor, const char *path,
                 qa_q1_sound_channel channel, double attenuation, double volume) {
  const qa_body_state_result read = qa_shared_body_table_read(game->host.bodies, actor);
  if (!read.has_value) qa_range_error("Missing Q1 sound emitter body"); /* TS Error throw */
  const qa_body_state body = read.value;
  qa_vec3 origin = qa_q1_vadd(
      body.origin, qa_q1_vscale(qa_q1_vadd(body.bounds.min, body.bounds.max), 0.5));
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "sound";
  event.has_actor = true;
  event.actor = actor;
  event.has_origin = true;
  event.origin = origin;
  event.path = path;
  event.sound_channel = channel;
  event.volume = volume;
  event.attenuation = attenuation;
  qa_q1_emit(game, event);
}
void qa_q1_sound_defaults(qa_q1_services *game, qa_actor_id actor,
                          const char *path) {
  qa_q1_sound(game, actor, path, "voice", 1, 1);
}
void qa_q1_message(qa_q1_services *game, const qa_actor_id *player,
                   const char *text, bool center, const qa_q1_msg_arg *args,
                   size_t arg_count) {
  if (player == NULL || !qa_q1_is_player(game, player) || text[0] == '\0') return;
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "message";
  event.has_player = true;
  event.player = *player;
  event.text = text;
  event.center = center;
  if (arg_count > 0) {
    event.args = args;
    event.arg_count = arg_count;
  }
  qa_q1_emit(game, event);
}
void qa_q1_effect(qa_q1_services *game, const char *effect, qa_vec3 origin,
                  const qa_actor_id *actor, double amount) {
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "effect";
  event.effect = effect;
  event.has_origin = true;
  event.origin = origin;
  if (actor != NULL) {
    event.has_actor = true;
    event.actor = *actor;
  }
  event.amount = amount;
  qa_q1_emit(game, event);
}
size_t qa_q1_find(qa_q1_services *game, const char *targetname,
                  qa_q1_actor **out, size_t capacity) {
  if (targetname[0] == '\0') return 0;
  size_t found = 0;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (same(entity->targetname, targetname)) {
      if (out != NULL && found < capacity) out[found] = entity;
      found++;
    }
  }
  return found;
}
bool qa_q1_path_goal(qa_q1_services *game, const char *name, qa_actor_id *out) {
  if (name[0] != '\0') {
    qa_q1_actor *first = NULL;
    if (qa_q1_find(game, name, &first, 1) == 0) return false;
    *out = first->actor.id;
    return true;
  }
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  if (count == 0) return false;
  *out = players[0];
  return true;
}
size_t qa_q1_presentations(qa_q1_services *game, qa_q1_presentation *out,
                           size_t capacity) {
  size_t found = 0;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (out != NULL && found < capacity) {
      qa_q1_presentation *p = &out[found];
      memset(p, 0, sizeof *p);
      p->actor = entity->actor.id;
      p->classname = entity->classname;
      p->model = entity->model;
      p->frame = entity->frame;
      p->skin = entity->skin;
      p->effects = entity->effects;
      p->solid = entity->solid;
      p->movement = entity->movement;
      p->targetname = entity->targetname;
      p->has_source_ordinal = entity->has_source_ordinal;
      p->source_ordinal = entity->source_ordinal;
    }
    found++;
  }
  return found;
}

/* --- targets / powerups / punch --- */
double qa_q1_powerup_expires(qa_q1_services *game, qa_actor_id actor,
                             qa_q1_powerup powerup) {
  if (game->host.powerup_expires != NULL)
    return game->host.powerup_expires(game->host.ctx, actor, powerup);
  qa_q1_player_state *player = qa_q1_player(game, actor);
  return player == NULL ? 0 : qa_q1_player_powerup_get(player, powerup);
}
qa_q1_monster_target_result qa_q1_monster_target(qa_q1_services *game,
                                                 qa_actor_id actor) {
  if (game->host.monster_target != NULL)
    return game->host.monster_target(game->host.ctx, actor);
  qa_q1_monster_target_result result;
  memset(&result, 0, sizeof result);
  qa_q1_actor *entity = qa_q1_get_entity(game, &actor);
  qa_q1_player_state *player = qa_q1_player(game, actor);
  if (entity == NULL && player == NULL) return result;
  result.has_value = true;
  result.value.view_height = player == NULL ? 25 : 22;
  result.value.notarget =
      (qa_js_to_int32(entity != NULL ? entity->movement_flags : 0) & 128) != 0;
  result.value.invisible =
      (player != NULL ? qa_q1_player_powerup_get(player, "invisibility") : 0) > game->time;
  result.value.has_light_level = false;
  result.value.has_hostile_until = player != NULL;
  result.value.hostile_until = player != NULL ? player->hostile_until : 0;
  return result;
}
bool qa_q1_can_damage(qa_q1_services *game, qa_actor_id target,
                      qa_actor_id inflictor) {
  const qa_body_state_result target_read = qa_shared_body_table_read(game->host.bodies, target);
  const qa_body_state_result source_read = qa_shared_body_table_read(game->host.bodies, inflictor);
  if (!target_read.has_value || !source_read.has_value) return false;
  const qa_body_state target_body = target_read.value;
  const qa_body_state source = source_read.value;
  bool push = qa_q1_get_source_target(game, target).push;
  qa_vec3 destination =
      push ? qa_q1_vadd(target_body.origin,
                        qa_q1_vscale(qa_q1_vadd(target_body.bounds.min,
                                                target_body.bounds.max),
                                     0.5))
           : target_body.origin;
  static const qa_vec3 plain_offsets[] = {{0, 0, 0}};
  static const qa_vec3 spread_offsets[] = {
      {0, 0, 0}, {15, 15, 0}, {-15, -15, 0}, {-15, 15, 0}, {15, -15, 0}};
  const qa_vec3 *offsets = push ? plain_offsets : spread_offsets;
  size_t count = push ? 1 : 5;
  for (size_t i = 0; i < count; i++) {
    qa_q1_trace_request request;
    memset(&request, 0, sizeof request);
    request.start = source.origin;
    request.end = qa_q1_vadd(destination, offsets[i]);
    request.bounds = QA_Q1_POINT;
    request.has_ignore = true;
    request.ignore = inflictor;
    request.monsters = false;
    qa_q1_trace trace = qa_q1_host_trace(game, request);
    if (trace.fraction == 1 ||
        (trace.has_actor && qa_same_actor(trace.actor, target)))
      return true;
  }
  return false;
}
void qa_q1_weapon_punch(qa_q1_services *game, qa_q1_player_state *player,
                        double pitch) {
  if (pitch == 0) return;
  if (!game->host.has_punch_angles) {
    player->punch_angles.x = pitch;
  } else {
    qa_vec3 angles = game->host.punch_read(game->host.ctx, player->actor.id);
    angles.x = pitch;
    game->host.punch_write(game->host.ctx, player->actor.id, angles);
  }
}
bool qa_q1_advance_punch(qa_q1_services *game, qa_actor_id actor, double elapsed,
                         const qa_numeric_operations *numeric, qa_vec3 *out) {
  qa_q1_player_state *player = qa_q1_player(game, actor);
  if (player == NULL) return false;
  qa_vec3 current = game->host.has_punch_angles
                        ? game->host.punch_read(game->host.ctx, actor)
                        : player->punch_angles;
  qa_vec3 punch = qa_q1_drop_punch(current, elapsed, numeric);
  if (!game->host.has_punch_angles)
    player->punch_angles = punch;
  else
    game->host.punch_write(game->host.ctx, actor, punch);
  if (out != NULL) *out = punch;
  return true;
}
void qa_q1_give_powerup(qa_q1_services *game, qa_q1_player_state *player,
                        qa_q1_powerup powerup, double duration) {
  double expires = (float)(game->time + duration); /* TS Math.fround */
  qa_q1_player_powerup_set(player, powerup, expires);
  game->host.powerup(game->host.ctx, player->actor, powerup, expires);
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "powerup";
  event.has_player = true;
  event.player = player->actor.id;
  event.powerup = powerup;
  event.expires = expires;
  qa_q1_emit(game, event);
}
const qa_monster_mission *qa_q1_mission(qa_q1_services *game,
                                           qa_actor_id actor) {
  for (size_t i = 0; i < game->mission_count; i++)
    if (qa_same_actor(game->missions[i].actor, actor))
      return game->missions[i].mission;
  return NULL;
}
void qa_q1_set_mission(qa_q1_services *game, qa_actor_id actor,
                       const qa_monster_mission *mission) {
  for (size_t i = 0; i < game->mission_count; i++)
    if (qa_same_actor(game->missions[i].actor, actor)) {
      if (mission == NULL) {
        splice_actor((void **)&game->missions, &game->mission_count,
                     sizeof *game->missions, i);
      } else {
        game->missions[i].mission = mission;
      }
      return;
    }
  if (mission == NULL) return;
  grow((void **)&game->missions, &game->mission_count, &game->mission_capacity,
       sizeof *game->missions);
  game->missions[game->mission_count].actor = actor;
  game->missions[game->mission_count].mission = mission;
  game->mission_count++;
}
qa_authored_target *qa_q1_authored_target(qa_q1_services *game, qa_actor_id actor) {
  for (size_t i = 0; i < game->authored_target_count; i++)
    if (qa_same_actor(game->authored_targets[i].actor, actor)) return game->authored_targets[i].target;
  return NULL;
}
void qa_q1_set_authored_target(qa_q1_services *game, qa_actor_id actor, qa_authored_target *target) {
  for (size_t i = 0; i < game->authored_target_count; i++)
    if (qa_same_actor(game->authored_targets[i].actor, actor)) {
      if (target == NULL)
        splice_actor((void **)&game->authored_targets, &game->authored_target_count,
                     sizeof *game->authored_targets, i);
      else
        game->authored_targets[i].target = target;
      return;
    }
  if (target == NULL) return;
  grow((void **)&game->authored_targets, &game->authored_target_count,
       &game->authored_target_capacity, sizeof *game->authored_targets);
  game->authored_targets[game->authored_target_count].actor = actor;
  game->authored_targets[game->authored_target_count].target = target;
  game->authored_target_count++;
}

/* --- frame / travel / selection --- */
qa_q1_basis qa_q1_make_vectors(qa_q1_services *game, qa_vec3 angles) {
  game->basis = qa_q1_vectors(angles);
  return game->basis;
}
void qa_q1_begin_frame(qa_q1_services *game, double seconds,
                       double elapsed_seconds) {
  qa_q1_services_set_time(game, seconds);
  game->frame_seconds = elapsed_seconds;
}
void qa_q1_services_player_input(qa_q1_services *game, qa_owned_actor actor, bool attack,
                        bool jump, const double *teleport_until) {
  qa_q1_player_state *player = player_of(game, actor);
  if (player == NULL) return;
  player->attack_held = attack;
  player->jump_held = jump;
  if (teleport_until != NULL) player->teleport_until = *teleport_until;
}
void qa_q1_travel(qa_q1_services *game, const char *map,
                  const qa_actor_id *cause) {
  size_t length = strlen(map) + 4;
  char *named = xmalloc(length);
  memcpy(named, "q1:", 3);
  memcpy(named + 3, map, strlen(map) + 1);
  qa_transition_intent intent;
  memset(&intent, 0, sizeof intent);
  intent.kind = "campaign-level";
  intent.campaign = game->options.campaign;
  intent.map = named;
  intent.spawn_point = "";
  intent.gates = NULL;
  intent.gate_count = 0;
  intent.cause = cause;
  game->host.transition(game->host.ctx, intent);
  free(named);
}
void qa_q1_begin_intermission(qa_q1_services *game, const char *map,
                              const qa_actor_id *cause) {
  qa_q1_actor *selected = NULL;
  size_t spots = 0;
  for (size_t i = 0; i < game->entity_count; i++)
    if (game->entities[i] != NULL &&
        same(game->entities[i]->classname, "info_intermission"))
      spots++;
  if (spots > 0) {
    double cycle = (float)(qa_q1_host_random(game) * 4);
    size_t index = 0;
    while (cycle > 1) {
      index = (index + 1) % spots;
      cycle = (float)(cycle - 1);
    }
    size_t seen = 0;
    for (size_t i = 0; i < game->entity_count; i++)
      if (game->entities[i] != NULL &&
          same(game->entities[i]->classname, "info_intermission")) {
        if (seen == index) selected = game->entities[i];
        seen++;
      }
  } else {
    for (size_t i = 0; selected == NULL && i < game->entity_count; i++)
      if (game->entities[i] != NULL &&
          same(game->entities[i]->classname, "info_player_start"))
        selected = game->entities[i];
    for (size_t i = 0; selected == NULL && i < game->entity_count; i++)
      if (game->entities[i] != NULL &&
          same(game->entities[i]->classname, "testplayerstart"))
        selected = game->entities[i];
  }
  if (selected == NULL) qa_range_error("FindIntermission: no spot");
  qa_q1_body body = qa_q1_get_body(game, selected);
  double exit_after = game->time + (game->options.deathmatch != 0 ? 5 : 2);
  free(game->intermission_map);
  game->intermission_map = dup_string(map);
  game->has_intermission = true;
  game->has_intermission_cause = cause != NULL;
  if (cause != NULL) game->intermission_cause = *cause;
  game->intermission_exit_after = exit_after;
  if (game->source_fog_push.fn != NULL && game->world != NULL) {
    size_t count = 0;
    const qa_actor_id *players = game->host.players(game->host.ctx, &count);
    for (size_t i = 0; i < count; i++) (void)game->source_fog_push.fn(game->source_fog_push.ctx, players[i], game->world);
  }
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "intermission";
  event.has_origin = true;
  event.origin = body.origin;
  event.angles = qa_q1_actor_vector(selected, "mangle");
  event.map = map;
  event.exit_after = exit_after;
  event.track = 3;
  qa_q1_emit(game, event);
}
bool qa_q1_request_intermission_exit(qa_q1_services *game, double seconds,
                                     bool pressed) {
  if (!game->has_intermission || seconds < game->intermission_exit_after ||
      !pressed)
    return false;
  char *map = game->intermission_map;
  qa_actor_id cause = game->intermission_cause;
  bool has_cause = game->has_intermission_cause;
  qa_q1_services_set_time(game, seconds);
  game->has_intermission = false;
  game->intermission_map = NULL;
  qa_q1_travel(game, map, has_cause ? &cause : NULL);
  free(map);
  return true;
}
bool qa_q1_select_weapon(qa_q1_services *game, qa_owned_actor actor,
                         qa_q1_weapon weapon) {
  qa_q1_player_state *player = player_of(game, actor);
  if (player == NULL ||
      qa_shared_inventory_table_count(game->host.inventory, actor.id,
                                      qa_q1_services_weapon_item(game, weapon)) == 0)
    return false;
  if (!qa_q1_is_base_weapon(weapon) && registered(game, weapon) == NULL)
    return false;
  const qa_q1_weapon_definition *definition = registered(game, weapon);
  if (definition != NULL && definition->available != NULL &&
      !definition->available(definition->ctx, game, player))
    return false;
  player->weapon = weapon;
  player->weapon_frame = 0;
  player->continuous_firing = false;
  player->weapon_animation_at = -1;
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "weapon";
  event.has_player = true;
  event.player = actor.id;
  event.weapon = weapon;
  event.view_model = qa_q1_services_weapon_model(game, weapon, player);
  event.frame = 0;
  event.punch = 0;
  qa_q1_emit(game, event);
  return true;
}
qa_q1_weapon qa_q1_choose_best(qa_q1_services *game, qa_owned_actor actor) {
  return qa_q1_best_weapon(game, actor);
}
qa_q1_weapon qa_q1_choose_best_counted(qa_q1_services *game, qa_owned_actor actor,
                                       qa_q1_ammo_count_fn count, void *count_ctx) {
  return qa_q1_best_weapon_counted(game, actor, count, count_ctx);
}
void qa_q1_weapon_frame(qa_q1_services *game, qa_owned_actor actor,
                        double seconds) {
  qa_q1_services_set_time(game, seconds);
  qa_q1_player_state *player = player_of(game, actor);
  if (player == NULL) return;
  const qa_q1_weapon_definition *extension = registered(game, player->weapon);
  if (extension != NULL && extension->animate != NULL)
    extension->animate(extension->ctx, game, player, seconds);
  if ((extension == NULL || extension->animate == NULL) &&
      qa_q1_is_base_weapon(player->weapon) && !player->continuous_firing &&
      player->weapon_animation_at >= 0) {
    double frame = floor((seconds - player->weapon_animation_at) / 0.1);
    double count = same(player->weapon, "axe") ? 4 : 6;
    double next = frame >= count ? 0 : player->weapon_animation_base + frame;
    if (next != player->weapon_frame) {
      player->weapon_frame = next;
      qa_q1_event event;
      memset(&event, 0, sizeof event);
      event.kind = "weapon";
      event.has_player = true;
      event.player = actor.id;
      event.weapon = player->weapon;
      event.view_model =
          qa_q1_services_weapon_model(game, player->weapon, player);
      event.frame = next;
      event.punch = 0;
      qa_q1_emit(game, event);
    }
    if (frame >= count) player->weapon_animation_at = -1;
  }
}
void qa_q1_player_after_physics(qa_q1_services *game, qa_owned_actor actor,
                                double seconds) {
  qa_q1_services_set_time(game, seconds);
  qa_q1_player_state *player = player_of(game, actor);
  if (player == NULL) return;
  for (size_t i = 0; i < game->player_extension_count; i++)
    if (game->player_extensions[i]->after_physics != NULL)
      game->player_extensions[i]->after_physics(
          game->player_extensions[i]->ctx, game, player, seconds);
}
/* TS powerupCues WeakMap<player, Map<powerup, cue>> rows. */
static size_t powerup_cue(qa_q1_services *game, const qa_q1_player_state *player, qa_q1_powerup powerup,
                          bool create) {
  for (size_t i = 0; i < game->powerup_cue_count; i++)
    if (game->powerup_cues[i].player == player && same(game->powerup_cues[i].powerup, powerup)) return i;
  if (!create) return SIZE_MAX;
  grow((void **)&game->powerup_cues, &game->powerup_cue_count, &game->powerup_cue_capacity,
       sizeof *game->powerup_cues);
  const size_t at = game->powerup_cue_count++;
  game->powerup_cues[at].player = player;
  game->powerup_cues[at].powerup = powerup;
  game->powerup_cues[at].warned = false;
  game->powerup_cues[at].flash_at = 0;
  game->powerup_cues[at].hum = 0;
  return at;
}
/* TS private powerupWarnings. */
static void powerup_warnings(qa_q1_services *game, qa_owned_actor actor, qa_q1_player_state *player,
                             double seconds) {
  static const struct { const char *powerup, *message, *sound; } WARNINGS[4] = {
      {"invisibility", "$qc_ring_fade", "items/inv2.wav"},
      {"invulnerability", "$qc_protection_fade", "items/protect2.wav"},
      {"quad", "$qc_quad_fade", "items/damage2.wav"},
      {"suit", "$qc_biosuit_fade", "items/suit2.wav"}};
  for (size_t p = 0; p < player->powerup_count; p++) {
    const qa_q1_powerup powerup = player->powerups[p].kind;
    const double expires = player->powerups[p].expires;
    size_t w = 0;
    while (w < 4 && !same(WARNINGS[w].powerup, powerup)) w++;
    if (w == 4 || expires <= seconds) continue;
    const size_t cue = powerup_cue(game, player, powerup, true);
    if (same(powerup, "invisibility") && game->powerup_cues[cue].hum < seconds) {
      qa_q1_sound(game, actor.id, "items/inv3.wav", "auto", 3, 0.5);
      game->powerup_cues[cue].hum = seconds + (qa_q1_host_random(game) * 3 + 1);
    }
    if (expires >= seconds + 3) { game->powerup_cues[cue].warned = false; continue; }
    const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor.id);
    if (!game->powerup_cues[cue].warned) {
      game->powerup_cues[cue].warned = true;
      game->powerup_cues[cue].flash_at = seconds + 1;
      qa_q1_message(game, &actor.id, WARNINGS[w].message, false, NULL, 0);
      qa_q1_sound(game, actor.id, WARNINGS[w].sound, "auto", 1, 1);
      if (body.has_value) qa_q1_effect(game, "pickup", body.value.origin, &actor.id, 1);
    } else if (game->powerup_cues[cue].flash_at < seconds) {
      game->powerup_cues[cue].flash_at = seconds + 1;
      if (body.has_value) qa_q1_effect(game, "pickup", body.value.origin, &actor.id, 1);
    }
  }
}
static bool waterborne_delete(qa_q1_services *game, const qa_q1_player_state *player) {
  for (size_t i = 0; i < game->waterborne_count; i++)
    if (game->waterborne[i] == player) {
      splice_actor((void **)&game->waterborne, &game->waterborne_count, sizeof *game->waterborne, i);
      return true;
    }
  return false;
}
static bool waterborne_has(const qa_q1_services *game, const qa_q1_player_state *player) {
  for (size_t i = 0; i < game->waterborne_count; i++)
    if (game->waterborne[i] == player) return true;
  return false;
}
void qa_q1_player_frame(qa_q1_services *game, qa_owned_actor actor, double seconds,
                        const double *water_level) {
  qa_q1_services_set_time(game, seconds);
  qa_q1_player_state *player = player_of(game, actor);
  if (player == NULL) return;
  if (water_level != NULL) player->water_level = *water_level;
  for (size_t i = 0; i < game->player_extension_count; i++)
    if (game->player_extensions[i]->frame != NULL)
      game->player_extensions[i]->frame(game->player_extensions[i]->ctx, game,
                                        player, seconds);
  qa_q1_weapon_frame(game, actor, seconds);
  if (player->mega_rot_at >= 0 && player->mega_rot_at <= seconds) {
    double health = qa_q1_health(game, actor.id);
    if (health > player->max_health) {
      qa_gameplay_authority_set_health(game->host.combat, actor, health - 1);
      player->mega_rot_at = seconds + 1;
    } else {
      player->mega_rot_at = -1;
    }
  }
  powerup_warnings(game, actor, player, seconds);
  /* Copy kinds first: delete splices the array TS iterates as a Map. */
  qa_q1_powerup kinds[16];
  double expiries[16];
  size_t kind_count = player->powerup_count;
  for (size_t i = 0; i < kind_count; i++) {
    kinds[i] = player->powerups[i].kind;
    expiries[i] = player->powerups[i].expires;
  }
  for (size_t i = 0; i < kind_count; i++) {
    if (expiries[i] > seconds) continue;
    qa_q1_player_powerup_delete(player, kinds[i]);
    game->host.powerup(game->host.ctx, actor, kinds[i], 0);
    const size_t cue = powerup_cue(game, player, kinds[i], false);
    if (cue != SIZE_MAX)
      splice_actor((void **)&game->powerup_cues, &game->powerup_cue_count, sizeof *game->powerup_cues, cue);
    if (same(kinds[i], "q3:flight") && game->host.set_gravity != NULL)
      qa_q1_set_gravity(game, actor.id, 1);
  }
  /* Foreign Q3 powerups granted by Q3-authored items on this primary. */
  if (qa_q1_player_powerup_get(player, "q3:regen") > seconds &&
      game->frame_seconds > 0) {
    double health = qa_q1_health(game, actor.id);
    if (health > 0 && health < 200) {
      double healed = health + 5 * game->frame_seconds;
      qa_gameplay_authority_set_health(game->host.combat, actor, healed < 200 ? healed : 200);
    }
  }
  if (qa_q1_player_powerup_get(player, "q3:haste") > seconds &&
      game->frame_seconds > 0 && player->attack_finished > seconds) {
    double reduced = player->attack_finished - 0.3 * game->frame_seconds;
    player->attack_finished = reduced > seconds ? reduced : seconds;
  }
  if (game->has_intermission || qa_q1_health(game, actor.id) < 0) return;
  bool suit = qa_q1_player_powerup_get(player, "suit") > seconds;
  bool lava_suit = qa_q1_player_powerup_get(player, "mg3:lavasuit") > seconds;
  if (player->water_level != 3) {
    if (player->air_finished < seconds) qa_q1_sound(game, actor.id, "player/gasp2.wav", "voice", 1, 1);
    else if (player->air_finished < seconds + 9) qa_q1_sound(game, actor.id, "player/gasp1.wav", "voice", 1, 1);
  }
  qa_actor_id world_id = game->world != NULL ? game->world->actor.id : actor.id;
  const qa_actor_id *world_attacker = game->world != NULL ? &world_id : NULL;
  if (player->water_level != 3 || suit || lava_suit) {
    player->air_finished = seconds + 12;
    player->drown_damage = 2;
  } else if (player->air_finished < seconds && player->drown_at < seconds) {
    player->drown_damage += 2;
    if (player->drown_damage > 15) player->drown_damage = 10;
    (void)qa_q1_damage(game, actor.id, world_id, world_attacker, player->drown_damage,
                       NULL, "direct", "drown", NULL);
    player->drown_at = seconds + 1;
  }
  if (player->water_level == 0) {
    if (waterborne_delete(game, player)) qa_q1_sound(game, actor.id, "misc/outwater.wav", "body", 1, 1);
    return;
  }
  const qa_body_state_result read = qa_shared_body_table_read(game->host.bodies, actor.id);
  if (!read.has_value) return;
  qa_vec3 point = read.value.origin;
  point.z += read.value.bounds.min.z + 1;
  const char *contents = game->host.contents(game->host.ctx, point);
  if (player->hazard_at < seconds) {
    if (same(contents, "lava") && !lava_suit) {
      player->hazard_at = seconds + (suit ? 1 : 0.2);
      (void)qa_q1_damage(game, actor.id, world_id, world_attacker,
                         10 * player->water_level, NULL, "direct", "lava", NULL);
    } else if (same(contents, "slime") && !suit && !lava_suit) {
      player->hazard_at = seconds + 1;
      (void)qa_q1_damage(game, actor.id, world_id, world_attacker,
                         4 * player->water_level, NULL, "direct", "slime", NULL);
    }
  }
  if (!waterborne_has(game, player)) {
    grow((void **)&game->waterborne, &game->waterborne_count, &game->waterborne_capacity,
         sizeof *game->waterborne);
    game->waterborne[game->waterborne_count++] = player;
    const char *entry = same(contents, "lava") ? "player/inlava.wav"
                        : same(contents, "water") ? "player/inh2o.wav"
                        : same(contents, "slime") ? "player/slimbrn2.wav" : NULL;
    if (entry != NULL) qa_q1_sound(game, actor.id, entry, "body", 1, 1);
    /* The first contact tick is followed by another on the next frame. */
    player->hazard_at = 0;
  }
}
bool qa_q1_attack(qa_q1_services *game, qa_owned_actor actor, qa_vec3 view_angles,
                  double seconds, double water_level) {
  qa_q1_services_set_time(game, seconds);
  qa_q1_player_state *player = player_of(game, actor);
  if (player == NULL) qa_range_error("Player has no Q1 weapon state");
  player->view_angles = view_angles;
  player->water_level = water_level;
  return qa_q1_fire_weapon(game, player);
}
bool qa_q1_weapon_input(qa_q1_services *game, qa_owned_actor actor, bool pressed,
                        qa_vec3 view_angles, double seconds, double water_level) {
  qa_q1_player_state *player = player_of(game, actor);
  if (player == NULL) qa_range_error("Player has no Q1 weapon state");
  qa_q1_services_set_time(game, seconds);
  player->view_angles = view_angles;
  player->water_level = water_level;
  player->attack_held = pressed;
  if (!pressed) {
    if (player->continuous_firing) {
      player->continuous_firing = false;
      player->weapon_animation_at = -1;
      player->weapon_frame = 0;
    }
    return false;
  }
  return qa_q1_attack(game, actor, view_angles, seconds, water_level);
}

/* --- construction / precache / spawn --- */
const char *qa_q1_services_precache_model(qa_q1_services *game, const char *path) {
  return qa_q1_precache_model(game->precaches, path);
}
const char *qa_q1_services_precache_sound(qa_q1_services *game, const char *path) {
  return qa_q1_precache_sound(game->precaches, path);
}
static void sub_remove(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_remove(game, entity);
}
/* TS constructor's "base:explosion_frame" action (BecomeExplosion's s_explode frames). */
static void explosion_frame(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->frame++;
  if (entity->frame >= 6) qa_q1_remove(game, entity);
  else qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, "base:explosion_frame"));
}
static void sub_remove_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                             const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)other; (void)normal; (void)surface;
  qa_q1_remove(game, entity);
}
static void sub_null(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  (void)game;
  (void)entity;
}
static void delay_think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_use_targets(game, entity,
                    entity->has_activator ? &entity->activator : NULL);
  qa_q1_remove(game, entity);
}
bool qa_q1_services_init(qa_q1_services *game, qa_q1_host host,
                         qa_q1_options options) {
  memset(game, 0, sizeof *game);
  game->host = host;
  game->options = options;
  game->named = qa_q1_callback_registry_create(game);
  game->precaches = qa_q1_precache_registry_create();
  if (game->named == NULL || game->precaches == NULL) {
    qa_q1_callback_registry_destroy(game->named);
    qa_q1_precache_registry_destroy(game->precaches);
    game->named = NULL;
    game->precaches = NULL;
    return false;
  }
  game->next_dynamic_slot = (options.has_max_clients ? options.max_clients : 0) + 1;
  game->sight_time = -1;
  game->base_team_health = true; /* TS field initializer */
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.action = sub_remove;
  handlers.touch = sub_remove_touch;
  qa_q1_callback_registry_register(game->named, "SUB_Remove", handlers);
  memset(&handlers, 0, sizeof handlers);
  handlers.action = explosion_frame;
  qa_q1_callback_registry_register(game->named, "base:explosion_frame", handlers);
  memset(&handlers, 0, sizeof handlers);
  handlers.action = sub_null;
  qa_q1_callback_registry_register(game->named, "SUB_Null", handlers);
  memset(&handlers, 0, sizeof handlers);
  handlers.action = delay_think;
  qa_q1_callback_registry_register(game->named, "DelayThink", handlers);
  qa_q1_register_mover_callbacks(game);
  qa_q1_register_spawn_callbacks(game);
  qa_q1_register_pickup_callbacks(game);
  qa_q1_register_weapon_callbacks(game);
  game->release_subscription = qa_session_actor_registry_on_release(game->host.actors, on_release, game);
  return true;
}
void qa_q1_legacy_animations_set(qa_q1_services *game, qa_owned_actor owner, qa_q1_legacy_animation animation) {
  for (size_t i = 0; i < game->legacy_animation_count; i++) {
    if (qa_same_owned_actor(game->legacy_animations[i].owner, owner)) {
      game->legacy_animations[i].animation = animation;
      return;
    }
  }
  if (game->legacy_animation_count == game->legacy_animation_capacity) {
    const size_t capacity = game->legacy_animation_capacity ? game->legacy_animation_capacity * 2 : 8;
    void *grown = realloc(game->legacy_animations, capacity * sizeof *game->legacy_animations);
    if (grown == NULL) qa_range_error("out of memory");
    game->legacy_animations = grown;
    game->legacy_animation_capacity = capacity;
  }
  game->legacy_animations[game->legacy_animation_count].owner = owner;
  game->legacy_animations[game->legacy_animation_count].animation = animation;
  game->legacy_animation_count++;
}
void qa_q1_services_free(qa_q1_services *game) {
  if (game == NULL) return;
  if (game->named != NULL)
    qa_session_actor_registry_off_release(game->host.actors, game->release_subscription);
  /* Entities and players are owned here (TS GC). */
  for (size_t i = 0; i < game->entity_count; i++) { qa_q1_actor_free(game->entities[i]); free(game->entities[i]); }
  for (size_t i = 0; i < game->released_entity_count; i++) {
    qa_q1_actor_free(game->released_entities[i]);
    free(game->released_entities[i]);
  }
  free(game->released_entities);
  for (size_t i = 0; i < game->player_count; i++) free(game->players[i]);
  for (size_t i = 0; i < game->released_player_count; i++) free(game->released_players[i]);
  free(game->released_players);
  free(game->protect_sounds);
  free(game->quad_sounds);
  free(game->wind_fly_sounds);
  free(game->waterborne);
  free(game->powerup_cues);
  free(game->authored_targets);
  free(game->world_precaches);
  qa_q1_callback_registry_destroy(game->named);
  qa_q1_precache_registry_destroy(game->precaches);
  free(game->entities);
  free(game->players);
  free(game->map_name);
  free(game->intermission_map);
  free(game->registered_weapons);
  free(game->player_extensions);
  free(game->weapon_rules);
  free(game->state_extensions);
  free(game->weapon_order_owner);
  for (size_t i = 0; i < game->source_damage_effect_count; i++) free(game->source_damage_effects[i].id);
  free(game->source_damage_effects);
  for (size_t i = 0; i < game->actor_binding_count; i++) free(game->actor_bindings[i]);
  free(game->actor_bindings);
  for (size_t i = 0; i < game->path_touch_count; i++) free(game->path_touches[i].id);
  free(game->path_touches);
  for (size_t i = 0; i < game->spawner_count; i++) free(game->spawners[i].classname);
  free(game->spawners);
  qa_q1_source_world_words_clear(game);
  free(game->source_world_words);
  free(game->restore_absents);
  free(game->missions);
  free(game->legacy_animations);
  emit_intern_free(game->emit_intern);
  memset(game, 0, sizeof *game);
}
/* `q1:${classname}`: the registry borrows definition strings for the
 * actor's lifetime and may outlive these services, so they are interned
 * process-wide (reachable, one per classname). */
static const char *actor_definition(const char *classname) {
  static char **interned = NULL;
  static size_t count = 0, capacity = 0;
  for (size_t i = 0; i < count; i++)
    if (!strcmp(interned[i] + 3, classname)) return interned[i];
  grow((void **)&interned, &count, &capacity, sizeof *interned);
  const size_t length = strlen(classname) + 4;
  char *definition = xmalloc(length);
  memcpy(definition, "q1:", 3);
  memcpy(definition + 3, classname, length - 3);
  interned[count++] = definition;
  return definition;
}
qa_q1_actor *qa_q1_create(qa_q1_services *game, const char *classname,
                          const qa_q1_entity *source,
                          const double *source_ordinal) {
  double max_clients = game->options.has_max_clients ? game->options.max_clients : 0;
  double slot = source_ordinal == NULL
                    ? game->next_dynamic_slot++
                    : *source_ordinal == 0 ? 0 : *source_ordinal + max_clients;
  qa_session_actor_registry * registry = game->host.actors;
  qa_provider_id owner2 = qa_q1_services_provider(game);
  const char * definition = actor_definition(classname);
  const qa_owned_actor owner = qa_session_actor_registry_allocate_at_source(
      registry, owner2, (int64_t)slot, definition);
  qa_q1_actor *entity = xmalloc(sizeof *entity);
  if (!qa_q1_actor_init(entity, owner, classname, source_ordinal, game->host.combat, source))
    qa_range_error("Out of memory");
  qa_body_state body;
  memset(&body, 0, sizeof body);
  body.origin = source == NULL ? QA_Q1_ZERO : qa_q1_actor_vector(entity, "origin");
  body.angles = source == NULL ? QA_Q1_ZERO : qa_q1_source_angles(source);
  body.velocity = QA_Q1_ZERO;
  body.bounds = QA_Q1_POINT;
  body.ground = NULL;
  qa_shared_body_table_create_body(game->host.bodies, owner, body);
  qa_combat_state combat;
  memset(&combat, 0, sizeof combat);
  combat.health = entity->max_health;
  combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  combat.armor.powered.kind = "none";
  combat.mass = 100;
  combat.can_take_damage = false;
  combat.invulnerable = false;
  combat.team = NULL;
  qa_gameplay_authority_create_state(game->host.combat, owner, combat, NULL, NULL);
  return admit_entity(game, entity);
}
void qa_q1_spawn_entity(qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1_spawn_fn extension = NULL;
  void *extension_ctx = NULL;
  for (size_t i = 0; i < game->spawner_count; i++)
    if (same(game->spawners[i].classname, entity->classname)) {
      extension = game->spawners[i].fn;
      extension_ctx = game->spawners[i].ctx;
    }
  if (extension == NULL)
    qa_q1_spawn_map_actor(game, entity);
  else
    extension(extension_ctx, game, entity);
  if (qa_q1_live(game, entity)) qa_q1_link(game, entity);
}
typedef struct deathmatch_restore { qa_q1_services *game; double previous; } deathmatch_restore;
static void restore_deathmatch(void *arg) {
  const deathmatch_restore *restore = arg;
  restore->game->options.deathmatch = restore->previous;
}
void qa_q1_spawn_entity_context(qa_q1_services *game, qa_q1_actor *entity, const double *deathmatch) {
  /* TS spawnOptions = { ...this.options, deathmatch } for the spawn, restored
   * in finally: the cleanup node restores it when a spawn raises. */
  deathmatch_restore restore = {game, game->options.deathmatch};
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, restore_deathmatch, &restore);
  if (deathmatch != NULL) game->options.deathmatch = *deathmatch;
  qa_q1_spawn_entity(game, entity);
  qa_cleanup_pop(&cleanup);
  game->options.deathmatch = restore.previous;
}
static qa_q1_bound_action named_action(qa_q1_services *game,
                                       qa_q1_actor *entity, const char *name) {
  qa_q1_bound_action action;
  memset(&action, 0, sizeof action);
  if (game->named == NULL) return action;
  return qa_q1_callback_registry_action(game->named, entity, name);
}
void qa_q1_calc_move(qa_q1_services *game, qa_q1_actor *entity,
                     qa_vec3 destination, double speed,
                     qa_q1_bound_action done) {
  if (!(speed > 0)) qa_range_error("Q1 mover speed must be positive");
  qa_q1_cancel(game, entity);
  qa_vec3 delta = qa_q1_vsub(destination, qa_q1_get_body(game, entity).origin);
  double travel = (float)(qa_q1_length(delta) / speed);
  entity->has_move = true;
  entity->move.destination = destination;
  entity->move.done = done;
  qa_q1_body patch;
  memset(&patch, 0, sizeof patch);
  patch.velocity =
      travel < 0.1 ? QA_Q1_ZERO : qa_q1_vscale(delta, (float)(1 / travel));
  qa_q1_set_body_patch(game, entity, &patch, false, false, true, false, false);
  const double wait = qa_js_max(0.1, travel); /* Math.max keeps NaN */
  double due_seconds = (float)(qa_q1_actor_number(entity, "ltime") + wait);
  qa_q1_bound_action callback = named_action(game, entity, "SUB_CalcMoveDone");
  qa_q1_schedule_at(game, entity,
                    due_seconds, callback);
}
typedef struct target_row { qa_owned_actor actor; double slot; } target_row;
static double source_slot(qa_q1_services *game, qa_actor_id actor) {
  const qa_actor_source_result source = qa_session_actor_registry_source_of(game->host.actors, actor);
  return source.has_value ? (double)source.slot : (double)actor.slot;
}
/* TS targetActors: Q1 entities then authored targets with this targetname,
 * live, stably sorted by source slot. *out is malloc'd; caller frees. */
static size_t target_actors(qa_q1_services *game, const char *name, target_row **out) {
  *out = NULL;
  if (name == NULL || name[0] == '\0') return 0;
  size_t count = 0, capacity = 0;
  for (size_t i = 0; i < game->entity_count; i++) {
    const qa_q1_actor *entity = game->entities[i];
    if (!same(entity->targetname, name) ||
        !qa_session_actor_registry_is_live(game->host.actors, entity->actor.id))
      continue;
    grow((void **)out, &count, &capacity, sizeof **out);
    (*out)[count].actor = entity->actor;
    (*out)[count].slot = source_slot(game, entity->actor.id);
    count++;
  }
  for (size_t i = 0; i < game->authored_target_count; i++) {
    const qa_authored_target *target = game->authored_targets[i].target;
    if (!same(target->targetname, name) ||
        !qa_session_actor_registry_is_live(game->host.actors, target->actor.id))
      continue;
    grow((void **)out, &count, &capacity, sizeof **out);
    (*out)[count].actor = target->actor;
    (*out)[count].slot = source_slot(game, target->actor.id);
    count++;
  }
  for (size_t i = 1; i < count; i++) { /* stable insertion sort (JS sort is stable) */
    const target_row held = (*out)[i];
    size_t k = i;
    while (k > 0 && (*out)[k - 1].slot > held.slot) {
      (*out)[k] = (*out)[k - 1];
      k--;
    }
    (*out)[k] = held;
  }
  return count;
}
static void set_owned_text(char **field, const char *value) {
  free(*field);
  *field = dup_string(value != NULL ? value : "");
}
void qa_q1_use_targets(qa_q1_services *game, qa_q1_actor *entity,
                       const qa_actor_id *activator) {
  if (entity->delay != 0) {
    qa_q1_actor *delayed = qa_q1_create(game, "DelayedUse", NULL, NULL);
    set_owned_text(&delayed->target, entity->target);
    set_owned_text(&delayed->killtarget, entity->killtarget);
    set_owned_text(&delayed->message, entity->message);
    delayed->has_activator = activator != NULL;
    if (activator != NULL) delayed->activator = *activator;
    delayed->spawnflags = (double)(qa_js_to_int32(entity->spawnflags) & 8388608);
    qa_q1_schedule(game, delayed, entity->delay,
                   named_action(game, delayed, "DelayThink"));
    return;
  }
  const char *message = entity->message != NULL ? entity->message : "";
  const bool everyone = game->source_message_all_players &&
                        (qa_js_to_int32(entity->spawnflags) & 8388608) != 0 && message[0] != '\0';
  if (everyone) {
    size_t count = 0;
    const qa_actor_id *players = game->host.players(game->host.ctx, &count);
    for (size_t i = 0; i < count; i++) qa_q1_message(game, &players[i], message, true, NULL, 0);
  } else {
    qa_q1_message(game, activator, message, true, NULL, 0);
  }
  if (message[0] != '\0' && activator != NULL && qa_q1_is_player(game, activator) &&
      qa_q1_actor_text(entity, "noise")[0] == '\0') {
    qa_owned_actor owner;
    if (qa_session_actor_registry_resolve_owned(game->host.actors, *activator, &owner))
      qa_q1_sound(game, owner.id, "misc/talk.wav", "voice", 1, 1);
  }
  target_row *victims = NULL;
  size_t count = target_actors(game, entity->killtarget, &victims);
  for (size_t i = 0; i < count; i++)
    if (qa_session_actor_registry_is_live(game->host.actors, victims[i].actor.id))
      qa_session_actor_registry_release(game->host.actors, victims[i].actor);
  free(victims);
  /* Each use executes synchronously; targets removed by a nested call
   * are not invoked afterward. */
  target_row *targets = NULL;
  count = target_actors(game, entity->target, &targets);
  const qa_actor_id other = entity->actor.id;
  qa_cleanup targets_cleanup; /* a use callback can raise */
  qa_cleanup_push(&targets_cleanup, free, targets);
  for (size_t i = 0; i < count; i++)
    if (qa_session_actor_registry_is_live(game->host.actors, targets[i].actor.id))
      (void)qa_actor_callback_table_use(game->host.callbacks, targets[i].actor, &other, activator);
  qa_cleanup_pop(&targets_cleanup);
  free(targets);
}
/* TS useTargets(entity: AuthoredTarget, activator) for an authored target
 * that is not a Q1Actor (selected monster entries): the instanceof Q1Actor
 * branches see spawnflags 0 and no "noise" field. */
void qa_q1_use_authored_targets(qa_q1_services *game, const qa_authored_target *entity,
                                const qa_actor_id *activator) {
  const char *target = entity->target != NULL ? entity->target : "";
  const char *killtarget = entity->killtarget != NULL ? entity->killtarget : "";
  const char *message = entity->message != NULL ? entity->message : "";
  if (entity->delay != 0) {
    qa_q1_actor *delayed = qa_q1_create(game, "DelayedUse", NULL, NULL);
    set_owned_text(&delayed->target, target);
    set_owned_text(&delayed->killtarget, killtarget);
    set_owned_text(&delayed->message, message);
    delayed->has_activator = activator != NULL;
    if (activator != NULL) delayed->activator = *activator;
    delayed->spawnflags = 0;
    qa_q1_schedule(game, delayed, entity->delay, named_action(game, delayed, "DelayThink"));
    return;
  }
  qa_q1_message(game, activator, message, true, NULL, 0);
  if (message[0] != '\0' && activator != NULL && qa_q1_is_player(game, activator)) {
    qa_owned_actor owner;
    if (qa_session_actor_registry_resolve_owned(game->host.actors, *activator, &owner))
      qa_q1_sound(game, owner.id, "misc/talk.wav", "voice", 1, 1);
  }
  target_row *victims = NULL;
  size_t count = target_actors(game, killtarget, &victims);
  for (size_t i = 0; i < count; i++)
    if (qa_session_actor_registry_is_live(game->host.actors, victims[i].actor.id))
      qa_session_actor_registry_release(game->host.actors, victims[i].actor);
  free(victims);
  target_row *targets = NULL;
  count = target_actors(game, target, &targets);
  const qa_actor_id other = entity->actor.id;
  qa_cleanup targets_cleanup; /* a use callback can raise */
  qa_cleanup_push(&targets_cleanup, free, targets);
  for (size_t i = 0; i < count; i++)
    if (qa_session_actor_registry_is_live(game->host.actors, targets[i].actor.id))
      (void)qa_actor_callback_table_use(game->host.callbacks, targets[i].actor, &other, activator);
  qa_cleanup_pop(&targets_cleanup);
  free(targets);
}
void qa_q1_drop_shells(qa_q1_services *game, qa_vec3 origin) {
  qa_q1_actor *backpack = qa_q1_create(game, "item_backpack", NULL, NULL);
  qa_vec3 at = origin;
  at.z -= 24;
  qa_q1_set_origin(game, backpack, at);
  if (!qa_q1_actor_field_set(backpack, "shells", "5"))
    qa_range_error("Out of memory");
  (void)qa_q1_spawn_pickup(game, backpack);
  qa_q1_schedule(game, backpack, 120, named_action(game, backpack, "SUB_Remove"));
}
void qa_q1_check_water_transition(qa_q1_services *game, qa_q1_actor *entity) {
  const char *contents =
      game->host.contents(game->host.ctx, qa_q1_get_body(game, entity).origin);
  double value = same(contents, "solid")    ? -2
                 : same(contents, "water")  ? -3
                 : same(contents, "slime")  ? -4
                 : same(contents, "lava")   ? -5
                 : same(contents, "sky")    ? -6
                                            : -1;
  qa_q1_water_transition_result transition =
      qa_q1_water_transition(entity->water_type, value);
  if (transition.splash)
    qa_q1_sound(game, entity->actor.id, "misc/h2ohit1.wav", "auto", 1, 1);
  entity->water_type = transition.water_type;
  entity->water_level = transition.water_level;
}

/* --- damage --- */
static bool actor_is_live(void *ctx, qa_actor_id actor) {
  const qa_q1_services *game = ctx;
  return qa_session_actor_registry_is_live(game->host.actors, actor);
}
double qa_q1_option_skill(const qa_q1_services *game) { return qa_q1_options_skill(&game->options); }
double qa_q1_option_teamplay(const qa_q1_services *game) { return qa_q1_options_teamplay(&game->options); }
double qa_q1_option_gravity(const qa_q1_services *game) { return qa_q1_options_gravity(&game->options); }
double qa_q1_player_timer_get(const qa_q1_player_timer *table, size_t count, const qa_q1_player_state *player) {
  for (size_t i = 0; i < count; i++)
    if (table[i].player == player) return table[i].at;
  return 0;
}
void qa_q1_player_timer_set(qa_q1_player_timer **table, size_t *count, size_t *capacity,
                            const qa_q1_player_state *player, double at) {
  for (size_t i = 0; i < *count; i++)
    if ((*table)[i].player == player) { (*table)[i].at = at; return; }
  grow((void **)table, count, capacity, sizeof **table);
  (*table)[*count].player = player;
  (*table)[*count].at = at;
  (*count)++;
}
static double protect_sound_at(const qa_q1_services *game, const qa_q1_player_state *player) {
  for (size_t i = 0; i < game->protect_sound_count; i++)
    if (game->protect_sounds[i].player == player) return game->protect_sounds[i].at;
  return 0; /* TS ?? 0 */
}
static void set_protect_sound(qa_q1_services *game, const qa_q1_player_state *player, double at) {
  for (size_t i = 0; i < game->protect_sound_count; i++)
    if (game->protect_sounds[i].player == player) { game->protect_sounds[i].at = at; return; }
  grow((void **)&game->protect_sounds, &game->protect_sound_count, &game->protect_sound_capacity,
       sizeof *game->protect_sounds);
  game->protect_sounds[game->protect_sound_count].player = player;
  game->protect_sounds[game->protect_sound_count].at = at;
  game->protect_sound_count++;
}
qa_damage_outcome qa_q1_damage(qa_q1_services *game, qa_actor_id target, qa_actor_id inflictor,
                               const qa_actor_id *attacker, double amount, qa_q1_weapon weapon,
                               const char *delivery, const char *death_type, const char *armor_effect) {
  const qa_body_state_result target_body = qa_shared_body_table_read(game->host.bodies, target);
  const qa_body_state_result source = qa_shared_body_table_read(game->host.bodies, inflictor);
  qa_q1_player_state *protected_player = qa_q1_player(game, target);
  if (protected_player != NULL &&
      qa_q1_player_powerup_get(protected_player, "invulnerability") >= game->time &&
      protect_sound_at(game, protected_player) < game->time) {
    set_protect_sound(game, protected_player, game->time + 2);
    qa_q1_sound(game, protected_player->actor.id, "items/protect3.wav", "item", 1, 1);
  }
  const qa_vec3 point = target_body.has_value ? target_body.value.origin : QA_Q1_ZERO;
  const qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(point, source.has_value ? source.value.origin : point));
  double multiplier = 1;
  if (attacker != NULL && game->host.source_damage_multiplier != NULL)
    multiplier = game->host.source_damage_multiplier(game->host.ctx, *attacker);
  const double scaled = (float)((double)(float)amount * (double)(float)multiplier);
  const qa_actor_id inflictor_id = inflictor;
  qa_damage_request request;
  memset(&request, 0, sizeof request);
  request.target = target;
  request.amount = scaled;
  request.knockback = scaled;
  request.direction = direction;
  request.point = point;
  request.normal = QA_Q1_ZERO;
  request.delivery = delivery != NULL ? delivery : "direct";
  request.attack.sequence = game->sequence++;
  request.attack.time.kind = QA_SOURCE_TIME_SECONDS;
  request.attack.time.value = game->time;
  request.attack.attacker = attacker;
  request.attack.inflictor = &inflictor_id;
  request.attack.has_damage_powerup_owner = game->host.source_damage_powerup_owner != NULL;
  request.attack.damage_powerup_owner = game->host.source_damage_powerup_owner;
  request.attack.weapon = weapon == NULL ? NULL : qa_q1_services_weapon_item(game, weapon);
  request.attack.weapon_provider = qa_q1_services_provider(game);
  request.attack.combat_provider = game->options.combat_provider;
  request.attack.inventory_provider = game->options.inventory_provider;
  request.attack.movement_provider = game->options.movement_provider;
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q1;
  request.attack.cause.death_type = death_type != NULL ? death_type : "";
  request.attack.cause.armor_effect = armor_effect;
  qa_damage_request modified =
      qa_apply_source_damage_modifier(request, game->host.source_damage_modifier, actor_is_live, game);
  modified.knockback = modified.amount;
  return qa_gameplay_authority_apply(game->host.combat, modified, NULL, NULL);
}
qa_q1_combat_context qa_q1_combat_context_for(qa_q1_services *game, qa_damage_request request) {
  qa_body_state_result projectile;
  memset(&projectile, 0, sizeof projectile);
  if (request.attack.has_originating_projectile)
    projectile = qa_shared_body_table_read(game->host.bodies, request.attack.originating_projectile);
  qa_linked_body_result inflictor;
  memset(&inflictor, 0, sizeof inflictor);
  if (request.attack.inflictor != NULL &&
      !(game->world != NULL && qa_same_actor(game->world->actor.id, *request.attack.inflictor)))
    inflictor = qa_shared_body_table_linked(game->host.bodies, *request.attack.inflictor);
  /* A source may report world as the damage inflictor while retaining the actual projectile. */
  bool has_origin = true;
  qa_vec3 origin = QA_Q1_ZERO;
  if (projectile.has_value)
    origin = qa_q1_vadd(projectile.value.origin,
                        qa_q1_vscale(qa_q1_vadd(projectile.value.bounds.min, projectile.value.bounds.max), 0.5));
  else if (inflictor.has_value)
    origin = qa_q1_vscale(qa_q1_vadd(inflictor.value.absolute_bounds.min, inflictor.value.absolute_bounds.max), 0.5);
  else
    has_origin = false;
  const qa_body_state_result target = qa_shared_body_table_read(game->host.bodies, request.target);
  qa_q1_combat_context context;
  memset(&context, 0, sizeof context);
  context.arithmetic = "binary32";
  context.quad = !request.attack.has_damage_powerup_owner && request.attack.attacker != NULL &&
                 qa_q1_powerup_expires(game, *request.attack.attacker, "quad") > game->time;
  context.teamplay = qa_q1_option_teamplay(game);
  context.has_base_team_health = true;
  context.base_team_health = game->base_team_health;
  context.walk = qa_q1_is_player(game, &request.target);
  context.has_momentum_direction = target.has_value && has_origin;
  if (context.has_momentum_direction)
    context.momentum_direction = qa_q1_normalize(qa_q1_vsub(target.value.origin, origin));
  return context;
}
void qa_q1_radius_damage(qa_q1_services *game, qa_actor_id inflictor,
                         const qa_actor_id *attacker, double amount,
                         const qa_actor_id *ignore, qa_q1_weapon weapon,
                         const char *death_type) {
  const qa_body_state_result source_read = qa_shared_body_table_read(game->host.bodies, inflictor);
  if (!source_read.has_value) return;
  const qa_vec3 source_origin = source_read.value.origin;
  /* findradius prepends to its chain, so the highest edict is damaged
   * first, and it skips SOLID_NOT (a downed zombie). TS spreads the
   * observations before reversing them. */
  size_t count = 0;
  const qa_actor_observation *live = qa_session_actor_registry_observations(game->host.actors, &count);
  qa_actor_id *targets = xmalloc((count != 0 ? count : 1) * sizeof *targets);
  for (size_t i = 0; i < count; i++) targets[i] = live[count - 1 - i].id;
  qa_cleanup targets_cleanup; /* damage reactions can raise */
  qa_cleanup_push(&targets_cleanup, free, targets);
  for (size_t i = 0; i < count; i++) {
    const qa_actor_id target = targets[i];
    const qa_combat_state_result combat = qa_gameplay_authority_read(game->host.combat, target);
    const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, target);
    if (!combat.has_value || !combat.value.can_take_damage || !body.has_value) continue;
    if (ignore != NULL && qa_same_actor(*ignore, target)) continue;
    const qa_q1_actor *entity = qa_q1_get_entity(game, &target);
    if (entity != NULL && same(entity->solid, "none")) continue;
    const qa_vec3 center = qa_q1_vadd(
        body.value.origin, qa_q1_vscale(qa_q1_vadd(body.value.bounds.min, body.value.bounds.max), 0.5));
    const double distance = qa_q1_length(qa_q1_vsub(center, source_origin));
    if (distance > amount + 40) continue;
    double points = (float)(amount - 0.5 * distance);
    if (attacker != NULL && qa_same_actor(target, *attacker)) points *= 0.5;
    if (same(game->host.classname(game->host.ctx, target), "monster_shambler")) points *= 0.5;
    if (points > 0 && qa_q1_can_damage(game, target, inflictor))
      (void)qa_q1_damage(game, target, inflictor, attacker, points, weapon, "radius", death_type, NULL);
  }
  qa_cleanup_pop(&targets_cleanup);
  free(targets);
}

/* --- clone --- */
static void apply_source_state(qa_q1_actor *target,
                               qa_q1_entity_source_state state) {
  set_owned_text(&target->model, state.model);
  target->frame = state.frame;
  target->skin = state.skin;
  target->effects = state.effects;
  target->solid = state.solid;
  target->movement = state.movement;
  set_owned_text(&target->target, state.target);
  set_owned_text(&target->targetname, state.targetname);
  set_owned_text(&target->killtarget, state.killtarget);
  set_owned_text(&target->message, state.message);
  target->delay = state.delay;
  target->spawnflags = state.spawnflags;
  target->sounds = state.sounds;
  target->wait = state.wait;
  target->speed = state.speed;
  target->damage = state.damage;
  target->max_health = state.max_health;
  target->aimed_damage = state.aimed_damage;
  target->next_think = state.next_think;
  set_owned_text(&target->original_model, state.original_model);
  target->pos1 = state.pos1;
  target->pos2 = state.pos2;
  target->dest1 = state.dest1;
  target->dest2 = state.dest2;
  target->movedir = state.movedir;
  target->mangle = state.mangle;
  target->state = state.state;
  target->has_trigger_bounds = state.has_trigger_bounds;
  target->trigger_bounds = state.trigger_bounds;
  target->attack_finished = state.attack_finished;
  target->count = state.count;
  target->activated = state.activated;
  target->projectile = state.projectile;
  target->projectile_weapon = state.projectile_weapon;
  target->angular_velocity = state.angular_velocity;
  target->water_level = state.water_level;
  target->water_type = state.water_type;
  target->movement_flags = state.movement_flags;
  target->ideal_yaw = state.ideal_yaw;
  target->yaw_speed = state.yaw_speed;
  target->attack_state = state.attack_state;
}
qa_q1_actor *qa_q1_clone_entity(qa_q1_services *game, qa_q1_actor *source) {
  qa_session_actor_registry_assert_owned(game->host.actors, source->actor);
  const char *think_name = qa_q1_action_name(source->think);
  const char *use_name = qa_q1_use_name(source->use);
  const char *touch_name = qa_q1_touch_name(source->touch);
  const char *pain_name = qa_q1_pain_name(source->pain);
  const char *die_name = qa_q1_die_name(source->die);
  const char *blocked_name = qa_q1_blocked_name(source->blocked);
  const char *path_end_name = qa_q1_action_name(source->path_end);
  const char *done_name =
      source->has_move ? qa_q1_action_name(source->move.done) : NULL;
  qa_q1_actor *target = qa_q1_create(game, source->classname, NULL, NULL);
  apply_source_state(target, qa_q1_capture_entity_source_state(source));
  for (size_t i = 0; i < source->field_count; i++)
    if (!qa_q1_actor_field_set(target, source->fields[i].key,
                               source->fields[i].value))
      qa_range_error("Out of memory");
  for (size_t i = 0; i < source->reference_count; i++)
    if (!qa_q1_actor_reference_set(
            target, source->references[i].key,
            source->references[i].has_actor ? &source->references[i].actor
                                            : NULL))
      qa_range_error("Out of memory");
  target->has_owner = source->has_owner;
  target->owner = source->owner;
  target->has_activator = source->has_activator;
  target->activator = source->activator;
  free(target->door_group);
  target->door_group = NULL;
  target->door_group_count = source->door_group_count;
  if (source->door_group_count > 0) {
    target->door_group =
        xmalloc(source->door_group_count * sizeof *target->door_group);
    memcpy(target->door_group, source->door_group,
           source->door_group_count * sizeof *target->door_group);
  }
  if (source->monster != NULL) {
    qa_q1_monster *monster = xmalloc(sizeof *monster);
    *monster = *source->monster;
    monster->path =
        source->monster->path != NULL ? dup_string(source->monster->path) : NULL;
    target->monster = monster;
  }
  const qa_q1_body body = qa_q1_get_body(game, source);
  qa_shared_body_table_write(game->host.bodies, target->actor, qa_q1_body_to_state(&body));
  const qa_combat_state_result combat = qa_gameplay_authority_read(game->host.combat, source->actor.id);
  if (!combat.has_value) qa_range_error("Source clone has no combat state"); /* TS Error throw */
  qa_gameplay_authority_set_health(game->host.combat, target->actor, combat.value.health);
  qa_gameplay_authority_set_armor(game->host.combat, target->actor, combat.value.armor);
  qa_combat_trait_changes traits;
  memset(&traits, 0, sizeof traits);
  traits.has_can_take_damage = true; traits.can_take_damage = combat.value.can_take_damage;
  traits.has_mass = true; traits.mass = combat.value.mass;
  traits.has_invulnerable = true; traits.invulnerable = combat.value.invulnerable;
  traits.has_team = true; traits.team = combat.value.team;
  traits.has_no_knockback = combat.value.has_no_knockback; traits.no_knockback = combat.value.no_knockback;
  qa_gameplay_authority_set_traits(game->host.combat, target->actor, traits);
  if (qa_shared_inventory_table_has(game->host.inventory, source->actor.id)) {
    size_t entry_count = 0;
    const qa_inventory_entry *entries =
        qa_shared_inventory_table_entries(game->host.inventory, source->actor.id, &entry_count);
    qa_shared_inventory_table_create_entries(game->host.inventory, target->actor, entries, entry_count);
  }
  if (game->named != NULL) {
    if (think_name != NULL)
      target->think = qa_q1_callback_registry_action(game->named, target, think_name);
    if (use_name != NULL)
      target->use = qa_q1_callback_registry_use(game->named, target, use_name);
    if (touch_name != NULL)
      target->touch = qa_q1_callback_registry_touch(game->named, target, touch_name);
    if (pain_name != NULL)
      target->pain = qa_q1_callback_registry_pain(game->named, target, pain_name);
    if (die_name != NULL)
      target->die = qa_q1_callback_registry_die(game->named, target, die_name);
    if (blocked_name != NULL)
      target->blocked =
          qa_q1_callback_registry_blocked(game->named, target, blocked_name);
    if (path_end_name != NULL)
      target->path_end =
          qa_q1_callback_registry_action(game->named, target, path_end_name);
    if (source->has_move && done_name != NULL) {
      target->has_move = true;
      target->move.destination = source->move.destination;
      target->move.done =
          qa_q1_callback_registry_action(game->named, target, done_name);
    }
  }
  for (size_t i = 0; i < game->state_extension_count; i++)
    if (game->state_extensions[i]->clone != NULL)
      game->state_extensions[i]->clone(game->state_extensions[i]->ctx, source,
                                        target);
  if (!same(target->movement, "push") && target->think.fn != NULL &&
      target->next_think >= 0)
    game->host.schedule_think(game->host.ctx, target->actor, target->next_think);
  return target;
}
