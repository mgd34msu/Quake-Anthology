/* Port of src/app/bootstrap/application.ts, group P6: local player changes,
 * command dispatch (application, guest and source commands) and presentOutput
 * (docs/application_layout.md). TS async members are synchronous; a TS throw
 * is qa_raise, a TS catch qa_try. GPL-2.0-or-later. */
#include "application_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/commands.h"
#include "qa/app/bootstrap/local_seat_change.h"
#include "qa/app/bootstrap/match_results.h"
#include "qa/app/bootstrap/player_userinfo.h"
#include "qa/app/bootstrap/q1_client_commands.h"
#include "qa/app/bootstrap/simulation/q3/server_state.h"
#include "qa/console/session.h"
#include "qa/core/q3_product_policy.h"
#include "qa/foundation/js.h"
#include "qa/input/user_command.h"
#include "qa/input/weapon_bindings.h"
#include "qa/network/common/js_number.h"
#include "qa/network/q3/admission.h"
#include "qa/world/session/mod_commands.h"

static void *checked(void *p) {
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *copy = checked(malloc(length + 1));
  memcpy(copy, text, length + 1);
  return copy;
}
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static bool one_of(const char *name, const char *const *names, size_t count) {
  for (size_t i = 0; i < count; i++) if (is(name, names[i])) return true;
  return false;
}
#define ONE_OF(name, ...) one_of((name), (const char *const[]){ __VA_ARGS__ }, \
    sizeof((const char *const[]){ __VA_ARGS__ }) / sizeof(const char *))

/* TS `let origin = source?.origin; while (origin?.kind === "script") origin = origin.caller`. */
static const qa_command_origin *unwrapped_origin(const qa_command_context *source) {
  const qa_command_origin *origin = source == NULL ? NULL : &source->origin;
  while (origin != NULL && origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  return origin;
}
/* TS { session: this.session.session, origin: { kind } }. */
static qa_command_context plain_context(qa_application *app, qa_command_origin_kind kind) {
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = qa_engine_session_id(app->session);
  context.origin.kind = kind;
  return context;
}

/* ---- commandActor (TS 7092-7096) ---- */
static qa_actor_id command_actor(qa_application *app, const qa_seat_id *seat) {
  size_t count = qa_application_local_player_count(app);
  for (size_t i = 0; i < count; i++) {
    qa_app_local_player player = qa_application_local_player(app, i);
    if (seat == NULL || qa_same_seat(qa_session_seat_id(player.seat), *seat)) return player.actor;
  }
  qa_raise("Error", 0, 0, "Command requires a player");
}
qa_actor_id qa_application_command_actor(qa_application *app, const qa_seat_id *seat) { return command_actor(app, seat); }

/* ---- pendingShellPublication (TS 7121-7123) ---- */
bool qa_application_pending_shell_publication(qa_application *app) {
  qa_app_client_bootstrap *client = app->ownership.borrowed_client;
  return client != NULL && client->has_pending_source(client->bootstrap_ctx);
}

/* ---- nativeMatchTransition (TS 7131-7145) ---- */
static bool native_match_transition(qa_application *app, const qa_application_command_request *request) {
  const qa_command_producer *producer = request->has_source ? request->source.producer : NULL;
  if ((!is(request->name, "map") && !is(request->name, "gamemap")) || request->target != QA_APPLICATION_TARGET_APPLICATION
      || request->argument_count != 1 || producer == NULL || !is(producer->kind, "game-module") || producer->instance != NULL
      || !request->has_source || request->source.session.token != qa_engine_session_id(app->session).token
      || !qa_app_shared_sim_q2_native(app->world_simulation, NULL, NULL))
    return false;
  qa_app_native_q2_map_transition transition = { "game-module-map-transition", producer->module };
  return qa_application_finish_q2_match(app, &transition);
}

/* ---- rankLocalMatch (TS 7147-7171) ---- */
static qa_app_player_progress_store *player_progress(qa_application *app) {
  if (app->player_progress == NULL) {
    size_t length = strlen(app->input_config.root) + sizeof "/player-progress.json";
    char *path = checked(malloc(length));
    (void)snprintf(path, length, "%s/player-progress.json", app->input_config.root);
    char *normalized = qa_application_path_normalize(path);
    free(path);
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, normalized);
    app->player_progress = checked(qa_app_player_progress_store_open(normalized));
    qa_cleanup_pop(&cleanup);
    free(normalized);
  }
  return app->player_progress;
}
typedef struct rank_call {
  qa_application *app;
  qa_app_match_rankings *rankings;
  const qa_online_account *account;
  const qa_app_native_q2_map_transition *transition;
  qa_ranking_entry *entries; size_t entry_count;
  double *locals; size_t local_count;
} rank_call;
static void rank_call_free(void *arg) {
  rank_call *call = arg;
  qa_ranking_entries_destroy(call->entries, call->entry_count);
  free(call->locals);
  call->entries = NULL; call->entry_count = 0; call->locals = NULL; call->local_count = 0;
}
static void rank_run(void *ctx) {
  rank_call *call = ctx;
  qa_application *app = call->app;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, rank_call_free, call);
  qa_app_match_rankings_attach(call->rankings, player_progress(app));
  const qa_app_match_result *result = qa_app_match_rankings_observe(call->rankings, app->world_simulation, call->transition);
  if (result != NULL) {
    call->locals = checked(calloc(app->local_seat_count + 1, sizeof *call->locals));
    for (size_t i = 0; i < app->local_seat_count; i++) {
      double slot = (double)qa_session_client_id(qa_session_seat_client(app->local_seats[i])).slot;
      bool seen = false;
      for (size_t j = 0; j < call->local_count && !seen; j++) seen = call->locals[j] == slot;
      if (!seen) call->locals[call->local_count++] = slot;
      (void)qa_app_match_rankings_bind(call->rankings, slot, call->account->name);
    }
    for (size_t p = 0; p < result->player_count; p++) {
      const qa_app_match_result_player *player = &result->players[p];
      bool local = false;
      for (size_t j = 0; j < call->local_count && !local; j++) local = call->locals[j] == player->slot;
      if (local) continue;
      if (player->identity == NULL) qa_app_match_rankings_unbind(call->rankings, player->slot);
      else (void)qa_app_match_rankings_bind(call->rankings, player->slot, player->identity);
    }
    call->entries = qa_app_match_rankings_settle(call->rankings, &call->entry_count);
    for (size_t e = 0; e < call->entry_count; e++) {
      char wins[32], matches[32], score[32];
      (void)snprintf(wins, sizeof wins, "%s", QA_JS_NUMBER(call->entries[e].wins));
      (void)snprintf(matches, sizeof matches, "%s", QA_JS_NUMBER(call->entries[e].matches));
      (void)snprintf(score, sizeof score, "%s", QA_JS_NUMBER(call->entries[e].score));
      qa_application_printf(app, "Local standings: %s %s wins in %s matches, score %s\n", call->account->name, wins, matches, score);
    }
  }
  qa_cleanup_pop(&cleanup);
  rank_call_free(call);
}
static void rank_local_match(qa_application *app, const qa_app_native_q2_map_transition *transition) {
  qa_app_match_rankings *rankings = app->host.local_rankings;
  const qa_online_account *account = app->host.lobby == NULL ? NULL : app->host.lobby->account;
  if (rankings == NULL || account == NULL) return;
  rank_call call;
  memset(&call, 0, sizeof call);
  call.app = app; call.rankings = rankings; call.account = account; call.transition = transition;
  qa_caught_error caught;
  /* TS String(error): "<name>: <message>". */
  if (!qa_try(rank_run, &call, &caught)) qa_application_printf(app, "Local ranking failed: %s: %s\n", caught.name, caught.message);
}
void qa_application_rank_local_match(qa_application *app) { rank_local_match(app, NULL); }

/* ---- finishQ2Match (TS 7173-7190) ---- */
typedef struct finish_call { qa_application *app; qa_app_q2_match_local *locals; qa_app_q2_match_report_rows reports; } finish_call;
static void finish_call_free(void *arg) {
  finish_call *call = arg;
  free(call->locals); call->locals = NULL;
  qa_app_q2_match_report_rows_destroy(&call->reports);
}
bool qa_application_finish_q2_match(qa_application *app, const qa_app_native_q2_map_transition *transition) {
  qa_app_q2_match_simulation match = qa_app_shared_sim_q2_match_simulation(app->world_simulation);
  if (!qa_app_q2_match_completed(&match, transition)) return false;
  rank_local_match(app, transition);
  finish_call call;
  memset(&call, 0, sizeof call);
  call.app = app;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, finish_call_free, &call);
  size_t local_count = qa_application_local_player_count(app);
  call.locals = checked(calloc(local_count + 1, sizeof *call.locals));
  for (size_t i = 0; i < local_count; i++) {
    qa_app_local_player player = qa_application_local_player(app, i);
    call.locals[i].actor = player.actor;
    call.locals[i].seat = qa_session_seat_id(player.seat);
  }
  match = qa_app_shared_sim_q2_match_simulation(app->world_simulation);
  call.reports = qa_app_q2_match_reports(&match, call.locals, local_count, transition);
  if (call.reports.count != 0) {
    qa_app_player_progress_store *progress = player_progress(app);
    for (size_t i = 0; i < call.reports.count; i++) (void)qa_app_player_progress_store_record(progress, call.reports.items[i]);
  }
  qa_cleanup_pop(&cleanup);
  finish_call_free(&call);
  if (app->host.lobby == NULL) return false;
  if (app->lobby_completion != app->world_simulation) {
    app->host.lobby->complete(app->host.lobby->ctx);
    app->lobby_completion = app->world_simulation;
  }
  qa_application_return_to_lobby(app);
  return true;
}

/* ---- commands (TS 7192-7280) ---- */
static bool application_command(qa_application *app, qa_application_command_request *command, const qa_command_context *source,
    void (*print)(void *ctx, const char *text), void *print_ctx);
static bool guest_command(qa_application *app, qa_application_command_request *command, const qa_command_context *source,
    void (*print)(void *ctx, const char *text), void *print_ctx);
static void source_command(qa_application *app, qa_application_command_request *request, const qa_command_context *source,
    void (*print)(void *ctx, const char *text), void *print_ctx);

/* TS print closure of one dispatched request. */
typedef struct dispatch_print { qa_application *app; const qa_command_context *source; bool has_seat; } dispatch_print;
static void dispatch_print_text(void *ctx, const char *text) {
  dispatch_print *print = ctx;
  qa_application *app = print->app;
  if (app->operator_output->write != NULL) {
    app->operator_output->write(app->operator_output->ctx, text);
    return;
  }
  if (print->source != NULL && app->graphical != NULL) qa_app_application_input_print(app->graphical->input, text, print->source);
  else {
    qa_application_print(app, text);
    if (!print->has_seat && app->graphical != NULL) {
      size_t count = qa_app_application_input_local_count(app->graphical->input);
      for (size_t i = 0; i < count; i++) qa_seat_console_print(qa_app_application_input_local(app->graphical->input, i)->console, text);
    }
  }
}

/* The requests one commands() call owns: next..count are still owned here (a
 * prepend hands the rest back to the queue). */
typedef struct commands_state {
  qa_application *app;
  qa_application_command_request *pending; size_t count, next;
  void (*after)(void *ctx); void *after_ctx;
  bool route_applications;
} commands_state;
static void commands_state_free(void *arg) {
  commands_state *state = arg;
  for (size_t i = state->next; i < state->count; i++) qa_application_command_request_free(&state->pending[i]);
  free(state->pending);
  state->pending = NULL; state->count = state->next = 0;
}
/* TS requestedCommands.prepend(pending.slice(from)). */
static void prepend_from(commands_state *state, size_t from) {
  qa_application *app = state->app;
  for (size_t i = state->next; i < from && i < state->count; i++) qa_application_command_request_free(&state->pending[i]);
  if (from < state->count) qa_event_ring_prepend(&app->requested_commands, state->pending + from, state->count - from);
  state->next = state->count;
}
/* TS request.seat !== null && !this.localPlayers.some(player => request.seat?.equals(player.seat.id)). */
static bool seat_is_local_player(qa_application *app, qa_seat_id seat) {
  size_t count = qa_application_local_player_count(app);
  for (size_t i = 0; i < count; i++)
    if (qa_same_seat(qa_session_seat_id(qa_application_local_player(app, i).seat), seat)) return true;
  return false;
}
/* TS graphical.input.locals.find(local => local.player.seat.id.equals(seat)). */
static qa_app_local_input *graphical_local(qa_application *app, qa_seat_id seat) {
  if (app->graphical == NULL) return NULL;
  size_t count = qa_app_application_input_local_count(app->graphical->input);
  for (size_t i = 0; i < count; i++) {
    qa_app_local_input *local = qa_app_application_input_local(app->graphical->input, i);
    if (qa_same_seat(qa_session_seat_id(local->player.seat), seat)) return local;
  }
  return NULL;
}
typedef struct dispatch_call {
  qa_application *app;
  qa_application_command_request *command;
  const qa_command_context *source;
  dispatch_print print;
} dispatch_call;
static void dispatch_run(void *ctx) {
  dispatch_call *call = ctx;
  if (application_command(call->app, call->command, call->source, dispatch_print_text, &call->print)) return;
  if (guest_command(call->app, call->command, call->source, dispatch_print_text, &call->print)) return;
  source_command(call->app, call->command, call->source, dispatch_print_text, &call->print);
}
static void run_after(commands_state *state) {
  if (state->after != NULL) state->after(state->after_ctx);
}
/* One request of the loop; false returns from commands(). */
static bool dispatch_one(commands_state *state, size_t index) {
  qa_application *app = state->app;
  qa_application_command_request *request = &state->pending[index];
  qa_application_retire_removed_local_players(app);
  const qa_command_origin *origin = unwrapped_origin(request->has_source ? &request->source : NULL);
  if (origin != NULL && (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT || origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT)) {
    qa_session_client *client = qa_engine_session_client_at(app->session, origin->client.slot);
    if (client == NULL || !qa_same_client(qa_session_client_id(client), origin->client)) return true;
  }
  if (app->graphical != NULL && request->has_seat && !seat_is_local_player(app, request->seat)) return true;
  const qa_command_producer *producer = request->has_source ? request->source.producer : NULL;
  if (producer != NULL && is(producer->kind, "game-module") && producer->instance != NULL) {
    qa_mod_commands *mods = (qa_mod_commands *)app->world_simulation->options.mod_commands;
    if (mods == NULL || !qa_mod_commands_active(mods, request->source)) return true;
  }
  if (app->stepping && (qa_application_pending_shell_publication(app)
      || (qa_application_video_restart(app) != NULL && qa_app_video_restart_pending(qa_application_video_restart(app))))) {
    prepend_from(state, index);
    return false;
  }
  if (producer != NULL && is(producer->kind, "game-module") && (is(request->name, "map") || is(request->name, "gamemap"))) {
    if (qa_application_capture_blocks_transition(app)) {
      prepend_from(state, index);
      return false;
    }
    if (native_match_transition(app, request)) return true;
  }
  if (request->target == QA_APPLICATION_TARGET_COMPONENT_CLIENT) {
    if (app->graphical != NULL) {
      qa_app_component_client_command_request component = { request->consumer, request->mode, request->name,
        (const char *const *)request->arguments, request->argument_count, request->seat, request->source };
      (void)qa_app_application_mod_presentations_dispatch_command(app->graphical->mod_presentations, &component);
    }
    return true;
  }
  qa_app_client_bootstrap *borrowed = app->ownership.borrowed_client;
  if (state->route_applications && request->target == QA_APPLICATION_TARGET_APPLICATION && app->source_published && borrowed != NULL) {
    qa_command_context source = request->has_source ? request->source : qa_command_buffer_context(qa_app_prepared_startup_commands(borrowed->prepared));
    if (app->stepping) {
      if (borrowed->route_command(borrowed->bootstrap_ctx, request->name, (const char *const *)request->arguments, request->argument_count, &source)) {
        qa_application_command_request_free(request);
        state->next = index + 1;
        prepend_from(state, index + 1);
        return false;
      }
    } else {
      qa_app_configuration_command_request configuration = { "application", request->name, (const char *const *)request->arguments,
        request->argument_count, request->has_seat, request->seat, source };
      borrowed->dispatch_application_request(borrowed->bootstrap_ctx, &configuration);
      return true;
    }
  }
  if (app->closed) return true;
  if (qa_application_capture_blocks_transition(app)) {
    prepend_from(state, index);
    return false;
  }
  qa_app_local_input *local = request->has_seat ? graphical_local(app, request->seat) : NULL;
  qa_command_context seat_source;
  const qa_command_context *source = NULL;
  if (request->has_source) source = &request->source;
  else if (local != NULL) {
    seat_source = plain_context(app, QA_COMMAND_ORIGIN_LOCAL_SEAT);
    seat_source.origin.seat = qa_session_seat_id(local->player.seat);
    seat_source.origin.client = qa_session_client_id(qa_session_seat_client(local->player.seat));
    source = &seat_source;
  }
  dispatch_call call = { app, request, source, { app, source, request->has_seat } };
  qa_caught_error caught;
  if (!qa_try(dispatch_run, &call, &caught)) {
    if (app->fatal_world_failure || is(caught.name, QA_APP_Q3_GAME_CALLBACK_ERROR) || is(caught.name, "Q2GameCallbackError")
        || is(caught.name, "ClientSourcePublicationError")) {
      run_after(state);
      qa_rethrow(&caught);
    }
    size_t length = strlen(caught.message) + 2;
    char *line = checked(malloc(length));
    (void)snprintf(line, length, "%s\n", caught.message);
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, line);
    dispatch_print_text(&call.print, line);
    qa_cleanup_pop(&cleanup);
    free(line);
  }
  run_after(state);
  return true;
}
static void commands_run(qa_application *app, void (*after)(void *ctx), void *after_ctx,
    qa_application_command_request *direct, size_t direct_count, bool has_direct, bool route_applications) {
  commands_state state;
  memset(&state, 0, sizeof state);
  state.app = app; state.after = after; state.after_ctx = after_ctx; state.route_applications = route_applications;
  if (has_direct) { state.pending = direct; state.count = direct_count; }
  else {
    state.pending = qa_event_ring_take(&app->requested_commands, &app->requested_cursor, &state.count);
    qa_event_ring_retire(&app->requested_commands);
  }
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, commands_state_free, &state);
  for (; state.next < state.count;) {
    size_t index = state.next;
    bool more = dispatch_one(&state, index);
    if (!more) break;
    /* The request is consumed: free it unless a prepend already handed it back. */
    if (state.next == index) {
      qa_application_command_request_free(&state.pending[index]);
      state.next = index + 1;
    }
  }
  qa_cleanup_pop(&cleanup);
  commands_state_free(&state);
}
void qa_application_commands(qa_application *app) { commands_run(app, NULL, NULL, NULL, 0, false, true); }
void qa_application_commands_routed(qa_application *app, bool route_applications) {
  commands_run(app, NULL, NULL, NULL, 0, false, route_applications);
}

