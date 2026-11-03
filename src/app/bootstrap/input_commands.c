/* Port of src/app/bootstrap/input.ts command surface: print,
 * executeUiCommand, registerCommand, activateCommands, retireCommands,
 * the staged command ring and the seat UI event closures.
 * GPL-2.0-or-later. */
#include "input_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/commands.h"
#include "qa/app/bootstrap/shared_setting_cvars.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"

/* ---- contexts ---- */
typedef struct context_node {
  qa_command_context context;
  qa_command_producer producer;
  qa_command_origin *origins; size_t origin_count;
  char **names;
} context_node;

qa_command_context *qa_app_input_internal_copy_context(const qa_command_context *source) {
  context_node *node = calloc(1, sizeof *node);
  if (node == NULL) qa_range_error("Out of memory");
  size_t depth = 1;
  for (const qa_command_origin *origin = &source->origin; origin->kind == QA_COMMAND_ORIGIN_SCRIPT; origin = origin->caller) depth++;
  node->origins = calloc(depth, sizeof *node->origins);
  node->names = calloc(depth, sizeof *node->names);
  if (node->origins == NULL || node->names == NULL) qa_range_error("Out of memory");
  node->origin_count = depth;
  const qa_command_origin *origin = &source->origin;
  for (size_t i = 0; i < depth; i++, origin = origin->caller) {
    node->origins[i] = *origin;
    if (origin->name != NULL) { node->names[i] = qa_app_input_internal_dup(origin->name); node->origins[i].name = node->names[i]; }
    node->origins[i].caller = i + 1 < depth ? &node->origins[i + 1] : NULL;
    if (origin->kind != QA_COMMAND_ORIGIN_SCRIPT) break;
  }
  node->context = *source;
  node->context.origin = node->origins[0];
  if (source->producer != NULL) { node->producer = *source->producer; node->context.producer = &node->producer; }
  return &node->context;
}

void qa_app_input_internal_free_context(qa_command_context *context) {
  if (context == NULL) return;
  context_node *node = (context_node *)context; /* context is the first member */
  for (size_t i = 0; i < node->origin_count; i++) free(node->names[i]);
  free(node->names); free(node->origins); free(node);
}

/* ---- print ---- */
void qa_app_input_internal_print_source(void *ctx, const char *text, const qa_command_context *source) {
  qa_app_application_input *input = ctx;
  const qa_command_context *context = source != NULL ? source
      : input->commands != NULL ? qa_command_buffer_execution_context(input->commands) : NULL;
  if (context == NULL) {
    input->actions.print(input->actions.ctx, text);
    for (size_t i = 0; i < input->local_count; i++) qa_seat_console_print(input->locals[i]->pub.console, text);
    return;
  }
  const qa_command_origin *origin = qa_app_input_internal_unwind(&context->origin);
  if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) {
    input_local *local = qa_app_input_internal_find_local(input, origin->seat);
    if (local != NULL) qa_seat_console_print(local->pub.console, text);
  } else {
    input->actions.print(input->actions.ctx, text);
    if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_CONSOLE && input->local_count > 0)
      qa_seat_console_print(input->locals[0]->pub.console, text);
  }
}

static void print_plain(void *ctx, const char *text) { qa_app_input_internal_print_source(ctx, text, NULL); }

/* forwardToServer of the candidate command options. */
void qa_app_input_internal_forward_to_server(void *ctx, qa_command_invocation *invocation) {
  qa_app_application_input *input = ctx;
  if (invocation->argc == 0) return;
  const qa_command_origin *origin = qa_app_input_internal_unwind(&invocation->source.origin);
  const qa_seat_id *seat = origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ? &origin->seat : NULL;
  qa_app_input_internal_execute(input, invocation->argv[0], (const char *const *)invocation->argv + 1,
                                invocation->argc - 1, seat, &invocation->source);
}

/* ---- staged commands ---- */
void qa_app_input_internal_staged_reset(qa_app_application_input *input) {
  size_t count = 0;
  input_staged_command *records = qa_event_ring_drain_all(input->staged, &count);
  for (size_t i = 0; i < count; i++) { free(records[i].text); qa_app_input_internal_free_context(records[i].source); }
  free(records);
  qa_event_ring_reset(input->staged);
}

static bool keep_unretired(const void *record, void *ctx) {
  const input_staged_command *command = record;
  const qa_client_id *client = ctx;
  const qa_command_origin *origin = qa_app_input_internal_unwind(&command->source->origin);
  bool retired = (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT || origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT)
      && qa_same_client(origin->client, *client);
  if (retired) { free(command->text); qa_app_input_internal_free_context(command->source); }
  return !retired;
}

