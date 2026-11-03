/* Port of src/input/source-joystick.ts. GPL-2.0-or-later. */
#include "qa/input/source_joystick.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/key_codes.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

static void pad_left(const char *text, size_t width, char *out, size_t out_size) {
  size_t length = strlen(text);
  size_t padding = length >= width ? 0 : width - length;
  size_t at = 0;
  for (size_t i = 0; i < padding && at + 1 < out_size; i++) out[at++] = ' ';
  for (size_t i = 0; i < length && at + 1 < out_size; i++) out[at++] = text[i];
  out[at] = '\0';
}

char *qa_windows_joystick_debug(const qa_sdl_event *events, size_t count) {
  uint32_t buttons = 0;
  double pov = 65535;
  int16_t axes[6] = {0, 0, 0, 0, 0, 0};
  for (size_t i = 0; i < count; i++) {
    const qa_sdl_event *event = &events[i];
    if (event->kind == QA_SDL_EVENT_JOYSTICK_BUTTON &&
        event->as.joystick_button.button < 32) {
      if (event->as.joystick_button.down) {
        buttons |= 1u << event->as.joystick_button.button;
      } else {
        buttons &= ~(1u << event->as.joystick_button.button);
      }
    } else if (event->kind == QA_SDL_EVENT_JOYSTICK_AXIS &&
               event->as.joystick_axis.axis < 6) {
      axes[event->as.joystick_axis.axis] = event->as.joystick_axis.value;
    } else if (event->kind == QA_SDL_EVENT_JOYSTICK_HAT &&
               event->as.joystick_hat.hat == 0) {
      switch (event->as.joystick_hat.value) {
        case 1: pov = 0; break;
        case 3: pov = 4500; break;
        case 2: pov = 9000; break;
        case 6: pov = 13500; break;
        case 4: pov = 18000; break;
        case 12: pov = 22500; break;
        case 8: pov = 27000; break;
        case 9: pov = 31500; break;
        default: pov = 65535;
      }
    }
  }
  char fields[8][32];
  char raw[32];
  snprintf(raw, sizeof raw, "%x", buttons);
  pad_left(raw, 8, fields[0], sizeof fields[0]);
  snprintf(raw, sizeof raw, "%s", QA_JS_NUMBER(pov));
  pad_left(raw, 5, fields[1], sizeof fields[1]);
  for (int axis = 0; axis < 6; axis++) {
    if (axis < 4) {
      /* Math.fround(value / 32768).toFixed(2) */
      float fraction = (float)((double)axes[axis] / 32768);
      qa_js_to_fixed((double)fraction, 2, raw, sizeof raw); /* exact ties round up */
      pad_left(raw, 5, fields[2 + axis], sizeof fields[0]);
    } else {
      snprintf(raw, sizeof raw, "%d", axes[axis]);
      pad_left(raw, 6, fields[2 + axis], sizeof fields[0]);
    }
  }
  char joined[512];
  snprintf(joined, sizeof joined, "%s %s %s %s %s %s %s %s\n", fields[0], fields[1],
           fields[2], fields[3], fields[4], fields[5], fields[6], fields[7]);
  return qa_input_dup(joined);
}

double qa_sdl_joystick_axes(const int16_t *values, size_t count, double threshold) {
  int32_t axes = 0;
  for (size_t index = 0; index < count; index++) {
    if (index >= 16) break;
    double fraction = (double)(float)((double)values[index] / 32767);
    if (fraction < -threshold) axes |= (int32_t)(1u << (index * 2));
    else if (fraction > threshold) axes |= (int32_t)(1u << (index * 2 + 1));
  }
  return axes;
}

static const int joystick_keys[16] = {
    QA_KEY_LEFT, QA_KEY_RIGHT, QA_KEY_UP, QA_KEY_DOWN,
    QA_KEY_JOY16, QA_KEY_JOY17, QA_KEY_JOY18, QA_KEY_JOY19,
    QA_KEY_JOY20, QA_KEY_JOY21, QA_KEY_JOY22, QA_KEY_JOY23,
    QA_KEY_JOY24, QA_KEY_JOY25, QA_KEY_JOY26, QA_KEY_JOY27};

void qa_source_joystick_state_init(qa_source_joystick_state *state) {
  memset(state, 0, sizeof *state);
}

static bool held_has(const qa_source_joystick_state *state, double key) {
  for (size_t i = 0; i < state->held_button_count; i++) {
    if (state->held_buttons[i] == key) return true;
  }
  return false;
}

static void held_add(qa_source_joystick_state *state, double key) {
  if (!held_has(state, key) && state->held_button_count < 256) {
    state->held_buttons[state->held_button_count++] = key;
  }
}

static void held_remove(qa_source_joystick_state *state, double key) {
  for (size_t i = 0; i < state->held_button_count; i++) {
    if (state->held_buttons[i] == key) {
      memmove(&state->held_buttons[i], &state->held_buttons[i + 1],
              (state->held_button_count - i - 1) * sizeof *state->held_buttons);
      state->held_button_count--;
      return;
    }
  }
}

