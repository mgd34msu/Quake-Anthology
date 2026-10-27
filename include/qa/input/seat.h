/*
 * Port of src/input/seat.ts: per-seat binding and CL_KeyState
 * flow derived from Quake I/II/III cl_input.c. The seat owns deep
 * copies of its bindings and focus; samples own their button
 * rows. The uiEvent closure and bindUiEvent's returned cleanup
 * become function pointers plus ctx and a seat-owned binding
 * node. GPL-2.0-or-later.
 */
#ifndef QA_INPUT_SEAT_H
#define QA_INPUT_SEAT_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/ui.h"
#include "qa/core/commands/index.h"
#include "qa/input/binding_store.h"
#include "qa/input/buttons.h"
#include "qa/input/gamepad.h"

/** TS SourceAction: InputAction spellings plus "turn-left",
 * "turn-right", "look-up", "look-down", "strafe", "mlook",
 * "klook", "holster", "button0".."button14". */
typedef const char *qa_source_action;

/** TS ButtonSample. action is an owned copy in samples. */
typedef struct qa_button_sample {
  char *action;
  double fraction;
  bool active;
  bool pressed;
} qa_button_sample;

/** TS SeatInputSample. focus strings borrow seat storage. */
typedef struct qa_seat_input_sample {
  qa_seat_id seat;
  double now_milliseconds;
  double frame_milliseconds;
  qa_seat_input_focus focus;
  qa_button_sample *buttons;
  size_t button_count;
  qa_vec2 mouse;
  qa_vec2 gamepad_move;
  qa_vec2 gamepad_look_degrees;
  double impulse;
  double any_key_down;
} qa_seat_input_sample;

void qa_seat_input_sample_dispose(qa_seat_input_sample *sample);

/** TS SeatInputOptions.uiEvent. */
typedef bool (*qa_seat_ui_event_fn)(void *ctx, const qa_seat_input_event *event,
                                    qa_seat_input_focus focus);
/** TS () => number clock for bindUiEvent. */
typedef double (*qa_seat_now_fn)(void *ctx);

/** TS SeatInputOptions. gamepad NULL selects the default tuning. */
typedef struct qa_seat_input_options {
  qa_seat_id seat;
  qa_command_dialect dialect;
  qa_command_context context;
  qa_command_buffer *commands;
  qa_seat_ui_event_fn ui_event;
  void *ui_event_ctx;
  const qa_gamepad_tuning *gamepad;
} qa_seat_input_options;

typedef struct qa_seat_input qa_seat_input;
typedef struct qa_seat_ui_binding qa_seat_ui_binding;

/* NULL only on allocation failure; a foreign context aborts with
 * the TS Error message. */
qa_seat_input *qa_seat_input_create(const qa_seat_input_options *options);
void qa_seat_input_destroy(qa_seat_input *input);

qa_command_dialect qa_seat_input_dialect(const qa_seat_input *input);
qa_seat_id qa_seat_input_seat(const qa_seat_input *input);
qa_gamepad_input *qa_seat_input_gamepad(qa_seat_input *input);
qa_seat_input_focus qa_seat_input_get_focus(const qa_seat_input *input);
bool qa_seat_input_focused(const qa_seat_input *input);
/* Borrowed seat-owned array in insertion order. */
const qa_input_binding *qa_seat_input_bindings(const qa_seat_input *input,
                                               size_t *out_count);
bool qa_seat_input_is_down(const qa_seat_input *input,
                           const qa_physical_input *physical);
/* False only on allocation failure. */
bool qa_seat_input_bind(qa_seat_input *input, const qa_input_binding *binding);
void qa_seat_input_unbind(qa_seat_input *input, const qa_physical_input *physical);
void qa_seat_input_unbind_all(qa_seat_input *input);
/* Borrowed target, or NULL for the TS null. */
const qa_input_binding_target *qa_seat_input_binding(
    const qa_seat_input *input, const qa_physical_input *physical);
void qa_seat_input_remap_controller_bindings(qa_seat_input *input, double device);
void qa_seat_input_set_focus(qa_seat_input *input, qa_seat_input_focus focus,
                             double now_milliseconds);
/* The returned node is seat-owned; pass it to
 * qa_seat_input_unbind_ui_event for the TS cleanup closure. */
qa_seat_ui_binding *qa_seat_input_bind_ui_event(qa_seat_input *input,
                                                qa_seat_ui_event_fn callback,
                                                void *callback_ctx,
                                                qa_seat_now_fn now, void *now_ctx);
void qa_seat_input_unbind_ui_event(qa_seat_input *input,
                                   qa_seat_ui_binding *binding);
bool qa_seat_input_has_held_input(const qa_seat_input *input);
void qa_seat_input_set_profile(qa_seat_input *input, qa_command_dialect dialect);
void qa_seat_input_set_impulse(qa_seat_input *input, double value);
/* Creates the action's button on first use, as the TS does. */
qa_input_button *qa_seat_input_button(qa_seat_input *input, qa_source_action action);
void qa_seat_input_command_button(qa_seat_input *input, qa_source_action action,
                                  const char *key, bool down,
                                  double time_milliseconds);
bool qa_seat_input_input(qa_seat_input *input, const qa_seat_input_event *event);
void qa_seat_input_gyro(qa_seat_input *input, qa_vec3 sample,
                        double time_milliseconds);
void qa_seat_input_release_device(qa_seat_input *input, double device, double time);
/* commands NULL selects the seat's command buffer (the TS default). */
void qa_seat_input_release(qa_seat_input *input, double time,
                           qa_command_buffer *commands);
/* False only on allocation failure; out owns its button rows. */
bool qa_seat_input_take_sample(qa_seat_input *input, double now_milliseconds,
                          double frame_milliseconds, qa_seat_input_sample *out);

/** TS registerInputCommands lookup; NULL is the TS null. */
typedef qa_seat_input *(*qa_seat_lookup_fn)(void *ctx, qa_seat_id seat);
/** TS clientScores hook. */
typedef bool (*qa_client_scores_fn)(void *ctx, qa_command_invocation *command);
typedef struct qa_input_commands_registration qa_input_commands_registration;
/* NULL only on allocation failure. client_scores may be NULL. */
qa_input_commands_registration *qa_register_input_commands(
    qa_command_buffer *commands, qa_seat_lookup_fn lookup, void *lookup_ctx,
    qa_client_scores_fn client_scores, void *client_scores_ctx);
void qa_input_commands_registration_destroy(
    qa_input_commands_registration *registration);

#endif
