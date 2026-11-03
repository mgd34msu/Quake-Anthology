/*
 * Port of src/content/q2/base/player/index.ts. Source player phases
 * are invoked by the common simulation. This is not a second
 * G_RunFrame. GPL-2.0-or-later.
 */
#include "qa/content/q2/base/player/index.h"
#include "qa/content/q2/foundation/monsters/gibs.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/base/player/commands.h"
#include "qa/content/q2/base/player/environment.h"
#include "qa/content/q2/base/player/landmarks.h"
#include "qa/content/q2/base/player/obituary.h"
#include "qa/content/q2/base/player/spawns.h"
#include "qa/content/q2/base/player/view.h"
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/weapons/presentation.h"
#include "qa/content/q2/foundation/weapons/turn.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"

typedef struct player_entry {
  char model[600];
  qa_actor_id actor;
  qa_q2_base_player_state *state;
  qa_q2_entity *entity;
} player_entry;

/* Stable storage behind entity->last_attack (TS keeps the attack object
 * alive by reference). One record per actor, reused for each new attack and
 * kept until the players object is destroyed, so a pointer an entity holds
 * never moves or dangles; the provenance's actor pointers target the
 * record's own copies. */
typedef struct attack_record {
  qa_actor_id actor;
  qa_attack_provenance attack;
  qa_actor_id attacker, inflictor;
  struct attack_record *next;
} attack_record;

typedef struct release_record {
  qa_q2_players *players;
  qa_session_actor_registry *registry;
  qa_actor_id actor;
  int64_t subscription;
} release_record;

struct qa_q2_players {
  qa_q2_item_module *items;
  qa_q2w_weapons *weapons;
  qa_q2_player_hooks hooks;
  /* TS subclass dispatch: overrides and the subclass object. */
  const qa_q2_players_vtable *vtable;
  void *self;
  char *intermission_map; /* owned copy behind intermission.map */
  qa_q2_base_player_rules rules;
  const char **owned_map_list;
  qa_q2_intermission intermission;
  player_entry *entries; size_t entry_count, entry_cap;
  release_record **releases; size_t release_count, release_cap;
  double corpse_index, death_animation, pain_animation;
  qa_q2_named_pain pains[1];
  qa_q2_named_die dies[2];
  qa_q2_callback_definitions defs, spawn_defs;
  qa_q2_spawn_module spawn;
  qa_actor_id chase_scratch[64];
  attack_record *attacks;
};

static const qa_attack_provenance *keep_attack(qa_q2_players *players, qa_actor_id actor, qa_attack_provenance attack) {
  attack_record *record = players->attacks;
  while (record && !qa_same_actor(record->actor, actor)) record = record->next;
  if (!record) {
    record = calloc(1, sizeof *record);
    if (!record) qa_range_error("Out of memory");
    record->actor = actor;
    record->next = players->attacks;
    players->attacks = record;
  }
  record->attack = attack;
  if (attack.attacker) { record->attacker = *attack.attacker; record->attack.attacker = &record->attacker; }
  if (attack.inflictor) { record->inflictor = *attack.inflictor; record->attack.inflictor = &record->inflictor; }
  return &record->attack;
}

static void spectator_respawn_fn(qa_q2_players *, qa_q2_entity *, qa_q2_game_services *);
static void player_pain_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_pain_reaction);
static void player_die_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_death_reaction);
static void body_die_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_death_reaction);
static bool players_spawn_fn(void *, qa_q2_entity *, qa_q2_game_services *);

static player_entry *find_entry(qa_q2_players *players, qa_actor_id actor) {
  for (size_t i = 0; i < players->entry_count; i++)
    if (qa_same_actor(players->entries[i].actor, actor)) return &players->entries[i];
  return NULL;
}
static void remove_entry(qa_q2_players *players, qa_actor_id actor) {
  for (size_t i = 0; i < players->entry_count; i++) {
    if (qa_same_actor(players->entries[i].actor, actor)) {
      qa_q2_base_player_state_destroy(players->entries[i].state);
      free(players->entries[i].state);
      /* TS Map.delete keeps the remaining insertion order. */
      memmove(&players->entries[i], &players->entries[i + 1], (players->entry_count - i - 1) * sizeof *players->entries);
      players->entry_count--;
      return;
    }
  }
}
static player_entry *add_entry(qa_q2_players *players, qa_actor_id actor, qa_q2_entity *entity) {
  if (players->entry_count == players->entry_cap) {
    size_t cap = players->entry_cap ? players->entry_cap * 2 : 8;
    player_entry *grown = realloc(players->entries, cap * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    players->entries = grown; players->entry_cap = cap;
  }
  player_entry *entry = &players->entries[players->entry_count++];
  *entry = (player_entry){0};
  entry->actor = actor;
  entry->entity = entity;
  entry->state = calloc(1, sizeof *entry->state);
  if (!entry->state) qa_range_error("Out of memory");
  return entry;
}

qa_q2_base_player_state *qa_q2_players_get_state(qa_q2_players *players, qa_actor_id actor) {
  player_entry *entry = find_entry(players, actor);
  return entry ? entry->state : NULL;
}
void qa_q2_players_set_state(qa_q2_players *players, qa_actor_id actor, qa_q2_base_player_state *state) {
  player_entry *entry = find_entry(players, actor);
  if (!entry) {
    entry = add_entry(players, actor, NULL);
    free(entry->state);
  } else {
    qa_q2_base_player_state_destroy(entry->state);
    free(entry->state);
  }
  entry->state = state;
}
size_t qa_q2_players_state_count(qa_q2_players *players) { return players->entry_count; }
bool qa_q2_players_state_at(qa_q2_players *players, size_t index, qa_actor_id *out_actor, qa_q2_base_player_state **out_state) {
  if (index >= players->entry_count) return false;
  *out_actor = players->entries[index].actor;
  *out_state = players->entries[index].state;
  return true;
}

const char *qa_q2_userinfo_get(const char *source, const char *key, char *scratch, size_t scratch_size) {
  const char *cursor = source && source[0] == '\\' ? source + 1 : source;
  if (!cursor) return NULL;
  /* Tokens alternate key/value; the first occurrence wins. */
  for (;;) {
    const char *key_end = strchr(cursor, '\\');
    if (!key_end) return NULL;
    size_t key_length = (size_t)(key_end - cursor);
    const char *value = key_end + 1;
    const char *value_end = strchr(value, '\\');
    size_t value_length = value_end ? (size_t)(value_end - value) : strlen(value);
    if (key_length == strlen(key) && strncmp(cursor, key, key_length) == 0) {
      if (value_length >= scratch_size) value_length = scratch_size - 1;
      memcpy(scratch, value, value_length);
      scratch[value_length] = '\0';
      return scratch;
    }
    if (!value_end) return NULL;
    cursor = value_end + 1;
  }
}
static double info_number(const char *value) {
  if (!value) return 0;
  while (isspace((unsigned char)*value)) value++;
  bool negative = false;
  if (*value == '+' || *value == '-') { negative = *value == '-'; value++; }
  if (!isdigit((unsigned char)*value)) return 0;
  double result = 0;
  while (isdigit((unsigned char)*value)) { result = result * 10 + (*value - '0'); value++; }
  return negative ? -result : result;
}

qa_q2_players *qa_q2_players_create(qa_q2_item_module *items, qa_q2w_weapons *weapons,
                                    qa_q2_player_hooks hooks, const qa_q2_base_player_rules *rules, uint32_t rules_mask) {
  return qa_q2_players_create_derived(items, weapons, hooks, rules, rules_mask, NULL, NULL);
}
qa_q2_players *qa_q2_players_create_derived(qa_q2_item_module *items, qa_q2w_weapons *weapons,
                                            qa_q2_player_hooks hooks, const qa_q2_base_player_rules *rules,
                                            uint32_t rules_mask, const qa_q2_players_vtable *vtable, void *self) {
  qa_q2_players *players = calloc(1, sizeof *players);
  if (!players) return NULL;
  players->items = items;
  players->weapons = weapons;
  players->vtable = vtable;
  players->self = self;
  players->hooks = hooks;
  players->rules = qa_q2_create_player_rules(rules, rules_mask);
  players->intermission.kind = "playing";
  players->pains[0] = (qa_q2_named_pain){"player_pain", {players, player_pain_fn}};
  players->dies[0] = (qa_q2_named_die){"player_die", {players, player_die_fn}};
  players->dies[1] = (qa_q2_named_die){"body_die", {players, body_die_fn}};
  /* TS { ...q2PlayerSpawnCallbacks, pain: { player_pain }, die: { player_die, body_die } }. */
  const qa_q2_callback_definitions *spawn_callbacks = qa_q2_player_spawn_callbacks();
  players->defs = (qa_q2_callback_definitions){.think = spawn_callbacks->think, .think_count = spawn_callbacks->think_count,
                                               .pain = players->pains, .pain_count = 1, .die = players->dies, .die_count = 2};
  players->spawn = (qa_q2_spawn_module){players, &players->defs, NULL, players_spawn_fn};
  return players;
}
void qa_q2_players_destroy(qa_q2_players *players) {
  if (!players) return;
  for (size_t i = 0; i < players->entry_count; i++) {
    qa_q2_base_player_state_destroy(players->entries[i].state);
    free(players->entries[i].state);
  }
  /* Unsubscribe before freeing: the registry outlives this object in
   * the normal teardown order and must not dispatch into it. */
  for (size_t i = 0; i < players->release_count; i++) {
    qa_session_actor_registry_off_release(players->releases[i]->registry, players->releases[i]->subscription);
    free(players->releases[i]);
  }
  free(players->entries);
  free(players->releases);
  free(players->owned_map_list);
  free(players->intermission_map);
  while (players->attacks) { attack_record *next = players->attacks->next; free(players->attacks); players->attacks = next; }
  free(players);
}
qa_q2_item_module *qa_q2_players_items(qa_q2_players *players) { return players->items; }
qa_q2w_weapons *qa_q2_players_weapons(qa_q2_players *players) { return players->weapons; }
qa_q2_player_hooks *qa_q2_players_hooks(qa_q2_players *players) { return &players->hooks; }
qa_q2_base_player_rules *qa_q2_players_rules(qa_q2_players *players) { return &players->rules; }
qa_q2_intermission qa_q2_players_intermission(qa_q2_players *players) { return players->intermission; }
void qa_q2_players_set_intermission(qa_q2_players *players, qa_q2_intermission intermission) {
  char *map = NULL;
  if (intermission.map) {
    map = malloc(strlen(intermission.map) + 1);
    if (!map) qa_range_error("Out of memory");
    strcpy(map, intermission.map);
  }
  free(players->intermission_map);
  players->intermission_map = map;
  intermission.map = map;
  players->intermission = intermission;
}
qa_q2_spawn_module qa_q2_players_as_spawn(qa_q2_players *players) {
  players->spawn_defs = qa_q2_players_callbacks(players);
  players->spawn.callbacks = &players->spawn_defs;
  return players->spawn;
}
bool qa_q2_players_super_spawn(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)players;
  return qa_q2_player_spawns_spawn(entity, game);
}
static bool players_spawn_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  return qa_q2_players_spawn(ctx, entity, game);
}
qa_q2_callback_definitions qa_q2_players_super_callbacks(qa_q2_players *players) { return players->defs; }

qa_q2_player_context qa_q2_players_context(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_base_player_state *state = qa_q2_players_get_state(players, entity->actor.id);
  if (!state) qa_range_error("Q2 player has not been admitted");
  qa_q2_player_context context = {0};
  context.entity = entity;
  context.game = game;
  context.state = state;
  context.movement = players->hooks.movement(players->hooks.ctx, entity->actor.id);
  context.rules = &players->rules;
  context.hooks = &players->hooks;
  context.items = players->items;
  context.weapons = players->weapons;
  context.players = players;
  return context;
}

qa_q2_player_powerups qa_q2_player_context_powerups(qa_q2_players *players, qa_q2_entity *entity) {
  return qa_q2_item_module_player_powerups(players->items, entity->actor.id);
}
qa_q2w_state *qa_q2_players_weapon_state(qa_q2_players *players, qa_actor_id actor) {
  return players->weapons ? qa_q2w_ballistics_state(&players->weapons->base, actor) : NULL;
}
bool qa_q2_player_context_weapon_state(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_character_weapon *out) {
  if (players->hooks.weapon_state) return players->hooks.weapon_state(players->hooks.ctx, entity->actor.id, out);
  qa_q2w_state *weapon = qa_q2_players_weapon_state(players, entity->actor.id);
  if (!weapon) return false;
  out->q2_name = weapon->weapon;
  out->ammo = weapon->weapon == NULL ? NULL : qa_q2w_weapons_definition(players->weapons, weapon->weapon)->ammo;
  qa_q2w_recoil_result recoil = qa_q2w_weapon_recoil(weapon, game->options.edition, game->host->now(game->host->ctx));
  out->kick_origin = recoil.kick_origin;
  out->kick_angles = recoil.kick_angles;
  out->loop_sound = weapon->loop_sound ? weapon->loop_sound : "";
  return true;
}
void qa_q2_player_context_environment_damage(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                             double amount, double means, double flags) {
  (void)players;
  qa_actor_id world = game->host->world_actor(game->host->ctx);
  qa_attack_provenance attack = game->attack(game->ctx, entity, entity->actor.id, &world, means, flags, NULL);
  /* The TS spread keeps the runtime-built provenance but retargets its
   * inflictor to the world actor; the request lives for the apply call. */
  qa_actor_id inflictor = world;
  attack.inflictor = &inflictor;
  qa_damage_request request = {0};
  request.attack = attack;
  request.target = entity->actor.id;
  request.amount = amount;
  request.knockback = 0;
  request.direction = means == 22 ? (qa_vec3){0, 0, 1} : QA_Q2_ZERO;
  request.point = qa_q2_game_body(game, entity).origin;
  request.normal = QA_Q2_ZERO;
  request.delivery = "direct";
  (void)qa_gameplay_authority_apply(game->host->combat, request, NULL, NULL);
}

