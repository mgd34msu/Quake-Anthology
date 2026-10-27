/* Port of src/input/bindings.ts. GPL-2.0-or-later. */
#include "qa/input/bindings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/input/key_codes.h"
#include "qa/input/keys.h"
#include "qa/input/mouse_buttons.h"
#include "qa/input/support.h"

typedef struct controller_button {
  const char *command;
  double button;
  const char *label;
} controller_button;

static const controller_button controller_buttons[] = {
    {"gamepad_a_button", 0, "A"}, {"gamepad_b_button", 1, "B"},
    {"gamepad_x_button", 2, "X"}, {"gamepad_y_button", 3, "Y"},
    {"gamepad_back", 4, "Back"}, {"gamepad_guide", 5, "Guide"},
    {"gamepad_start", 6, "Start"}, {"gamepad_left_stick", 7, "Left stick press"},
    {"gamepad_right_stick", 8, "Right stick press"},
    {"gamepad_left_shoulder", 9, "Left shoulder"},
    {"gamepad_right_shoulder", 10, "Right shoulder"},
    {"gamepad_dpad_up", 11, "D-pad up"}, {"gamepad_dpad_down", 12, "D-pad down"},
    {"gamepad_dpad_left", 13, "D-pad left"},
    {"gamepad_dpad_right", 14, "D-pad right"}, {"gamepad_misc", 15, "Miscellaneous"},
    {"gamepad_paddle1", 16, "Paddle 1"}, {"gamepad_paddle2", 17, "Paddle 2"},
    {"gamepad_paddle3", 18, "Paddle 3"}, {"gamepad_paddle4", 19, "Paddle 4"},
    {"gamepad_touchpad", 20, "Touchpad"}};

static char *lower_copy(const char *text) {
  char *out = qa_input_dup(text);
  if (out == NULL) return NULL;
  for (char *at = out; *at != '\0'; at++) {
    if (*at >= 'A' && *at <= 'Z') *at = (char)(*at + 32);
  }
  return out;
}

bool qa_named_physical_input(const char *name, double device,
                             qa_physical_input *out) {
  char *lower = lower_copy(name);
  if (lower == NULL) return false;
  memset(out, 0, sizeof *out);
  bool found = false;
  /* /^mouse([1-9][0-9]*)$/ */
  if (strncmp(lower, "mouse", 5) == 0 && lower[5] >= '1' && lower[5] <= '9') {
    bool digits = true;
    for (const char *at = lower + 6; *at != '\0'; at++) {
      if (*at < '0' || *at > '9') digits = false;
    }
    if (digits) {
      out->kind = QA_PHYSICAL_INPUT_MOUSE_BUTTON;
      out->button = qa_physical_mouse_button(qa_input_js_parse_number(lower + 5));
      found = true;
    }
  }
  if (!found) {
    char controller_name[256];
    if (strncmp(lower, "gamepad_", 8) == 0) {
      snprintf(controller_name, sizeof controller_name, "%s", lower);
    } else {
      snprintf(controller_name, sizeof controller_name, "gamepad_%s", lower);
    }
    for (size_t i = 0; i < sizeof controller_buttons / sizeof controller_buttons[0];
         i++) {
      if (strcmp(controller_buttons[i].command, controller_name) == 0) {
        out->kind = QA_PHYSICAL_INPUT_CONTROLLER_BUTTON;
        out->device = device;
        out->button = controller_buttons[i].button;
        found = true;
      }
    }
    if (!found && (strcmp(controller_name, "gamepad_left_trigger") == 0 ||
                   strcmp(controller_name, "gamepad_right_trigger") == 0)) {
      out->kind = QA_PHYSICAL_INPUT_CONTROLLER_AXIS;
      out->device = device;
      out->axis = strcmp(controller_name, "gamepad_left_trigger") == 0
                      ? "left-trigger"
                      : "right-trigger";
      out->direction = "positive";
      found = true;
    }
  }
  if (!found) {
    double code = qa_string_to_keynum(lower);
    if (code >= 0) {
      out->kind = QA_PHYSICAL_INPUT_KEY;
      out->code = code;
      found = true;
    }
  }
  free(lower);
  return found;
}

