/* Port of src/input/seat.ts. GPL-2.0-or-later. */
#include "qa/input/seat.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/commands/index.h"
#include "qa/core/commands/text.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/input/impulse.h"
#include "qa/input/key_codes.h"
#include "qa/input/mouse_buttons.h"
#include "qa/input/support.h"

typedef struct action_command {
  const char *name;
  const char *action;
} action_command;

static const action_command action_commands[] = {
    {"attack", "attack"}, {"jump", "jump"}, {"forward", "forward"},
    {"back", "back"}, {"moveleft", "move-left"}, {"moveright", "move-right"},
    {"moveup", "move-up"}, {"movedown", "move-down"}, {"use", "use"},
    {"crouch", "crouch"}, {"speed", "walk"}, {"scores", "scores"},
    {"showscores", "scores"}, {"left", "turn-left"}, {"right", "turn-right"},
    {"lookup", "look-up"}, {"lookdown", "look-down"}, {"strafe", "strafe"},
    {"mlook", "mlook"}, {"klook", "klook"}, {"holster", "holster"}};

static double command_key(const qa_physical_input *input) {
  switch (input->kind) {
    case QA_PHYSICAL_INPUT_KEY:
      return input->code;
    case QA_PHYSICAL_INPUT_MOUSE_BUTTON:
      return QA_KEY_MOUSE1 + qa_quake_mouse_button(input->button) - 1;
    case QA_PHYSICAL_INPUT_CONTROLLER_BUTTON:
      return 65536 + input->device * 64 + input->button;
    case QA_PHYSICAL_INPUT_CONTROLLER_AXIS: {
      static const char *const axes[6] = {"left-x", "left-y", "right-x",
                                          "right-y", "left-trigger",
                                          "right-trigger"};
      double index = -1;
      for (int i = 0; i < 6; i++) {
        if (input->axis != NULL && strcmp(input->axis, axes[i]) == 0) index = i;
      }
      double positive =
          input->direction != NULL && strcmp(input->direction, "positive") == 0
              ? 1
              : 0;
      return 65536 + input->device * 64 + 32 + index * 2 + positive;
    }
  }
  return 0;
}

struct qa_seat_ui_binding {
  qa_seat_ui_event_fn callback;
  void *callback_ctx;
  qa_seat_now_fn now;
  void *now_ctx;
  qa_seat_ui_binding *previous;
  /* Focus snapshot: owned string copies. */
  const char *focus_kind;
  char *focus_menu;
  char *focus_control;
  bool focus_team;
  char *focus_text;
  bool active;
};

typedef struct held_binding {
  qa_physical_input input;
  bool has_target;
  qa_input_binding_target target;
} held_binding;

typedef struct button_entry {
  char *action;
  qa_input_button button;
} button_entry;

struct qa_seat_input {
  qa_seat_input_options options;
  qa_command_dialect dialect;
  qa_gamepad_input gamepad;
  /* Current focus: owned string copies. */
  const char *focus_kind;
  char *focus_menu;
  char *focus_control;
  bool focus_team;
  char *focus_text;
  bool window_focused;
  qa_input_binding *bindings;
  char **binding_keys;
  size_t binding_count;
  size_t binding_capacity;
  held_binding *held;
  char **held_keys;
  size_t held_count;
  size_t held_capacity;
  button_entry *buttons;
  size_t button_count;
  size_t button_capacity;
  qa_vec2 mouse;
  double wheel_remainder;
  double pending_impulse;
  qa_seat_ui_binding *ui_binding;
  qa_seat_ui_binding **ui_nodes;
  size_t ui_node_count;
  size_t ui_node_capacity;
};

static char *dup_or_null(const char *text) {
  return text != NULL ? qa_input_dup(text) : NULL;
}

static qa_seat_input_focus focus_of(const qa_seat_input *input) {
  qa_seat_input_focus focus;
  focus.kind = input->focus_kind;
  focus.menu = input->focus_menu;
  focus.control = input->focus_control;
  focus.team = input->focus_team;
  focus.text = input->focus_text;
  return focus;
}

static void set_current_focus(qa_seat_input *input, const char *kind,
                              const char *menu, const char *control, bool team,
                              const char *text) {
  free(input->focus_menu);
  free(input->focus_control);
  free(input->focus_text);
  input->focus_kind = kind;
  input->focus_menu = dup_or_null(menu);
  input->focus_control = dup_or_null(control);
  input->focus_team = team;
  input->focus_text = dup_or_null(text);
}

static const char *intern_focus_kind(const char *kind) {
  if (kind != NULL) {
    if (strcmp(kind, "game") == 0) return "game";
    if (strcmp(kind, "menu") == 0) return "menu";
    if (strcmp(kind, "console") == 0) return "console";
    if (strcmp(kind, "chat") == 0) return "chat";
  }
  return kind;
}

