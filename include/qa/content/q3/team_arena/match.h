/* Port of src/content/q3/team-arena/match.ts. GPL-2.0-or-later.
 * ps.ping has no landed mirror field (logExit logs 0). changemap and
 * vote strings are owned char* (NULL changemap is TS null). */
#ifndef QA_CONTENT_Q3_TEAM_ARENA_MATCH_H
#define QA_CONTENT_Q3_TEAM_ARENA_MATCH_H
#include <stdbool.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/numeric.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/team_arena/client_events.h" /* spawn pose */
#include "qa/persistence/value.h"

typedef struct qa_q3_ta_vote_state {
  double time, yes, no; char *string, *display_string; double execute_time;
} qa_q3_ta_vote_state;
typedef struct qa_q3_ta_team_vote_state { double time, yes, no; char *string; } qa_q3_ta_team_vote_state;
void qa_q3_ta_vote_state_init(qa_q3_ta_vote_state *state);
void qa_q3_ta_vote_state_destroy(qa_q3_ta_vote_state *state);
void qa_q3_ta_team_vote_state_init(qa_q3_ta_team_vote_state *state);
void qa_q3_ta_team_vote_state_destroy(qa_q3_ta_team_vote_state *state);

/* TS MatchState. */
typedef struct qa_q3_ta_match_state {
  double time, start_time, warmup_time, warmup_modification_count; bool restarted;
  double num_connected_clients, num_non_spectator_clients, num_playing_clients, num_voting_clients;
  double num_team_voting_clients[2];
  double sorted_clients[QA_Q3_MAX_CLIENTS];
  double follow1, follow2;
  double intermission_time, intermission_queued;
  qa_vec3 intermission_origin, intermission_angle;
  char *changemap; bool ready_to_exit; double exit_time;
  qa_q3_ta_vote_state vote;
  qa_q3_ta_team_vote_state team_votes[2];
} qa_q3_ta_match_state;
void qa_q3_ta_match_state_init(qa_q3_ta_match_state *state);
void qa_q3_ta_match_state_destroy(qa_q3_ta_match_state *state);

/* TS MatchSettings. password borrowed. */
typedef struct qa_q3_ta_match_settings {
  double game_type, time_limit, frag_limit, capture_limit;
  double warmup_seconds, warmup_modification_count;
  const char *password; double password_modification_count;
} qa_q3_ta_match_settings;

/* TS MatchHost (services + product variant callbacks; unused variant
 * callback may be NULL). set_cvar name is one of the TS literals. */
typedef struct qa_q3_ta_match_host {
  qa_q3_base_product product;
  qa_q3_ta_match_state *state;
  qa_q3_entity_pool *pool;
  qa_q3_game_player_slots_mirror *team_scores;
  qa_q3_game_random *random;
  void *ctx;
  qa_q3_ta_match_settings (*settings)(void *ctx);
  qa_q3_ta_spawn_pose (*select_spawn_point)(void *ctx, qa_vec3 origin);
  void (*respawn)(void *ctx, qa_q3_game_entity *entity);
  void (*set_team)(void *ctx, qa_q3_game_entity *entity, const char *team); /* "f" | "s" */
  void (*stop_following)(void *ctx, qa_q3_game_entity *entity);
  void (*send_scoreboard)(void *ctx, qa_q3_game_entity *entity);
  void (*client_userinfo_changed)(void *ctx, double client_num);
  void (*write_session_data)(void *ctx);
  void (*append_console_command)(void *ctx, const char *text);
  void (*send_server_command)(void *ctx, double client_num, const char *text);
  void (*set_configstring)(void *ctx, double index, const char *text);
  void (*set_cvar)(void *ctx, const char *name, const char *value);
  void (*log)(void *ctx, const char *text);
  void (*warn)(void *ctx, const char *text);
  void (*bot_interbreed_end_match)(void *ctx);
  void (*update_tournament_info)(void *ctx);
  void (*spawn_models_on_victory_pads)(void *ctx); /* baseq3 */
  bool (*single_player)(void *ctx);                /* missionpack */
} qa_q3_ta_match_host;

