/* Port of src/app/bootstrap/input.ts platform and publication surface:
 * retireLocalSeats, startup adoption, pump, haptics, build, focus, seat
 * selections, UI attachment, window and platform transfer, client seat
 * publication, rebindPlayers, saveSettings and close. GPL-2.0-or-later. */
#include "input_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/cvar_archives.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/network/common/js_number.h"

static void *xcalloc(size_t count, size_t size) {
  void *value = calloc(count == 0 ? 1 : count, size);
  if (value == NULL) qa_range_error("Out of memory");
  return value;
}

static qa_seat_id local_seat(const input_local *local) { return qa_session_seat_id(local->pub.player.seat); }

static bool focus_is_game(const qa_seat_input *input) {
  return qa_seat_input_focused(input) && strcmp(qa_seat_input_get_focus(input).kind, "game") == 0;
}

/* ---- releaseOffhand ---- */
void qa_app_input_internal_release_offhand(qa_app_application_input *input, bool all) {
  for (size_t i = 0; i < input->local_count; i++) {
    input_local *local = input->locals[i];
    if (!all && focus_is_game(local->pub.input)) continue;
    qa_seat_id seat = local_seat(local);
    input_offhand *buttons = NULL;
    for (size_t b = 0; b < input->offhand_count && buttons == NULL; b++)
      if (qa_same_seat(input->offhand[b]->seat, seat)) buttons = input->offhand[b];
    if (buttons == NULL) continue;
    static const char *const names[] = {"-grapple", "-grenade"};
    qa_input_button *pair[] = {&buttons->grapple, &buttons->grenade};
    for (size_t n = 0; n < 2; n++) {
      if (!qa_input_button_active(pair[n])) continue;
      qa_input_button_release(pair[n], qa_app_input_internal_now(input));
      qa_app_input_internal_execute(input, names[n], NULL, 0, &seat, NULL);
    }
  }
}

/* ---- retireLocalSeats ---- */
typedef struct retire_step {
  qa_app_application_input *input;
  int kind;
  input_local *local;
  qa_seat_id *ids; size_t id_count;
  qa_client_id client;
  qa_app_client_bootstrap *bootstrap;
  bool has_keyboard; qa_seat_id keyboard;
} retire_step;

static void retire_run(void *ctx) {
  retire_step *step = ctx;
  qa_app_application_input *input = step->input;
  switch (step->kind) {
    case 0: qa_app_input_internal_retire_commands(input); break;
    case 1: (void)qa_seat_haptics_close(step->local->pub.haptics); break;
    case 2: qa_client_command_bindings_publish_seats(input->client_commands, step->ids, step->id_count); break;
    case 3: qa_input_router_retain_seats(input->router, step->ids, step->id_count, step->has_keyboard, step->keyboard); break;
    case 4: qa_app_controller_settings_publish_seats(input->controller_settings, step->ids, step->id_count); break;
    case 5: qa_command_buffer_discard_client(input->commands, step->client); break;
    case 6: qa_app_prepared_startup_set_active_seats(step->bootstrap->prepared, step->ids, step->id_count); break;
    case 7: if (input->startup != NULL) qa_app_prepared_startup_set_active_seats(input->startup, step->ids, step->id_count); break;
    case 8: qa_app_input_internal_activate_commands(input, true); break;
    default: break;
  }
}

/* TS dispose(run): failures are collected and rethrown together. */
static void dispose(retire_step *step, size_t *failures, qa_caught_error *first) {
  qa_caught_error caught;
  if (qa_try(retire_run, step, &caught)) return;
  if (*failures == 0) *first = caught;
  (*failures)++;
}

static bool seat_listed(qa_session_seat *const *seats, size_t count, const qa_session_seat *seat) {
  for (size_t i = 0; i < count; i++) if (seats[i] == seat) return true;
  return false;
}

static void client_console_delete(qa_app_client_bootstrap *client, const qa_session_seat *seat) {
  for (size_t i = 0; i < client->console_count; i++)
    if (client->consoles[i].seat == seat) {
      memmove(&client->consoles[i], &client->consoles[i + 1], (client->console_count - i - 1) * sizeof *client->consoles);
      client->console_count--;
      return;
    }
}

void qa_app_application_input_retire_local_seats(qa_app_application_input *input, qa_session_seat *const *seats,
    size_t seat_count, qa_app_client_bootstrap *client) {
  input_local **removed = xcalloc(input->local_count, sizeof *removed);
  size_t removed_count = 0;
  for (size_t i = 0; i < input->local_count; i++)
    if (seat_listed(seats, seat_count, input->locals[i]->pub.player.seat)) removed[removed_count++] = input->locals[i];
  if (removed_count != seat_count) { free(removed); qa_input_error("Retired local seats differ from their active input owners"); }
  if (removed_count == 0) { free(removed); return; }
  input_local **next = xcalloc(input->local_count, sizeof *next);
  size_t next_count = 0;
  for (size_t i = 0; i < input->local_count; i++) {
    bool gone = false;
    for (size_t r = 0; r < removed_count && !gone; r++) gone = removed[r] == input->locals[i];
    if (!gone) next[next_count++] = input->locals[i];
  }
  qa_seat_id keyboard;
  bool has_keyboard = qa_input_router_keyboard_seat(input->router, &keyboard);
  size_t failures = 0;
  qa_caught_error first;
  memset(&first, 0, sizeof first);
  retire_step step;
  memset(&step, 0, sizeof step);
  step.input = input;
  step.kind = 0; dispose(&step, &failures, &first);
  for (size_t r = 0; r < removed_count; r++) {
    qa_app_input_internal_forget_seat(input, removed[r]);
    removed[r]->retired = true;
    step.kind = 1; step.local = removed[r]; dispose(&step, &failures, &first);
  }
  input->local_count = 0;
  for (size_t i = 0; i < next_count; i++) *INPUT_PUSH(input->locals, input->local_count, input->local_capacity) = next[i];
  qa_seat_id *ids = xcalloc(next_count, sizeof *ids);
  for (size_t i = 0; i < next_count; i++) ids[i] = local_seat(next[i]);
  step.ids = ids; step.id_count = next_count;
  step.kind = 2; dispose(&step, &failures, &first);
  step.has_keyboard = false;
  if (has_keyboard && qa_app_input_internal_find_local(input, keyboard) != NULL) { step.has_keyboard = true; step.keyboard = keyboard; }
  else if (next_count > 0) { step.has_keyboard = true; step.keyboard = ids[0]; }
  step.kind = 3; dispose(&step, &failures, &first);
  step.kind = 4; dispose(&step, &failures, &first);
  for (size_t r = 0; r < removed_count; r++) {
    qa_client_id client_id = qa_session_client_id(qa_session_seat_client(removed[r]->pub.player.seat));
    step.client = client_id;
    step.kind = 5; dispose(&step, &failures, &first);
    qa_app_input_internal_staged_filter_client(input, client_id);
  }
  if (client != NULL) {
    size_t kept = 0;
    for (size_t i = 0; i < client->local_count; i++)
      if (!seat_listed(seats, seat_count, client->locals[i].seat)) client->locals[kept++] = client->locals[i];
    client->local_count = kept;
    for (size_t s = 0; s < seat_count; s++) client_console_delete(client, seats[s]);
    step.bootstrap = client;
    step.kind = 6; dispose(&step, &failures, &first);
  } else { step.kind = 7; dispose(&step, &failures, &first); }
  step.kind = 8; dispose(&step, &failures, &first);
  free(ids); free(next); free(removed);
  if (failures != 0)
    qa_raise("AggregateError", 0, 0, "Local input retirement failed (%zu errors; first %s: %s)", failures, first.name, first.message);
}