static size_t binding_index(const qa_seat_input *input, const char *key) {
  for (size_t i = 0; i < input->binding_count; i++) {
    if (strcmp(input->binding_keys[i], key) == 0) return i;
  }
  return input->binding_count;
}

qa_seat_input *qa_seat_input_create(const qa_seat_input_options *options) {
  qa_seat_input *input = (qa_seat_input *)calloc(1, sizeof *input);
  if (input == NULL) return NULL;
  input->options = *options;
  input->dialect = options->dialect;
  input->focus_kind = "game";
  input->window_focused = true;
  input->ui_binding = (qa_seat_ui_binding *)calloc(1, sizeof *input->ui_binding);
  if (input->ui_binding == NULL) {
    free(input);
    return NULL;
  }
  input->ui_nodes = (qa_seat_ui_binding **)malloc(4 * sizeof *input->ui_nodes);
  if (input->ui_nodes == NULL) {
    free(input->ui_binding);
    free(input);
    return NULL;
  }
  input->ui_nodes[0] = input->ui_binding;
  input->ui_node_count = 1;
  input->ui_node_capacity = 4;
  input->ui_binding->callback = options->ui_event;
  input->ui_binding->callback_ctx = options->ui_event_ctx;
  input->ui_binding->focus_kind = "game";
  input->ui_binding->active = true;
  bool same_session =
      options->context.session.token == options->seat.session.token;
  if (!same_session ||
      options->context.origin.kind != QA_COMMAND_ORIGIN_LOCAL_SEAT ||
      !qa_same_seat(options->context.origin.seat, options->seat)) {
    qa_input_error("Input command context must belong to its local seat");
  }
  qa_gamepad_input_init(&input->gamepad, options->gamepad);
  return input;
}

void qa_seat_input_destroy(qa_seat_input *input) {
  if (input == NULL) return;
  for (size_t i = 0; i < input->binding_count; i++) {
    qa_input_binding_dispose(&input->bindings[i]);
    free(input->binding_keys[i]);
  }
  free(input->bindings);
  free(input->binding_keys);
  for (size_t i = 0; i < input->held_count; i++) {
    free((void *)input->held[i].input.axis);
    free((void *)input->held[i].input.direction);
    free((void *)input->held[i].target.action);
    free((void *)input->held[i].target.text);
    free(input->held_keys[i]);
  }
  free(input->held);
  free(input->held_keys);
  for (size_t i = 0; i < input->button_count; i++) {
    free(input->buttons[i].action);
    qa_input_button_destroy(&input->buttons[i].button);
  }
  free(input->buttons);
  for (size_t i = 0; i < input->ui_node_count; i++) {
    free(input->ui_nodes[i]->focus_menu);
    free(input->ui_nodes[i]->focus_control);
    free(input->ui_nodes[i]->focus_text);
    free(input->ui_nodes[i]);
  }
  free(input->ui_nodes);
  free(input->focus_menu);
  free(input->focus_control);
  free(input->focus_text);
  free(input);
}

qa_command_dialect qa_seat_input_dialect(const qa_seat_input *input) {
  return input->dialect;
}

qa_seat_id qa_seat_input_seat(const qa_seat_input *input) {
  return input->options.seat;
}

qa_gamepad_input *qa_seat_input_gamepad(qa_seat_input *input) {
  return &input->gamepad;
}

qa_seat_input_focus qa_seat_input_get_focus(const qa_seat_input *input) {
  return focus_of(input);
}

bool qa_seat_input_focused(const qa_seat_input *input) {
  return input->window_focused;
}

const qa_input_binding *qa_seat_input_bindings(const qa_seat_input *input,
                                               size_t *out_count) {
  *out_count = input->binding_count;
  return input->bindings;
}

bool qa_seat_input_is_down(const qa_seat_input *input,
                           const qa_physical_input *physical) {
  char *key = qa_physical_input_key(physical);
  if (key == NULL) return false;
  bool down = false;
  for (size_t i = 0; i < input->held_count; i++) {
    if (strcmp(input->held_keys[i], key) == 0) down = true;
  }
  free(key);
  return down;
}

bool qa_seat_input_bind(qa_seat_input *input, const qa_input_binding *binding) {
  char *key = qa_physical_input_key(&binding->input);
  if (key == NULL) return false;
  size_t index = binding_index(input, key);
  qa_input_binding copy;
  if (!qa_input_binding_copy(&copy, binding)) {
    free(key);
    return false;
  }
  if (index != input->binding_count) {
    qa_input_binding_dispose(&input->bindings[index]);
    input->bindings[index] = copy;
    free(key);
    return true;
  }
  if (input->binding_count == input->binding_capacity) {
    size_t grown = input->binding_capacity == 0 ? 8 : input->binding_capacity * 2;
    qa_input_binding *bindings =
        (qa_input_binding *)realloc(input->bindings, grown * sizeof *bindings);
    if (bindings == NULL) {
      qa_input_binding_dispose(&copy);
      free(key);
      return false;
    }
    input->bindings = bindings;
    char **keys = (char **)realloc(input->binding_keys, grown * sizeof *keys);
    if (keys == NULL) {
      qa_input_binding_dispose(&copy);
      free(key);
      return false;
    }
    input->binding_keys = keys;
    input->binding_capacity = grown;
  }
  input->bindings[input->binding_count] = copy;
  input->binding_keys[input->binding_count] = key;
  input->binding_count++;
  return true;
}