char *qa_physical_input_name(const qa_physical_input *input) {
  char buffer[256];
  switch (input->kind) {
    case QA_PHYSICAL_INPUT_KEY:
      return qa_keynum_to_string(input->code);
    case QA_PHYSICAL_INPUT_MOUSE_BUTTON: {
      char *button = qa_input_js_number(qa_quake_mouse_button(input->button));
      if (button == NULL) return NULL;
      snprintf(buffer, sizeof buffer, "MOUSE%s", button);
      free(button);
      return qa_input_dup(buffer);
    }
    case QA_PHYSICAL_INPUT_CONTROLLER_BUTTON: {
      const char *label = NULL;
      for (size_t i = 0; i < sizeof controller_buttons / sizeof controller_buttons[0];
           i++) {
        if (controller_buttons[i].button == input->button) {
          label = controller_buttons[i].label;
        }
      }
      char *device = qa_input_js_number(input->device + 1);
      if (device == NULL) return NULL;
      if (label != NULL) {
        snprintf(buffer, sizeof buffer, "Pad %s %s", device, label);
      } else {
        char *number = qa_input_js_number(input->button + 1);
        snprintf(buffer, sizeof buffer, "Pad %s Button %s", device,
                 number != NULL ? number : "");
        free(number);
      }
      free(device);
      return qa_input_dup(buffer);
    }
    case QA_PHYSICAL_INPUT_CONTROLLER_AXIS: {
      char *device = qa_input_js_number(input->device + 1);
      if (device == NULL) return NULL;
      bool positive = input->direction != NULL &&
                      strcmp(input->direction, "positive") == 0;
      const char *axis = input->axis != NULL ? input->axis : "";
      if (strcmp(axis, "left-x") == 0) {
        snprintf(buffer, sizeof buffer, "Pad %s Left stick %s", device,
                 positive ? "right" : "left");
      } else if (strcmp(axis, "left-y") == 0) {
        snprintf(buffer, sizeof buffer, "Pad %s Left stick %s", device,
                 positive ? "down" : "up");
      } else if (strcmp(axis, "right-x") == 0) {
        snprintf(buffer, sizeof buffer, "Pad %s Right stick %s", device,
                 positive ? "right" : "left");
      } else if (strcmp(axis, "right-y") == 0) {
        snprintf(buffer, sizeof buffer, "Pad %s Right stick %s", device,
                 positive ? "down" : "up");
      } else if (strcmp(axis, "left-trigger") == 0) {
        snprintf(buffer, sizeof buffer, "Pad %s Left trigger%s", device,
                 positive ? "" : " released");
      } else {
        snprintf(buffer, sizeof buffer, "Pad %s Right trigger%s", device,
                 positive ? "" : " released");
      }
      free(device);
      return qa_input_dup(buffer);
    }
  }
  return NULL;
}

