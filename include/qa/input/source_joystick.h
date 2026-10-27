/*
 * Port of src/input/source-joystick.ts: source joystick profiles
 * from Quake III linux_joystick.c and win_input.c. A NULL
 * threshold pointer is the TS null. GPL-2.0-or-later.
 */
#ifndef QA_INPUT_SOURCE_JOYSTICK_H
#define QA_INPUT_SOURCE_JOYSTICK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/input/support.h"
#include "qa/platform/sdl.h"

/** TS SourceJoystickProfile. */
typedef enum qa_source_joystick_profile {
  QA_SOURCE_JOYSTICK_LINUX, QA_SOURCE_JOYSTICK_WINDOWS
} qa_source_joystick_profile;

/* IN_JoyMove's diagnostic row. Malloc'd; caller frees. */
char *qa_windows_joystick_debug(const qa_sdl_event *events, size_t count);
double qa_sdl_joystick_axes(const int16_t *values, size_t count, double threshold);

typedef struct qa_source_joystick_state {
  double held_buttons[256];
  size_t held_button_count;
  int16_t axes[16];
  int32_t old_axes;
  double hat;
} qa_source_joystick_state;

void qa_source_joystick_state_init(qa_source_joystick_state *state);
void qa_source_joystick_state_button(qa_source_joystick_state *state, double button,
                                      bool down, qa_queue_key_fn queue_key,
                                      void *queue_ctx, bool transitions_only);
void qa_source_joystick_state_axis(qa_source_joystick_state *state, double axis,
                                   double value);
void qa_source_joystick_state_pov(qa_source_joystick_state *state, double hat,
                                  double value);
void qa_source_joystick_state_frame(qa_source_joystick_state *state,
                                    const double *threshold,
                                    qa_queue_key_fn queue_key, void *queue_ctx);
void qa_source_joystick_state_windows_frame(
    qa_source_joystick_state *state, const double *threshold, double axis_count,
    double ball_scale, qa_queue_key_fn queue_key, void *queue_key_ctx,
    qa_queue_mouse_fn queue_mouse, void *queue_mouse_ctx);
void qa_source_joystick_state_remove_device(qa_source_joystick_state *state,
                                            qa_queue_key_fn queue_key,
                                            void *queue_ctx);
void qa_source_joystick_state_release(qa_source_joystick_state *state, double time,
                                      qa_queue_key_fn queue_key, void *queue_ctx);
void qa_source_joystick_state_clear(qa_source_joystick_state *state);

#endif
