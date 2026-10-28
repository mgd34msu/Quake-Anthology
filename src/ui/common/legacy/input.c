/* See include/qa/ui/common/legacy/input.h. GPL-2.0-or-later. */
#include "qa/ui/common/legacy/input.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/input/key_codes.h"
#include "qa/ui/common/legacy/script/lexer.h" /* qa_ui_script_plain_error */

#define FR(x) ((double)(float)(x))

typedef struct held_entry {
  char *key;
  double code;
} held_entry;

struct qa_legacy_ui_seat {
  qa_seat_id seat;
  qa_ui_runtime *runtime;
  qa_vec2 pointer;
  held_entry *held;
  size_t held_count, held_capacity;
  qa_seat_input_event *pending;
  size_t pending_count, pending_capacity;
  bool focused;
  bool closed;
};

static char *dupstr(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  if (copy != NULL) memcpy(copy, text, length + 1);
  return copy;
}

static bool mouse_key(double button, int32_t *out) {
  if (button == 1) *out = QA_KEY_MOUSE1;
  else if (button == 2) *out = QA_KEY_MOUSE3;
  else if (button == 3) *out = QA_KEY_MOUSE2;
  else if (button == 4) *out = QA_KEY_MOUSE4;
  else if (button == 5) *out = QA_KEY_MOUSE5;
  else return false;
  return true;
}

static bool controller_key(double button, int32_t *out) {
  if (button == 0) *out = QA_KEY_ENTER;
  else if (button == 1 || button == 6) *out = QA_KEY_ESCAPE;
  else if (button == 11) *out = QA_KEY_UP;
  else if (button == 12) *out = QA_KEY_DOWN;
  else if (button == 13) *out = QA_KEY_LEFT;
  else if (button == 14) *out = QA_KEY_RIGHT;
  else if (button >= 0 && button < 32) *out = (int32_t)(QA_KEY_JOY1 + button);
  else return false;
  return true;
}

qa_legacy_ui_seat *qa_legacy_ui_seat_create(qa_seat_id seat,
                                            qa_ui_runtime *runtime) {
  qa_legacy_ui_seat *adapter = calloc(1, sizeof *adapter);
  if (adapter == NULL) return NULL;
  adapter->seat = seat;
  adapter->runtime = runtime;
  adapter->pointer.x = 320;
  adapter->pointer.y = 240;
  adapter->focused = true;
  return adapter;
}

static void clear_held(qa_legacy_ui_seat *adapter) {
  for (size_t i = 0; i < adapter->held_count; i++) free(adapter->held[i].key);
  adapter->held_count = 0;
}

static void clear_pending(qa_legacy_ui_seat *adapter) {
  for (size_t i = 0; i < adapter->pending_count; i++)
    free((void *)adapter->pending[i].text);
  adapter->pending_count = 0;
}

void qa_legacy_ui_seat_destroy(qa_legacy_ui_seat *adapter) {
  if (adapter == NULL) return;
  clear_held(adapter);
  free(adapter->held);
  clear_pending(adapter);
  free(adapter->pending);
  free(adapter);
}

qa_seat_id qa_legacy_ui_seat_seat(const qa_legacy_ui_seat *adapter) {
  return adapter->seat;
}

qa_ui_runtime *qa_legacy_ui_seat_runtime(const qa_legacy_ui_seat *adapter) {
  return adapter->runtime;
}

qa_vec2 qa_legacy_ui_seat_cursor(const qa_legacy_ui_seat *adapter) {
  return adapter->pointer;
}

static void held_set(qa_legacy_ui_seat *adapter, const char *key,
                     double code) {
  for (size_t i = 0; i < adapter->held_count; i++) {
    if (strcmp(adapter->held[i].key, key) == 0) {
      adapter->held[i].code = code;
      return;
    }
  }
  if (adapter->held_count == adapter->held_capacity) {
    size_t capacity =
        adapter->held_capacity == 0 ? 8 : adapter->held_capacity * 2;
    held_entry *grown = realloc(adapter->held, capacity * sizeof *grown);
    if (grown == NULL) return;
    adapter->held = grown;
    adapter->held_capacity = capacity;
  }
  adapter->held[adapter->held_count].key = dupstr(key);
  adapter->held[adapter->held_count].code = code;
  adapter->held_count++;
}

static void held_delete(qa_legacy_ui_seat *adapter, const char *key) {
  for (size_t i = 0; i < adapter->held_count; i++) {
    if (strcmp(adapter->held[i].key, key) == 0) {
      free(adapter->held[i].key);
      memmove(&adapter->held[i], &adapter->held[i + 1],
              (adapter->held_count - i - 1) * sizeof(held_entry));
      adapter->held_count--;
      return;
    }
  }
}

