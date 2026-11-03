/* Port of src/world/session/mod-client-input-values.ts.
 * GPL-2.0-or-later. */
#include "qa/world/session/mod_client_input_values.h"
#include "qa/foundation/js.h"

#include <stdint.h>
#include <string.h>

#include "qa/world/session/mod_client_command.h"

static double buttons_of(qa_user_command command) {
  switch (command.kind) {
    case QA_USER_COMMAND_Q1: return command.as.q1.buttons;
    case QA_USER_COMMAND_QW: return command.as.qw.buttons;
    case QA_USER_COMMAND_Q2: return command.as.q2.buttons;
    case QA_USER_COMMAND_Q2_RERELEASE: return command.as.q2_rerelease.buttons;
    case QA_USER_COMMAND_Q3: return command.as.q3.buttons;
  }
  return 0;
}
static double seconds(qa_source_time time) {
  return time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000;
}
static qa_mod_runtime_value float_value(double value) {
  qa_mod_runtime_value result;
  memset(&result, 0, sizeof result);
  result.kind = "float";
  result.number_value = value;
  return result;
}

qa_mod_client_input_values qa_mod_client_input_values_of(
    const qa_mod_client_application *application) {
  qa_user_command command = application->command;
  double buttons = buttons_of(command);
  bool jump = command.kind == QA_USER_COMMAND_Q1 ||
                      command.kind == QA_USER_COMMAND_QW
                  ? ((qa_js_to_int32(buttons) & 2) != 0)
                  : command.kind == QA_USER_COMMAND_Q2_RERELEASE
                        ? ((qa_js_to_int32(buttons) & 8) != 0)
                        : (command.kind == QA_USER_COMMAND_Q3
                               ? command.as.q3.up_move
                               : command.as.q2.up_move) >= 10;
  /* controls?.impulse ?? arsenal?.impulse ?? (q3/q2r receipt or
   * command impulse). The C application always carries the effective
   * controls impulse computed at begin(), matching the TS getter. */
  double impulse = application->controls_impulse;
  double scale = qa_mod_client_move_scale(command);
  double forward, side, up;
  switch (command.kind) {
    case QA_USER_COMMAND_Q1:
      forward = command.as.q1.forward_move;
      side = command.as.q1.side_move;
      up = command.as.q1.up_move / scale;
      break;
    case QA_USER_COMMAND_QW:
      forward = command.as.qw.forward_move;
      side = command.as.qw.side_move;
      up = command.as.qw.up_move / scale;
      break;
    case QA_USER_COMMAND_Q2:
      forward = command.as.q2.forward_move;
      side = command.as.q2.side_move;
      up = command.as.q2.up_move / scale;
      break;
    case QA_USER_COMMAND_Q2_RERELEASE:
      forward = command.as.q2_rerelease.forward_move;
      side = command.as.q2_rerelease.side_move;
      up = (qa_js_to_int32(buttons) & 8) != 0 ? 1
           : (qa_js_to_int32(buttons) & 16) != 0 ? -1 : 0;
      break;
    case QA_USER_COMMAND_Q3:
    default:
      forward = command.as.q3.forward_move;
      side = command.as.q3.right_move;
      up = command.as.q3.up_move / scale;
      break;
  }
  qa_mod_client_input_values values;
  memset(&values, 0, sizeof values);
  values.count = 10;
  values.entries[0].name = "self";
  values.entries[0].value.kind = "actor";
  values.entries[0].value.actor_value = &application->identity.actor;
  values.entries[1].name = "time";
  values.entries[1].value = float_value(seconds(application->frame.time));
  values.entries[2].name = "elapsed";
  values.entries[2].value = float_value(seconds(application->frame.elapsed));
  values.entries[3].name = "view-angles";
  values.entries[3].value.kind = "vector";
  values.entries[3].value.vector_value = application->absolute_aim;
  values.entries[4].name = "attack";
  values.entries[4].value = float_value((double)(qa_js_to_int32(buttons) & 1));
  values.entries[5].name = "jump";
  values.entries[5].value = float_value(jump ? 1 : 0);
  values.entries[6].name = "impulse";
  values.entries[6].value = float_value(impulse);
  values.entries[7].name = "forward-move";
  values.entries[7].value = float_value(forward / scale);
  values.entries[8].name = "side-move";
  values.entries[8].value = float_value(side / scale);
  values.entries[9].name = "up-move";
  values.entries[9].value = float_value(up);
  return values;
}
