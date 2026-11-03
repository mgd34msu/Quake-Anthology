/* Port of src/app/bootstrap/application.ts, region 4932-5205: step, run,
 * recoverDedicatedFrame, readPixels, captureNextFrame. TS awaits are
 * synchronous calls; the try/catch/finally of step is one qa_try over the
 * frame body. Layout: docs/application_layout.md. GPL-2.0-or-later. */
#include "application_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/frame_loop.h"
#include "qa/app/bootstrap/frame_time.h"
#include "qa/app/bootstrap/player_userinfo.h"
#include "qa/app/bootstrap/q2_match_reports.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/core/common_error.h"
#include "qa/foundation/js.h"
#include "qa/network/q3/adapters.h"
#include "qa/network/common/js_number.h"
#include "qa/app/bootstrap/simulation/q3_commands.h"

static void *checked(void *p) {
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

/* TS presentation-event arrays the frame keeps (owned copies). */
typedef struct event_list { qa_app_sim_presentation_event *items; size_t count, capacity; } event_list;
static void event_list_push(event_list *list, const qa_app_sim_presentation_event *event) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity == 0 ? 8 : list->capacity * 2;
    list->items = checked(realloc(list->items, list->capacity * sizeof *list->items));
  }
  if (!qa_app_sim_presentation_event_copy(&list->items[list->count], event)) qa_range_error("Out of memory");
  list->count++;
}
static void event_list_append_pointers(event_list *list, const qa_app_sim_presentation_event *const *events, size_t count) {
  for (size_t i = 0; i < count; i++) event_list_push(list, events[i]);
}
static void event_list_append(event_list *list, const qa_app_sim_presentation_event *events, size_t count) {
  for (size_t i = 0; i < count; i++) event_list_push(list, &events[i]);
}
static void event_list_free(event_list *list) {
  for (size_t i = 0; i < list->count; i++) qa_app_sim_presentation_event_release(&list->items[i]);
  free(list->items);
  memset(list, 0, sizeof *list);
}
/* Borrowed pointer view (the network and recording layers take pointer arrays). */
static const qa_app_sim_presentation_event **event_pointers(const event_list *list) {
  const qa_app_sim_presentation_event **pointers = checked(calloc(list->count + 1, sizeof *pointers));
  for (size_t i = 0; i < list->count; i++) pointers[i] = &list->items[i];
  return pointers;
}
/* TS this.simulation.drainPresentationEvents() as an owned list. */
static event_list drain_events(qa_application *app) {
  event_list list = { NULL, 0, 0 };
  size_t count = 0;
  const qa_app_sim_presentation_event *const *events = qa_app_shared_sim_drain_presentation_events(app->world_simulation, &count);
  event_list_append_pointers(&list, events, count);
  return list;
}
/* TS roundPresentationEvents.take(roundCursor); retire(). */
static event_list take_round_events(qa_application *app) {
  event_list list = { NULL, 0, 0 };
  size_t count = 0;
  qa_app_sim_presentation_event *taken = qa_event_ring_take(&app->round_presentation_events, &app->round_cursor, &count);
  qa_event_ring_retire(&app->round_presentation_events);
  list.items = taken; list.count = count; list.capacity = count;
  return list;
}
static void set_source_events(qa_application *app, event_list *list) {
  for (size_t i = 0; i < app->source_event_count; i++) qa_app_sim_presentation_event_release(&app->source_events[i]);
  free(app->source_events);
  app->source_events = list->items; app->source_event_count = list->count;
  memset(list, 0, sizeof *list);
}

static bool seat_presentation_menu_open(const qa_application_graphical *graphical) {
  if (graphical == NULL) return false;
  for (size_t i = 0; i < graphical->presentation_count; i++)
    if (qa_app_seat_ui_pause_menu_open(graphical->presentations[i].ui)) return true;
  return false;
}
static bool is_bot_client(qa_application *app, qa_client_id client) {
  const qa_app_sim_bot_client *bots = NULL;
  size_t count = qa_application_bot_clients(app, &bots);
  for (size_t i = 0; i < count; i++) if (qa_same_client(qa_session_client_id(bots[i].client), client)) return true;
  return false;
}
static double source_seconds(const qa_simulation_output *output) {
  qa_source_time time = output->snapshot.frame.time;
  return time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000;
}

/* TS simulation.q2Native() !== null. */
static bool is_q2_native(qa_app_shared_simulation *simulation) {
  qa_app_sim_classic_guest_world *classic = NULL;
  qa_app_sim_rerelease_guest_world *rerelease = NULL;
  return qa_app_shared_sim_q2_native(simulation, &classic, &rerelease);
}

