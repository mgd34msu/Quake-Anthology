/* Port of src/input/buttons.ts. GPL-2.0-or-later. */
#include "qa/input/buttons.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/input/support.h"
#include "qa/foundation/js.h"

void qa_input_button_init(qa_input_button *button) {
  memset(button, 0, sizeof *button);
}

void qa_input_button_destroy(qa_input_button *button) {
  for (size_t i = 0; i < button->source_count; i++) free(button->sources[i]);
  free(button->sources);
  memset(button, 0, sizeof *button);
}

bool qa_input_button_active(const qa_input_button *button) {
  return button->source_count != 0;
}

bool qa_input_button_pressed(const qa_input_button *button) {
  return button->impulse_down;
}

static size_t find_source(const qa_input_button *button, const char *source) {
  for (size_t i = 0; i < button->source_count; i++) {
    if (strcmp(button->sources[i], source) == 0) return i;
  }
  return button->source_count;
}

bool qa_input_button_down(qa_input_button *button, const char *source,
                          double time_milliseconds) {
  if (find_source(button, source) != button->source_count) return true;
  bool active = qa_input_button_active(button);
  if (button->source_count == button->source_capacity) {
    size_t grown = button->source_capacity == 0 ? 4 : button->source_capacity * 2;
    char **sources = (char **)realloc(button->sources, grown * sizeof *sources);
    if (sources == NULL) return false;
    button->sources = sources;
    button->source_capacity = grown;
  }
  char *copy = qa_input_dup(source);
  if (copy == NULL) return false;
  button->sources[button->source_count++] = copy;
  if (active) return true;
  button->down_time = time_milliseconds;
  button->impulse_down = true;
  return true;
}

void qa_input_button_up(qa_input_button *button, const char *source,
                        double time_milliseconds,
                        double missing_time_milliseconds) {
  size_t index = find_source(button, source);
  if (index == button->source_count) return;
  free(button->sources[index]);
  memmove(&button->sources[index], &button->sources[index + 1],
          (button->source_count - index - 1) * sizeof *button->sources);
  button->source_count--;
  if (qa_input_button_active(button)) return;
  button->milliseconds += time_milliseconds == 0
                               ? missing_time_milliseconds
                               : qa_js_max(0, time_milliseconds - button->down_time);
  button->impulse_up = true;
}

void qa_input_button_release(qa_input_button *button, double time_milliseconds) {
  while (button->source_count != 0) {
    /* up() frees the source; iterate from the head like the TS Set. */
    qa_input_button_up(button, button->sources[0], time_milliseconds, 10);
  }
}

double qa_input_button_sample(qa_input_button *button, qa_button_timing timing,
                              double now_milliseconds, double frame_milliseconds) {
  double result;
  if (timing == QA_BUTTON_TIMING_Q1) {
    result = button->impulse_down && button->impulse_up
                 ? qa_input_button_active(button) ? 0.75 : 0.25
             : button->impulse_down
                 ? qa_input_button_active(button) ? 0.5 : 0
             : qa_input_button_active(button) ? 1 : 0;
  } else {
    if (qa_input_button_active(button)) {
      button->milliseconds += button->down_time == 0
                                   ? now_milliseconds
                                   : qa_js_max(0, now_milliseconds - button->down_time);
      button->down_time = now_milliseconds;
    }
    double value = timing == QA_BUTTON_TIMING_Q3
                       ? (double)(float)((double)(float)button->milliseconds /
                                         (double)(float)frame_milliseconds)
                       : button->milliseconds / frame_milliseconds;
    result = qa_js_max(0, qa_js_min(1, value));
  }
  button->milliseconds = 0;
  button->impulse_down = false;
  button->impulse_up = false;
  return result;
}

void qa_input_button_clear(qa_input_button *button) {
  for (size_t i = 0; i < button->source_count; i++) free(button->sources[i]);
  button->source_count = 0;
  button->milliseconds = 0;
  button->down_time = 0;
  button->impulse_down = false;
  button->impulse_up = false;
}