qa_q2_base_players_checkpoint qa_q2_players_capture(qa_q2_players *players) {
  qa_q2_base_players_checkpoint out = {0};
  out.version = 1;
  out.corpse_index = players->corpse_index;
  out.death_animation = players->death_animation;
  out.pain_animation = players->pain_animation;
  out.rules = players->rules;
  qa_q2_intermission intermission = players->intermission;
  out.intermission.kind = intermission.kind;
  if (strcmp(intermission.kind, "playing") != 0) {
    out.intermission.map = intermission.map;
    out.intermission.started = intermission.started;
    out.intermission.exit = intermission.exit;
    out.intermission.has_landmark = intermission.has_landmark;
    out.intermission.landmark_first = intermission.landmark_first;
    if (intermission.has_landmark) {
      out.intermission.landmark.player = (qa_saved_actor_id){(double)intermission.landmark.player.slot,
                                                             (double)intermission.landmark.player.generation};
      out.intermission.landmark.name = intermission.landmark.name;
      out.intermission.landmark.relative_origin = intermission.landmark.relative_origin;
      out.intermission.landmark.relative_velocity = intermission.landmark.relative_velocity;
      out.intermission.landmark.relative_view_angles = intermission.landmark.relative_view_angles;
    }
  }
  out.player_count = players->entry_count;
  out.players = malloc((out.player_count ? out.player_count : 1) * sizeof *out.players);
  if (!out.players) qa_range_error("Out of memory");
  for (size_t i = 0; i < players->entry_count; i++) {
    out.players[i].actor = (qa_saved_actor_id){(double)players->entries[i].actor.slot,
                                               (double)players->entries[i].actor.generation};
    qa_q2_base_player_state_checkpoint_capture(players->entries[i].state, &out.players[i].state);
  }
  return out;
}
void qa_q2_base_players_checkpoint_clear(qa_q2_base_players_checkpoint *checkpoint) {
  for (size_t i = 0; i < checkpoint->player_count; i++)
    qa_q2_base_player_state_checkpoint_clear(&checkpoint->players[i].state);
  free(checkpoint->players);
  checkpoint->players = NULL;
  checkpoint->player_count = 0;
}

typedef struct restore_resolver { qa_q2_game_services *game; } restore_resolver;
static qa_actor_id resolve_saved_reference(void *ctx, qa_saved_actor_id saved) {
  restore_resolver *resolver = ctx;
  return qa_session_actor_registry_reference_saved(resolver->game->host->actors, saved, "checkpoint");
}

void qa_q2_players_restore(qa_q2_players *players, qa_q2_game_services *game, qa_q2_base_players_checkpoint checkpoint) {
  for (size_t i = 0; i < players->entry_count; i++) {
    qa_q2_base_player_state_destroy(players->entries[i].state);
    free(players->entries[i].state);
  }
  players->entry_count = 0;
  players->corpse_index = checkpoint.corpse_index;
  players->death_animation = checkpoint.death_animation;
  players->pain_animation = checkpoint.pain_animation;
  /* TS Object.assign(this.rules, structuredClone(checkpoint.rules)): keep
   * the strings and own the map list, since a decoded checkpoint's storage
   * is released after restore. */
  players->rules = checkpoint.rules;
  players->rules.password = qa_q2_checkpoint_keep_string(checkpoint.rules.password);
  players->rules.spectator_password = qa_q2_checkpoint_keep_string(checkpoint.rules.spectator_password);
  players->rules.next_map = qa_q2_checkpoint_keep_string(checkpoint.rules.next_map);
  players->rules.spawn_point = qa_q2_checkpoint_keep_string(checkpoint.rules.spawn_point);
  free(players->owned_map_list);
  players->owned_map_list = NULL;
  if (checkpoint.rules.map_list_count) {
    players->owned_map_list = malloc(checkpoint.rules.map_list_count * sizeof *players->owned_map_list);
    if (!players->owned_map_list) qa_range_error("Out of memory");
    for (size_t i = 0; i < checkpoint.rules.map_list_count; i++)
      players->owned_map_list[i] = qa_q2_checkpoint_keep_string(checkpoint.rules.map_list[i]);
  }
  players->rules.map_list = players->owned_map_list;
  const qa_q2_base_player_intermission_checkpoint *intermission = &checkpoint.intermission;
  if (strcmp(intermission->kind, "playing") == 0) qa_q2_players_set_intermission(players, (qa_q2_intermission){.kind = "playing"});
  else {
    qa_q2_intermission restored = {.kind = "intermission", .map = intermission->map,
        .started = intermission->started, .exit = intermission->exit, .has_landmark = intermission->has_landmark,
        .landmark_first = intermission->landmark_first};
    if (intermission->has_landmark) {
      restored.landmark.player = qa_q2_restore_actor(game, intermission->landmark.player).id;
      restored.landmark.name = qa_q2_checkpoint_keep_string(intermission->landmark.name);
      restored.landmark.relative_origin = intermission->landmark.relative_origin;
      restored.landmark.relative_velocity = intermission->landmark.relative_velocity;
      restored.landmark.relative_view_angles = intermission->landmark.relative_view_angles;
    }
    qa_q2_players_set_intermission(players, restored);
  }
  restore_resolver resolver = {game};
  for (size_t i = 0; i < checkpoint.player_count; i++) {
    const qa_q2_base_player_checkpoint_entry *saved = &checkpoint.players[i];
    qa_owned_actor owner = qa_q2_restore_actor(game, saved->actor);
    qa_q2_entity *entity = game->entity(game->ctx, &owner.id);
    if (!entity) qa_range_error("Q2 player checkpoint references missing source wrapper");
    player_entry *entry = add_entry(players, owner.id, entity);
    qa_q2_base_player_state_init(entry->state, saved->state.slot, saved->state.entered_at);
    qa_q2_base_player_state_checkpoint_restore(entry->state, &saved->state, resolve_saved_reference, &resolver);
  }
}

static void player_pain_fn(void *c, qa_q2_entity *entity, qa_q2_game_services *game, qa_pain_reaction reaction) {
  (void)c; (void)entity; (void)game; (void)reaction;
}
static void player_die_fn(void *c, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction) {
  qa_q2_players_death(c, entity, game, reaction);
}

qa_q2_connection_result qa_q2_players_super_connect(qa_q2_players *players, qa_q2_game_services *game, const char *userinfo) {
  qa_q2_connection_result result = {0};
  char spectator_value[512], password_value[512], ip_value[512];
  const char *spectator_info = qa_q2_userinfo_get(userinfo, "spectator", spectator_value, sizeof spectator_value);
  bool spectator = game->options.mode == QA_Q2_MODE_DEATHMATCH && spectator_info && spectator_info[0] != '\0' && strcmp(spectator_info, "0") != 0;
  const char *pass = spectator ? players->rules.spectator_password : players->rules.password;
  const char *reason = "";
  const char *ip = qa_q2_userinfo_get(userinfo, "ip", ip_value, sizeof ip_value);
  if (players->hooks.banned(players->hooks.ctx, ip ? ip : "")) reason = "Banned.";
  else if (pass[0] != '\0' && strcmp(pass, "none") != 0) {
    const char *supplied = qa_q2_userinfo_get(userinfo, spectator ? "spectator" : "password", password_value, sizeof password_value);
    if (!supplied || strcmp(pass, supplied) != 0)
      reason = spectator ? "Spectator password required or incorrect." : "Password required or incorrect.";
  }
  if (reason[0] == '\0' && spectator) {
    size_t spectators = 0;
    for (size_t i = 0; i < players->entry_count; i++)
      if (players->entries[i].state->connected && players->entries[i].state->requested_spectator) spectators++;
    if ((double)spectators >= players->rules.max_spectators) reason = "Server spectator limit is full.";
  }
  if (reason[0] != '\0') {
    /* Rebuild the userinfo without rejmsg, then append the reason. */
    result.allowed = false;
    (void)snprintf(result.reason, sizeof result.reason, "%s", reason);
    result.userinfo[0] = '\0';
    const char *cursor = userinfo[0] == '\\' ? userinfo + 1 : userinfo;
    while (*cursor) {
      const char *key_end = strchr(cursor, '\\');
      if (!key_end) break;
      const char *value = key_end + 1;
      const char *value_end = strchr(value, '\\');
      size_t key_length = (size_t)(key_end - cursor);
      size_t value_length = value_end ? (size_t)(value_end - value) : strlen(value);
      if (!(key_length == 6 && strncmp(cursor, "rejmsg", 6) == 0)) {
        size_t used = strlen(result.userinfo);
        (void)snprintf(result.userinfo + used, sizeof result.userinfo - used, "\\%.*s\\%.*s",
                       (int)key_length, cursor, (int)value_length, value);
      }
      if (!value_end) break;
      cursor = value_end + 1;
    }
    size_t used = strlen(result.userinfo);
    (void)snprintf(result.userinfo + used, sizeof result.userinfo - used, "\\rejmsg\\%s", reason);
    return result;
  }
  result.allowed = true;
  (void)snprintf(result.userinfo, sizeof result.userinfo, "%s", userinfo);
  return result;
}

static void attach_release(void *ctx, qa_owned_actor actor) {
  release_record *record = ctx;
  /* TS: `if (actor.id === entity.actor.id)`; other releases pass by. */
  if (!qa_same_actor(actor.id, record->actor)) return;
  qa_q2_players *players = record->players;
  remove_entry(players, actor.id);
  qa_session_actor_registry_off_release(record->registry, record->subscription);
  for (size_t i = 0; i < players->release_count; i++) {
    if (players->releases[i] == record) {
      players->releases[i] = players->releases[--players->release_count];
      break;
    }
  }
  free(record);
}

qa_q2_base_player_state *qa_q2_players_super_attach(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_player_admission admission) {
  qa_session_actor_registry_assert_owned(game->host->actors, entity->actor);
  if (find_entry(players, entity->actor.id)) qa_range_error("Q2 player lifecycle already bound");
  if (admission.slot != trunc(admission.slot) || admission.slot < 0 || admission.slot >= game->options.max_clients)
    qa_range_error("Q2 player slot is outside maxclients");
  player_entry *entry = add_entry(players, entity->actor.id, entity);
  qa_q2_base_player_state *state = entry->state;
  qa_q2_base_player_state_init(state, admission.slot, game->host->now(game->host->ctx));
  state->use_q2_weapons = admission.has_use_q2_weapons ? admission.use_q2_weapons : true;
  state->use_q2_inventory = admission.has_use_q2_inventory ? admission.use_q2_inventory
      : strncmp(game->options.inventory_provider, "q2:", 3) == 0;
  state->air_finished = game->host->now(game->host->ctx) + 12;
  release_record *record = malloc(sizeof *record);
  if (!record) qa_range_error("Out of memory");
  *record = (release_record){players, game->host->actors, entity->actor.id, 0};
  record->subscription = qa_session_actor_registry_on_release(game->host->actors, attach_release, record);
  if (players->release_count == players->release_cap) {
    size_t cap = players->release_cap ? players->release_cap * 2 : 8;
    release_record **grown = realloc(players->releases, cap * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    players->releases = grown; players->release_cap = cap;
  }
  players->releases[players->release_count++] = record;
  qa_q2_players_userinfo_changed(players, entity, game, admission.userinfo);
  if (!qa_shared_inventory_table_has(game->host->inventory, entity->actor.id))
    qa_shared_inventory_table_create_entries(game->host->inventory, entity->actor, NULL, 0);
  if (!qa_gameplay_authority_read(game->host->combat, entity->actor.id).has_value) {
    qa_combat_state combat = {0};
    combat.health = 100;
    combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
    combat.armor.powered.kind = "none";
    combat.mass = 200; combat.can_take_damage = true; combat.invulnerable = false; combat.team = NULL;
    qa_gameplay_authority_create_state(game->host->combat, entity->actor, combat, NULL, NULL);
  }
  if (admission.initialize_inventory) {
    qa_q2_item_module_configure_player(players->items, entity->actor, game, true);
    if (game->options.instagib) state->selected_item = "q2:weapon_railgun";
  }
  if (state->use_q2_weapons && !qa_q2_players_weapon_state(players, entity->actor.id))
    (void)qa_q2w_weapons_bind(players->weapons, entity->actor, game, NULL);
  entity->pain = (qa_q2_pain){players, player_pain_fn};
  entity->die = (qa_q2_die){players, player_die_fn};
  if (entity->max_health == 0) entity->max_health = 100;
  if (entity->view_height == 0) entity->view_height = 22;
  if (admission.carry) qa_q2_players_restore_carry(players, entity, game, *admission.carry);
  size_t spawn_count = 0;
  const qa_inventory_entry *spawn_entries = qa_shared_inventory_table_entries(game->host->inventory, entity->actor.id, &spawn_count);
  free(state->spawn_inventory);
  state->spawn_inventory = NULL;
  state->spawn_inventory_count = spawn_count;
  if (spawn_count) {
    state->spawn_inventory = malloc(spawn_count * sizeof *state->spawn_inventory);
    if (!state->spawn_inventory) qa_range_error("Out of memory");
    memcpy(state->spawn_inventory, spawn_entries, spawn_count * sizeof *state->spawn_inventory);
  }
  qa_q2_base_player_carry carry = qa_q2_players_save_carry(players, entity, game);
  qa_q2_base_player_carry_set(&state->coop_respawn, carry);
  qa_q2_base_player_carry_clear(&carry);
  state->has_coop_respawn = true;
  state->spectator = state->requested_spectator;
  return state;
}

void qa_q2_players_super_userinfo_changed(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, const char *source) {
  qa_q2_base_player_state *state = qa_q2_players_get_state(players, entity->actor.id);
  if (!state) qa_range_error("Q2 userinfo requires an admitted player");
  bool valid = !strchr(source, '"') && !strchr(source, ';');
  /* TS slice(0, 511): the fixed buffer holds 511 chars + NUL. */
  (void)snprintf(state->userinfo, sizeof state->userinfo, "%s", valid ? source : "\\name\\badinfo\\skin\\male/grunt");
  if (strlen(state->userinfo) > 511) state->userinfo[511] = '\0';
  char value[512];
  const char *name = qa_q2_userinfo_get(state->userinfo, "name", value, sizeof value);
  (void)snprintf(state->name, sizeof state->name, "%.15s", name ? name : "");
  const char *skin = qa_q2_userinfo_get(state->userinfo, "skin", value, sizeof value);
  (void)snprintf(state->skin, sizeof state->skin, "%s", skin ? skin : "");
  const char *gender = qa_q2_userinfo_get(state->userinfo, "gender", value, sizeof value);
  char gender_letter = gender && gender[0] ? (char)tolower((unsigned char)gender[0]) : '\0';
  state->gender = gender_letter == 'f' ? "female" : gender_letter == 'm' ? "male" : "neutral";
  const char *spectator = qa_q2_userinfo_get(state->userinfo, "spectator", value, sizeof value);
  state->requested_spectator = game->options.mode == QA_Q2_MODE_DEATHMATCH && spectator && spectator[0] != '\0' && strcmp(spectator, "0") != 0;
  double fov = info_number(qa_q2_userinfo_get(state->userinfo, "fov", value, sizeof value));
  state->fov = game->options.mode == QA_Q2_MODE_DEATHMATCH && (qa_js_to_int32(qa_q2_options_deathmatch_flags(&game->options)) & 32768) != 0 ? 90
      : fov < 1 ? 90 : qa_js_min(160, fov);
  double hand = info_number(qa_q2_userinfo_get(state->userinfo, "hand", value, sizeof value));
  state->hand = hand == 1 ? "left" : hand == 2 ? "center" : "right";
  qa_q2_player_event event = {0};
  event.kind = "userinfo";
  event.actor = entity->actor.id;
  event.slot = state->slot;
  event.name = state->name;
  event.skin = state->skin;
  players->hooks.emit(players->hooks.ctx, event);
}

qa_q2_base_player_carry qa_q2_players_super_save_carry(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_player_context context = qa_q2_players_context(players, entity, game);
  qa_q2_base_player_state *state = context.state;
  qa_combat_state_result combat = qa_gameplay_authority_read(game->host->combat, entity->actor.id);
  if (!combat.has_value) qa_range_error("Q2 carry requires shared combat state");
  qa_q2_base_player_carry carry = {0};
  carry.health = combat.value.health;
  carry.maximum_health = entity->max_health;
  carry.armor = combat.value.armor;
  size_t count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host->inventory, entity->actor.id, &count);
  carry.inventory_count = count;
  if (count) {
    carry.inventory = malloc(count * sizeof *carry.inventory);
    if (!carry.inventory) qa_range_error("Out of memory");
    memcpy(carry.inventory, entries, count * sizeof *carry.inventory);
  }
  qa_q2w_state *weapon = qa_q2_players_weapon_state(players, entity->actor.id);
  carry.weapon = weapon ? weapon->weapon : NULL;
  carry.selected_item = state->selected_item;
  carry.score = state->score;
  carry.flags = (double)(qa_js_to_int32(entity->flags) & (16 | 32 | 4096));
  carry.power_cubes = entity->power_cubes;
  return carry;
}

