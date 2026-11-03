/* Port of src/content/q3/team-arena/client-admission.ts. GPL-2.0-or-later.
 * ClientCapabilityError aborts via qa_range_error (TS throw reaches the
 * crash handler). DeathRuntime/MatchRuntime picks become callbacks.
 * String returns are malloc'd; connect returns NULL for TS null. */
#ifndef QA_CONTENT_Q3_TEAM_ARENA_CLIENT_ADMISSION_H
#define QA_CONTENT_Q3_TEAM_ARENA_CLIENT_ADMISSION_H
#include <stdbool.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/world.h"
#include "qa/content/q3/team_arena/match.h"
#include "qa/content/q3/team_arena/session.h"

typedef struct qa_q3_ta_client_admission_settings { double game_type; const char *password; } qa_q3_ta_client_admission_settings;

/* TS ClientBotServices union. reason borrowed (unavailable only). */
typedef enum qa_q3_ta_bot_services_kind { QA_Q3_TA_BOTS_AVAILABLE, QA_Q3_TA_BOTS_UNAVAILABLE } qa_q3_ta_bot_services_kind;
typedef struct qa_q3_ta_client_bot_services {
  qa_q3_ta_bot_services_kind kind;
  const char *reason;
  void *ctx;
  void (*remove_queued_begin)(void *ctx, double client_num);
  bool (*connect)(void *ctx, double client_num, bool restart);
  void (*shutdown_client)(void *ctx, double client_num, bool restart);
} qa_q3_ta_client_bot_services;

/* TS ClientAdmissionHost. */
typedef struct qa_q3_ta_client_admission_host {
  qa_q3_base_product product;
  qa_q3_entity_pool *pool;
  qa_q3_base_player_state_slots *team_scores;
  qa_q3_base_server_world *world; /* link_state/unlink used */
  qa_q3_ta_match_state *state;
  qa_q3_ta_game_session_manager *session;
  qa_q3_ta_client_bot_services bots;
  void *ctx;
  bool (*new_session)(void *ctx);
  void (*client_spawn)(void *ctx, qa_q3_game_entity *entity);
  void (*toss_client_items)(void *ctx, qa_q3_game_entity *entity);
  void (*toss_client_persistant_powerups)(void *ctx, qa_q3_game_entity *entity);
  void (*toss_client_cubes)(void *ctx, qa_q3_game_entity *entity);
  void (*calculate_ranks)(void *ctx);
  void (*broadcast_team_change)(void *ctx, double client_num, double old_team);
  void (*stop_following)(void *ctx, qa_q3_game_entity *entity);
  qa_q3_ta_client_admission_settings (*settings)(void *ctx);
  const char *(*get_userinfo)(void *ctx, double client_num); /* borrowed */
  void (*set_configstring)(void *ctx, double index, const char *value);
  void (*send_server_command)(void *ctx, double client_num, const char *value);
  void (*log)(void *ctx, const char *value);
  bool (*filter_packet)(void *ctx, const char *address);
} qa_q3_ta_client_admission_host;

/* Malloc'd results (NULL only on OOM) except connect's NULL = TS null. */
char *qa_q3_ta_client_info_value(const char *info, const char *wanted);
char *qa_q3_ta_clean_client_name(const char *input);
/* bot_team < 0 is TS null (pass has_bot_team false). */
char *qa_q3_ta_client_presentation_config(qa_q3_game_client *client, const char *userinfo,
    double game_type, bool has_bot_team, qa_q3_base_team bot_team);

typedef struct qa_q3_ta_client_admission_runtime { qa_q3_ta_client_admission_host host; } qa_q3_ta_client_admission_runtime;
void qa_q3_ta_client_admission_runtime_init(qa_q3_ta_client_admission_runtime *runtime, qa_q3_ta_client_admission_host host);
void qa_q3_ta_client_admission_runtime_userinfo_changed(qa_q3_ta_client_admission_runtime *runtime, double client_num);
/* Borrowed static reason string, or NULL for TS null (bot reason borrowed). */
const char *qa_q3_ta_client_admission_runtime_connect(qa_q3_ta_client_admission_runtime *runtime,
    double client_num, bool first_time, bool is_bot);
void qa_q3_ta_client_admission_runtime_begin(qa_q3_ta_client_admission_runtime *runtime, double client_num);
void qa_q3_ta_client_admission_runtime_disconnect(qa_q3_ta_client_admission_runtime *runtime, double client_num);
#endif
