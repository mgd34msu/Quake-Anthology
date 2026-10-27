/* Port of src/compat/qvm/cgame.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/cgame.h"

#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

void qa_qvm_cgame_init_module(qa_qvm_cgame *cgame, qa_seat_id seat,
                              const qa_qvm_module_options *options, qa_qvm_cgame_lifetime lifetime) {
  if (strcmp(options->artifact->role, "cgame") != 0)
    qa_range_error("QvmCgame requires a cgame artifact");
  cgame->seat = seat;
  cgame->lifetime = lifetime;
  cgame->phase_kind = QA_QVM_CGAME_CREATED;
  cgame->phase_generation = 0;
  qa_qvm_module_init(&cgame->module, options);
  qa_qvm_abi_profile profile = qa_qvm_module_get_abi_profile(&cgame->module);
  cgame->api.kind = QA_Q3_API_CGAME;
  cgame->api.version = profile != NULL && strcmp(profile, "q3-modern") == 0 ? 4 : 3;
}

void qa_qvm_cgame_destroy(qa_qvm_cgame *cgame) {
  if (cgame == NULL) return;
  qa_qvm_module_destroy(&cgame->module);
}

bool qa_qvm_cgame_get_supports_input_events(const qa_qvm_cgame *cgame) {
  return cgame->api.version >= 4;
}

static void check_current(qa_qvm_cgame *cgame, qa_qvm_cgame_export command) {
  cgame->lifetime.assert_current_operation(cgame->lifetime.ctx);
  if (cgame->phase_kind == QA_QVM_CGAME_RETIRED) qa_range_error("Cgame module has been retired");
  if (command == QA_QVM_CGAME_EXPORT_CG_SHUTDOWN ||
      command == QA_QVM_CGAME_EXPORT_CG_CONSOLE_COMMAND)
    return;
  if (cgame->phase_kind == QA_QVM_CGAME_CREATED) qa_range_error("Cgame module has not initialized");
  qa_qvm_cgame_lifetime_state state = cgame->lifetime.current(cgame->lifetime.ctx);
  if (state.dropped != NULL) qa_range_error("%s", state.dropped);
  if (state.generation != cgame->phase_generation)
    qa_range_error("Cgame module belongs to a stale engine gamestate");
}

typedef struct validate_ctx {
  qa_qvm_cgame *cgame;
  qa_qvm_cgame_export command;
} validate_ctx;

static void validate_current(void *ctx) {
  validate_ctx *validate = ctx;
  check_current(validate->cgame, validate->command);
}

static double call_command(qa_qvm_cgame *cgame, qa_qvm_cgame_export command, const double *values,
                           size_t value_count) {
  check_current(cgame, command);
  qa_qvm_abi_profile profile = qa_qvm_module_get_abi_profile(&cgame->module);
  bool modern = profile != NULL && strcmp(profile, "q3-modern") == 0;
  if (!modern && command > QA_QVM_CGAME_EXPORT_CG_LAST_ATTACKER)
    qa_range_error("Legacy cgame does not export %d", (int)command);
  int32_t words[16];
  words[0] = command;
  for (size_t i = 0; i < value_count; i++) words[i + 1] = qa_int32(values[i]);
  validate_ctx validate = {cgame, command};
  int32_t result =
      qa_qvm_module_call_async(&cgame->module, words, value_count + 1, 0, validate_current, &validate);
  check_current(cgame, command);
  return result;
}

void qa_qvm_cgame_init(qa_qvm_cgame *cgame, double server_message_number,
                       double server_command_sequence, double client_number) {
  cgame->lifetime.assert_current_operation(cgame->lifetime.ctx);
  if (cgame->phase_kind == QA_QVM_CGAME_RETIRED || cgame->phase_kind == QA_QVM_CGAME_INITIALIZING)
    qa_range_error("Cgame cannot initialize in its current lifecycle");
  qa_qvm_cgame_lifetime_state state = cgame->lifetime.current(cgame->lifetime.ctx);
  if (state.dropped != NULL) qa_range_error("%s", state.dropped);
  if (state.generation == 0) qa_range_error("CG_Init requires a live engine gamestate");
  if (state.server_message_number != server_message_number)
    qa_range_error("CG_Init message differs from the active engine message");
  cgame->phase_kind = QA_QVM_CGAME_INITIALIZING;
  cgame->phase_generation = state.generation;
  cgame->lifetime.begin_loading(cgame->lifetime.ctx);
  double values[] = {server_message_number, server_command_sequence, client_number};
  (void)call_command(cgame, QA_QVM_CGAME_EXPORT_CG_INIT, values, 3);
  if (cgame->lifetime.current(cgame->lifetime.ctx).server_message_number != server_message_number) {
    qa_qvm_cgame_retire(cgame);
    qa_range_error("Engine server message parsing must serialize behind CG_Init");
  }
  cgame->lifetime.prime(cgame->lifetime.ctx, state.generation);
  cgame->phase_kind = QA_QVM_CGAME_INITIALIZED;
  cgame->phase_generation = state.generation;
}

void qa_qvm_cgame_shutdown(qa_qvm_cgame *cgame) {
  (void)call_command(cgame, QA_QVM_CGAME_EXPORT_CG_SHUTDOWN, NULL, 0);
}

void qa_qvm_cgame_retire(qa_qvm_cgame *cgame) {
  cgame->phase_kind = QA_QVM_CGAME_RETIRED;
  qa_qvm_module_retire(&cgame->module);
}

bool qa_qvm_cgame_console_command(qa_qvm_cgame *cgame, const char *const *arguments,
                                  size_t argument_count) {
  check_current(cgame, QA_QVM_CGAME_EXPORT_CG_CONSOLE_COMMAND);
  int32_t words[] = {QA_QVM_CGAME_EXPORT_CG_CONSOLE_COMMAND};
  validate_ctx validate = {cgame, QA_QVM_CGAME_EXPORT_CG_CONSOLE_COMMAND};
  bool result = qa_qvm_module_command_async(&cgame->module, words, 1, arguments, argument_count,
                                            validate_current, &validate) != 0;
  check_current(cgame, QA_QVM_CGAME_EXPORT_CG_CONSOLE_COMMAND);
  return result;
}

void qa_qvm_cgame_draw_active_frame(qa_qvm_cgame *cgame, double time, qa_stereo_view stereo,
                                    bool demo_playback) {
  double values[] = {time, strcmp(stereo, "center") == 0 ? 0 : strcmp(stereo, "left") == 0 ? 1 : 2,
                     demo_playback ? 1 : 0};
  (void)call_command(cgame, QA_QVM_CGAME_EXPORT_CG_DRAW_ACTIVE_FRAME, values, 3);
}

qa_number_result qa_qvm_cgame_crosshair_player(qa_qvm_cgame *cgame) {
  double value = call_command(cgame, QA_QVM_CGAME_EXPORT_CG_CROSSHAIR_PLAYER, NULL, 0);
  qa_number_result result = {value >= 0, value};
  return result;
}

qa_number_result qa_qvm_cgame_last_attacker(qa_qvm_cgame *cgame) {
  double value = call_command(cgame, QA_QVM_CGAME_EXPORT_CG_LAST_ATTACKER, NULL, 0);
  qa_number_result result = {value >= 0, value};
  return result;
}

void qa_qvm_cgame_key_event(qa_qvm_cgame *cgame, double key, bool down) {
  double values[] = {key, down ? 1 : 0};
  (void)call_command(cgame, QA_QVM_CGAME_EXPORT_CG_KEY_EVENT, values, 2);
}

void qa_qvm_cgame_mouse_event(qa_qvm_cgame *cgame, double dx, double dy) {
  double values[] = {dx, dy};
  (void)call_command(cgame, QA_QVM_CGAME_EXPORT_CG_MOUSE_EVENT, values, 2);
}

void qa_qvm_cgame_event_handling(qa_qvm_cgame *cgame, qa_q3_cgame_event_handling mode) {
  double values[] = {strcmp(mode, "none") == 0 ? 0
                     : strcmp(mode, "team-menu") == 0 ? 1
                     : strcmp(mode, "scoreboard") == 0 ? 2 : 3};
  (void)call_command(cgame, QA_QVM_CGAME_EXPORT_CG_EVENT_HANDLING, values, 1);
}

static void exports_init(void *ctx, double server_message_number, double server_command_sequence,
                         double client_number) {
  qa_qvm_cgame_init(ctx, server_message_number, server_command_sequence, client_number);
}
static void exports_shutdown(void *ctx) { qa_qvm_cgame_shutdown(ctx); }
static bool exports_console_command(void *ctx, qa_string_view_list arguments) {
  return qa_qvm_cgame_console_command(ctx, arguments.values, arguments.count);
}
static void exports_draw_active_frame(void *ctx, double time, qa_stereo_view stereo,
                                      bool demo_playback) {
  qa_qvm_cgame_draw_active_frame(ctx, time, stereo, demo_playback);
}
static qa_number_result exports_crosshair_player(void *ctx) {
  return qa_qvm_cgame_crosshair_player(ctx);
}
static qa_number_result exports_last_attacker(void *ctx) { return qa_qvm_cgame_last_attacker(ctx); }
static void exports_key_event(void *ctx, double key, bool down) {
  qa_qvm_cgame_key_event(ctx, key, down);
}
static void exports_mouse_event(void *ctx, double dx, double dy) {
  qa_qvm_cgame_mouse_event(ctx, dx, dy);
}
static void exports_event_handling(void *ctx, qa_q3_cgame_event_handling mode) {
  qa_qvm_cgame_event_handling(ctx, mode);
}

qa_q3_cgame_exports qa_qvm_cgame_as_exports(qa_qvm_cgame *cgame) {
  qa_q3_cgame_exports exports;
  memset(&exports, 0, sizeof exports);
  exports.ctx = cgame;
  exports.api = cgame->api;
  exports.seat = cgame->seat;
  exports.init = exports_init;
  exports.shutdown = exports_shutdown;
  exports.console_command = exports_console_command;
  exports.draw_active_frame = exports_draw_active_frame;
  exports.crosshair_player = exports_crosshair_player;
  exports.last_attacker = exports_last_attacker;
  exports.key_event = exports_key_event;
  exports.mouse_event = exports_mouse_event;
  exports.event_handling = exports_event_handling;
  return exports;
}