/* ---- startup publication shared by prepareLocalSeats ---- */
void qa_app_input_internal_publish_seats_to_startup(qa_app_application_input *input, bool retain) {
  qa_app_prepared_startup *startup = input->startup;
  size_t old_count = 0;
  const qa_app_prepared_seat *old = qa_app_prepared_startup_seats(startup, &old_count);
  qa_app_prepared_seat *published = xcalloc(input->local_count + old_count, sizeof *published);
  qa_seat_id *active = xcalloc(input->local_count, sizeof *active);
  size_t count = 0;
  for (size_t i = 0; i < input->local_count; i++) {
    input_local *local = input->locals[i];
    qa_seat_id seat = local_seat(local);
    qa_mouse_settings *mouse = qa_app_input_internal_mouse(input, seat);
    qa_cvar_registry *cvars = qa_app_input_internal_console_seat(input, seat);
    if (cvars == NULL)
      for (size_t o = 0; o < old_count && cvars == NULL; o++) if (qa_same_seat(old[o].id, seat)) cvars = old[o].cvars;
    if (mouse == NULL || cvars == NULL) { free(published); free(active); qa_input_error("Local seat has no registry owners"); }
    published[count].id = seat; published[count].input = local->pub.input;
    published[count].mouse = *mouse; published[count].cvars = cvars;
    published[count].context = qa_app_input_internal_seat_context(seat.session, &local->pub.player);
    qa_app_input_internal_hold_for(startup, local->pub.input);
    qa_app_input_internal_hold_for(startup, mouse->cvars);
    active[count] = seat;
    count++;
  }
  size_t active_count = count;
  if (retain)
    for (size_t o = 0; o < old_count; o++) {
      bool republished = false;
      for (size_t i = 0; i < active_count && !republished; i++) republished = qa_same_seat(active[i], old[o].id);
      if (!republished) published[count++] = old[o];
    }
  qa_app_prepared_startup_publish_seats(startup, published, count, active, active_count, true);
  free(published); free(active);
}

/* ---- startup adoption ---- */
static qa_cvar_registry *adoption_source(qa_app_application_input *input) {
  qa_app_console_server server;
  if (input->actions.console != NULL && input->actions.console->server(input->actions.console->ctx, &server)) return server.cvars;
  return !input->has_external_routing ? qa_app_prepared_startup_source(input->startup) : input->console_cvars;
}

static bool startup_reader(qa_app_application_input *input, qa_app_prepared_scoped_read_fn *read, void **read_ctx) {
  return input->actions.startup_reader != NULL
      && input->actions.startup_reader(input->actions.ctx, input->script_owner, input->options, read, read_ctx);
}

void qa_app_application_input_validate_startup_adoption(qa_app_application_input *input) {
  if (input->startup == NULL) return;
  qa_app_prepared_scoped_read_fn read = NULL; void *read_ctx = NULL;
  if (!startup_reader(input, &read, &read_ctx)) qa_input_error("Startup script reader adoption is missing");
  qa_app_prepared_owners owners;
  memset(&owners, 0, sizeof owners);
  owners.source = adoption_source(input); owners.movement = input->cvar_owner; owners.fallback = input->console_cvars;
  qa_app_prepared_startup_validate_owners(input->startup, &owners);
}

static void startup_forward(void *ctx, const char *name, const char *const *args, size_t count, qa_command_context source) {
  const qa_command_origin *origin = qa_app_input_internal_unwind(&source.origin);
  qa_app_input_internal_execute(ctx, name, args, count, origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ? &origin->seat : NULL, &source);
}

void qa_app_application_input_adopt_startup(qa_app_application_input *input) {
  if (input->startup == NULL) return;
  qa_app_prepared_scoped_read_fn read = NULL; void *read_ctx = NULL;
  if (!startup_reader(input, &read, &read_ctx)) qa_input_error("Startup script reader adoption is missing");
  qa_command_cvar_routing routing;
  if (!qa_app_input_internal_routing(input, &routing)) qa_input_error("Startup adoption has no cvar routing");
  qa_app_prepared_owners owners;
  memset(&owners, 0, sizeof owners);
  owners.source = adoption_source(input); owners.movement = input->cvar_owner; owners.fallback = input->console_cvars;
  owners.scripts = input->script_owner; owners.read = read; owners.read_ctx = read_ctx;
  qa_app_prepared_startup_adopt(input->startup, routing, startup_forward, input, &owners);
  for (size_t i = 0; i < input->local_count; i++) {
    qa_seat_id seat = local_seat(input->locals[i]);
    qa_cvar_registry *cvars = qa_app_input_internal_console_seat(input, seat);
    if (cvars == NULL) {
      size_t count = 0;
      const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(input->startup, &count);
      for (size_t s = 0; s < count && cvars == NULL; s++) if (qa_same_seat(seats[s].id, seat)) cvars = seats[s].cvars;
    }
    if (cvars == NULL && input->has_external_routing) cvars = input->console_cvars;
    qa_app_prepared_startup_adopt_seat(input->startup, seat, cvars, qa_app_input_internal_mouse(input, seat));
  }
  qa_app_prepared_profile *configuration = input->actions.configuration;
  if (configuration != NULL && !input->configuration_published) {
    if (input->candidate_program != NULL) qa_input_error("Configuration commands must publish before their continuation");
    qa_app_prepared_profile_publish_continuation(configuration, input->startup);
    input->configuration_published = true;
  }
}