qa_input_binding *qa_default_bindings(double device, qa_command_dialect dialect,
                                      const qa_weapon_binding_item *items,
                                      size_t item_count, size_t *out_count) {
  bool q1 = dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE ||
            dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
  const struct {
    const char *name;
    const char *text;
  } defaults[] = {
      {"w", "+forward"}, {"s", "+back"}, {"a", "+moveleft"}, {"d", "+moveright"},
      {"SPACE", q1 ? "+jump" : "+moveup"}, {"CTRL", "+movedown"},
      {"SHIFT", "+speed"}, {"MOUSE1", "+attack"}, {"TAB", "+scores"},
      {"MWHEELUP", "weapprev"}, {"MWHEELDOWN", "weapnext"}, {"q", "+weaponwheel"},
      {"GAMEPAD_RIGHT_TRIGGER", "+attack"},
      {"GAMEPAD_A_BUTTON", q1 ? "+jump" : "+moveup"},
      {"GAMEPAD_B_BUTTON", "+movedown"}, {"GAMEPAD_X_BUTTON", "+use"},
      {"GAMEPAD_LEFT_SHOULDER", "weapprev"}, {"GAMEPAD_RIGHT_SHOULDER", "weapnext"},
      {"GAMEPAD_BACK", "+scores"}};
  size_t fixed = sizeof defaults / sizeof defaults[0];
  size_t weapon_count = 0;
  qa_weapon_key_binding *weapons =
      qa_default_weapon_bindings(items, item_count, &weapon_count);
  size_t capacity = fixed + weapon_count;
  qa_input_binding *bindings =
      (qa_input_binding *)calloc(capacity != 0 ? capacity : 1, sizeof *bindings);
  if (bindings == NULL) {
    qa_weapon_key_bindings_free(weapons, weapon_count);
    *out_count = 0;
    return NULL;
  }
  size_t count = 0;
  bool ok = true;
  for (size_t i = 0; i < fixed + weapon_count && ok; i++) {
    const char *name = i < fixed ? defaults[i].name : weapons[i - fixed].key;
    const char *text = i < fixed ? defaults[i].text : weapons[i - fixed].command;
    qa_physical_input physical;
    if (!qa_named_physical_input(name, device, &physical)) continue;
    qa_input_binding binding;
    memset(&binding, 0, sizeof binding);
    binding.input = physical;
    binding.target.is_action = false;
    binding.target.text = text;
    ok = qa_input_binding_copy(&bindings[count], &binding);
    if (ok) count++;
  }
  qa_weapon_key_bindings_free(weapons, weapon_count);
  if (!ok) {
    for (size_t i = 0; i < count; i++) qa_input_binding_dispose(&bindings[i]);
    free(bindings);
    *out_count = 0;
    return NULL;
  }
  *out_count = count;
  return bindings;
}

static char *quoted(const char *value) {
  if (strchr(value, '"') != NULL || strchr(value, '\r') != NULL ||
      strchr(value, '\n') != NULL) {
    qa_input_error(
        "Source cfg cannot represent a binding containing quotes or newlines; "
        "use the seat settings document");
  }
  size_t length = strlen(value) + 3;
  char *out = (char *)malloc(length);
  if (out != NULL) snprintf(out, length, "\"%s\"", value);
  return out;
}

char **qa_archived_bindings(const qa_input_binding *bindings, size_t count,
                            bool include_controller_bindings, size_t *out_count) {
  char **commands = (char **)calloc(count + 1, sizeof *commands);
  if (commands == NULL) {
    *out_count = 0;
    return NULL;
  }
  commands[0] = qa_input_dup("unbindall");
  size_t written = 1;
  for (size_t i = 0; i < count; i++) {
    const qa_input_binding *binding = &bindings[i];
    if (binding->target.is_action) continue;
    const qa_physical_input *physical = &binding->input;
    char *name = NULL;
    if (physical->kind == QA_PHYSICAL_INPUT_KEY ||
        physical->kind == QA_PHYSICAL_INPUT_MOUSE_BUTTON) {
      name = qa_physical_input_name(physical);
    }
    if (include_controller_bindings &&
        (physical->kind == QA_PHYSICAL_INPUT_CONTROLLER_BUTTON ||
         physical->kind == QA_PHYSICAL_INPUT_CONTROLLER_AXIS) &&
        physical->device == 0) {
      if (physical->kind == QA_PHYSICAL_INPUT_CONTROLLER_BUTTON) {
        for (size_t j = 0;
             j < sizeof controller_buttons / sizeof controller_buttons[0]; j++) {
          if (controller_buttons[j].button == physical->button) {
            name = qa_input_dup(controller_buttons[j].command);
            if (name != NULL) {
              for (char *at = name; *at != '\0'; at++) {
                if (*at >= 'a' && *at <= 'z') *at = (char)(*at - 32);
              }
            }
          }
        }
      } else if (physical->direction != NULL &&
                 strcmp(physical->direction, "positive") == 0 &&
                 physical->axis != NULL &&
                 (strcmp(physical->axis, "left-trigger") == 0 ||
                  strcmp(physical->axis, "right-trigger") == 0)) {
        name = qa_input_dup(strcmp(physical->axis, "left-trigger") == 0
                                ? "GAMEPAD_LEFT_TRIGGER"
                                : "GAMEPAD_RIGHT_TRIGGER");
      }
    }
    if (name != NULL) {
      char *quoted_name = quoted(name);
      char *quoted_text = quoted(binding->target.text != NULL
                                     ? binding->target.text
                                     : "");
      size_t length = strlen(quoted_name) + strlen(quoted_text) + 8;
      commands[written] = (char *)malloc(length);
      if (commands[written] != NULL) {
        snprintf(commands[written], length, "bind %s %s", quoted_name, quoted_text);
        written++;
      }
      free(quoted_name);
      free(quoted_text);
    }
    free(name);
  }
  *out_count = written;
  return commands;
}