static void set_inventory(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, const qa_inventory_entry *entries, size_t count) {
  size_t catalog_count = 0;
  const qa_q2_inventory_item *catalog = qa_q2_item_module_list(players->items, &catalog_count);
  size_t current_count = 0;
  const qa_inventory_entry *current = qa_shared_inventory_table_entries(game->host->inventory, entity->actor.id, &current_count);
  /* Copy first: configure may invalidate the table's scratch array. */
  qa_inventory_entry *copy = NULL;
  if (current_count) {
    copy = malloc(current_count * sizeof *copy);
    if (!copy) qa_range_error("Out of memory");
    memcpy(copy, current, current_count * sizeof *copy);
  }
  for (size_t i = 0; i < current_count; i++) {
    qa_inventory_entry zeroed = copy[i];
    zeroed.count = 0;
    /* capacity: catalog.get(entry.item) ?? entry.capacity (Map: the last list row with that id wins). */
    for (size_t j = 0; j < catalog_count; j++)
      if (strcmp(catalog[j].id, zeroed.item) == 0) zeroed.capacity = catalog[j].capacity;
    qa_shared_inventory_table_configure(game->host->inventory, entity->actor, zeroed, NULL, NULL);
  }
  free(copy);
  for (size_t i = 0; i < count; i++)
    qa_shared_inventory_table_configure(game->host->inventory, entity->actor, entries[i], NULL, NULL);
}

void qa_q2_players_super_restore_carry(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_base_player_carry carry) {
  qa_q2_player_context context = qa_q2_players_context(players, entity, game);
  qa_q2_base_player_state *state = context.state;
  qa_gameplay_authority_set_health(game->host->combat, entity->actor, carry.health);
  qa_gameplay_authority_set_armor(game->host->combat, entity->actor, carry.armor);
  set_inventory(players, entity, game, carry.inventory, carry.inventory_count);
  entity->max_health = carry.maximum_health;
  entity->flags = (double)(qa_js_to_int32(entity->flags) | qa_js_to_int32(carry.flags));
  entity->power_cubes = carry.power_cubes;
  state->god = (qa_js_to_int32(carry.flags) & 16) != 0;
  state->notarget = (qa_js_to_int32(carry.flags) & 32) != 0;
  state->score = carry.score;
  state->selected_item = carry.selected_item;
  qa_combat_trait_changes tc = {0};
  tc.has_invulnerable = true; tc.invulnerable = state->god;
  qa_gameplay_authority_set_traits(game->host->combat, entity->actor, tc);
  qa_q2w_state *weapon = qa_q2_players_weapon_state(players, entity->actor.id);
  if (weapon) { weapon->weapon = carry.weapon; weapon->pending = NULL; }
}

void qa_q2_players_consumed_key(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_player_context context = qa_q2_players_context(players, entity, game);
  qa_q2_base_player_state *state = context.state;
  if (state->has_coop_respawn) {
    size_t count = 0;
    const qa_inventory_entry *current = qa_shared_inventory_table_entries(game->host->inventory, entity->actor.id, &count);
    qa_q2_base_player_carry next = state->coop_respawn;
    next.power_cubes = entity->power_cubes;
    next.inventory = NULL;
    next.inventory_count = state->coop_respawn.inventory_count;
    if (next.inventory_count) {
      next.inventory = malloc(next.inventory_count * sizeof *next.inventory);
      if (!next.inventory) qa_range_error("Out of memory");
      for (size_t i = 0; i < next.inventory_count; i++) {
        qa_inventory_entry saved = state->coop_respawn.inventory[i];
        if (saved.item && strncmp(saved.item, "q2:key_", 7) == 0) {
          const qa_inventory_entry *found = NULL;
          for (size_t j = 0; j < count; j++) if (current[j].item && strcmp(current[j].item, saved.item) == 0) found = &current[j];
          if (found) saved = *found;
          else saved.count = 0;
        }
        next.inventory[i] = saved;
      }
    }
    qa_q2_base_player_carry_set(&state->coop_respawn, next);
    free(next.inventory);
  }
}

qa_q2_spawn_placement qa_q2_players_super_spawn_placement(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                                    const qa_q2_landmark_carry *landmark) {
  if (players->hooks.select_spawn) {
    qa_vec3 origin, angles;
    if (players->hooks.select_spawn(players->hooks.ctx, entity, game, &origin, &angles))
      return (qa_q2_spawn_placement){origin, angles, QA_Q2_ZERO, false};
  }
  qa_q2_player_context old = qa_q2_players_context(players, entity, game);
  /* Source selects spawn while the prior life still contributes its
   * old health/distance. */
  qa_q2_entity *spot = qa_q2_select_spawn(game, old.state, players->rules.spawn_point);
  qa_q2_landmark_placement placement = {0};
  bool has_placement = landmark && qa_q2_place_landmark(entity, game, *landmark, spot, old.movement.standing_bounds, &placement);
  qa_vec3 origin = has_placement ? qa_q2_add(placement.origin, (qa_vec3){0, 0, 1}) : qa_q2_spawn_origin(spot, game);
  qa_vec3 source_angles = has_placement ? placement.angles : qa_q2_game_body(game, spot).angles;
  qa_vec3 velocity = has_placement ? placement.velocity : QA_Q2_ZERO;
  qa_vec3 angles = has_placement ? (qa_vec3){source_angles.x / 3, source_angles.y, source_angles.z}
                                 : (qa_vec3){0, source_angles.y, 0};
  return (qa_q2_spawn_placement){origin, angles, velocity, has_placement};
}
bool qa_q2_players_super_kill_box(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)players;
  return qa_q2_player_kill_box(entity, game);
}
qa_q2_spawn_placement qa_q2_players_select_teleport_spawn(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  return qa_q2_players_spawn_placement(players, entity, game, NULL);
}

static void clear_powerups(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_item_module_clear_powerups(players->items, entity->actor.id);
  qa_q2_base_player_state *state = qa_q2_players_get_state(players, entity->actor.id);
  qa_combat_trait_changes tc = {0};
  tc.has_invulnerable = true; tc.invulnerable = state ? state->god : false;
  qa_gameplay_authority_set_traits(game->host->combat, entity->actor, tc);
}

void qa_q2_players_super_put_in_server(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                 bool restore_loadout, const qa_q2_landmark_carry *landmark) {
  qa_q2_player_context old = qa_q2_players_context(players, entity, game);
  qa_q2_base_player_state *state = old.state;
  qa_q2_spawn_placement placement = qa_q2_players_spawn_placement(players, entity, game, landmark);
  if (restore_loadout) {
    if (game->options.mode == QA_Q2_MODE_COOP && state->has_coop_respawn) {
      qa_q2_base_player_carry carry = state->coop_respawn;
      carry.inventory = NULL;
      qa_q2_base_player_carry_set(&carry, state->coop_respawn);
      carry.score = qa_js_max(state->score, state->coop_respawn.score);
      qa_q2_players_restore_carry(players, entity, game, carry);
      qa_q2_base_player_carry_clear(&carry);
    } else if (game->options.mode == QA_Q2_MODE_DEATHMATCH ||
               (qa_gameplay_authority_read(game->host->combat, entity->actor.id).has_value
                    ? qa_gameplay_authority_read(game->host->combat, entity->actor.id).value.health : 0) <= 0) {
      if (state->use_q2_inventory) set_inventory(players, entity, game, NULL, 0);
      else set_inventory(players, entity, game, state->spawn_inventory, state->spawn_inventory_count);
      if (state->use_q2_inventory) qa_q2_item_module_configure_player(players->items, entity->actor, game, true);
      qa_gameplay_authority_set_health(game->host->combat, entity->actor, 100);
      qa_armor_state none = {0};
      none.regular.kind = QA_REGULAR_ARMOR_NONE;
      none.powered.kind = "none";
      qa_gameplay_authority_set_armor(game->host->combat, entity->actor, none);
      entity->max_health = 100;
      state->selected_item = game->options.instagib ? "q2:weapon_railgun" : "q2:weapon_blaster";
      if (state->use_q2_inventory && players->hooks.persistent_inventory_initialized)
        players->hooks.persistent_inventory_initialized(players->hooks.ctx, entity, game);
    }
  }
  state->dead = false; state->gibbed = false; state->old_water_level = 0;
  state->air_finished = game->host->now(game->host->ctx) + 12;
  state->drown_damage = 2; state->damage_alpha = 0; state->bonus_alpha = 0;
  state->damage_blood = 0; state->damage_armor = 0; state->damage_power_armor = 0; state->damage_knockback = 0;
  state->fall_time = 0; state->bob_time = 0; state->bob_move = 0;
  state->animation_priority = 0; state->animation_end = 39;
  state->spectator = state->requested_spectator;
  state->noclip = state->spectator;
  state->has_chase_target = false;
  state->old_velocity = QA_Q2_ZERO;
  state->landmark_free_fall = placement.from_landmark;
  clear_powerups(players, entity, game);
  entity->view_height = 22;
  entity->server_flags = (double)(qa_js_to_int32(entity->server_flags) & ~(1 | 2));
  entity->flags = (double)(qa_js_to_int32(entity->flags) & ~(1024 | 0x20000));
  entity->angular_velocity = QA_Q2_ZERO;
  entity->frame = 0; entity->old_frame = -1; entity->effects = 0;
  entity->render_flags = game->options.edition == QA_Q2_EDITION_RERELEASE ? 32768 : 0;
  entity->visible = !state->spectator;
  if (old.movement.animate_q2) {
    /* TS `players/${skin.split("/")[0] || "male"}/tris.md2`, stored
     * per entry because entity strings are borrowed. */
    player_entry *entry = find_entry(players, entity->actor.id);
    const char *slash = strchr(state->skin, '/');
    size_t model_length = slash ? (size_t)(slash - state->skin) : strlen(state->skin);
    if (model_length == 0) (void)snprintf(entry->model, sizeof entry->model, "players/male/tris.md2");
    else (void)snprintf(entry->model, sizeof entry->model, "players/%.*s/tris.md2", (int)model_length, state->skin);
    entity->model = entry->model;
  }
  qa_combat_trait_changes traits = {0};
  traits.has_can_take_damage = true; traits.can_take_damage = !state->spectator;
  traits.has_mass = true; traits.mass = 200;
  traits.has_invulnerable = true; traits.invulnerable = state->god;
  qa_gameplay_authority_set_traits(game->host->combat, entity->actor, traits);
  qa_q2_body_changes changes = {0};
  changes.has_origin = true; changes.origin = placement.origin;
  changes.has_velocity = true; changes.velocity = placement.velocity;
  changes.has_angles = true; changes.angles = placement.angles;
  changes.has_bounds = true; changes.bounds = old.movement.standing_bounds;
  changes.has_ground = true; changes.ground_null = true;
  game->move(game->ctx, entity, changes, false);
  game->solid(game->ctx, entity, state->spectator ? "none" : "box");
  game->motion(game->ctx, entity, "stationary");
  qa_q2_player_movement_change movement_change = {0};
  movement_change.kind = "spawn";
  movement_change.origin = placement.origin;
  movement_change.velocity = placement.velocity;
  movement_change.angles = placement.angles;
  movement_change.command_angles = old.movement.command_angles;
  movement_change.hold_milliseconds = 0;
  movement_change.spectator = state->spectator;
  players->hooks.set_movement(players->hooks.ctx, entity->actor.id, movement_change);
  if (!state->spectator) (void)qa_q2_players_kill_box_placement(players, entity, game);
  qa_q2w_state *weapon = qa_q2_players_weapon_state(players, entity->actor.id);
  if (state->use_q2_weapons && weapon) {
    const char *selected = game->options.mode == QA_Q2_MODE_DEATHMATCH ? "blaster"
        : state->has_coop_respawn && state->coop_respawn.weapon ? state->coop_respawn.weapon
        : weapon->weapon;
    if (!selected) selected = "blaster";
    qa_q2w_ballistics_reset_silencer(&players->weapons->base, entity->actor.id);
    /* TS Object.assign(weapon, new Q2WeaponState(selected)). */
    qa_q2w_state_init(weapon, selected);
  }
  if (old.movement.animate_q2) game->show(game->ctx, entity);
  game->link(game->ctx, entity);
  if (players->hooks.player_spawned) players->hooks.player_spawned(players->hooks.ctx, entity, game);
}