/* ---- loading / pump ---- */
static bool quit_event(const qa_sdl_event *event) {
  return event->kind == QA_SDL_EVENT_QUIT || (event->kind == QA_SDL_EVENT_WINDOW && event->as.window.event == 14);
}

void qa_app_application_input_poll_loading_events(qa_app_application_input *input) {
  size_t count = 0;
  qa_sdl_event *events = input->window.input.poll_events(input->window.input.ctx, &count);
  if (count != 0) qa_event_ring_admit_all(input->window_events, events, count);
  size_t controller_count = 0;
  qa_controller_event *controller_events = qa_sdl_controllers_poll_events(input->controllers, &controller_count);
  /* The ring takes the records (and their device strings) as they are. */
  if (controller_count != 0) qa_event_ring_admit_all(input->controller_events, controller_events, controller_count);
  free(controller_events);
  bool quit = false;
  for (size_t i = 0; i < count && !quit; i++) quit = quit_event(&events[i]);
  free(events);
  if (quit) input->actions.quit(input->actions.ctx);
}

static void handle_window(qa_app_application_input *input, const qa_sdl_event *event) {
  if (event->kind == QA_SDL_EVENT_WINDOW && event->as.window.event == 13) qa_app_application_input_stop_haptics(input);
  qa_input_router_handle_platform(input->router, event);
}
static void drain_window(const void *record, int64_t id, void *ctx) { (void)id; handle_window(ctx, record); }
typedef struct drain_controller_ctx { qa_input_router *router; } drain_controller_ctx;
static void drain_controller(const void *record, int64_t id, void *ctx) {
  (void)id;
  drain_controller_ctx *drain = ctx;
  qa_input_router_handle_controller(drain->router, record);
  qa_controller_event copy = *(const qa_controller_event *)record;
  if (copy.kind == QA_CONTROLLER_EVENT_CONNECTED || copy.kind == QA_CONTROLLER_EVENT_REMAPPED)
    qa_controller_device_dispose(&copy.device);
}
static void drain_platform(const void *record, int64_t id, void *ctx) {
  (void)id;
  qa_input_router_handle_platform(ctx, record);
}

void qa_app_application_input_pump(qa_app_application_input *input, bool execute_commands) {
  qa_app_input_internal_release_offhand(input, false);
  qa_app_input_internal_synchronize_client_focus(input);
  qa_event_ring_drain(input->window_events, input->window_cursor, drain_window, input);
  qa_event_ring_retire(input->window_events);
  size_t count = 0;
  qa_sdl_event *events = input->window.input.poll_events(input->window.input.ctx, &count);
  for (size_t i = 0; i < count; i++) handle_window(input, &events[i]);
  free(events);
  qa_app_input_devices_frame(input->input_devices, qa_app_input_internal_now(input));
  drain_controller_ctx drain = {input->router};
  qa_event_ring_drain(input->controller_events, input->controller_cursor, drain_controller, &drain);
  qa_event_ring_retire(input->controller_events);
  size_t controller_count = 0;
  qa_controller_event *controller_events = qa_sdl_controllers_poll_events(input->controllers, &controller_count);
  for (size_t i = 0; i < controller_count; i++) qa_input_router_handle_controller(input->router, &controller_events[i]);
  qa_controller_events_free(controller_events, controller_count);
  qa_app_controller_settings_update(input->controller_settings);
  if (execute_commands) (void)qa_command_buffer_execute(input->commands);
  qa_input_router_update_capture(input->router);
  for (size_t i = 0; i < input->local_count; i++) {
    input_local *local = input->locals[i];
    qa_seat_haptics_set_active(local->pub.haptics, focus_is_game(local->pub.input));
    (void)qa_seat_haptics_update(local->pub.haptics);
  }
}

/* ---- haptics ---- */
void qa_app_application_input_bind_haptics(qa_app_application_input *input, qa_app_haptic_load_fn load, void *load_ctx) {
  for (size_t i = 0; i < input->local_count; i++) qa_seat_haptics_invalidate_assets(input->locals[i]->pub.haptics);
  input->haptic_load = load; input->haptic_load_ctx = load_ctx;
}

void qa_app_application_input_stop_haptics(qa_app_application_input *input) {
  for (size_t i = 0; i < input->local_count; i++) (void)qa_seat_haptics_cancel(input->locals[i]->pub.haptics);
}

void qa_app_application_input_sound_haptics(qa_app_application_input *input, qa_content_id content,
    const char *sound, const qa_actor_id *actor, qa_audio_audience audience) {
  if (actor == NULL) return;
  for (size_t i = 0; i < input->local_count; i++) {
    input_local *local = input->locals[i];
    if (!qa_same_actor(local->pub.player.actor, *actor)
        || (audience.kind == QA_AUDIO_AUDIENCE_SEAT && !qa_same_seat(audience.seat, local_seat(local)))) continue;
    qa_seat_haptics_set_active(local->pub.haptics, focus_is_game(local->pub.input));
    qa_haptics_result result;
    (void)qa_seat_haptics_sound(local->pub.haptics, content, sound, &result);
  }
}