static const qa_input_binding *store_bindings_of(void *ctx, size_t *out_count) {
  return qa_binding_store_bindings((const qa_binding_store *)ctx, out_count);
}
static const qa_input_binding_target *store_binding_of(
    void *ctx, const qa_physical_input *input) {
  return qa_binding_store_binding((const qa_binding_store *)ctx, input);
}
static bool store_bind(void *ctx, const qa_input_binding *binding) {
  return qa_binding_store_bind((qa_binding_store *)ctx, binding);
}
static void store_unbind(void *ctx, const qa_physical_input *input) {
  qa_binding_store_unbind((qa_binding_store *)ctx, input);
}
static void store_unbind_all(void *ctx) {
  qa_binding_store_unbind_all((qa_binding_store *)ctx);
}
static const qa_input_binding *seat_bindings_of(void *ctx, size_t *out_count) {
  return qa_seat_input_bindings((const qa_seat_input *)ctx, out_count);
}
static const qa_input_binding_target *seat_binding_of(
    void *ctx, const qa_physical_input *input) {
  return qa_seat_input_binding((const qa_seat_input *)ctx, input);
}
static bool seat_bind(void *ctx, const qa_input_binding *binding) {
  return qa_seat_input_bind((qa_seat_input *)ctx, binding);
}
static void seat_unbind(void *ctx, const qa_physical_input *input) {
  qa_seat_input_unbind((qa_seat_input *)ctx, input);
}
static void seat_unbind_all(void *ctx) {
  qa_seat_input_unbind_all((qa_seat_input *)ctx);
}

qa_binding_command_seat qa_binding_store_command_seat(qa_binding_store *store) {
  qa_binding_command_seat seat = {store, store_bindings_of, store_binding_of,
                                  store_bind, store_unbind, store_unbind_all};
  return seat;
}

qa_binding_command_seat qa_seat_input_command_seat(qa_seat_input *input) {
  qa_binding_command_seat seat = {input, seat_bindings_of, seat_binding_of,
                                  seat_bind, seat_unbind, seat_unbind_all};
  return seat;
}

struct qa_binding_commands_registration {
  qa_command_buffer *commands;
  qa_binding_seat_lookup_fn lookup;
  void *lookup_ctx;
  qa_command_print_fn print;
  void *print_ctx;
  bool has_console;
  qa_binding_command_seat console;
  char **names;
  size_t name_count;
};