/* ---- afterCommandDispatch (TS 7098-7111) ---- */
/* TS async () => { if (this.closed) return; await this.applyTransition(); if (pending) await this.afterCommandDispatch(); } */
static void after_request(void *ctx) {
  qa_application *app = ctx;
  if (app->closed) return;
  qa_application_apply_transition(app);
  if (qa_event_ring_pending(&app->requested_commands, &app->requested_cursor) != 0) qa_application_after_command_dispatch(app);
}
void qa_application_after_command_dispatch(qa_application *app) {
  qa_application_retire_removed_local_players(app);
  if (app->tools != NULL) {
    const char *error = qa_app_application_tools_drain(app->tools);
    if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
  }
  if (app->closed || app->fatal_world_failure || qa_application_client_commands_blocked(app) || qa_application_pending_shell_publication(app))
    return;
  do {
    commands_run(app, after_request, app, NULL, 0, false, true);
    if (app->closed || qa_application_pending_shell_publication(app)
        || (qa_application_video_restart(app) != NULL && qa_app_video_restart_pending(qa_application_video_restart(app))))
      return;
    qa_application_apply_transition(app);
  } while (qa_event_ring_pending(&app->requested_commands, &app->requested_cursor) != 0 && !qa_application_client_commands_blocked(app));
}

/* ---- executeApplicationRequest (TS 7113-7119) ---- */
static qa_application_command_request owned_request(const qa_app_configuration_command_request *request) {
  qa_application_command_request owned;
  memset(&owned, 0, sizeof owned);
  owned.target = QA_APPLICATION_TARGET_APPLICATION;
  owned.name = dup_text(request->name);
  owned.argument_count = request->argument_count;
  owned.arguments = checked(calloc(request->argument_count + 1, sizeof *owned.arguments));
  for (size_t i = 0; i < request->argument_count; i++) owned.arguments[i] = dup_text(request->arguments[i]);
  owned.has_seat = request->has_seat; owned.seat = request->seat;
  owned.has_source = true; owned.source = qa_command_context_copy(&request->source);
  return owned;
}
void qa_application_execute_application_request(qa_application *app, const qa_app_configuration_command_request *request) {
  qa_application_command_request *direct = checked(malloc(sizeof *direct));
  *direct = owned_request(request);
  commands_run(app, after_request, app, direct, 1, true, false);
}

/* ---- takePendingClientCommands (TS 7125-7129) ---- */
struct qa_application_pending_client_commands {
  qa_application *app;
  qa_application_command_request *requests; size_t count;
};
qa_application_pending_client_commands *qa_application_take_pending_client_commands(qa_application *app) {
  qa_application_pending_client_commands *pending = checked(calloc(1, sizeof *pending));
  pending->app = app;
  pending->requests = qa_event_ring_take(&app->requested_commands, &app->requested_cursor, &pending->count);
  qa_event_ring_retire(&app->requested_commands);
  return pending;
}
void qa_application_pending_client_commands_run(qa_application_pending_client_commands *pending) {
  qa_application *app = pending->app;
  qa_application_command_request *requests = pending->requests;
  size_t count = pending->count;
  free(pending);
  commands_run(app, NULL, NULL, requests, count, true, true);
}
void qa_application_pending_client_commands_discard(qa_application_pending_client_commands *pending) {
  if (pending == NULL) return;
  for (size_t i = 0; i < pending->count; i++) qa_application_command_request_free(&pending->requests[i]);
  free(pending->requests);
  free(pending);
}

/* ---- shared helpers of the three dispatch stages ---- */
/* TS this.content.q3Product?.policy ?? { kind: "retail" }. */
static qa_q3_product_policy content_q3_policy(qa_application *app) {
  qa_q3_product_policy policy;
  if (!qa_app_loaded_content_q3_policy(app->loaded_content, &policy)) policy = qa_q3_product_policy_make(false, false);
  return policy;
}
/* TS Number(cvars.find(name)?.latchedValue ?? cvars.variableString(name)). */
static double latched_number(qa_cvar_registry *cvars, const char *name) {
  qa_cvar_read read;
  memset(&read, 0, sizeof read);
  double value;
  if (qa_cvar_registry_find(cvars, name, &read) && read.latched_value != NULL) value = qa_js_string_to_number(read.latched_value);
  else {
    char *text = qa_cvar_registry_variable_string(cvars, name);
    value = qa_js_string_to_number(text != NULL ? text : "");
    free(text);
  }
  qa_cvar_read_free(&read);
  return value;
}
static void clear_pending_native_travel(qa_application *app) {
  if (!app->has_pending_native_travel) return;
  free(app->pending_native_travel.name); free(app->pending_native_travel.spawn_point);
  qa_app_q2_travel_target_destroy(app->pending_native_travel.next);
  memset(&app->pending_native_travel, 0, sizeof app->pending_native_travel);
  app->has_pending_native_travel = false;
}
static void set_pending_map(qa_application *app, char *map) {
  free(app->pending_map);
  app->pending_map = map;
}
static void clear_pending_restart(qa_application *app) { app->has_pending_restart = false; app->pending_restart = 0; }
/* TS this.graphical?.q3.get(seat). */
static qa_application_q3_seat_client *q3_seat_client(qa_application *app, qa_seat_id seat) {
  if (app->graphical == NULL) return NULL;
  for (size_t i = 0; i < app->graphical->q3_count; i++)
    if (qa_same_seat(app->graphical->q3_seats[i], seat)) return &app->graphical->q3[i];
  return NULL;
}
/* TS resolveWeaponSelection(name, args, simulation.playerUi(actor).items). */
static bool resolve_selection(qa_application *app, qa_actor_id actor, const char *name, const char *const *args, size_t count,
    qa_resolved_weapon_selection *out) {
  qa_app_sim_player_ui ui = qa_app_shared_sim_player_ui(app->world_simulation, actor);
  qa_weapon_binding_item *items = checked(calloc(ui.item_count + 1, sizeof *items));
  for (size_t i = 0; i < ui.item_count; i++) {
    items[i].id = ui.items[i].id;
    items[i].label = ui.items[i].label;
    items[i].kind = is(ui.items[i].kind, "powerup") ? QA_WEAPON_BINDING_POWERUP : QA_WEAPON_BINDING_WEAPON;
  }
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, items);
  bool resolved = qa_resolve_weapon_selection(name, args, count, items, ui.item_count, out);
  qa_cleanup_pop(&cleanup);
  free(items);
  return resolved;
}
/* TS Q3_WEAPON_ITEMS.find(weapon => weapon.item === item). */
static const qa_q3_weapon_item *q3_weapon_item_of(const char *item) {
  if (item == NULL) return NULL;
  for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++) if (is(qa_q3_weapon_items[i].item, item)) return &qa_q3_weapon_items[i];
  return NULL;
}
/* TS sourceClient.client.command(["weapon", String(weapon.weapon)]). */
static bool seat_client_weapon(qa_application_q3_seat_client *client, double weapon) {
  char number[32];
  (void)snprintf(number, sizeof number, "%s", QA_JS_NUMBER(weapon));
  const char *argv[2] = { "weapon", number };
  return qa_app_application_q3_client_command(client->client, argv, 2);
}
/* [name, ...args] argv (malloc'd array of borrowed strings). */
static const char **command_argv(const qa_application_command_request *command) {
  const char **argv = checked(calloc(command->argument_count + 2, sizeof *argv));
  argv[0] = command->name;
  for (size_t i = 0; i < command->argument_count; i++) argv[i + 1] = command->arguments[i];
  return argv;
}
static const char *const *request_args(const qa_application_command_request *command) {
  return (const char *const *)command->arguments;
}
static const char *request_arg(const qa_application_command_request *command, size_t index) {
  return index < command->argument_count ? command->arguments[index] : NULL;
}
/* An awaited TS promise rejected with this text. */
static void raise_text(const char *error) {
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}

