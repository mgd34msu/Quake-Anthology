/* Port of src/world/session/mod-client-command.ts. GPL-2.0-or-later. */
#include "qa/world/session/mod_client_command.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
/* JS bitwise ops convert through int32. */
static double bit(double buttons, int32_t mask, bool enabled) {
  int32_t value = qa_js_to_int32(buttons);
  return (double)(enabled ? value | mask : value & ~mask);
}
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
static void set_buttons(qa_user_command *command, double buttons) {
  switch (command->kind) {
    case QA_USER_COMMAND_Q1: command->as.q1.buttons = buttons; break;
    case QA_USER_COMMAND_QW: command->as.qw.buttons = buttons; break;
    case QA_USER_COMMAND_Q2: command->as.q2.buttons = buttons; break;
    case QA_USER_COMMAND_Q2_RERELEASE:
      command->as.q2_rerelease.buttons = buttons;
      break;
    case QA_USER_COMMAND_Q3: command->as.q3.buttons = buttons; break;
  }
}
static double move_value(double value, qa_user_command command) {
  double scaled = value * qa_mod_client_move_scale(command);
  if (command.kind == QA_USER_COMMAND_Q2_RERELEASE) {
    if (!isfinite((double)(float)scaled))
      qa_range_error("Source movement output exceeds destination command ABI");
    return scaled;
  }
  double maximum = command.kind == QA_USER_COMMAND_Q3 ? 127 : 32767;
  double minimum = command.kind == QA_USER_COMMAND_Q3 ? -127 : -32768;
  if (scaled < minimum || scaled > maximum)
    qa_range_error("Source movement output exceeds destination command ABI");
  return trunc(scaled);
}

double qa_mod_client_move_scale(qa_user_command command) {
  return command.kind == QA_USER_COMMAND_Q3
             ? 127
             : command.kind == QA_USER_COMMAND_Q1 ||
                       command.kind == QA_USER_COMMAND_QW
                   ? 320
                   : 200;
}

double qa_mod_client_command_impulse(qa_user_command command) {
  if (command.kind == QA_USER_COMMAND_Q3 ||
      command.kind == QA_USER_COMMAND_Q2_RERELEASE)
    return 0;
  switch (command.kind) {
    case QA_USER_COMMAND_Q1: return command.as.q1.impulse;
    case QA_USER_COMMAND_QW: return command.as.qw.impulse;
    case QA_USER_COMMAND_Q2: return command.as.q2.impulse;
    default: return 0;
  }
}

qa_user_command qa_mod_client_command_aim(qa_user_command command,
                                          qa_vec3 aim, qa_vec3 previous) {
  if (command.kind == QA_USER_COMMAND_Q1) {
    command.as.q1.view_angles = aim;
    return command;
  }
  if (command.kind == QA_USER_COMMAND_QW) {
    qa_vec3 a = command.as.qw.angles;
    command.as.qw.angles =
        (qa_vec3){a.x + aim.x - previous.x, a.y + aim.y - previous.y,
                  a.z + aim.z - previous.z};
    return command;
  }
  if (command.kind == QA_USER_COMMAND_Q2_RERELEASE) {
    qa_vec3 a = command.as.q2_rerelease.angles;
    command.as.q2_rerelease.angles =
        (qa_vec3){a.x + aim.x - previous.x, a.y + aim.y - previous.y,
                  a.z + aim.z - previous.z};
    return command;
  }
  qa_integer_triple source = command.kind == QA_USER_COMMAND_Q3
                                 ? command.as.q3.angle_words
                                 : command.as.q2.angle_shorts;
  qa_integer_triple words = {{
      source.values[0] + trunc((aim.x - previous.x) * 65536 / 360),
      source.values[1] + trunc((aim.y - previous.y) * 65536 / 360),
      source.values[2] + trunc((aim.z - previous.z) * 65536 / 360),
  }};
  if (command.kind == QA_USER_COMMAND_Q3)
    command.as.q3.angle_words = words;
  else
    command.as.q2.angle_shorts = words;
  return command;
}