static bool held_get(qa_legacy_ui_seat *adapter, const char *key,
                     double *out) {
  for (size_t i = 0; i < adapter->held_count; i++) {
    if (strcmp(adapter->held[i].key, key) == 0) {
      *out = adapter->held[i].code;
      return true;
    }
  }
  return false;
}

static bool runtime_key(qa_legacy_ui_seat *adapter, double code, bool down) {
  qa_ui_key_event event = {QA_UI_KEY_EVENT_KEY, code, down};
  return qa_ui_runtime_handle_key(adapter->runtime, event,
                                  adapter->pointer.x, adapter->pointer.y);
}

static bool seat_key(qa_legacy_ui_seat *adapter, const char *physical,
                     bool has_code, double code, bool down) {
  if (!has_code) return false;
  if (down) held_set(adapter, physical, code);
  else held_delete(adapter, physical);
  return runtime_key(adapter, code, down);
}

static void format_physical(char *buffer, size_t capacity, const char *prefix,
                            double a, bool has_b, double b,
                            const char *suffix) {
  if (suffix != NULL) {
    snprintf(buffer, capacity, "%s%.17g:%s", prefix, a, suffix);
  } else if (has_b) {
    snprintf(buffer, capacity, "%s%.17g:%.17g", prefix, a, b);
  } else {
    snprintf(buffer, capacity, "%s%.17g", prefix, a);
  }
}

bool qa_legacy_ui_seat_route(qa_legacy_ui_seat *adapter,
                             const qa_seat_input_event *event) {
  if (adapter->closed)
    qa_ui_script_plain_error("Legacy UI seat is closed");
  if (!qa_same_seat(event->seat, adapter->seat)) return false;
  if (adapter->pending_count == adapter->pending_capacity) {
    size_t capacity =
        adapter->pending_capacity == 0 ? 8 : adapter->pending_capacity * 2;
    qa_seat_input_event *grown =
        realloc(adapter->pending, capacity * sizeof *grown);
    if (grown == NULL) return false;
    adapter->pending = grown;
    adapter->pending_capacity = capacity;
  }
  qa_seat_input_event copy = *event;
  copy.text = dupstr(event->text);
  adapter->pending[adapter->pending_count++] = copy;
  return strcmp(event->kind, "focus") != 0;
}

void qa_legacy_ui_seat_drain(qa_legacy_ui_seat *adapter,
                             const qa_draw2d *draw) {
  if (adapter->closed)
    qa_ui_script_plain_error("Legacy UI seat is closed");
  qa_seat_input_event *pending = adapter->pending;
  size_t count = adapter->pending_count;
  adapter->pending = NULL;
  adapter->pending_count = 0;
  adapter->pending_capacity = 0;
  for (size_t i = 0; i < count; i++) {
    (void)qa_legacy_ui_seat_input(adapter, &pending[i], draw);
    free((void *)pending[i].text);
  }
  free(pending);
}

/* Decode one UTF-8 codepoint; invalid bytes decode as themselves. */
static uint32_t decode_codepoint(const unsigned char *text, size_t *advance) {
  unsigned char first = text[0];
  if (first < 0x80) {
    *advance = 1;
    return first;
  }
  int extra = first >= 0xf0 ? 3 : first >= 0xe0 ? 2 : first >= 0xc0 ? 1 : 0;
  if (extra == 0) {
    *advance = 1;
    return first;
  }
  uint32_t code = first & (uint32_t)(0x7f >> (extra + 1));
  for (int i = 1; i <= extra; i++) {
    if ((text[i] & 0xc0) != 0x80) {
      *advance = 1;
      return first;
    }
    code = (code << 6) | (uint32_t)(text[i] & 0x3f);
  }
  *advance = (size_t)extra + 1;
  return code;
}