/* ---- applicationCommand (TS 7283-7447) ---- */
static bool always_current(void *ctx) { (void)ctx; return true; } /* TS () => true */
static qa_application_base_arena_progress *base_arena_progress_of(qa_application *app, qa_app_shared_simulation *simulation) {
  for (size_t i = 0; i < app->base_arena_progress_count; i++)
    if (app->base_arena_progress[i].simulation == simulation) return &app->base_arena_progress[i];
  return NULL;
}
/* The recording commands' context: source ?? local-console. */
static qa_command_context source_or_console(qa_application *app, const qa_command_context *source) {
  return source != NULL ? *source : plain_context(app, QA_COMMAND_ORIGIN_LOCAL_CONSOLE);
}
static void recording_command(qa_application *app, const qa_application_command_request *command, const qa_command_context *source) {
  qa_command_context context = source_or_console(app, source);
  const char *name = command->name;
  if (is(name, "stop") || is(name, "stoprecord")) raise_text(qa_app_client_demo_recording_stop(qa_application_standalone_recording(app)));
  else if (is(name, "serverstop")) {
    if (command->argument_count != 0) qa_raise("Error", 0, 0, "Usage: serverstop");
    raise_text(qa_app_client_demo_recording_stop_server(qa_application_standalone_recording(app)));
  } else if (is(name, "mvdstop")) {
    if (command->argument_count != 0) qa_raise("Error", 0, 0, "Usage: mvdstop");
    raise_text(qa_app_client_demo_recording_stop_mvd(qa_application_standalone_recording(app)));
  } else {
    const char *target = request_arg(command, 0);
    if (command->argument_count > 1 || ((is(name, "rerecord") || is(name, "mvdrecord") || is(name, "serverrecord")) && target == NULL))
      qa_raise("Error", 0, 0, "Usage: %s <name>", name);
    if (is(name, "rerecord") && target != NULL)
      raise_text(qa_app_client_demo_recording_rerecord(qa_application_standalone_recording(app), target, context));
    else if (is(name, "serverrecord") && target != NULL)
      raise_text(qa_app_client_demo_recording_start_server(qa_application_standalone_recording(app), target, context));
    else if (is(name, "mvdrecord") && target != NULL)
      raise_text(qa_app_client_demo_recording_start_mvd(qa_application_standalone_recording(app), target, context));
    else raise_text(qa_app_client_demo_recording_start(qa_application_standalone_recording(app), target, context));
  }
}
/* TS ["end.roq", "demoend.roq"].includes(name.toLowerCase().split("/").at(-1) ?? ""). */
static bool final_cinematic(const char *name) {
  const char *slash = strrchr(name, '/');
  const char *last = slash == NULL ? name : slash + 1;
  size_t length = strlen(last);
  char *lower = checked(malloc(length + 1));
  for (size_t i = 0; i <= length; i++) lower[i] = (char)(last[i] >= 'A' && last[i] <= 'Z' ? last[i] - 'A' + 'a' : last[i]);
  bool final = is(lower, "end.roq") || is(lower, "demoend.roq");
  free(lower);
  return final;
}
static void cinematic_command(qa_application *app, const qa_application_command_request *command) {
  const char *name = request_arg(command, 0);
  qa_application_graphical *graphical = app->graphical;
  bool has_seat = command->has_seat;
  qa_seat_id seat = command->seat;
  if (!has_seat && qa_application_local_player_count(app) != 0) {
    seat = qa_session_seat_id(qa_application_local_player(app, 0).seat);
    has_seat = true;
  }
  if (name == NULL || graphical == NULL || !has_seat) qa_raise("Error", 0, 0, "cinematic requires a movie and a local presentation");
  const char *mode = request_arg(command, 1);
  const qa_app_system_cinematic_host *host = qa_application_system_cinematics(app, app->world_simulation, app->loaded_content,
      graphical->assets, graphical->audio, graphical->renderer, seat, graphical->input, NULL);
  qa_app_q3_screen_cinematic_request request;
  request.name = name;
  request.loop = (mode != NULL && mode[0] == '2') || is(mode, "loop");
  request.hold = (mode != NULL && mode[0] == '1') || is(mode, "hold") || final_cinematic(name);
  request.silent = false;
  (void)host->open(host->ctx, request, always_current, NULL);
}
static void centerview_command(qa_application *app, const qa_application_command_request *command) {
  if (app->graphical == NULL) return;
  size_t count = qa_app_application_input_local_count(app->graphical->input);
  for (size_t i = 0; i < count; i++) {
    qa_app_local_input *target = qa_app_application_input_local(app->graphical->input, i);
    qa_seat_id seat = qa_session_seat_id(target->player.seat);
    if (command->has_seat && !qa_same_seat(seat, command->seat)) continue;
    qa_application_q3_seat_client *client = q3_seat_client(app, seat);
    bool has_state = false;
    double delta_x = 0;
    if (qa_input_command_builder_dialect(target->builder) == QA_COMMAND_DIALECT_Q3 && client != NULL) {
      const qa_app_q3_client_source *source = qa_app_application_q3_client_source(client->client);
      qa_q3_wire_snapshot snapshot;
      memset(&snapshot, 0, sizeof snapshot);
      if (source->snapshots.read(source->snapshots.ctx, source->snapshots.current(source->snapshots.ctx).number, &snapshot)) {
        has_state = true;
        delta_x = snapshot.player_state.delta_angles.x;
        qa_q3_wire_snapshot_destroy(&snapshot);
      }
    }
    qa_vec3 angles = qa_input_command_builder_view_angles(target->builder);
    angles.x = !has_state ? 0 : -qa_js_shr(qa_js_shl(delta_x, 16), 16) * (360.0 / 65536.0);
    qa_input_command_builder_set_view_angles(target->builder, angles);
  }
}
static bool operator_command(qa_application *app, const qa_application_command_request *command,
    void (*print)(void *ctx, const char *text), void *print_ctx) {
  qa_app_server_operator_host operator = qa_application_operator_host(app);
  if (ONE_OF(command->name, "addlrconcmd", "dellrconcmd", "listlrconcmds")) {
    const char *argument = request_arg(command, 0);
    if (qa_app_server_operator_limited_rcon_command(app->operator_state, command->name, argument != NULL ? argument : "", print, print_ctx))
      return true;
  }
  if (is(command->name, "setmaster")) {
    qa_app_server_operator_set_masters(app->operator_state, operator, request_args(command), command->argument_count);
    return true;
  }
  if (is(command->name, "heartbeat")) {
    operator.heartbeat(operator.ctx);
    return true;
  }
  return qa_app_server_operator_filter_command(app->operator_state, operator, command->name, request_args(command), command->argument_count);
}
static void source_application_action(qa_application *app, const qa_application_command_request *command,
    void (*print)(void *ctx, const char *text), void *print_ctx) {
  const char *name = command->name;
  if ((is(name, "postgame") || is(name, "spPostgame")) && base_arena_progress_of(app, app->world_simulation) != NULL)
    qa_application_complete_base_arena(app, request_args(command), command->argument_count);
  else if (ONE_OF(name, "arena-reset", "arena-unlock", "arena-medals")) {
    qa_application_base_arena_progress *state = base_arena_progress_of(app, app->world_simulation);
    if (state == NULL) qa_raise("Error", 0, 0, "No base arena progression is active");
    if (is(name, "arena-reset")) {
      qa_app_base_arena_progression_reset(state->progression);
      if (state->has_result) free(state->result.result.awards);
      state->has_result = false;
      memset(&state->result, 0, sizeof state->result);
    } else if (is(name, "arena-unlock")) qa_app_base_arena_progression_unlock_levels(state->progression);
    else qa_app_base_arena_progression_unlock_medals(state->progression);
    qa_application_save_source_archive(app, &app->launch_options, app->loaded_content, app->world_simulation, app->team_arena_overrides);
  } else if (is(name, "postgame")) {
    app->pending_team_arena_postgame = qa_app_parse_team_arena_postgame(request_args(command), command->argument_count);
    app->has_pending_team_arena_postgame = true;
    print(print_ctx, "Match complete. Choose Next match, Retry match, or Main menu.\n");
  } else if (is(name, "teamarena-results")) {
    if (!qa_application_is_team_arena_skirmish(app, NULL)) qa_raise("Error", 0, 0, "No Team Arena skirmish is active");
  } else if (is(name, "kick")) {
    qa_app_q3_source_runtime *game = qa_app_shared_sim_q3_source(app->world_simulation);
    if (game == NULL) qa_raise("Error", 0, 0, "Source kick requires Quake III");
    qa_actor_id *targets = NULL;
    size_t count = qa_application_kick_targets(app, request_args(command), command->argument_count, &targets);
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, targets);
    for (size_t i = 0; i < count; i++) {
      qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(app->world_simulation, targets[i]);
      if (player == NULL) continue;
      qa_app_q3_source_host *host = qa_app_q3_source_runtime_host(game);
      host->engine.drop_client(host->engine.ctx, (double)player->client.slot, "was kicked");
    }
    qa_cleanup_pop(&cleanup);
    free(targets);
  } else qa_raise("Error", 0, 0, "Unknown source application action %s", name);
}
typedef struct audio_registrations { qa_q3_pres_sound_registration *items; size_t count; } audio_registrations;
static void audio_registrations_free(void *arg) { free(((audio_registrations *)arg)->items); }
static void audio_command(qa_application *app, const qa_application_command_request *command,
    void (*print)(void *ctx, const char *text), void *print_ctx) {
  qa_application_graphical *graphical = app->graphical;
  if (graphical == NULL) qa_raise("Error", 0, 0, "Sound system is not started");
  /* [...graphical.q3.values()].flatMap(value => value.client.media.bank.registrations()). */
  audio_registrations registrations = { NULL, 0 };
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, audio_registrations_free, &registrations);
  for (size_t i = 0; i < graphical->q3_count; i++) {
    size_t count = 0;
    const qa_q3_pres_sound_registration *rows = qa_q3_pres_presentation_sound_bank_registrations(
        qa_app_application_q3_assets_bank(qa_app_application_q3_client_media(graphical->q3[i].client)), &count);
    if (count == 0) continue;
    registrations.items = checked(realloc(registrations.items, (registrations.count + count) * sizeof *registrations.items));
    memcpy(registrations.items + registrations.count, rows, count * sizeof *rows);
    registrations.count += count;
  }
  qa_app_audio_command_request request = { command->name, request_args(command), command->argument_count, command->has_seat,
    command->seat, registrations.items, registrations.count, print, print_ctx };
  (void)qa_app_application_audio_command(graphical->audio, &request);
  qa_cleanup_pop(&cleanup);
  audio_registrations_free(&registrations);
}
static bool application_command(qa_application *app, qa_application_command_request *command, const qa_command_context *source,
    void (*print)(void *ctx, const char *text), void *print_ctx) {
  bool application = command->target == QA_APPLICATION_TARGET_APPLICATION;
  const char *name = command->name;
  if (application && app->has_owned_demos
      && qa_app_client_demo_commands_handle(app->owned_demos_service, name, request_args(command), command->argument_count,
          source_or_console(app, source)))
    return true;
  if (application && is(name, "clientLevelShot")) {
    qa_app_local_input *local = command->has_seat ? graphical_local(app, command->seat) : NULL;
    if (local != NULL && qa_app_shared_sim_q3_guest(app->world_simulation) != NULL) {
      qa_seat_console_close(local->console);
      qa_command_context context = plain_context(app, QA_COMMAND_ORIGIN_LOCAL_SEAT);
      context.origin.seat = qa_session_seat_id(local->player.seat);
      context.origin.client = qa_session_client_id(qa_session_seat_client(local->player.seat));
      qa_app_application_input_enqueue_client_command(app->graphical->input, "wait; wait; wait; wait; screenshot levelshot\n", &context);
    }
    return true;
  }
  if (is(name, "local_join") || is(name, "local_drop")) {
    qa_application_change_local_players_seat(app, is(name, "local_join") ? "join" : "drop", request_args(command), command->argument_count,
        command->has_seat ? &command->seat : NULL);
    return true;
  }
  if (application && ONE_OF(name, "record", "stop", "stoprecord", "rerecord", "mvdrecord", "mvdstop", "serverrecord", "serverstop")) {
    recording_command(app, command, source);
    return true;
  }
  if (is(name, "cinematicpause") || is(name, "stopcinematic")) {
    qa_application_movie *movie = qa_application_campaign_movie(app);
    if (movie == NULL) qa_raise("Error", 0, 0, "No cinematic is playing");
    if (is(name, "stopcinematic")) {
      qa_app_campaign_cinematic_close(movie->playback, app->presentation_frames);
      qa_application_set_campaign_movie(app, NULL);
    } else qa_app_campaign_cinematic_pause(movie->playback, !is(qa_app_campaign_cinematic_status(movie->playback), "paused"));
    return true;
  }
  if (is(name, "cinematic")) {
    cinematic_command(app, command);
    return true;
  }
  if (is(name, "centerview")) {
    centerview_command(app, command);
    return true;
  }
  if (app->operator_state != NULL && operator_command(app, command, print, print_ctx)) return true;
  if (command->target == QA_APPLICATION_TARGET_SOURCE) {
    source_application_action(app, command, print, print_ctx);
    return true;
  }
  if (application && (is(name, "in_restart") || is(name, "midiinfo"))) {
    qa_app_application_input *input = app->graphical == NULL ? NULL : app->graphical->input;
    if (input == NULL) {
      print(print_ctx, "Input devices require a graphical client.\n");
      return true;
    }
    if (is(name, "midiinfo")) qa_app_input_devices_info(qa_app_application_input_input_devices(input));
    else {
      qa_app_application_input_release_for_profile_change(input, NULL);
      qa_app_input_devices_restart(qa_app_application_input_input_devices(input));
      qa_input_router_restart(qa_app_application_input_router(input));
    }
    return true;
  }
  if (is(name, "quit")) {
    qa_application_request_quit(app);
    return true;
  }
  if (application && one_of(name, qa_app_application_audio_commands, qa_app_application_audio_command_count)) {
    audio_command(app, command, print, print_ctx);
    return true;
  }
  return false;
}

/* ---- guestCommand (TS 7450-7567) ---- */
/* TS q3GameCallback(() => guest.consoleCommand([name, ...args])). */
typedef struct guest_console_call { qa_app_q3_qvm_server_game *guest; const char **argv; size_t argc; bool handled; } guest_console_call;
static void guest_console_run(void *ctx) {
  guest_console_call *call = ctx;
  call->handled = qa_app_q3_qvm_server_game_console_command(call->guest, call->argv, call->argc);
}
static bool guest_console_command(qa_app_q3_qvm_server_game *guest, const qa_application_command_request *command) {
  const char **argv = command_argv(command);
  guest_console_call call = { guest, argv, command->argument_count + 1, false };
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, argv);
  qa_app_q3_game_call(guest_console_run, &call);
  qa_cleanup_pop(&cleanup);
  free(argv);
  return call.handled;
}
/* guest.players() as a malloc'd array. */
static qa_app_q3_player *guest_players(qa_app_q3_qvm_server_game *guest, size_t *out_count) {
  size_t count = qa_app_q3_qvm_server_game_players(guest, NULL, 0);
  qa_app_q3_player *players = checked(calloc(count + 1, sizeof *players));
  *out_count = qa_app_q3_qvm_server_game_players(guest, players, count);
  return players;
}
/* TS q3InfoValue(guest.state.getUserinfo(entity) ?? "", "name"): malloc'd. */
static char *guest_player_name(qa_app_q3_qvm_server_game *guest, double entity) {
  const char *userinfo = qa_app_q3_server_state_get_userinfo(qa_app_q3_qvm_server_game_state(guest), entity);
  return qa_q3_info_value(userinfo != NULL ? userinfo : "", "name");
}
static void guest_bot_command(qa_app_q3_qvm_server_game *guest, const qa_application_command_request *command,
    void (*print)(void *ctx, const char *text), void *print_ctx) {
  if (is(command->name, "addbot")) {
    if (!guest_console_command(guest, command)) print(print_ctx, "The selected game module does not provide addbot.\n");
    return;
  }
  size_t all_count = 0;
  qa_app_q3_player *all = guest_players(guest, &all_count);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, all);
  size_t bot_count = 0;
  for (size_t i = 0; i < all_count; i++) if (qa_app_q3_qvm_server_game_is_bot(guest, all[i].client)) all[bot_count++] = all[i];
  if (is(command->name, "botlist")) {
    for (size_t i = 0; i < bot_count; i++) {
      char *name = guest_player_name(guest, all[i].source_entity);
      char line[1024];
      (void)snprintf(line, sizeof line, "%s: %s\n", QA_JS_NUMBER(all[i].source_entity), name != NULL ? name : "");
      free(name);
      print(print_ctx, line);
    }
    char line[64];
    (void)snprintf(line, sizeof line, "%zu bot(s).\n", bot_count);
    print(print_ctx, line);
  } else {
    const char *target = request_arg(command, 0);
    if (target == NULL) qa_raise("Error", 0, 0, "Usage: removebot <slot|name|all>");
    for (size_t i = 0; i < bot_count; i++) {
      char slot[32];
      (void)snprintf(slot, sizeof slot, "%s", QA_JS_NUMBER(all[i].source_entity));
      char *name = guest_player_name(guest, all[i].source_entity);
      bool match = is(target, "all") || is(target, slot) || is(target, name != NULL ? name : "");
      free(name);
      if (match) qa_app_q3_qvm_server_game_disconnect(guest, all[i]);
    }
  }
  qa_cleanup_pop(&cleanup);
  free(all);
}
static bool guest_unsupported(const char *name) {
  return ONE_OF(name, "map", "map_restart", "devmap", "spmap", "spdevmap", "addbot", "removebot", "botlist");
}
/* TS /\.[^/]+$/.test(argument). */
static bool has_extension(const char *argument) {
  const char *dot = strrchr(argument, '.');
  return dot != NULL && dot[1] != '\0' && strchr(dot + 1, '/') == NULL;
}
static void guest_exec(qa_application *app, const qa_application_command_request *command, const qa_command_context *source,
    void (*print)(void *ctx, const char *text), void *print_ctx) {
  const char *argument = request_arg(command, 0);
  if (argument == NULL || command->argument_count != 1) qa_raise("Error", 0, 0, "Usage: exec <filename>");
  size_t length = strlen(argument) + sizeof ".cfg";
  char *path = checked(malloc(length));
  if (has_extension(argument)) (void)snprintf(path, length, "%s", argument);
  else (void)snprintf(path, length, "%s.cfg", argument);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, path);
  qa_content_opened_resource script;
  memset(&script, 0, sizeof script);
  char line[4200];
  if (!qa_content_mounted_open((qa_content_mounted *)qa_app_loaded_content_mounts(app->loaded_content), path, NULL, NULL, &script)) {
    (void)snprintf(line, sizeof line, "couldn't exec %s\n", path);
    print(print_ctx, line);
  } else {
    char *text = checked(malloc(script.byte_length + 1));
    memcpy(text, script.bytes, script.byte_length);
    text[script.byte_length] = '\0';
    free(script.bytes);
    (void)snprintf(line, sizeof line, "execing %s\n", path);
    qa_cleanup text_cleanup;
    qa_cleanup_push(&text_cleanup, free, text);
    print(print_ctx, line);
    qa_command_context caller = source != NULL ? *source : plain_context(app, QA_COMMAND_ORIGIN_SERVER_CONSOLE);
    qa_command_origin caller_origin = caller.origin;
    qa_command_context context;
    memset(&context, 0, sizeof context);
    context.session = caller.session;
    context.origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
    context.origin.name = path;
    context.origin.caller = &caller_origin;
    if (app->source_commands != NULL) qa_command_buffer_insert(app->source_commands, text, &context, -1);
    qa_cleanup_pop(&text_cleanup);
    free(text);
  }
  qa_cleanup_pop(&cleanup);
  free(path);
}
/* TS this.network?.server.disconnectClient(client, reason). */
static void network_disconnect(qa_application *app, qa_client_id client, const char *reason) {
  qa_application_native_server *network = app->network;
  if (network == NULL) return;
  switch (network->kind) {
    case QA_APPLICATION_NETWORK_Q1: (void)qa_app_q1_server_network_disconnect_client(network->q1, client, reason); break;
    case QA_APPLICATION_NETWORK_QW: (void)qa_app_qw_server_network_disconnect_client(network->qw, client, reason); break;
    case QA_APPLICATION_NETWORK_Q2: (void)qa_app_q2_server_network_disconnect_client(network->q2, client, reason); break;
    case QA_APPLICATION_NETWORK_Q3: (void)qa_app_q3_server_network_disconnect_client(network->q3, client, reason); break;
    case QA_APPLICATION_NETWORK_UNIFIED: (void)qa_app_unified_server_network_disconnect_client(network->unified, client, reason); break;
  }
}
static void guest_server_command(qa_application *app, qa_app_q3_qvm_server_game *guest, const qa_application_command_request *command,
    const qa_command_context *source, void (*print)(void *ctx, const char *text), void *print_ctx) {
  if (command->has_seat) qa_raise("Error", 0, 0, "Q3 guest local seats are unsupported");
  if (guest_unsupported(command->name)) qa_raise("Error", 0, 0, "Q3 guest command %s is unsupported", command->name);
  if (is(command->name, "exec")) guest_exec(app, command, source, print, print_ctx);
  else if (is(command->name, "kick")) {
    const char *target = request_arg(command, 0);
    if (target == NULL) qa_raise("Error", 0, 0, "Usage: kick <slot|all>");
    size_t count = 0;
    qa_app_q3_player *players = guest_players(guest, &count);
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, players);
    for (size_t i = 0; i < count; i++) {
      char slot[32];
      (void)snprintf(slot, sizeof slot, "%s", QA_JS_NUMBER(players[i].source_entity));
      if (is(target, "all") || is(target, slot)) network_disconnect(app, players[i].client, "was kicked");
    }
    qa_cleanup_pop(&cleanup);
    free(players);
  } else if (!guest_console_command(guest, command)) {
    char line[1100];
    (void)snprintf(line, sizeof line, "Unknown game command: %s\n", command->name);
    print(print_ctx, line);
  }
}
static bool guest_command(qa_application *app, qa_application_command_request *command, const qa_command_context *source,
    void (*print)(void *ctx, const char *text), void *print_ctx) {
  qa_app_shared_simulation *simulation = app->world_simulation;
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(simulation);
  bool client = command->target == QA_APPLICATION_TARGET_CLIENT;
  const char *name = command->name;
  const qa_seat_id *seat = command->has_seat ? &command->seat : NULL;
  if (client && guest == NULL) {
    if (seat == NULL) qa_raise("Error", 0, 0, "Reliable client command requires an invoking local seat");
    qa_app_shared_sim_player_command(simulation, command_actor(app, seat), name, request_args(command), command->argument_count);
    return true;
  }
  if (guest != NULL && !client) {
    size_t map_command_count = 0;
    const char *const *map_commands = qa_q3_product_map_commands(content_q3_policy(app), &map_command_count);
    if (one_of(name, map_commands, map_command_count) || is(name, "map_restart")) {
      if (is(name, "map_restart")) qa_application_request_restart(app, request_args(command), command->argument_count);
      else {
        const char *map = request_arg(command, 0);
        if (map == NULL || command->argument_count != 1) qa_raise("Error", 0, 0, "Usage: %s <name>", name);
        qa_app_q3_map_launch launch;
        qa_q3_product_policy policy = content_q3_policy(app);
        double game_type = latched_number(qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(guest)), "g_gametype");
        qa_app_q3_map_launch_make(&launch, policy, name, game_type);
        app->pending_q3_map = launch;
        app->has_pending_q3_map = true;
        set_pending_map(app, checked(qa_app_map_resource_path(map)));
        clear_pending_native_travel(app);
        clear_pending_restart(app);
      }
      return true;
    }
  }
  if (guest != NULL && !client && ONE_OF(name, "addbot", "removebot", "botlist")) {
    guest_bot_command(guest, command, print, print_ctx);
    return true;
  }
  if (ONE_OF(name, "+grapple", "-grapple", "+grenade", "-grenade")) {
    if (seat == NULL) qa_raise("Error", 0, 0, "Offhand commands require an invoking local seat");
    qa_actor_id actor = command_actor(app, seat);
    bool held = name[0] == '+';
    const qa_equipment_selection *equipment = &simulation->recipe.equipment;
    size_t length = strlen(name);
    bool grapple = length >= 7 && strcmp(name + length - 7, "grapple") == 0;
    bool available = grapple ? equipment->grapple.is_enabled && is(equipment->grapple.binding, "offhand")
                             : equipment->hand_grenades.is_enabled;
    if (!available) {
      if (held) qa_raise("Error", 0, 0, "No selected offhand action is available");
      return true;
    }
    if (grapple) qa_app_shared_sim_set_grapple_input(simulation, actor, held);
    else qa_app_shared_sim_set_hand_grenade_input(simulation, actor, held);
    return true;
  }
  if ((guest == NULL || seat != NULL) && (is(name, "use") || is(name, "drop"))
      && qa_app_shared_sim_source_item_command(simulation, command_actor(app, seat), name, request_args(command), command->argument_count))
    return true;
  if (guest != NULL && seat != NULL && ONE_OF(name, "use", "weapon", "weapnext", "weapprev")) {
    qa_actor_id actor = command_actor(app, seat);
    qa_resolved_weapon_selection selected;
    bool has_selected = resolve_selection(app, actor, name, request_args(command), command->argument_count, &selected);
    const char *item_args[1] = { has_selected ? selected.item : NULL };
    if (qa_app_shared_sim_selected_weapon_command(simulation, actor, has_selected ? "use" : name,
            has_selected ? item_args : request_args(command), has_selected ? 1 : command->argument_count))
      return true;
  }
  if (guest != NULL && seat != NULL && (client || (!is(name, "save") && !is(name, "load")))) {
    qa_application_q3_seat_client *seat_client = q3_seat_client(app, *seat);
    if (seat_client == NULL || !seat_client->qvm) qa_raise("Error", 0, 0, "Guest command has no local client");
    if (guest_unsupported(name)) qa_raise("Error", 0, 0, "Q3 guest command %s is unsupported", name);
    const char **argv = command_argv(command);
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, argv);
    if (client || !qa_app_application_q3_client_command(seat_client->client, argv, command->argument_count + 1)) {
      const qa_app_application_q3_client_options *options = qa_app_application_q3_client_options_of(seat_client->client);
      qa_app_q3_player player;
      if (!qa_app_q3_qvm_server_game_player(guest, qa_session_client_id(qa_session_seat_client(options->local->player.seat)), &player))
        qa_raise("Error", 0, 0, "Guest local client is disconnected");
      qa_app_q3_qvm_server_game_command(guest, player, argv, command->argument_count + 1);
    }
    qa_cleanup_pop(&cleanup);
    free(argv);
    return true;
  }
  if (guest != NULL && !is(name, "save") && !is(name, "load")) {
    guest_server_command(app, guest, command, source, print, print_ctx);
    return true;
  }
  return false;
}