void qa_seat_input_unbind(qa_seat_input *input, const qa_physical_input *physical) {
  char *key = qa_physical_input_key(physical);
  if (key == NULL) return;
  size_t index = binding_index(input, key);
  free(key);
  if (index == input->binding_count) return;
  qa_input_binding_dispose(&input->bindings[index]);
  free(input->binding_keys[index]);
  memmove(&input->bindings[index], &input->bindings[index + 1],
          (input->binding_count - index - 1) * sizeof *input->bindings);
  memmove(&input->binding_keys[index], &input->binding_keys[index + 1],
          (input->binding_count - index - 1) * sizeof *input->binding_keys);
  input->binding_count--;
}

void qa_seat_input_unbind_all(qa_seat_input *input) {
  for (size_t i = 0; i < input->binding_count; i++) {
    qa_input_binding_dispose(&input->bindings[i]);
    free(input->binding_keys[i]);
  }
  input->binding_count = 0;
}

const qa_input_binding_target *qa_seat_input_binding(
    const qa_seat_input *input, const qa_physical_input *physical) {
  char *key = qa_physical_input_key(physical);
  if (key == NULL) return NULL;
  size_t index = binding_index(input, key);
  free(key);
  return index == input->binding_count ? NULL : &input->bindings[index].target;
}

void qa_seat_input_remap_controller_bindings(qa_seat_input *input, double device) {
  size_t count = input->binding_count;
  qa_input_binding *snapshot = NULL;
  if (count != 0) {
    snapshot = (qa_input_binding *)malloc(count * sizeof *snapshot);
    if (snapshot == NULL) return;
    size_t copied = 0;
    for (size_t i = 0; i < count; i++) {
      if (!qa_input_binding_copy(&snapshot[i], &input->bindings[i])) break;
      copied++;
    }
    if (copied != count) {
      for (size_t i = 0; i < copied; i++) qa_input_binding_dispose(&snapshot[i]);
      free(snapshot);
      return;
    }
  }
  for (size_t i = 0; i < count; i++) {
    if (snapshot[i].input.kind == QA_PHYSICAL_INPUT_CONTROLLER_BUTTON ||
        snapshot[i].input.kind == QA_PHYSICAL_INPUT_CONTROLLER_AXIS) {
      qa_seat_input_unbind(input, &snapshot[i].input);
      qa_input_binding moved = snapshot[i];
      moved.input.device = device;
      (void)qa_seat_input_bind(input, &moved);
    }
    qa_input_binding_dispose(&snapshot[i]);
  }
  free(snapshot);
}

static void run_binding(qa_seat_input *input, const held_binding *binding, bool down,
                        double now, qa_command_buffer *commands,
                        const qa_command_append_target *target);

static void release_impl(qa_seat_input *input, double time, qa_command_buffer *commands,
                         const qa_command_append_target *target) {
  if (commands == NULL) commands = input->options.commands;
  for (size_t i = 0; i < input->held_count; i++) {
    run_binding(input, &input->held[i], false, time, commands, target);
  }
  for (size_t i = 0; i < input->held_count; i++) {
    free((void *)input->held[i].input.axis);
    free((void *)input->held[i].input.direction);
    free((void *)input->held[i].target.action);
    free((void *)input->held[i].target.text);
    free(input->held_keys[i]);
  }
  input->held_count = 0;
  for (size_t i = 0; i < input->button_count; i++) {
    qa_input_button_release(&input->buttons[i].button, time);
  }
  qa_gamepad_input_clear(&input->gamepad);
  input->mouse.x = 0;
  input->mouse.y = 0;
  input->pending_impulse = 0;
}

void qa_seat_input_release(qa_seat_input *input, double time,
                           qa_command_buffer *commands) {
  release_impl(input, time, commands, NULL);
}

void qa_seat_input_release_to(qa_seat_input *input, double time,
                              const qa_command_append_target *commands) {
  release_impl(input, time, NULL, commands);
}

void qa_seat_input_set_focus(qa_seat_input *input, qa_seat_input_focus focus,
                             double now_milliseconds) {
  qa_seat_input_release(input, now_milliseconds, NULL);
  set_current_focus(input, intern_focus_kind(focus.kind), focus.menu, focus.control,
                    focus.team, focus.text);
}

static void snapshot_focus_into(qa_seat_ui_binding *binding,
                                const qa_seat_input *input) {
  free(binding->focus_menu);
  free(binding->focus_control);
  free(binding->focus_text);
  binding->focus_kind = input->focus_kind;
  binding->focus_menu = dup_or_null(input->focus_menu);
  binding->focus_control = dup_or_null(input->focus_control);
  binding->focus_team = input->focus_team;
  binding->focus_text = dup_or_null(input->focus_text);
}