void qa_q2_players_super_respawn(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_base_player_state *state = qa_q2_players_context(players, entity, game).state;
  if (game->options.mode == QA_Q2_MODE_SINGLEPLAYER) {
    qa_q2_player_event event = {0};
    event.kind = "load-menu"; event.actor = entity->actor.id;
    players->hooks.emit(players->hooks.ctx, event);
    return;
  }
  if (!state->noclip) qa_q2_players_copy_to_body_queue(players, entity, game);
  qa_q2_players_put_in_server(players, entity, game, true, NULL);
  qa_body_state body = qa_q2_game_body(game, entity);
  qa_q2_player_movement movement = players->hooks.movement(players->hooks.ctx, entity->actor.id);
  qa_q2_player_movement_change change = {0};
  change.kind = "spawn";
  change.origin = body.origin;
  change.velocity = body.velocity;
  change.angles = movement.view_angles;
  change.command_angles = movement.command_angles;
  change.hold_milliseconds = 112;
  change.spectator = state->spectator;
  players->hooks.set_movement(players->hooks.ctx, entity->actor.id, change);
  state->respawn_time = game->host->now(game->host->ctx);
  state->event = "q2:player-teleport";
}

void qa_q2_players_copy_to_body_queue(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  size_t count = 0;
  qa_q2_entity **corpses = qa_q2_entities_named(game, "bodyque", &count);
  qa_q2_entity *corpse = (size_t)players->corpse_index < count ? corpses[(size_t)players->corpse_index] : NULL;
  if (!corpse) {
    free(corpses);
    qa_range_error("Q2 corpse reuse requires worldspawn's eight reserved body slots");
  }
  players->corpse_index = fmod(players->corpse_index + 1, 8);
  qa_shared_body_table_unlink(game->host->bodies, entity->actor);
  qa_shared_body_table_unlink(game->host->bodies, corpse->actor);
  corpse->model = entity->model; corpse->frame = entity->frame; corpse->skin = entity->skin;
  corpse->effects = entity->effects; corpse->render_flags = entity->render_flags;
  corpse->server_flags = entity->server_flags; corpse->clip_mask = entity->clip_mask;
  corpse->has_owner = entity->has_owner;
  if (entity->has_owner) corpse->owner = entity->owner;
  corpse->visible = entity->visible;
  qa_body_state body = qa_q2_game_body(game, entity);
  qa_q2_body_changes changes = {0};
  changes.has_origin = true; changes.origin = body.origin;
  changes.has_angles = true; changes.angles = body.angles;
  changes.has_velocity = true; changes.velocity = body.velocity;
  changes.has_bounds = true; changes.bounds = body.bounds;
  changes.has_ground = true;
  changes.ground_null = body.ground == NULL;
  if (body.ground) changes.ground = *body.ground;
  game->move(game->ctx, corpse, changes, false);
  /* Original CopyToBodyQue leaves the reserved edict's health value
   * in place. */
  if (!qa_gameplay_authority_read(game->host->combat, corpse->actor.id).has_value) {
    qa_combat_state combat = {0};
    combat.health = 0;
    combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
    combat.armor.powered.kind = "none";
    combat.can_take_damage = true; combat.invulnerable = false; combat.mass = 0; combat.team = NULL;
    qa_gameplay_authority_create_state(game->host->combat, corpse->actor, combat, NULL, NULL);
  } else {
    qa_combat_trait_changes tc = {0};
    tc.has_can_take_damage = true; tc.can_take_damage = true;
    tc.has_invulnerable = true; tc.invulnerable = false;
    qa_gameplay_authority_set_traits(game->host->combat, corpse->actor, tc);
  }
  corpse->die = (qa_q2_die){players, body_die_fn};
  game->solid(game->ctx, corpse, entity->solid);
  game->motion(game->ctx, corpse, entity->motion);
  game->show(game->ctx, corpse);
  free(corpses);
}


static void throw_client_head(qa_q2_entity *entity, qa_q2_game_services *game, double damage) {
  bool head = game->host->random(game->host->ctx) < 0.5;
  entity->model = head ? "models/objects/gibs/head2/tris.md2" : "models/objects/gibs/skull/tris.md2";
  entity->skin = head ? 1 : 0;
  entity->frame = 0; entity->effects = 2; entity->angular_velocity = QA_Q2_ZERO;
  double magnitude = damage < 50 ? 0.7 : 1.2;
  double x = (game->host->random(game->host->ctx) * 2 - 1) * 100 * magnitude;
  double y = (game->host->random(game->host->ctx) * 2 - 1) * 100 * magnitude;
  double z = (200 + game->host->random(game->host->ctx) * 100) * magnitude;
  qa_vec3 impulse = {x, y, z};
  qa_body_state body = qa_q2_game_body(game, entity);
  qa_q2_body_changes changes = {0};
  changes.has_origin = true; changes.origin = qa_q2_add(body.origin, (qa_vec3){0, 0, 32});
  changes.has_velocity = true; changes.velocity = qa_q2_add(body.velocity, impulse);
  changes.has_bounds = true; changes.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 16}};
  game->move(game->ctx, entity, changes, true);
  game->solid(game->ctx, entity, "none");
  game->motion(game->ctx, entity, "bounce");
  game->show(game->ctx, entity);
}

static void body_die_fn(void *c, qa_q2_entity *self, qa_q2_game_services *active, qa_death_reaction reaction) {
  (void)c;
  qa_combat_state_result combat = qa_gameplay_authority_read(active->host->combat, self->actor.id);
  if ((combat.has_value ? combat.value.health : 0) < -40) {
    active->sound(active->ctx, self->actor, "misc/udeath.wav", 4, 1, 1);
    for (int index = 0; index < 4; index++) (void)qa_q2m_throw_gib(self, active, "models/objects/gibs/sm_meat/tris.md2", reaction.pain.damage, (qa_q2m_gib_options){0});
    qa_q2_body_changes changes = {0};
    changes.has_origin = true;
    changes.origin = qa_q2_add(qa_q2_game_body(active, self).origin, (qa_vec3){0, 0, -48});
    active->move(active->ctx, self, changes, true);
    throw_client_head(self, active, reaction.pain.damage);
    qa_combat_trait_changes tc = {0};
    tc.has_can_take_damage = true; tc.can_take_damage = false;
    qa_gameplay_authority_set_traits(active->host->combat, self->actor, tc);
  }
}

void qa_q2_players_super_death(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction) {
  qa_q2_player_context context = qa_q2_players_context(players, entity, game);
  qa_q2_base_player_state *state = context.state;
  if (!context.movement.animate_q2) {
    (void)qa_q2_players_record_death(players, entity, game, reaction);
    return;
  }
  entity->angular_velocity = QA_Q2_ZERO;
  entity->server_flags = (double)(qa_js_to_int32(entity->server_flags) | 2);
  qa_body_state body = qa_q2_game_body(game, entity);
  qa_q2_body_changes changes = {0};
  changes.has_angles = true; changes.angles = (qa_vec3){0, body.angles.y, 0};
  changes.has_bounds = true; changes.bounds = body.bounds; changes.bounds.max.z = -8;
  game->move(game->ctx, entity, changes, false);
  game->motion(game->ctx, entity, "toss");
  qa_combat_trait_changes tc = {0};
  tc.has_can_take_damage = true; tc.can_take_damage = true;
  qa_gameplay_authority_set_traits(game->host->combat, entity->actor, tc);
  bool first = qa_q2_players_record_death(players, entity, game, reaction);
  qa_combat_state_result combat = qa_gameplay_authority_read(game->host->combat, entity->actor.id);
  double health = combat.has_value ? combat.value.health : 0;
  if (health < -40 && !state->gibbed) {
    if ((qa_js_to_int32(entity->flags) & 0x10000) == 0) {
      game->sound(game->ctx, entity->actor, "misc/udeath.wav", 4, 1, 1);
      for (int index = 0; index < 4; index++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", reaction.pain.damage, (qa_q2m_gib_options){0});
    }
    entity->flags = (double)(qa_js_to_int32(entity->flags) & ~0x10000);
    throw_client_head(entity, game, reaction.pain.damage);
    state->gibbed = true;
    tc.can_take_damage = false;
    qa_gameplay_authority_set_traits(game->host->combat, entity->actor, tc);
  } else if (first) {
    players->death_animation = fmod(players->death_animation + 1, 3);
    state->animation_priority = 5;
    entity->frame = context.movement.ducked ? 172 : players->death_animation == 0 ? 177 : players->death_animation == 1 ? 183 : 189;
    state->animation_end = context.movement.ducked ? 177 : players->death_animation == 0 ? 183 : players->death_animation == 1 ? 189 : 197;
    char path[64];
    (void)snprintf(path, sizeof path, "*death%d.wav", (int)floor(game->host->random(game->host->ctx) * 4) + 1);
    game->sound(game->ctx, entity->actor, path, 2, 1, 1);
  }
  game->link(game->ctx, entity);
  game->show(game->ctx, entity);
}

/** Campaign death bookkeeping also applies to a foreign character on this actor. */
typedef struct score_apply { qa_q2_players *players; qa_q2_entity *victim; qa_q2_game_services *game; qa_death_reaction reaction; } score_apply;
static void apply_score_cb(void *ctx, qa_q2_base_player_state *recipient, double change) {
  score_apply *apply = ctx;
  qa_q2_players_apply_score(apply->players, apply->victim, apply->game, apply->reaction, recipient, change);
}
void qa_q2_players_super_obituary(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction) {
  qa_q2_base_player_state *state = qa_q2_players_context(players, entity, game).state;
  const qa_attack_cause *cause = entity->last_attack ? &entity->last_attack->cause : NULL;
  /* entity.enemy = a player attacker other than the victim, else null (the scoreboard killer). */
  entity->has_enemy = reaction.pain.attacker != NULL && !qa_same_actor(*reaction.pain.attacker, entity->actor.id) &&
                      qa_q2_players_get_state(players, *reaction.pain.attacker) != NULL;
  if (entity->has_enemy) entity->enemy = *reaction.pain.attacker;
  qa_q2_base_player_state *attacker = reaction.pain.attacker ? qa_q2_players_get_state(players, *reaction.pain.attacker) : NULL;
  char text[512];
  score_apply apply = {players, entity, game, reaction};
  qa_q2_obituary(state, attacker, cause && cause->kind == QA_ATTACK_CAUSE_Q2 ? cause->means_of_death : 0,
                 game->options.mode == QA_Q2_MODE_DEATHMATCH, game->options.mode == QA_Q2_MODE_COOP,
                 &apply, apply_score_cb, text, sizeof text);
  qa_q2_player_event event = {0};
  event.kind = "print";
  event.has_target = false;
  event.level = "medium";
  event.text = text;
  players->hooks.emit(players->hooks.ctx, event);
}
void qa_q2_players_apply_score(qa_q2_players *players, qa_q2_entity *victim, qa_q2_game_services *game,
                               qa_death_reaction reaction, qa_q2_base_player_state *recipient, double change) {
  if (!players->hooks.score) { recipient->score += change; return; }
  const qa_attack_cause *cause = victim->last_attack ? &victim->last_attack->cause : NULL;
  qa_q2_entity *scorer = recipient == qa_q2_players_get_state(players, victim->actor.id)
      ? victim : (reaction.pain.attacker ? game->entity(game->ctx, reaction.pain.attacker) : NULL);
  if (!scorer) qa_range_error("Q2 obituary score recipient has no live source actor");
  qa_q2_entity *attacker = reaction.pain.attacker ? game->entity(game->ctx, reaction.pain.attacker) : NULL;
  players->hooks.score(players->hooks.ctx, victim, attacker, game, change,
                       cause && cause->kind == QA_ATTACK_CAUSE_Q2 ? cause->means_of_death : 0, scorer);
}
void qa_q2_players_super_clear_death_inventory(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  set_inventory(players, entity, game, NULL, 0);
}
bool qa_q2_players_super_can_drop_coop_stay_items(qa_q2_players *players, qa_q2_game_services *game) {
  (void)players; (void)game;
  return false;
}

static void toss_weapon(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (game->options.mode != QA_Q2_MODE_DEATHMATCH) return;
  qa_q2_base_player_state *state = qa_q2_players_get_state(players, entity->actor.id);
  if (!state || !state->use_q2_weapons) return;
  qa_q2w_state *weapon_state = qa_q2_players_weapon_state(players, entity->actor.id);
  const char *weapon = weapon_state ? weapon_state->weapon : NULL;
  const qa_q2w_definition *definition = weapon ? qa_q2w_weapons_definition(players->weapons, weapon) : NULL;
  qa_item_id item = definition && strcmp(definition->name, "blaster") != 0 &&
          (definition->ammo == NULL || qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, definition->ammo) != 0)
      ? definition->item : NULL;
  double quad = qa_q2_item_module_player_powerups(players->items, entity->actor.id).quad_until;
  double quad_fire = players->hooks.quad_fire_drop_until ? players->hooks.quad_fire_drop_until(players->hooks.ctx, entity->actor.id) : 0;
  bool drop_quad = (qa_js_to_int32(qa_q2_options_deathmatch_flags(&game->options)) & 16384) != 0 && quad > game->host->now(game->host->ctx) + 1;
  bool drop_quad_fire = quad_fire > game->host->now(game->host->ctx) + 1;
  double spread = item == NULL ? 0 : drop_quad ? 22.5 : drop_quad_fire ? 12.5 : 0;
  if (item) {
    qa_q2_drop_options options = {0};
    options.player_death = true; options.has_yaw_offset = true; options.yaw_offset = -spread;
    (void)qa_q2_item_module_drop(players->items, entity, game, item, options);
  }
  if (drop_quad) {
    qa_q2_drop_options options = {0};
    options.player_death = true; options.has_yaw_offset = true; options.yaw_offset = spread;
    options.has_expires_at = true; options.expires_at = quad;
    options.immediate_touch = game->options.edition == QA_Q2_EDITION_RERELEASE;
    (void)qa_q2_item_module_drop(players->items, entity, game, "q2:item_quad", options);
  }
  if (drop_quad_fire) {
    qa_q2_drop_options options = {0};
    options.player_death = true; options.has_yaw_offset = true; options.yaw_offset = spread;
    options.has_expires_at = true; options.expires_at = quad_fire;
    options.immediate_touch = true;
    (void)qa_q2_item_module_drop(players->items, entity, game, "q2:item_quadfire", options);
  }
}