/* ---- sourceCommand (TS 7570-7740) ---- */
static bool file_exists(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return false;
  fclose(file);
  return true;
}
/* The rewritten request of sourceCommand: borrows the original's strings. */
typedef struct source_request {
  const qa_application_command_request *original;
  const char *name;
  const char *const *arguments; size_t argument_count;
  const char *selected_argument[1];
} source_request;
static void source_player_command(qa_application *app, const source_request *command) {
  const qa_seat_id *seat = command->original->has_seat ? &command->original->seat : NULL;
  qa_app_shared_sim_player_command(app->world_simulation, command_actor(app, seat), command->name, command->arguments, command->argument_count);
}
/* The selected Q3 seat client of sourceCommand (TS graphical.q3.values().next().value or q3.get(seat)). */
static qa_application_q3_seat_client *source_seat_client(qa_application *app, const qa_seat_id *seat) {
  if (app->graphical == NULL) return NULL;
  if (seat == NULL) return app->graphical->q3_count == 0 ? NULL : &app->graphical->q3[0];
  return q3_seat_client(app, *seat);
}
/* Returns true when the arsenal branch handled the request. */
static bool source_arsenal_command(qa_application *app, const source_request *command, qa_application_q3_seat_client *client) {
  qa_app_shared_simulation *simulation = app->world_simulation;
  const qa_seat_id *seat = command->original->has_seat ? &command->original->seat : NULL;
  qa_actor_id actor = command_actor(app, seat);
  qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(simulation, actor);
  bool q3 = player != NULL && is(player->arsenal.state.kind, "q3");
  if (q3 && qa_app_shared_sim_has_weapon_slot(simulation, actor)) {
    qa_app_shared_sim_player_command(simulation, actor, command->name, command->arguments, command->argument_count);
    qa_app_sim_weapon_slot_projection slot = qa_app_shared_sim_weapon_slot(simulation, actor);
    free((void *)slot.ui.items);
    const qa_weapon_reference *selected = slot.pending != NULL ? slot.pending : slot.active;
    /* The player record may have moved during the command: read it again. */
    player = qa_app_shared_sim_movement_player(simulation, actor);
    const qa_q3_weapon_item *weapon = selected != NULL && player != NULL && is(selected->provider, player->arsenal.provider)
        ? q3_weapon_item_of(selected->item) : NULL;
    if (weapon != NULL) (void)seat_client_weapon(client, weapon->weapon);
    return true;
  }
  if (!q3 || !is(player->arsenal.provider, simulation->recipe.map.entities.provider)) {
    qa_app_shared_sim_player_command(simulation, actor, command->name, command->arguments, command->argument_count);
    return true;
  }
  return false;
}
/* TS name === "use" over the Q3 arsenal: true when handled. */
static bool source_use_command(qa_application *app, const source_request *command, qa_application_q3_seat_client *client) {
  qa_app_shared_simulation *simulation = app->world_simulation;
  const qa_seat_id *seat = command->original->has_seat ? &command->original->seat : NULL;
  qa_actor_id actor = command_actor(app, seat);
  qa_resolved_weapon_selection choice;
  bool has_choice = resolve_selection(app, actor, command->name, command->arguments, command->argument_count, &choice);
  qa_app_sim_player_ui ui = qa_app_shared_sim_player_ui(simulation, actor);
  const qa_app_sim_player_ui_item *item = NULL;
  for (size_t i = 0; i < ui.item_count && item == NULL; i++)
    if (is(ui.items[i].kind, "weapon") && ui.items[i].owned && has_choice && is(ui.items[i].id, choice.item)) item = &ui.items[i];
  const qa_q3_weapon_item *weapon = item == NULL ? NULL : q3_weapon_item_of(item->id);
  qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(simulation, actor);
  if (weapon == NULL || player == NULL || !is(player->arsenal.state.kind, "q3")) return false;
  if (client != NULL) {
    (void)seat_client_weapon(client, weapon->weapon);
    return true;
  }
  if (app->graphical == NULL) return false;
  size_t count = qa_app_application_input_local_count(app->graphical->input);
  for (size_t i = 0; i < count; i++) {
    qa_app_local_input *local = qa_app_application_input_local(app->graphical->input, i);
    if (!qa_same_actor(local->player.actor, actor)) continue;
    qa_app_arsenal_selection selection = { player->arsenal.provider, weapon->item };
    qa_app_application_input_set_arsenal_selection(app->graphical->input, qa_session_seat_id(local->player.seat), &selection);
    qa_weapon_reference reference = { player->arsenal.provider, weapon->item };
    (void)qa_app_shared_sim_request_weapon(simulation, actor, reference);
    return true;
  }
  return false;
}
static void source_load(qa_application *app, const source_request *command) {
  qa_app_load_request load = qa_app_parse_load_request(command->arguments, command->argument_count);
  const char *directory = qa_application_save_directory(app);
  char *path = checked(qa_save_command_path(directory, load.name));
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, path);
  size_t external_count = 0;
  const char *const *external = qa_application_external_save_directories(app, &external_count);
  if (!file_exists(path)) {
    char *resolved = qa_app_resolve_external_save_path(external, external_count, load.name);
    if (resolved != NULL) {
      qa_cleanup_pop(&cleanup);
      free(path);
      path = resolved;
      qa_cleanup_push(&cleanup, free, path);
    }
  }
  qa_assert_load_path_allowed(directory, path, command->original->has_source ? &command->original->source : NULL,
      app->launch_options.allow_load_paths, external, external_count);
  if (app->network != NULL) qa_raise("Error", 0, 0, "Save/load unavailable while hosting a network game.");
  qa_cleanup_pop(&cleanup);
  if (app->has_pending_save) { free(app->pending_save_path); free(app->pending_save_source_product); }
  app->has_pending_save = true;
  app->pending_save_path = path;
  app->pending_save_source_product = dup_text(load.source_product);
}
static void source_q1_status(qa_application *app, const source_request *command, const qa_command_context *source,
    void (*print)(void *ctx, const char *text), void *print_ctx) {
  qa_app_shared_simulation *simulation = app->world_simulation;
  if (command->argument_count != 0) qa_raise("Error", 0, 0, "Usage: %s", command->name);
  if (is(command->name, "pause")) {
    const qa_command_origin *origin = unwrapped_origin(source);
    qa_actor_id actor;
    bool server = origin != NULL && origin->kind == QA_COMMAND_ORIGIN_SERVER_CONSOLE;
    if (!server) actor = command_actor(app, command->original->has_seat ? &command->original->seat : NULL);
    print(print_ctx, qa_app_shared_sim_toggle_q1_pause(simulation, server ? NULL : &actor));
    return;
  }
  /* TS pings: network.server.clientPings() for Q1 and QW servers, else an empty map. */
  qa_client_id *ping_clients = NULL; double *ping_values = NULL; size_t ping_count = 0;
  qa_application_native_server *network = app->network;
  if (network != NULL && network->kind == QA_APPLICATION_NETWORK_Q1) {
    size_t total = qa_app_q1_server_network_client_pings(network->q1, NULL, 0);
    qa_app_q1_client_ping *rows = checked(calloc(total + 1, sizeof *rows));
    ping_count = qa_app_q1_server_network_client_pings(network->q1, rows, total);
    if (ping_count > total) ping_count = total;
    ping_clients = checked(calloc(ping_count + 1, sizeof *ping_clients));
    ping_values = checked(calloc(ping_count + 1, sizeof *ping_values));
    for (size_t i = 0; i < ping_count; i++) { ping_clients[i] = rows[i].client; ping_values[i] = rows[i].ping; }
    free(rows);
  } else if (network != NULL && network->kind == QA_APPLICATION_NETWORK_QW) {
    size_t total = qa_app_qw_server_network_client_pings(network->qw, NULL, NULL, 0);
    ping_clients = checked(calloc(total + 1, sizeof *ping_clients));
    ping_values = checked(calloc(total + 1, sizeof *ping_values));
    ping_count = qa_app_qw_server_network_client_pings(network->qw, ping_clients, ping_values, total);
    if (ping_count > total) ping_count = total;
  }
  qa_cleanup clients_cleanup, values_cleanup;
  qa_cleanup_push(&clients_cleanup, free, ping_clients);
  qa_cleanup_push(&values_cleanup, free, ping_values);
  bool ping = is(command->name, "ping");
  char line[4200];
  size_t player_count = 0;
  const qa_actor_id *listed = qa_app_shared_sim_players(simulation, &player_count);
  qa_actor_id *players = checked(calloc(player_count + 1, sizeof *players));
  if (player_count != 0) memcpy(players, listed, player_count * sizeof *players);
  qa_cleanup players_cleanup;
  qa_cleanup_push(&players_cleanup, free, players);
  if (ping) print(print_ctx, "Client ping times:\n");
  else {
    char count_text[32], max_text[32];
    (void)snprintf(count_text, sizeof count_text, "%zu", player_count);
    (void)snprintf(max_text, sizeof max_text, "%s", QA_JS_NUMBER(simulation->options.max_clients));
    (void)snprintf(line, sizeof line, "map: %s\nplayers: %s active (%s max)\n", app->launch_options.map, count_text, max_text);
    print(print_ctx, line);
  }
  for (size_t i = 0; i < player_count; i++) {
    qa_client_id client;
    if (!qa_app_shared_sim_player_client(simulation, players[i], &client)) continue;
    const char *name = qa_app_shared_sim_source_player_name(simulation, players[i]);
    if (ping) {
      double value = 0;
      for (size_t p = 0; p < ping_count; p++) if (qa_same_client(ping_clients[p], client)) { value = ping_values[p]; break; }
      (void)snprintf(line, sizeof line, "%s %s\n", QA_JS_NUMBER(value), name != NULL ? name : "");
    } else (void)snprintf(line, sizeof line, "#%s %s\n", QA_JS_NUMBER((double)client.slot + 1), name != NULL ? name : "");
    print(print_ctx, line);
  }
  qa_cleanup_pop(&players_cleanup);
  qa_cleanup_pop(&values_cleanup);
  qa_cleanup_pop(&clients_cleanup);
  free(players); free(ping_values); free(ping_clients);
}
typedef struct local_actor_call { qa_application *app; const qa_seat_id *seat; } local_actor_call;
static qa_actor_id local_actor(void *ctx) {
  local_actor_call *call = ctx;
  return command_actor(call->app, call->seat);
}
static void source_q1_host_command(qa_application *app, const source_request *command, const qa_command_context *source) {
  qa_app_shared_simulation *simulation = app->world_simulation;
  size_t count = 0;
  const qa_actor_id *listed = qa_app_shared_sim_players(simulation, &count);
  qa_app_q1_host_player *players = checked(calloc(count + 1, sizeof *players));
  size_t player_count = 0;
  for (size_t i = 0; i < count; i++) {
    qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(simulation, listed[i]);
    if (player == NULL) continue;
    players[player_count].actor = listed[i];
    players[player_count].client = player->client;
    player_count++;
  }
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, players);
  local_actor_call call = { app, command->original->has_seat ? &command->original->seat : NULL };
  qa_actor_id actor = qa_app_resolve_q1_host_command_actor(command->name, command->arguments, command->argument_count, source,
      players, player_count, local_actor, &call, is(command->name, "give"));
  const qa_command_origin *origin = unwrapped_origin(source);
  bool server = origin != NULL && origin->kind == QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  /* args.slice(1) of an empty list is empty. */
  size_t skip = server && command->argument_count != 0 ? 1 : 0;
  qa_app_shared_sim_player_command(simulation, actor, command->name, command->arguments + skip, command->argument_count - skip);
  qa_cleanup_pop(&cleanup);
  free(players);
}
static void source_command(qa_application *app, qa_application_command_request *request, const qa_command_context *source,
    void (*print)(void *ctx, const char *text), void *print_ctx) {
  qa_app_shared_simulation *simulation = app->world_simulation;
  const qa_seat_id *seat = request->has_seat ? &request->seat : NULL;
  source_request command = { request, request->name, request_args(request), request->argument_count, { NULL } };
  if (is(command.name, "use") || is(command.name, "weapon")) {
    qa_resolved_weapon_selection selected;
    if (resolve_selection(app, command_actor(app, seat), command.name, command.arguments, command.argument_count, &selected)) {
      command.name = "use";
      command.selected_argument[0] = selected.item;
      command.arguments = command.selected_argument;
      command.argument_count = 1;
    }
  }
  qa_application_q3_seat_client *client = source_seat_client(app, seat);
  if (client != NULL && (is(command.name, "use") || is(command.name, "weapnext") || is(command.name, "weapprev"))
      && source_arsenal_command(app, &command, client))
    return;
  if (is(command.name, "use") && source_use_command(app, &command, client)) return;
  if (client != NULL) {
    const char **argv = checked(calloc(command.argument_count + 2, sizeof *argv));
    argv[0] = command.name;
    for (size_t i = 0; i < command.argument_count; i++) argv[i + 1] = command.arguments[i];
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, argv);
    bool handled = qa_app_application_q3_client_command(client->client, argv, command.argument_count + 1);
    qa_cleanup_pop(&cleanup);
    free(argv);
    if (handled) return;
  }
  const char *name = command.name;
  bool q3_source = qa_app_shared_sim_q3_source(simulation) != NULL;
  bool q2_native = qa_app_shared_sim_q2_native(simulation, NULL, NULL);
  bool q1_source = qa_app_shared_sim_q1_source(simulation) != NULL;
  bool quakec_source = qa_app_shared_sim_quakec_source(simulation) != NULL;
  bool q2_source = qa_app_shared_sim_q2_source(simulation) != NULL;
  if (is(name, "addbot") || is(name, "botlist")) {
    if (app->bots == NULL) {
      qa_application_create_bots_setup setup;
      memset(&setup, 0, sizeof setup);
      setup.requested = true;
      app->bots = qa_application_create_bots(app, app->loaded_content, simulation, &setup);
    }
    if (app->bots == NULL) qa_raise("Error", 0, 0, "Bot observations are unavailable for this world");
    const char **argv = checked(calloc(command.argument_count + 2, sizeof *argv));
    argv[0] = name;
    for (size_t i = 0; i < command.argument_count; i++) argv[i + 1] = command.arguments[i];
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, argv);
    app->bots->console_command(app->bots->ctx, argv, command.argument_count + 1);
    qa_cleanup_pop(&cleanup);
    free(argv);
  } else if (is(name, "kick")) qa_application_kick_clients(app, command.arguments, command.argument_count);
  else if (is(name, "removebot")) {
    const char *argument = command.argument_count == 0 ? NULL : command.arguments[0];
    const qa_app_sim_bot_client *bots = NULL;
    size_t bot_count = qa_application_bot_clients(app, &bots);
    for (size_t i = 0; i < bot_count; i++) {
      char slot[32];
      int64_t client_slot = qa_session_client_id(bots[i].client).slot;
      (void)snprintf(slot, sizeof slot, "%lld", (long long)client_slot);
      if (argument != NULL && !is(slot, argument)) continue;
      if (app->bots != NULL) (void)app->bots->disconnect(app->bots->ctx, client_slot);
      break;
    }
  } else if (is(name, "save")) {
    qa_app_save_request save = qa_app_parse_save_request(command.arguments, command.argument_count);
    char *path = checked(qa_save_command_path(qa_application_save_directory(app), save.name));
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, path);
    qa_application_save_game(app, path, save.format);
    size_t length = strlen(path) + 16;
    char *line = checked(malloc(length));
    (void)snprintf(line, length, "Saved %s.\n", path);
    qa_cleanup_pop(&cleanup);
    free(path);
    qa_cleanup_push(&cleanup, free, line);
    print(print_ctx, line);
    qa_cleanup_pop(&cleanup);
    free(line);
  } else if (is(name, "weapnext") || is(name, "weapprev") || is(name, "use")) source_player_command(app, &command);
  else if (is(name, "say") || is(name, "say_team")) {
    if (!q1_source && !q2_source && !q2_native && !q3_source)
      qa_raise("Error", 0, 0, "Selected guest source chat requires its native command channel");
    source_player_command(app, &command);
  } else if (is(name, "map_restart")) qa_application_request_restart(app, command.arguments, command.argument_count);
  else if (is(name, "load")) source_load(app, &command);
  else if (is(name, "teamarena-next") || is(name, "teamarena-retry")) {
    qa_app_q3_source_runtime *q3 = qa_app_shared_sim_q3_source(simulation);
    if (!qa_application_is_team_arena_skirmish(app, NULL) || (q3 != NULL && qa_app_q3_source_runtime_intermission_time(q3) == 0))
      qa_raise("Error", 0, 0, "Team Arena match has not finished");
    app->pending_team_arena = is(name, "teamarena-next") ? "next" : "retry";
  } else if ((is(name, "gamemap") || is(name, "changelevel")) && q2_native) {
    const char *map = command.argument_count == 0 ? NULL : command.arguments[0];
    if (map == NULL || command.argument_count != 1) qa_raise("Error", 0, 0, "Usage: %s <map>", name);
    qa_app_q2_travel_target *target = qa_app_parse_q2_travel(map);
    clear_pending_native_travel(app);
    app->pending_native_travel = *target;
    app->has_pending_native_travel = true;
    free(target);
    set_pending_map(app, NULL);
    app->has_pending_q3_map = false;
    clear_pending_restart(app);
    app->has_pending_transition = false;
  } else if (ONE_OF(name, "map", "gamemap", "changelevel") || (q3_source && ONE_OF(name, "devmap", "spmap", "spdevmap"))) {
    const char *map = command.argument_count == 0 ? NULL : command.arguments[0];
    if (map == NULL) qa_raise("Error", 0, 0, "Usage: map <name>");
    qa_app_q3_source_runtime *q3 = qa_app_shared_sim_q3_source(simulation);
    if (q3 == NULL) app->has_pending_q3_map = false;
    else {
      qa_app_q3_map_launch launch;
      qa_q3_product_policy policy = content_q3_policy(app);
      double game_type = latched_number(qa_app_q3_source_runtime_host(q3)->cvars, "g_gametype");
      qa_app_q3_map_launch_make(&launch, policy, is(name, "gamemap") || is(name, "changelevel") ? "map" : name, game_type);
      app->pending_q3_map = launch;
      app->has_pending_q3_map = true;
    }
    set_pending_map(app, checked(qa_app_map_resource_path(map)));
    clear_pending_native_travel(app);
  } else if ((is(name, "pause") || is(name, "status") || is(name, "ping")) && (q1_source || quakec_source))
    source_q1_status(app, &command, source, print, print_ctx);
  else if (ONE_OF(name, "god", "notarget", "noclip", "fly", "give", "giveall", "kill", "suicide"))
    source_q1_host_command(app, &command, source);
  else if (q1_source || quakec_source || q2_source || q2_native || q3_source) {
    qa_app_q3_source_runtime *q3 = qa_app_shared_sim_q3_source(simulation);
    bool handled = false;
    if (q3 != NULL) {
      const char **argv = checked(calloc(command.argument_count + 2, sizeof *argv));
      argv[0] = name;
      for (size_t i = 0; i < command.argument_count; i++) argv[i + 1] = command.arguments[i];
      qa_cleanup cleanup;
      qa_cleanup_push(&cleanup, free, argv);
      handled = qa_q3_ta_game_server_command_runtime_console_command(
          qa_app_q3_source_subsystems_owner_server_commands(qa_app_q3_source_runtime_owner(q3)), argv, command.argument_count + 1);
      qa_cleanup_pop(&cleanup);
      free(argv);
    }
    if (!handled) source_player_command(app, &command);
  } else qa_raise("Error", 0, 0, "Unknown application command: %s", name);
}

