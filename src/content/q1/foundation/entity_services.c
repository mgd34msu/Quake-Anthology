/*
 * Port of src/content/q1/foundation/entity-services.ts: the
 * getters (entity()/player()/isPlayer()/body()/sourceTarget(), the
 * renamed qa_q1_get_*; a qa_q1_entity() function would collide
 * with the qa_q1_entity typedef), dropQ1Punch/ammoItem, the
 * registration family, attachExisting/attachPlayer, weapon
 * selection, inventory, scheduling, and the emit/host forwards,
 * plus attack/weaponInput/playerFrame/physicsStep/physicsEntity
 * (with applyProjectileBehavior/projectilePhysics) on the landed
 * weapons.ts fire paths and the host weapon-behavior/pusher
 * ports. qa_q1_services_init registers the landed module
 * callbacks including registerWeaponCallbacks. GPL-2.0-or-later.
 *
 * TS resolves actors through host.actors.resolveOwned and the
 * entities/players OwnedActor maps; the C qa_q1_host mirror keeps
 * bodies/live state actor-local (see types.h), so resolution is a
 * scan of the services' entity/player arrays gated on the actor's
 * live flag (resolveOwned returns null for a released actor).
 * qa_q1_remove splices the arrays (the TS onRelease map deletes)
 * but does not free the actor/player structs: TS has GC, and the
 * C arrays may hold caller-owned storage.
 */
#include "qa/content/q1/foundation/entity_services.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/checkpoint.h"
#include "qa/content/q1/foundation/monsters.h"
#include "qa/content/q1/foundation/movers.h"
#include "qa/content/q1/foundation/pickups.h"
#include "qa/content/q1/foundation/spawns.h"
#include "qa/content/q1/foundation/weapons.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/movement/q1/water_transition.h"

qa_q1_actor *qa_q1_get_entity(qa_q1_services *game, const qa_actor_id *actor) {
  if (actor == NULL) return NULL;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (entity != NULL && entity->live && qa_same_actor(entity->actor.id, *actor))
      return entity;
  }
  return NULL;
}
qa_q1_player_state *qa_q1_player(qa_q1_services *game, qa_actor_id actor) {
  for (size_t i = 0; i < game->player_count; i++) {
    qa_q1_player_state *player = game->players[i];
    if (player != NULL && qa_same_actor(player->actor.id, actor)) return player;
  }
  return NULL;
}
bool qa_q1_is_player(qa_q1_services *game, const qa_actor_id *actor) {
  if (actor == NULL) return false;
  if (qa_q1_player(game, *actor) != NULL) return true;
  if (game->host.players == NULL) return false;
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  for (size_t i = 0; i < count; i++)
    if (qa_same_actor(players[i], *actor)) return true;
  return false;
}
qa_q1_body qa_q1_get_body(qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  /* TS: host.bodies.read(entity.actor.id); null throws. The
   * actor-local mirror has no body once the actor is released. */
  if (entity == NULL || !entity->live) qa_range_error("Missing Q1 body");
  return entity->body;
}
qa_q1_source_target qa_q1_get_source_target(qa_q1_services *game, qa_actor_id actor) {
  if (game->host.source_target != NULL) {
    qa_q1_source_target observed;
    if (game->host.source_target(game->host.ctx, actor, &observed)) return observed;
  }
  qa_q1_actor *entity = qa_q1_get_entity(game, &actor);
  qa_q1_source_target result = {
      entity != NULL && entity->aimed_damage,
      entity != NULL && entity->movement != NULL && strcmp(entity->movement, "push") == 0,
      qa_q1_is_player(game, &actor),
      entity != NULL && entity->solid != NULL && strcmp(entity->solid, "slidebox") == 0};
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
static qa_q1_actor *entity_any(qa_q1_services *game, qa_actor_id actor) {
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (entity != NULL && qa_same_actor(entity->actor.id, actor)) return entity;
  }
  return NULL;
}
static qa_q1_player_state *player_owned(qa_q1_services *game,
                                        qa_owned_actor actor) {
  return qa_q1_player(game, actor.id);
}