qa_seat_ui_binding *qa_seat_input_bind_ui_event(qa_seat_input *input,
                                                qa_seat_ui_event_fn callback,
                                                void *callback_ctx,
                                                qa_seat_now_fn now, void *now_ctx) {
  qa_seat_ui_binding *previous = input->ui_binding;
  snapshot_focus_into(previous, input);
  qa_seat_input_release(input, now(now_ctx), NULL);
  qa_seat_ui_binding *binding =
      (qa_seat_ui_binding *)calloc(1, sizeof *binding);
  if (binding == NULL) return NULL;
  binding->callback = callback;
  binding->callback_ctx = callback_ctx;
  binding->now = now;
  binding->now_ctx = now_ctx;
  binding->previous = previous;
  binding->active = true;
  snapshot_focus_into(binding, input);
  if (input->ui_node_count == input->ui_node_capacity) {
    size_t grown = input->ui_node_capacity * 2;
    qa_seat_ui_binding **nodes = (qa_seat_ui_binding **)realloc(
        input->ui_nodes, grown * sizeof *nodes);
    if (nodes == NULL) {
      free(binding->focus_menu);
      free(binding->focus_control);
      free(binding->focus_text);
      free(binding);
      return NULL;
    }
    input->ui_nodes = nodes;
    input->ui_node_capacity = grown;
  }
  input->ui_nodes[input->ui_node_count++] = binding;
  input->ui_binding = binding;
  return binding;
}

void qa_seat_input_unbind_ui_event(qa_seat_input *input,
                                   qa_seat_ui_binding *binding) {
  if (binding == NULL || !binding->active) return;
  binding->active = false;
  if (input->ui_binding != binding) return;
  qa_seat_input_release(input, binding->now(binding->now_ctx), NULL);
  qa_seat_ui_binding *restored = binding->previous;
  while (restored != NULL && !restored->active && restored->previous != NULL) {
    restored = restored->previous;
  }
  if (restored == NULL) restored = binding;
  input->ui_binding = restored;
  set_current_focus(input, restored->focus_kind, restored->focus_menu,
                    restored->focus_control, restored->focus_team,
                    restored->focus_text);
}

bool qa_seat_input_has_held_input(const qa_seat_input *input) {
  if (input->held_count != 0) return true;
  for (size_t i = 0; i < input->button_count; i++) {
    if (qa_input_button_active(&input->buttons[i].button)) return true;
  }
  return false;
}

void qa_seat_input_set_profile(qa_seat_input *input, qa_command_dialect dialect) {
  if (input->dialect == dialect) return;
  if (qa_seat_input_has_held_input(input)) {
    qa_input_error("Input profile requires released keys");
  }
  input->dialect = dialect;
}

void qa_seat_input_set_impulse(qa_seat_input *input, double value) {
  if (value != trunc(value) || value < 0 || value > 255) {
    qa_range_error("Input impulse must fit a byte");
  }
  input->pending_impulse = value;
}

qa_input_button *qa_seat_input_button(qa_seat_input *input, qa_source_action action) {
  for (size_t i = 0; i < input->button_count; i++) {
    if (strcmp(input->buttons[i].action, action) == 0) {
      return &input->buttons[i].button;
    }
  }
  if (input->button_count == input->button_capacity) {
    size_t grown = input->button_capacity == 0 ? 8 : input->button_capacity * 2;
    button_entry *buttons =
        (button_entry *)realloc(input->buttons, grown * sizeof *buttons);
    if (buttons == NULL) return NULL;
    input->buttons = buttons;
    input->button_capacity = grown;
  }
  char *copy = qa_input_dup(action);
  if (copy == NULL) return NULL;
  button_entry *entry = &input->buttons[input->button_count++];
  entry->action = copy;
  qa_input_button_init(&entry->button);
  return &entry->button;
}

void qa_seat_input_command_button(qa_seat_input *input, qa_source_action action,
                                  const char *key, bool down,
                                  double time_milliseconds) {
  qa_input_button *button = qa_seat_input_button(input, action);
  if (button == NULL) return;
  if (down) (void)qa_input_button_down(button, key, time_milliseconds);
  else qa_input_button_up(button, key, time_milliseconds, 10);
}

/* TS commands.append(line, source): a release target replaces the buffer
 * (TS Pick<CommandBuffer, "append">). */
static void append_line(qa_command_buffer *commands, const qa_command_append_target *target,
                        const char *line, const qa_command_context *source) {
  if (target != NULL) target->append(target->ctx, line, source);
  else qa_command_buffer_append(commands, line, source, -1);
}

