/* Port of src/movement/client-outputs.ts. GPL-2.0-or-later. */
#include "qa/movement/client_outputs.h"

#include <math.h>

#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"


qa_user_command qa_client_stance_command(qa_user_command command, bool has_crouched, bool crouched) {
  if (!has_crouched) return command;
  switch (command.kind) {
    case QA_USER_COMMAND_Q2_RERELEASE:
      command.as.q2_rerelease.buttons = crouched ? (double)(qa_int32(command.as.q2_rerelease.buttons) | 16)
                                                 : (double)(qa_int32(command.as.q2_rerelease.buttons) & ~16);
      return command;
    case QA_USER_COMMAND_QW:
      command.as.qw.up_move = crouched ? -qa_js_max(1, fabs(command.as.qw.up_move)) : qa_js_max(0, command.as.qw.up_move);
      if (crouched) command.as.qw.buttons = (double)(qa_int32(command.as.qw.buttons) & ~2);
      return command;
    case QA_USER_COMMAND_Q2:
      command.as.q2.up_move = crouched ? -qa_js_max(1, fabs(command.as.q2.up_move)) : qa_js_max(0, command.as.q2.up_move);
      return command;
    case QA_USER_COMMAND_Q3:
      command.as.q3.up_move = crouched ? -qa_js_max(1, fabs(command.as.q3.up_move)) : qa_js_max(0, command.as.q3.up_move);
      return command;
    case QA_USER_COMMAND_Q1:
      qa_range_error("NetQuake has no source crouch command; authored bounds require their own qualified movement boundary");
  }
  return command;
}

double qa_client_movement_type(qa_movement_state_kind kind, qa_mod_client_movement_mode mode) {
  if (kind == QA_MOVEMENT_STATE_Q1)
    return mode == QA_MOD_CLIENT_MOVEMENT_NORMAL ? 3 : mode == QA_MOD_CLIENT_MOVEMENT_NOCLIP ? 8 : 0;
  if (kind == QA_MOVEMENT_STATE_QW) return mode == QA_MOD_CLIENT_MOVEMENT_NOCLIP ? 1 : 0;
  if (kind == QA_MOVEMENT_STATE_Q2_RERELEASE)
    return mode == QA_MOD_CLIENT_MOVEMENT_NORMAL ? 0 : mode == QA_MOD_CLIENT_MOVEMENT_NOCLIP ? 2 : 6;
  return mode == QA_MOD_CLIENT_MOVEMENT_NORMAL ? 0 : mode == QA_MOD_CLIENT_MOVEMENT_NOCLIP ? 1 : 4;
}

bool qa_client_movement_mode(const qa_mod_client_movement_outputs *outputs, double health,
                             qa_mod_client_movement_mode *out_mode) {
  /* TS health > 0 ? ... : undefined; NaN health has no mode. */
  if (!(health > 0) || outputs == NULL || !outputs->has_mode) return false;
  *out_mode = outputs->mode;
  return true;
}
