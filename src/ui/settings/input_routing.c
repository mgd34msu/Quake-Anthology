/* Port of src/ui/settings/input-routing.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/input_routing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/support.h"

typedef struct routing_state {
  qa_input_router *(*router)(void *);
  void *router_ctx;
  const qa_controller_device *(*devices)(void *, size_t *);
  void *devices_ctx;
  int64_t selected_seat;
  qa_ui_setting_binding items[3];
  qa_ui_choice *choices;
  size_t choice_capacity;
  char **strings;
  size_t string_count, string_capacity;
  char read_buffer[1024];
} routing_state;

static void oom(void) { qa_range_error("Out of memory"); }
static char *pool(routing_state *s, const char *text) {
  if (s->string_count == s->string_capacity) {
    size_t cap = s->string_capacity == 0 ? 16 : s->string_capacity * 2;
    char **next = realloc(s->strings, cap * sizeof *next);
    if (next == NULL) oom();
    s->strings = next;
    s->string_capacity = cap;
  }
  char *copy = qa_input_dup(text);
  if (copy == NULL) oom();
  s->strings[s->string_count++] = copy;
  return copy;
}
static void pool_clear(routing_state *s) {
  for (size_t i = 0; i < s->string_count; i++) free(s->strings[i]);
  s->string_count = 0;
}
static qa_ui_choice *choice_pool(routing_state *s, size_t count) {
  if (s->choice_capacity < count) {
    qa_ui_choice *next = realloc(s->choices, count * sizeof *next);
    if (next == NULL) oom();
    s->choices = next;
    s->choice_capacity = count;
  }
  return s->choices;
}
static void selection_key(qa_controller_selection selection, char *out,
                          size_t size) {
  switch (selection.kind) {
    case QA_CONTROLLER_SELECTION_AUTOMATIC:
      snprintf(out, size, "automatic");
      break;
    case QA_CONTROLLER_SELECTION_NONE:
      snprintf(out, size, "none");
      break;
    case QA_CONTROLLER_SELECTION_SERIAL:
      snprintf(out, size, "serial:%s:%s", selection.guid,
               selection.serial != NULL ? selection.serial : "");
      break;
    default: {
      char *ordinal = qa_input_js_number(selection.ordinal);
      if (ordinal == NULL) oom();
      snprintf(out, size, "device:%s:%s", selection.guid, ordinal);
      free(ordinal);
      break;
    }
  }
}
static bool device_selection(const qa_controller_device *device,
                             qa_controller_selection *out) {
  if (device->guid == NULL) return false;
  memset(out, 0, sizeof *out);
  snprintf(out->guid, sizeof out->guid, "%s", device->guid);
  if (device->serial != NULL && device->serial[0] != '\0') {
    out->kind = QA_CONTROLLER_SELECTION_SERIAL;
    out->serial = device->serial;
  } else {
    out->kind = QA_CONTROLLER_SELECTION_DEVICE;
    out->ordinal = (double)device->ordinal;
  }
  return true;
}
static qa_seat_input *selected_input(routing_state *s) {
  size_t count = 0;
  qa_seat_input **inputs =
      qa_input_router_inputs(s->router(s->router_ctx), &count);
  for (size_t i = 0; i < count; i++)
    if (qa_seat_input_seat(inputs[i]).index == s->selected_seat)
      return inputs[i];
  return count > 0 ? inputs[0] : NULL;
}
static const qa_ui_choice *seat_choices(routing_state *s, bool with_none,
                                        size_t *out_count) {
  pool_clear(s);
  size_t count = 0;
  qa_seat_input **inputs =
      qa_input_router_inputs(s->router(s->router_ctx), &count);
  qa_ui_choice *pool_items = choice_pool(s, count + 1);
  size_t at = 0;
  for (size_t i = 0; i < count; i++) {
    int64_t index = qa_seat_input_seat(inputs[i]).index;
    char id[32], label[48];
    char *num = qa_input_js_number((double)index);
    if (num == NULL) oom();
    snprintf(id, sizeof id, "%s", num);
    free(num);
    char *plus = qa_input_js_number((double)(index + 1));
    if (plus == NULL) oom();
    snprintf(label, sizeof label, "Player %s", plus);
    free(plus);
    pool_items[at++] = (qa_ui_choice){ pool(s, id), pool(s, label) };
  }
  if (with_none) pool_items[at++] = (qa_ui_choice){ "none", "None" };
  *out_count = at;
  return pool_items;
}
static bool always_enabled(void *ctx) { (void)ctx; return true; }

static const char *keyboard_read(void *v) {
  routing_state *s = v;
  qa_seat_id seat;
  if (!qa_input_router_keyboard_seat(s->router(s->router_ctx), &seat))
    return "none";
  char *num = qa_input_js_number((double)seat.index);
  if (num == NULL) oom();
  snprintf(s->read_buffer, sizeof s->read_buffer, "%s", num);
  free(num);
  return s->read_buffer;
}
static const qa_ui_choice *keyboard_choices(void *v, size_t *out_count) {
  return seat_choices(v, true, out_count);
}
static void keyboard_write(void *v, const char *value) {
  routing_state *s = v;
  qa_input_router *router = s->router(s->router_ctx);
  size_t count = 0;
  qa_seat_input **inputs = qa_input_router_inputs(router, &count);
  qa_seat_input *found = NULL;
  for (size_t i = 0; i < count; i++) {
    char *num = qa_input_js_number(
        (double)qa_seat_input_seat(inputs[i]).index);
    if (num == NULL) oom();
    bool equal = strcmp(num, value) == 0;
    free(num);
    if (equal) found = inputs[i];
  }
  if (strcmp(value, "none") != 0 && found == NULL)
    qa_range_error("Unknown keyboard player");
  qa_seat_id none;
  memset(&none, 0, sizeof none);
  qa_input_router_set_keyboard_seat(
      router, found != NULL,
      found != NULL ? qa_seat_input_seat(found) : none);
  qa_input_router_update_capture(router);
}

static bool player_enabled(void *v) {
  routing_state *s = v;
  size_t count = 0;
  qa_input_router_inputs(s->router(s->router_ctx), &count);
  return count > 1;
}
static const char *player_read(void *v) {
  routing_state *s = v;
  qa_seat_input *input = selected_input(s);
  char *num = qa_input_js_number(
      input != NULL ? (double)qa_seat_input_seat(input).index : 0);
  if (num == NULL) oom();
  snprintf(s->read_buffer, sizeof s->read_buffer, "%s", num);
  free(num);
  return s->read_buffer;
}
static const qa_ui_choice *player_choices(void *v, size_t *out_count) {
  return seat_choices(v, false, out_count);
}
static void player_write(void *v, const char *value) {
  routing_state *s = v;
  size_t count = 0;
  qa_seat_input **inputs =
      qa_input_router_inputs(s->router(s->router_ctx), &count);
  for (size_t i = 0; i < count; i++) {
    int64_t index = qa_seat_input_seat(inputs[i]).index;
    char *num = qa_input_js_number((double)index);
    if (num == NULL) oom();
    bool equal = strcmp(num, value) == 0;
    free(num);
    if (equal) {
      s->selected_seat = index;
      return;
    }
  }
  qa_range_error("Unknown controller player");
}

static bool device_enabled(void *v) {
  return selected_input((routing_state *)v) != NULL;
}
static const char *device_read(void *v) {
  routing_state *s = v;
  qa_seat_input *input = selected_input(s);
  if (input == NULL) return "none";
  qa_controller_selection selection = qa_input_router_controller_selection(
      s->router(s->router_ctx), qa_seat_input_seat(input));
  selection_key(selection, s->read_buffer, sizeof s->read_buffer);
  return s->read_buffer;
}
static const qa_ui_choice *device_choices(void *v, size_t *out_count) {
  routing_state *s = v;
  pool_clear(s);
  size_t device_count = 0;
  const qa_controller_device *devices =
      s->devices(s->devices_ctx, &device_count);
  qa_ui_choice *items = choice_pool(s, device_count + 3);
  size_t at = 0;
  items[at++] = (qa_ui_choice){ "automatic", "Automatic" };
  items[at++] = (qa_ui_choice){ "none", "None" };
  for (size_t i = 0; i < device_count; i++) {
    qa_controller_selection target;
    if (!device_selection(&devices[i], &target)) continue;
    char key[1024], label[1024];
    selection_key(target, key, sizeof key);
    char *ordinal = qa_input_js_number(
        (double)(devices[i].ordinal + 1));
    if (ordinal == NULL) oom();
    snprintf(label, sizeof label, "%s (%s)", devices[i].name, ordinal);
    free(ordinal);
    items[at++] = (qa_ui_choice){ pool(s, key), pool(s, label) };
  }
  qa_seat_input *input = selected_input(s);
  char current[1024];
  if (input == NULL) {
    snprintf(current, sizeof current, "none");
  } else {
    qa_controller_selection selection = qa_input_router_controller_selection(
        s->router(s->router_ctx), qa_seat_input_seat(input));
    selection_key(selection, current, sizeof current);
  }
  bool seen = false;
  for (size_t i = 0; i < at; i++)
    if (strcmp(items[i].id, current) == 0) seen = true;
  if (!seen)
    items[at++] = (qa_ui_choice){ pool(s, current),
                                  "Saved controller (disconnected)" };
  *out_count = at;
  return items;
}
static void device_write(void *v, const char *value) {
  routing_state *s = v;
  qa_input_router *router = s->router(s->router_ctx);
  qa_seat_input *input = selected_input(s);
  if (input == NULL) qa_range_error("No local player");
  size_t device_count = 0;
  const qa_controller_device *devices =
      s->devices(s->devices_ctx, &device_count);
  const qa_controller_device *device = NULL;
  qa_controller_selection device_target;
  memset(&device_target, 0, sizeof device_target);
  for (size_t i = 0; i < device_count; i++) {
    qa_controller_selection target;
    if (!device_selection(&devices[i], &target)) continue;
    char key[1024];
    selection_key(target, key, sizeof key);
    if (strcmp(key, value) == 0) {
      device = &devices[i];
      device_target = target;
      break; /* TS devices().find(): first match wins. */
    }
  }
  qa_controller_selection target;
  memset(&target, 0, sizeof target);
  bool have_target = true;
  if (strcmp(value, "automatic") == 0) {
    target.kind = QA_CONTROLLER_SELECTION_AUTOMATIC;
  } else if (strcmp(value, "none") == 0) {
    target.kind = QA_CONTROLLER_SELECTION_NONE;
  } else if (device == NULL) {
    have_target = false;
  } else {
    target = device_target;
  }
  qa_seat_id seat = qa_seat_input_seat(input);
  if (!have_target) {
    char current[1024];
    selection_key(qa_input_router_controller_selection(router, seat),
                  current, sizeof current);
    if (strcmp(current, value) == 0) return;
    qa_range_error("Controller is no longer available");
  }
  if (target.kind != QA_CONTROLLER_SELECTION_AUTOMATIC
      && target.kind != QA_CONTROLLER_SELECTION_NONE) {
    size_t count = 0;
    qa_seat_input **inputs = qa_input_router_inputs(router, &count);
    for (size_t i = 0; i < count; i++) {
      qa_seat_id previous_seat = qa_seat_input_seat(inputs[i]);
      if (qa_same_seat(previous_seat, seat)) continue;
      char key[1024];
      selection_key(qa_input_router_controller_selection(router,
                                                         previous_seat),
                    key, sizeof key);
      double instance = 0;
      bool has_instance =
          qa_input_router_controller_for(router, previous_seat, &instance);
      if (strcmp(key, value) == 0
          || (device != NULL && has_instance
              && (int32_t)instance == device->instance)) {
        qa_controller_selection none;
        memset(&none, 0, sizeof none);
        none.kind = QA_CONTROLLER_SELECTION_NONE;
        qa_input_router_set_controller_selection(router, previous_seat,
                                                 none);
      }
    }
  }
  qa_input_router_set_controller_selection(router, seat, target);
}