/* TS MatchModuleState. */
typedef struct qa_q3_ta_match_module_state { double password_modification_count; } qa_q3_ta_match_module_state;
void qa_q3_ta_match_module_state_init(qa_q3_ta_match_module_state *state);

/* TS captureSaveState() result shape. */
typedef struct qa_q3_ta_match_save_state { double password_modification_count; } qa_q3_ta_match_save_state;

/* TS MatchRuntime. module_state NULL uses owned default storage. */
typedef struct qa_q3_ta_match_runtime {
  qa_q3_ta_match_host host;
  qa_q3_ta_match_module_state *module_state;
  qa_q3_ta_match_module_state owned_module_state;
} qa_q3_ta_match_runtime;
void qa_q3_ta_match_runtime_init(qa_q3_ta_match_runtime *runtime, qa_q3_ta_match_host host,
    qa_q3_ta_match_module_state *module_state);
qa_q3_ta_match_save_state qa_q3_ta_match_runtime_capture_save_state(qa_q3_ta_match_runtime *runtime);
void qa_q3_ta_match_runtime_restore_save_state(qa_q3_ta_match_runtime *runtime, const qa_save_value *value);
void qa_q3_ta_match_runtime_add_tournament_player(qa_q3_ta_match_runtime *runtime);
void qa_q3_ta_match_runtime_remove_tournament_loser(qa_q3_ta_match_runtime *runtime);
void qa_q3_ta_match_runtime_remove_tournament_winner(qa_q3_ta_match_runtime *runtime);
void qa_q3_ta_match_runtime_adjust_tournament_scores(qa_q3_ta_match_runtime *runtime);
double qa_q3_ta_match_runtime_sort_ranks(qa_q3_ta_match_runtime *runtime, double first, double second);
void qa_q3_ta_match_runtime_calculate_ranks(qa_q3_ta_match_runtime *runtime);
void qa_q3_ta_match_runtime_send_scoreboard_to_all(qa_q3_ta_match_runtime *runtime);
void qa_q3_ta_match_runtime_move_client_to_intermission(qa_q3_ta_match_runtime *runtime, qa_q3_game_entity *entity);
qa_q3_ta_spawn_pose qa_q3_ta_match_runtime_find_intermission_point(qa_q3_ta_match_runtime *runtime);
void qa_q3_ta_match_runtime_begin_intermission(qa_q3_ta_match_runtime *runtime);
void qa_q3_ta_match_runtime_exit_level(qa_q3_ta_match_runtime *runtime);
void qa_q3_ta_match_runtime_log_exit(qa_q3_ta_match_runtime *runtime, const char *reason);
void qa_q3_ta_match_runtime_check_intermission_exit(qa_q3_ta_match_runtime *runtime);
bool qa_q3_ta_match_runtime_score_is_tied(qa_q3_ta_match_runtime *runtime);
void qa_q3_ta_match_runtime_check_exit_rules(qa_q3_ta_match_runtime *runtime);
void qa_q3_ta_match_runtime_check_tournament(qa_q3_ta_match_runtime *runtime);
void qa_q3_ta_match_runtime_check_vote(qa_q3_ta_match_runtime *runtime);
void qa_q3_ta_match_runtime_print_team(qa_q3_ta_match_runtime *runtime, qa_q3_base_team team, const char *message);
void qa_q3_ta_match_runtime_set_leader(qa_q3_ta_match_runtime *runtime, qa_q3_base_team team, double client_num);
void qa_q3_ta_match_runtime_check_team_leader(qa_q3_ta_match_runtime *runtime, qa_q3_base_team team);
void qa_q3_ta_match_runtime_check_team_vote(qa_q3_ta_match_runtime *runtime, qa_q3_base_team team);
void qa_q3_ta_match_runtime_check_cvars(qa_q3_ta_match_runtime *runtime);
#endif