static bool local_seat(qa_binding_commands_registration *registration,
                       qa_command_invocation *invocation,
                       qa_binding_command_seat *out) {
  const qa_command_origin *origin = &invocation->source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL) {
    origin = origin->caller;
  }
  if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) {
    return registration->lookup(registration->lookup_ctx, origin->seat, out);
  }
  if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_CONSOLE ||
      origin->kind == QA_COMMAND_ORIGIN_SERVER_CONSOLE) {
    if (!registration->has_console) return false;
    *out = registration->console;
    return true;
  }
  return false;
}

static void print_text(qa_binding_commands_registration *registration,
                       const char *text) {
  registration->print(registration->print_ctx, text);
}

static void bind_handler(void *ctx, qa_command_invocation *invocation) {
  qa_binding_commands_registration *registration =
      (qa_binding_commands_registration *)ctx;
  qa_binding_command_seat seat;
  bool has_seat = local_seat(registration, invocation, &seat);
  const char *name = invocation->argc > 1 ? invocation->argv[1] : NULL;
  if (!has_seat || name == NULL) {
    print_text(registration, "bind <key> [command]\n");
    return;
  }
  size_t count = 0;
  const qa_input_binding *bindings = seat.bindings(seat.ctx, &count);
  double device = 0;
  for (size_t i = 0; i < count; i++) {
    if (bindings[i].input.kind == QA_PHYSICAL_INPUT_CONTROLLER_BUTTON ||
        bindings[i].input.kind == QA_PHYSICAL_INPUT_CONTROLLER_AXIS) {
      device = bindings[i].input.device;
      break;
    }
  }
  qa_physical_input physical;
  if (!qa_named_physical_input(name, device, &physical)) {
    char line[512];
    snprintf(line, sizeof line, "Unknown key %s\n", name);
    print_text(registration, line);
    return;
  }
  if (invocation->argc == 2) {
    const qa_input_binding_target *binding = seat.binding(seat.ctx, &physical);
    char *display = qa_physical_input_name(&physical);
    char line[1024];
    if (binding == NULL) {
      snprintf(line, sizeof line, "%s = Unbound\n", display != NULL ? display : "");
    } else {
      snprintf(line, sizeof line, "%s = %s\n", display != NULL ? display : "",
               !binding->is_action
                   ? (binding->text != NULL ? binding->text : "")
                   : (binding->action != NULL ? binding->action : ""));
    }
    print_text(registration, line);
    free(display);
    return;
  }
  size_t length = 0;
  for (size_t i = 2; i < invocation->argc; i++) {
    length += strlen(invocation->argv[i]) + 1;
  }
  char *text = (char *)malloc(length + 1);
  if (text == NULL) return;
  text[0] = '\0';
  for (size_t i = 2; i < invocation->argc; i++) {
    if (i > 2) strcat(text, " ");
    strcat(text, invocation->argv[i]);
  }
  qa_input_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.input = physical;
  binding.target.is_action = false;
  binding.target.text = text;
  (void)seat.bind(seat.ctx, &binding);
  free(text);
}