static void routing_teardown(void *v) {
  routing_state *s = v;
  pool_clear(s);
  free(s->strings);
  free(s->choices);
  free(s);
}

qa_ui_setting_bindings qa_ui_bind_input_routing_settings(
    qa_input_router *(*router)(void *ctx), void *router_ctx,
    const qa_controller_device *(*devices)(void *ctx, size_t *out_count),
    void *devices_ctx) {
  routing_state *s = calloc(1, sizeof *s);
  if (s == NULL) oom();
  s->router = router;
  s->router_ctx = router_ctx;
  s->devices = devices;
  s->devices_ctx = devices_ctx;
  qa_ui_setting_binding *b = s->items;
  b[0].id = "ui:input:keyboard-player";
  b[0].label = "Keyboard and mouse player";
  b[0].category = "input";
  b[0].group = "Player assignment";
  b[0].kind = "choice";
  b[0].ctx = s;
  b[0].enabled = always_enabled;
  b[0].read_str = keyboard_read;
  b[0].choices = keyboard_choices;
  b[0].write_str = keyboard_write;
  b[1].id = "ui:input:controller-player";
  b[1].label = "Assign controller to";
  b[1].category = "input";
  b[1].group = "Player assignment";
  b[1].kind = "choice";
  b[1].ctx = s;
  b[1].enabled = player_enabled;
  b[1].read_str = player_read;
  b[1].choices = player_choices;
  b[1].write_str = player_write;
  b[2].id = "ui:input:controller-device";
  b[2].label = "Controller device";
  b[2].category = "input";
  b[2].group = "Player assignment";
  b[2].kind = "choice";
  b[2].ctx = s;
  b[2].enabled = device_enabled;
  b[2].read_str = device_read;
  b[2].choices = device_choices;
  b[2].write_str = device_write;
  return qa_ui_setting_bindings_own(s->items, 3, s, routing_teardown);
}