/* ---- build ---- */
static qa_actor_command build_one(qa_app_application_input *input, qa_seat_id seat, double elapsed, double server_ms,
    double server_frame, double wall_elapsed) {
  input_local *local = qa_app_input_internal_find_local(input, seat);
  if (local == NULL) qa_input_error("Command input seat is not active");
  qa_command_dialect dialect = input->dialect;
  qa_user_command_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.kind = dialect;
  if (dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE) frame.acknowledged_server_time_seconds = server_ms / 1000;
  else if (dialect == QA_COMMAND_DIALECT_Q2_CLASSIC) { frame.light_level = 128; frame.attack_allowed = true; }
  else if (dialect == QA_COMMAND_DIALECT_Q2_RERELEASE) { frame.server_frame = server_frame; frame.attack_allowed = true; }
  else if (dialect == QA_COMMAND_DIALECT_Q3) { frame.server_time_milliseconds = trunc(server_ms); frame.weapon = 2; frame.sensitivity = 1; }
  qa_seat_input_sample sample;
  if (!qa_seat_input_take_sample(local->pub.input, qa_app_input_internal_now(input), wall_elapsed, &sample))
    qa_range_error("Out of memory");
  input_seat_ui *ui = qa_app_input_internal_seat_ui(input, seat);
  if (ui != NULL) ui->ui.sample(ui->ui.ctx, &sample);
  qa_app_q3_command_selection selection = {0, 0};
  bool has_selection = false;
  for (size_t i = 0; i < input->q3_count; i++)
    if (qa_same_seat(input->q3[i].seat, seat)) { selection = input->q3[i].selection; has_selection = true; }
  qa_app_sim_player_view view = input->simulation->player_view(input->simulation->ctx, local->pub.player.actor);
  if (view.has_pitch_drift) {
    frame.has_pitch_drift = true;
    frame.pitch_drift.grounded = view.pitch_drift_grounded;
    frame.pitch_drift.ideal_pitch = view.pitch_drift_ideal_pitch;
    frame.pitch_drift.disabled = view.pitch_drift_disabled;
  }
  if (dialect == QA_COMMAND_DIALECT_Q3 && has_selection) { frame.weapon = selection.weapon; frame.sensitivity = selection.sensitivity; }
  qa_provider_id impulse_provider = NULL;
  bool has_impulse_provider = false;
  if ((dialect == QA_COMMAND_DIALECT_Q3 || dialect == QA_COMMAND_DIALECT_Q2_RERELEASE) && input->actions.arsenal_impulse_provider != NULL)
    has_impulse_provider = input->actions.arsenal_impulse_provider(input->actions.ctx, seat, &impulse_provider);
  const input_arsenal *selected = NULL;
  if (!has_impulse_provider)
    for (size_t i = 0; i < input->arsenal_count; i++) if (qa_same_seat(input->arsenal[i].seat, seat)) selected = &input->arsenal[i];
  qa_actor_command command;
  memset(&command, 0, sizeof command);
  command.actor = local->pub.player.actor;
  command.source.kind = QA_COMMAND_SOURCE_LOCAL_SEAT;
  command.source.seat = seat;
  command.source.client = qa_session_client_id(qa_session_seat_client(local->pub.player.seat));
  command.sequence = input->sequence++;
  command.command = qa_input_command_builder_build(local->pub.builder, &sample, &frame, elapsed);
  if (has_impulse_provider || selected != NULL) {
    qa_arsenal_intent *arsenal = INPUT_PUSH(input->arsenal_out, input->arsenal_out_count, input->arsenal_out_capacity);
    memset(arsenal, 0, sizeof *arsenal);
    arsenal->provider = has_impulse_provider ? impulse_provider : selected->provider;
    arsenal->weapon = has_impulse_provider ? NULL : selected->weapon;
    if (has_impulse_provider && sample.impulse != 0) { arsenal->has_impulse = true; arsenal->impulse = sample.impulse; }
    bool use = false;
    for (size_t b = 0; b < sample.button_count && !use; b++) {
      const qa_button_sample *button = &sample.buttons[b];
      use = (strcmp(button->action, "use") == 0 || strcmp(button->action, "button2") == 0) && (button->active || button->pressed);
    }
    arsenal->use_holdable = strcmp(sample.focus.kind, "game") == 0 && use;
    command.arsenal = arsenal;
  }
  qa_seat_input_sample_dispose(&sample);
  return command;
}

static void reserve_arsenal(qa_app_application_input *input, size_t count) {
  input->arsenal_out_count = 0;
  if (input->arsenal_out_capacity >= count) return;
  qa_arsenal_intent *grown = realloc(input->arsenal_out, (count == 0 ? 1 : count) * sizeof *grown);
  if (grown == NULL) qa_range_error("Out of memory");
  input->arsenal_out = grown; input->arsenal_out_capacity = count == 0 ? 1 : count;
}

qa_actor_command *qa_app_application_input_build(qa_app_application_input *input, double elapsed_milliseconds,
    double server_milliseconds, double server_frame, double wall_elapsed_milliseconds, size_t *out_count) {
  size_t count = input->local_count;
  reserve_arsenal(input, count);
  qa_seat_id *seats = xcalloc(count, sizeof *seats);
  for (size_t i = 0; i < count; i++) seats[i] = local_seat(input->locals[i]);
  qa_actor_command *commands = xcalloc(count, sizeof *commands);
  for (size_t i = 0; i < count; i++)
    commands[i] = build_one(input, seats[i], elapsed_milliseconds, server_milliseconds, server_frame, wall_elapsed_milliseconds);
  free(seats);
  *out_count = count;
  return commands;
}

qa_actor_command qa_app_application_input_build_for_seat(qa_app_application_input *input, qa_seat_id seat,
    double elapsed_milliseconds, double server_milliseconds, double server_frame, double wall_elapsed_milliseconds) {
  reserve_arsenal(input, 1);
  return build_one(input, seat, elapsed_milliseconds, server_milliseconds, server_frame, wall_elapsed_milliseconds);
}

double qa_app_application_input_next_command_sequence(const qa_app_application_input *input) { return input->sequence; }

void qa_app_application_input_resume_commands(qa_app_application_input *input, double sequence) {
  if (!(isfinite(sequence) && trunc(sequence) == sequence && fabs(sequence) <= 9007199254740991.0) || sequence < 0)
    qa_range_error("Input command sequence must be a nonnegative safe integer");
  input->sequence = input->sequence > sequence ? input->sequence : sequence;
}

/* ---- focus / events ---- */
void qa_app_input_internal_synchronize_client_focus(qa_app_application_input *input) {
  for (size_t i = 0; i < input->local_count; i++) {
    input_local *local = input->locals[i];
    qa_seat_input_focus focus = qa_seat_input_get_focus(local->pub.input);
    bool captured = input->actions.client_captures_input != NULL
        && input->actions.client_captures_input(input->actions.ctx, local_seat(local));
    if (captured && strcmp(focus.kind, "game") == 0) {
      qa_seat_input_focus menu;
      memset(&menu, 0, sizeof menu);
      menu.kind = "menu"; menu.menu = "menu:q3:cgame"; menu.control = NULL;
      qa_seat_input_set_focus(local->pub.input, menu, qa_app_input_internal_now(input));
    } else if (!captured && strcmp(focus.kind, "menu") == 0 && focus.menu != NULL && strcmp(focus.menu, "menu:q3:cgame") == 0) {
      qa_seat_input_focus game;
      memset(&game, 0, sizeof game);
      game.kind = "game";
      qa_seat_input_set_focus(local->pub.input, game, qa_app_input_internal_now(input));
    }
  }
}

bool qa_app_application_input_input(qa_app_application_input *input, const qa_seat_input_event *event) {
  if (strcmp(event->kind, "focus") == 0 && !event->focused) {
    input_local *local = qa_app_input_internal_find_local(input, event->seat);
    if (local != NULL) (void)qa_seat_haptics_cancel(local->pub.haptics);
  }
  qa_app_input_internal_synchronize_client_focus(input);
  qa_seat_input *seat = qa_input_router_seat(input->router, event->seat);
  return seat != NULL && qa_seat_input_input(seat, event);
}

