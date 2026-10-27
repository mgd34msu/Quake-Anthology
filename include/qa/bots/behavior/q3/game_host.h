/* Port of src/bots/behavior/q3/game-host.ts (interfaces only). GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_GAME_HOST_H
#define QA_BOTS_BEHAVIOR_Q3_GAME_HOST_H
#include "qa/contracts/math.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct qa_source_bot_engine_bots_a {
  void *ctx;
  void (*print)(void *ctx, const char *text);
  const char *(*get_userinfo)(void *ctx, int64_t client);
  void (*set_userinfo)(void *ctx, int64_t client, const char *userinfo);
  void (*send_server_command)(void *ctx, int64_t client, const char *command);
  void (*drop_client)(void *ctx, int64_t client, const char *reason);
  void (*insert_console_command)(void *ctx, const char *command);
  void (*append_console_command)(void *ctx, const char *command);
} qa_source_bot_engine_bots_a;
typedef struct qa_bot_observed_entity_bots_a {
  int64_t generation;
  bool present, linked, hidden, bot;
  qa_vec3 origin, angles;
  qa_bounds bounds;
  int64_t contents;
  bool has_inline_model;
  int64_t inline_model;
  const char *classname;
  int64_t event_time;
  bool has_activator_frame;
  int64_t activator_frame;
  bool proximity_trigger;
  bool has_player;
  bool player_connected;
  int64_t player_team;
  const char *player_name;
  int64_t last_hurt_client, last_hurt_mod;
  int64_t model_index, entity_type, weapon, state_flags, powerups;
} qa_bot_observed_entity_bots_a;
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
} qa_bot_weapon_tactics_bots_a;
typedef struct qa_source_bot_game_bots_a {
  void *ctx;
  int64_t game_type, max_clients, entity_count;
  double clock_time, clock_start_time, clock_intermission_time;
  qa_bot_observed_entity_bots_a (*get_entity)(void *ctx, int64_t number);
  int64_t (*model_index)(void *ctx, const char *name);
  int64_t (*choose_team)(void *ctx, int64_t client);
  void (*activate_bot)(void *ctx, int64_t client);
  void (*exit_level)(void *ctx);
  void (*reset_podium)(void *ctx);
  void (*client_userinfo_changed)(void *ctx, int64_t client);
  const char *(*client_connect)(void *ctx, int64_t client, bool first, bool is_bot);
  void (*client_begin)(void *ctx, int64_t client);
  double (*random_unit)(void *ctx);
  double (*crandom)(void *ctx);
  int64_t (*point_contents)(void *ctx, qa_vec3 point, int64_t pass_entity);
} qa_source_bot_game_bots_a;
#endif