/* ---- changeLocalPlayers (TS 6894-7090) ---- */
typedef struct local_change {
  qa_application *app;
  qa_application_graphical *graphical;
  const char *kind;
  const char *const *args; size_t arg_count;
  const qa_seat_id *invoking_seat;
  qa_app_prepared_local_seat_change *change;
  qa_app_local_seat_preparation *prepared_input;
  bool committed;
  qa_app_world_seat_presentation *staged_presentation;
  qa_app_seat_ui *staged_ui;
  bool has_staged_native; qa_application_native_q2_seat_client staged_native;
  /* scratch */
  qa_app_local_seat_identity *current; size_t current_count;
  qa_session_seat **next_seats;
  qa_cvar_archive_entry *archive; size_t archive_count;
  qa_app_local_player *players; size_t player_count;
  qa_application_seat_presentation *presentations; size_t presentation_count;
  qa_session_presentation_entry addition; bool has_addition;
  qa_app_rerelease_seat *rerelease_seats;
  char *userinfo;
} local_change;
static void local_change_scratch_free(local_change *c) {
  free(c->current); free(c->next_seats); free(c->players); free(c->presentations); free(c->rerelease_seats); free(c->userinfo);
  qa_app_cvar_archive_entries_free(c->archive, c->archive_count);
  c->current = NULL; c->next_seats = NULL; c->players = NULL; c->presentations = NULL; c->rerelease_seats = NULL; c->userinfo = NULL;
  c->archive = NULL; c->archive_count = 0;
}
static void grow_array(void **items, size_t *capacity, size_t needed, size_t size) {
  if (needed <= *capacity) return;
  size_t next = *capacity == 0 ? 4 : *capacity * 2;
  while (next < needed) next *= 2;
  *items = checked(realloc(*items, next * size));
  *capacity = next;
}
/* Presentations hold their seat's q3 client by address: re-point them after the q3 rows moved. */
static void repoint_q3_clients(qa_application_graphical *graphical) {
  for (size_t i = 0; i < graphical->presentation_count; i++) {
    qa_application_seat_presentation *presentation = &graphical->presentations[i];
    presentation->q3_client = NULL;
    qa_seat_id seat = qa_session_seat_id(presentation->local->player.seat);
    for (size_t q = 0; q < graphical->q3_count; q++)
      if (qa_same_seat(graphical->q3_seats[q], seat)) presentation->q3_client = &graphical->q3[q];
  }
}
static void q3_client_delete(qa_application_graphical *graphical, qa_seat_id seat) {
  for (size_t i = 0; i < graphical->q3_count; i++) {
    if (!qa_same_seat(graphical->q3_seats[i], seat)) continue;
    memmove(&graphical->q3_seats[i], &graphical->q3_seats[i + 1], (graphical->q3_count - i - 1) * sizeof *graphical->q3_seats);
    memmove(&graphical->q3[i], &graphical->q3[i + 1], (graphical->q3_count - i - 1) * sizeof *graphical->q3);
    graphical->q3_count--;
    repoint_q3_clients(graphical);
    return;
  }
}
static void q3_client_set(qa_application_graphical *graphical, qa_seat_id seat, qa_application_q3_seat_client client) {
  for (size_t i = 0; i < graphical->q3_count; i++)
    if (qa_same_seat(graphical->q3_seats[i], seat)) { graphical->q3[i] = client; repoint_q3_clients(graphical); return; }
  graphical->q3_seats = checked(realloc(graphical->q3_seats, (graphical->q3_count + 1) * sizeof *graphical->q3_seats));
  graphical->q3 = checked(realloc(graphical->q3, (graphical->q3_count + 1) * sizeof *graphical->q3));
  graphical->q3_seats[graphical->q3_count] = seat;
  graphical->q3[graphical->q3_count] = client;
  graphical->q3_count++;
  repoint_q3_clients(graphical);
}
static void native_q2_delete_at(qa_application_graphical *graphical, size_t index) {
  memmove(&graphical->native_q2_seats[index], &graphical->native_q2_seats[index + 1],
      (graphical->native_q2_count - index - 1) * sizeof *graphical->native_q2_seats);
  memmove(&graphical->native_q2[index], &graphical->native_q2[index + 1], (graphical->native_q2_count - index - 1) * sizeof *graphical->native_q2);
  graphical->native_q2_count--;
}
static void native_q2_set(qa_application_graphical *graphical, qa_seat_id seat, qa_application_native_q2_seat_client client) {
  for (size_t i = 0; i < graphical->native_q2_count; i++)
    if (qa_same_seat(graphical->native_q2_seats[i], seat)) { graphical->native_q2[i] = client; return; }
  graphical->native_q2_seats = checked(realloc(graphical->native_q2_seats, (graphical->native_q2_count + 1) * sizeof *graphical->native_q2_seats));
  graphical->native_q2 = checked(realloc(graphical->native_q2, (graphical->native_q2_count + 1) * sizeof *graphical->native_q2));
  graphical->native_q2_seats[graphical->native_q2_count] = seat;
  graphical->native_q2[graphical->native_q2_count] = client;
  graphical->native_q2_count++;
}
/* this.localSeats.set / clientCvars.set / sourceConnections.set and their deletes. */
static void local_seat_set(qa_application *app, qa_client_id client, qa_session_seat *seat) {
  for (size_t i = 0; i < app->local_seat_count; i++)
    if (qa_same_client(app->local_seat_clients[i], client)) { app->local_seats[i] = seat; return; }
  size_t capacity = app->local_seat_capacity;
  grow_array((void **)&app->local_seat_clients, &capacity, app->local_seat_count + 1, sizeof *app->local_seat_clients);
  capacity = app->local_seat_capacity;
  grow_array((void **)&app->local_seats, &capacity, app->local_seat_count + 1, sizeof *app->local_seats);
  app->local_seat_capacity = capacity;
  app->local_seat_clients[app->local_seat_count] = client;
  app->local_seats[app->local_seat_count] = seat;
  app->local_seat_count++;
}
static void local_seat_delete(qa_application *app, qa_client_id client) {
  for (size_t i = 0; i < app->local_seat_count; i++) {
    if (!qa_same_client(app->local_seat_clients[i], client)) continue;
    memmove(&app->local_seat_clients[i], &app->local_seat_clients[i + 1], (app->local_seat_count - i - 1) * sizeof *app->local_seat_clients);
    memmove(&app->local_seats[i], &app->local_seats[i + 1], (app->local_seat_count - i - 1) * sizeof *app->local_seats);
    app->local_seat_count--;
    return;
  }
}
static void client_cvars_set(qa_application *app, qa_seat_id seat, qa_cvar_registry *cvars) {
  for (size_t i = 0; i < app->client_cvar_count; i++)
    if (qa_same_seat(app->client_cvar_seats[i], seat)) { app->client_cvars[i] = cvars; return; }
  size_t capacity = app->client_cvar_capacity;
  grow_array((void **)&app->client_cvar_seats, &capacity, app->client_cvar_count + 1, sizeof *app->client_cvar_seats);
  capacity = app->client_cvar_capacity;
  grow_array((void **)&app->client_cvars, &capacity, app->client_cvar_count + 1, sizeof *app->client_cvars);
  app->client_cvar_capacity = capacity;
  app->client_cvar_seats[app->client_cvar_count] = seat;
  app->client_cvars[app->client_cvar_count] = cvars;
  app->client_cvar_count++;
}
static void client_cvars_delete(qa_application *app, qa_seat_id seat) {
  for (size_t i = 0; i < app->client_cvar_count; i++) {
    if (!qa_same_seat(app->client_cvar_seats[i], seat)) continue;
    memmove(&app->client_cvar_seats[i], &app->client_cvar_seats[i + 1], (app->client_cvar_count - i - 1) * sizeof *app->client_cvar_seats);
    memmove(&app->client_cvars[i], &app->client_cvars[i + 1], (app->client_cvar_count - i - 1) * sizeof *app->client_cvars);
    app->client_cvar_count--;
    return;
  }
}
static void source_connection_set(qa_application *app, qa_session_client *client, qa_session_connection *connection) {
  for (size_t i = 0; i < app->source_connection_count; i++)
    if (app->source_connection_clients[i] == client) { app->source_connections[i] = connection; return; }
  size_t capacity = app->source_connection_capacity;
  grow_array((void **)&app->source_connection_clients, &capacity, app->source_connection_count + 1, sizeof *app->source_connection_clients);
  capacity = app->source_connection_capacity;
  grow_array((void **)&app->source_connections, &capacity, app->source_connection_count + 1, sizeof *app->source_connections);
  app->source_connection_capacity = capacity;
  app->source_connection_clients[app->source_connection_count] = client;
  app->source_connections[app->source_connection_count] = connection;
  app->source_connection_count++;
}
static void source_connection_delete(qa_application *app, qa_session_client *client) {
  for (size_t i = 0; i < app->source_connection_count; i++) {
    if (app->source_connection_clients[i] != client) continue;
    memmove(&app->source_connection_clients[i], &app->source_connection_clients[i + 1],
        (app->source_connection_count - i - 1) * sizeof *app->source_connection_clients);
    memmove(&app->source_connections[i], &app->source_connections[i + 1], (app->source_connection_count - i - 1) * sizeof *app->source_connections);
    app->source_connection_count--;
    return;
  }
}
static void local_guest_seat_delete(qa_application_local_q3_guest_world *world, qa_seat_id seat) {
  if (world == NULL) return;
  for (size_t i = 0; i < world->seat_count; i++) {
    if (!qa_same_seat(world->seat_ids[i], seat)) continue;
    memmove(&world->seat_ids[i], &world->seat_ids[i + 1], (world->seat_count - i - 1) * sizeof *world->seat_ids);
    memmove(&world->seats[i], &world->seats[i + 1], (world->seat_count - i - 1) * sizeof *world->seats);
    world->seat_count--;
    return;
  }
}
/* requestedCommands.filterLive(command => command.seat === null || !command.seat.equals(removedSeat)). */
static bool request_not_for_seat(const void *record, void *ctx) {
  const qa_application_command_request *request = record;
  const qa_seat_id *removed = ctx;
  if (!request->has_seat || !qa_same_seat(request->seat, *removed)) return true;
  qa_application_command_request_free((qa_application_command_request *)record);
  return false;
}
static int player_seat_order(const void *left, const void *right) {
  int64_t a = qa_session_seat_id(((const qa_app_local_player *)left)->seat).index;
  int64_t b = qa_session_seat_id(((const qa_app_local_player *)right)->seat).index;
  return a < b ? -1 : a > b ? 1 : 0;
}
static int presentation_seat_order(const void *left, const void *right) {
  int64_t a = qa_session_seat_id(((const qa_application_seat_presentation *)left)->local->player.seat).index;
  int64_t b = qa_session_seat_id(((const qa_application_seat_presentation *)right)->local->player.seat).index;
  return a < b ? -1 : a > b ? 1 : 0;
}
/* additions cleanup: () => presentation.close(); the C presentation is freed with it (TS GC). */
static void addition_cleanup(void *ctx) {
  qa_app_world_seat_presentation_close(ctx);
  qa_app_world_seat_presentation_destroy(ctx);
}
/* `${cvars.infoString(CvarFlag.UserInfo)}\\ip\\localhost`. */
static char *localhost_userinfo(qa_cvar_registry *cvars) {
  char *info = qa_cvar_registry_info_string(cvars, QA_CVAR_FLAG_USER_INFO, -1);
  size_t length = strlen(info != NULL ? info : "") + sizeof "\\ip\\localhost";
  char *text = checked(malloc(length));
  (void)snprintf(text, length, "%s\\ip\\localhost", info != NULL ? info : "");
  free(info);
  return text;
}
static void admit_added(local_change *c, const qa_app_local_seat_identity *added, qa_cvar_registry *cvars) {
  qa_application *app = c->app;
  qa_app_shared_simulation *simulation = app->world_simulation;
  qa_client_id client = qa_session_client_id(added->client);
  qa_actor_id actor;
  if (app->local_guest != NULL) {
    qa_app_sim_q3_server_authority *authority = &app->local_guest->authority;
    c->userinfo = localhost_userinfo(cvars);
    qa_app_q3_admission admission = authority->connect(authority->host.ctx, client, c->userinfo);
    if (!admission.accepted) qa_raise("Error", 0, 0, "%s", admission.reason != NULL ? admission.reason : "");
    qa_application_prepare_local_guest_seat(app, app->local_guest, simulation, added->seat, cvars, c->userinfo,
        authority->host.game_state(authority->host.ctx, admission.player, 1));
    if (authority->host.begin != NULL) {
      qa_q3_wire_user_command command;
      memset(&command, 0, sizeof command);
      command.weapon = 2;
      authority->host.begin(authority->host.ctx, admission.player, &command);
    }
    actor = admission.player.actor;
  } else {
    qa_app_q3_source_runtime *q3 = qa_app_shared_sim_q3_source(simulation);
    if (q3 != NULL) {
      char *userinfo = localhost_userinfo(cvars);
      qa_app_q3_source_host *host = qa_app_q3_source_runtime_host(q3);
      host->engine.set_userinfo(host->engine.ctx, (double)client.slot, userinfo);
      free(userinfo);
    }
    qa_app_sim_player_admission admitted;
    if (!qa_app_shared_sim_q2_native(simulation, NULL, NULL)) {
      char *userinfo = qa_app_player_userinfo(cvars);
      qa_cleanup cleanup;
      qa_cleanup_push(&cleanup, free, userinfo);
      admitted = qa_app_shared_sim_admit_player_with(simulation, client, NULL, userinfo);
      qa_cleanup_pop(&cleanup);
      free(userinfo);
    } else {
      c->userinfo = localhost_userinfo(cvars);
      admitted = qa_app_shared_sim_admit_q2_native_player(simulation, client, c->userinfo, true);
    }
    actor = admitted.actor;
  }
  c->players[c->player_count++] = (qa_app_local_player){ added->seat, actor };
}
static void drop_removed(local_change *c, const qa_app_local_seat_identity *removed_identity) {
  qa_application *app = c->app;
  qa_application_graphical *graphical = c->graphical;
  qa_app_shared_simulation *simulation = app->world_simulation;
  qa_app_local_input *removed = NULL;
  size_t local_count = qa_app_application_input_local_count(graphical->input);
  for (size_t i = 0; i < local_count && removed == NULL; i++) {
    qa_app_local_input *local = qa_app_application_input_local(graphical->input, i);
    if (local->player.seat == removed_identity->seat) removed = local;
  }
  if (removed == NULL) qa_raise("Error", 0, 0, "Dropped player is missing");
  qa_actor_id removed_actor = removed->player.actor;
  for (size_t i = 0; i < graphical->presentation_count; i++)
    if (graphical->presentations[i].local->player.seat == removed_identity->seat) { qa_app_seat_ui_close_menus(graphical->presentations[i].ui); break; }
  qa_client_id client = qa_session_client_id(removed_identity->client);
  qa_seat_id seat = qa_session_seat_id(removed_identity->seat);
  qa_application_disconnect_rankings(app, simulation, client);
  qa_application_q3_seat_client *seat_client = q3_seat_client(app, seat);
  if (seat_client != NULL) qa_app_application_q3_client_shutdown(seat_client->client);
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(simulation);
  if (guest != NULL) {
    size_t count = 0;
    qa_app_q3_player *players = guest_players(guest, &count);
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, players);
    const qa_app_q3_player *found = NULL;
    for (size_t i = 0; i < count && found == NULL; i++) if (qa_same_actor(players[i].actor, removed_actor)) found = &players[i];
    if (found == NULL) qa_raise("Error", 0, 0, "Guest player is missing");
    qa_app_q3_qvm_server_game_disconnect(guest, *found);
    qa_cleanup_pop(&cleanup);
    free(players);
  } else qa_app_shared_sim_disconnect_player(simulation, removed_actor);
  local_seat_delete(app, client);
  client_cvars_delete(app, seat);
  local_guest_seat_delete(app->local_guest, seat);
  q3_client_delete(graphical, seat);
  source_connection_delete(app, removed_identity->client);
  qa_event_ring_filter_live(&app->requested_commands, request_not_for_seat, &seat);
  size_t kept = 0;
  for (size_t i = 0; i < app->client_input_count; i++)
    if (!qa_same_seat(app->client_inputs[i].seat, seat)) app->client_inputs[kept++] = app->client_inputs[i];
  app->client_input_count = kept;
}
static void publish_local_guest_snapshots_run(void *ctx) { qa_application_publish_local_guest_snapshots(ctx); }
static void retired_close_run(void *ctx) {
  qa_session_resource *retired = ctx;
  if (retired->close != NULL) retired->close(retired->ctx);
}
static void change_body(void *ctx) {
  local_change *c = ctx;
  qa_application *app = c->app;
  qa_application_graphical *graphical = c->graphical;
  qa_app_shared_simulation *simulation = app->world_simulation;
  qa_app_application_input_save_settings(graphical->input);
  qa_application_save_client_archives(app, &app->launch_options, app->loaded_content, app->client_cvar_seats,
      (qa_cvar_registry *const *)app->client_cvars, app->client_cvar_count, app->team_arena_overrides);
  size_t next_count = 0;
  const qa_app_local_seat_identity *next = qa_app_prepared_local_seat_change_next(c->change, &next_count);
  c->next_seats = checked(calloc(next_count + 1, sizeof *c->next_seats));
  for (size_t i = 0; i < next_count; i++) c->next_seats[i] = next[i].seat;
  c->prepared_input = qa_app_application_input_prepare_local_seats(graphical->input, c->next_seats, next_count);
  const qa_app_local_seat_identity *added = qa_app_prepared_local_seat_change_added(c->change);
  if (added != NULL) c->archive = qa_application_load_client_archive(app, added->seat, app->loaded_content, NULL, &c->archive_count);
  qa_text_font_selection font = qa_app_application_assets_load_console_font(graphical->assets);
  const qa_app_menu_typography *typography = qa_app_application_assets_load_menu_typography(graphical->assets);
  const qa_q3_character_assets *characters = NULL;
  if (added != NULL && app->launch_options.character == QA_GAME_FAMILY_Q3)
    characters = qa_app_load_q3_character_for_content(app->loaded_content,
        qa_app_loaded_content_recipe(app->loaded_content)->character.appearance.content, app->launch_options.character_model);
  qa_app_prepared_local_seat_change_validate(c->change, NULL, 0);
  if (app->graphical != graphical || app->closed) qa_raise("Error", 0, 0, "World changed during local player preparation");
  c->committed = true;
  const qa_app_local_seat_identity *removed = qa_app_prepared_local_seat_change_removed(c->change);
  size_t local_count = qa_app_application_input_local_count(graphical->input);
  c->players = checked(calloc(local_count + 2, sizeof *c->players));
  for (size_t i = 0; i < local_count; i++) {
    qa_app_local_input *local = qa_app_application_input_local(graphical->input, i);
    if (removed != NULL && local->player.seat == removed->seat) continue;
    c->players[c->player_count++] = local->player;
  }
  if (added != NULL) {
    qa_cvar_registry *cvars = qa_application_create_client_cvars(app, simulation, app->loaded_content, &app->launch_options, added->seat,
        NULL, NULL, c->archive, c->archive_count, true);
    local_seat_set(app, qa_session_client_id(added->client), added->seat);
    client_cvars_set(app, qa_session_seat_id(added->seat), cvars);
    source_connection_set(app, added->client, qa_session_client_connect(added->client, "loopback"));
    admit_added(c, added, cvars);
  }
  if (removed != NULL) drop_removed(c, removed);
  qa_js_sort(c->players, c->player_count, sizeof *c->players, player_seat_order);
  qa_app_local_seat_preparation_publish(c->prepared_input, c->players, c->player_count, app->ownership.borrowed_client, NULL, false);
  if (added != NULL) {
    qa_seat_id added_seat = qa_session_seat_id(added->seat);
    qa_application_restore_source_input_angles(app, graphical->input, simulation, &added_seat);
  }
  app->launch_options.seats = (double)c->player_count;
  c->rerelease_seats = checked(calloc(c->player_count + 1, sizeof *c->rerelease_seats));
  for (size_t i = 0; i < c->player_count; i++)
    c->rerelease_seats[i] = (qa_app_rerelease_seat){ qa_session_seat_id(c->players[i].seat), c->players[i].actor, NULL };
  qa_app_rerelease_publish_seats(graphical->rerelease, c->rerelease_seats, c->player_count);
  c->presentations = checked(calloc(graphical->presentation_count + 2, sizeof *c->presentations));
  for (size_t i = 0; i < graphical->presentation_count; i++) {
    if (removed != NULL && graphical->presentations[i].local->player.seat == removed->seat) continue;
    c->presentations[c->presentation_count++] = graphical->presentations[i];
  }
  if (added != NULL) {
    qa_app_local_input *local = NULL;
    size_t count = qa_app_application_input_local_count(graphical->input);
    for (size_t i = 0; i < count && local == NULL; i++) {
      qa_app_local_input *candidate = qa_app_application_input_local(graphical->input, i);
      if (candidate->player.seat == added->seat) local = candidate;
    }
    if (local == NULL) qa_raise("Error", 0, 0, "Admitted local input is missing");
    qa_seat_id seat = qa_session_seat_id(local->player.seat);
    qa_application_q3_seat_client source_client;
    bool has_source = qa_application_create_q3_seat_client(app, local, graphical->assets, graphical->audio, graphical->input,
        graphical->renderer, simulation, NULL, 0, app->local_guest, app->guest_browser, qa_application_client_cvars_get(app, seat), &source_client);
    if (has_source) q3_client_set(graphical, seat, source_client);
    qa_application_q3_seat_client *stored_source = has_source ? q3_seat_client(app, seat) : NULL;
    double field_of_view;
    if (qa_app_view_settings_override(app->view_settings, &field_of_view)) {
      double current = qa_app_view_settings_field_of_view(app->view_settings);
      if (stored_source == NULL || !stored_source->qvm)
        qa_app_shared_sim_set_player_field_of_view(simulation, local->player.actor, current, "change");
      if (stored_source != NULL) {
        char text[32];
        (void)snprintf(text, sizeof text, "%s", QA_JS_NUMBER(current));
        qa_application_cvar_set(qa_app_application_q3_client_cvars(stored_source->client), "cg_fov", text, false);
      }
    }
    c->has_staged_native = qa_application_prepare_native_q2_seat(app, local, graphical->assets, simulation,
        qa_application_client_cvars_get(app, seat), &c->staged_native);
    c->staged_ui = qa_application_create_seat_ui(app, local, graphical->art, graphical->input, graphical->renderer, simulation, font,
        graphical->audio, typography, graphical->rerelease, app->loaded_content, &app->launch_options,
        stored_source != NULL && stored_source->qvm, NULL, NULL, 0);
    c->staged_presentation = qa_application_create_world_seat_presentation(app, local, graphical->assets, graphical->renderer, simulation,
        (double)c->player_count, font, characters, c->staged_ui, graphical->effects, stored_source, graphical->rerelease,
        c->has_staged_native ? &c->staged_native : NULL, NULL);
    c->presentations[c->presentation_count++] = (qa_application_seat_presentation){ c->staged_presentation, local, c->staged_ui, stored_source };
    c->addition.seat = added->seat;
    c->addition.presentation = qa_app_world_seat_presentation_seat(c->staged_presentation);
    c->addition.cleanup = addition_cleanup;
    c->addition.cleanup_ctx = c->staged_presentation;
    c->has_addition = true;
  }
  qa_js_sort(c->presentations, c->presentation_count, sizeof *c->presentations, presentation_seat_order);
  for (size_t i = 0; i < c->presentation_count; i++)
    qa_app_world_seat_presentation_publish_layout(c->presentations[i].presentation, (double)i, (double)c->presentation_count);
  qa_session_resource retired = qa_app_prepared_local_seat_change_publish(c->change, c->has_addition ? &c->addition : NULL,
      c->has_addition ? 1 : 0);
  for (size_t i = graphical->native_q2_count; i-- > 0;) {
    bool kept = false;
    for (size_t p = 0; p < c->presentation_count && !kept; p++)
      kept = qa_same_seat(qa_session_seat_id(c->presentations[p].local->player.seat), graphical->native_q2_seats[i]);
    if (!kept) native_q2_delete_at(graphical, i);
  }
  if (c->has_staged_native && c->staged_presentation != NULL)
    native_q2_set(graphical, qa_session_seat_id(qa_app_world_seat_presentation_local(c->staged_presentation)->player.seat), c->staged_native);
  /* this.graphical = { ...graphical, presentations } */
  free(graphical->presentations);
  graphical->presentations = c->presentations;
  graphical->presentation_count = c->presentation_count;
  c->presentations = NULL;
  repoint_q3_clients(graphical);
  qa_caught_error failures[2];
  size_t failure_count = 0;
  if (!qa_try(publish_local_guest_snapshots_run, app, &failures[failure_count])) failure_count++;
  if (!qa_try(retired_close_run, &retired, &failures[failure_count])) failure_count++;
  if (failure_count == 1) qa_rethrow(&failures[0]);
  if (failure_count > 1) qa_raise("AggregateError", 0, (long long)failure_count, "Local player publication and retirement failed");
  char line[64];
  (void)snprintf(line, sizeof line, "Local players: %zu\n", c->player_count);
  qa_application_print(app, line);
}
static void staged_presentation_close(void *ctx) { qa_app_world_seat_presentation_close(((local_change *)ctx)->staged_presentation); }
static void staged_ui_close(void *ctx) { qa_app_seat_ui_close(((local_change *)ctx)->staged_ui); }
static void staged_native_close(void *ctx) { qa_app_application_effects_close(((local_change *)ctx)->staged_native.effects); }
static void change_discard(void *ctx) { qa_app_prepared_local_seat_change_discard(((local_change *)ctx)->change); }
void qa_application_change_local_players_seat(qa_application *app, const char *kind, const char *const *args, size_t count,
    const qa_seat_id *invoking_seat) {
  qa_application_graphical *graphical = app->graphical;
  if (graphical == NULL || app->world_operation != QA_APPLICATION_IDLE)
    qa_raise("Error", 0, 0, "Local player changes require an active graphical world");
  bool drop = is(kind, "drop");
  if (count > (drop ? 1u : 0u)) qa_raise("Error", 0, 0, "%s", drop ? "Usage: local_drop [player number]" : "Usage: local_join");
  size_t local_count = qa_app_application_input_local_count(graphical->input);
  bool has_selected = false;
  qa_seat_id selected;
  if (count == 0) {
    if (invoking_seat != NULL) { selected = *invoking_seat; has_selected = true; }
    else if (local_count != 0) {
      selected = qa_session_seat_id(qa_app_application_input_local(graphical->input, local_count - 1)->player.seat);
      has_selected = true;
    }
  } else {
    double number = qa_js_string_to_number(args[0]);
    for (size_t i = 0; i < local_count && !has_selected; i++) {
      qa_seat_id seat = qa_session_seat_id(qa_app_application_input_local(graphical->input, i)->player.seat);
      if ((double)seat.index + 1 == number) { selected = seat; has_selected = true; }
    }
  }
  if (drop && (!has_selected || local_count == 1)) qa_raise("Error", 0, 0, "Keep at least one local player in the world");
  qa_app_local_seat_request request;
  memset(&request, 0, sizeof request);
  if (!drop) request.kind = "join";
  else if (has_selected) { request.kind = "drop"; request.seat = selected; }
  else qa_raise("Error", 0, 0, "Unknown local player");
  local_change c;
  memset(&c, 0, sizeof c);
  c.app = app; c.graphical = graphical; c.kind = kind; c.args = args; c.arg_count = count; c.invoking_seat = invoking_seat;
  c.current = checked(calloc(local_count + 1, sizeof *c.current));
  for (size_t i = 0; i < local_count; i++) {
    qa_session_seat *seat = qa_app_application_input_local(graphical->input, i)->player.seat;
    c.current[i] = (qa_app_local_seat_identity){ qa_session_seat_client(seat), seat };
  }
  c.current_count = local_count;
  qa_app_local_seat_capacity capacity = { 4, app->world_simulation->options.max_clients };
  c.change = qa_app_prepare_local_seat_change(app->session, c.current, c.current_count, request, capacity);
  qa_caught_error caught;
  bool ok = qa_try(change_body, &c, &caught);
  if (ok) {
    if (c.prepared_input != NULL) qa_app_local_seat_preparation_destroy(c.prepared_input);
    qa_app_prepared_local_seat_change_destroy(c.change);
    local_change_scratch_free(&c);
    return;
  }
  if (c.committed) {
    app->fatal_world_failure = true;
    if (!qa_app_prepared_local_seat_change_published(c.change)) {
      size_t failure_count = 1;
      qa_caught_error cleanup;
      if (c.staged_presentation != NULL) {
        if (!qa_try(staged_presentation_close, &c, &cleanup)) failure_count++;
      } else {
        if (c.staged_ui != NULL && !qa_try(staged_ui_close, &c, &cleanup)) failure_count++;
        if (c.has_staged_native && !qa_try(staged_native_close, &c, &cleanup)) failure_count++;
      }
      if (failure_count > 1) {
        if (c.prepared_input != NULL) qa_app_local_seat_preparation_destroy(c.prepared_input);
        qa_app_prepared_local_seat_change_destroy(c.change);
        local_change_scratch_free(&c);
        qa_raise("AggregateError", 0, (long long)failure_count, "Local player publication cleanup failed");
      }
    }
    if (c.prepared_input != NULL) qa_app_local_seat_preparation_destroy(c.prepared_input);
    qa_app_prepared_local_seat_change_destroy(c.change);
    local_change_scratch_free(&c);
    qa_rethrow(&caught);
  }
  if (c.prepared_input != NULL) {
    qa_app_local_seat_preparation_discard(c.prepared_input);
    qa_app_local_seat_preparation_destroy(c.prepared_input);
  }
  qa_caught_error discard_error;
  bool discarded = qa_try(change_discard, &c, &discard_error);
  qa_app_prepared_local_seat_change_destroy(c.change);
  local_change_scratch_free(&c);
  if (!discarded) qa_raise("AggregateError", 0, 2, "Local player preparation cleanup failed");
  qa_rethrow(&caught);
}
void qa_application_change_local_players(qa_application *app, const char *kind, const char *const *args, size_t count,
    qa_seat_id invoking_seat) {
  qa_application_change_local_players_seat(app, kind, args, count, &invoking_seat);
}

