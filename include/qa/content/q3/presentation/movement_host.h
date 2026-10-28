/* Port of src/content/q3/presentation/movement-host.ts. GPL-2.0-or-later.
 * SourcePlayerState / UserCommand are the landed base/shared records
 * (qa/content/q3/base/shared/player_state.h); shared/ has landed, so the
 * old base/game/state.h mirrors are replaced. */
#ifndef QA_CONTENT_Q3_PRESENTATION_MOVEMENT_HOST_H
#define QA_CONTENT_Q3_PRESENTATION_MOVEMENT_HOST_H
#include <stdbool.h>
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/collision_host.h"
#include "qa/contracts/math.h"
typedef qa_q3_base_source_player_state qa_q3_pres_source_player_state;
typedef qa_q3_base_user_command qa_q3_pres_user_command;
/* TS MovementTrace extends TraceResult { readonly entityNum }. */
typedef struct qa_q3_pres_movement_trace {
  qa_q3_trace_result result;
  double entity_num;
} qa_q3_pres_movement_trace;
typedef struct qa_q3_pres_movement_options {
  bool has_original_server_time;
  double original_server_time;
  void *trace_ctx;
  qa_q3_pres_movement_trace (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds, double skip_number, double mask);
  double (*point_contents)(void *ctx, qa_vec3 point, double pass_entity);
  double trace_mask;
  bool has_fixed_msec; /* false is TS null */
  double fixed_msec;
  bool no_footsteps;
  bool gauntlet_hit;
} qa_q3_pres_movement_options;
/* Prediction invokes the recipe's selected movement, arsenal and character
 * owners together. command_timing is "q3" | "provider". */
typedef struct qa_q3_pres_movement_host {
  const char *command_timing;
  void *ctx;
  qa_bounds (*move_player)(void *ctx, qa_q3_pres_source_player_state *state, qa_q3_pres_user_command command,
                           qa_q3_pres_movement_options options);
  void (*update_view_angles)(void *ctx, qa_q3_pres_source_player_state *state, qa_q3_pres_user_command command);
} qa_q3_pres_movement_host;
#endif