static void unbind_handler(void *ctx, qa_command_invocation *invocation) {
  qa_binding_commands_registration *registration =
      (qa_binding_commands_registration *)ctx;
  const char *name = invocation->argc > 1 ? invocation->argv[1] : NULL;
  qa_binding_command_seat seat;
  bool has_seat = local_seat(registration, invocation, &seat);
  if (invocation->argc != 2 || name == NULL) {
    print_text(registration, "unbind <key> : remove commands from a key\n");
    return;
  }
  if (!has_seat) {
    print_text(registration, "unbind requires a local binding owner.\n");
    return;
  }
  qa_physical_input physical;
  if (!qa_named_physical_input(name, 0, &physical)) {
    char line[512];
    snprintf(line, sizeof line, "Unknown key %s\n", name);
    print_text(registration, line);
    return;
  }
  if (physical.kind == QA_PHYSICAL_INPUT_CONTROLLER_BUTTON ||
      physical.kind == QA_PHYSICAL_INPUT_CONTROLLER_AXIS) {
    size_t count = 0;
    const qa_input_binding *bindings = seat.bindings(seat.ctx, &count);
    qa_physical_input *matches =
        (qa_physical_input *)calloc(count != 0 ? count : 1, sizeof *matches);
    if (matches == NULL) return;
    size_t matched = 0;
    for (size_t i = 0; i < count; i++) {
      const qa_physical_input *candidate = &bindings[i].input;
      if ((physical.kind == QA_PHYSICAL_INPUT_CONTROLLER_BUTTON &&
           candidate->kind == QA_PHYSICAL_INPUT_CONTROLLER_BUTTON &&
           physical.button == candidate->button) ||
          (physical.kind == QA_PHYSICAL_INPUT_CONTROLLER_AXIS &&
           candidate->kind == QA_PHYSICAL_INPUT_CONTROLLER_AXIS &&
           physical.axis != NULL && candidate->axis != NULL &&
           strcmp(physical.axis, candidate->axis) == 0 &&
           physical.direction != NULL && candidate->direction != NULL &&
           strcmp(physical.direction, candidate->direction) == 0)) {
        matches[matched] = *candidate;
        /* Key derivation reads strings after unbind frees the
         * store copy; snapshot the strings. */
        matches[matched].axis =
            candidate->axis != NULL ? qa_input_dup(candidate->axis) : NULL;
        matches[matched].direction =
            candidate->direction != NULL ? qa_input_dup(candidate->direction)
                                         : NULL;
        matched++;
      }
    }
    for (size_t i = 0; i < matched; i++) {
      seat.unbind(seat.ctx, &matches[i]);
      free((void *)matches[i].axis);
      free((void *)matches[i].direction);
    }
    free(matches);
  } else {
    seat.unbind(seat.ctx, &physical);
  }
}

static void unbindall_handler(void *ctx, qa_command_invocation *invocation) {
  qa_binding_commands_registration *registration =
      (qa_binding_commands_registration *)ctx;
  qa_binding_command_seat seat;
  if (local_seat(registration, invocation, &seat)) seat.unbind_all(seat.ctx);
}

static void bindlist_handler(void *ctx, qa_command_invocation *invocation) {
  qa_binding_commands_registration *registration =
      (qa_binding_commands_registration *)ctx;
  qa_binding_command_seat seat;
  if (!local_seat(registration, invocation, &seat)) return;
  size_t count = 0;
  const qa_input_binding *bindings = seat.bindings(seat.ctx, &count);
  for (size_t i = 0; i < count; i++) {
    char *display = qa_physical_input_name(&bindings[i].input);
    char line[2048];
    snprintf(line, sizeof line, "%s = %s\n", display != NULL ? display : "",
             !bindings[i].target.is_action
                 ? (bindings[i].target.text != NULL ? bindings[i].target.text : "")
                 : (bindings[i].target.action != NULL ? bindings[i].target.action
                                                      : ""));
    print_text(registration, line);
    free(display);
  }
}

