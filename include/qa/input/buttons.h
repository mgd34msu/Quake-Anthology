/*
 * Port of src/input/buttons.ts: Quake cl_input.c kbutton state,
 * with Q2/Q3 millisecond sampling. Source names are copied (the
 * TS Set retains the JS strings). GPL-2.0-or-later.
 */
#ifndef QA_INPUT_BUTTONS_H
#define QA_INPUT_BUTTONS_H

#include <stdbool.h>
#include <stddef.h>

/** TS ButtonTiming: "q1" | "q2" | "q3". */
typedef enum qa_button_timing {
  QA_BUTTON_TIMING_Q1, QA_BUTTON_TIMING_Q2, QA_BUTTON_TIMING_Q3
} qa_button_timing;

typedef struct qa_input_button {
  char **sources;
  size_t source_count;
  size_t source_capacity;
  double down_time;
  double milliseconds;
  bool impulse_down;
  bool impulse_up;
} qa_input_button;

void qa_input_button_init(qa_input_button *button);
void qa_input_button_destroy(qa_input_button *button);
bool qa_input_button_active(const qa_input_button *button);
bool qa_input_button_pressed(const qa_input_button *button);
/* False only on allocation failure. */
bool qa_input_button_down(qa_input_button *button, const char *source,
                          double time_milliseconds);
/* missing_time_milliseconds mirrors the TS default of 10. */
void qa_input_button_up(qa_input_button *button, const char *source,
                        double time_milliseconds,
                        double missing_time_milliseconds);
void qa_input_button_release(qa_input_button *button, double time_milliseconds);
double qa_input_button_sample(qa_input_button *button, qa_button_timing timing,
                              double now_milliseconds, double frame_milliseconds);
void qa_input_button_clear(qa_input_button *button);

#endif
