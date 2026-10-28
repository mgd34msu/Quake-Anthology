/* Port of src/content/q3/team-arena/commands.ts. GPL-2.0-or-later.
 * GameCommandError aborts via qa_range_error. DeathRuntime.playerDie and
 * the item spawnItem call are host callbacks (landed headers expose no
 * equivalents); ps.ping has no mirror field (scoreboard logs 0). */
#ifndef QA_CONTENT_Q3_TEAM_ARENA_COMMANDS_H
#define QA_CONTENT_Q3_TEAM_ARENA_COMMANDS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/item_lifecycle.h"
#include "qa/content/q3/base/game/misc.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/team_arena/client_admission.h"
#include "qa/content/q3/team_arena/client_spawn.h"
#include "qa/content/q3/team_arena/match.h"
#include "qa/content/q3/team_arena/team.h"
#include "qa/contracts/identity.h"

typedef enum qa_q3_ta_say_mode { QA_Q3_TA_SAY_ALL, QA_Q3_TA_SAY_TEAM, QA_Q3_TA_SAY_TELL } qa_q3_ta_say_mode;

/* TS CommandSettings. */
typedef struct qa_q3_ta_command_settings {
  double game_type; bool cheats, team_force_balance; double max_game_clients;
  bool dedicated, allow_vote;
} qa_q3_ta_command_settings;

/* TS GameCommandImports. get_cvar/get_userinfo return borrowed strings. */
typedef struct qa_q3_ta_game_command_imports {
  void *ctx;
  void (*send_server_command)(void *ctx, double client_num, const char *text);
  void (*set_configstring)(void *ctx, double index, const char *text);
  void (*append_console_command)(void *ctx, const char *text);
  const char *(*get_cvar)(void *ctx, const char *name);
  const char *(*get_userinfo)(void *ctx, double client_num);
  void (*set_userinfo)(void *ctx, double client_num, const char *text);
  void (*log)(void *ctx, const char *text);
  void (*print)(void *ctx, const char *text);
} qa_q3_ta_game_command_imports;

/* TS GameCommandHost. grant/give optional callbacks: NULL fn is TS
 * undefined; category is "weapons" | "ammo". */
typedef struct qa_q3_ta_game_command_host {
  qa_q3_entity_pool *pool;
  qa_q3_ta_match_state *state;
  qa_q3_game_player_slots_mirror *team_scores;
  const qa_q3_ta_command_settings *settings;
  qa_q3_ta_game_command_imports imports;
  qa_q3_ta_team_runtime *team;
  qa_q3_ta_client_spawn_runtime *spawn;
  qa_q3_ta_client_admission_runtime *admission;
  qa_q3_ta_match_runtime *match;
  qa_q3_item_lifecycle_context items;
  qa_q3_teleport_context teleport;
  void *ctx;
  bool (*grant_selected_arsenal)(void *ctx, qa_actor_id actor, const char *category);
  bool (*give_selected_item)(void *ctx, qa_actor_id actor, const char *const *args, size_t arg_count);
  void (*player_die)(void *ctx, qa_q3_game_entity *self, qa_q3_damage_inflictor inflictor,
      qa_q3_damage_participant attacker, double damage, double method_of_death);
  void (*spawn_item)(void *ctx, qa_q3_game_entity *entity, const qa_q3_base_item_definition *item, bool disabled);
} qa_q3_ta_game_command_host;

/* Malloc'd concat result (NULL only on OOM); invalid start aborts. */
char *qa_q3_ta_concat_command_args(const char *const *argv, size_t argc, double start);

typedef struct qa_q3_ta_game_command_runtime { qa_q3_ta_game_command_host host; } qa_q3_ta_game_command_runtime;
void qa_q3_ta_game_command_runtime_init(qa_q3_ta_game_command_runtime *runtime, qa_q3_ta_game_command_host host);
void qa_q3_ta_game_command_runtime_scoreboard(qa_q3_ta_game_command_runtime *runtime, qa_q3_game_entity *entity);
bool qa_q3_ta_game_command_runtime_cheats_ok(qa_q3_ta_game_command_runtime *runtime, qa_q3_game_entity *entity);
/* Returns false for TS null. */
bool qa_q3_ta_game_command_runtime_client_number_from_string(qa_q3_ta_game_command_runtime *runtime,
    qa_q3_game_entity *entity, const char *text, double *out);
void qa_q3_ta_game_command_runtime_broadcast_team_change(qa_q3_ta_game_command_runtime *runtime,
    double client_num, double old_team);
void qa_q3_ta_game_command_runtime_set_team(qa_q3_ta_game_command_runtime *runtime,
    qa_q3_game_entity *entity, const char *request);
void qa_q3_ta_game_command_runtime_stop_following(qa_q3_ta_game_command_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_ta_game_command_runtime_follow_cycle(qa_q3_ta_game_command_runtime *runtime,
    qa_q3_game_entity *entity, double direction); /* 1 | -1 */
void qa_q3_ta_game_command_runtime_say(qa_q3_ta_game_command_runtime *runtime, qa_q3_game_entity *entity,
    qa_q3_game_entity *target, qa_q3_ta_say_mode mode, const char *chat_text);
void qa_q3_ta_game_command_runtime_voice(qa_q3_ta_game_command_runtime *runtime, qa_q3_game_entity *entity,
    qa_q3_game_entity *target, qa_q3_ta_say_mode mode, const char *id, bool voice_only);
void qa_q3_ta_game_command_runtime_dispatch(qa_q3_ta_game_command_runtime *runtime,
    double client_num, const char *const *argv, size_t argc);
#endif