/* TS network.server.poll(performance.now()): the commands of every kind. */
static qa_actor_command *network_poll(qa_application *app, size_t *out_count) {
  *out_count = 0;
  qa_application_native_server *network = app->network;
  if (network == NULL) return NULL;
  double now = qa_application_performance_now();
  size_t count = 0;
  if (network->kind == QA_APPLICATION_NETWORK_UNIFIED) {
    const qa_actor_command *commands = qa_app_unified_server_network_poll(network->unified, now, &count);
    qa_actor_command *out = count == 0 ? NULL : checked(malloc(count * sizeof *out));
    if (count != 0) memcpy(out, commands, count * sizeof *out);
    *out_count = count;
    return out;
  }
  const void *const *commands =
      network->kind == QA_APPLICATION_NETWORK_Q1 ? qa_app_q1_server_network_poll(network->q1, now, &count)
    : network->kind == QA_APPLICATION_NETWORK_QW ? qa_app_qw_server_network_poll(network->qw, now, &count)
    : network->kind == QA_APPLICATION_NETWORK_Q2 ? qa_app_q2_server_network_poll(network->q2, now, &count)
    : qa_app_q3_server_network_poll(network->q3, now, &count);
  qa_actor_command *out = count == 0 ? NULL : checked(malloc(count * sizeof *out));
  for (size_t i = 0; i < count; i++) out[i] = *(const qa_actor_command *)commands[i];
  *out_count = count;
  return out;
}
/* TS network.server.publish(output, sourceEvents, performance.now()). */
static void network_publish(qa_application *app, const qa_simulation_output *output, const event_list *events) {
  qa_application_native_server *network = app->network;
  if (network == NULL) return;
  const qa_app_sim_presentation_event **pointers = event_pointers(events);
  const void *const *opaque = (const void *const *)pointers;
  double now = qa_application_performance_now();
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, (void *)pointers);
  switch (network->kind) {
    case QA_APPLICATION_NETWORK_Q1: qa_app_q1_server_network_publish(network->q1, output, opaque, events->count, source_seconds(output), now); break;
    case QA_APPLICATION_NETWORK_QW: qa_app_qw_server_network_publish(network->qw, output, opaque, events->count, now); break;
    case QA_APPLICATION_NETWORK_Q2: qa_app_q2_server_network_publish(network->q2, output, opaque, events->count, now); break;
    case QA_APPLICATION_NETWORK_Q3: qa_app_q3_server_network_publish(network->q3, output, opaque, events->count, now); break;
    case QA_APPLICATION_NETWORK_UNIFIED: qa_app_unified_server_network_publish(network->unified, output, opaque, events->count, now); break;
  }
  qa_cleanup_pop(&cleanup);
  free(pointers);
}
/* TS recordingHost.host.observe(output, sourceEvents) for the q1, qw and q2 hosts. */
static void recording_observe(qa_application *app, const qa_simulation_output *output, const event_list *events) {
  qa_application_native_server_host *host = app->recording_host;
  const qa_app_sim_presentation_event **pointers = event_pointers(events);
  const void *const *opaque = (const void *const *)pointers;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, (void *)pointers);
  if (host->kind == QA_APPLICATION_NETWORK_Q1) host->q1.observe(host->q1.ctx, output, opaque, events->count);
  else if (host->kind == QA_APPLICATION_NETWORK_QW) host->qw.observe(host->qw.ctx, output, opaque, events->count);
  else host->q2.observe(host->q2.ctx, output, opaque, events->count);
  qa_cleanup_pop(&cleanup);
  free(pointers);
}

/* ---- the step frame ---- */
typedef struct step_frame {
  qa_application *app;
  double elapsed_milliseconds;
  double frame_started_at;
  qa_simulation_output result;
  /* progress closure state */
  event_list before_frame;           /* TS beforeFrameEvents */
  bool pending_consumed;             /* TS pendingBeforeFrameEvents = [] after the first progress */
  event_list progressed_events, progressed_round_events;
  double displayed_milliseconds;
  size_t progressed_event_count;
  double frame_milliseconds;
  qa_actor_command *commands; size_t command_count;
} step_frame;

/* TS progress(frame): native Q2 sources present each progressed frame. */
static void step_progress(void *ctx, qa_simulation_progress frame) {
  step_frame *f = ctx;
  qa_application *app = f->app;
  f->progressed_event_count += frame.output->event_count;
  qa_application_pump_client_input(app);
  qa_application_native_server_host *host = app->recording_host;
  if (host == NULL || host->kind != QA_APPLICATION_NETWORK_Q2 || host->q2.observe_progress == NULL || host->q2.local_messages == NULL)
    qa_raise("Error", 0, 0, "Native source progress lost its message publication owner");
  host->q2.observe_progress(host->q2.ctx);
  event_list events = drain_events(app);
  event_list_append(&f->progressed_events, events.items, events.count);
  event_list rounds = take_round_events(app);
  event_list_append(&f->progressed_round_events, rounds.items, rounds.count);
  event_list incoming = { NULL, 0, 0 };
  if (!f->pending_consumed) event_list_append(&incoming, f->before_frame.items, f->before_frame.count);
  f->pending_consumed = true;
  event_list_append(&incoming, events.items, events.count);
  double time_milliseconds = app->elapsed - frame.pending_milliseconds;
  const qa_app_sim_presentation_event **incoming_pointers = event_pointers(&incoming);
  const qa_app_sim_presentation_event **round_pointers = event_pointers(&rounds);
  qa_application_present_frame present = { incoming_pointers, incoming.count, round_pointers, rounds.count, NULL, 0,
    qa_js_max(0, time_milliseconds - f->displayed_milliseconds), qa_application_performance_now(), time_milliseconds };
  qa_application_present_output(app, *frame.output, &present);
  free(incoming_pointers); free(round_pointers);
  event_list_free(&incoming); event_list_free(&events); event_list_free(&rounds);
  f->displayed_milliseconds = time_milliseconds;
  /* TS awaits setTimeout(0) here; the synchronous port has no timer queue to drain. */
  qa_application_pump_client_input(app);
}
static void *advance_simulation(void *ctx) {
  step_frame *f = ctx;
  qa_application *app = f->app;
  qa_input_batch input = { f->frame_milliseconds, f->commands, f->command_count };
  bool progress = app->graphical != NULL && is_q2_native(app->world_simulation);
  f->result = qa_engine_session_step_async(app->session, input, progress ? step_progress : NULL, f);
  return &f->result;
}