/* ---- selections ---- */
void qa_app_application_input_set_q3_command_selection(qa_app_application_input *input, qa_seat_id seat,
    qa_app_q3_command_selection selection) {
  if (qa_app_input_internal_find_local(input, seat) == NULL) qa_input_error("Command selection has no local seat");
  for (size_t i = 0; i < input->q3_count; i++)
    if (qa_same_seat(input->q3[i].seat, seat)) { input->q3[i].selection = selection; return; }
  input_q3_selection *entry = INPUT_PUSH(input->q3, input->q3_count, input->q3_capacity);
  entry->seat = seat; entry->selection = selection;
}

void qa_app_application_input_set_arsenal_selection(qa_app_application_input *input, qa_seat_id seat,
    const qa_app_arsenal_selection *selection) {
  if (qa_app_input_internal_find_local(input, seat) == NULL) qa_input_error("Arsenal selection has no local seat");
  for (size_t i = 0; i < input->arsenal_count; i++)
    if (qa_same_seat(input->arsenal[i].seat, seat)) {
      free(input->arsenal[i].provider); free(input->arsenal[i].weapon);
      if (selection == NULL) {
        memmove(&input->arsenal[i], &input->arsenal[i + 1], (input->arsenal_count - i - 1) * sizeof *input->arsenal);
        input->arsenal_count--;
      } else {
        input->arsenal[i].provider = qa_app_input_internal_dup(selection->provider);
        input->arsenal[i].weapon = qa_app_input_internal_dup(selection->weapon);
      }
      return;
    }
  if (selection == NULL) return;
  input_arsenal *entry = INPUT_PUSH(input->arsenal, input->arsenal_count, input->arsenal_capacity);
  entry->seat = seat;
  entry->provider = qa_app_input_internal_dup(selection->provider);
  entry->weapon = qa_app_input_internal_dup(selection->weapon);
}

void qa_app_application_input_bind_arsenal_provider(qa_app_application_input *input, qa_seat_id seat, qa_provider_id provider) {
  for (size_t i = 0; i < input->arsenal_count; i++)
    if (qa_same_seat(input->arsenal[i].seat, seat) && strcmp(input->arsenal[i].provider, provider) == 0) return;
  qa_app_arsenal_selection selection = {provider, NULL};
  qa_app_application_input_set_arsenal_selection(input, seat, &selection);
}

qa_client_command_owner *qa_app_application_input_client_command_registration(qa_app_application_input *input,
    qa_seat_id seat, const qa_client_command_handler *handler) {
  return qa_client_command_bindings_create_owner(input->client_commands, seat, handler);
}

qa_client_command_bindings *qa_app_application_input_client_commands(const qa_app_application_input *input) {
  return input->client_commands;
}

/* ---- attachUi ---- */
struct qa_app_input_ui_attachment { qa_app_application_input *input; qa_seat_id seat; };

qa_app_input_ui_attachment *qa_app_application_input_attach_ui(qa_app_application_input *input, qa_seat_id seat,
    qa_app_input_ui ui) {
  if (qa_app_input_internal_find_local(input, seat) == NULL) qa_input_error("UI seat has no local input");
  if (qa_app_input_internal_seat_ui(input, seat) != NULL) qa_input_error("Seat UI is already attached");
  input_seat_ui *entry = INPUT_PUSH(input->seat_uis, input->seat_ui_count, input->seat_ui_capacity);
  entry->seat = seat; entry->ui = ui;
  qa_app_input_ui_attachment *attachment = xcalloc(1, sizeof *attachment);
  attachment->input = input; attachment->seat = seat;
  input->refs++;
  return attachment;
}

void qa_app_input_ui_attachment_detach(qa_app_input_ui_attachment *attachment) {
  if (attachment == NULL) return;
  qa_app_application_input *input = attachment->input;
  for (size_t i = 0; i < input->seat_ui_count; i++)
    if (qa_same_seat(input->seat_uis[i].seat, attachment->seat)) {
      memmove(&input->seat_uis[i], &input->seat_uis[i + 1], (input->seat_ui_count - i - 1) * sizeof *input->seat_uis);
      input->seat_ui_count--;
      break;
    }
  free(attachment);
  qa_app_application_input_destroy(input);
}

/* ---- profile / release ---- */
bool qa_app_application_input_profile_changed(qa_app_application_input *input) {
  qa_command_dialect wanted = input->actions.console != NULL ? input->actions.console->dialect(input->actions.console->ctx)
      : input->dialect;
  if (qa_command_buffer_dialect(input->commands) != wanted) return true;
  for (size_t i = 0; i < input->local_count; i++)
    if (qa_seat_input_dialect(input->locals[i]->pub.input) != input->dialect) return true;
  return false;
}

void qa_app_application_input_release_for_profile_change(qa_app_application_input *input,
    const qa_command_append_target *commands) {
  qa_app_input_internal_release_offhand(input, true);
  for (size_t i = 0; i < input->local_count; i++)
    qa_seat_input_release_to(input->locals[i]->pub.input, qa_app_input_internal_now(input), commands);
  qa_app_input_devices_release(input->input_devices, qa_app_input_internal_now(input), commands);
}

/* ---- window / platform ---- */
qa_app_input_window qa_app_application_input_window(const qa_app_application_input *input) { return input->window; }

typedef struct attach_ctx { qa_app_application_input *input; qa_app_input_window window; } attach_ctx;
static void attach_window(void *ctx) {
  attach_ctx *attach = ctx;
  qa_input_router_attach_window(attach->input->router, attach->window.input);
}

void qa_app_application_input_publish_window(qa_app_application_input *input, qa_app_input_window next) {
  if (next.input.ctx == input->window.input.ctx) return;
  qa_app_input_window previous = input->window;
  qa_app_application_input_release_for_profile_change(input, NULL);
  qa_app_application_input_stop_haptics(input);
  qa_event_ring_reset(input->window_events);
  input->window = next;
  attach_ctx attach = {input, next};
  qa_caught_error caught;
  if (!qa_try(attach_window, &attach, &caught)) {
    input->window = previous;
    qa_input_router_attach_window(input->router, previous.input);
    qa_rethrow(&caught);
  }
}

static void controller_record_dispose(void *record) {
  qa_controller_event *event = record;
  if (event->kind == QA_CONTROLLER_EVENT_CONNECTED || event->kind == QA_CONTROLLER_EVENT_REMAPPED)
    qa_controller_device_dispose(&event->device);
}