/* ---- presentOutput (TS 7742-7917) ---- */
/* A list of presentation events: rows are shallow views of events another owner keeps for
 * the frame (owned rows are listed in present_scratch.owned). */
typedef struct event_list { qa_app_sim_presentation_event *items; size_t count, capacity; } event_list;
static void events_push(event_list *list, const qa_app_sim_presentation_event *event) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
    list->items = checked(realloc(list->items, list->capacity * sizeof *list->items));
  }
  list->items[list->count++] = *event;
}
static bool recipient_is(const qa_app_sim_presentation_event *event, qa_actor_id actor) {
  return event->has_recipient && qa_same_actor(event->recipient, actor);
}
/* The frame's storage, released when presentOutput returns or raises (TS GC). */
typedef struct present_scratch {
  qa_application *app;
  bool had_milliseconds; double previous_milliseconds;
  event_list owned;  /* owned copies (drained prints, native client events) */
  event_list *lists; size_t list_count, list_capacity;
  void **blocks; size_t block_count, block_capacity;
  qa_app_q3_source_presentation_state *native_q3, *selected_q3;
} present_scratch;
static event_list *scratch_list(present_scratch *s) {
  if (s->list_count == s->list_capacity) {
    s->list_capacity = s->list_capacity == 0 ? 16 : s->list_capacity * 2;
    s->lists = checked(realloc(s->lists, s->list_capacity * sizeof *s->lists));
  }
  event_list *list = &s->lists[s->list_count++];
  memset(list, 0, sizeof *list);
  return list;
}
static void *scratch_block(present_scratch *s, void *block) {
  if (s->block_count == s->block_capacity) {
    s->block_capacity = s->block_capacity == 0 ? 16 : s->block_capacity * 2;
    s->blocks = checked(realloc(s->blocks, s->block_capacity * sizeof *s->blocks));
  }
  s->blocks[s->block_count++] = block;
  return block;
}
/* Keeps an owned copy of each event for the frame and lists the copies in `into`. */
static void own_events(present_scratch *s, event_list *into, const qa_app_sim_presentation_event *events, size_t count) {
  for (size_t i = 0; i < count; i++) {
    qa_app_sim_presentation_event copy;
    if (!qa_app_sim_presentation_event_copy(&copy, &events[i])) qa_range_error("Out of memory");
    events_push(&s->owned, &copy);
    events_push(into, &copy);
  }
}
static void present_release(void *arg) {
  present_scratch *s = arg;
  /* finally { this.presentationMilliseconds = previousTime } */
  s->app->has_presentation_milliseconds = s->had_milliseconds;
  s->app->presentation_milliseconds = s->previous_milliseconds;
  for (size_t i = 0; i < s->owned.count; i++) qa_app_sim_presentation_event_release(&s->owned.items[i]);
  free(s->owned.items);
  for (size_t i = 0; i < s->list_count; i++) free(s->lists[i].items);
  free(s->lists);
  for (size_t i = 0; i < s->block_count; i++) free(s->blocks[i]);
  free(s->blocks);
  if (s->native_q3 != NULL) qa_app_q3_source_presentation_state_destroy(s->native_q3);
  if (s->selected_q3 != NULL) qa_app_q3_source_presentation_state_destroy(s->selected_q3);
}
/* eventsFor(actor, events): recipient undefined or the actor. */
static event_list *events_for(present_scratch *s, qa_actor_id actor, const event_list *events) {
  event_list *out = scratch_list(s);
  for (size_t i = 0; i < events->count; i++)
    if (!events->items[i].has_recipient || recipient_is(&events->items[i], actor)) events_push(out, &events->items[i]);
  return out;
}
static event_list *events_to(present_scratch *s, qa_actor_id actor, const event_list *events) {
  event_list *out = scratch_list(s);
  for (size_t i = 0; i < events->count; i++)
    if (recipient_is(&events->items[i], actor)) events_push(out, &events->items[i]);
  return out;
}
static event_list *without_audio_control(present_scratch *s, const qa_app_sim_presentation_event *events, size_t count) {
  event_list *out = scratch_list(s);
  for (size_t i = 0; i < count; i++)
    if (!qa_app_presentation_audio_control(&events[i])) events_push(out, &events[i]);
  return out;
}
typedef struct sound_list { qa_app_source_effect_sound *items; size_t count, capacity; } sound_list;
static void sounds_push(sound_list *list, const qa_app_source_effect_sound *sounds, size_t count) {
  if (list->count + count > list->capacity) {
    while (list->count + count > list->capacity) list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
    list->items = checked(realloc(list->items, list->capacity * sizeof *list->items));
  }
  if (count != 0) memcpy(list->items + list->count, sounds, count * sizeof *sounds);
  list->count += count;
}
/* Records drained from an effects owner: their arrays stay with the frame. */
static qa_app_recipient_effect_sounds *drain_recipient_sounds(present_scratch *s, qa_app_application_effects *effects, size_t *count) {
  qa_app_recipient_effect_sounds *rows = scratch_block(s, qa_app_application_effects_drain_recipient_sounds(effects, count));
  for (size_t i = 0; i < *count; i++) scratch_block(s, rows[i].sounds);
  return rows;
}
typedef struct present_measure { qa_app_world_seat_presentation *presentation; const qa_world_snapshot *snapshot;
  const qa_app_simulation_presentation *presentations; size_t presentation_count;
  const qa_q3_character_view *characters; size_t character_count;
  qa_session_seat *seat; qa_renderer_backend backend; } present_measure;