void qa_app_input_internal_staged_filter_client(qa_app_application_input *input, qa_client_id client) {
  qa_event_ring_filter_live(input->staged, keep_unretired, &client);
}

void qa_app_application_input_enqueue_client_command(qa_app_application_input *input, const char *text,
    const qa_command_context *source) {
  if (input->candidate_program != NULL)
    qa_command_buffer_append(qa_app_prepared_client_commands_buffer(input->candidate_program), text, source, -1);
  else if (input->commands_active) qa_command_buffer_append(input->commands, text, source, -1);
  else {
    input_staged_command record = {false, qa_app_input_internal_dup(text), qa_app_input_internal_copy_context(source), NULL, NULL};
    qa_event_ring_admit(input->staged, &record);
  }
}

void qa_app_application_input_enqueue_client_reliable(qa_app_application_input *input, const char *text,
    const qa_command_context *source, qa_app_reliable_dispatch_fn dispatch, void *dispatch_ctx) {
  if (input->commands_active) { dispatch(dispatch_ctx, text, source); return; }
  input_staged_command record = {true, qa_app_input_internal_dup(text), qa_app_input_internal_copy_context(source),
                                 dispatch, dispatch_ctx};
  qa_event_ring_admit(input->staged, &record);
}

/* ---- executeUiCommand ---- */
static bool js_integer(double value) { return isfinite(value) && trunc(value) == value; }

static void execute_ui_command(qa_app_application_input *input, const char *name, const char *const *args,
    size_t count, const qa_seat_id *seat, const qa_command_context *source) {
  if (seat != NULL && (strcmp(name, "weapnext") == 0 || strcmp(name, "weapprev") == 0)) {
    input_seat_ui *ui = qa_app_input_internal_seat_ui(input, *seat);
    if (ui != NULL && ui->ui.cycle_weapon != NULL && ui->ui.cycle_weapon(ui->ui.ctx, strcmp(name, "weapnext") == 0 ? 1 : -1))
      return;
  }
  if (seat != NULL && strcmp(name, "switchweapon") == 0 && count == 2) {
    double first = qa_input_js_parse_number(args[0]), second = qa_input_js_parse_number(args[1]);
    if (js_integer(first) && js_integer(second)) {
      input_seat_ui *ui = qa_app_input_internal_seat_ui(input, *seat);
      if (ui != NULL && ui->ui.switch_weapon != NULL && ui->ui.switch_weapon(ui->ui.ctx, first, second)) return;
    }
  }
  qa_app_input_internal_execute(input, name, args, count, seat, source);
}

static void q2_execute(void *ctx, const char *name, const char *const *args, size_t count, const qa_seat_id *seat,
    qa_command_context source) {
  execute_ui_command(ctx, name, args, count, seat, &source);
}
static void q1_execute(void *ctx, const char *name, const char *const *args, size_t count, const qa_seat_id *seat,
    qa_command_context source) {
  qa_app_input_internal_execute(ctx, name, args, count, seat, &source);
}

/* ---- registerCommand ---- */
enum { COMMAND_IN_RESTART, COMMAND_MIDIINFO, COMMAND_QUIT, COMMAND_OFFHAND, COMMAND_UI };