bool qa_q2_players_super_record_death(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction) {
  qa_q2_base_player_state *state = qa_q2_players_context(players, entity, game).state;
  bool first = !state->dead;
  if (first) {
    state->respawn_time = game->host->now(game->host->ctx) + 1;
    entity->flags = (double)(qa_js_to_int32(entity->flags) & ~4096);
    /* TS combat.read(actor)?.armor.powered.kind !== "none": a missing state also clears. */
    qa_combat_state_result protection = qa_gameplay_authority_read(game->host->combat, entity->actor.id);
    if (!protection.has_value || strcmp(protection.value.armor.powered.kind, "none") != 0) {
      qa_powered_protection_state none = {"none", 0};
      qa_gameplay_authority_set_powered_protection(game->host->combat, entity->actor, none);
    }
    qa_body_state_result killer = {0};
    if (reaction.pain.attacker && !qa_same_actor(*reaction.pain.attacker, entity->actor.id))
      killer = qa_shared_body_table_read(game->host->bodies, *reaction.pain.attacker);
    else if (reaction.inflictor && !qa_same_actor(*reaction.inflictor, entity->actor.id))
      killer = qa_shared_body_table_read(game->host->bodies, *reaction.inflictor);
    if (killer.has_value) {
      qa_vec3 origin = qa_q2_game_body(game, entity).origin;
      state->killer_yaw = fmod(atan2(killer.value.origin.y - origin.y, killer.value.origin.x - origin.x) * 180 / M_PI + 360, 360);
    } else state->killer_yaw = qa_q2_game_body(game, entity).angles.y;
    const qa_attack_provenance *attack = entity->last_attack;
    qa_q2_players_obituary(players, entity, game, reaction);
    toss_weapon(players, entity, game);
    if (!state->use_q2_weapons && players->hooks.drop_inventory)
      players->hooks.drop_inventory(players->hooks.ctx, entity, game, attack);
    if (players->hooks.before_death_inventory)
      players->hooks.before_death_inventory(players->hooks.ctx, entity, game, attack);
    if (game->options.mode == QA_Q2_MODE_COOP && state->has_coop_respawn) {
      size_t count = 0;
      const qa_inventory_entry *current = qa_shared_inventory_table_entries(game->host->inventory, entity->actor.id, &count);
      qa_q2_base_player_carry next = state->coop_respawn;
      next.inventory = NULL;
      next.inventory_count = state->coop_respawn.inventory_count;
      if (next.inventory_count) {
        next.inventory = malloc(next.inventory_count * sizeof *next.inventory);
        if (!next.inventory) qa_range_error("Out of memory");
        for (size_t i = 0; i < next.inventory_count; i++) {
          qa_inventory_entry saved = state->coop_respawn.inventory[i];
          if (saved.item && strncmp(saved.item, "q2:key_", 7) == 0) {
            const qa_inventory_entry *found = NULL;
            for (size_t j = 0; j < count; j++) if (current[j].item && strcmp(current[j].item, saved.item) == 0) found = &current[j];
            if (found) saved = *found;
          }
          next.inventory[i] = saved;
        }
      }
      qa_q2_base_player_carry_set(&state->coop_respawn, next);
      free(next.inventory);
    }
    qa_q2_players_clear_death_inventory(players, entity, game);
    state->show_scores = game->options.mode == QA_Q2_MODE_DEATHMATCH;
    if (state->show_scores) qa_q2_players_scoreboard(players, entity, game, true);
  }
  state->dead = true;
  clear_powerups(players, entity, game);
  size_t count = 0;
  qa_q2_entity **trackers = qa_q2_entities_named(game, "pain daemon", &count);
  for (size_t i = 0; i < count; i++)
    if (trackers[i]->has_enemy && qa_same_actor(trackers[i]->enemy, entity->actor.id)) game->remove(game->ctx, trackers[i]);
  free(trackers);
  if (players->hooks.death) players->hooks.death(players->hooks.ctx, entity, game, entity->last_attack);
  return first;
}

/* Player-context adapter for the character-context modules. */
typedef qa_q2_player_character_adapter player_adapter;
static qa_body_state adapter_body(void *ctx, qa_owned_actor actor) {
  (void)actor;
  player_adapter *adapter = ctx;
  return qa_q2_game_body(adapter->game, adapter->entity);
}
static void adapter_move(void *ctx, qa_q2_entity *entity, qa_q2_body_changes changes, bool link) {
  player_adapter *adapter = ctx;
  adapter->game->move(adapter->game->ctx, entity, changes, link);
}
static void adapter_sound(void *ctx, qa_q2_entity *entity, const char *path, double channel, double volume, double attenuation) {
  player_adapter *adapter = ctx;
  adapter->game->sound(adapter->game->ctx, entity->actor, path, channel, volume, attenuation);
}
static qa_q2_player_powerups adapter_powerups(void *ctx) {
  player_adapter *adapter = ctx;
  return qa_q2_player_context_powerups(adapter->players, adapter->entity);
}
static bool adapter_weapon_state(void *ctx, qa_q2_character_weapon *out) {
  player_adapter *adapter = ctx;
  return qa_q2_player_context_weapon_state(adapter->players, adapter->entity, adapter->game, out);
}
static void adapter_environment_damage(void *ctx, double amount, double means, double flags) {
  player_adapter *adapter = ctx;
  qa_q2_player_context_environment_damage(adapter->players, adapter->entity, adapter->game, amount, means, flags);
}
qa_q2_character_context qa_q2_player_context_as_character(const qa_q2_player_context *pc,
                                                          qa_q2_player_character_adapter *adapter) {
  *adapter = (player_adapter){pc->players, pc->entity, pc->game};
  qa_q2_character_context context = {0};
  context.entity = pc->entity;
  context.state = pc->state;
  context.movement = pc->movement;
  context.rules = pc->rules;
  context.noise_ctx = pc->hooks->ctx;
  context.noise = pc->hooks->noise;
  context.cb_ctx = adapter;
  context.powerups = adapter_powerups;
  context.weapon_state = adapter_weapon_state;
  context.environment_damage = adapter_environment_damage;
  qa_q2_character_services *game = &context.game;
  game->host_ctx = pc->game->host->ctx;
  game->now = pc->game->host->now;
  game->random = pc->game->host->random;
  game->combat = pc->game->host->combat;
  game->inventory = pc->game->host->inventory;
  game->point_contents = pc->game->host->point_contents;
  game->emit = pc->game->host->emit;
  game->mode = pc->game->options.mode;
  game->deathmatch_flags = qa_q2_options_deathmatch_flags(&pc->game->options);
  game->edition = pc->game->options.edition;
  game->ctx = adapter;
  game->body = adapter_body;
  game->move = adapter_move;
  game->sound = adapter_sound;
  return context;
}

typedef struct weapon_tick { qa_q2_players *players; qa_q2_entity *entity; qa_q2_game_services *game; } weapon_tick;
static void weapon_tick_fn(void *ctx, bool latched_attack) {
  weapon_tick *tick = ctx;
  qa_q2w_input input = tick->players->hooks.weapon_input(tick->players->hooks.ctx, tick->entity->actor.id);
  input.latched_attack = latched_attack;
  qa_q2w_weapons_tick(tick->players->weapons, (qa_q2w_owner){tick->entity->actor, tick->entity->view_height}, tick->game, input);
}
static qa_q2w_turn_state turn_state_of(qa_q2_base_player_state *state) {
  return (qa_q2w_turn_state){state->buttons, state->latched_buttons, state->weapon_thunk};
}
static void turn_state_write(qa_q2_base_player_state *state, qa_q2w_turn_state turn) {
  state->buttons = turn.buttons;
  state->latched_buttons = turn.latched_buttons;
  state->weapon_thunk = turn.weapon_thunk;
}

void qa_q2_players_super_after_client_think(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_player_context context = qa_q2_players_context(players, entity, game);
  qa_q2_base_player_state *state = context.state;
  qa_q2w_turn_state turn = turn_state_of(state);
  qa_q2w_latch_buttons(&turn, context.movement.buttons);
  turn_state_write(state, turn);
  if (strcmp(players->intermission.kind, "intermission") == 0) {
    if (game->host->now(game->host->ctx) > players->intermission.started + 5 && state->buttons != 0)
      players->intermission.exit = true;
    return;
  }
  if (state->spectator) {
    if ((qa_js_to_int32(state->latched_buttons) & 1) != 0) {
      state->latched_buttons = (double)(qa_js_to_int32(state->latched_buttons) & ~1);
      qa_q2_players_chase(players, entity, game, 1, true);
    }
    if (context.movement.up_move >= 10) {
      if (!state->jump_held) {
        state->jump_held = true;
        qa_q2_players_chase(players, entity, game, 1, false);
      }
    } else state->jump_held = false;
  } else if (state->use_q2_weapons) {
    weapon_tick tick = {players, entity, game};
    turn = turn_state_of(state);
    qa_q2w_early_turn(&turn, (qa_q2w_turn_tick){&tick, weapon_tick_fn});
    turn_state_write(state, turn);
  }
  size_t count = players->entry_count;
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = players->entries[i].actor;
    qa_q2_base_player_state *watcher = players->entries[i].state;
    qa_q2_entity *observer = game->entity(game->ctx, &actor);
    if (watcher->has_chase_target && qa_same_actor(watcher->chase_target, entity->actor.id) && observer)
      qa_q2_players_update_chase(players, observer, game);
  }
}

void qa_q2_players_super_begin_frame(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (strcmp(players->intermission.kind, "playing") != 0) return;
  qa_q2_player_context context = qa_q2_players_context(players, entity, game);
  qa_q2_base_player_state *state = context.state;
  double now = game->host->now(game->host->ctx);
  if (game->options.mode == QA_Q2_MODE_DEATHMATCH && state->requested_spectator != state->spectator && now - state->respawn_time >= 5) {
    spectator_respawn_fn(players, entity, game);
    return;
  }
  weapon_tick tick = {players, entity, game};
  qa_q2w_turn_state turn = turn_state_of(state);
  qa_q2w_begin_turn(&turn, state->use_q2_weapons && !state->spectator, (qa_q2w_turn_tick){&tick, weapon_tick_fn});
  turn_state_write(state, turn);
  if (state->dead) { qa_q2_players_dead_frame(players, &context); return; }
  if (game->options.mode != QA_Q2_MODE_DEATHMATCH) {
    qa_q2_player_event event = {0};
    event.kind = "trail"; event.actor = entity->actor.id;
    event.origin = qa_q2_game_body(game, entity).origin; event.time = now;
    players->hooks.emit(players->hooks.ctx, event);
  }
  state->latched_buttons = 0;
}

void qa_q2_players_super_dead_frame(qa_q2_players *players, qa_q2_player_context *context) {
  qa_q2_base_player_state *state = context->state;
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  int32_t mask = game->options.mode == QA_Q2_MODE_DEATHMATCH ? 1 : -1;
  if (game->host->now(game->host->ctx) > state->respawn_time &&
      ((qa_js_to_int32(state->latched_buttons) & mask) != 0 ||
       (game->options.mode == QA_Q2_MODE_DEATHMATCH && (qa_js_to_int32(qa_q2_options_deathmatch_flags(&game->options)) & 1024) != 0))) {
    qa_q2_players_respawn(players, entity, game);
    state->latched_buttons = 0;
  }
}