/* TS movie branch of step. Returns true when step returns retained.output. */
typedef struct movie_frame { step_frame *frame; qa_application_movie movie; qa_app_shared_simulation *simulation; qa_simulation_output output; } movie_frame;
static void movie_body(void *ctx) {
  movie_frame *m = ctx;
  qa_application *app = m->frame->app;
  qa_application_graphical *graphical = app->graphical;
  bool console_open = false;
  if (graphical != NULL) {
    size_t locals = qa_app_application_input_local_count(graphical->input);
    for (size_t i = 0; i < locals; i++) {
      qa_seat_input_focus focus = qa_seat_input_get_focus(qa_app_application_input_local(graphical->input, i)->input);
      if (strcmp(focus.kind, "console") == 0) { console_open = true; break; }
    }
  }
  bool menu_open = seat_presentation_menu_open(graphical);
  qa_app_campaign_cinematic_activate(m->movie.playback);
  app->frames++;
  if (qa_app_campaign_cinematic_frame(m->movie.playback, m->frame->elapsed_milliseconds, ++app->presentation_frames, console_open, menu_open)) {
    qa_application_movie *current = NULL;
    for (size_t i = 0; i < app->movie_count; i++) if (app->movies[i].simulation == m->simulation) current = &app->movies[i];
    if (current != NULL && current->playback == m->movie.playback) {
      size_t index = (size_t)(current - app->movies);
      memmove(&app->movies[index], &app->movies[index + 1], (app->movie_count - index - 1) * sizeof *app->movies);
      app->movie_count--;
    }
    qa_app_campaign_cinematic_close(m->movie.playback, app->presentation_frames);
    if (m->movie.current(m->movie.ctx) && app->world_simulation == m->simulation) m->movie.complete(m->movie.ctx);
    for (size_t i = 0; i < app->movie_request_count; i++)
      if (app->movie_request_simulations[i] == m->simulation && app->movie_requests[i] == m->movie.request) {
        app->movie_request_simulations[i] = app->movie_request_simulations[app->movie_request_count - 1];
        app->movie_requests[i] = app->movie_requests[app->movie_request_count - 1];
        app->movie_request_count--;
        break;
      }
  } else if (console_open && graphical != NULL) {
    /* TS: this.simulation.presentations() and characterViews(), then prepare(snapshot, ...). */
    size_t presentation_count = 0, character_count = 0;
    const qa_app_simulation_presentation *presentations =
        qa_app_shared_sim_presentations(app->world_simulation, &presentation_count);
    const qa_q3_character_view *characters = qa_app_shared_sim_character_views(app->world_simulation, &character_count);
    for (size_t i = 0; i < graphical->presentation_count; i++) {
      qa_application_seat_presentation *presentation = &graphical->presentations[i];
      qa_app_world_seat_presentation_prepare(presentation->presentation, &m->output.snapshot,
          presentations, presentation_count, characters, character_count);
      (void)qa_session_seat_present(presentation->local->player.seat, m->output.snapshot, qa_app_native_renderer_backend(graphical->renderer));
    }
    qa_render_command swap;
    memset(&swap, 0, sizeof swap);
    swap.kind = QA_RENDER_COMMAND_SWAP_BUFFERS;
    qa_render_frame frame = { qa_app_application_assets_images(graphical->assets)->owner, app->presentation_frames, &swap, 1 };
    qa_app_native_renderer_execute(graphical->renderer, frame);
  }
}
static void forget_movie(qa_application *app, qa_app_shared_simulation *simulation, const qa_application_movie *movie) {
  for (size_t i = 0; i < app->movie_count; i++)
    if (app->movies[i].simulation == simulation && app->movies[i].playback == movie->playback) {
      memmove(&app->movies[i], &app->movies[i + 1], (app->movie_count - i - 1) * sizeof *app->movies);
      app->movie_count--;
      break;
    }
  for (size_t i = 0; i < app->movie_request_count; i++)
    if (app->movie_request_simulations[i] == simulation && app->movie_requests[i] == movie->request) {
      app->movie_request_simulations[i] = app->movie_request_simulations[app->movie_request_count - 1];
      app->movie_requests[i] = app->movie_requests[app->movie_request_count - 1];
      app->movie_request_count--;
      break;
    }
}
static void movie_close_entry(void *ctx) {
  movie_frame *m = ctx;
  qa_app_campaign_cinematic_close(m->movie.playback, m->frame->app->presentation_frames);
}

static void run_save_operation(void *ctx) {
  qa_application *app = ctx;
  app->save_operation.run(app, app->save_operation.ctx);
}

/* TS q3 user command selection per seat, arsenal bindings and selections. */
static void bind_arsenal_selections(qa_application *app) {
  qa_application_graphical *graphical = app->graphical;
  if (graphical == NULL) return;
  size_t locals = qa_app_application_input_local_count(graphical->input);
  for (size_t i = 0; i < locals; i++) {
    qa_app_local_input *local = qa_app_application_input_local(graphical->input, i);
    qa_arsenal_state arsenal; bool owned = false;
    if (qa_app_shared_sim_arsenal_state(app->world_simulation, local->player.actor, &arsenal, &owned)) {
      qa_app_application_input_bind_arsenal_provider(graphical->input, qa_session_seat_id(local->player.seat), arsenal.provider);
      if (owned) free((void *)arsenal.ammo);
    }
  }
  for (size_t i = 0; i < graphical->q3_count; i++) {
    qa_seat_id seat = graphical->q3_seats[i];
    qa_application_q3_seat_client *source = &graphical->q3[i];
    qa_app_q3_command_selection selection = qa_app_application_q3_client_user_command_selection(source->client);
    qa_app_application_input_set_q3_command_selection(graphical->input, seat, selection);
    qa_actor_id actor = source->actor;
    qa_arsenal_state arsenal; bool owned = false;
    bool has_arsenal = qa_app_shared_sim_arsenal_state(app->world_simulation, actor, &arsenal, &owned);
    double explicit_weapon = 0;
    bool has_explicit = qa_app_application_q3_client_consume_weapon_selection(source->client, &explicit_weapon);
    bool supplemental = false;
    if (has_arsenal) {
      qa_app_sim_weapon_slot_projection slot = qa_app_shared_sim_weapon_slot(app->world_simulation, actor);
      const qa_weapon_reference *weapons[2] = { slot.active, slot.pending };
      for (size_t w = 0; w < 2; w++)
        if (weapons[w] != NULL && strcmp(weapons[w]->provider, arsenal.provider) != 0) supplemental = true;
      free((void *)slot.ui.items);
    }
    bool selected_source = has_arsenal && (qa_app_shared_sim_q3_guest(app->world_simulation) != NULL
        || strcmp(arsenal.provider, qa_app_shared_sim_recipe(app->world_simulation)->map.entities.provider) != 0);
    bool has_weapon = supplemental || selected_source ? has_explicit : true;
    double weapon = supplemental || selected_source ? explicit_weapon : selection.weapon;
    if (!has_arsenal) { qa_app_application_input_set_arsenal_selection(graphical->input, seat, NULL); continue; }
    qa_app_arsenal_selection value = { arsenal.provider, NULL };
    if (strcmp(arsenal.state.kind, "q3") == 0 && has_weapon) {
      const qa_q3_weapon_item *item = qa_q3_get_weapon_item(weapon);
      value.weapon = item == NULL ? NULL : item->item;
    }
    qa_app_application_input_set_arsenal_selection(graphical->input, seat, &value);
    if (owned) free((void *)arsenal.ammo);
  }
}

/* TS captureTransitionPending branch: requests queued during the frame are held
 * back while sourceActions, commands and applyTransition run, then re-admitted. */