static void run_binding(qa_seat_input *input, const held_binding *binding, bool down,
                        double now, qa_command_buffer *commands,
                        const qa_command_append_target *sink) {
  if (!binding->has_target) return;
  const qa_input_binding_target *target = &binding->target;
  if (target->is_action) {
    char *key = qa_physical_input_key(&binding->input);
    if (key == NULL) return;
    qa_seat_input_command_button(input, target->action, key, down, now);
    free(key);
    return;
  }
  double key = command_key(&binding->input);
  qa_command_origin script;
  memset(&script, 0, sizeof script);
  script.kind = QA_COMMAND_ORIGIN_SCRIPT;
  script.name = "key-binding";
  script.caller = &input->options.context.origin;
  qa_command_context source;
  memset(&source, 0, sizeof source);
  source.session = input->options.context.session;
  source.origin = script;
  char *remaining = qa_source_command_text(target->text != NULL ? target->text : "");
  if (remaining == NULL) return;
  char *key_text = qa_input_js_number(key);
  char *now_text = qa_input_js_number(trunc(now));
  bool had_button = false;
  char *at = remaining;
  while (*at != '\0') {
    size_t offset = qa_command_separator_offset(at, input->dialect);
    char *segment_raw = qa_input_dup_range(at, offset);
    char *segment = segment_raw != NULL ? qa_input_js_trim(segment_raw) : NULL;
    free(segment_raw);
    at += offset;
    if (*at != '\0') at++;
    if (segment == NULL || segment[0] == '\0') {
      free(segment);
      continue;
    }
    if (segment[0] == '+') {
      size_t length = strlen(segment) + 64;
      char *line = (char *)malloc(length);
      if (line != NULL) {
        snprintf(line, length, "%c%s %s %s\n", down ? '+' : '-', segment + 1,
                 key_text != NULL ? key_text : "", now_text != NULL ? now_text : "");
        append_line(commands, sink, line, &source);
        free(line);
      }
      had_button = true;
    } else if (down || had_button) {
      size_t length = strlen(segment) + 2;
      char *line = (char *)malloc(length);
      if (line != NULL) {
        snprintf(line, length, "%s\n", segment);
        append_line(commands, sink, line, &source);
        free(line);
      }
    }
    free(segment);
  }
  free(key_text);
  free(now_text);
  free(remaining);
}

static void held_dispose(held_binding *held) {
  free((void *)held->input.axis);
  free((void *)held->input.direction);
  free((void *)held->target.action);
  free((void *)held->target.text);
}

static void digital(qa_seat_input *input, const qa_physical_input *physical,
                    bool down, double time, bool consumed) {
  char *key = qa_physical_input_key(physical);
  if (key == NULL) return;
  size_t index = input->held_count;
  for (size_t i = 0; i < input->held_count; i++) {
    if (strcmp(input->held_keys[i], key) == 0) index = i;
  }
  if (down) {
    if (index != input->held_count) {
      free(key);
      return;
    }
    held_binding held;
    memset(&held, 0, sizeof held);
    held.input = *physical;
    held.input.axis = dup_or_null(physical->axis);
    held.input.direction = dup_or_null(physical->direction);
    const qa_input_binding_target *target =
        !consumed && strcmp(input->focus_kind, "game") == 0 && input->window_focused
            ? qa_seat_input_binding(input, physical)
            : NULL;
    if (target != NULL) {
      held.has_target = true;
      held.target.is_action = target->is_action;
      held.target.action = dup_or_null(target->action);
      held.target.text = dup_or_null(target->text);
    }
    if (input->held_count == input->held_capacity) {
      size_t grown = input->held_capacity == 0 ? 8 : input->held_capacity * 2;
      held_binding *helds =
          (held_binding *)realloc(input->held, grown * sizeof *helds);
      if (helds == NULL) {
        held_dispose(&held);
        free(key);
        return;
      }
      input->held = helds;
      char **keys = (char **)realloc(input->held_keys, grown * sizeof *keys);
      if (keys == NULL) {
        held_dispose(&held);
        free(key);
        return;
      }
      input->held_keys = keys;
      input->held_capacity = grown;
    }
    input->held[input->held_count] = held;
    input->held_keys[input->held_count] = key;
    input->held_count++;
    run_binding(input, &input->held[input->held_count - 1], true, time,
                input->options.commands, NULL);
  } else if (index != input->held_count) {
    run_binding(input, &input->held[index], false, time, input->options.commands, NULL);
    held_dispose(&input->held[index]);
    free(input->held_keys[index]);
    memmove(&input->held[index], &input->held[index + 1],
            (input->held_count - index - 1) * sizeof *input->held);
    memmove(&input->held_keys[index], &input->held_keys[index + 1],
            (input->held_count - index - 1) * sizeof *input->held_keys);
    input->held_count--;
    free(key);
  } else {
    free(key);
  }
}

