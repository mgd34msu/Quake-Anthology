/* Port of src/app/bootstrap/network/q2-remote-view.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q2_remote_view.h"
#include <math.h>
#include "qa/foundation/support.h"
#include "qa/movement/q2/types.h"
#include "qa/network/q2/adapters.h"
double qa_app_q2_remote_view_height(const qa_app_q2_remote_player *player) {
  return player->rerelease ? player->modern.movement.view_height
                           : player->classic.view.view_offset.z;
}
qa_app_q2_remote_view_position qa_app_q2_remote_view_position_at(
    const qa_app_q2_remote_player *player, qa_vec3 origin, qa_vec3 offset, double view_height) {
  qa_app_q2_remote_view_position out = {{origin.x + offset.x, origin.y + offset.y,
      origin.z + (player->rerelease ? offset.z : 0)}, view_height};
  return out;
}
qa_bounds qa_app_q2_remote_body_bounds(const qa_app_q2_remote_player *player) {
  double type = player->rerelease ? player->modern.movement.type : player->classic.movement.type;
  double flags = player->rerelease ? player->modern.movement.flags : player->classic.movement.flags;
  bool gib = type == (player->rerelease ? QA_Q2_KEX_PM_GIB : QA_Q2_PM_GIB);
  bool dead = type == (player->rerelease ? QA_Q2_KEX_PM_DEAD : QA_Q2_PM_DEAD);
  qa_bounds bounds = {{-16, -16, gib ? 0 : -24},
      {16, 16, gib ? 16 : dead || ((int64_t)flags & QA_Q2_PMF_DUCKED) != 0 ? 4 : 32}};
  return bounds;
}
qa_q2_wire_usercmd qa_app_q2_remote_command(bool command_rerelease,
    const qa_q2_user_command *classic, const qa_q2_rerelease_user_command *modern,
    const qa_app_q2_remote_player *player) {
  qa_q2_wire_usercmd wire;
  if (command_rerelease && player->rerelease) {
    qa_q2_rerelease_user_command adjusted = *modern;
    qa_vec3 delta = player->modern.movement.delta_angles;
    /* Math.fround staging points. */
    adjusted.angles.x = (float)(modern->angles.x - delta.x);
    adjusted.angles.y = (float)(modern->angles.y - delta.y);
    adjusted.angles.z = (float)(modern->angles.z - delta.z);
    qa_q2_from_command_rerelease(&adjusted, &wire);
    return wire;
  }
  if (command_rerelease || player->rerelease)
    qa_range_error("Q2 command and server movement editions differ");
  qa_q2_from_command_classic(classic, &wire);
  for (int i = 0; i < 3; i++)
    wire.angles[i] = (int16_t)(((int)wire.angles[i]
        - (int)player->classic.movement.delta_angle_shorts.values[i]) & 65535);
  return wire;
}
void qa_app_q2_rerelease_view_height_init(qa_app_q2_rerelease_view_height *state) {
  state->has_state = false; state->previous = state->current = state->changed_at = 0;
}
void qa_app_q2_rerelease_view_height_reset(qa_app_q2_rerelease_view_height *state) {
  state->has_state = false;
}
double qa_app_q2_rerelease_view_height_sample(qa_app_q2_rerelease_view_height *state,
    double height, double time_milliseconds) {
  if (!state->has_state) {
    state->has_state = true; state->previous = height; state->current = height;
    state->changed_at = time_milliseconds;
  } else if (state->current != height) {
    state->previous = state->current; state->current = height;
    state->changed_at = time_milliseconds;
  }
  double elapsed = fmax(0, fmin(time_milliseconds - state->changed_at, 100));
  return state->current + (state->previous - state->current) * (100 - elapsed) * 0.01;
}
bool qa_app_q2_rerelease_view_continuous(const qa_q2_rerelease_player_state *previous,
    const qa_q2_rerelease_player_state *current, double previous_frame,
    double current_frame, double event) {
  qa_vec3 before = previous->movement.origin, after = current->movement.origin;
  double furthest = fmax(fabs(before.x - after.x),
      fmax(fabs(before.y - after.y), fabs(before.z - after.z)));
  return current_frame == previous_frame + 1 && event != 6 && event != 7 && furthest <= 256
      && (((int64_t)previous->view.render_flags ^ (int64_t)current->view.render_flags) & 16) == 0;
}
