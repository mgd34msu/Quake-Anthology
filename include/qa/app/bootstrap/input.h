/* Port of src/app/bootstrap/input.ts. The SDL/router/console
 * wiring rides the host below (landed input/platform/console
 * modules are composed by the composition root); movementDialect
 * and the seat/sequence guards are ported here. The private
 * constructor becomes create over prepared locals.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_INPUT_H
#define QA_APP_BOOTSTRAP_INPUT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/options.h"
#include "qa/contracts/common.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/ui.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
#include "qa/input/mouse_settings.h"
#include "qa/input/seat.h"
/* TS Q3CommandSelection. */
typedef struct qa_app_q3_command_selection { double weapon, sensitivity; } qa_app_q3_command_selection;
/* TS movementDialect. recipe NULL = TS undefined. Aborts when the
 * recipe has no timing for its movement provider. */
qa_command_dialect qa_app_movement_dialect(const qa_app_options *options,
    const qa_executable_recipe *recipe);
/* TS LocalPlayer / LocalInput minimal views. */
typedef struct qa_app_local_player {
  qa_seat_id seat;
  qa_actor_id actor;
} qa_app_local_player;
typedef struct qa_app_local_input {
  qa_app_local_player player;
  qa_seat_input *input;         /* borrowed */
  qa_cvar_registry *cvars;      /* borrowed, may be NULL */
  qa_mouse_settings *mouse;     /* borrowed, may be NULL */
} qa_app_local_input;
/* TS ApplicationInputCommandOwner. Pointers NULL = TS undefined. */
typedef struct qa_app_input_command_owner {
  qa_cvar_registry *movement;
  qa_cvar_registry *fallback;
  const void *scripts; /* borrowed ConsoleScriptFiles (other slice) */
  qa_cvar_registry *cvars;
  qa_command_buffer *commands;
  qa_command_cvar_routing routing;
  qa_mouse_settings *input_settings;
} qa_app_input_command_owner;
/* TS ApplicationInputUi. */
typedef struct qa_app_input_ui {
  void *ctx;
  bool (*input)(void *ctx, qa_seat_input_event event, qa_seat_input_focus focus);
  void (*close_menus)(void *ctx);
  qa_seat_input_sample (*sample)(void *ctx, qa_seat_input_sample input);
  void (*wheel)(void *ctx, const char *mode, bool down);
} qa_app_input_ui;
typedef struct qa_app_input_host {
  void *ctx;
  double (*local_player_capacity)(void *ctx);   /* may be NULL (-> 4) */
  bool (*can_remove_local_player)(void *ctx, qa_seat_id seat); /* may be NULL */
  const char *(*pump)(void *ctx, bool execute_commands);
  const char *(*build)(void *ctx, double elapsed_ms, double server_ms, double server_frame);
  const char *(*save_settings)(void *ctx);
  const char *(*close)(void *ctx);
} qa_app_input_host;
typedef struct qa_app_application_input qa_app_application_input;
/* Aborts with TS Error text for zero players / owner mismatch.
 * locals are copied (borrowed pointees). */
qa_app_application_input *qa_app_application_input_create(
    const qa_app_local_input *locals, size_t local_count,
    qa_command_dialect dialect, qa_cvar_registry *cvar_owner, qa_app_input_host host);
void qa_app_application_input_destroy(qa_app_application_input *input);
qa_cvar_registry *qa_app_application_input_cvars(const qa_app_application_input *input);
const qa_app_local_input *qa_app_application_input_locals(const qa_app_application_input *input, size_t *out_count);
double qa_app_application_input_local_player_capacity(qa_app_application_input *input);
bool qa_app_application_input_can_remove_local_player(qa_app_application_input *input, qa_seat_id seat);
double qa_app_application_input_next_command_sequence(const qa_app_application_input *input);
/* Aborts (RangeError) on a negative/non-integer sequence. */
void qa_app_application_input_resume_commands(qa_app_application_input *input, double sequence);
/* Aborts when the seat is not local (TS Error texts per method). */
void qa_app_application_input_set_q3_command_selection(qa_app_application_input *input,
    qa_seat_id seat, qa_app_q3_command_selection selection);
bool qa_app_application_input_q3_command_selection(const qa_app_application_input *input,
    qa_seat_id seat, qa_app_q3_command_selection *out);
typedef struct qa_app_input_ui_attachment qa_app_input_ui_attachment;
qa_app_input_ui_attachment *qa_app_application_input_attach_ui(qa_app_application_input *input,
    qa_seat_id seat, qa_app_input_ui ui);
void qa_app_input_ui_attachment_release(qa_app_input_ui_attachment *attachment);
const char *qa_app_application_input_pump(qa_app_application_input *input, bool execute_commands);
const char *qa_app_application_input_save_settings(qa_app_application_input *input);
void qa_app_application_input_close(qa_app_application_input *input);
#endif