bool qa_seat_input_input(qa_seat_input *input, const qa_seat_input_event *event) {
  if (!qa_same_seat(event->seat, input->options.seat)) {
    qa_input_error("Input event delivered to the wrong seat");
  }
  if (!isfinite(event->time_milliseconds) || event->time_milliseconds < 0) {
    qa_range_error("Invalid input event timestamp");
  }
  double time = event->time_milliseconds;
  const char *kind = event->kind != NULL ? event->kind : "";
  if (strcmp(kind, "focus") == 0) {
    input->window_focused = event->focused;
    if (!event->focused) qa_seat_input_release(input, time, NULL);
    return input->ui_binding->callback(input->ui_binding->callback_ctx, event,
                                       focus_of(input));
  }
  if (!input->window_focused) return false;
  bool consumed = input->ui_binding->callback(input->ui_binding->callback_ctx,
                                              event, focus_of(input));
  bool game = strcmp(input->focus_kind, "game") == 0;
  if (strcmp(kind, "key") == 0) {
    qa_physical_input physical;
    memset(&physical, 0, sizeof physical);
    physical.kind = QA_PHYSICAL_INPUT_KEY;
    physical.code = event->code;
    digital(input, &physical, event->down, time, consumed);
  } else if (strcmp(kind, "mouse-button") == 0) {
    qa_physical_input physical;
    memset(&physical, 0, sizeof physical);
    physical.kind = QA_PHYSICAL_INPUT_MOUSE_BUTTON;
    physical.button = event->button;
    digital(input, &physical, event->down, time, consumed);
  } else if (strcmp(kind, "controller-button") == 0) {
    qa_physical_input physical;
    memset(&physical, 0, sizeof physical);
    physical.kind = QA_PHYSICAL_INPUT_CONTROLLER_BUTTON;
    physical.device = event->device;
    physical.button = event->button;
    digital(input, &physical, event->down, time, consumed);
  } else if (strcmp(kind, "controller-axis") == 0) {
    qa_gamepad_input_preview_axis(&input->gamepad, event->axis, event->value);
    if (!consumed && game) {
      qa_gamepad_input_axis(&input->gamepad, event->axis, event->value);
    }
    const char *directions[2] = {"negative", "positive"};
    for (int i = 0; i < 2; i++) {
      double value = i == 1 ? event->value : -event->value;
      qa_physical_input physical;
      memset(&physical, 0, sizeof physical);
      physical.kind = QA_PHYSICAL_INPUT_CONTROLLER_AXIS;
      physical.device = event->device;
      physical.axis = event->axis;
      physical.direction = directions[i];
      digital(input, &physical, value > input->gamepad.tuning.trigger_threshold,
              time, consumed);
    }
  } else if (strcmp(kind, "mouse-motion") == 0) {
    if (!consumed && game) {
      input->mouse.x += event->delta.x;
      input->mouse.y += event->delta.y;
    }
  } else if (strcmp(kind, "mouse-wheel") == 0) {
    /* High-resolution wheels report fractions of a notch; one press is emitted
       per whole accumulated notch. */
    if (event->delta.y != 0 &&
        qa_js_sign(event->delta.y) != qa_js_sign(input->wheel_remainder)) {
      input->wheel_remainder = 0;
    }
    double total = input->wheel_remainder + event->delta.y;
    double notches = trunc(fabs(total));
    input->wheel_remainder = total - qa_js_sign(total) * notches;
    double code = total > 0 ? QA_KEY_MOUSE_WHEEL_UP : QA_KEY_MOUSE_WHEEL_DOWN;
    for (double index = 0; index < notches; index++) {
      qa_physical_input physical;
      memset(&physical, 0, sizeof physical);
      physical.kind = QA_PHYSICAL_INPUT_KEY;
      physical.code = code;
      digital(input, &physical, true, time, consumed);
      digital(input, &physical, false, time, false);
    }
  }
  return consumed || game;
}

void qa_seat_input_gyro(qa_seat_input *input, qa_vec3 sample,
                        double time_milliseconds) {
  if (input->window_focused) {
    qa_gamepad_input_gyro(&input->gamepad, sample, time_milliseconds,
                          strcmp(input->focus_kind, "game") == 0);
  }
}