static void run_command(void *ctx, qa_command_invocation *invocation) {
  input_command *command = ctx;
  qa_app_application_input *input = command->owner;
  const char *const *args = invocation->argc > 1 ? (const char *const *)invocation->argv + 1 : NULL;
  size_t arg_count = invocation->argc > 1 ? invocation->argc - 1 : 0;
  switch (command->kind) {
    case COMMAND_IN_RESTART:
      qa_app_application_input_release_for_profile_change(input, NULL);
      qa_app_input_devices_restart(input->input_devices);
      qa_input_router_restart(input->router);
      return;
    case COMMAND_MIDIINFO: qa_app_input_devices_info(input->input_devices); return;
    case COMMAND_QUIT: input->actions.quit(input->actions.ctx); return;
    case COMMAND_OFFHAND: {
      const qa_command_origin *origin = qa_app_input_internal_unwind(&invocation->source.origin);
      if (origin->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT) return;
      bool grapple = strstr(command->name, "grapple") != NULL, down = command->name[0] == '+';
      qa_ui_binding_capabilities capabilities = qa_app_application_input_binding_capabilities(input);
      if (!(grapple ? capabilities.offhand_grapple : capabilities.offhand_grenades)) {
        if (down) print_plain(input, "This session has no selected offhand action.\n");
        return;
      }
      input_offhand *buttons = NULL;
      for (size_t i = 0; i < input->offhand_count && buttons == NULL; i++)
        if (qa_same_seat(input->offhand[i]->seat, origin->seat)) buttons = input->offhand[i];
      if (buttons == NULL) {
        buttons = calloc(1, sizeof *buttons);
        if (buttons == NULL) qa_range_error("Out of memory");
        buttons->seat = origin->seat;
        qa_input_button_init(&buttons->grapple); qa_input_button_init(&buttons->grenade);
        *INPUT_PUSH(input->offhand, input->offhand_count, input->offhand_capacity) = buttons;
      }
      qa_input_button *button = grapple ? &buttons->grapple : &buttons->grenade;
      bool active = qa_input_button_active(button);
      const char *key = arg_count > 0 ? args[0] : "console";
      double time = qa_app_input_internal_now(input);
      if (down) { if (!qa_input_button_down(button, key, time)) qa_range_error("Out of memory"); }
      else if (arg_count == 0) qa_input_button_release(button, time);
      else qa_input_button_up(button, key, time, 10);
      if (active == qa_input_button_active(button)) return;
      qa_seat_id seat = origin->seat;
      qa_app_input_internal_execute(input, command->name, NULL, 0, &seat, &invocation->source);
      return;
    }
    default: {
      const qa_command_origin *origin = qa_app_input_internal_unwind(&invocation->source.origin);
      qa_seat_id seat = origin->seat;
      execute_ui_command(input, command->name, args, arg_count,
                         origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ? &seat : NULL, &invocation->source);
    }
  }
}

static input_cleanup *push_cleanup(qa_app_application_input *input, input_cleanup_kind kind, void *object, int64_t handle) {
  input_cleanup *entry = INPUT_PUSH(input->unregister, input->unregister_count, input->unregister_capacity);
  entry->kind = kind; entry->object = object; entry->handle = handle;
  return entry;
}

static void register_command(qa_app_application_input *input, const char *name, int kind, qa_command_ownership ownership) {
  if (qa_command_buffer_exists(input->commands, name)) return;
  input_command *command = calloc(1, sizeof *command);
  if (command == NULL) qa_range_error("Out of memory");
  command->owner = input; command->name = qa_app_input_internal_dup(name); command->kind = kind;
  if (qa_command_buffer_register(input->commands, name, run_command, command, NULL, ownership))
    push_cleanup(input, INPUT_CLEANUP_COMMAND, command, 0);
  else { free(command->name); free(command); }
}

/* ---- seat UI event closures ---- */
static bool ui_event(void *ctx, const qa_seat_input_event *event, qa_seat_input_focus focus) {
  input_ui_callback *callback = ctx;
  qa_app_application_input *input = callback->owner;
  input_seat_ui *ui = qa_app_input_internal_seat_ui(input, callback->seat);
  qa_seat_console *console = callback->joined ? callback->captured_console
      : callback->local != NULL ? callback->local->pub.console : NULL;
  if (strcmp(event->kind, "key") == 0 && (event->code == 96 || event->code == 126)) {
    if (event->down) {
      if (!event->repeat && ui != NULL) ui->ui.close_menus(ui->ui.ctx);
      if (console != NULL) qa_seat_console_toggle_from_key(console, event->repeat);
    }
    return true;
  }
  if (console != NULL && qa_seat_console_input(console, event, focus)) return true;
  if (input->actions.client_input != NULL && input->actions.client_input(input->actions.ctx, event)) return true;
  return ui != NULL && ui->ui.input(ui->ui.ctx, event, focus);
}

static double ui_now(void *ctx) { return qa_app_input_internal_now(ctx); }

/* ---- activate / retire ---- */
typedef struct pitch_drift_ctx { qa_cvar_registry *registry; } pitch_drift_ctx;
static void pitch_drift_settings(void *ctx, double *out_speed, double *out_delay) {
  pitch_drift_ctx *drift = ctx;
  *out_speed = qa_cvar_registry_variable_value(drift->registry, "v_centerspeed");
  *out_delay = qa_cvar_registry_variable_value(drift->registry, "v_centermove");
}
static void destroy_memory(void *object) { free(object); }