static void ring_release(qa_event_ring *ring, qa_event_cursor *cursor, void (*dispose_record)(void *)) {
  if (dispose_record != NULL) {
    size_t count = 0;
    void *records = qa_event_ring_drain_all(ring, &count);
    for (size_t i = 0; i < count; i++) dispose_record((char *)records + i * ring->elem_size);
    free(records);
  }
  qa_event_ring_close_cursor(ring, cursor);
  qa_event_ring_destroy(ring);
  free(ring); free(cursor);
}

static qa_event_ring *fresh_ring(size_t capacity, size_t size, qa_event_cursor **cursor) {
  qa_event_ring *ring = xcalloc(1, sizeof *ring);
  if (!qa_event_ring_init(ring, capacity, size)) qa_range_error("Out of memory");
  *cursor = xcalloc(1, sizeof **cursor);
  if (!qa_event_ring_open_cursor(ring, *cursor, false)) qa_range_error("Out of memory");
  return ring;
}

void qa_app_application_input_transfer_platform_to(qa_app_application_input *input, qa_app_application_input *next) {
  ring_release(next->window_events, next->window_cursor, NULL);
  next->window_events = input->window_events; next->window_cursor = input->window_cursor;
  input->window_events = fresh_ring(32768, sizeof(qa_sdl_event), &input->window_cursor);
  ring_release(next->controller_events, next->controller_cursor, controller_record_dispose);
  next->controller_events = input->controller_events; next->controller_cursor = input->controller_cursor;
  input->controller_events = fresh_ring(8192, sizeof(qa_controller_event), &input->controller_cursor);
  qa_app_input_internal_retire_commands(input);
  qa_app_application_input_adopt_startup(next);
  qa_input_router_transfer_window_to(input->router, next->router);
  qa_app_input_internal_activate_commands(next, false);
  qa_input_router_update_capture(next->router);
  next->owns_controllers = input->owns_controllers;
  input->owns_controllers = false;
  next->owns_input_devices = input->owns_input_devices;
  input->owns_input_devices = false;
}

void qa_app_application_input_transfer_platform_to_frontend(qa_app_application_input *input,
    qa_input_router *next, const qa_command_append_target *commands) {
  qa_app_application_input_release_for_profile_change(input, commands);
  qa_app_input_internal_retire_commands(input);
  qa_input_router_transfer_window_to(input->router, next);
  qa_event_ring_drain(input->window_events, input->window_cursor, drain_platform, next);
  qa_event_ring_retire(input->window_events);
  drain_controller_ctx drain = {next};
  qa_event_ring_drain(input->controller_events, input->controller_cursor, drain_controller, &drain);
  qa_event_ring_retire(input->controller_events);
  input->owns_controllers = false;
}

void qa_app_application_input_activate_prepared_platform(qa_app_application_input *input) {
  qa_input_router_attach_window(input->router, input->window.input);
  qa_app_input_internal_activate_commands(input, false);
  qa_app_application_input_adopt_startup(input);
}

/* ---- client publication ---- */
static void client_console_set(qa_app_client_bootstrap *client, qa_session_seat *seat, qa_seat_console *console) {
  for (size_t i = 0; i < client->console_count; i++)
    if (client->consoles[i].seat == seat) { client->consoles[i].console = console; return; }
  qa_app_client_console_entry *entry = INPUT_PUSH(client->consoles, client->console_count, client->console_capacity);
  entry->seat = seat; entry->console = console;
}

void qa_app_application_input_publish_client_seats(qa_app_application_input *input, qa_app_client_bootstrap *client,
    bool retain) {
  for (size_t i = 0; i < input->local_count; i++) {
    input_local *local = input->locals[i];
    qa_seat_console *retained = NULL;
    for (size_t c = 0; c < client->console_count && retained == NULL; c++)
      if (client->consoles[c].seat == local->pub.player.seat) retained = client->consoles[c].console;
    if (retained != NULL && retained != local->pub.console) {
      qa_seat_console_adopt(retained, local->pub.console, qa_seat_input_get_focus(local->pub.input));
      local->pub.console = retained;
      qa_app_input_internal_retain_object(input, retained);
    } else if (retained == NULL) {
      qa_seat_console_publish(local->pub.console, qa_seat_input_get_focus(local->pub.input));
      client_console_set(client, local->pub.player.seat, local->pub.console);
      qa_app_input_internal_hold_for(client, local->pub.console);
    }
  }
  qa_app_prepared_startup *prepared = client->prepared;
  size_t old_count = 0;
  const qa_app_prepared_seat *old = qa_app_prepared_startup_seats(prepared, &old_count);
  qa_app_prepared_seat *seats = xcalloc(input->local_count + old_count, sizeof *seats);
  qa_seat_id *active = xcalloc(input->local_count, sizeof *active);
  size_t count = 0;
  for (size_t i = 0; i < input->local_count; i++) {
    input_local *local = input->locals[i];
    qa_seat_id seat = local_seat(local);
    qa_mouse_settings *mouse = qa_app_input_internal_mouse(input, seat);
    qa_cvar_registry *cvars = qa_app_input_internal_console_seat(input, seat);
    if (cvars == NULL && input->startup != NULL) {
      size_t startup_count = 0;
      const qa_app_prepared_seat *startup_seats = qa_app_prepared_startup_seats(input->startup, &startup_count);
      for (size_t s = 0; s < startup_count && cvars == NULL; s++) if (qa_same_seat(startup_seats[s].id, seat)) cvars = startup_seats[s].cvars;
    }
    if (cvars == NULL && input->has_external_routing) cvars = input->cvar_owner;
    if (mouse == NULL || cvars == NULL) { free(seats); free(active); qa_input_error("Published client seat has no registry owners"); }
    seats[count].id = seat; seats[count].input = local->pub.input; seats[count].mouse = *mouse; seats[count].cvars = cvars;
    seats[count].context = qa_app_input_internal_seat_context(seat.session, &local->pub.player);
    qa_app_input_internal_hold_for(prepared, local->pub.input);
    qa_app_input_internal_hold_for(prepared, mouse->cvars);
    active[count] = seat;
    count++;
  }
  size_t active_count = count;
  if (retain)
    for (size_t o = 0; o < old_count; o++) {
      bool republished = false;
      for (size_t i = 0; i < active_count && !republished; i++) republished = qa_same_seat(active[i], old[o].id);
      if (!republished) seats[count++] = old[o];
    }
  qa_app_prepared_startup_publish_seats(prepared, seats, count, active, active_count, true);
  free(seats); free(active);
  if (input->actions.configuration != NULL) {
    qa_app_profile_binding_choice choices[8];
    size_t choice_count = qa_app_prepared_profile_binding_choices(input->actions.configuration, choices, 8);
    for (size_t i = 0; i < choice_count && i < 8; i++) {
      const qa_app_profile_binding_choice *choice = &choices[i];
      if (!qa_app_prepared_startup_clear_overridden_keys(prepared, choice->id))
        qa_input_error("Published configuration seat is missing");
      (void)qa_app_prepared_startup_publish_seat_choices(prepared, choice->id, choice->all_bindings_chosen,
          choice->selected_bindings != NULL, choice->selected_bindings, choice->selected_count,
          choice->authored_bindings != NULL, choice->authored_bindings, choice->authored_count,
          (const char *const *)choice->overridden_keys, choice->overridden_count);
    }
  }
  qa_app_client_bootstrap_seat *locals = xcalloc(input->local_count, sizeof *locals);
  for (size_t i = 0; i < input->local_count; i++) {
    input_local *local = input->locals[i];
    qa_seat_id seat = local_seat(local);
    size_t prepared_count = 0;
    const qa_app_prepared_seat *prepared_seats = qa_app_prepared_startup_seats(prepared, &prepared_count);
    bool found = false;
    for (size_t s = 0; s < prepared_count && !found; s++) found = qa_same_seat(prepared_seats[s].id, seat);
    if (!found) { free(locals); qa_input_error("Published input seat is missing"); }
    locals[i].client = qa_session_seat_client(local->pub.player.seat);
    locals[i].seat = local->pub.player.seat;
    locals[i].prepared = seat;
  }
  if (!retain) {
    client->local_count = 0;
    for (size_t i = 0; i < input->local_count; i++) *INPUT_PUSH(client->locals, client->local_count, client->local_capacity) = locals[i];
    for (size_t c = client->console_count; c-- > 0;) {
      bool kept = false;
      for (size_t i = 0; i < input->local_count && !kept; i++) kept = locals[i].seat == client->consoles[c].seat;
      if (!kept) client_console_delete(client, client->consoles[c].seat);
    }
  } else
    for (size_t i = 0; i < input->local_count; i++) {
      size_t index = client->local_count;
      for (size_t c = 0; c < client->local_count && index == client->local_count; c++)
        if (client->locals[c].seat == locals[i].seat && client->locals[c].client == locals[i].client) index = c;
      if (index == client->local_count) { free(locals); qa_input_error("Source input attempted to replace a retained client"); }
      client->locals[index] = locals[i];
    }
  free(locals);
}

