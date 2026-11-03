/* Port of src/content/q3/team-arena/session.ts. GPL-2.0-or-later.
 * SessionDataError (TS Error subclass) aborts via qa_range_error with the
 * same message, as every TS throw reaches the top-level crash handler. */
#ifndef QA_CONTENT_Q3_TEAM_ARENA_SESSION_H
#define QA_CONTENT_Q3_TEAM_ARENA_SESSION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/definitions.h"

/* TS SessionCvarService: get returns a borrowed string. */
typedef struct qa_q3_ta_session_cvar_service {
  void *ctx;
  const char *(*get)(void *ctx, const char *name);
  void (*set)(void *ctx, const char *name, const char *value);
} qa_q3_ta_session_cvar_service;
/* TS SessionUserinfo.valueForKey("team"): borrowed string. */
typedef struct qa_q3_ta_session_userinfo {
  void *ctx;
  const char *(*value_for_key)(void *ctx, const char *key);
} qa_q3_ta_session_userinfo;
/* TS SessionWorldState. clients are borrowed pointers (NULL = TS undefined);
 * team_scores is TS PlayerStateSlots(4). */
typedef struct qa_q3_ta_session_world_state {
  qa_q3_game_client **clients; size_t client_count;
  double max_clients;
  qa_q3_base_player_state_slots *team_scores;
  double game_type;
  bool team_auto_join;
  double max_game_clients, time, num_non_spectator_clients;
  bool new_session;
} qa_q3_ta_session_world_state;
/* TS SessionServices. broadcast_team_change old_team -1 is TS -1. */
typedef struct qa_q3_ta_session_services {
  qa_q3_ta_session_cvar_service cvars;
  void *ctx;
  void (*print)(void *ctx, const char *message);
  void (*broadcast_team_change)(void *ctx, double client_num, double old_team);
} qa_q3_ta_session_services;

double qa_q3_ta_team_count(const qa_q3_ta_session_world_state *world, double ignore_client_num, qa_q3_base_team team);
qa_q3_base_team qa_q3_ta_pick_team(const qa_q3_ta_session_world_state *world, double ignore_client_num);

/* TS GameSessionManager. world/services borrowed. */
typedef struct qa_q3_ta_game_session_manager {
  qa_q3_ta_session_world_state *world;
  qa_q3_ta_session_services services;
} qa_q3_ta_game_session_manager;
void qa_q3_ta_game_session_manager_init(qa_q3_ta_game_session_manager *manager,
    qa_q3_ta_session_world_state *world, qa_q3_ta_session_services services);
void qa_q3_ta_game_session_manager_write_client(qa_q3_ta_game_session_manager *manager, double client_num);
void qa_q3_ta_game_session_manager_read_client(qa_q3_ta_game_session_manager *manager, double client_num);
void qa_q3_ta_game_session_manager_initialize_client(qa_q3_ta_game_session_manager *manager,
    double client_num, qa_q3_ta_session_userinfo userinfo);
void qa_q3_ta_game_session_manager_initialize_world(qa_q3_ta_game_session_manager *manager);
void qa_q3_ta_game_session_manager_write_world(qa_q3_ta_game_session_manager *manager);
#endif