static void *present_prepare(void *ctx) {
  present_measure *m = ctx;
  qa_app_world_seat_presentation_prepare(m->presentation, m->snapshot, m->presentations, m->presentation_count, m->characters,
                                         m->character_count);
  return NULL;
}
static void *present_render(void *ctx) {
  present_measure *m = ctx;
  (void)qa_session_seat_present(m->seat, *m->snapshot, m->backend);
  return NULL;
}
static bool reported_gap(qa_application *app, const char *key) {
  for (size_t i = 0; i < app->reported_effect_gap_count; i++)
    if (strcmp(app->reported_effect_gaps[i], key) == 0) return true;
  if (app->reported_effect_gap_count == app->reported_effect_gap_capacity) {
    app->reported_effect_gap_capacity = app->reported_effect_gap_capacity == 0 ? 8 : app->reported_effect_gap_capacity * 2;
    app->reported_effect_gaps = checked(realloc(app->reported_effect_gaps, app->reported_effect_gap_capacity * sizeof *app->reported_effect_gaps));
  }
  app->reported_effect_gaps[app->reported_effect_gap_count++] = dup_text(key);
  return false;
}
/* TS Q2ServerEvent kind strings, in qa_q2_server_event_kind order. */
static const char *q2_server_event_kind(qa_q2_server_event_kind kind) {
  static const char *const names[] = {"nop", "disconnect", "reconnect", "level-restart", "server-data", "print",
      "center-print", "command-text", "layout", "achievement", "config-string", "baseline", "frame", "sound",
      "temporary-entity", "muzzle-flash", "inventory", "download", "setting", "seat", "damage", "localized-print",
      "fog", "poi", "help-path", "private"};
  return (size_t)kind < sizeof names / sizeof names[0] ? names[kind] : "unknown";
}
/* The native Q2 local services a seat's client leaves (TS for-of over receive()). */
static void native_local_services(qa_application *app, qa_application_graphical *graphical, qa_app_local_input *local,
    qa_application_native_q2_seat_client *native, const qa_q2_server_record *records, size_t count, double seconds) {
  qa_seat_id seat = qa_session_seat_id(local->player.seat);
  for (size_t i = 0; i < count; i++) {
    const qa_q2_server_event *event = &records[i].event;
    switch (event->kind) {
      case QA_Q2_SERVER_NOP: continue;
      case QA_Q2_SERVER_PRINT: qa_seat_console_print(local->console, event->text); break;
      case QA_Q2_SERVER_DISCONNECT:
        qa_seat_console_print(local->console, "Disconnected by the source game.\n");
        qa_application_request_quit(app);
        break;
      case QA_Q2_SERVER_LOCALIZED_PRINT: {
        const char *args[QA_Q2_KEX_LOCPRINT_ARGS_MAX];
        for (size_t a = 0; a < event->localized.arg_count; a++) args[a] = event->localized.args[a];
        char *text = qa_app_rerelease_localize_message(graphical->rerelease, seat, qa_app_native_q2_client_content(native->client),
                                                       event->localized.base, args, event->localized.arg_count);
        qa_cleanup text_cleanup;
        qa_cleanup_push(&text_cleanup, free, text);
        qa_app_native_q2_client_print(native->client, event->localized.flags, text, seconds);
        qa_cleanup_pop(&text_cleanup);
        free(text);
        break;
      }
      case QA_Q2_SERVER_COMMAND_TEXT: {
        qa_command_origin caller;
        memset(&caller, 0, sizeof caller);
        caller.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
        caller.seat = seat;
        caller.client = qa_session_client_id(qa_session_seat_client(local->player.seat));
        qa_command_context source;
        memset(&source, 0, sizeof source);
        source.session = qa_engine_session_id(app->session);
        source.origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
        source.origin.name = "q2-game";
        source.origin.caller = &caller;
        qa_app_application_input_enqueue_client_command(graphical->input, event->text, &source);
        break;
      }
      default:
        qa_raise("Error", 0, 0, "Unsupported native Q2 local service %s", q2_server_event_kind(event->kind));
    }
  }
}
typedef struct received_records { qa_q2_server_record *records; size_t count; } received_records;
static void received_records_free(void *arg) {
  received_records *r = arg;
  qa_q2_server_records_free(r->records, r->count);
  r->records = NULL;
  r->count = 0;
}
void qa_application_present_output(qa_application *app, qa_simulation_output output, const qa_application_present_frame *frame) {
  present_scratch s;
  memset(&s, 0, sizeof s);
  s.app = app;
  double presentation_frame = ++app->presentation_frames;
  s.had_milliseconds = app->has_presentation_milliseconds;
  s.previous_milliseconds = app->presentation_milliseconds;
  app->has_presentation_milliseconds = true;
  app->presentation_milliseconds = frame->time_milliseconds;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, present_release, &s);
  qa_application_graphical *graphical = app->graphical;
  if (graphical != NULL) {
    qa_app_shared_simulation *simulation = qa_application_simulation(app);
    const qa_world_snapshot *snapshot = &output.snapshot;
    qa_command_dialect dialect = qa_application_source_dialect(app, NULL);
    if (dialect == QA_COMMAND_DIALECT_Q2_CLASSIC || dialect == QA_COMMAND_DIALECT_Q2_RERELEASE)
      qa_source_debug_graph_add_frame(app->debug_graph, frame->frame_milliseconds / 1000, qa_application_debug_graph_settings(app));
    qa_app_shared_sim_begin_presentation_frame(simulation, presentation_frame);
    size_t presentation_count = 0, character_count = 0;
    const qa_app_simulation_presentation *presentations = qa_app_shared_sim_presentations(simulation, &presentation_count);
    const qa_q3_character_view *characters = qa_app_shared_sim_character_views(simulation, &character_count);
    event_list *source_events = scratch_list(&s), *round_events = scratch_list(&s);
    for (size_t i = 0; i < frame->source_event_count; i++) events_push(source_events, frame->source_events[i]);
    for (size_t i = 0; i < frame->round_event_count; i++) events_push(round_events, frame->round_events[i]);
    qa_app_rerelease_receive(graphical->rerelease, source_events->items, source_events->count);
    qa_app_rerelease_prepare(graphical->rerelease);
    event_list *presentation_events = scratch_list(&s);
    for (size_t i = 0; i < round_events->count; i++) events_push(presentation_events, &round_events->items[i]);
    for (size_t i = 0; i < source_events->count; i++) {
      const qa_app_sim_presentation_event *event = &source_events->items[i];
      if (is(event->kind, "q2-composition") && (event->q2_composition->kind == QA_Q2_COMPOSITION_EVENT_KICK ||
                                                event->q2_composition->kind == QA_Q2_COMPOSITION_EVENT_GRAPPLE_PREDICTION))
        continue;
      events_push(presentation_events, event);
    }
    size_t print_count = 0;
    const qa_app_sim_presentation_event *prints = qa_app_rerelease_drain_prints(graphical->rerelease, &print_count);
    own_events(&s, presentation_events, prints, print_count);
    /* nativeQ3 = this.simulation.q3Source()?.sourceState() */
    qa_app_q3_source_runtime *q3_source = qa_app_shared_sim_q3_source(simulation);
    if (q3_source != NULL) {
      s.native_q3 = qa_app_q3_source_runtime_source_state(q3_source);
      if (s.native_q3 == NULL) qa_range_error("Out of memory");
    }
    for (size_t i = 0; i < graphical->q3_count; i++) {
      qa_application_q3_seat_client *source = &graphical->q3[i];
      if (source->qvm) continue;
      if (s.native_q3 == NULL) qa_raise("Error", 0, 0, "Cgame has no authoritative source state");
      qa_app_simulation_prediction_host_capture_source(source->prediction, s.native_q3);
      event_list *received = events_for(&s, source->actor, source_events);
      qa_app_application_q3_client_receive(source->client, s.native_q3, received->items, received->count, frame->local_commands,
                                           frame->local_command_count);
    }
    qa_app_shared_sim_q3_source_state selected_state;
    bool has_selected = qa_app_shared_sim_selected_q3_source_state(simulation, &selected_state);
    if (has_selected) s.selected_q3 = selected_state.state;
    qa_app_selected_q3_source selected_q3 = {NULL, NULL, NULL};
    if (has_selected) {
      selected_q3.content = selected_state.content;
      selected_q3.source = selected_state.source;
      selected_q3.state = selected_state.state;
    }
    /* sharedPresentationEvents, effectEvents, commonEvents */
    event_list *shared_events = scratch_list(&s);
    for (size_t i = 0; i < presentation_events->count; i++) {
      const qa_app_sim_presentation_event *event = &presentation_events->items[i];
      if (has_selected && strcmp(event->content, selected_q3.content) == 0 && is(event->kind, "q3-source") &&
          event->q3_source->kind == QA_APP_Q3_EVENT_ENTITY)
        continue;
      events_push(shared_events, event);
    }
    event_list *effect_events = shared_events;
    if (graphical->q3_count != 0) {
      effect_events = scratch_list(&s);
      for (size_t i = 0; i < shared_events->count; i++) {
        const qa_app_sim_presentation_event *event = &shared_events->items[i];
        bool keep = is(event->kind, "q3-source") ? event->q3_source->kind == QA_APP_Q3_EVENT_SOUND : !is(event->kind, "q3-character");
        if (keep) events_push(effect_events, event);
      }
    }
    event_list *common_events = scratch_list(&s);
    for (size_t i = 0; i < effect_events->count; i++)
      if (!effect_events->items[i].has_recipient) events_push(common_events, &effect_events->items[i]);
    /* nativeEvents (per seat, presentation order), seatAudio, unhandled */
    event_list **native_events = scratch_block(&s, checked(calloc(graphical->presentation_count + 1, sizeof *native_events)));
    qa_app_audio_seat_events *seat_audio = scratch_block(&s, checked(calloc(graphical->presentation_count + 1, sizeof *seat_audio)));
    size_t seat_audio_count = 0;
    qa_app_unhandled_effect *unhandled = NULL;
    size_t unhandled_count = 0;
    if (graphical->native_q2_count == 0) {
      qa_app_application_effects_receive(graphical->effects, effect_events->items, effect_events->count);
      qa_app_application_effects_prepare(graphical->effects, snapshot, presentations, presentation_count, characters, character_count);
      unhandled = qa_app_application_effects_drain_unhandled(graphical->effects, &unhandled_count);
      size_t sound_count = 0;
      qa_app_source_effect_sound *sounds = scratch_block(&s, qa_app_application_effects_drain_sounds(graphical->effects, &sound_count));
      qa_app_application_audio_receive_effect_sounds(graphical->audio, sounds, sound_count);
      size_t private_count = 0;
      qa_app_recipient_effect_sounds *private_sounds = drain_recipient_sounds(&s, graphical->effects, &private_count);
      for (size_t p = 0; p < graphical->presentation_count; p++) {
        qa_app_local_input *local = graphical->presentations[p].local;
        qa_actor_id actor = local->player.actor;
        event_list *events = events_to(&s, actor, effect_events);
        sound_list *seat_sounds = scratch_block(&s, checked(calloc(1, sizeof *seat_sounds)));
        for (size_t b = 0; b < private_count; b++)
          if (qa_same_actor(private_sounds[b].recipient, actor)) sounds_push(seat_sounds, private_sounds[b].sounds, private_sounds[b].sound_count);
        scratch_block(&s, seat_sounds->items);
        if (events->count != 0 || seat_sounds->count != 0) {
          qa_app_audio_seat_events *batch = &seat_audio[seat_audio_count++];
          batch->seat = qa_session_seat_id(local->player.seat);
          batch->snapshot = snapshot;
          batch->events = events->items;
          batch->event_count = events->count;
          batch->music = p == 0;
          batch->effect_sounds = seat_sounds->items;
          batch->effect_sound_count = seat_sounds->count;
        }
      }
    } else {
      qa_application_native_server_host *host = app->recording_host;
      if (host == NULL || host->kind != QA_APPLICATION_NETWORK_Q2 || host->q2.raw_messages == NULL)
        qa_raise("Error", 0, 0, "Native Q2 presentation lost its source message owner");
      double seconds = frame->time_milliseconds / 1000;
      for (size_t p = 0; p < graphical->presentation_count; p++) {
        qa_app_local_input *local = graphical->presentations[p].local;
        qa_seat_id seat = qa_session_seat_id(local->player.seat);
        qa_application_native_q2_seat_client *native = NULL;
        for (size_t n = 0; n < graphical->native_q2_count && native == NULL; n++)
          if (qa_same_seat(graphical->native_q2_seats[n], seat)) native = &graphical->native_q2[n];
        if (native == NULL) qa_raise("Error", 0, 0, "Native Q2 presentation has no recipient state");
        qa_app_q2_player player = host->q2.carried_player(host->q2.ctx, qa_session_client_id(qa_session_seat_client(local->player.seat)));
        size_t message_count = 0;
        const qa_app_q2_raw_message *messages = NULL;
        if (host->q2.local_messages != NULL) messages = host->q2.local_messages(host->q2.ctx, player, &message_count);
        if (messages == NULL && host->q2.source_messages != NULL) messages = host->q2.source_messages(host->q2.ctx, player, &message_count);
        if (messages == NULL) messages = host->q2.raw_messages(host->q2.ctx, player, &message_count);
        qa_app_native_q2_message *rows = scratch_block(&s, checked(calloc(message_count + 1, sizeof *rows)));
        for (size_t m = 0; m < message_count; m++) {
          rows[m].bytes = messages[m].bytes;
          rows[m].length = messages[m].length;
        }
        qa_q2_server_record *records = NULL;
        size_t record_count = qa_app_native_q2_client_receive(native->client, rows, message_count, seconds, &records);
        received_records owned_records = {records, record_count};
        qa_cleanup records_cleanup;
        qa_cleanup_push(&records_cleanup, received_records_free, &owned_records);
        native_local_services(app, graphical, local, native, records, record_count, seconds);
        qa_cleanup_pop(&records_cleanup);
        received_records_free(&owned_records);
        qa_app_sim_presentation_event *taken = NULL;
        size_t taken_count = qa_app_native_q2_client_take_events(native->client, &taken);
        event_list *events = scratch_list(&s);
        for (size_t e = 0; e < taken_count; e++) {
          events_push(&s.owned, &taken[e]);
          events_push(events, &taken[e]);
        }
        free(taken);
        qa_app_rerelease_receive(graphical->rerelease, events->items, events->count);
        qa_app_rerelease_prepare(graphical->rerelease);
        size_t seat_print_count = 0;
        const qa_app_sim_presentation_event *seat_prints = qa_app_rerelease_drain_prints(graphical->rerelease, &seat_print_count);
        own_events(&s, events, seat_prints, seat_print_count);
        for (size_t e = 0; e < events->count; e++) qa_application_record_player_progress(app, &events->items[e]);
        native_events[p] = events;
        qa_actor_id actor = local->player.actor;
        event_list *private_events = events_to(&s, actor, effect_events);
        event_list *received = events_for(&s, actor, effect_events);
        for (size_t e = 0; e < events->count; e++) events_push(received, &events->items[e]);
        qa_app_application_effects_receive(native->effects, received->items, received->count);
        qa_app_application_effects_prepare(native->effects, snapshot, presentations, presentation_count, characters, character_count);
        size_t drained_count = 0;
        qa_app_unhandled_effect *drained = qa_app_application_effects_drain_unhandled(native->effects, &drained_count);
        if (drained_count != 0) {
          unhandled = checked(realloc(unhandled, (unhandled_count + drained_count) * sizeof *unhandled));
          memcpy(unhandled + unhandled_count, drained, drained_count * sizeof *drained);
          unhandled_count += drained_count;
        }
        free(drained);
        event_list *seat_events = scratch_list(&s);
        for (size_t e = 0; e < private_events->count; e++) events_push(seat_events, &private_events->items[e]);
        for (size_t e = 0; e < events->count; e++) events_push(seat_events, &events->items[e]);
        sound_list *seat_sounds = scratch_block(&s, checked(calloc(1, sizeof *seat_sounds)));
        size_t sound_count = 0;
        qa_app_source_effect_sound *sounds = scratch_block(&s, qa_app_application_effects_drain_sounds(native->effects, &sound_count));
        sounds_push(seat_sounds, sounds, sound_count);
        size_t private_count = 0;
        qa_app_recipient_effect_sounds *private_sounds = drain_recipient_sounds(&s, native->effects, &private_count);
        for (size_t b = 0; b < private_count; b++) sounds_push(seat_sounds, private_sounds[b].sounds, private_sounds[b].sound_count);
        scratch_block(&s, seat_sounds->items);
        qa_app_audio_seat_events *batch = &seat_audio[seat_audio_count++];
        batch->seat = seat;
        batch->snapshot = snapshot;
        batch->events = seat_events->items;
        batch->event_count = seat_events->count;
        batch->music = false;
        batch->effect_sounds = seat_sounds->items;
        batch->effect_sound_count = seat_sounds->count;
      }
    }
    /* this.unhandledEffects = unhandled */
    qa_app_unhandled_effects_free(app->unhandled_effects, app->unhandled_effect_count);
    app->unhandled_effects = unhandled;
    app->unhandled_effect_count = unhandled_count;
    for (size_t i = 0; i < unhandled_count; i++) {
      const qa_app_unhandled_effect *effect = &unhandled[i];
      size_t length = strlen(effect->source.content) + strlen(effect->reason) + 2;
      char *key = checked(malloc(length));
      snprintf(key, length, "%s:%s", effect->source.content, effect->reason);
      bool reported = reported_gap(app, key);
      free(key);
      if (!reported) {
        size_t text_length = strlen(effect->source.kind) + strlen(effect->reason) + 32;
        char *text = checked(malloc(text_length));
        snprintf(text, text_length, "Unresolved %s effect: %s\n", effect->source.kind, effect->reason);
        qa_cleanup text_cleanup;
        qa_cleanup_push(&text_cleanup, free, text);
        qa_application_print(app, text);
        qa_cleanup_pop(&text_cleanup);
        free(text);
      }
    }
    qa_app_component_media_host media = qa_app_component_media_host_of(simulation, graphical->audio, graphical->assets);
    const char *media_error = qa_app_prepare_presentation_audio(media, common_events->items, common_events->count, seat_audio, seat_audio_count);
    if (media_error != NULL) qa_raise("Error", 0, 0, "%s", media_error);
    media_error = qa_app_prepare_presentation_shaders(media);
    if (media_error != NULL) qa_raise("Error", 0, 0, "%s", media_error);
    for (size_t p = 0; p < graphical->presentation_count; p++) {
      qa_application_seat_presentation *presentation = &graphical->presentations[p];
      event_list *events = events_for(&s, presentation->local->player.actor, presentation_events);
      if (native_events[p] != NULL)
        for (size_t e = 0; e < native_events[p]->count; e++) events_push(events, &native_events[p]->items[e]);
      qa_app_world_seat_presentation_source_events(presentation->presentation, events->items, events->count);
      present_measure m = {presentation->presentation, snapshot, presentations, presentation_count, characters, character_count, NULL, {0}};
      if (app->tools == NULL) present_prepare(&m);
      else (void)qa_app_application_tools_measure(app->tools, "presentation", present_prepare, &m);
    }
    qa_app_viewing_seat *seats = scratch_block(&s, checked(calloc(graphical->presentation_count + 1, sizeof *seats)));
    for (size_t p = 0; p < graphical->presentation_count; p++)
      seats[p] = qa_app_world_seat_presentation_viewing_seat(graphical->presentations[p].presentation);
    size_t source_count = 0;
    const qa_active_mod_presentation *sources = qa_app_shared_sim_mod_presentation_sources(simulation, &source_count);
    const qa_active_mod_presentation **source_pointers = scratch_block(&s, checked(calloc(source_count + 1, sizeof *source_pointers)));
    for (size_t i = 0; i < source_count; i++) source_pointers[i] = &sources[i];
    qa_app_application_mod_presentations_prepare(graphical->mod_presentations, seats, graphical->presentation_count, source_pointers,
                                                 source_count, presentation_events->items, presentation_events->count, presentation_frame);
    qa_app_selected_q3_presentations_prepare(graphical->selected_q3_presentations, seats, graphical->presentation_count,
                                             has_selected ? &selected_q3 : NULL, presentation_events->items, presentation_events->count,
                                             presentation_frame);
    qa_renderer_backend backend = qa_app_native_renderer_backend(graphical->renderer);
    for (size_t p = 0; p < graphical->presentation_count; p++) {
      present_measure m;
      memset(&m, 0, sizeof m);
      m.snapshot = snapshot;
      m.seat = graphical->presentations[p].local->player.seat;
      m.backend = backend;
      if (app->tools == NULL) present_render(&m);
      else (void)qa_app_application_tools_measure(app->tools, "render", present_render, &m);
    }
    qa_render_command swap;
    memset(&swap, 0, sizeof swap);
    swap.kind = QA_RENDER_COMMAND_SWAP_BUFFERS;
    qa_render_frame render = {qa_app_application_assets_images(graphical->assets)->owner, presentation_frame, &swap, 1};
    qa_app_native_renderer_execute(graphical->renderer, render);
    qa_audio_listener *listeners = scratch_block(&s, checked(calloc(graphical->presentation_count + 1, sizeof *listeners)));
    for (size_t p = 0; p < graphical->presentation_count; p++) {
      qa_application_seat_presentation *presentation = &graphical->presentations[p];
      qa_scene_camera camera = qa_app_world_seat_presentation_camera(presentation->presentation);
      listeners[p].seat = qa_session_seat_id(presentation->local->player.seat);
      listeners[p].has_actor = true;
      listeners[p].actor = presentation->local->player.actor;
      listeners[p].origin = camera.origin;
      listeners[p].axis = camera.axis;
      listeners[p].gain = 1 / (double)graphical->presentation_count;
      listeners[p].underwater = qa_application_underwater(app, camera, presentation->local->player.actor);
    }
    event_list *audible = without_audio_control(&s, common_events->items, common_events->count);
    qa_app_audio_seat_events *audible_seats = scratch_block(&s, checked(calloc(seat_audio_count + 1, sizeof *audible_seats)));
    for (size_t b = 0; b < seat_audio_count; b++) {
      audible_seats[b] = seat_audio[b];
      event_list *events = without_audio_control(&s, seat_audio[b].events, seat_audio[b].event_count);
      audible_seats[b].events = events->items;
      audible_seats[b].event_count = events->count;
    }
    qa_app_application_audio_frame(graphical->audio, snapshot, listeners, graphical->presentation_count, audible->items, audible->count,
                                   frame->frame_started_at, audible_seats, seat_audio_count);
  }
  qa_cleanup_pop(&cleanup);
  present_release(&s);
}