void qa_q2_players_super_end_frame(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_player_context context = qa_q2_players_context(players, entity, game);
  qa_q2_base_player_state *state = context.state;
  double now = game->host->now(game->host->ctx);
  if (strcmp(players->intermission.kind, "playing") != 0) {
    qa_q2_player_event event = {0};
    event.kind = "view"; event.actor = entity->actor.id;
    event.view = qa_q2_players_build_view(players, &context, 0, true);
    players->hooks.emit(players->hooks.ctx, event);
    return;
  }
  qa_q2_player_powerups powers = qa_q2_item_module_player_powerups(players->items, entity->actor.id);
  qa_combat_trait_changes traits = {0};
  traits.has_invulnerable = true;
  traits.invulnerable = state->god || powers.invulnerability_until > now;
  qa_gameplay_authority_set_traits(game->host->combat, entity->actor, traits);
  qa_q2_players_world_effects(players, &context);
  qa_body_state body = qa_q2_game_body(game, entity);
  qa_angle_vectors vectors = qa_math_angle_vectors_rule(context.movement.view_angles, QA_ANGLE_RULE_NATIVE);
  double side = qa_q2_dot(body.velocity, vectors.right);
  double roll = (side < 0 ? -1 : 1) * qa_js_min(fabs(side) * players->rules.roll_angle / players->rules.roll_speed, players->rules.roll_angle);
  if (context.movement.animate_q2) {
    qa_q2_body_changes changes = {0};
    changes.has_angles = true;
    changes.angles = (qa_vec3){(context.movement.view_angles.x > 180 ? context.movement.view_angles.x - 360 : context.movement.view_angles.x) / 3,
                               context.movement.view_angles.y, roll * 4};
    game->move(game->ctx, entity, changes, false);
  }
  double speed = hypot(body.velocity.x, body.velocity.y);
  qa_q2_players_update_bob(players, &context);
  qa_q2_players_falling_damage(players, &context);
  qa_q2_damage_feedback_result feedback = qa_q2_players_damage_feedback(players, &context, players->pain_animation);
  players->pain_animation = feedback.pain_index;
  qa_q2_base_player_view view = qa_q2_players_build_view(players, &context, feedback.flashes, false);
  qa_q2_player_event view_event = {0};
  view_event.kind = "view"; view_event.actor = entity->actor.id; view_event.view = view;
  players->hooks.emit(players->hooks.ctx, view_event);
  double cycle = trunc(context.movement.ducked ? state->bob_time * 4 : state->bob_time);
  if (state->event[0] == '\0' && context.movement.grounded && speed > 225 && trunc(state->bob_time + state->bob_move) != cycle)
    state->event = "q2:footstep";
  if (state->event[0] != '\0') {
    double event = strcmp(state->event, "q2:footstep") == 0 ? 2 : strcmp(state->event, "q2:fall-short") == 0 ? 3
        : strcmp(state->event, "q2:fall") == 0 ? 4 : strcmp(state->event, "q2:fall-far") == 0 ? 5
        : strcmp(state->event, "q2:player-teleport") == 0 ? 6 : -1;
    if (event != -1) {
      qa_q2_presentation_event ev = {0};
      ev.kind = "entity-event"; ev.has_actor = true; ev.actor = entity->actor.id; ev.event = event;
      game->host->emit(game->host->ctx, ev);
    } else {
      qa_q2_presentation_event ev = {0};
      ev.kind = "effect"; ev.effect = state->event; ev.origin = body.origin;
      ev.direction = QA_Q2_ZERO; ev.count = 1; ev.color = 0;
      game->host->emit(game->host->ctx, ev);
    }
    state->event = "";
  }
  {
    player_adapter adapter;
    qa_q2_character_context cc = qa_q2_player_context_as_character(&context, &adapter);
    qa_q2_client_effects(&cc);
  }
  qa_q2_players_client_animation(players, &context);
  if (context.movement.animate_q2) game->show(game->ctx, entity);
  state->old_velocity = body.velocity;
  state->old_view_angles = view.angles;
  if (state->show_scores && (qa_js_to_int32(qa_js_round(now * 10)) & 31) == 0) qa_q2_players_scoreboard(players, entity, game, false);
}

void qa_q2_players_super_record_damage(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_damage_decision decision) {
  player_entry *entry = find_entry(players, entity->actor.id);
  if (!entry) return;
  qa_q2_base_player_state *state = entry->state;
  entity->last_attack = keep_attack(players, entity->actor.id, decision.request.attack);
  if (decision.reaction && strcmp(decision.reaction, "death") == 0) return;
  bool feedback = decision.has_feedback && decision.feedback_kind && strcmp(decision.feedback_kind, "q2") == 0;
  state->damage_blood += feedback ? decision.feedback_blood : decision.applied_damage;
  state->damage_armor += feedback ? decision.feedback_armor : 0;
  state->damage_power_armor += feedback ? decision.feedback_power_armor : 0;
  if ((feedback ? decision.feedback_power_armor : 0) > 0) state->power_armor_time = game->host->now(game->host->ctx) + 0.2;
  state->damage_knockback += feedback ? decision.feedback_knockback : decision.request.knockback;
  state->damage_from = decision.request.point;
}

void qa_q2_players_weapon_event(qa_q2_players *players, qa_q2w_event event) {
  qa_q2_base_player_state *state = event.actor ? qa_q2_players_get_state(players, *event.actor) : NULL;
  if (state && event.kind == QA_Q2W_EVENT_PLAYER_ANIMATION) {
    state->animation_priority = event.priority && strcmp(event.priority, "attack") == 0 ? 4
        : event.priority && strcmp(event.priority, "pain") == 0 ? 3 : 6;
    state->animation_end = event.last;
    player_entry *entry = find_entry(players, *event.actor);
    if (entry && entry->entity) entry->entity->frame = event.first;
  }
}

void qa_q2_players_teleport_player(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 origin, qa_vec3 angles) {
  qa_q2_player_context context = qa_q2_players_context(players, entity, game);
  context.state->event = "q2:player-teleport";
  qa_q2_player_movement_change change = {0};
  change.kind = "teleport";
  change.origin = origin;
  change.velocity = QA_Q2_ZERO;
  change.angles = angles;
  change.command_angles = context.movement.command_angles;
  change.hold_milliseconds = 160;
  change.spectator = context.state->spectator;
  players->hooks.set_movement(players->hooks.ctx, entity->actor.id, change);
}

static void spectator_respawn_fn(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_base_player_state *state = qa_q2_players_context(players, entity, game).state;
  qa_q2_connection_result result = qa_q2_players_connect(players, game, state->userinfo);
  if (!result.allowed) {
    state->requested_spectator = state->spectator;
    qa_q2_player_event print = {0};
    char text[sizeof result.reason + 1];
    (void)snprintf(text, sizeof text, "%s\n", result.reason);
    print.kind = "print"; print.has_target = true; print.target = entity->actor.id;
    print.level = "high"; print.text = text;
    players->hooks.emit(players->hooks.ctx, print);
    char stuff[64];
    (void)snprintf(stuff, sizeof stuff, "spectator %d\n", state->spectator ? 1 : 0);
    qa_q2_player_event stuff_event = {0};
    stuff_event.kind = "stufftext"; stuff_event.actor = entity->actor.id; stuff_event.text = stuff;
    players->hooks.emit(players->hooks.ctx, stuff_event);
    return;
  }
  state->score = 0;
  qa_q2_players_put_in_server(players, entity, game, true, NULL);
  state->respawn_time = game->host->now(game->host->ctx);
  char text[512];
  (void)snprintf(text, sizeof text, "%s %s\n", state->name, state->spectator ? "has moved to the sidelines" : "joined the game");
  qa_q2_player_event print = {0};
  print.kind = "print"; print.has_target = false; print.level = "high"; print.text = text;
  players->hooks.emit(players->hooks.ctx, print);
  if (!state->spectator) state->event = "q2:player-teleport";
}

void qa_q2_players_disconnect(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_base_player_state *state = qa_q2_players_context(players, entity, game).state;
  char text[512];
  (void)snprintf(text, sizeof text, "%s disconnected\n", state->name);
  qa_q2_player_event print = {0};
  print.kind = "print"; print.has_target = false; print.level = "high"; print.text = text;
  players->hooks.emit(players->hooks.ctx, print);
  size_t count = 0;
  qa_q2_entity **trackers = qa_q2_entities_named(game, "pain daemon", &count);
  for (size_t i = 0; i < count; i++)
    if (trackers[i]->has_enemy && qa_same_actor(trackers[i]->enemy, entity->actor.id)) game->remove(game->ctx, trackers[i]);
  free(trackers);
  if (players->hooks.disconnect) players->hooks.disconnect(players->hooks.ctx, entity, game);
  state->connected = false;
  entity->visible = false;
  game->solid(game->ctx, entity, "none");
  game->show(game->ctx, entity);
  qa_q2_presentation_event effect = {0};
  effect.kind = "effect"; effect.effect = "q2:logout";
  effect.origin = qa_q2_game_body(game, entity).origin;
  effect.direction = QA_Q2_ZERO; effect.count = 1; effect.color = 0;
  game->host->emit(game->host->ctx, effect);
  qa_q2_player_event userinfo = {0};
  userinfo.kind = "userinfo"; userinfo.actor = entity->actor.id;
  userinfo.slot = state->slot; userinfo.name = ""; userinfo.skin = "";
  players->hooks.emit(players->hooks.ctx, userinfo);
  remove_entry(players, entity->actor.id);
}

void qa_q2_players_scoreboard(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, bool reliable) {
  qa_q2_base_player_state **list = malloc((players->entry_count ? players->entry_count : 1) * sizeof *list);
  if (!list) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < players->entry_count; i++)
    if (players->entries[i].state->connected && !players->entries[i].state->spectator) list[count++] = players->entries[i].state;
  for (size_t i = 1; i < count; i++) {
    qa_q2_base_player_state *key = list[i];
    size_t j = i;
    while (j > 0 && (list[j - 1]->score < key->score || (list[j - 1]->score == key->score && list[j - 1]->slot > key->slot))) {
      list[j] = list[j - 1]; j--;
    }
    list[j] = key;
  }
  if (count > 12) count = 12;
  qa_q2_score_row rows[12];
  for (size_t i = 0; i < count; i++) {
    rows[i] = (qa_q2_score_row){list[i]->slot, list[i]->name, list[i]->score, qa_js_min(list[i]->ping, 999),
        trunc((game->host->now(game->host->ctx) - list[i]->entered_at) / 60), list[i]->spectator};
  }
  free(list);
  qa_q2_player_event event = {0};
  event.kind = "scoreboard";
  event.actor = entity->actor.id;
  event.rows = rows; event.row_count = count;
  event.has_killer = entity->has_enemy;
  if (entity->has_enemy) event.killer = entity->enemy;
  event.reliable = reliable;
  players->hooks.emit(players->hooks.ctx, event);
}

void qa_q2_players_chase(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, double direction, bool toggle) {
  /* TS default from = this.context(entity, game).state.chaseTarget. */
  qa_q2_base_player_state *state = qa_q2_players_context(players, entity, game).state;
  qa_actor_id from = state->chase_target;
  qa_q2_players_chase_from(players, entity, game, direction, toggle, state->has_chase_target ? &from : NULL);
}
void qa_q2_players_chase_from(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, double direction, bool toggle,
                              const qa_actor_id *from) {
  qa_q2_base_player_state *state = qa_q2_players_context(players, entity, game).state;
  if (toggle && state->has_chase_target) state->has_chase_target = false;
  else {
    /* Candidates in slot order, gathered in the fixed scratch. */
    size_t count = 0;
    qa_actor_id *candidates = players->chase_scratch;
    size_t capacity = sizeof players->chase_scratch / sizeof players->chase_scratch[0];
    for (size_t i = 0; i < players->entry_count && count < capacity; i++) {
      if (players->entries[i].state->connected && !players->entries[i].state->spectator) {
        /* Insertion sort by slot. */
        qa_actor_id actor = players->entries[i].actor;
        double slot = players->entries[i].state->slot;
        size_t j = count;
        while (j > 0) {
          qa_q2_base_player_state *other = qa_q2_players_get_state(players, candidates[j - 1]);
          if (!other || other->slot <= slot) break;
          candidates[j] = candidates[j - 1];
          j--;
        }
        candidates[j] = actor;
        count++;
      }
    }
    /* g_chase.c steps from the old target's slot, which may already be gone. */
    qa_q2_base_player_state *from_state = from != NULL ? qa_q2_players_get_state(players, *from) : NULL;
    bool has_slot = from_state != NULL;
    double slot = has_slot ? from_state->slot : 0;
    const qa_actor_id *next = NULL;
    if (has_slot) {
      if (direction == 1) {
        for (size_t i = 0; i < count && next == NULL; i++)
          if (qa_q2_players_get_state(players, candidates[i])->slot > slot) next = &candidates[i];
      } else {
        for (size_t i = count; i > 0 && next == NULL; i--)
          if (qa_q2_players_get_state(players, candidates[i - 1])->slot < slot) next = &candidates[i - 1];
      }
    }
    const qa_actor_id *wrap = count == 0 ? NULL : direction == 1 ? &candidates[0] : &candidates[count - 1];
    const qa_actor_id *chosen = has_slot && next != NULL ? next : wrap;
    state->has_chase_target = chosen != NULL;
    if (chosen != NULL) state->chase_target = *chosen;
    if (!state->has_chase_target && from == NULL) {
      qa_q2_presentation_event centerprint = {0};
      centerprint.kind = "centerprint"; centerprint.has_actor = true; centerprint.actor = entity->actor.id;
      centerprint.text = "No other players to chase.";
      game->host->emit(game->host->ctx, centerprint);
    }
  }
  qa_q2_player_event event = {0};
  event.kind = "chase"; event.actor = entity->actor.id;
  event.has_chase_target = state->has_chase_target;
  if (state->has_chase_target) event.chase_target = state->chase_target;
  players->hooks.emit(players->hooks.ctx, event);
  if (state->has_chase_target) qa_q2_players_update_chase(players, entity, game);
  else {
    qa_q2_player_movement_change change = {0};
    change.kind = "noclip"; change.enabled = true;
    players->hooks.set_movement(players->hooks.ctx, entity->actor.id, change);
  }
}

