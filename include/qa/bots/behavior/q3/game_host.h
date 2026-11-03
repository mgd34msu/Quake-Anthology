/*
 * Port of src/bots/behavior/q3/game-host.ts (interfaces). TS interfaces become
 * fn+ctx tables; every member receives the table's own ctx.
 * C accommodations:
 *  - TS getters that read live values (gameType, maxClients, entityCount, clock)
 *    are functions.
 *  - qa_bot_observed_entity_bots_a is returned by value. TS detaches a fresh
 *    PlayerState per call; player_state is borrowed from the game, valid until
 *    its next get_entity call. classname/player_name likewise.
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_GAME_HOST_H
#define QA_BOTS_BEHAVIOR_Q3_GAME_HOST_H
#include "qa/bots/behavior/library/weapons.h"
#include "qa/content/q3/base/game/memory.h"
#include "qa/content/q3/base/game/numeric.h"
#include "qa/content/q3/base/game/utilities.h"
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/base/world.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/pickups.h"
#include "qa/core/cvars/index.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct qa_bot_state_bots_a;
struct qa_bot_library_bots_a;

/* TS SourceBotEngine. get_userinfo returns text borrowed until its next call.
 * insert_console_command receives insert_ctx (q3BotGame replaces that member
 * alone); every other member receives ctx. */
typedef struct qa_source_bot_engine_bots_a {
  void *ctx;
  void *insert_ctx;
  void (*print)(void *ctx, const char *text);
  const char *(*get_userinfo)(void *ctx, int64_t client);
  void (*set_userinfo)(void *ctx, int64_t client, const char *userinfo);
  void (*send_server_command)(void *ctx, int64_t client, const char *command);
  void (*drop_client)(void *ctx, int64_t client, const char *reason);
  void (*insert_console_command)(void *ctx, const char *command);
  void (*append_console_command)(void *ctx, const char *command);
} qa_source_bot_engine_bots_a;

/* TS BotObservedEntity, with BotObservedPlayer flattened behind has_player. */
typedef struct qa_bot_observed_entity_bots_a {
  int64_t generation;
  bool present, linked, hidden, bot;
  qa_q3_base_entity_state state;
  qa_vec3 origin, angles;
  qa_bounds bounds;
  int64_t contents;
  bool has_inline_model;
  int64_t inline_model;
  const char *classname; /* NULL is TS null */
  int64_t event_time;
  bool has_activator_frame;
  int64_t activator_frame;
  bool proximity_trigger;
  bool has_player;
  const qa_q3_base_player_state *player_state;
  bool player_connected;
  int64_t player_team;
  const char *player_name;
  int64_t last_hurt_client, last_hurt_mod;
} qa_bot_observed_entity_bots_a;

/* TS BotWeaponKnowledge supply. */
typedef struct qa_bot_weapon_supply_bots_a {
  qa_item_id weapon;
  bool owned;
  bool has_ammo;
  qa_item_id ammo_item;
  double per_shot;
} qa_bot_weapon_supply_bots_a;
typedef struct qa_bot_weapon_knowledge_bots_a {
  qa_weapon_info_bots_a info;
  bool has_maximum_range;
  double maximum_range;
  bool melee;
  bool has_personality_role;
  int64_t personality_role;
  bool has_supply;
  qa_bot_weapon_supply_bots_a supply;
} qa_bot_weapon_knowledge_bots_a;
/* TS BotArsenalData. candidates returns a malloc'd array the caller frees. */
typedef struct qa_bot_arsenal_data_bots_a {
  void *ctx;
  void (*update_inventory)(void *ctx, struct qa_bot_state_bots_a *state);
  qa_bot_weapon_knowledge_bots_a *(*candidates)(void *ctx, struct qa_bot_library_bots_a *library,
                                                int64_t handle, size_t *out_count);
} qa_bot_arsenal_data_bots_a;
typedef struct qa_bot_weapon_tactics_bots_a {
  bool melee;
  bool has_max_range;
  double max_range;
  bool has_aim_accuracy;
  double aim_accuracy;
  bool has_aim_skill;
  double aim_skill;
  double weakness;
  bool predict_occluded_splash;
  /* Whether aim accuracy falls off at short range (machinegun, shotgun, lightning,
   * railgun in the source). */
  bool close_range_aim_penalty;
} qa_bot_weapon_tactics_bots_a;
/* TS BotArsenalKnowledge. */
typedef struct qa_bot_arsenal_interface_bots_a {
  void *ctx;
  double (*pickup_utility)(void *ctx, struct qa_bot_library_bots_a *library,
                           struct qa_bot_state_bots_a *state,
                           const qa_pickup_supply_preview *preview);
  int64_t (*choose_weapon)(void *ctx, struct qa_bot_library_bots_a *library,
                           struct qa_bot_state_bots_a *state);
  int64_t (*activation_weapon)(void *ctx, struct qa_bot_library_bots_a *library,
                               struct qa_bot_state_bots_a *state);
  qa_bot_weapon_tactics_bots_a (*tactics)(void *ctx, int64_t weapon);
  double (*aggression)(void *ctx, struct qa_bot_state_bots_a *state);
  void (*update_inventory)(void *ctx, struct qa_bot_state_bots_a *state);
  /* false is TS undefined */
  bool (*weapon_info)(void *ctx, struct qa_bot_library_bots_a *library, int64_t handle,
                      int64_t weapon, qa_weapon_info_bots_a *out);
} qa_bot_arsenal_interface_bots_a;