typedef struct held_frame { qa_application *app; qa_application_command_request *held; size_t count; } held_frame;
static void held_transition(void *ctx) {
  held_frame *hold = ctx;
  qa_application_source_actions(hold->app, hold->app->source_events, hold->app->source_event_count);
  qa_application_commands(hold->app);
  qa_application_apply_transition(hold->app);
}

/* TS () => this.flushClientCommands() and () => !this.clientCommandsBlocked. */
static void flush_after_dispatch(void *ctx) { qa_application_flush_client_commands(ctx); }
static bool client_commands_unblocked(void *ctx) { return !qa_application_client_commands_blocked(ctx); }
/* TS () => this.afterCommandDispatch() and () => !this.clientCommandsBlocked && !this.pendingShellPublication. */
static void after_dispatch(void *ctx) { qa_application_after_command_dispatch(ctx); }
static bool source_commands_continue(void *ctx) {
  return !qa_application_client_commands_blocked(ctx) && !qa_application_pending_shell_publication(ctx);
}
/* TS refreshQ3Masters(cvars, text => this.host.print(text)): the operator state reads print only. */
static void operator_print(void *ctx, const char *text) { qa_application_print(ctx, text); }
static qa_app_server_operator_host operator_print_host(qa_application *app) {
  qa_app_server_operator_host host;
  memset(&host, 0, sizeof host);
  host.ctx = app;
  host.print = operator_print;
  return host;
}
/* TS q2GameCallback(() => native.client.world.userinfo(native.client.sourceSlot, `${userinfo}\\ip\\localhost`)). */
typedef struct native_userinfo_call { qa_application *app; double slot; const char *value; } native_userinfo_call;
static void native_userinfo_run(void *ctx) {
  native_userinfo_call *call = ctx;
  qa_app_sim_classic_guest_world *classic = NULL;
  qa_app_sim_rerelease_guest_world *rerelease = NULL;
  (void)qa_app_shared_sim_q2_native(call->app->world_simulation, &classic, &rerelease);
  if (classic != NULL) qa_app_sim_classic_guest_world_userinfo(classic, call->slot, call->value);
  else if (rerelease != NULL) qa_app_sim_rerelease_guest_world_userinfo(rerelease, call->slot, call->value);
}
static void native_userinfo(qa_application *app, const qa_application_native_q2_seat_client *native, const char *userinfo) {
  size_t length = strlen(userinfo) + sizeof "\\ip\\localhost";
  char *value = checked(malloc(length));
  (void)snprintf(value, length, "%s\\ip\\localhost", userinfo);
  native_userinfo_call call = { app, native->source_slot, value };
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, value);
  qa_app_q2_game_callback(native_userinfo_run, &call);
  qa_cleanup_pop(&cleanup);
  free(value);
}
/* TS native.client.world.actor(native.client.sourceSlot). */
static bool native_actor(qa_application *app, const qa_application_native_q2_seat_client *native, qa_actor_id *out) {
  qa_app_sim_classic_guest_world *classic = NULL;
  qa_app_sim_rerelease_guest_world *rerelease = NULL;
  (void)qa_app_shared_sim_q2_native(app->world_simulation, &classic, &rerelease);
  if (classic != NULL) return qa_app_sim_classic_guest_world_actor(classic, native->source_slot, out);
  if (rerelease != NULL) return qa_app_sim_rerelease_guest_world_actor(rerelease, native->source_slot, out);
  return false;
}