qa_user_command qa_mod_client_command_scalar(
    qa_user_command command, qa_mod_client_input input, double value) {
  if (!isfinite(value))
    qa_range_error("Source input output must be finite");
  if (str_eq(input, "attack")) {
    set_buttons(&command, bit(buttons_of(command), 1, value != 0));
    return command;
  }
  if (str_eq(input, "jump")) {
    if (command.kind == QA_USER_COMMAND_Q1 ||
        command.kind == QA_USER_COMMAND_QW) {
      set_buttons(&command, bit(buttons_of(command), 2, value != 0));
      return command;
    }
    if (command.kind == QA_USER_COMMAND_Q2_RERELEASE) {
      set_buttons(&command, bit(buttons_of(command), 8, value != 0));
      return command;
    }
    double scale = qa_mod_client_move_scale(command);
    if (command.kind == QA_USER_COMMAND_Q3) {
      double up = command.as.q3.up_move;
      command.as.q3.up_move =
          value == 0 ? qa_js_min(0, up) : qa_js_max(10, qa_js_max(up, scale));
    } else {
      double up = command.as.q2.up_move;
      command.as.q2.up_move =
          value == 0 ? qa_js_min(0, up) : qa_js_max(10, qa_js_max(up, scale));
    }
    return command;
  }
  if (str_eq(input, "impulse")) {
    if (trunc(value) != value || value < 0 || value > 255)
      qa_range_error("Source impulse output must fit one byte");
    if (command.kind == QA_USER_COMMAND_Q3 ||
        command.kind == QA_USER_COMMAND_Q2_RERELEASE)
      return command;
    switch (command.kind) {
      case QA_USER_COMMAND_Q1: command.as.q1.impulse = value; break;
      case QA_USER_COMMAND_QW: command.as.qw.impulse = value; break;
      case QA_USER_COMMAND_Q2: command.as.q2.impulse = value; break;
      default: break;
    }
    return command;
  }
  if (str_eq(input, "forward-move")) {
    double moved = move_value(value, command);
    switch (command.kind) {
      case QA_USER_COMMAND_Q1: command.as.q1.forward_move = moved; break;
      case QA_USER_COMMAND_QW: command.as.qw.forward_move = moved; break;
      case QA_USER_COMMAND_Q2: command.as.q2.forward_move = moved; break;
      case QA_USER_COMMAND_Q2_RERELEASE:
        command.as.q2_rerelease.forward_move = moved;
        break;
      case QA_USER_COMMAND_Q3: command.as.q3.forward_move = moved; break;
    }
    return command;
  }
  if (str_eq(input, "side-move")) {
    double moved = move_value(value, command);
    if (command.kind == QA_USER_COMMAND_Q3) {
      command.as.q3.right_move = moved;
      return command;
    }
    switch (command.kind) {
      case QA_USER_COMMAND_Q1: command.as.q1.side_move = moved; break;
      case QA_USER_COMMAND_QW: command.as.qw.side_move = moved; break;
      case QA_USER_COMMAND_Q2: command.as.q2.side_move = moved; break;
      case QA_USER_COMMAND_Q2_RERELEASE:
        command.as.q2_rerelease.side_move = moved;
        break;
      default: break;
    }
    return command;
  }
  /* "up-move" */
  if (command.kind == QA_USER_COMMAND_Q2_RERELEASE) {
    int32_t buttons = qa_js_to_int32(buttons_of(command));
    set_buttons(&command,
                (double)((buttons & ~(8 | 16)) |
                         (value > 0 ? 8 : value < 0 ? 16 : 0)));
    return command;
  }
  double moved = move_value(value, command);
  switch (command.kind) {
    case QA_USER_COMMAND_Q1: command.as.q1.up_move = moved; break;
    case QA_USER_COMMAND_QW: command.as.qw.up_move = moved; break;
    case QA_USER_COMMAND_Q2: command.as.q2.up_move = moved; break;
    case QA_USER_COMMAND_Q3: command.as.q3.up_move = moved; break;
    default: break;
  }
  return command;
}

size_t qa_mod_client_output_change_count(qa_mod_client_input_output output) {
  return output.kind == QA_MOD_CLIENT_INPUT_OUTPUT_CONSUME
             ? output.input_count
             : 1;
}
qa_mod_client_input_output qa_mod_client_output_change_at(
    qa_mod_client_input_output output, size_t index) {
  if (output.kind != QA_MOD_CLIENT_INPUT_OUTPUT_CONSUME) return output;
  qa_mod_client_input_output change;
  memset(&change, 0, sizeof change);
  change.kind = QA_MOD_CLIENT_INPUT_OUTPUT_SET;
  change.input = output.inputs[index];
  change.number_value = 0;
  return change;
}