typedef struct qa_bot_observed_pickup_bots_a {
  qa_pickup_supply_observation observation;
  qa_pickup_supply_preview preview;
  int64_t entity;
  qa_vec3 origin;
  qa_bounds bounds;
  const char *name;
} qa_bot_observed_pickup_bots_a;
/* TS BotPickupObservations. owns_item_goal NULL is the absent member; candidates
 * is borrowed until the next call; inspect false is TS null. */
typedef struct qa_bot_pickup_observations_bots_a {
  void *ctx;
  bool (*owns_item_goal)(void *ctx, int64_t client, int64_t entity);
  const qa_bot_observed_pickup_bots_a *(*candidates)(void *ctx, int64_t client, size_t *out_count);
  bool (*inspect)(void *ctx, int64_t client, qa_actor_id actor, qa_bot_observed_pickup_bots_a *out);
} qa_bot_pickup_observations_bots_a;

/* TS SourceBotGame.random: Pick<GameRandom, "random" | "crandom">. A seeded
 * game stream (qa_source_bot_game_random_bots_a) or a host stream such as the
 * shared world's source random; both members receive ctx. */
typedef struct qa_source_bot_random_bots_a {
  void *ctx;
  double (*random)(void *ctx);
  double (*crandom)(void *ctx);
} qa_source_bot_random_bots_a;
/* GameRandom bound as the bot random (ctx is the borrowed qa_game_random). */
qa_source_bot_random_bots_a qa_source_bot_game_random_bots_a(qa_q3_game_random *random);

typedef struct qa_source_bot_game_options_bots_a {
  qa_q3_base_product product;
  qa_cvar_registry *cvars;
  qa_q3_config_string_store *configstrings;
  qa_source_bot_engine_bots_a engine;
} qa_source_bot_game_options_bots_a;

/* TS SourceBotGame. Members borrow; the table outlives every user. */
typedef struct qa_source_bot_game_bots_a {
  void *ctx;
  const qa_bot_pickup_observations_bots_a *pickups; /* NULL is TS null */
  qa_source_bot_game_options_bots_a options;
  int64_t (*game_type)(void *ctx);
  int64_t (*max_clients)(void *ctx);
  int64_t (*entity_count)(void *ctx);
  /* TS world */
  qa_q3_base_server_trace_result (*trace)(void *ctx, qa_q3_base_server_trace_query query);
  int64_t (*point_contents)(void *ctx, qa_vec3 point, int64_t pass_entity);
  qa_source_bot_random_bots_a random;
  /* TS clock */
  double (*clock_time)(void *ctx);
  double (*clock_start_time)(void *ctx);
  double (*clock_intermission_time)(void *ctx);
  qa_q3_game_memory *memory;
  const qa_bot_arsenal_interface_bots_a *knowledge;
  qa_bot_observed_entity_bots_a (*entity)(void *ctx, int64_t number);
  /* Cheap present, linked and visible test, so scans over every slot need not
   * copy absent entities. */
  bool (*observable)(void *ctx, int64_t number);
  int64_t (*model_index)(void *ctx, const char *name);
  int64_t (*choose_team)(void *ctx, int64_t client);
  void (*activate_bot)(void *ctx, int64_t client);
  void (*exit_level)(void *ctx);
  void (*reset_podium_players)(void *ctx);
  void (*client_userinfo_changed)(void *ctx, int64_t client);
  /* NULL is TS null: admitted */
  const char *(*client_connect)(void *ctx, int64_t client, bool first_time, bool is_bot);
  void (*client_begin)(void *ctx, int64_t client);
} qa_source_bot_game_bots_a;
#endif