static void release_append(void *ctx, const char *text, const qa_command_context *source) {
  qa_app_prepared_client_commands_release_append(ctx, text, source);
}

void qa_app_application_input_publish_client_platform(qa_app_application_input *input,
    qa_app_client_bootstrap *client, bool retain) {
  if (input->controllers != client->controllers || input->commands != qa_app_prepared_startup_commands(client->prepared))
    qa_input_error("Client input publication changed retained owners");
  qa_app_application_input_validate_startup_adoption(input);
  qa_app_application_input_validate_candidate_commands(input);
  qa_app_client_input_publication previous = client->platform;
  if (previous.kind == QA_APP_CLIENT_INPUT_WORLD) qa_app_application_input_release_into_candidate(input, previous.input, true);
  else {
    qa_command_append_target target = {input->candidate_program, release_append};
    size_t count = 0;
    const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(client->prepared, &count);
    for (size_t i = 0; i < count; i++)
      qa_seat_input_release_to(seats[i].input, qa_app_input_internal_now(input), input->candidate_program != NULL ? &target : NULL);
  }
  qa_app_application_input_publish_candidate_commands(input);
  qa_app_application_input_publish_client_seats(input, client, retain);
  if (previous.kind == QA_APP_CLIENT_INPUT_WORLD) qa_app_application_input_transfer_platform_to(previous.input, input);
  else {
    if (previous.kind == QA_APP_CLIENT_INPUT_MENU) previous.retire_commands(previous.ctx);
    qa_app_application_input_adopt_startup(input);
    if (previous.kind == QA_APP_CLIENT_INPUT_NONE) qa_input_router_attach_window(input->router, input->window.input);
    else qa_input_router_transfer_window_to(previous.router, input->router);
    qa_app_input_internal_activate_commands(input, false);
    qa_input_router_update_capture(input->router);
  }
  input->owns_controllers = false;
  memset(&client->platform, 0, sizeof client->platform);
  client->platform.kind = QA_APP_CLIENT_INPUT_WORLD;
  client->platform.input = input;
  qa_app_input_internal_hold_for(client, input);
}

/* ---- rebindPlayers ---- */
void qa_app_application_input_rebind_players(qa_app_application_input *input, const qa_app_local_player *players,
    size_t player_count, const qa_app_sim_presentation_access *simulation, bool world) {
  input->simulation = simulation;
  if (world) qa_app_input_internal_release_offhand(input, true);
  if (player_count != input->local_count) qa_input_error("World travel changed the local seat count");
  for (size_t i = 0; i < input->local_count; i++) {
    input_local *local = input->locals[i];
    qa_seat_id seat = local_seat(local);
    const qa_app_local_player *player = NULL;
    for (size_t p = 0; p < player_count && player == NULL; p++)
      if (qa_same_seat(qa_session_seat_id(players[p].seat), seat)) player = &players[p];
    if (player == NULL) qa_input_error("World travel has no player for a local seat");
    input_seat_ui *ui = qa_app_input_internal_seat_ui(input, seat);
    if (ui != NULL && ui->ui.clear_prompt != NULL) ui->ui.clear_prompt(ui->ui.ctx);
    qa_seat_haptics_invalidate_assets(local->pub.haptics);
    if (world) qa_seat_input_release(local->pub.input, qa_app_input_internal_now(input), NULL);
    local->pub.player.actor = player->actor;
    if (world || qa_input_command_builder_dialect(local->pub.builder) != QA_COMMAND_DIALECT_Q3) {
      qa_app_sim_player_view view = simulation->player_view(simulation->ctx, player->actor);
      qa_input_command_builder_set_view_angles(local->pub.builder, view.angles);
    }
  }
  input->q3_count = 0;
  for (size_t i = 0; i < input->arsenal_count; i++) { free(input->arsenal[i].provider); free(input->arsenal[i].weapon); }
  input->arsenal_count = 0;
}