static void wheel(void *ctx, qa_seat_id seat, qa_wheel_mode mode, bool down) {
  input_seat_ui *ui = qa_app_input_internal_seat_ui(ctx, seat);
  if (ui != NULL) ui->ui.wheel(ui->ui.ctx, mode, down);
}
static qa_seat_input *lookup_input(void *ctx, qa_seat_id seat) {
  input_local *local = qa_app_input_internal_find_local(ctx, seat);
  return local != NULL ? local->pub.input : NULL;
}
static bool lookup_binding_seat(void *ctx, qa_seat_id seat, qa_binding_command_seat *out) {
  qa_seat_input *found = lookup_input(ctx, seat);
  if (found == NULL) return false;
  *out = qa_seat_input_command_seat(found);
  return true;
}
static bool client_scores(void *ctx, qa_command_invocation *command) {
  qa_app_application_input *input = ctx;
  return qa_client_command_bindings_dispatch(input->client_commands, command);
}

void qa_app_input_internal_activate_commands(qa_app_application_input *input, bool preserve_focus) {
  if (input->commands_active) return;
  input->commands_active = true;
  qa_app_input_devices_activate(input->input_devices, input->router);
  register_command(input, "in_restart", COMMAND_IN_RESTART, QA_COMMAND_ENGINE);
  register_command(input, "midiinfo", COMMAND_MIDIINFO, QA_COMMAND_ENGINE);
  if (input->startup != NULL)
    push_cleanup(input, INPUT_CLEANUP_STARTUP_OUTPUT, input->startup,
                 qa_app_prepared_startup_bind_output(input->startup, qa_app_input_internal_print_source, input));
  qa_command_context context = qa_cvar_registry_context(input->console_cvars);
  qa_command_dialect source_dialect = qa_app_input_internal_source_dialect(input);
  for (size_t i = 0; i < input->local_count; i++) {
    input_local *local = input->locals[i];
    qa_seat_id seat = qa_session_seat_id(local->pub.player.seat);
    qa_cvar_registry *registry = qa_app_application_input_input_cvars(input, true, seat);
    if (registry == NULL) qa_input_error("Local input has no settings registry");
    pitch_drift_ctx *drift = calloc(1, sizeof *drift);
    if (drift == NULL) qa_range_error("Out of memory");
    drift->registry = registry;
    qa_app_input_internal_own(input, drift, destroy_memory);
    qa_input_command_builder_bind_pitch_drift(local->pub.builder, pitch_drift_settings, drift);
    qa_command_context source = qa_app_input_internal_seat_context(context.session, &local->pub.player);
    qa_cvar_read previous_run;
    bool has_previous = qa_command_buffer_find_cvar(input->commands, "cl_run", &source, &previous_run);
    qa_cvar_read present;
    bool registered = qa_cvar_registry_find(registry, "cl_run", &present);
    if (registered) qa_cvar_read_free(&present);
    if (!registered && has_previous) {
      qa_cvar_archive_entry entry = {(char *)"cl_run", previous_run.value};
      qa_cvar_registry_apply_archive(registry, &entry, 1);
    }
    if (has_previous) qa_cvar_read_free(&previous_run);
    push_cleanup(input, INPUT_CLEANUP_RUN_BINDING, registry, qa_app_bind_run_cvar(registry, local->pub.builder));
  }
  push_cleanup(input, INPUT_CLEANUP_WHEEL, qa_register_wheel_commands(input->commands, wheel, input), 0);
  push_cleanup(input, INPUT_CLEANUP_INPUT_COMMANDS,
               qa_register_input_commands(input->commands, lookup_input, input, client_scores, input), 0);
  if (input->startup == NULL)
    push_cleanup(input, INPUT_CLEANUP_BINDING_COMMANDS,
                 qa_register_binding_commands(input->commands, lookup_binding_seat, input, print_plain, input, NULL), 0);
  push_cleanup(input, INPUT_CLEANUP_DISCOVERY, qa_register_discovery_commands(input->commands, print_plain, input), 0);
  push_cleanup(input, INPUT_CLEANUP_LLM,
               qa_register_llm_commands(input->commands, qa_app_input_internal_print_source, input, input->actions.llm), 0);
  push_cleanup(input, INPUT_CLEANUP_Q2, qa_app_register_q2_client_commands(input->commands, source_dialect, q2_execute, input), 0);
  push_cleanup(input, INPUT_CLEANUP_Q1, qa_app_register_q1_client_commands(input->commands, source_dialect, q1_execute, input), 0);
  register_command(input, "quit", COMMAND_QUIT, QA_COMMAND_WORLD);
  static const char *const offhand[] = {"+grapple", "-grapple", "+grenade", "-grenade"};
  for (size_t i = 0; i < 4; i++) register_command(input, offhand[i], COMMAND_OFFHAND, QA_COMMAND_WORLD);
  static const char *const ui_names[] = {"local_join", "local_drop", "weapnext", "weapprev", "switchweapon", "use", "weapon",
                                         "save", "load", "map", "say", "say_team", "centerview"};
  size_t ui_count = sizeof ui_names / sizeof ui_names[0];
  for (size_t i = 0; i < ui_count + qa_app_application_audio_command_count; i++) {
    const char *name = i < ui_count ? ui_names[i] : qa_app_application_audio_commands[i - ui_count];
    if ((source_dialect == QA_COMMAND_DIALECT_Q2_CLASSIC || source_dialect == QA_COMMAND_DIALECT_Q2_RERELEASE)
        && qa_command_buffer_exists(input->commands, name)) continue;
    register_command(input, name, COMMAND_UI, QA_COMMAND_WORLD);
  }
  for (size_t i = 0; i < input->ui_callback_count; i++) {
    input_ui_callback *callback = input->ui_callbacks[i];
    input_ui_release *release = INPUT_PUSH(input->release_ui, input->release_ui_count, input->release_ui_capacity);
    release->input = callback->input;
    release->binding = qa_seat_input_bind_ui_event(callback->input, ui_event, callback, ui_now, input);
    if (!preserve_focus) {
      qa_seat_input_focus game;
      memset(&game, 0, sizeof game);
      game.kind = "game";
      qa_seat_input_set_focus(callback->input, game, qa_app_input_internal_now(input));
    }
  }
  for (size_t i = 0; i < input->local_count; i++)
    qa_seat_console_publish(input->locals[i]->pub.console, qa_seat_input_get_focus(input->locals[i]->pub.input));
  qa_client_command_bindings_activate(input->client_commands);
  size_t count = 0;
  input_staged_command *staged = qa_event_ring_take(input->staged, input->staged_cursor, &count);
  qa_event_ring_retire(input->staged);
  for (size_t i = 0; i < count; i++) {
    if (!staged[i].reliable) qa_command_buffer_append(input->commands, staged[i].text, staged[i].source, -1);
    else staged[i].dispatch(staged[i].dispatch_ctx, staged[i].text, staged[i].source);
    free(staged[i].text);
    qa_app_input_internal_free_context(staged[i].source);
  }
  free(staged);
}