/* TS seat.input.gamepad.tuning = tuning (remote-application.ts publishConfiguration). */
void qa_seat_input_set_gamepad_tuning(qa_seat_input *input, const qa_gamepad_tuning *tuning) {
  input->gamepad.tuning = *tuning;
}
void qa_seat_input_release_device(qa_seat_input *input, double device, double time) {
  size_t i = 0;
  while (i < input->held_count) {
    qa_physical_input *physical = &input->held[i].input;
    if ((physical->kind == QA_PHYSICAL_INPUT_CONTROLLER_AXIS ||
         physical->kind == QA_PHYSICAL_INPUT_CONTROLLER_BUTTON) &&
        physical->device == device) {
      run_binding(input, &input->held[i], false, time, input->options.commands, NULL);
      held_dispose(&input->held[i]);
      free(input->held_keys[i]);
      memmove(&input->held[i], &input->held[i + 1],
              (input->held_count - i - 1) * sizeof *input->held);
      memmove(&input->held_keys[i], &input->held_keys[i + 1],
              (input->held_count - i - 1) * sizeof *input->held_keys);
      input->held_count--;
    } else {
      i++;
    }
  }
  /* A stick held while the device vanished would otherwise keep repeating in
     the menu until it closes. */
  static const char *const menu_axes[2] = {"left-x", "left-y"};
  for (int axis = 0; axis < 2; axis++) {
    qa_seat_input_event release;
    memset(&release, 0, sizeof release);
    release.kind = "controller-axis";
    release.seat = input->options.seat;
    release.time_milliseconds = time;
    release.device = device;
    release.axis = menu_axes[axis];
    release.value = 0;
    (void)input->ui_binding->callback(input->ui_binding->callback_ctx, &release,
                                      focus_of(input));
  }
  qa_gamepad_input_clear(&input->gamepad);
  qa_gamepad_input_reset_gyro_calibration(&input->gamepad);
}

bool qa_seat_input_take_sample(qa_seat_input *input, double now_milliseconds,
                          double frame_milliseconds, qa_seat_input_sample *out) {
  if (!isfinite(now_milliseconds) || !isfinite(frame_milliseconds) ||
      frame_milliseconds <= 0) {
    qa_range_error("Input sample requires a positive frame duration");
  }
  memset(out, 0, sizeof *out);
  qa_button_timing timing =
      input->dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE ||
              input->dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD
          ? QA_BUTTON_TIMING_Q1
          : input->dialect == QA_COMMAND_DIALECT_Q3 ? QA_BUTTON_TIMING_Q3
                                                    : QA_BUTTON_TIMING_Q2;
  if (input->button_count != 0) {
    out->buttons =
        (qa_button_sample *)calloc(input->button_count, sizeof *out->buttons);
    if (out->buttons == NULL) return false;
  }
  out->button_count = input->button_count;
  for (size_t i = 0; i < input->button_count; i++) {
    qa_input_button *button = &input->buttons[i].button;
    bool pressed = qa_input_button_pressed(button);
    bool active = qa_input_button_active(button);
    out->buttons[i].action = qa_input_dup(input->buttons[i].action);
    out->buttons[i].pressed = pressed;
    out->buttons[i].active = active;
    out->buttons[i].fraction =
        qa_input_button_sample(button, timing, now_milliseconds, frame_milliseconds);
  }
  qa_gamepad_sample gamepad =
      qa_gamepad_input_sample(&input->gamepad, frame_milliseconds);
  out->seat = input->options.seat;
  out->now_milliseconds = now_milliseconds;
  out->frame_milliseconds = frame_milliseconds;
  out->focus = focus_of(input);
  out->mouse = input->mouse;
  out->gamepad_move = gamepad.move;
  out->gamepad_look_degrees = gamepad.look_degrees;
  out->impulse = input->pending_impulse;
  out->any_key_down = (double)input->held_count;
  input->mouse.x = 0;
  input->mouse.y = 0;
  input->pending_impulse = 0;
  return true;
}

void qa_seat_input_sample_dispose(qa_seat_input_sample *sample) {
  for (size_t i = 0; i < sample->button_count; i++) free(sample->buttons[i].action);
  free(sample->buttons);
  memset(sample, 0, sizeof *sample);
}

struct qa_input_commands_registration {
  qa_command_buffer *commands;
  qa_seat_lookup_fn lookup;
  void *lookup_ctx;
  qa_client_scores_fn client_scores;
  void *client_scores_ctx;
  char **names;
  size_t name_count;
  size_t name_capacity;
  void *handler_ctxs; /* action_handler_ctx array */
};

typedef struct action_handler_ctx {
  qa_input_commands_registration *registration;
  const char *name;
  const char *action;
  bool down;
  bool impulse;
} action_handler_ctx;

static const qa_command_origin *root_origin(const qa_command_context *source) {
  const qa_command_origin *origin = &source->origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL) {
    origin = origin->caller;
  }
  return origin;
}