/* ---- saveSettings ---- */
static qa_settings_controller_selection settings_selection(qa_controller_selection selection) {
  qa_settings_controller_selection saved;
  memset(&saved, 0, sizeof saved);
  saved.kind = selection.kind == QA_CONTROLLER_SELECTION_NONE ? QA_CONTROLLER_NONE
      : selection.kind == QA_CONTROLLER_SELECTION_DEVICE ? QA_CONTROLLER_DEVICE
      : selection.kind == QA_CONTROLLER_SELECTION_SERIAL ? QA_CONTROLLER_SERIAL : QA_CONTROLLER_AUTOMATIC;
  memcpy(saved.guid, selection.guid, sizeof saved.guid);
  saved.ordinal = selection.ordinal;
  saved.serial = (char *)selection.serial; /* borrowed for the save */
  return saved;
}

static void save_archive(qa_app_application_input *input, const char *kind, const char *extra, qa_cvar_registry *registry) {
  const char *owner[3] = {kind, qa_app_configuration_dialect_name(qa_cvar_registry_dialect(registry)), extra};
  qa_app_save_cvar_archive(input->settings, owner, extra != NULL ? 3 : 2, registry, NULL, 0);
}

void qa_app_application_input_save_settings(qa_app_application_input *input) {
  if (input->startup != NULL && qa_app_prepared_startup_pending(input->startup)) return;
  qa_app_input_devices_save(input->input_devices);
  qa_app_controller_settings_settle(input->controller_settings);
  for (size_t i = 0; i < input->local_count; i++) {
    input_local *local = input->locals[i];
    qa_seat_id seat = local_seat(local);
    size_t binding_count = 0;
    const qa_input_binding *bindings = qa_seat_input_bindings(local->pub.input, &binding_count);
    qa_input_binding *saved_bindings = xcalloc(binding_count, sizeof *saved_bindings);
    for (size_t b = 0; b < binding_count; b++) {
      saved_bindings[b] = bindings[b];
      if (bindings[b].input.kind == QA_PHYSICAL_INPUT_CONTROLLER_BUTTON || bindings[b].input.kind == QA_PHYSICAL_INPUT_CONTROLLER_AXIS)
        saved_bindings[b].input.device = 0;
    }
    size_t history_count = 0;
    const char **history = qa_console_history_lines(&local->pub.console->history, &history_count);
    qa_seat_settings settings;
    memset(&settings, 0, sizeof settings);
    settings.version = 1;
    settings.bindings = saved_bindings; settings.binding_count = binding_count;
    settings.has_always_run = true;
    settings.always_run = qa_input_command_builder_tuning(local->pub.builder).always_run;
    settings.gamepad = qa_seat_input_gamepad(local->pub.input)->tuning;
    settings.mouse = qa_mouse_input_tuning(qa_input_command_builder_mouse(local->pub.builder));
    settings.history = (char **)history; settings.history_count = history_count;
    settings.rumble = qa_seat_haptics_enabled(local->pub.haptics);
    settings.rumble_strength = qa_seat_haptics_strength(local->pub.haptics);
    settings.controller = settings_selection(qa_input_router_controller_selection(input->router, seat));
    char name[64];
    snprintf(name, sizeof name, "input/seat-%lld.json", (long long)(seat.index + 1));
    bool saved = qa_config_store_save_seat(input->settings, name, &settings);
    free(saved_bindings); free(history);
    if (!saved) qa_input_error("Could not write %s", name);
    qa_app_controller_settings_save(input->controller_settings, seat);
  }
  qa_seat_id keyboard;
  bool has_keyboard = qa_input_router_keyboard_seat(input->router, &keyboard);
  double index = has_keyboard ? (double)keyboard.index : -1;
  if (!qa_config_store_save_input_routing(input->settings, "input/routing.json", index < 0 ? -1 : index, index >= 0))
    qa_input_error("Could not write input/routing.json");
  if (input->archive_persistence && !(input->startup != NULL && qa_app_prepared_startup_pending(input->startup))) {
    save_archive(input, "movement", NULL, input->cvar_owner);
    if (input->console_cvars != input->cvar_owner) save_archive(input, "fallback", NULL, input->console_cvars);
    for (size_t i = 0; i < input->mouse_count; i++) {
      char seat_index[32];
      snprintf(seat_index, sizeof seat_index, "%s", QA_JS_NUMBER((double)input->mice[i].seat.index));
      save_archive(input, "input", seat_index, input->mice[i].settings.cvars);
    }
  }
}

/* ---- close ---- */
void qa_app_application_input_close(qa_app_application_input *input) {
  input->closed = true;
  qa_app_input_internal_release_offhand(input, true);
  if (input->owns_input_devices) qa_app_input_devices_close(input->input_devices);
  qa_app_controller_settings_close(input->controller_settings);
  for (size_t i = 0; i < input->local_count; i++) (void)qa_seat_haptics_close(input->locals[i]->pub.haptics);
  qa_input_router_close(input->router);
  if (input->owns_controllers) qa_sdl_controllers_close(input->controllers);
  qa_app_input_internal_staged_reset(input);
  input->seat_ui_count = 0;
  input->q3_count = 0;
  for (size_t i = 0; i < input->arsenal_count; i++) { free(input->arsenal[i].provider); free(input->arsenal[i].weapon); }
  input->arsenal_count = 0;
  qa_app_input_internal_retire_commands(input);
  if (input->console_routing != NULL) qa_app_console_routing_close(input->console_routing);
  if (input->actions.configuration != NULL) qa_app_console_routing_close(qa_app_prepared_profile_routing(input->actions.configuration));
}

/* ---- ApplicationAudio's view of this input (TS audio.bindHaptics(input) / input.soundHaptics) ---- */
static bool audio_haptic_load(void *ctx, qa_resource_request request, uint8_t **out_bytes, size_t *out_length) {
  qa_app_application_input *input = ctx;
  uint8_t *bytes = input->audio_haptic_load(input->audio_haptic_load_ctx, request, out_length);
  if (bytes == NULL) return false;
  *out_bytes = bytes;
  return true;
}
static void audio_bind_haptics(void *ctx, qa_app_audio_haptic_load_fn load, void *load_ctx) {
  qa_app_application_input *input = ctx;
  input->audio_haptic_load = load; input->audio_haptic_load_ctx = load_ctx;
  qa_app_application_input_bind_haptics(input, audio_haptic_load, input);
}
static void audio_sound_haptics(void *ctx, qa_content_id content, const char *sound, bool has_actor, qa_actor_id actor,
    qa_audio_audience audience) {
  qa_app_application_input_sound_haptics(ctx, content, sound, has_actor ? &actor : NULL, audience);
}
qa_app_audio_haptics qa_app_application_input_audio_haptics(qa_app_application_input *input) {
  qa_app_audio_haptics haptics = {input, audio_sound_haptics, audio_bind_haptics};
  return haptics;
}