bool qa_legacy_ui_seat_input(qa_legacy_ui_seat *adapter,
                             const qa_seat_input_event *event,
                             const qa_draw2d *draw) {
  if (adapter->closed)
    qa_ui_script_plain_error("Legacy UI seat is closed");
  if (!qa_same_seat(event->seat, adapter->seat)) return false;
  if (!qa_same_seat(draw->commands.seat, adapter->seat))
    qa_ui_script_plain_error("Legacy UI drawing belongs to another seat");
  qa_ui_runtime_set_display_time(adapter->runtime,
                                 qa_int32(event->time_milliseconds));
  if (strcmp(event->kind, "focus") == 0) {
    adapter->focused = event->focused;
    if (!event->focused) {
      for (size_t i = 0; i < adapter->held_count; i++)
        (void)runtime_key(adapter, adapter->held[i].code, false);
      clear_held(adapter);
    }
    return false;
  }
  if (!adapter->focused) return false;
  char physical[128];
  if (strcmp(event->kind, "key") == 0) {
    format_physical(physical, sizeof physical, "key:", event->code, false, 0,
                    NULL);
    return seat_key(adapter, physical, true, event->code, event->down);
  }
  if (strcmp(event->kind, "text") == 0) {
    bool consumed = false;
    const unsigned char *text = (const unsigned char *)(event->text != NULL
                                                            ? event->text
                                                            : "");
    size_t offset = 0;
    while (text[offset] != '\0') {
      size_t advance = 1;
      uint32_t code = decode_codepoint(text + offset, &advance);
      offset += advance;
      if (code <= 255) {
        qa_ui_key_event character = {QA_UI_KEY_EVENT_CHARACTER, (double)code,
                                     false};
        bool handled = qa_ui_runtime_handle_key(
            adapter->runtime, character, adapter->pointer.x,
            adapter->pointer.y);
        consumed = handled || consumed;
      }
    }
    return consumed;
  }
  if (strcmp(event->kind, "mouse-motion") == 0) {
    qa_rect target = draw->commands.target;
    double x = (event->position.x - target.x - qa_draw2d_bias_x(draw)) /
               qa_draw2d_scale_x(draw);
    double y = (event->position.y - target.y) / qa_draw2d_scale_y(draw);
    adapter->pointer.x = FR(x < 0 ? 0 : x > 640 ? 640 : x);
    adapter->pointer.y = FR(y < 0 ? 0 : y > 480 ? 480 : y);
    qa_ui_runtime_set_display_cursor(adapter->runtime, adapter->pointer.x,
                                     adapter->pointer.y);
    return qa_ui_runtime_pointer_move(adapter->runtime, adapter->pointer.x,
                                      adapter->pointer.y);
  }
  if (strcmp(event->kind, "mouse-button") == 0) {
    format_physical(physical, sizeof physical, "mouse:", event->button,
                    false, 0, NULL);
    int32_t code = 0;
    bool has = mouse_key(event->button, &code);
    return seat_key(adapter, physical, has, code, event->down);
  }
  if (strcmp(event->kind, "mouse-wheel") == 0) {
    bool consumed = false;
    int32_t code =
        event->delta.y > 0 ? QA_KEY_MOUSE_WHEEL_UP : QA_KEY_MOUSE_WHEEL_DOWN;
    for (double count = 0; count < fabs(event->delta.y); count++) {
      bool handled = seat_key(adapter, "wheel", true, code, true);
      consumed = handled || consumed;
      (void)seat_key(adapter, "wheel", true, code, false);
    }
    return consumed;
  }
  if (strcmp(event->kind, "controller-button") == 0) {
    format_physical(physical, sizeof physical, "controller:", event->device,
                    true, event->button, NULL);
    int32_t code = 0;
    bool has = controller_key(event->button, &code);
    return seat_key(adapter, physical, has, code, event->down);
  }
  if (strcmp(event->kind, "controller-axis") == 0) {
    if (strcmp(event->axis, "left-x") != 0 &&
        strcmp(event->axis, "left-y") != 0)
      return false;
    format_physical(physical, sizeof physical, "axis:", event->device, false,
                    0, event->axis);
    double previous = 0;
    bool has_previous = held_get(adapter, physical, &previous);
    bool has_current = fabs(event->value) >= 0.5;
    double current = 0;
    if (has_current) {
      current = strcmp(event->axis, "left-x") == 0
                    ? (event->value < 0 ? QA_KEY_LEFT : QA_KEY_RIGHT)
                    : (event->value < 0 ? QA_KEY_UP : QA_KEY_DOWN);
    }
    if (has_current == has_previous &&
        (!has_current || current == previous))
      return has_current;
    bool consumed = false;
    if (has_previous)
      consumed = seat_key(adapter, physical, true, previous, false);
    if (has_current) {
      bool handled = seat_key(adapter, physical, true, current, true);
      consumed = handled || consumed;
    }
    return consumed;
  }
  return false;
}

void qa_legacy_ui_seat_close(qa_legacy_ui_seat *adapter) {
  if (adapter->closed) return;
  adapter->closed = true;
  clear_pending(adapter);
  clear_held(adapter);
  qa_ui_runtime_dispose(adapter->runtime);
}
