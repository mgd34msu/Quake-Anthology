/*
 * Port of src/input/source-input.ts: Quake III source
 * joystick/mouse lifecycle. Client-build input survives window
 * replacement and starts at Sys_Init, before Netchan_Init.
 * GPL-2.0-or-later.
 */
#ifndef QA_INPUT_SOURCE_INPUT_H
#define QA_INPUT_SOURCE_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/core/commands/text.h"
#include "qa/core/cvars/index.h"
#include "qa/input/source_joystick.h"
#include "qa/input/support.h"
#include "qa/platform/sdl.h"

/** TS SourceInputOptions. */
typedef struct qa_source_input_options {
  qa_cvar_registry *cvars;
  qa_command_print_fn print;
  void *print_ctx;
} qa_source_input_options;

typedef struct qa_source_input_state qa_source_input_state;

/* Inert construction; NULL only on allocation failure. */
qa_source_input_state *qa_source_input_create(qa_source_input_options options);
void qa_source_input_destroy(qa_source_input_state *state);
/* False is the TS null instance. */
bool qa_source_input_instance(const qa_source_input_state *state, int32_t *out);
bool qa_source_input_mouse_available(const qa_source_input_state *state);
bool qa_source_input_mouse_active(const qa_source_input_state *state);
double qa_source_input_mouse_reset_time(const qa_source_input_state *state);
void qa_source_input_set_mouse_active(qa_source_input_state *state, bool active);
void qa_source_input_set_mouse_reset_time(qa_source_input_state *state, double time);
qa_source_joystick_state *qa_source_input_joystick_state(
    qa_source_input_state *state);
void qa_source_input_initialize(qa_source_input_state *state);
void qa_source_input_restart(qa_source_input_state *state);
void qa_source_input_queue_joystick_event(qa_source_input_state *state,
                                          const qa_sdl_event *event);
/* queue_mouse may be NULL outside the windows profile. */
void qa_source_input_joystick_frame(qa_source_input_state *state,
                                    qa_queue_key_fn queue_key, void *queue_key_ctx,
                                    qa_queue_mouse_fn queue_mouse,
                                    void *queue_mouse_ctx);
void qa_source_input_release_joystick_state(qa_source_input_state *state,
                                            double time, qa_queue_key_fn queue_key,
                                            void *queue_ctx);
void qa_source_input_close(qa_source_input_state *state);

#endif