void qa_q2_players_update_chase(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_base_player_state *state = qa_q2_players_context(players, entity, game).state;
  qa_q2_entity *target = state->has_chase_target ? game->entity(game->ctx, &state->chase_target) : NULL;
  qa_q2_base_player_state *target_state = target ? qa_q2_players_get_state(players, target->actor.id) : NULL;
  if (!target || !target_state || !target_state->connected || target_state->spectator) {
    if (state->has_chase_target) {
      qa_actor_id previous = state->chase_target;
      state->has_chase_target = false;
      qa_q2_players_chase_from(players, entity, game, 1, false, &previous);
    }
    return;
  }
  qa_body_state target_body = qa_q2_game_body(game, target);
  qa_q2_player_movement movement = players->hooks.movement(players->hooks.ctx, target->actor.id);
  qa_vec3 eye = qa_q2_add(target_body.origin, (qa_vec3){0, 0, target->view_height});
  qa_vec3 clamped = movement.view_angles;
  clamped.x = qa_js_min(clamped.x, 56);
  qa_vec3 forward = qa_math_angle_vectors_rule(clamped, QA_ANGLE_RULE_NATIVE).forward;
  qa_vec3 behind = qa_q2_add(eye, qa_q2_scale(forward, -30));
  qa_vec3 desired = behind;
  desired.z = qa_js_max(behind.z, target_body.origin.z + 20) + (movement.grounded ? 0 : 16);
  qa_actor_id ignore = target->actor.id;
  qa_q2_trace_request request = {0};
  request.ignore = &ignore; request.mask = 3;
  request.start = eye; request.end = desired;
  qa_vec3 goal = qa_q2_add(game->host->trace(game->host->ctx, request).end, qa_q2_scale(forward, 2));
  request.start = goal; request.end = qa_q2_add(goal, (qa_vec3){0, 0, 6});
  qa_trace_result ceiling = game->host->trace(game->host->ctx, request);
  if (ceiling.fraction < 1) goal = qa_q2_add(ceiling.end, (qa_vec3){0, 0, -6});
  request.start = goal; request.end = qa_q2_add(goal, (qa_vec3){0, 0, -6});
  qa_trace_result floor_trace = game->host->trace(game->host->ctx, request);
  if (floor_trace.fraction < 1) goal = qa_q2_add(floor_trace.end, (qa_vec3){0, 0, 6});
  qa_vec3 angles = target_state->dead ? (qa_vec3){-15, target_state->killer_yaw, 40} : movement.view_angles;
  entity->view_height = 0;
  qa_q2_body_changes changes = {0};
  changes.has_origin = true; changes.origin = goal;
  changes.has_velocity = true; changes.velocity = QA_Q2_ZERO;
  game->move(game->ctx, entity, changes, true);
  qa_q2_player_movement_change change = {0};
  change.kind = "freeze"; change.origin = goal; change.angles = angles;
  players->hooks.set_movement(players->hooks.ctx, entity->actor.id, change);
}

void qa_q2_players_super_begin_intermission(qa_q2_players *players, qa_q2_game_services *game, const char *map, const qa_q2_landmark_carry *landmark) {
  if (strcmp(players->intermission.kind, "playing") != 0) return;
  size_t entry_count = players->entry_count;
  for (size_t i = 0; i < entry_count; i++) {
    qa_actor_id actor = players->entries[i].actor;
    qa_q2_entity *entity = game->entity(game->ctx, &actor);
    qa_combat_state_result combat = qa_gameplay_authority_read(game->host->combat, actor);
    if (entity && (combat.has_value ? combat.value.health : 0) <= 0) qa_q2_players_put_in_server(players, entity, game, true, NULL);
  }
  bool end_unit = strchr(map, '*') != NULL;
  if (end_unit && game->options.mode == QA_Q2_MODE_COOP) {
    for (size_t i = 0; i < players->entry_count; i++) {
      qa_actor_id actor = players->entries[i].actor;
      qa_owned_actor owned;
      if (!qa_session_actor_registry_resolve_owned(game->host->actors, actor, &owned)) continue;
      size_t count = 0;
      const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host->inventory, actor, &count);
      qa_inventory_entry *copy = NULL;
      if (count) {
        copy = malloc(count * sizeof *copy);
        if (!copy) qa_range_error("Out of memory");
        memcpy(copy, entries, count * sizeof *copy);
      }
      for (size_t j = 0; j < count; j++) {
        if (copy[j].item && strncmp(copy[j].item, "q2:key_", 7) == 0) {
          qa_inventory_entry zeroed = copy[j];
          zeroed.count = 0;
          qa_shared_inventory_table_configure(game->host->inventory, owned, zeroed, NULL, NULL);
        }
      }
      free(copy);
    }
  }
  qa_q2_intermission next = {.kind = "intermission", .map = map,
      .started = game->host->now(game->host->ctx),
      .exit = !end_unit && game->options.mode != QA_Q2_MODE_DEATHMATCH,
      .has_landmark = landmark != NULL};
  if (landmark) next.landmark = *landmark;
  qa_q2_players_set_intermission(players, next);
  if (players->intermission.exit) return;
  size_t authored_count = 0;
  qa_q2_entity **authored = qa_q2_entities_named(game, "info_player_intermission", &authored_count);
  qa_q2_entity *spot = NULL;
  qa_q2_entity **fallback = NULL;
  size_t fallback_count = 0;
  if (authored_count == 0) {
    fallback = qa_q2_entities_named(game, "info_player_start", &fallback_count);
    if (fallback_count > 0) spot = fallback[0];
    else {
      free(fallback);
      fallback = qa_q2_entities_named(game, "info_player_deathmatch", &fallback_count);
      if (fallback_count > 0) spot = fallback[0];
    }
  } else {
    size_t at = (size_t)((qa_js_to_int32(floor(game->host->random(game->host->ctx) * 4)) & 3) % (int64_t)authored_count);
    spot = authored[at];
  }
  if (!spot) {
    free(authored); free(fallback);
    qa_range_error("Q2 intermission has no authored camera or player spawn");
  }
  qa_body_state camera = qa_q2_game_body(game, spot);
  free(authored); free(fallback);
  for (size_t i = 0; i < players->entry_count; i++) {
    qa_actor_id actor = players->entries[i].actor;
    qa_q2_base_player_state *state = players->entries[i].state;
    qa_q2_entity *entity = game->entity(game->ctx, &actor);
    if (!entity) continue;
    state->show_scores = game->options.mode != QA_Q2_MODE_SINGLEPLAYER;
    state->loop_sound = "";
    clear_powerups(players, entity, game);
    entity->view_height = 0; entity->visible = false; entity->effects = 0;
    game->solid(game->ctx, entity, "none");
    qa_q2_body_changes changes = {0};
    changes.has_origin = true; changes.origin = camera.origin;
    changes.has_velocity = true; changes.velocity = QA_Q2_ZERO;
    game->move(game->ctx, entity, changes, true);
    game->show(game->ctx, entity);
    qa_q2_player_movement_change change = {0};
    change.kind = "freeze"; change.origin = camera.origin; change.angles = camera.angles;
    players->hooks.set_movement(players->hooks.ctx, actor, change);
    if (state->show_scores) qa_q2_players_scoreboard(players, entity, game, true);
  }
}

void qa_q2_players_super_before_exit_level(qa_q2_players *players, qa_q2_game_services *game, const char *map) {
  (void)players; (void)game; (void)map;
}

void qa_q2_players_super_check_rules(qa_q2_players *players, qa_q2_game_services *game) {
  if (strcmp(players->intermission.kind, "intermission") == 0) {
    if (!players->intermission.exit) return;
    /* Keep the map string alive past the reset to playing. */
    char *owned_map = players->intermission_map;
    players->intermission_map = NULL;
    const char *map = owned_map ? owned_map : "";
    bool has_landmark = players->intermission.has_landmark;
    qa_q2_landmark_carry landmark = players->intermission.landmark;
    if (has_landmark) {
      for (size_t i = 0; i < players->entry_count; i++) {
        qa_actor_id actor = players->entries[i].actor;
        qa_q2_entity *entity = game->entity(game->ctx, &actor);
        if (entity) qa_q2_players_end_frame(players, entity, game);
      }
    }
    players->intermission = (qa_q2_intermission){.kind = "playing"};
    if (!has_landmark) {
      for (size_t i = 0; i < players->entry_count; i++) {
        qa_actor_id actor = players->entries[i].actor;
        qa_q2_entity *entity = game->entity(game->ctx, &actor);
        if (entity) qa_q2_players_end_frame(players, entity, game);
      }
    }
    qa_q2_players_before_exit_level(players, game, map);
    for (size_t i = 0; i < players->entry_count; i++) {
      qa_actor_id actor = players->entries[i].actor;
      qa_q2_entity *entity = game->entity(game->ctx, &actor);
      if (!entity) continue;
      qa_combat_state_result combat = qa_gameplay_authority_read(game->host->combat, actor);
      double health = combat.has_value ? combat.value.health : 0;
      if (health > entity->max_health) qa_gameplay_authority_set_health(game->host->combat, entity->actor, entity->max_health);
    }
    game->host->prepare_level_change(game->host->ctx, map, has_landmark ? &landmark : NULL, game->counters->server_flags);
    const char *unit_map = map[0] == '*' ? map + 1 : map;
    const char *dollar = strchr(unit_map, '$');
    char destination[512];
    size_t destination_length = dollar ? (size_t)(dollar - unit_map) : strlen(unit_map);
    if (destination_length >= sizeof destination) destination_length = sizeof destination - 1;
    memcpy(destination, unit_map, destination_length);
    destination[destination_length] = '\0';
    if (destination[0] == '\0') qa_range_error("Q2 transition has no map destination");
    char full_map[560];
    (void)snprintf(full_map, sizeof full_map, "q2:%s", destination);
    qa_transition_intent intent = {0};
    intent.kind = "campaign-level";
    intent.campaign = game->options.campaign;
    intent.map = full_map;
    intent.spawn_point = dollar ? dollar + 1 : "";
    intent.gates = NULL; intent.gate_count = 0;
    intent.cause = NULL;
    game->host->transition(game->host->ctx, intent);
    free(owned_map);
    return;
  }
  if (game->options.mode != QA_Q2_MODE_DEATHMATCH) return;
  const char *message = "";
  if (players->rules.time_limit_minutes != 0 && game->host->now(game->host->ctx) >= players->rules.time_limit_minutes * 60)
    message = "Timelimit hit.\n";
  else if (players->rules.frag_limit != 0) {
    for (size_t i = 0; i < players->entry_count; i++)
      if (players->entries[i].state->connected && players->entries[i].state->score >= players->rules.frag_limit) {
        message = "Fraglimit hit.\n";
        break;
      }
  }
  if (message[0] != '\0') {
    qa_q2_player_event event = {0};
    event.kind = "print"; event.has_target = false; event.level = "high"; event.text = message;
    players->hooks.emit(players->hooks.ctx, event);
    qa_q2_players_end_deathmatch_level(players, game);
  }
}

void qa_q2_players_end_deathmatch_level(qa_q2_players *players, qa_q2_game_services *game) {
  const char *next = game->options.map_name;
  if ((qa_js_to_int32(qa_q2_options_deathmatch_flags(&game->options)) & 32) == 0) {
    const char *const *maps = players->rules.map_list;
    size_t count = players->rules.map_list_count;
    int64_t index = -1;
    for (size_t i = 0; i < count; i++) {
      /* TS map.toLowerCase() === mapName.toLowerCase(). */
      if (qa_js_equals_ignore_case(maps[i], game->options.map_name)) { index = (int64_t)i; break; }
    }
    if (index >= 0) {
      if (game->options.edition == QA_Q2_EDITION_RERELEASE && players->rules.map_list_shuffle && count > 1 && index == (int64_t)count - 1) {
        const char **shuffled = malloc(count * sizeof *shuffled);
        if (!shuffled) qa_range_error("Out of memory");
        for (size_t i = 0; i < count; i++) shuffled[i] = maps[i];
        for (int64_t i = (int64_t)count - 1; i > 0; i--) {
          int64_t j = game->host->rerelease_random
              ? (int64_t)qa_q2_rerelease_random_integer_max(game->host->rerelease_random, i + 1)
              : (int64_t)floor(game->host->random(game->host->ctx) * (double)(i + 1));
          const char *current = shuffled[i], *selected = shuffled[j];
          shuffled[i] = selected; shuffled[j] = current;
        }
        if (strcmp(shuffled[0], game->options.map_name) == 0) {
          const char *first = shuffled[0];
          shuffled[0] = shuffled[count - 1];
          shuffled[count - 1] = first;
        }
        free(players->owned_map_list);
        players->owned_map_list = shuffled;
        players->rules.map_list = shuffled;
        next = shuffled[0];
      } else if (game->options.edition != QA_Q2_EDITION_RERELEASE || count != 1) next = maps[(index + 1) % (int64_t)count];
    } else {
      if (players->rules.next_map[0] != '\0') next = players->rules.next_map;
      else {
        size_t change_count = 0;
        qa_q2_entity **changes = qa_q2_entities_named(game, "target_changelevel", &change_count);
        const char *authored = change_count > 0 ? qa_q2_spawn_get(&changes[0]->spawn, "map") : NULL;
        if (authored && authored[0] != '\0') next = authored;
        /* The authored value is consumed by beginIntermission. */
        qa_q2_players_begin_intermission(players, game, next, NULL);
        free(changes);
        return;
      }
    }
  }
  qa_q2_players_begin_intermission(players, game, next, NULL);
}

