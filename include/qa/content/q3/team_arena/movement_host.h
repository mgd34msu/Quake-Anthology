/* Port of src/content/q3/team-arena/movement-host.ts (types only).
 * GPL-2.0-or-later. GameEntity is the landed game/state type. */
#ifndef QA_CONTENT_Q3_TEAM_ARENA_MOVEMENT_HOST_H
#define QA_CONTENT_Q3_TEAM_ARENA_MOVEMENT_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"

typedef struct qa_q3_ta_client_movement_options {
  double trace_mask;
  bool has_fixed_msec; double fixed_msec; /* has=false is TS null */
  bool no_footsteps, gauntlet_hit;
  double debug_level;
} qa_q3_ta_client_movement_options;
typedef struct qa_q3_ta_client_movement_result {
  const qa_actor_id *contacts; size_t contact_count; /* borrowed */
  qa_bounds bounds;
  double waterlevel, watertype, xyspeed;
} qa_q3_ta_client_movement_result;
typedef struct qa_q3_ta_client_movement_host {
  void *ctx;
  qa_q3_ta_client_movement_result (*move_client)(void *ctx, qa_q3_game_entity *entity,
      qa_q3_base_user_command command, qa_q3_ta_client_movement_options options);
} qa_q3_ta_client_movement_host;
#endif
