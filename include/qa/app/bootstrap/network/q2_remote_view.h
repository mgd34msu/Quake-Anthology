/* Port of src/app/bootstrap/network/q2-remote-view.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q2_REMOTE_VIEW_H
#define QA_APP_BOOTSTRAP_NETWORK_Q2_REMOTE_VIEW_H
#include <stdbool.h>
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
#include "qa/network/q2/state.h"
/* TS Player union discriminated by rerelease flag. */
typedef struct qa_app_q2_remote_player {
  bool rerelease;
  qa_q2_player_state classic; qa_q2_rerelease_player_state modern;
} qa_app_q2_remote_player;
double qa_app_q2_remote_view_height(const qa_app_q2_remote_player *player);
typedef struct qa_app_q2_remote_view_position {
  qa_vec3 origin; double view_height;
} qa_app_q2_remote_view_position;
qa_app_q2_remote_view_position qa_app_q2_remote_view_position_at(
    const qa_app_q2_remote_player *player, qa_vec3 origin, qa_vec3 offset, double view_height);
qa_bounds qa_app_q2_remote_body_bounds(const qa_app_q2_remote_player *player);
/* TS q2RemoteCommand: exactly one of classic/modern command is used,
 * selected by command_rerelease. Edition mismatch aborts. */
qa_q2_wire_usercmd qa_app_q2_remote_command(bool command_rerelease,
    const qa_q2_user_command *classic, const qa_q2_rerelease_user_command *modern,
    const qa_app_q2_remote_player *player);
typedef struct qa_app_q2_rerelease_view_height {
  bool has_state; double previous, current, changed_at;
} qa_app_q2_rerelease_view_height;
void qa_app_q2_rerelease_view_height_init(qa_app_q2_rerelease_view_height *state);
void qa_app_q2_rerelease_view_height_reset(qa_app_q2_rerelease_view_height *state);
double qa_app_q2_rerelease_view_height_sample(qa_app_q2_rerelease_view_height *state,
    double height, double time_milliseconds);
bool qa_app_q2_rerelease_view_continuous(const qa_q2_rerelease_player_state *previous,
    const qa_q2_rerelease_player_state *current, double previous_frame,
    double current_frame, double event);
#endif