static void step_body(void *ctx) {
  step_frame *f = ctx;
  qa_application *app = f->app;
  qa_application_retire_removed_local_players(app);
  if (app->stopping && qa_application_local_player_count(app) == 0) {
    f->result = app->has_last_output ? app->last_output : qa_app_shared_sim_current_output(app->world_simulation);
    return;
  }
  if (app->has_save_operation) {
    app->has_save_operation = false;
    qa_application_save_operation operation = app->save_operation;
    qa_caught_error caught;
    if (qa_try(run_save_operation, app, &caught)) operation.settle(operation.ctx, NULL);
    else {
      operation.settle(operation.ctx, &caught);
      if (app->fatal_world_failure) qa_rethrow(&caught);
    }
  }
  qa_application_apply_transition(app);
  if (app->ownership.borrowed_client == NULL) {
    bool startup_frame = qa_application_advance_client_startup(app);
    qa_application_flush_client_commands(app);
    qa_application_pump_client_input(app);
    if (!startup_frame && app->graphical != NULL)
      (void)qa_command_buffer_execute_async(qa_app_application_input_commands(app->graphical->input),
          flush_after_dispatch, app, client_commands_unblocked, app);
  }
  qa_application_enable_startup_persistence(app);
  qa_application_movie *found = qa_application_campaign_movie(app);
  qa_application_movie movie;
  bool has_movie = found != NULL;
  if (has_movie) movie = *found;
  if (has_movie && !movie.current(movie.ctx)) {
    forget_movie(app, app->world_simulation, &movie);
    qa_app_campaign_cinematic_close(movie.playback, app->presentation_frames);
    has_movie = false;
  }
  if (has_movie) {
    movie_frame m;
    m.frame = f; m.movie = movie;
    if (app->has_last_output && app->last_output_simulation == app->world_simulation) { m.simulation = app->last_output_simulation; m.output = app->last_output; }
    else { m.simulation = app->world_simulation; m.output = qa_app_shared_sim_current_output(app->world_simulation); }
    qa_caught_error caught;
    if (!qa_try(movie_body, &m, &caught)) {
      forget_movie(app, m.simulation, &movie);
      qa_caught_error close_error;
      if (!qa_try(movie_close_entry, &m, &close_error)) qa_application_printf(app, "%s: %s\n", close_error.name, close_error.message);
      qa_application_report_campaign_travel_error(app, &caught);
    }
    qa_application_commands(app);
    if (app->world_simulation == m.simulation) { f->result = m.output; return; }
  }
  qa_application_retire_removed_local_players(app);
  if (app->stopping && qa_application_local_player_count(app) == 0) {
    f->result = app->has_last_output ? app->last_output : qa_app_shared_sim_current_output(app->world_simulation);
    return;
  }
  qa_application_dispatch_client_inputs(app);
  bool q1 = qa_app_shared_sim_q1_source(app->world_simulation) != NULL || qa_app_shared_sim_quakec_source(app->world_simulation) != NULL;
  event_list before_frame = { NULL, 0, 0 };
  if (q1) before_frame = drain_events(app);
  {
    const qa_app_sim_presentation_event **pointers = event_pointers(&before_frame);
    qa_application_append_q1_commands(app, pointers, before_frame.count);
    free(pointers);
  }
  if (app->dedicated_commands != NULL && app->dedicated_console != NULL) (void)qa_dedicated_console_drain(app->dedicated_console, app->dedicated_commands);
  if (app->source_commands != NULL)
    (void)qa_command_buffer_execute_async(app->source_commands, after_dispatch, app, source_commands_continue, app);
  if (app->dedicated_commands != NULL && app->dedicated_commands != app->source_commands)
    (void)qa_command_buffer_execute_async(app->dedicated_commands, after_dispatch, app, source_commands_continue, app);
  bool capture_transition_pending = qa_application_capture_blocks_transition(app);
  const qa_ss_source_runtime *q1_source = qa_app_shared_sim_q1_source(app->world_simulation);
  qa_cvar_registry *bot_configuration = q1_source != NULL ? q1_source->q1_cvars
      : app->q2_console != NULL ? qa_app_q2_console_cvars(app->q2_console) : NULL;
  if (app->bots == NULL && qa_app_shared_sim_q3_source(app->world_simulation) == NULL
      && (bot_configuration == NULL ? 0 : qa_cvar_registry_variable_value(bot_configuration, "bot_minplayers")) > 0) {
    qa_app_sim_bot_simulation access = qa_app_shared_sim_bot_simulation(app->world_simulation);
    const char *unsupported = qa_app_sim_bot_admission_error(&access);
    if (unsupported == NULL) {
      qa_application_create_bots_setup setup;
      memset(&setup, 0, sizeof setup);
      setup.requested = true;
      app->bots = qa_application_create_bots(app, app->loaded_content, app->world_simulation, &setup);
    } else {
      qa_application_printf(app, "%s\n", unsupported);
      if (bot_configuration != NULL) qa_application_cvar_set(bot_configuration, "bot_minplayers", "0", true);
    }
  }
  if (app->local_guest != NULL) {
    qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(app->world_simulation);
    for (size_t i = 0; i < app->local_guest->seat_count; i++) {
      qa_application_local_q3_guest_seat *seat = &app->local_guest->seats[i];
      bool has_player = false;
      qa_app_q3_player player;
      if (guest != NULL) {
        qa_app_q3_player players[64];
        size_t count = qa_app_q3_qvm_server_game_players(guest, players, 64);
        for (size_t p = 0; p < count && p < 64; p++)
          if (players[p].source_entity == qa_app_local_q3_client_state_client_number(seat->state)) { player = players[p]; has_player = true; break; }
      }
      char *info = checked(qa_cvar_registry_info_string(seat->cvars, QA_CVAR_FLAG_USER_INFO,
          qa_cvar_registry_dialect(seat->cvars) == QA_COMMAND_DIALECT_Q3 ? 1024 : 512));
      size_t length = strlen(info) + sizeof "\\ip\\localhost";
      char *userinfo = checked(malloc(length));
      (void)snprintf(userinfo, length, "%s\\ip\\localhost", info);
      free(info);
      if (has_player && (seat->userinfo == NULL || strcmp(userinfo, seat->userinfo) != 0)) {
        app->local_guest->authority.host.userinfo(app->local_guest->authority.host.ctx, player, userinfo);
        free(seat->userinfo); seat->userinfo = userinfo;
      } else free(userinfo);
    }
  }
  bool has_retained = app->has_last_output;
  qa_app_shared_simulation *retained_simulation = app->last_output_simulation;
  qa_simulation_output retained = app->last_output;
  size_t remote_count = 0;
  qa_actor_command *remote = network_poll(app, &remote_count);
  qa_cleanup remote_cleanup;
  qa_cleanup_push(&remote_cleanup, free, remote);
  if (app->closed) qa_raise("Error", 0, 0, "Application closed during step");
  bool menu_pause = seat_presentation_menu_open(app->graphical);
  bool menu_pause_request = menu_pause && strcmp(app->launch_options.mode, "singleplayer") == 0;
  qa_app_q3_source_runtime *q3_source = qa_app_shared_sim_q3_source(app->world_simulation);
  qa_app_q3_qvm_server_game *q3_guest = qa_app_shared_sim_q3_guest(app->world_simulation);
  qa_cvar_registry *q3_cvars = q3_source != NULL ? qa_app_q3_source_runtime_host(q3_source)->cvars
      : q3_guest != NULL ? qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(q3_guest)) : NULL;
  if (q3_cvars != NULL && menu_pause_request != app->q3_menu_pause)
    for (size_t i = 0; i < app->client_cvar_count; i++) qa_application_cvar_set(app->client_cvars[i], "cl_paused", menu_pause_request ? "1" : "0", true);
  app->q3_menu_pause = q3_cvars != NULL && menu_pause_request;
  bool paused;
  if (q3_cvars != NULL) {
    bool requested = menu_pause_request;
    for (size_t i = 0; i < app->client_cvar_count && !requested; i++)
      if (qa_cvar_registry_variable_value(app->client_cvars[i], "cl_paused") != 0) requested = true;
    size_t client_count = 0;
    const qa_client_id *clients = qa_app_shared_sim_client_identities(app->world_simulation, &client_count);
    double humans = 0;
    for (size_t i = 0; i < client_count; i++) if (!is_bot_client(app, clients[i])) humans++;
    paused = qa_app_q3_server_paused(q3_cvars, requested, humans);
  } else {
    size_t player_count = 0;
    (void)qa_app_shared_sim_players(app->world_simulation, &player_count);
    const qa_app_sim_bot_client *bots = NULL;
    size_t bot_count = qa_application_bot_clients(app, &bots);
    paused = strcmp(app->launch_options.mode, "singleplayer") == 0 && app->network == NULL
        && player_count == qa_application_local_player_count(app) + bot_count && menu_pause
        && has_retained && retained_simulation == app->world_simulation;
  }
  if (q3_cvars != NULL) {
    char *paused_text = qa_application_cvar_string(q3_cvars, "sv_paused");
    for (size_t i = 0; i < app->client_cvar_count; i++) qa_application_cvar_set(app->client_cvars[i], "sv_paused", paused_text, true);
    free(paused_text);
  }
  qa_cvar_registry *time_cvars = qa_application_source_cvars(app, NULL);
  qa_app_frame_host frame_host = { app->launch_options.dedicated, true };
  double source_milliseconds = time_cvars == NULL ? f->elapsed_milliseconds
      : qa_app_source_frame_milliseconds(qa_application_source_dialect(app, app->loaded_content), f->elapsed_milliseconds,
                                         qa_app_read_frame_time_controls(time_cvars), frame_host);
  qa_app_capture_frame_result capture_frame = { source_milliseconds, false };
  if (app->graphical != NULL && app->tools != NULL) capture_frame = qa_app_application_tools_frame_time(app->tools, source_milliseconds, true);
  double frame_milliseconds = capture_frame.milliseconds;
  if (capture_frame.capture && app->capture != NULL) qa_app_capture_frame(app->capture);
  if (app->tools != NULL) qa_omni_timer_stamp(qa_app_application_tools_timer(app->tools), "frame begin");
  app->elapsed += qa_app_shared_sim_take_admission_settle_milliseconds(app->world_simulation);
  if (!paused) app->elapsed += frame_milliseconds;
  if (app->closed) qa_raise("Error", 0, 0, "Application closed during step");
  qa_cvar_registry *server_cvars = qa_application_source_cvars(app, NULL);
  if (server_cvars != NULL && qa_cvar_registry_dialect(server_cvars) == QA_COMMAND_DIALECT_Q3 && app->launch_options.dedicated
      && qa_cvar_registry_variable_value(server_cvars, "dedicated") == 2 && app->operator_state != NULL && app->network != NULL)
    qa_app_server_operator_refresh_q3_masters(app->operator_state, operator_print_host(app), server_cvars);
  if (app->graphical != NULL) {
    size_t locals = qa_app_application_input_local_count(app->graphical->input);
    for (size_t i = 0; i < locals; i++) {
      qa_app_local_input *local = qa_app_application_input_local(app->graphical->input, i);
      qa_cvar_registry *cvars = qa_application_client_cvars_get(app, qa_session_seat_id(local->player.seat));
      if (cvars == NULL) continue;
      char *userinfo = checked(qa_app_player_userinfo(cvars));
      const char *published = NULL;
      size_t index = 0;
      for (; index < app->published_userinfo_count; index++)
        if (app->published_userinfo_cvars[index] == cvars) { published = app->published_userinfo[index]; break; }
      if (published == NULL || strcmp(published, userinfo) != 0) {
        qa_app_shared_sim_update_player_userinfo(app->world_simulation, local->player.actor, userinfo);
        if (published == NULL) {
          if (app->published_userinfo_count == app->published_userinfo_capacity) {
            app->published_userinfo_capacity = app->published_userinfo_capacity == 0 ? 4 : app->published_userinfo_capacity * 2;
            app->published_userinfo_cvars = checked(realloc(app->published_userinfo_cvars, app->published_userinfo_capacity * sizeof *app->published_userinfo_cvars));
            app->published_userinfo = checked(realloc(app->published_userinfo, app->published_userinfo_capacity * sizeof *app->published_userinfo));
          }
          app->published_userinfo_cvars[app->published_userinfo_count] = cvars;
          app->published_userinfo[app->published_userinfo_count++] = userinfo;
        } else { free(app->published_userinfo[index]); app->published_userinfo[index] = userinfo; }
      } else free(userinfo);
    }
    for (size_t i = 0; i < app->graphical->native_q2_count; i++) {
      qa_application_native_q2_seat_client *native = &app->graphical->native_q2[i];
      char *userinfo = checked(qa_cvar_registry_info_string(native->cvars, QA_CVAR_FLAG_USER_INFO,
          qa_cvar_registry_dialect(native->cvars) == QA_COMMAND_DIALECT_Q3 ? 1024 : 512));
      if (native->userinfo != NULL && strcmp(userinfo, native->userinfo) == 0) { free(userinfo); continue; }
      native_userinfo(app, native, userinfo);
      qa_actor_id actor;
      if (native_actor(app, native, &actor)) qa_app_shared_sim_notify_client_event(app->world_simulation, "userinfo", actor);
      free(native->userinfo); native->userinfo = userinfo;
    }
  }
  bind_arsenal_selections(app);
  if (paused && app->graphical != NULL) {
    size_t locals = qa_app_application_input_local_count(app->graphical->input);
    for (size_t i = 0; i < locals; i++) {
      qa_seat_input_sample sample;
      if (qa_seat_input_take_sample(qa_app_application_input_local(app->graphical->input, i)->input,
              qa_app_application_input_now(app->graphical->input), f->elapsed_milliseconds, &sample))
        qa_seat_input_sample_dispose(&sample);
    }
  }
  size_t local_count = 0;
  qa_actor_command *local_commands = paused || app->graphical == NULL ? NULL
      : qa_app_application_input_build(app->graphical->input, frame_milliseconds, app->elapsed, app->frames, f->elapsed_milliseconds, &local_count);
  qa_cleanup local_cleanup;
  qa_cleanup_push(&local_cleanup, free, local_commands);
  for (size_t i = 0; i < local_count; i++) {
    const qa_actor_command *command = &local_commands[i];
    if (command->source.kind != QA_COMMAND_SOURCE_LOCAL_SEAT || app->local_guest == NULL) continue;
    for (size_t s = 0; s < app->local_guest->seat_count; s++) {
      if (!qa_same_seat(app->local_guest->seat_ids[s], command->source.seat)) continue;
      qa_q3_wire_user_command wire = command->command.kind == QA_USER_COMMAND_Q3 ? qa_q3_from_user_command(&command->command.as.q3)
          : qa_app_sim_wire_q3_user_command(command->command, app->elapsed, 0);
      (void)qa_q3_wire_command_history_append(qa_app_local_q3_client_state_commands(app->local_guest->seats[s].state), &wire);
      break;
    }
  }
  f->before_frame = before_frame;
  f->pending_consumed = false;
  f->displayed_milliseconds = app->elapsed - frame_milliseconds;
  f->frame_milliseconds = frame_milliseconds;
  f->command_count = local_count + remote_count;
  f->commands = f->command_count == 0 ? NULL : checked(malloc(f->command_count * sizeof *f->commands));
  if (local_count != 0) memcpy(f->commands, local_commands, local_count * sizeof *f->commands);
  if (remote_count != 0) memcpy(f->commands + local_count, remote, remote_count * sizeof *f->commands);
  qa_simulation_output output;
  if (paused) {
    if (has_retained && retained_simulation == app->world_simulation) { output = retained; output.events = NULL; output.event_count = 0; }
    else output = qa_app_shared_sim_current_output(app->world_simulation);
  } else if (app->tools == NULL) output = *(qa_simulation_output *)advance_simulation(f);
  else output = *(qa_simulation_output *)qa_app_application_tools_measure(app->tools, "simulation", advance_simulation, f);
  free(f->commands); f->commands = NULL;
  qa_application_retire_removed_local_players(app);
  qa_app_shared_simulation *completed_lobby_simulation = NULL;
  if (app->host.lobby != NULL) {
    qa_app_q2_match_simulation match = qa_app_shared_sim_q2_match_simulation(app->world_simulation);
    if (qa_app_q2_match_completed(&match, NULL)) completed_lobby_simulation = app->world_simulation;
  }
  qa_application_queue_ranking_frame(app);
  app->has_last_output = true; app->last_output_simulation = app->world_simulation; app->last_output = output;
  if (!paused) qa_application_publish_local_guest_snapshots(app);
  if (app->guest_browser != NULL) { qa_app_startup_server_browser_poll(app->guest_browser->host); qa_q3_browser_view_poll(app->guest_browser->view); }
  app->frames++;
  event_list frame_events = drain_events(app);
  if (q1) {
    const qa_app_sim_presentation_event **pointers = event_pointers(&frame_events);
    qa_application_append_q1_commands(app, pointers, frame_events.count);
    free(pointers);
  }
  {
    event_list source = { NULL, 0, 0 };
    event_list_append(&source, f->before_frame.items, f->before_frame.count);
    event_list_append(&source, f->progressed_events.items, f->progressed_events.count);
    event_list_append(&source, frame_events.items, frame_events.count);
    set_source_events(app, &source);
  }
  if (!paused && app->bots != NULL) app->bots->receive(app->bots->ctx, app->source_events, app->source_event_count);
  if (app->closed) qa_raise("Error", 0, 0, "Application closed during step");
  event_list current = { app->source_events, app->source_event_count, app->source_event_count };
  if (!paused) network_publish(app, &output, &current);
  if (app->network == NULL && app->recording_host != NULL && app->recording_host->kind != QA_APPLICATION_NETWORK_Q3
      && (app->local_recording_active || is_q2_native(app->world_simulation)))
    recording_observe(app, &output, &current);
  if (app->local_recording != NULL && app->local_recording_active) {
    const qa_app_sim_presentation_event **pointers = event_pointers(&current);
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, (void *)pointers);
    qa_app_local_demo_recording_publish(app->local_recording, &output, pointers, current.count);
    qa_cleanup_pop(&cleanup);
    free(pointers);
  }
  size_t intent_count = 0;
  const qa_transition_intent *intents = paused ? NULL : qa_app_shared_sim_take_transitions(app->world_simulation, &intent_count);
  if (intent_count != 0) {
    const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(app->loaded_content);
    qa_transition_mode mode;
    memset(&mode, 0, sizeof mode);
    if (recipe->campaign.is_campaign) { mode.kind = "campaign"; mode.campaign = recipe->campaign.mission.provider; mode.allow_round_restart = false; }
    else { mode.kind = "competitive"; mode.match = recipe->match.provider; }
    int64_t id = -1;
    qa_transition_decision decision = qa_shared_transition_coordinator_resolve(app->transitions, mode, intents, intent_count, &id);
    qa_shared_transition_coordinator_commit(app->transitions, id, decision);
  }
  event_list final_rounds = take_round_events(app);
  event_list round_events = { NULL, 0, 0 };
  event_list_append(&round_events, f->progressed_round_events.items, f->progressed_round_events.count);
  event_list_append(&round_events, final_rounds.items, final_rounds.count);
  qa_simulation_output final_presentation = output;
  if (f->progressed_event_count != 0) {
    final_presentation.events = output.event_count > f->progressed_event_count ? output.events + f->progressed_event_count : NULL;
    final_presentation.event_count = output.event_count > f->progressed_event_count ? output.event_count - f->progressed_event_count : 0;
  }
  {
    event_list incoming = { NULL, 0, 0 };
    if (!f->pending_consumed) event_list_append(&incoming, f->before_frame.items, f->before_frame.count);
    event_list_append(&incoming, frame_events.items, frame_events.count);
    const qa_app_sim_presentation_event **incoming_pointers = event_pointers(&incoming);
    const qa_app_sim_presentation_event **round_pointers = event_pointers(&final_rounds);
    qa_application_present_frame present = { incoming_pointers, incoming.count, round_pointers, final_rounds.count,
      local_commands, local_count, qa_js_max(0, app->elapsed - f->displayed_milliseconds), f->frame_started_at, app->elapsed };
    qa_application_present_output(app, final_presentation, &present);
    free(incoming_pointers); free(round_pointers);
    event_list_free(&incoming);
  }
  event_list_free(&final_rounds);
  event_list_free(&frame_events);
  if (app->capture != NULL) qa_app_capture_drain(app->capture);
  if (app->tools != NULL) qa_omni_timer_stamp(qa_app_application_tools_timer(app->tools), "frame end");
  if (capture_transition_pending) {
    size_t held_count = 0;
    qa_application_command_request *held = qa_event_ring_take(&app->requested_commands, &app->requested_cursor, &held_count);
    qa_event_ring_retire(&app->requested_commands);
    held_frame hold = { app, held, held_count };
    qa_caught_error caught;
    bool ok = qa_try(held_transition, &hold, &caught);
    if (held_count != 0) (void)qa_event_ring_admit_all(&app->requested_commands, held, held_count);
    free(held);
    if (!ok) qa_rethrow(&caught);
    qa_application_after_command_dispatch(app);
  } else {
    qa_application_commands(app);
    qa_application_source_actions(app, app->source_events, app->source_event_count);
    qa_application_commands(app);
  }
  if (app->graphical != NULL && app->image_settings != NULL) {
    qa_application_graphical *graphics = app->graphical;
    qa_app_world_seat_presentation **presentations = calloc(graphics->presentation_count ? graphics->presentation_count : 1, sizeof *presentations);
    if (presentations == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < graphics->presentation_count; i++) presentations[i] = graphics->presentations[i].presentation;
    qa_cleanup presentations_cleanup;
    qa_cleanup_push(&presentations_cleanup, free, presentations);
    qa_app_image_settings_refresh(app->image_settings, graphics->assets, presentations, graphics->presentation_count,
        graphics->rerelease, graphics->renderer);
    qa_cleanup_pop(&presentations_cleanup);
    free(presentations);
  }
  {
    event_list combined = { NULL, 0, 0 };
    event_list_append(&combined, round_events.items, round_events.count);
    event_list_append(&combined, app->source_events, app->source_event_count);
    set_source_events(app, &combined);
  }
  event_list_free(&round_events);
  qa_application_rank_local_match(app);
  if (completed_lobby_simulation != NULL && app->lobby_completion != completed_lobby_simulation && app->host.lobby != NULL) {
    app->host.lobby->complete(app->host.lobby->ctx);
    app->lobby_completion = completed_lobby_simulation;
  }
  f->result = output;
  qa_cleanup_pop(&local_cleanup);
  free(local_commands);
  qa_cleanup_pop(&remote_cleanup);
  free(remote);
}

