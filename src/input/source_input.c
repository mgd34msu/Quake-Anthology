/* Port of src/input/source-input.ts. GPL-2.0-or-later. */
#include "qa/input/source_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/input/device_settings.h"

struct qa_source_input_state {
  qa_source_input_options options;
  bool mouse_available;
  bool mouse_active;
  double mouse_reset_time;
  qa_source_joystick_state joystick_state;
  bool closed;
  bool has_joystick;
  qa_sdl_joystick joystick;
  qa_sdl_event *events;
  size_t event_count;
  size_t event_capacity;
  qa_source_joystick_profile profile;
};

qa_source_input_state *qa_source_input_create(qa_source_input_options options) {
  qa_source_input_state *state =
      (qa_source_input_state *)calloc(1, sizeof *state);
  if (state == NULL) return NULL;
  state->options = options;
  qa_source_joystick_state_init(&state->joystick_state);
  state->profile = QA_SOURCE_JOYSTICK_LINUX;
  return state;
}

void qa_source_input_destroy(qa_source_input_state *state) {
  if (state == NULL) return;
  qa_source_input_close(state);
  free(state->events);
  free(state);
}

bool qa_source_input_instance(const qa_source_input_state *state, int32_t *out) {
  if (!state->has_joystick) return false;
  *out = state->joystick.instance;
  return true;
}

bool qa_source_input_mouse_available(const qa_source_input_state *state) {
  return state->mouse_available;
}

bool qa_source_input_mouse_active(const qa_source_input_state *state) {
  return state->mouse_active;
}

double qa_source_input_mouse_reset_time(const qa_source_input_state *state) {
  return state->mouse_reset_time;
}

void qa_source_input_set_mouse_active(qa_source_input_state *state, bool active) {
  state->mouse_active = active;
}

void qa_source_input_set_mouse_reset_time(qa_source_input_state *state,
                                          double time) {
  state->mouse_reset_time = time;
}

qa_source_joystick_state *qa_source_input_joystick_state(
    qa_source_input_state *state) {
  return &state->joystick_state;
}

static void require_open(const qa_source_input_state *state) {
  if (state->closed) qa_input_error("Source input is closed");
}

static void cvar_snapshot(qa_source_input_state *state, const char *name,
                          qa_cvar_snapshot *out) {
  memset(out, 0, sizeof *out);
  if (!qa_cvar_registry_get(state->options.cvars, name, out)) {
    qa_input_error("Input cvar %s no longer exists", name);
  }
}

static void print_text(qa_source_input_state *state, const char *text) {
  state->options.print(state->options.print_ctx, text);
}

static void joystick_print(const char *text, void *ctx) {
  qa_source_input_state *state = (qa_source_input_state *)ctx;
  print_text(state, text);
}

static qa_sdl_joystick_profile sdl_profile(qa_source_joystick_profile profile) {
  return profile == QA_SOURCE_JOYSTICK_WINDOWS ? QA_SDL_JOYSTICK_WINDOWS
                                               : QA_SDL_JOYSTICK_LINUX;
}

