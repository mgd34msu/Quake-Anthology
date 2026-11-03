/* Port of src/content/q3/team-arena/client-policy.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_TEAM_ARENA_CLIENT_POLICY_H
#define QA_CONTENT_Q3_TEAM_ARENA_CLIENT_POLICY_H
#include <stdbool.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/base/world.h"
#include "qa/content/q3/team_arena/movement_host.h"

/* TS ClientPolicyContext (extends ClientMovementHost). */
typedef struct qa_q3_ta_client_policy_context {
  qa_q3_ta_client_movement_host movement;
  qa_q3_entity_pool *pool;
  qa_q3_base_server_world *world; /* borrowed */
  double time, inactivity_seconds, follow1, follow2;
  void *ctx;
  void (*touch_triggers)(void *ctx, qa_q3_game_entity *entity);
  void (*follow_cycle)(void *ctx, qa_q3_game_entity *entity, double direction); /* 1 | -1 */
  void (*client_begin)(void *ctx, double client_num);
  void (*drop_client)(void *ctx, double client_num, const char *reason);
  void (*send_server_command)(void *ctx, double client_num, const char *text);
} qa_q3_ta_client_policy_context;

void qa_q3_ta_spectator_think(qa_q3_ta_client_policy_context *context, qa_q3_game_entity *entity,
    qa_q3_base_user_command command);
void qa_q3_ta_spectator_client_end_frame(qa_q3_ta_client_policy_context *context, qa_q3_game_entity *entity);
bool qa_q3_ta_client_inactivity_timer(qa_q3_ta_client_policy_context *context, qa_q3_game_client *client);
void qa_q3_ta_client_intermission_think(qa_q3_game_client *client);
/* TS snapshotViewSource(own, clientAt): the followed client's ps while own has
 * PMF_FOLLOW, else own. */
const qa_q3_base_player_state *qa_q3_ta_snapshot_view_source(const qa_q3_base_player_state *own,
    qa_q3_game_client *(*client_at)(void *ctx, double number), void *ctx);
#endif