static void step_frame_release(step_frame *f) {
  event_list_free(&f->before_frame);
  event_list_free(&f->progressed_events);
  event_list_free(&f->progressed_round_events);
  free(f->commands); f->commands = NULL;
}

/* TS step(elapsedMilliseconds). */
qa_simulation_output qa_application_step(qa_application *app, double elapsed_milliseconds) {
  if (app->closed) qa_raise("Error", 0, 0, "Application is closed");
  if (app->stepping) qa_raise("Error", 0, 0, "Application step is already in progress");
  if (app->world_operation != QA_APPLICATION_IDLE) qa_raise("Error", 0, 0, "A world operation is in progress");
  if (!isfinite(elapsed_milliseconds) || elapsed_milliseconds <= 0) qa_range_error("Application step requires positive elapsed milliseconds");
  if (app->ownership.borrowed_client == NULL && app->owned_video_restart != NULL) (void)qa_app_video_restart_drain(app->owned_video_restart);
  app->stepping = true;
  app->step_completion = true;
  step_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.app = app;
  frame.elapsed_milliseconds = elapsed_milliseconds;
  frame.frame_started_at = qa_application_performance_now();
  qa_caught_error caught;
  bool ok = qa_try(step_body, &frame, &caught);
  step_frame_release(&frame);
  if (ok) { app->stepping = false; app->step_completion = false; return frame.result; }
  if (!app->fatal_world_failure) {
    app->stepping = false; app->step_completion = false;
    if (app->capture != NULL) qa_app_capture_before_world_change(app->capture);
    qa_rethrow(&caught);
  }
  app->stepping = false; app->step_completion = false;
  qa_application_close_failed_world(app, &caught);
}