void qa_source_input_initialize(qa_source_input_state *state) {
  require_open(state);
  qa_cvar_registry *cvars = state->options.cvars;
  print_text(state, "\n------- Input Initialization -------\n");
  qa_register_source_input_settings(cvars);
  size_t applied_count = 0;
  qa_cvar_snapshot *applied =
      qa_cvar_registry_apply_latched(cvars, "in_joystick", &applied_count);
  for (size_t i = 0; i < applied_count; i++) qa_cvar_read_free(&applied[i]);
  free(applied);
  applied =
      qa_cvar_registry_apply_latched(cvars, "in_joystickProfile", &applied_count);
  for (size_t i = 0; i < applied_count; i++) qa_cvar_read_free(&applied[i]);
  free(applied);
  qa_cvar_snapshot snapshot;
  cvar_snapshot(state, "in_joystickProfile", &snapshot);
  const char *profile = snapshot.value != NULL ? snapshot.value : "";
  if (strcmp(profile, "linux") != 0 && strcmp(profile, "windows") != 0) {
    qa_cvar_read_free(&snapshot);
    qa_input_error("in_joystickProfile must be linux or windows");
  }
  state->profile = strcmp(profile, "windows") == 0 ? QA_SOURCE_JOYSTICK_WINDOWS
                                                   : QA_SOURCE_JOYSTICK_LINUX;
  qa_cvar_read_free(&snapshot);
  cvar_snapshot(state, "in_mouse", &snapshot);
  state->mouse_available = snapshot.numeric_value != 0;
  qa_cvar_read_free(&snapshot);
  /* The source abandons the old descriptor but retains
   * IN_JoyMove's static axis state. */
  bool had_joystick = state->has_joystick;
  qa_sdl_joystick previous = state->joystick;
  state->has_joystick = false;
  state->event_count = 0;
  if (had_joystick) qa_sdl_joystick_close(&previous);
  cvar_snapshot(state, "in_joystick", &snapshot);
  double enabled = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  if (enabled != 0) {
    if (state->profile == QA_SOURCE_JOYSTICK_WINDOWS) {
      qa_source_joystick_state_clear(&state->joystick_state);
    }
    qa_sdl_joystick joystick;
    memset(&joystick, 0, sizeof joystick);
    bool found = qa_sdl_joystick_open_first(joystick_print, state,
                                             sdl_profile(state->profile), &joystick);
    if (!found) {
      print_text(state, "No joystick found.\n");
    } else {
      state->joystick = joystick;
      state->has_joystick = true;
      char line[1024];
      snprintf(line, sizeof line,
               "Joystick SDL instance %d found\nName:    %s\nAxes:    %d\n"
               "Buttons: %d\n",
               joystick.instance, joystick.name != NULL ? joystick.name : "",
               joystick.axes, joystick.buttons);
      print_text(state, line);
      /* Linux discards JS_EVENT_INIT; Windows samples current
       * values in each frame. */
      size_t polled = 0;
      qa_sdl_event *events = qa_sdl_joystick_poll_events(
          &state->joystick, sdl_profile(state->profile), &polled);
      free(events);
    }
  } else {
    print_text(state, "Joystick is not active.\n");
  }
  print_text(state, "------------------------------------\n");
}

void qa_source_input_restart(qa_source_input_state *state) {
  require_open(state);
  state->mouse_available = false;
  qa_source_input_initialize(state);
}

static bool event_instance(const qa_sdl_event *event, int32_t *out) {
  switch (event->kind) {
    case QA_SDL_EVENT_JOYSTICK_AXIS:
      *out = event->as.joystick_axis.instance;
      return true;
    case QA_SDL_EVENT_JOYSTICK_HAT:
      *out = event->as.joystick_hat.instance;
      return true;
    case QA_SDL_EVENT_JOYSTICK_BUTTON:
      *out = event->as.joystick_button.instance;
      return true;
    case QA_SDL_EVENT_JOYSTICK_REMOVED:
      *out = event->as.joystick_removed.instance;
      return true;
    default:
      return false;
  }
}

void qa_source_input_queue_joystick_event(qa_source_input_state *state,
                                          const qa_sdl_event *event) {
  require_open(state);
  int32_t instance = 0;
  if (!state->has_joystick || !event_instance(event, &instance) ||
      instance != state->joystick.instance) {
    return;
  }
  if (state->event_count == state->event_capacity) {
    size_t grown = state->event_capacity == 0 ? 8 : state->event_capacity * 2;
    qa_sdl_event *events =
        (qa_sdl_event *)realloc(state->events, grown * sizeof *events);
    if (events == NULL) return;
    state->events = events;
    state->event_capacity = grown;
  }
  state->events[state->event_count++] = *event;
}