qa_binding_commands_registration *qa_register_binding_commands(
    qa_command_buffer *commands, qa_binding_seat_lookup_fn lookup,
    void *lookup_ctx, qa_command_print_fn print, void *print_ctx,
    const qa_binding_command_seat *console_bindings) {
  qa_binding_commands_registration *registration =
      (qa_binding_commands_registration *)calloc(1, sizeof *registration);
  if (registration == NULL) return NULL;
  registration->commands = commands;
  registration->lookup = lookup;
  registration->lookup_ctx = lookup_ctx;
  registration->print = print;
  registration->print_ctx = print_ctx;
  registration->has_console = console_bindings != NULL;
  if (console_bindings != NULL) registration->console = *console_bindings;
  registration->names = (char **)calloc(4, sizeof *registration->names);
  if (registration->names == NULL) {
    free(registration);
    return NULL;
  }
  static const char *const bind_examples[] = {"bind Q", "bind SPACE \"+jump\"",
                                              "bind F1 \"echo ready; echo done\""};
  static const char *const unbind_examples[] = {"unbind Q", "unbind MOUSE2"};
  static const char *const unbindall_examples[] = {"unbindall"};
  static const char *const bindlist_examples[] = {"bindlist"};
  const struct {
    const char *name;
    qa_command_handler_fn handler;
    qa_command_documentation documentation;
  } entries[] = {
      {"bind", bind_handler,
       {"Read or set a binding for the invoking local seat or dedicated console. "
        "Quote multiple commands to keep them on one key.",
        "bind <key> [command]", bind_examples, 3, NULL, 0}},
      {"unbind", unbind_handler,
       {"Remove a key or mouse binding from the invoking local seat or dedicated "
        "console. Named controller buttons are cleared on that seat's devices.",
        "unbind <key>", unbind_examples, 2, NULL, 0}},
      {"unbindall", unbindall_handler,
       {"Clear all bindings for the invoking local seat or dedicated console, "
        "including controller bindings.",
        "unbindall", unbindall_examples, 1, NULL, 0}},
      {"bindlist", bindlist_handler,
       {"List the invoking local seat or dedicated console's current key, mouse "
        "and controller bindings.",
        "bindlist", bindlist_examples, 1, NULL, 0}}};
  for (size_t i = 0; i < sizeof entries / sizeof entries[0]; i++) {
    if (qa_command_buffer_register_engine(commands, entries[i].name,
                                          entries[i].handler, registration,
                                          &entries[i].documentation)) {
      registration->names[registration->name_count] =
          qa_input_dup(entries[i].name);
      if (registration->names[registration->name_count] != NULL) {
        registration->name_count++;
      }
    }
  }
  return registration;
}

void qa_binding_commands_registration_destroy(
    qa_binding_commands_registration *registration) {
  if (registration == NULL) return;
  for (size_t i = 0; i < registration->name_count; i++) {
    (void)qa_command_buffer_unregister(registration->commands,
                                       registration->names[i], NULL, NULL, false);
    free(registration->names[i]);
  }
  free(registration->names);
  free(registration);
}

typedef struct wheel_command {
  const char *name;
  qa_wheel_mode mode;
} wheel_command;

static const wheel_command wheel_commands[] = {
    {"weaponwheel", QA_WHEEL_WEAPONS}, {"wheel", QA_WHEEL_WEAPONS},
    {"powerupwheel", QA_WHEEL_POWERUPS}, {"wheel2", QA_WHEEL_POWERUPS}};

char *qa_canonical_wheel_command(const char *text) {
  char *trimmed = qa_input_js_trim(text);
  char *lowered = trimmed != NULL ? lower_copy(trimmed) : NULL;
  free(trimmed);
  if (lowered == NULL) return NULL;
  char prefix = lowered[0];
  if (prefix != '+' && prefix != '-') {
    free(lowered);
    return qa_input_dup(text);
  }
  const wheel_command *found = NULL;
  for (size_t i = 0; i < sizeof wheel_commands / sizeof wheel_commands[0]; i++) {
    if (strcmp(wheel_commands[i].name, lowered + 1) == 0) found = &wheel_commands[i];
  }
  free(lowered);
  if (found == NULL) return qa_input_dup(text);
  char buffer[32];
  snprintf(buffer, sizeof buffer, "%c%s", prefix,
           found->mode == QA_WHEEL_WEAPONS ? "weaponwheel" : "powerupwheel");
  return qa_input_dup(buffer);
}

typedef struct wheel_handler_ctx {
  qa_wheel_fn wheel;
  void *wheel_ctx;
  qa_wheel_mode mode;
  bool down;
} wheel_handler_ctx;

struct qa_wheel_commands_registration {
  qa_command_buffer *commands;
  char *names[8];
  size_t name_count;
  wheel_handler_ctx ctxs[8];
};