double qa_q2_players_need_password(qa_q2_players *players) {
  const char *password = players->rules.password;
  const char *spectator = players->rules.spectator_password;
  bool has_password = password[0] != '\0';
  bool has_spectator = spectator[0] != '\0';
  if (has_password) {
    const char *a = password, *b = "none";
    bool is_none = true;
    while (*a && *b) { if (tolower((unsigned char)*a) != *b) { is_none = false; break; } a++; b++; }
    has_password = !(is_none && *a == *b);
  }
  if (has_spectator) {
    const char *a = spectator, *b = "none";
    bool is_none = true;
    while (*a && *b) { if (tolower((unsigned char)*a) != *b) { is_none = false; break; } a++; b++; }
    has_spectator = !(is_none && *a == *b);
  }
  return (double)((has_password ? 1 : 0) | (has_spectator ? 2 : 0));
}
bool qa_q2_players_chat_allowed(qa_q2_players *players, qa_actor_id actor, qa_q2_game_services *game) {
  qa_q2_entity *entity = game->entity(game->ctx, &actor);
  if (!entity || !qa_q2_players_get_state(players, actor)) return false;
  qa_q2_player_context context = qa_q2_players_context(players, entity, game);
  return qa_q2_chat_allowed(&context);
}
bool qa_q2_players_super_client_command(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                  const char *command, const char *const *args, size_t arg_count) {
  qa_q2_player_context context = qa_q2_players_context(players, entity, game);
  return qa_q2_run_client_command(players, &context, command, args, arg_count);
}




/* TS protected view/environment bodies (Q2Players). */
void qa_q2_players_super_world_effects(qa_q2_players *players, qa_q2_player_context *context) {
  (void)players;
  player_adapter adapter;
  qa_q2_character_context cc = qa_q2_player_context_as_character(context, &adapter);
  qa_q2_world_effects(&cc);
}
void qa_q2_players_super_falling_damage(qa_q2_players *players, qa_q2_player_context *context) {
  (void)players;
  player_adapter adapter;
  qa_q2_character_context cc = qa_q2_player_context_as_character(context, &adapter);
  qa_q2_falling_damage(&cc);
}
qa_q2_damage_feedback_result qa_q2_players_super_damage_feedback(qa_q2_players *players, qa_q2_player_context *context, double pain_index) {
  (void)players;
  player_adapter adapter;
  qa_q2_character_context cc = qa_q2_player_context_as_character(context, &adapter);
  return qa_q2_damage_feedback(&cc, pain_index);
}
qa_q2_base_player_view qa_q2_players_super_build_view(qa_q2_players *players, qa_q2_player_context *context, double flashes, bool intermission) {
  (void)players;
  player_adapter adapter;
  qa_q2_character_context cc = qa_q2_player_context_as_character(context, &adapter);
  qa_q2_base_player_view view = qa_q2_build_view(&cc, flashes, intermission);
  qa_q2_base_player_state *state = context->state;
  qa_q2_game_services *game = context->game;
  if (game->options.edition != QA_Q2_EDITION_CLASSIC) return view;
  /* G_SetStats syncs the counter, ClientEndServerFrame beeps up to three times, 64 frames apart, and the icon blinks. */
  if (state->game_help_changed != game->help_changed) {
    state->game_help_changed = game->help_changed;
    state->help_changed = 1;
  }
  const int32_t frame = qa_js_to_int32(qa_js_round(game->host->now(game->host->ctx) * 10));
  if (state->help_changed > 0 && state->help_changed <= 3 && (frame & 63) == 0) {
    state->help_changed++;
    game->sound(game->ctx, context->entity->actor, "misc/pc_up.wav", 2, 1, 3);
  }
  if (state->help_changed > 0 && (frame & 8) != 0) view.help_icon = true;
  return view;
}
void qa_q2_players_super_update_bob(qa_q2_players *players, qa_q2_player_context *context) {
  (void)players;
  qa_q2_base_player_state *state = context->state;
  qa_body_state body = qa_q2_game_body(context->game, context->entity);
  double speed = hypot(body.velocity.x, body.velocity.y);
  if (speed < 5) { state->bob_move = 0; state->bob_time = 0; }
  else if (context->movement.grounded) state->bob_move = speed > 210 ? 0.25 : speed > 100 ? 0.125 : 0.0625;
  state->bob_time += state->bob_move;
}
void qa_q2_players_super_client_animation(qa_q2_players *players, qa_q2_player_context *context) {
  (void)players;
  player_adapter adapter;
  qa_q2_character_context cc = qa_q2_player_context_as_character(context, &adapter);
  qa_q2_client_animation(&cc);
}

/* TS virtual dispatch: the subclass override when it supplies one. */
#define QA_Q2_PLAYERS_OVERRIDE(name) (players->vtable && players->vtable->name)
bool qa_q2_players_spawn(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  return QA_Q2_PLAYERS_OVERRIDE(spawn) ? players->vtable->spawn(players->self, entity, game)
                                       : qa_q2_players_super_spawn(players, entity, game);
}
qa_q2_callback_definitions qa_q2_players_callbacks(qa_q2_players *players) {
  return QA_Q2_PLAYERS_OVERRIDE(callbacks) ? players->vtable->callbacks(players->self) : qa_q2_players_super_callbacks(players);
}
qa_q2_connection_result qa_q2_players_connect(qa_q2_players *players, qa_q2_game_services *game, const char *userinfo) {
  return QA_Q2_PLAYERS_OVERRIDE(connect) ? players->vtable->connect(players->self, game, userinfo, false)
                                         : qa_q2_players_super_connect(players, game, userinfo);
}
qa_q2_base_player_state *qa_q2_players_attach(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_player_admission admission) {
  return QA_Q2_PLAYERS_OVERRIDE(attach) ? players->vtable->attach(players->self, entity, game, admission)
                                        : qa_q2_players_super_attach(players, entity, game, admission);
}
void qa_q2_players_userinfo_changed(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, const char *source) {
  if (QA_Q2_PLAYERS_OVERRIDE(userinfo_changed)) players->vtable->userinfo_changed(players->self, entity, game, source);
  else qa_q2_players_super_userinfo_changed(players, entity, game, source);
}
qa_q2_base_player_carry qa_q2_players_save_carry(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  return QA_Q2_PLAYERS_OVERRIDE(save_carry) ? players->vtable->save_carry(players->self, entity, game)
                                            : qa_q2_players_super_save_carry(players, entity, game);
}
void qa_q2_players_restore_carry(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_base_player_carry carry) {
  if (QA_Q2_PLAYERS_OVERRIDE(restore_carry)) players->vtable->restore_carry(players->self, entity, game, carry);
  else qa_q2_players_super_restore_carry(players, entity, game, carry);
}
qa_q2_spawn_placement qa_q2_players_spawn_placement(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                                    const qa_q2_landmark_carry *landmark) {
  return QA_Q2_PLAYERS_OVERRIDE(spawn_placement) ? players->vtable->spawn_placement(players->self, entity, game, landmark)
                                                 : qa_q2_players_super_spawn_placement(players, entity, game, landmark);
}
bool qa_q2_players_kill_box_placement(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  return QA_Q2_PLAYERS_OVERRIDE(kill_box) ? players->vtable->kill_box(players->self, entity, game)
                                          : qa_q2_players_super_kill_box(players, entity, game);
}
void qa_q2_players_put_in_server(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                 bool restore_loadout, const qa_q2_landmark_carry *landmark) {
  if (QA_Q2_PLAYERS_OVERRIDE(put_in_server)) players->vtable->put_in_server(players->self, entity, game, restore_loadout, landmark);
  else qa_q2_players_super_put_in_server(players, entity, game, restore_loadout, landmark);
}
void qa_q2_players_respawn(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (QA_Q2_PLAYERS_OVERRIDE(respawn)) players->vtable->respawn(players->self, entity, game);
  else qa_q2_players_super_respawn(players, entity, game);
}
void qa_q2_players_death(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction) {
  if (QA_Q2_PLAYERS_OVERRIDE(death)) players->vtable->death(players->self, entity, game, reaction);
  else qa_q2_players_super_death(players, entity, game, reaction);
}
void qa_q2_players_obituary(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction) {
  if (QA_Q2_PLAYERS_OVERRIDE(obituary)) players->vtable->obituary(players->self, entity, game, reaction);
  else qa_q2_players_super_obituary(players, entity, game, reaction);
}
void qa_q2_players_clear_death_inventory(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (QA_Q2_PLAYERS_OVERRIDE(clear_death_inventory)) players->vtable->clear_death_inventory(players->self, entity, game);
  else qa_q2_players_super_clear_death_inventory(players, entity, game);
}
bool qa_q2_players_can_drop_coop_stay_items(qa_q2_players *players, qa_q2_game_services *game) {
  return QA_Q2_PLAYERS_OVERRIDE(can_drop_coop_stay_items) ? players->vtable->can_drop_coop_stay_items(players->self, game)
                                                          : qa_q2_players_super_can_drop_coop_stay_items(players, game);
}
bool qa_q2_players_record_death(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction) {
  return QA_Q2_PLAYERS_OVERRIDE(record_death) ? players->vtable->record_death(players->self, entity, game, reaction)
                                              : qa_q2_players_super_record_death(players, entity, game, reaction);
}
void qa_q2_players_after_client_think(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (QA_Q2_PLAYERS_OVERRIDE(after_client_think)) players->vtable->after_client_think(players->self, entity, game);
  else qa_q2_players_super_after_client_think(players, entity, game);
}
void qa_q2_players_begin_frame(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (QA_Q2_PLAYERS_OVERRIDE(begin_frame)) players->vtable->begin_frame(players->self, entity, game);
  else qa_q2_players_super_begin_frame(players, entity, game);
}
void qa_q2_players_dead_frame(qa_q2_players *players, qa_q2_player_context *context) {
  if (QA_Q2_PLAYERS_OVERRIDE(dead_frame)) players->vtable->dead_frame(players->self, context);
  else qa_q2_players_super_dead_frame(players, context);
}
void qa_q2_players_world_effects(qa_q2_players *players, qa_q2_player_context *context) {
  if (QA_Q2_PLAYERS_OVERRIDE(world_effects)) players->vtable->world_effects(players->self, context);
  else qa_q2_players_super_world_effects(players, context);
}
void qa_q2_players_falling_damage(qa_q2_players *players, qa_q2_player_context *context) {
  if (QA_Q2_PLAYERS_OVERRIDE(falling_damage)) players->vtable->falling_damage(players->self, context);
  else qa_q2_players_super_falling_damage(players, context);
}
qa_q2_damage_feedback_result qa_q2_players_damage_feedback(qa_q2_players *players, qa_q2_player_context *context, double pain_index) {
  return QA_Q2_PLAYERS_OVERRIDE(damage_feedback) ? players->vtable->damage_feedback(players->self, context, pain_index)
                                                 : qa_q2_players_super_damage_feedback(players, context, pain_index);
}
qa_q2_base_player_view qa_q2_players_build_view(qa_q2_players *players, qa_q2_player_context *context, double flashes, bool intermission) {
  return QA_Q2_PLAYERS_OVERRIDE(build_view) ? players->vtable->build_view(players->self, context, flashes, intermission)
                                            : qa_q2_players_super_build_view(players, context, flashes, intermission);
}
void qa_q2_players_update_bob(qa_q2_players *players, qa_q2_player_context *context) {
  if (QA_Q2_PLAYERS_OVERRIDE(update_bob)) players->vtable->update_bob(players->self, context);
  else qa_q2_players_super_update_bob(players, context);
}
void qa_q2_players_client_animation(qa_q2_players *players, qa_q2_player_context *context) {
  if (QA_Q2_PLAYERS_OVERRIDE(client_animation)) players->vtable->client_animation(players->self, context);
  else qa_q2_players_super_client_animation(players, context);
}
void qa_q2_players_end_frame(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (QA_Q2_PLAYERS_OVERRIDE(end_frame)) players->vtable->end_frame(players->self, entity, game);
  else qa_q2_players_super_end_frame(players, entity, game);
}
void qa_q2_players_record_damage(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_damage_decision decision) {
  if (QA_Q2_PLAYERS_OVERRIDE(record_damage)) players->vtable->record_damage(players->self, entity, game, decision);
  else qa_q2_players_super_record_damage(players, entity, game, decision);
}
void qa_q2_players_begin_intermission(qa_q2_players *players, qa_q2_game_services *game, const char *map, const qa_q2_landmark_carry *landmark) {
  if (QA_Q2_PLAYERS_OVERRIDE(begin_intermission)) players->vtable->begin_intermission(players->self, game, map, landmark);
  else qa_q2_players_super_begin_intermission(players, game, map, landmark);
}
void qa_q2_players_check_rules(qa_q2_players *players, qa_q2_game_services *game) {
  if (QA_Q2_PLAYERS_OVERRIDE(check_rules)) players->vtable->check_rules(players->self, game);
  else qa_q2_players_super_check_rules(players, game);
}
void qa_q2_players_before_exit_level(qa_q2_players *players, qa_q2_game_services *game, const char *map) {
  if (QA_Q2_PLAYERS_OVERRIDE(before_exit_level)) players->vtable->before_exit_level(players->self, game, map);
  else qa_q2_players_super_before_exit_level(players, game, map);
}
bool qa_q2_players_client_command(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                  const char *command, const char *const *args, size_t arg_count) {
  return QA_Q2_PLAYERS_OVERRIDE(client_command)
      ? players->vtable->client_command(players->self, entity, game, command, args, arg_count)
      : qa_q2_players_super_client_command(players, entity, game, command, args, arg_count);
}
#undef QA_Q2_PLAYERS_OVERRIDE