void qa_source_input_joystick_frame(qa_source_input_state *state,
                                    qa_queue_key_fn queue_key, void *queue_key_ctx,
                                    qa_queue_mouse_fn queue_mouse,
                                    void *queue_mouse_ctx) {
  require_open(state);
  if (!state->has_joystick) return;
  size_t polled_count = 0;
  qa_sdl_event *polled = qa_sdl_joystick_poll_events(
      &state->joystick, sdl_profile(state->profile), &polled_count);
  size_t queued = state->event_count;
  size_t total = queued + polled_count;
  qa_sdl_event *events =
      (qa_sdl_event *)calloc(total != 0 ? total : 1, sizeof *events);
  if (events == NULL) {
    free(polled);
    return;
  }
  if (queued != 0) memcpy(events, state->events, queued * sizeof *events);
  if (polled_count != 0) {
    memcpy(events + queued, polled, polled_count * sizeof *events);
  }
  free(polled);
  state->event_count = 0;
  if (state->profile == QA_SOURCE_JOYSTICK_WINDOWS) {
    qa_cvar_snapshot snapshot;
    cvar_snapshot(state, "in_debugjoystick", &snapshot);
    double debug = snapshot.integer_value;
    qa_cvar_read_free(&snapshot);
    bool removed = false;
    for (size_t i = 0; i < total; i++) {
      if (events[i].kind == QA_SDL_EVENT_JOYSTICK_REMOVED) removed = true;
    }
    if (debug != 0 && !removed) {
      char *row = qa_windows_joystick_debug(events, total);
      if (row != NULL) print_text(state, row);
      free(row);
    }
  }
  for (size_t i = 0; i < total; i++) {
    if (!state->has_joystick) break;
    const qa_sdl_event *event = &events[i];
    switch (event->kind) {
      case QA_SDL_EVENT_JOYSTICK_BUTTON:
        qa_source_joystick_state_button(
            &state->joystick_state, event->as.joystick_button.button,
            event->as.joystick_button.down, queue_key, queue_key_ctx,
            state->profile == QA_SOURCE_JOYSTICK_WINDOWS);
        break;
      case QA_SDL_EVENT_JOYSTICK_AXIS:
        qa_source_joystick_state_axis(&state->joystick_state,
                                       event->as.joystick_axis.axis,
                                       event->as.joystick_axis.value);
        break;
      case QA_SDL_EVENT_JOYSTICK_HAT:
        qa_source_joystick_state_pov(&state->joystick_state,
                                      event->as.joystick_hat.hat,
                                      event->as.joystick_hat.value);
        break;
      case QA_SDL_EVENT_JOYSTICK_REMOVED:
        print_text(state, "SDL joystick disconnected.\n");
        qa_sdl_joystick_close(&state->joystick);
        state->has_joystick = false;
        qa_source_joystick_state_remove_device(&state->joystick_state, queue_key,
                                                queue_key_ctx);
        break;
      default:
        break;
    }
  }
  free(events);
  double threshold = 0;
  const double *threshold_ptr = NULL;
  if (state->has_joystick) {
    qa_cvar_snapshot snapshot;
    cvar_snapshot(state, "joy_threshold", &snapshot);
    threshold = snapshot.numeric_value;
    qa_cvar_read_free(&snapshot);
    threshold_ptr = &threshold;
  }
  if (state->profile == QA_SOURCE_JOYSTICK_WINDOWS) {
    if (queue_mouse == NULL) {
      qa_input_error("Windows joystick input requires a mouse event consumer");
    }
    qa_cvar_snapshot snapshot;
    cvar_snapshot(state, "in_joyBallScale", &snapshot);
    double ball_scale = snapshot.numeric_value;
    qa_cvar_read_free(&snapshot);
    qa_source_joystick_state_windows_frame(
        &state->joystick_state, threshold_ptr,
        state->has_joystick ? state->joystick.axes : 0, ball_scale, queue_key,
        queue_key_ctx, queue_mouse, queue_mouse_ctx);
  } else {
    qa_source_joystick_state_frame(&state->joystick_state, threshold_ptr, queue_key,
                                    queue_key_ctx);
  }
}

void qa_source_input_release_joystick_state(qa_source_input_state *state,
                                            double time, qa_queue_key_fn queue_key,
                                            void *queue_ctx) {
  require_open(state);
  qa_source_joystick_state_release(&state->joystick_state, time, queue_key,
                                    queue_ctx);
}

void qa_source_input_close(qa_source_input_state *state) {
  if (state->closed) return;
  state->closed = true;
  state->mouse_available = false;
  bool had_joystick = state->has_joystick;
  qa_sdl_joystick joystick = state->joystick;
  state->has_joystick = false;
  state->event_count = 0;
  qa_source_joystick_state_clear(&state->joystick_state);
  if (had_joystick) qa_sdl_joystick_close(&joystick);
}