static void wheel_handler(void *ctx, qa_command_invocation *invocation) {
  wheel_handler_ctx *handler = (wheel_handler_ctx *)ctx;
  const qa_command_origin *origin = &invocation->source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL) {
    origin = origin->caller;
  }
  if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) {
    handler->wheel(handler->wheel_ctx, origin->seat, handler->mode, handler->down);
  }
}

qa_wheel_commands_registration *qa_register_wheel_commands(
    qa_command_buffer *commands, qa_wheel_fn wheel, void *wheel_ctx) {
  qa_wheel_commands_registration *registration =
      (qa_wheel_commands_registration *)calloc(1, sizeof *registration);
  if (registration == NULL) return NULL;
  registration->commands = commands;
  size_t at = 0;
  for (size_t i = 0; i < sizeof wheel_commands / sizeof wheel_commands[0]; i++) {
    for (int d = 0; d < 2; d++) {
      bool down = d == 0;
      char name[32];
      snprintf(name, sizeof name, "%c%s", down ? '+' : '-', wheel_commands[i].name);
      registration->ctxs[at].wheel = wheel;
      registration->ctxs[at].wheel_ctx = wheel_ctx;
      registration->ctxs[at].mode = wheel_commands[i].mode;
      registration->ctxs[at].down = down;
      if (qa_command_buffer_register_engine(commands, name, wheel_handler,
                                            &registration->ctxs[at], NULL)) {
        registration->names[registration->name_count] = qa_input_dup(name);
        if (registration->names[registration->name_count] != NULL) {
          registration->name_count++;
        }
      }
      at++;
    }
  }
  return registration;
}

void qa_wheel_commands_registration_destroy(
    qa_wheel_commands_registration *registration) {
  if (registration == NULL) return;
  for (size_t i = 0; i < registration->name_count; i++) {
    (void)qa_command_buffer_unregister(registration->commands,
                                       registration->names[i], NULL, NULL, false);
    free(registration->names[i]);
  }
  free(registration);
}

bool qa_source_key_number(double key, qa_game_family family, double *out) {
  if (family == QA_GAME_FAMILY_Q3) {
    if (key >= 0 && key <= 255) {
      *out = key;
      return true;
    }
    return false;
  }
  if (key < 128) {
    *out = key;
    return true;
  }
  if (key >= QA_KEY_UP && key <= QA_KEY_SHIFT) {
    *out = key - 4;
    return true;
  }
  if (key >= QA_KEY_INSERT && key <= QA_KEY_END) {
    *out = key + 8;
    return true;
  }
  if (key >= QA_KEY_F1 && key <= QA_KEY_F12) {
    *out = key - 10;
    return true;
  }
  if (key == QA_KEY_PAUSE) {
    *out = 255;
    return true;
  }
  if (key >= QA_KEY_MOUSE1 && key <= QA_KEY_MOUSE3) {
    *out = key - QA_KEY_MOUSE1 + 200;
    return true;
  }
  if (key >= QA_KEY_JOY1 && key <= QA_KEY_JOY4) {
    *out = key - QA_KEY_JOY1 + 203;
    return true;
  }
  if (key >= QA_KEY_AUX1 && key <= QA_KEY_AUX16) {
    *out = key - QA_KEY_AUX1 + 207;
    return true;
  }
  if (key >= 256 && key <= 271) {
    *out = key - 256 + 223;
    return true;
  }
  if (key == QA_KEY_MOUSE_WHEEL_UP) {
    *out = family == QA_GAME_FAMILY_Q2 ? 240 : 239;
    return true;
  }
  if (key == QA_KEY_MOUSE_WHEEL_DOWN) {
    *out = family == QA_GAME_FAMILY_Q2 ? 239 : 240;
    return true;
  }
  if (family == QA_GAME_FAMILY_Q2 && key >= QA_KEY_KEYPAD_HOME &&
      key <= QA_KEY_KEYPAD_PLUS) {
    *out = key;
    return true;
  }
  return false;
}