/* TS run(): runFrameLoop over step. */
static bool run_alive(void *ctx) {
  qa_application *app = ctx;
  return !app->stopping && !app->closed && (!app->launch_options.has_frame_limit || app->frames < app->launch_options.frame_limit);
}
static double run_minimum(void *ctx) {
  qa_application *app = ctx;
  return qa_app_minimum_frame_milliseconds(qa_application_source_cvars(app, NULL), app->launch_options.dedicated);
}
static void run_step(void *ctx, double elapsed) { (void)qa_application_step(ctx, elapsed); }
/* TS clockState: `${this.videoRestart?.generation}:${this.worldOperations}`. */
static void run_clock_state(void *ctx, char *out, size_t capacity) {
  qa_application *app = ctx;
  qa_app_video_restart *restart = qa_application_video_restart(app);
  char generation[40], operations[40];
  if (restart == NULL) (void)snprintf(generation, sizeof generation, "undefined");
  else qa_net_js_number(qa_app_video_restart_generation(restart), generation, sizeof generation);
  qa_net_js_number(app->world_operations, operations, sizeof operations);
  (void)snprintf(out, capacity, "%s:%s", generation, operations);
}
static void run_recover(void *ctx, const qa_caught_error *error) { qa_application_recover_dedicated_frame(ctx, error); }
void qa_application_run(qa_application *app) {
  qa_app_frame_loop_host host = { app, run_alive, run_minimum, run_step, run_clock_state, run_recover };
  qa_app_run_frame_loop(host);
}