void qa_source_joystick_state_button(qa_source_joystick_state *state, double button,
                                      bool down, qa_queue_key_fn queue_key,
                                      void *queue_ctx, bool transitions_only) {
  if (button != trunc(button) || button < 0 || button > 255) {
    qa_range_error("Joystick button requires an unsigned byte");
  }
  double key = QA_KEY_JOY1 + button;
  if (transitions_only && held_has(state, key) == down) return;
  queue_key(queue_ctx, key, down, 0);
  if (down) held_add(state, key);
  else held_remove(state, key);
}

void qa_source_joystick_state_axis(qa_source_joystick_state *state, double axis,
                                   double value) {
  /* Typed-array stores outside 0..15 (or fractional) drop silently. */
  if (axis == trunc(axis) && axis >= 0 && axis < 16) {
    state->axes[(int)axis] = qa_js_to_int16(value); /* Int16Array store: NaN and infinities to 0 */
  }
}

void qa_source_joystick_state_pov(qa_source_joystick_state *state, double hat,
                                  double value) {
  if (hat == 0) state->hat = value;
}

static void publish_axes(qa_source_joystick_state *state, int32_t axes,
                         qa_queue_key_fn queue_key, void *queue_ctx) {
  for (int bit = 0; bit < 16; bit++) {
    int32_t mask = (int32_t)(1u << bit);
    if ((axes & mask) != (state->old_axes & mask)) {
      queue_key(queue_ctx, joystick_keys[bit], (axes & mask) != 0, 0);
    }
  }
  state->old_axes = axes;
}

void qa_source_joystick_state_frame(qa_source_joystick_state *state,
                                    const double *threshold,
                                    qa_queue_key_fn queue_key, void *queue_ctx) {
  int32_t axes = threshold == NULL
                     ? 0
                     : (int32_t)qa_sdl_joystick_axes(state->axes, 16, *threshold);
  publish_axes(state, axes, queue_key, queue_ctx);
}

void qa_source_joystick_state_windows_frame(
    qa_source_joystick_state *state, const double *threshold, double axis_count,
    double ball_scale, qa_queue_key_fn queue_key, void *queue_key_ctx,
    qa_queue_mouse_fn queue_mouse, void *queue_mouse_ctx) {
  int32_t axes = 0;
  if (threshold != NULL) {
    double limit = qa_js_min(axis_count, 4);
    for (int index = 0; index < limit; index++) {
      double fraction = (double)(float)((double)state->axes[index] / 32768);
      if (fraction < -*threshold) axes |= (int32_t)(1u << (index * 2));
      else if (fraction > *threshold) axes |= (int32_t)(1u << (index * 2 + 1));
    }
    /* SDL hat bits are up/right/down/left; source accepts cardinal POV only. */
    if (state->hat == 1) axes |= 1 << 12;
    else if (state->hat == 4) axes |= 1 << 13;
    else if (state->hat == 2) axes |= 1 << 14;
    else if (state->hat == 8) axes |= 1 << 15;
  }
  publish_axes(state, axes, queue_key, queue_key_ctx);
  if (threshold != NULL && axis_count >= 6) {
    int16_t u = state->axes[4], v = state->axes[5];
    float scale = (float)ball_scale;
    double dx = trunc((double)(float)((double)u * (double)scale));
    double dy = trunc((double)(float)((double)v * (double)scale));
    if (dx != 0 || dy != 0) queue_mouse(queue_mouse_ctx, dx, dy, 0);
  }
}

void qa_source_joystick_state_remove_device(qa_source_joystick_state *state,
                                            qa_queue_key_fn queue_key,
                                            void *queue_ctx) {
  for (size_t i = 0; i < state->held_button_count; i++) {
    queue_key(queue_ctx, state->held_buttons[i], false, 0);
  }
  state->held_button_count = 0;
  memset(state->axes, 0, sizeof state->axes);
  state->hat = 0;
}

void qa_source_joystick_state_release(qa_source_joystick_state *state, double time,
                                      qa_queue_key_fn queue_key, void *queue_ctx) {
  double held[272];
  size_t count = 0;
  for (size_t i = 0; i < state->held_button_count; i++) held[count++] = state->held_buttons[i];
  for (int bit = 0; bit < 16; bit++) {
    if ((state->old_axes & (int32_t)(1u << bit)) != 0) {
      double key = joystick_keys[bit];
      bool present = false;
      for (size_t i = 0; i < count; i++) present = present || held[i] == key;
      if (!present) held[count++] = key;
    }
  }
  for (size_t i = 0; i < count; i++) queue_key(queue_ctx, held[i], false, time);
  qa_source_joystick_state_clear(state);
}

void qa_source_joystick_state_clear(qa_source_joystick_state *state) {
  state->held_button_count = 0;
  memset(state->axes, 0, sizeof state->axes);
  state->old_axes = 0;
  state->hat = 0;
}
