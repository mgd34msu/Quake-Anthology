/* Port of src/app/bootstrap/input.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/input.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/contracts/execution.h"
#include "qa/contracts/time.h"
#include "qa/foundation/support.h"
static qa_command_dialect dialect_of_clock(qa_clock_profile_kind kind) {
  switch (kind) {
    case QA_CLOCK_Q1_NETQUAKE: return QA_COMMAND_DIALECT_Q1_NETQUAKE;
    case QA_CLOCK_Q1_QUAKEWORLD: return QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
    case QA_CLOCK_Q2_CLASSIC: return QA_COMMAND_DIALECT_Q2_CLASSIC;
    case QA_CLOCK_Q2_RERELEASE: return QA_COMMAND_DIALECT_Q2_RERELEASE;
    default: return QA_COMMAND_DIALECT_Q3;
  }
}
qa_command_dialect qa_app_movement_dialect(const qa_app_options *options,
    const qa_executable_recipe *recipe) {
  if (recipe != NULL) {
    for (size_t i = 0; i < recipe->execution_count; i++) {
      const qa_execution_module *module = &recipe->execution[i];
      if (module->kind != QA_EXECUTION_MODULE_NATIVE || strcmp(module->role, "server-game") != 0) continue;
      if (module->api.kind == QA_GAME_API_Q2_GAME) {
        if (module->api.as.q2_game.kind == QA_Q2_GAME_API_RERELEASE) return QA_COMMAND_DIALECT_Q2_RERELEASE;
        return QA_COMMAND_DIALECT_Q2_CLASSIC;
      }
    }
    const qa_provider_timing *timing = NULL;
    for (size_t i = 0; i < recipe->timing_count; i++)
      if (strcmp(recipe->timing[i].provider, recipe->movement.provider) == 0) timing = &recipe->timing[i];
    if (timing == NULL) qa_range_error("Recipe has no timing for %s", recipe->movement.provider);
    return dialect_of_clock(timing->clock.kind);
  }
  if (options != NULL && strcmp(options->network_kind, "qw-client") == 0)
    return QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
  qa_game_family movement = options != NULL ? options->movement : QA_GAME_FAMILY_Q1;
  return movement == QA_GAME_FAMILY_Q1 ? QA_COMMAND_DIALECT_Q1_NETQUAKE
      : movement == QA_GAME_FAMILY_Q2 ? QA_COMMAND_DIALECT_Q2_CLASSIC : QA_COMMAND_DIALECT_Q3;
}
typedef struct seat_ui_entry { qa_seat_id seat; qa_app_input_ui ui; bool present; } seat_ui_entry;
typedef struct seat_selection { qa_seat_id seat; qa_app_q3_command_selection selection; bool present; } seat_selection;
struct qa_app_application_input {
  qa_app_local_input *locals; size_t local_count;
  qa_command_dialect dialect;
  qa_cvar_registry *cvar_owner;
  qa_app_input_host host;
  double sequence;
  seat_ui_entry *uis; size_t ui_count, ui_capacity;
  seat_selection *selections; size_t selection_count, selection_capacity;
  bool closed;
};
struct qa_app_input_ui_attachment { qa_app_application_input *input; qa_seat_id seat; };
qa_app_application_input *qa_app_application_input_create(
    const qa_app_local_input *locals, size_t local_count,
    qa_command_dialect dialect, qa_cvar_registry *cvar_owner, qa_app_input_host host) {
  if (local_count == 0) qa_range_error("Native input requires at least one local player");
  qa_app_application_input *input = calloc(1, sizeof *input);
  if (input == NULL) qa_range_error("Out of memory");
  input->locals = calloc(local_count, sizeof *input->locals);
  if (input->locals == NULL) qa_range_error("Out of memory");
  memcpy(input->locals, locals, local_count * sizeof *locals);
  input->local_count = local_count;
  input->dialect = dialect;
  input->cvar_owner = cvar_owner;
  input->host = host;
  return input;
}
void qa_app_application_input_destroy(qa_app_application_input *input) {
  if (input == NULL) return;
  free(input->locals); free(input->uis); free(input->selections);
  free(input);
}
qa_cvar_registry *qa_app_application_input_cvars(const qa_app_application_input *input) {
  return input->cvar_owner;
}
const qa_app_local_input *qa_app_application_input_locals(const qa_app_application_input *input, size_t *out_count) {
  *out_count = input->local_count;
  return input->locals;
}
double qa_app_application_input_local_player_capacity(qa_app_application_input *input) {
  return input->host.local_player_capacity != NULL
      ? input->host.local_player_capacity(input->host.ctx) : 4;
}
bool qa_app_application_input_can_remove_local_player(qa_app_application_input *input, qa_seat_id seat) {
  return input->local_count > 1
      && (input->host.can_remove_local_player == NULL
          || input->host.can_remove_local_player(input->host.ctx, seat));
}
double qa_app_application_input_next_command_sequence(const qa_app_application_input *input) {
  return input->sequence;
}
void qa_app_application_input_resume_commands(qa_app_application_input *input, double sequence) {
  if (sequence != trunc(sequence) || sequence < 0 || sequence > 9007199254740991.0)
    qa_range_error("Input command sequence must be a nonnegative safe integer");
  input->sequence = sequence;
}
static bool has_seat(qa_app_application_input *input, qa_seat_id seat) {
  for (size_t i = 0; i < input->local_count; i++)
    if (qa_same_seat(input->locals[i].player.seat, seat)) return true;
  return false;
}
void qa_app_application_input_set_q3_command_selection(qa_app_application_input *input,
    qa_seat_id seat, qa_app_q3_command_selection selection) {
  if (!has_seat(input, seat)) qa_range_error("Command selection has no local seat");
  for (size_t i = 0; i < input->selection_count; i++) {
    if (qa_same_seat(input->selections[i].seat, seat)) {
      input->selections[i].selection = selection;
      input->selections[i].present = true;
      return;
    }
  }
  if (input->selection_count == input->selection_capacity) {
    size_t cap = input->selection_capacity == 0 ? 4 : input->selection_capacity * 2;
    seat_selection *next = realloc(input->selections, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    input->selections = next; input->selection_capacity = cap;
  }
  input->selections[input->selection_count].seat = seat;
  input->selections[input->selection_count].selection = selection;
  input->selections[input->selection_count].present = true;
  input->selection_count++;
}
bool qa_app_application_input_q3_command_selection(const qa_app_application_input *input,
    qa_seat_id seat, qa_app_q3_command_selection *out) {
  for (size_t i = 0; i < input->selection_count; i++) {
    if (qa_same_seat(input->selections[i].seat, seat) && input->selections[i].present) {
      *out = input->selections[i].selection;
      return true;
    }
  }
  return false;
}
qa_app_input_ui_attachment *qa_app_application_input_attach_ui(qa_app_application_input *input,
    qa_seat_id seat, qa_app_input_ui ui) {
  if (!has_seat(input, seat)) qa_range_error("UI seat has no local input");
  for (size_t i = 0; i < input->ui_count; i++)
    if (input->uis[i].present && qa_same_seat(input->uis[i].seat, seat))
      qa_range_error("Seat UI is already attached");
  if (input->ui_count == input->ui_capacity) {
    size_t cap = input->ui_capacity == 0 ? 4 : input->ui_capacity * 2;
    seat_ui_entry *next = realloc(input->uis, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    input->uis = next; input->ui_capacity = cap;
  }
  input->uis[input->ui_count].seat = seat;
  input->uis[input->ui_count].ui = ui;
  input->uis[input->ui_count].present = true;
  input->ui_count++;
  qa_app_input_ui_attachment *attachment = calloc(1, sizeof *attachment);
  if (attachment == NULL) qa_range_error("Out of memory");
  attachment->input = input;
  attachment->seat = seat;
  return attachment;
}
void qa_app_input_ui_attachment_release(qa_app_input_ui_attachment *attachment) {
  if (attachment == NULL) return;
  qa_app_application_input *input = attachment->input;
  for (size_t i = 0; i < input->ui_count; i++)
    if (qa_same_seat(input->uis[i].seat, attachment->seat)) input->uis[i].present = false;
  free(attachment);
}
const char *qa_app_application_input_pump(qa_app_application_input *input, bool execute_commands) {
  return input->host.pump(input->host.ctx, execute_commands);
}
const char *qa_app_application_input_save_settings(qa_app_application_input *input) {
  return input->host.save_settings(input->host.ctx);
}
void qa_app_application_input_close(qa_app_application_input *input) {
  if (input == NULL || input->closed) return;
  input->closed = true;
  const char *error = input->host.close != NULL ? input->host.close(input->host.ctx) : NULL;
  if (error != NULL) qa_range_error("%s", error);
}