/* TS recoverDedicatedFrame(error): a dedicated server survives a frame fault by
 * restarting its map; repeated faults still end the process. */
void qa_application_recover_dedicated_frame(qa_application *app, const qa_caught_error *error) {
  if (!app->launch_options.dedicated || app->closed || app->stopping || app->fatal_world_failure) qa_rethrow(error);
  if (strcmp(error->name, "CommonError") == 0 && error->code == QA_COMMON_ERROR_FATAL) qa_rethrow(error);
  double now = qa_application_performance_now();
  double *ring = app->dedicated_frame_faults;
  size_t length = QA_APPLICATION_DEDICATED_FAULT_LIMIT + 1;
  ring[(size_t)fmod(app->dedicated_frame_fault_count++, (double)length)] = now;
  if (app->dedicated_frame_fault_count > QA_APPLICATION_DEDICATED_FAULT_LIMIT
      && now - ring[(size_t)fmod(app->dedicated_frame_fault_count, (double)length)] < QA_APPLICATION_DEDICATED_FAULT_WINDOW_MILLISECONDS)
    qa_rethrow(error);
  qa_application_printf(app, "Server frame failed; restarting %s: %s: %s\n", app->launch_options.map, error->name, error->message);
  qa_application_replace_world(app, app->launch_options.map, NULL);
}

/* TS readPixels(). The pixels are owned by the caller. */
const uint8_t *qa_application_read_pixels(qa_application *app, size_t *out_count) {
  if (app->graphical == NULL) qa_raise("Error", 0, 0, "Dedicated applications have no framebuffer");
  return qa_app_native_renderer_read_pixels(app->graphical->renderer, out_count);
}