static void action_handler(void *ctx, qa_command_invocation *command) {
  action_handler_ctx *handler = (action_handler_ctx *)ctx;
  qa_input_commands_registration *registration = handler->registration;
  if (handler->impulse) {
    const qa_command_origin *origin = root_origin(&command->source);
    if (origin->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT) return;
    const char *argument = command->argc > 1 ? command->argv[1] : "";
    double impulse = qa_parse_impulse(argument, command->dialect);
    if (impulse != trunc(impulse) || !isfinite(impulse) || impulse < 0 ||
        impulse > 255) {
      qa_raise(QA_COMMAND_USAGE_ERROR, 0, 0, "Input impulse must fit a byte");
    }
    qa_seat_input *seat = registration->lookup(registration->lookup_ctx, origin->seat);
    if (seat != NULL) qa_seat_input_set_impulse(seat, impulse);
    return;
  }
  if (strcmp(handler->name, "scores") == 0 && registration->client_scores != NULL &&
      registration->client_scores(registration->client_scores_ctx, command)) {
    return;
  }
  const qa_command_origin *origin = root_origin(&command->source);
  if (origin->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT) return;
  qa_seat_input *seat = registration->lookup(registration->lookup_ctx, origin->seat);
  if (seat == NULL) return;
  double time = qa_input_js_parse_number(command->argc > 2 ? command->argv[2] : "0");
  const char *key = command->argc > 1 ? command->argv[1] : "console";
  if (!isfinite(time)) {
    qa_raise(QA_COMMAND_USAGE_ERROR, 0, 0, "Invalid button command timestamp");
  }
  if (!handler->down && command->argc <= 1) {
    qa_input_button *button = qa_seat_input_button(seat, handler->action);
    if (button != NULL) qa_input_button_release(button, time);
  } else {
    qa_seat_input_command_button(seat, handler->action, key, handler->down, time);
  }
}

static bool registration_push_name(qa_input_commands_registration *registration,
                                   const char *name) {
  if (registration->name_count == registration->name_capacity) {
    size_t grown =
        registration->name_capacity == 0 ? 16 : registration->name_capacity * 2;
    char **names =
        (char **)realloc(registration->names, grown * sizeof *names);
    if (names == NULL) return false;
    registration->names = names;
    registration->name_capacity = grown;
  }
  char *copy = qa_input_dup(name);
  if (copy == NULL) return false;
  registration->names[registration->name_count++] = copy;
  return true;
}

qa_input_commands_registration *qa_register_input_commands(
    qa_command_buffer *commands, qa_seat_lookup_fn lookup, void *lookup_ctx,
    qa_client_scores_fn client_scores, void *client_scores_ctx) {
  qa_input_commands_registration *registration =
      (qa_input_commands_registration *)calloc(1, sizeof *registration);
  if (registration == NULL) return NULL;
  registration->commands = commands;
  registration->lookup = lookup;
  registration->lookup_ctx = lookup_ctx;
  registration->client_scores = client_scores;
  registration->client_scores_ctx = client_scores_ctx;
  size_t fixed = sizeof action_commands / sizeof action_commands[0];
  size_t total = (fixed + 15) * 2 + 1;
  action_handler_ctx *ctxs =
      (action_handler_ctx *)calloc(total, sizeof *ctxs);
  char (*button_names)[16] = (char (*)[16])calloc(15, 16);
  if (ctxs == NULL || button_names == NULL) {
    free(ctxs);
    free(button_names);
    free(registration);
    return NULL;
  }
  /* Button action strings live beside the ctx array. */
  registration->handler_ctxs = ctxs;
  size_t at = 0;
  for (size_t i = 0; i < fixed + 15; i++) {
    const char *name, *action;
    if (i < fixed) {
      name = action_commands[i].name;
      action = action_commands[i].action;
    } else {
      snprintf(button_names[i - fixed], 16, "button%zu", i - fixed);
      name = button_names[i - fixed];
      action = button_names[i - fixed];
    }
    for (int d = 0; d < 2; d++) {
      bool down = d == 0;
      char command_name[40];
      snprintf(command_name, sizeof command_name, "%c%s", down ? '+' : '-', name);
      ctxs[at].registration = registration;
      ctxs[at].name = name;
      ctxs[at].action = action;
      ctxs[at].down = down;
      if (qa_command_buffer_register_engine(commands, command_name, action_handler,
                                            &ctxs[at], NULL)) {
        (void)registration_push_name(registration, command_name);
      }
      at++;
    }
  }
  ctxs[at].registration = registration;
  ctxs[at].impulse = true;
  if (qa_command_buffer_register(commands, "impulse", action_handler, &ctxs[at],
                                 NULL, QA_COMMAND_WORLD)) {
    (void)registration_push_name(registration, "impulse");
  }
  /* The button name strings stay attached to the ctx block; leak
   * them with the registration's ctx storage is avoided by keeping
   * the pointer in the last ctx slot's name field cast. */
  ctxs[at].name = (const char *)button_names;
  return registration;
}

void qa_input_commands_registration_destroy(
    qa_input_commands_registration *registration) {
  if (registration == NULL) return;
  for (size_t i = 0; i < registration->name_count; i++) {
    (void)qa_command_buffer_unregister(registration->commands,
                                       registration->names[i], NULL, NULL, false);
    free(registration->names[i]);
  }
  free(registration->names);
  action_handler_ctx *ctxs = (action_handler_ctx *)registration->handler_ctxs;
  if (ctxs != NULL) {
    /* The final slot carries the button-name block pointer. */
    size_t fixed = sizeof action_commands / sizeof action_commands[0];
    size_t total = (fixed + 15) * 2 + 1;
    free((void *)ctxs[total - 1].name);
  }
  free(ctxs);
  free(registration);
}