static void run_cleanup(input_cleanup *entry) {
  switch (entry->kind) {
    case INPUT_CLEANUP_COMMAND: {
      input_command *command = entry->object;
      (void)qa_command_buffer_unregister(command->owner->commands, command->name, NULL, NULL, false);
      free(command->name); free(command);
      return;
    }
    case INPUT_CLEANUP_RUN_BINDING: qa_cvar_registry_unbind_value(entry->object, entry->handle); return;
    case INPUT_CLEANUP_WHEEL: qa_wheel_commands_registration_destroy(entry->object); return;
    case INPUT_CLEANUP_INPUT_COMMANDS: qa_input_commands_registration_destroy(entry->object); return;
    case INPUT_CLEANUP_BINDING_COMMANDS: qa_binding_commands_registration_destroy(entry->object); return;
    case INPUT_CLEANUP_DISCOVERY: qa_console_discovery_registration_remove(entry->object); return;
    case INPUT_CLEANUP_LLM: qa_llm_commands_registration_remove(entry->object); return;
    case INPUT_CLEANUP_Q2: qa_app_q2_client_commands_release(entry->object); return;
    case INPUT_CLEANUP_Q1: qa_app_q1_client_commands_release(entry->object); return;
    case INPUT_CLEANUP_STARTUP_OUTPUT: qa_app_prepared_startup_unbind_output(entry->object, entry->handle); return;
  }
}

void qa_app_input_internal_retire_commands(qa_app_application_input *input) {
  qa_client_command_bindings_deactivate(input->client_commands);
  /* releaseUi.runAll(), then unregister.runAll(): each snapshots, runs in
   * registration order and empties. */
  input_ui_release *releases = input->release_ui;
  size_t release_count = input->release_ui_count;
  input->release_ui = NULL; input->release_ui_count = 0; input->release_ui_capacity = 0;
  for (size_t i = 0; i < release_count; i++) qa_seat_input_unbind_ui_event(releases[i].input, releases[i].binding);
  free(releases);
  input_cleanup *entries = input->unregister;
  size_t count = input->unregister_count;
  input->unregister = NULL; input->unregister_count = 0; input->unregister_capacity = 0;
  for (size_t i = 0; i < count; i++) run_cleanup(&entries[i]);
  free(entries);
  input->commands_active = false;
}