/* --- dropQ1Punch / ammoItem --- */
qa_vec3 qa_q1_drop_punch(qa_vec3 angles, double elapsed,
                         const qa_numeric_operations *numeric) {
  qa_mutable_vector_math math;
  qa_create_mutable_vector_math(&math, numeric, QA_ZERO_LENGTH_PRESERVE);
  qa_vec3 direction = angles;
  double magnitude =
      qa_mvm_vector_normalize(&math, qa_vec3_access_plain(&direction));
  double remaining = numeric->subtract(
      numeric->ctx, magnitude, numeric->multiply(numeric->ctx, 10, elapsed));
  if (remaining < 0) remaining = 0;
  qa_vec3 punch = QA_Q1_ZERO;
  qa_mvm_vector_scale(&math, qa_vec3_access_plain(&direction), remaining,
                      qa_vec3_access_plain(&punch));
  return punch;
}
qa_item_id qa_q1_ammo_item(qa_q1_weapon weapon) {
  if (same(weapon, "axe")) return NULL;
  if (same(weapon, "shotgun") || same(weapon, "supershotgun"))
    return "q1:ammo/shells";
  if (same(weapon, "nailgun") || same(weapon, "supernailgun"))
    return "q1:ammo/nails";
  if (same(weapon, "grenadelauncher") || same(weapon, "rocketlauncher"))
    return "q1:ammo/rockets";
  if (same(weapon, "lightning") || same(weapon, "hipnotic:laser") ||
      same(weapon, "mg3:laser"))
    return "q1:ammo/cells";
  if (same(weapon, "hipnotic:mjolnir") || same(weapon, "rogue:grapple") ||
      same(weapon, "ctf:grapple") || same(weapon, "mg3:mjolnir"))
    return NULL;
  if (same(weapon, "hipnotic:proximity")) return "q1:ammo/rockets";
  if (same(weapon, "rogue:lava-nailgun") ||
      same(weapon, "rogue:lava-supernailgun"))
    return "rogue:ammo/lava-nails";
  if (same(weapon, "rogue:multi-grenade") || same(weapon, "rogue:multi-rocket"))
    return "rogue:ammo/multi-rockets";
  if (same(weapon, "rogue:plasma")) return "rogue:ammo/plasma";
  return NULL; /* TS switch is exhaustive over Q1Weapon */
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
  return game->host.random != NULL ? game->host.random(game->host.ctx) : 0;
}
qa_q1_trace qa_q1_host_trace(qa_q1_services *game, qa_q1_trace_request request) {
  if (game->host.trace != NULL) return game->host.trace(game->host.ctx, request);
  qa_q1_trace empty;
  memset(&empty, 0, sizeof empty);
  return empty;
}
void qa_q1_emit(qa_q1_services *game, qa_q1_event event) {
  if (game->host.emit != NULL) game->host.emit(game->host.ctx, event);
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
  for (size_t i = 0; i < game->weapon_rule_count; i++) {
    if (same(game->weapon_rules[i]->id, rules->id))
      qa_range_error("Duplicate Q1 weapon rules: %s", rules->id);
    if (rules->consume_ammo != NULL && game->weapon_rules[i]->consume_ammo != NULL)
      qa_range_error("Q1 source ammunition consumption already registered");
  }
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
  return qa_q1_inventory_consume(game, player->actor, item, amount);
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
  bool best = purpose == NULL || strcmp(purpose, "best") == 0;
  if (qa_q1_inventory_count(game, player->actor.id,
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
  return ammo == NULL ||
         qa_q1_inventory_count(game, player->actor.id, ammo) >= needed;
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
                          qa_q1_spawn_fn handler) {
  for (size_t i = 0; i < game->spawner_count; i++)
    if (same(game->spawners[i].classname, classname))
      qa_range_error("Q1 spawn handler already registered: %s", classname);
  grow((void **)&game->spawners, &game->spawner_count, &game->spawner_capacity,
       sizeof *game->spawners);
  game->spawners[game->spawner_count].classname = dup_string(classname);
  game->spawners[game->spawner_count].fn = handler;
  game->spawner_count++;
}
void qa_q1_replace_spawn(qa_q1_services *game, const char *classname,
                         qa_q1_spawn_fn handler) {
  for (size_t i = 0; i < game->spawner_count; i++)
    if (same(game->spawners[i].classname, classname)) {
      game->spawners[i].fn = handler;
      return;
    }
  qa_range_error("No registered Q1 spawn to replace: %s", classname);
}

/* --- inventory (host table or the services fallback rows) --- */
static size_t inventory_index(qa_q1_services *game, qa_actor_id actor,
                              qa_item_id item, bool create) {
  for (size_t i = 0; i < game->inventory_count; i++)
    if (qa_same_actor(game->inventory[i].actor, actor) &&
        same(game->inventory[i].item, item))
      return i;
  if (!create) return SIZE_MAX;
  grow((void **)&game->inventory, &game->inventory_count,
       &game->inventory_capacity, sizeof *game->inventory);
  size_t at = game->inventory_count++;
  game->inventory[at].actor = actor;
  game->inventory[at].item = dup_string(item);
  game->inventory[at].count = 0;
  game->inventory[at].capacity = 0;
  game->inventory[at].has_capacity = false;
  return at;
}
double qa_q1_inventory_count(qa_q1_services *game, qa_actor_id actor,
                             qa_item_id item) {
  if (game->host.inventory_count != NULL)
    return game->host.inventory_count(game->host.ctx, actor, item);
  size_t at = inventory_index(game, actor, item, false);
  return at == SIZE_MAX ? 0 : game->inventory[at].count;
}
double qa_q1_inventory_give(qa_q1_services *game, qa_owned_actor actor,
                            qa_item_id item, double count) {
  if (game->host.inventory_give != NULL)
    return game->host.inventory_give(game->host.ctx, actor, item, count);
  size_t at = inventory_index(game, actor.id, item, true);
  double given = count;
  if (game->inventory[at].has_capacity) {
    double room = game->inventory[at].capacity - game->inventory[at].count;
    if (given > room) given = room > 0 ? room : 0;
  }
  game->inventory[at].count += given;
  return given;
}
bool qa_q1_inventory_consume(qa_q1_services *game, qa_owned_actor actor,
                             qa_item_id item, double count) {
  if (game->host.inventory_consume != NULL)
    return game->host.inventory_consume(game->host.ctx, actor, item, count);
  size_t at = inventory_index(game, actor.id, item, false);
  if (at == SIZE_MAX || game->inventory[at].count < count) return false;
  game->inventory[at].count -= count;
  return true;
}
static void inventory_configure(qa_q1_services *game, qa_owned_actor actor,
                                qa_item_id item, double count, double capacity) {
  if (game->host.inventory_configure != NULL) {
    game->host.inventory_configure(game->host.ctx, actor, item, count, capacity);
    return;
  }
  size_t at = inventory_index(game, actor.id, item, true);
  game->inventory[at].count = count;
  game->inventory[at].capacity = capacity;
  game->inventory[at].has_capacity = true;
}
void qa_q1_initialize_weapon_inventory(qa_q1_services *game,
                                       qa_owned_actor actor) {
  /* TS: create(entries) when no inventory exists, else configure
   * each entry; the mirror host has no create, so configure serves
   * both (and seeds the fallback rows when no host table is set). */
  for (size_t i = 0; i < 8; i++)
    inventory_configure(game, actor, qa_q1_weapon_item(QA_Q1_WEAPONS[i]),
                        same(QA_Q1_WEAPONS[i], "axe") ||
                                same(QA_Q1_WEAPONS[i], "shotgun")
                            ? 1
                            : 0,
                        1);
  inventory_configure(game, actor, "q1:ammo/shells", 25, 100);
  inventory_configure(game, actor, "q1:ammo/nails", 0, 200);
  inventory_configure(game, actor, "q1:ammo/rockets", 0, 100);
  inventory_configure(game, actor, "q1:ammo/cells", 0, 100);
}

/* --- attach --- */
qa_q1_actor *qa_q1_attach_existing(qa_q1_services *game, qa_owned_actor actor,
                                   const char *classname,
                                   const qa_q1_entity *source,
                                   const double *source_ordinal) {
  qa_q1_actor *entity = xmalloc(sizeof *entity);
  if (!qa_q1_actor_init(entity, actor, classname, source_ordinal, source))
    qa_range_error("Out of memory");
  /* TS admitEntity: assertOwned + duplicate + shared body/combat
   * checks; the mirror's actor-local body/health always exist.
   * Damage-adjustment/callback binding and host.registerEntity
   * have no mirror slots yet. */
  if (entity_any(game, actor.id) != NULL) {
    qa_q1_actor_free(entity);
    free(entity);
    qa_range_error("Q1 actor already attached");
  }
  grow((void **)&game->entities, &game->entity_count, &game->entity_capacity,
       sizeof *game->entities);
  game->entities[game->entity_count++] = entity;
  return entity;
}
qa_q1_player_state *qa_q1_attach_player(qa_q1_services *game,
                                        qa_owned_actor actor, qa_q1_weapon weapon,
                                        bool initialize_inventory,
                                        const double *max_health) {
  qa_q1_player_state *existing = player_owned(game, actor);
  if (existing != NULL) return existing;
  if (initialize_inventory) {
    qa_q1_initialize_weapon_inventory(game, actor);
    inventory_configure(game, actor, "q1:key/silver", 0, 1);
    inventory_configure(game, actor, "q1:key/gold", 0, 1);
  }
  qa_q1_player_state *state = xmalloc(sizeof *state);
  memset(state, 0, sizeof *state);
  state->actor = actor;
  state->weapon = weapon;
  state->weapon_animation_at = -1;
  state->weapon_animation_base = 1;
  state->nail_side = 1;
  state->max_health =
      max_health != NULL
          ? *max_health
          : (same(game->options.edition, "rerelease") &&
                     game->options.skill == 3 && game->options.deathmatch == 0
                 ? 50
                 : 100);
  state->mega_rot_at = -1;
  qa_q1_actor *entity = qa_q1_get_entity(game, &actor.id);
  state->view_angles = entity != NULL ? entity->body.angles : QA_Q1_ZERO;
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
  (void)game;
  return entity != NULL && entity->live;
}
double qa_q1_health(qa_q1_services *game, qa_actor_id actor) {
  qa_q1_actor *entity = qa_q1_get_entity(game, &actor);
  return entity == NULL ? 0 : entity->health;
}
void qa_q1_set_health(qa_q1_services *game, qa_owned_actor actor, double health) {
  qa_q1_actor *entity = entity_any(game, actor.id);
  if (entity != NULL) entity->health = health;
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
  entity->body = body;
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
  qa_q1_actor *entity = entity_any(game, actor.id);
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
  double gravity = game->options.gravity;
  if (gib) {
    double scale = qa_q1_actor_number(entity, "gravity");
    if (scale == 0 || scale != scale) scale = 1; /* TS || 1 (0/NaN falsy) */
    gravity = scale * game->options.gravity;
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
void qa_q1_physics_entity(qa_q1_services *game, qa_owned_actor actor, double seconds,
                          double elapsed_seconds) {
  qa_q1_services_set_time(game, seconds);
  game->frame_seconds = elapsed_seconds;
  qa_q1_actor *entity = entity_any(game, actor.id);
  if (entity == NULL || !qa_q1_live(game, entity)) return;
  apply_projectile_behavior(game, actor, seconds);
  if (same(entity->movement, "push")) {
    /* TS stepQ1Pusher({actor, elapsedSeconds, "translate"},
     * host.pusherServices(this)); the mirror host seam is pusher_step. */
    if (game->host.pusher_step != NULL)
      (void)game->host.pusher_step(game->host.ctx, game, entity, elapsed_seconds);
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
    if (snapshot[i] != NULL) qa_q1_physics_entity(game, snapshot[i]->actor, seconds, elapsed_seconds);
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
  /* TS host.bodies.link recomputes shared-table links; the
   * actor-local mirror body needs none. */
  (void)game;
  (void)entity;
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
  if (!qa_q1_live(game, entity)) return;
  entity->live = false;
  /* TS host.actors.release -> onRelease deletes the entity/player/
   * mission maps and cancels the think. */
  for (size_t i = 0; i < game->entity_count; i++)
    if (game->entities[i] == entity) {
      splice_actor((void **)&game->entities, &game->entity_count,
                   sizeof *game->entities, i);
      break;
    }
  for (size_t i = 0; i < game->player_count; i++)
    if (game->players[i] != NULL &&
        qa_same_actor(game->players[i]->actor.id, entity->actor.id)) {
      splice_actor((void **)&game->players, &game->player_count,
                   sizeof *game->players, i);
      break;
    }
  for (size_t i = 0; i < game->mission_count; i++)
    if (qa_same_actor(game->missions[i].actor, entity->actor.id)) {
      splice_actor((void **)&game->missions, &game->mission_count,
                   sizeof *game->missions, i);
      break;
    }
  if (game->host.cancel_think != NULL)
    game->host.cancel_think(game->host.ctx, entity->actor);
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
    if (game->host.cancel_think != NULL)
      game->host.cancel_think(game->host.ctx, entity->actor);
    return;
  }
  if (game->host.schedule_think != NULL)
    game->host.schedule_think(game->host.ctx, entity->actor, entity->next_think);
}
void qa_q1_cancel(qa_q1_services *game, qa_q1_actor *entity) {
  entity->next_think = -1;
  memset(&entity->think, 0, sizeof entity->think);
  if (game->host.cancel_think != NULL)
    game->host.cancel_think(game->host.ctx, entity->actor);
}
void qa_q1_run_think(qa_q1_services *game, qa_q1_actor *entity) {
  /* TS bindActorCallbacks think dispatch (frame time is applied by
   * the host before this runs in the C mirror). */
  qa_q1_bound_action callback = entity->think;
  memset(&entity->think, 0, sizeof entity->think);
  entity->next_think = -1;
  if (callback.fn != NULL) callback.fn(callback.ctx, game, entity);
}
void qa_q1_resume_thinks(qa_q1_services *game) {
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (entity != NULL && !same(entity->movement, "push") &&
        entity->think.fn != NULL && entity->next_think >= 0 &&
        game->host.schedule_think != NULL)
      game->host.schedule_think(game->host.ctx, entity->actor,
                                entity->next_think);
  }
}

/* --- events --- */
void qa_q1_sound(qa_q1_services *game, qa_actor_id actor, const char *path,
                 qa_q1_sound_channel channel, double attenuation, double volume) {
  qa_q1_actor *entity = qa_q1_get_entity(game, &actor);
  if (entity == NULL) qa_range_error("Missing Q1 sound emitter body");
  qa_q1_body body = entity->body;
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
    if (entity != NULL && same(entity->targetname, targetname)) {
      if (out != NULL && found < capacity) out[found] = entity;
      found++;
    }
  }
  return found;
}
size_t qa_q1_presentations(qa_q1_services *game, qa_q1_presentation *out,
                           size_t capacity) {
  size_t found = 0;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (entity == NULL || !entity->live) continue;
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
      entity != NULL && ((int64_t)entity->movement_flags & 128) != 0;
  result.value.invisible =
      player != NULL &&
      qa_q1_player_powerup_get(player, "invisibility") > game->time;
  result.value.light_level = NULL;
  result.value.hostile_until = player == NULL ? NULL : &player->hostile_until;
  return result;
}
bool qa_q1_can_damage(qa_q1_services *game, qa_actor_id target,
                      qa_actor_id inflictor) {
  qa_q1_actor *target_entity = qa_q1_get_entity(game, &target);
  qa_q1_actor *source_entity = qa_q1_get_entity(game, &inflictor);
  if (target_entity == NULL || source_entity == NULL) return false;
  qa_q1_body target_body = target_entity->body;
  qa_q1_body source = source_entity->body;
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
  if (game->host.powerup != NULL)
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
const qa_q1_monster_mission *qa_q1_mission(qa_q1_services *game,
                                           qa_actor_id actor) {
  for (size_t i = 0; i < game->mission_count; i++)
    if (qa_same_actor(game->missions[i].actor, actor))
      return game->missions[i].mission;
  return NULL;
}
void qa_q1_set_mission(qa_q1_services *game, qa_actor_id actor,
                       const qa_q1_monster_mission *mission) {
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
void qa_q1_player_input(qa_q1_services *game, qa_owned_actor actor, bool attack,
                        bool jump, const double *teleport_until) {
  qa_q1_player_state *player = player_owned(game, actor);
  if (player == NULL) return;
  player->attack_held = attack;
  player->jump_held = jump;
  if (teleport_until != NULL) player->teleport_until = *teleport_until;
}
void qa_q1_travel(qa_q1_services *game, const char *map,
                  const qa_actor_id *cause) {
  if (game->host.transition == NULL) return;
  /* TS transition intent map is `q1:${map}`; the mirror host
   * transition takes that map string plus the cause. */
  size_t length = strlen(map) + 4;
  char *named = xmalloc(length);
  memcpy(named, "q1:", 3);
  memcpy(named + 3, map, strlen(map) + 1);
  game->host.transition(game->host.ctx, named, cause);
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
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "intermission";
  event.has_origin = true;
  event.origin = body.origin;
  event.angles = same(selected->classname, "info_intermission")
                     ? qa_q1_actor_vector(selected, "mangle")
                     : body.angles;
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
  qa_q1_player_state *player = player_owned(game, actor);
  if (player == NULL ||
      qa_q1_inventory_count(game, actor.id,
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
void qa_q1_weapon_frame(qa_q1_services *game, qa_owned_actor actor,
                        double seconds) {
  qa_q1_services_set_time(game, seconds);
  qa_q1_player_state *player = player_owned(game, actor);
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
  qa_q1_player_state *player = player_owned(game, actor);
  if (player == NULL) return;
  for (size_t i = 0; i < game->player_extension_count; i++)
    if (game->player_extensions[i]->after_physics != NULL)
      game->player_extensions[i]->after_physics(
          game->player_extensions[i]->ctx, game, player, seconds);
}
void qa_q1_player_frame(qa_q1_services *game, qa_owned_actor actor, double seconds,
                        const double *water_level) {
  qa_q1_services_set_time(game, seconds);
  qa_q1_player_state *player = player_owned(game, actor);
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
      qa_q1_set_health(game, actor, health - 1);
      player->mega_rot_at = seconds + 1;
    } else {
      player->mega_rot_at = -1;
    }
  }
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
    if (game->host.powerup != NULL)
      game->host.powerup(game->host.ctx, actor, kinds[i], 0);
    if (same(kinds[i], "q3:flight") && game->host.set_gravity != NULL)
      qa_q1_set_gravity(game, actor.id, 1);
  }
  if (qa_q1_player_powerup_get(player, "q3:regen") > seconds &&
      game->frame_seconds > 0) {
    double health = qa_q1_health(game, actor.id);
    if (health > 0 && health < 200) {
      double healed = health + 5 * game->frame_seconds;
      qa_q1_set_health(game, actor, healed < 200 ? healed : 200);
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
  qa_actor_id world_id = game->world != NULL ? game->world->actor.id : actor.id;
  const qa_actor_id *world_attacker = game->world != NULL ? &world_id : NULL;
  if (player->water_level != 3 || suit || lava_suit) {
    player->air_finished = seconds + 12;
    player->drown_damage = 2;
  } else if (player->air_finished < seconds && player->drown_at < seconds) {
    player->drown_damage += 2;
    if (player->drown_damage > 15) player->drown_damage = 10;
    qa_q1_damage(game, actor.id, world_id, world_attacker, player->drown_damage,
                 NULL, "direct", "drown");
    player->drown_at = seconds + 1;
  }
  qa_q1_actor *entity = qa_q1_get_entity(game, &actor.id);
  if (entity != NULL && player->water_level > 0 && player->hazard_at < seconds &&
      game->host.contents != NULL) {
    qa_q1_body body = entity->body;
    qa_vec3 point = body.origin;
    point.z += body.bounds.min.z + 1;
    const char *contents = game->host.contents(game->host.ctx, point);
    if (same(contents, "lava") && !lava_suit) {
      player->hazard_at = seconds + (suit ? 1 : 0.2);
      qa_q1_damage(game, actor.id, world_id, world_attacker,
                   10 * player->water_level, NULL, "direct", "lava");
    } else if (same(contents, "slime") && !suit && !lava_suit) {
      player->hazard_at = seconds + 1;
      qa_q1_damage(game, actor.id, world_id, world_attacker,
                   4 * player->water_level, NULL, "direct", "slime");
    }
  }
}
bool qa_q1_attack(qa_q1_services *game, qa_owned_actor actor, qa_vec3 view_angles,
                  double seconds, double water_level) {
  qa_q1_services_set_time(game, seconds);
  qa_q1_player_state *player = player_owned(game, actor);
  if (player == NULL) qa_range_error("Player has no Q1 weapon state");
  player->view_angles = view_angles;
  player->water_level = water_level;
  return qa_q1_fire_weapon(game, player);
}
bool qa_q1_weapon_input(qa_q1_services *game, qa_owned_actor actor, bool pressed,
                        qa_vec3 view_angles, double seconds, double water_level) {
  qa_q1_player_state *player = player_owned(game, actor);
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
  qa_q1_callback_registry_register(game->named, "SUB_Remove", handlers);
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
  qa_q1_foundation_register_monster_callbacks(game);
  /* TS also installs host.actors.onRelease cleanup; the mirror
   * host has no release hook, so qa_q1_remove performs it. */
  return true;
}
void qa_q1_services_free(qa_q1_services *game) {
  if (game == NULL) return;
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
  for (size_t i = 0; i < game->path_touch_count; i++) free(game->path_touches[i].id);
  free(game->path_touches);
  for (size_t i = 0; i < game->spawner_count; i++) free(game->spawners[i].classname);
  free(game->spawners);
  free(game->missions);
  for (size_t i = 0; i < game->inventory_count; i++) free(game->inventory[i].item);
  free(game->inventory);
  free(game->inventory_owners);
  memset(game, 0, sizeof *game);
}
qa_q1_actor *qa_q1_create(qa_q1_services *game, const char *classname,
                          const qa_q1_entity *source,
                          const double *source_ordinal) {
  double max_clients = game->options.has_max_clients ? game->options.max_clients : 0;
  double slot = source_ordinal == NULL
                    ? game->next_dynamic_slot++
                    : *source_ordinal == 0 ? 0 : *source_ordinal + max_clients;
  /* TS host.actors.allocateAtSource(provider, slot, `q1:${classname}`)
   * mints the OwnedActor; the mirror host has no allocator, so the
   * owned actor is synthesized with a NULL identity token (it can
   * never qa_same_actor-match a registry-minted actor) and
   * generation 0. Bodies/combat creation is the actor-local body
   * below plus health = maxHealth, as in TS create(). */
  qa_owned_actor owner;
  memset(&owner, 0, sizeof owner);
  owner.id.slot = (int64_t)slot;
  owner.id.generation = 0;
  owner.owner = qa_q1_services_provider(game);
  qa_q1_actor *entity = xmalloc(sizeof *entity);
  if (!qa_q1_actor_init(entity, owner, classname, source_ordinal, source))
    qa_range_error("Out of memory");
  entity->body.origin =
      source == NULL ? QA_Q1_ZERO : qa_q1_actor_vector(entity, "origin");
  entity->body.angles =
      source == NULL ? QA_Q1_ZERO : qa_q1_source_angles(source);
  entity->body.velocity = QA_Q1_ZERO;
  entity->body.bounds = QA_Q1_POINT;
  entity->body.has_ground = false;
  entity->health = entity->max_health;
  if (entity_any(game, owner.id) != NULL) {
    qa_q1_actor_free(entity);
    free(entity);
    qa_range_error("Q1 actor already attached");
  }
  grow((void **)&game->entities, &game->entity_count, &game->entity_capacity,
       sizeof *game->entities);
  game->entities[game->entity_count++] = entity;
  return entity;
}
void qa_q1_spawn_entity(qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1_spawn_fn extension = NULL;
  for (size_t i = 0; i < game->spawner_count; i++)
    if (same(game->spawners[i].classname, entity->classname))
      extension = game->spawners[i].fn;
  if (extension == NULL)
    qa_q1_spawn_map_actor(game, entity);
  else
    extension(game, entity);
  if (qa_q1_live(game, entity)) qa_q1_link(game, entity);
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
  double wait = travel > 0.1 ? travel : 0.1;
  qa_q1_schedule_at(game, entity,
                    (float)(qa_q1_actor_number(entity, "ltime") + wait),
                    named_action(game, entity, "SUB_CalcMoveDone"));
}
static size_t target_actors(qa_q1_services *game, const char *name,
                            qa_q1_actor **out) {
  if (name == NULL || name[0] == '\0') return 0;
  size_t count = 0;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (entity != NULL && entity->live && same(entity->targetname, name))
      out[count++] = entity;
  }
  /* TS sorts by source slot (host sourceOf ?? id.slot); mirror
   * created actors carry their source slot in id.slot. Authored
   * targets have no services array in the mirror struct. */
  for (size_t i = 1; i < count; i++) {
    qa_q1_actor *held = out[i];
    size_t k = i;
    while (k > 0 && out[k - 1]->actor.id.slot > held->actor.id.slot) {
      out[k] = out[k - 1];
      k--;
    }
    out[k] = held;
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
    qa_q1_schedule(game, delayed, entity->delay,
                   named_action(game, delayed, "DelayThink"));
    return;
  }
  qa_q1_message(game, activator, entity->message != NULL ? entity->message : "",
                true, NULL, 0);
  qa_q1_actor **targets =
      xmalloc((game->entity_count != 0 ? game->entity_count : 1) *
              sizeof *targets);
  size_t count = target_actors(game, entity->killtarget, targets);
  for (size_t i = 0; i < count; i++)
    if (targets[i]->live) qa_q1_remove(game, targets[i]);
  /* Each use executes synchronously; targets removed by a nested
   * call are not invoked afterward (fresh scan, live re-check). */
  count = target_actors(game, entity->target, targets);
  for (size_t i = 0; i < count; i++) {
    if (!targets[i]->live) continue;
    /* TS host.callbacks.use(target.actor, entity.actor.id, activator);
     * the mirror dispatches the target's bound use slot. */
    qa_actor_id other = entity->actor.id;
    qa_q1_call_use(game, targets[i], targets[i]->use, &other, activator);
  }
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
  if (game->host.contents == NULL) return;
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

/* --- damage (combat-lite mirror; see types.h) --- */
double qa_q1_damage(qa_q1_services *game, qa_actor_id target,
                    qa_actor_id inflictor, const qa_actor_id *attacker,
                    double amount, qa_q1_weapon weapon, const char *delivery,
                    const char *death_type) {
  /* TS scales with staged fround and builds a DamageRequest whose
   * geometry/attack provenance the mirror host does not carry;
   * the sequence still advances exactly as in TS. */
  double multiplier = 1;
  if (attacker != NULL && game->host.source_damage_multiplier != NULL)
    multiplier = game->host.source_damage_multiplier(game->host.ctx, *attacker);
  else if (attacker == NULL)
    multiplier = 1;
  double scaled = (float)((float)amount * (float)multiplier);
  game->sequence++;
  if (game->host.apply_damage != NULL)
    return game->host.apply_damage(game->host.ctx, target, inflictor, attacker,
                                   scaled, weapon,
                                   delivery != NULL ? delivery : "direct",
                                   death_type != NULL ? death_type : "");
  /* Direct health adjust fallback (types.h): no armor stages or
   * pain/die reactions without the combat authority. */
  qa_q1_actor *entity = qa_q1_get_entity(game, &target);
  if (entity == NULL) return 0;
  entity->health -= scaled;
  return scaled;
}
void qa_q1_radius_damage(qa_q1_services *game, qa_actor_id inflictor,
                         const qa_actor_id *attacker, double amount,
                         const qa_actor_id *ignore, qa_q1_weapon weapon,
                         const char *death_type) {
  qa_q1_actor *source_entity = qa_q1_get_entity(game, &inflictor);
  if (source_entity == NULL) return;
  qa_q1_body source = source_entity->body;
  /* TS iterates host actor observations; the mirror iterates the
   * attached entities (combat canTakeDamage is damageable). */
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (entity == NULL || !entity->live || !entity->damageable) continue;
    qa_actor_id target = entity->actor.id;
    if (ignore != NULL && qa_same_actor(*ignore, target)) continue;
    qa_q1_body body = entity->body;
    qa_vec3 center = qa_q1_vadd(
        body.origin, qa_q1_vscale(qa_q1_vadd(body.bounds.min, body.bounds.max), 0.5));
    double distance = qa_q1_length(qa_q1_vsub(center, source.origin));
    if (distance > amount + 40) continue;
    double points = (float)(amount - 0.5 * distance);
    if (attacker != NULL && qa_same_actor(target, *attacker)) points *= 0.5;
    const char *classname = game->host.classname != NULL
                                ? game->host.classname(game->host.ctx, target)
                                : entity->classname;
    if (same(classname, "monster_shambler")) points *= 0.5;
    if (points > 0 && qa_q1_can_damage(game, target, inflictor))
      qa_q1_damage(game, target, inflictor, attacker, points, weapon, "radius",
                   death_type);
  }
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
  target->body = qa_q1_get_body(game, source);
  /* TS copies combat health/armor/traits; the mirror combat state
   * is the actor-local health. A clone of a released source has no
   * body above (qa_q1_get_body aborts, like the TS combat read). */
  target->health = source->health;
  bool source_has_inventory =
      game->host.inventory_has != NULL
          ? game->host.inventory_has(game->host.ctx, source->actor.id)
          : false;
  if (game->host.inventory_has == NULL) {
    /* Fallback rows are the only inventory the mirror can copy. */
    size_t rows = game->inventory_count;
    for (size_t i = 0; i < rows; i++)
      if (qa_same_actor(game->inventory[i].actor, source->actor.id)) {
        size_t at =
            inventory_index(game, target->actor.id, game->inventory[i].item, true);
        game->inventory[at].count = game->inventory[i].count;
        game->inventory[at].capacity = game->inventory[i].capacity;
        game->inventory[at].has_capacity = game->inventory[i].has_capacity;
      }
  }
  (void)source_has_inventory;
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
      target->next_think >= 0 && game->host.schedule_think != NULL)
    game->host.schedule_think(game->host.ctx, target->actor, target->next_think);
  return target;
}
