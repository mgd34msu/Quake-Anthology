/* Port of src/app/bootstrap/remote-application.ts, the local remote seats
 * region (TS 3255-3904 in the 2026-11-03 tree). Status per member:
 * docs/remote_application_layout.md. GPL-2.0-or-later. */
#include <stdlib.h>
#include <string.h>
#include "remote_application_internal.h"
#include <stdio.h>
#include "qa/app/bootstrap/server_map.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/core/numeric.h"
#include "qa/network/common/js_number.h"
#include "qa/network/q3/admission.h"
#include "qa/foundation/support.h"

void qa_remote_application_drain_seat_publications(qa_remote_application *app) {
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    qa_remote_seat_peer *peer = app->remote_seats[i];
    qa_app_remote_seat_pump_drain_ready(peer->pump);
    qa_app_remote_seat_pump_throw_failure(peer->pump);
  }
}
/* remote.player?.actor.equals(actor) || (remote instanceof Q3RemotePresentation && remote.admittedPlayer.actor.equals(actor)) */
static bool drives(const qa_remote_presentation *remote, qa_actor_id actor) {
  qa_actor_id player;
  if (qa_app_remote_seat_presentation_player(remote, &player) && qa_same_actor(player, actor)) return true;
  return remote->family == QA_APP_REMOTE_FAMILY_Q3
      && qa_same_actor(qa_app_q3_remote_admitted_player(remote->q3).actor, actor);
}
const qa_remote_presentation *qa_remote_application_source_for_actor(qa_remote_application *app, qa_actor_id actor) {
  const qa_remote_presentation *remote = qa_remote_application_remote(app);
  if (drives(remote, actor)) return remote;
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    qa_remote_seat_peer *peer = app->remote_seats[i];
    if (peer->closed) continue;
    const qa_remote_presentation *candidate = qa_app_remote_seat_source_remote(peer->source);
    if (drives(candidate, actor)) return candidate;
  }
  qa_raise("Error", 0, 0, "Remote input actor has no active native channel");
}
qa_rect qa_remote_application_seat_viewport(qa_remote_application *app, qa_seat_id id) {
  size_t count = app->controls != NULL ? qa_app_application_input_local_count(app->controls) : 1;
  double index = -1;
  for (size_t i = 0; i < count && index < 0; i++) {
    qa_seat_id candidate = app->controls != NULL
        ? qa_session_seat_id(qa_app_application_input_local(app->controls, i)->player.seat)
        : qa_remote_application_seat_id(app);
    if (qa_same_seat(candidate, id)) index = (double)i;
  }
  qa_sdl_size size = qa_sdl_window_drawable_size(qa_remote_application_window(app));
  return qa_app_seat_viewport(index, (double)count, size.width, size.height);
}
void qa_remote_application_publish_remote_layouts(qa_remote_application *app) {
  size_t count = app->controls != NULL ? qa_app_application_input_local_count(app->controls) : 0;
  for (size_t i = 0; i < count; i++) {
    qa_seat_id seat = qa_session_seat_id(qa_app_application_input_local(app->controls, i)->player.seat);
    qa_app_world_seat_presentation *presentation = NULL;
    if (qa_same_seat(seat, qa_remote_application_seat_id(app))) presentation = qa_remote_application_presentation(app);
    else
      for (size_t j = 0; j < app->remote_seat_count && presentation == NULL; j++) {
        qa_remote_seat_peer *peer = app->remote_seats[j];
        if (!peer->closed && qa_same_seat(qa_session_seat_id(peer->seat), seat) && peer->view != NULL)
          presentation = qa_app_remote_seat_view_presentation(peer->view);
      }
    if (presentation != NULL) qa_app_world_seat_presentation_publish_layout(presentation, (double)i, (double)count);
  }
}
bool qa_remote_application_peer_world_current(qa_remote_application *app, const qa_remote_seat_peer *peer) {
  const char *map = qa_remote_application_options(app)->map;
  bool same_map = (peer->map == NULL && map == NULL) || (peer->map != NULL && map != NULL && strcmp(peer->map, map) == 0);
  if (!same_map || peer->loaded_content != app->loaded_content) return false;
  const qa_remote_presentation *remote = qa_app_remote_seat_source_remote(peer->source);
  if (remote->family != QA_APP_REMOTE_FAMILY_UNIFIED) return true;
  const qa_remote_presentation *primary = qa_remote_application_remote(app);
  return primary->family == QA_APP_REMOTE_FAMILY_UNIFIED
      && qa_app_unified_remote_epoch(remote->unified) == qa_app_unified_remote_epoch(primary->unified);
}
void qa_remote_application_poll_remote_seats(qa_remote_application *app, double now) {
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    qa_remote_seat_peer *peer = app->remote_seats[i];
    if (!peer->closed && peer->disconnected == NULL) qa_app_remote_seat_pump_poll(peer->pump, now);
  }
}

/* ---- shared helpers ---------------------------------------------------- */
static void *seats_xcalloc(size_t count, size_t size) {
  void *memory = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (memory == NULL) qa_range_error("Out of memory");
  return memory;
}
static qa_app_local_input *local_for(qa_app_application_input *controls, qa_seat_id id) {
  size_t count = qa_app_application_input_local_count(controls);
  for (size_t i = 0; i < count; i++) {
    qa_app_local_input *local = qa_app_application_input_local(controls, i);
    if (qa_same_seat(qa_session_seat_id(local->player.seat), id)) return local;
  }
  return NULL;
}
static void remove_qport(qa_remote_application *app, uint16_t port) {
  size_t kept = 0;
  for (size_t i = 0; i < app->qport_count; i++) if (app->qports[i] != port) app->qports[kept++] = app->qports[i];
  app->qport_count = kept;
}

/* ---- retireRemoteSeat (TS 3813-3848) ----------------------------------- */
typedef struct retire_seat { qa_remote_application *app; qa_remote_seat_peer *peer; } retire_seat;
static void retire_view(void *ctx) {
  retire_seat *r = ctx;
  if (r->peer->view != NULL) qa_app_remote_seat_view_close(r->peer->view);
}
static void retire_pump(void *ctx) { qa_app_remote_seat_pump_close(((retire_seat *)ctx)->peer->pump); }
static void retire_channel(void *ctx) { qa_app_remote_seat_channel_close(((retire_seat *)ctx)->peer->channel); }
static void retire_content(void *ctx) {
  retire_seat *r = ctx;
  if (r->peer->content != NULL) qa_app_remote_content_mounts_close(r->peer->content);
}
void qa_remote_application_retire_remote_seat(qa_remote_application *app, qa_remote_seat_peer *peer, bool remove_input) {
  if (peer->closed) return;
  if (remove_input) {
    qa_remote_application_drop_remote_seat(app, qa_session_seat_id(peer->seat).index);
    return;
  }
  peer->closed = true;
  retire_seat r = {app, peer};
  bool failed = false;
  qa_caught_error caught;
  if (!qa_try(retire_view, &r, &caught)) failed = true;
  if (peer->view != NULL) qa_remote_arena_add(&app->arena, (void (*)(void *))qa_app_remote_seat_view_destroy, peer->view);
  peer->view = NULL;
  if (!qa_try(retire_pump, &r, &caught)) failed = true;
  if (!qa_try(retire_channel, &r, &caught)) failed = true;
  if (!qa_try(retire_content, &r, &caught)) failed = true;
  peer->content = NULL;
  peer->loaded_content = NULL;
  remove_qport(app, (uint16_t)qa_app_remote_seat_source_qport(peer->source));
  size_t kept = 0;
  for (size_t i = 0; i < app->remote_seat_count; i++) if (app->remote_seats[i] != peer) app->remote_seats[kept++] = app->remote_seats[i];
  app->remote_seat_count = kept;
  if (failed) qa_raise("AggregateError", 0, 0, "Remote seat retirement failed");
}

/* ---- retireRemoteViews (TS 3850-3861) ---------------------------------- */
typedef struct retire_views { qa_remote_seat_peer *peer; } retire_views;
static void retire_one_view(void *ctx) {
  qa_remote_seat_peer *peer = ((retire_views *)ctx)->peer;
  if (peer->view != NULL) qa_app_remote_seat_view_close(peer->view);
}
void qa_remote_application_retire_remote_views(qa_remote_application *app) {
  bool failed = false;
  qa_caught_error caught;
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    qa_remote_seat_peer *peer = app->remote_seats[i];
    retire_views r = {peer};
    if (!qa_try(retire_one_view, &r, &caught)) failed = true;
    if (peer->view != NULL) qa_remote_arena_add(&app->arena, (void (*)(void *))qa_app_remote_seat_view_destroy, peer->view);
    peer->view = NULL;
  }
  if (failed) qa_raise("AggregateError", 0, 0, "Remote seat views failed to retire");
}

/* ---- bindRemoteSeat (TS 3714-3807) ------------------------------------- */
typedef struct remote_seat_bind {
  qa_remote_application *app; qa_remote_seat_peer *peer; qa_q3_client_connection *connection;
  qa_app_application_input *controls; qa_remote_world_frontend *frontend; double generation;
  qa_app_local_input *local; qa_app_remote_seat_view_q3 q3; qa_app_remote_seat_view *view;
} remote_seat_bind;
static void bind_assert(void *ctx) {
  remote_seat_bind *b = ctx;
  qa_remote_application *app = b->app;
  if (b->peer->closed || app->closed || app->closing || b->generation != app->world_load_generation || app->controls != b->controls
      || app->frontend != b->frontend)
    qa_raise("Error", 0, 0, "Remote seat presentation was replaced");
}
static qa_rect bind_viewport(void *ctx) {
  remote_seat_bind *b = ctx;
  return qa_remote_application_seat_viewport(b->app, qa_session_seat_id(b->peer->seat));
}
static double bind_count(void *ctx) { return (double)qa_app_application_input_local_count(((remote_seat_bind *)ctx)->controls); }
static double bind_index(void *ctx) {
  remote_seat_bind *b = ctx;
  size_t count = qa_app_application_input_local_count(b->controls);
  for (size_t i = 0; i < count; i++) if (qa_app_application_input_local(b->controls, i) == b->local) return (double)i;
  return -1;
}
static double bind_now(void *ctx) { (void)ctx; return qa_app_remote_access_now_milliseconds(NULL); }
static void bind_quit(void *ctx) { qa_remote_application_request_quit(((remote_seat_bind *)ctx)->app); }
static void bind_execute(void *ctx, const char *name, const char *const *args, size_t arg_count) {
  remote_seat_bind *b = ctx;
  qa_seat_id seat = qa_session_seat_id(b->peer->seat);
  qa_remote_application_queue_command(b->app, name, args, arg_count, &seat, NULL);
}
static void bind_print(void *ctx, const char *text) { qa_remote_application_print(((remote_seat_bind *)ctx)->app, text, NULL); }
static int compare_players(const qa_app_local_player *a, const qa_app_local_player *b, qa_seat_id primary) {
  qa_seat_id left = qa_session_seat_id(a->seat), right = qa_session_seat_id(b->seat);
  if (qa_same_seat(left, primary)) return -1;
  if (qa_same_seat(right, primary)) return 1;
  return left.index < right.index ? -1 : left.index > right.index ? 1 : 0;
}
static void sort_players(qa_app_local_player *players, size_t count, qa_seat_id primary) {
  for (size_t i = 1; i < count; i++) {
    qa_app_local_player moving = players[i];
    size_t at = i;
    while (at > 0 && compare_players(&players[at - 1], &moving, primary) > 0) { players[at] = players[at - 1]; at--; }
    players[at] = moving;
  }
}
static void bind_body(void *ctx) {
  remote_seat_bind *b = ctx;
  qa_remote_application *app = b->app;
  qa_remote_seat_peer *peer = b->peer;
  const qa_remote_presentation *remote = qa_app_remote_seat_source_remote(peer->source);
  qa_actor_id player;
  bool has_player = qa_remote_presentation_admitted_player(remote, b->connection != NULL, &player);
  if (!has_player || (b->connection == NULL && qa_app_remote_seat_presentation_output(remote) == NULL)) return;
  b->generation = app->world_load_generation;
  size_t local_count = qa_app_application_input_local_count(b->controls);
  qa_app_local_player *players = seats_xcalloc(local_count + 1, sizeof *players);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, players);
  size_t count = 0;
  for (size_t i = 0; i < local_count; i++) {
    qa_app_local_input *local = qa_app_application_input_local(b->controls, i);
    if (!qa_same_seat(qa_session_seat_id(local->player.seat), qa_session_seat_id(peer->seat))) players[count++] = local->player;
  }
  players[count].seat = peer->seat;
  players[count].actor = player;
  count++;
  sort_players(players, count, qa_remote_application_seat_id(app));
  qa_session_seat **seats = seats_xcalloc(count, sizeof *seats);
  for (size_t i = 0; i < count; i++) seats[i] = players[i].seat;
  qa_app_local_seat_preparation *prepared = qa_app_application_input_prepare_local_seats(b->controls, seats, count);
  free(seats);
  bind_assert(b);
  qa_app_local_seat_preparation_publish(prepared, players, count, app->ownership.borrowed ? app->ownership.client : NULL,
      qa_remote_application_source_access(app), true);
  qa_app_local_seat_preparation_destroy(prepared);
  qa_cleanup_pop(&guard);
  free(players);
  b->local = local_for(b->controls, qa_session_seat_id(peer->seat));
  if (b->local == NULL) qa_raise("Error", 0, 0, "Remote input publication lost its admitted seat");
  qa_cvar_registry *cvars = qa_remote_configuration_seat_source(app->configuration, qa_session_seat_id(peer->seat));
  if (cvars == NULL) qa_raise("Error", 0, 0, "Remote seat presentation lost its registry");
  const qa_app_remote_seat_view_q3 *q3 = NULL;
  if (b->connection != NULL) {
    if (app->q3_browser == NULL || app->key_profile == NULL) qa_raise("Error", 0, 0, "Q3 remote seat has no guest services");
    b->q3 = (qa_app_remote_seat_view_q3){b->connection, app->q3_browser, app->key_profile};
    q3 = &b->q3;
  }
  qa_app_remote_seat_view_options options;
  memset(&options, 0, sizeof options);
  options.local = b->local;
  options.controls = b->controls;
  options.source = peer->source;
  options.cvars = cvars;
  options.assets = b->frontend->assets;
  options.content = peer->loaded_content;
  options.art = b->frontend->art;
  options.font = b->frontend->font;
  options.audio = b->frontend->audio;
  options.renderer = app->renderer;
  options.images = app->image_settings;
  options.view = app->view_settings;
  options.options = &app->launch_options;
  options.q3 = q3;
  qa_remote_component_cinematics cinematics = qa_remote_application_component_cinematics(app, b->frontend->audio);
  options.system_cinematics = cinematics.host;
  options.system_cinematics_ctx = cinematics.ctx;
  options.ctx = b;
  options.viewport = bind_viewport;
  options.count = bind_count;
  options.index = bind_index;
  options.now = bind_now;
  options.assert_current = bind_assert;
  options.quit = bind_quit;
  options.execute = bind_execute;
  options.print = bind_print;
  b->view = qa_app_remote_seat_view_prepare(&options);
  if (b->view == NULL) qa_range_error("Out of memory");
  bind_assert(b);
  qa_app_remote_seat_view_publish(b->view, bind_index(b), bind_count(b));
  peer->view = b->view;
  b->view = NULL;
  double fov = 0;
  qa_app_application_q3_client *client = qa_remote_presentation_q3_client(qa_app_remote_seat_view_presentation(peer->view));
  if (client != NULL && qa_app_view_settings_override(app->view_settings, &fov)) {
    char text[40];
    qa_net_js_number(qa_app_view_settings_field_of_view(app->view_settings), text, sizeof text);
    qa_remote_cvar_set(qa_app_application_q3_client_cvars(client), "cg_fov", text, false);
  }
  qa_remote_application_publish_remote_layouts(app);
  if (b->connection != NULL) {
    char *info = NULL;
    if (!qa_q3_client_game_state_storage_get(qa_q3_client_connection_game_state(b->connection), 1, &info) || info == NULL)
      info = strdup("");
    if (info == NULL) qa_range_error("Out of memory");
    char *server = qa_q3_info_value(info, "sv_serverid");
    free(info);
    if (server == NULL) qa_range_error("Out of memory");
    char *pure = qa_app_remote_seat_source_pure_command(peer->source, qa_native_atoi(server));
    free(server);
    if (pure != NULL) {
      qa_q3_reliable_command added;
      bool ok = qa_q3_client_reliable_commands_add(qa_q3_client_connection_reliable(b->connection), pure, &added);
      free(pure);
      if (!ok) qa_range_error("Out of memory");
    }
  }
}
static void bind_close_view(void *ctx) {
  remote_seat_bind *b = ctx;
  if (b->view != NULL) qa_app_remote_seat_view_close(b->view);
}
void qa_remote_application_bind_remote_seat(qa_remote_application *app, qa_remote_seat_peer *peer,
    qa_q3_client_connection *connection) {
  const qa_remote_presentation *remote = qa_app_remote_seat_source_remote(peer->source);
  if (remote->family == QA_APP_REMOTE_FAMILY_UNIFIED ? !qa_remote_application_peer_world_current(app, peer)
      : peer->loaded_content != app->loaded_content) return;
  if (peer->closed || peer->view != NULL || app->controls == NULL || app->frontend == NULL || peer->loaded_content == NULL
      || (app->primary_peer != peer && qa_remote_application_presentation(app) == NULL))
    return;
  remote_seat_bind *b = seats_xcalloc(1, sizeof *b);
  qa_remote_arena_add(&app->arena, free, b); /* the view's closures read it for the view's life */
  b->app = app;
  b->peer = peer;
  b->connection = connection;
  b->controls = app->controls;
  b->frontend = app->frontend;
  b->generation = app->world_load_generation;
  qa_caught_error caught;
  if (!qa_try(bind_body, b, &caught)) {
    if (b->view != NULL) {
      qa_caught_error cleanup;
      bool closed = qa_try(bind_close_view, b, &cleanup);
      qa_app_remote_seat_view_destroy(b->view);
      b->view = NULL;
      if (!closed) qa_rethrow(&cleanup);
    }
    /* A seat retired while its view was still binding ends quietly; live failures still throw. */
    if (peer->closed || app->closed || app->closing) return;
    qa_rethrow(&caught);
  }
}

/* ---- joinRemoteSeat (TS 3290-3364) ------------------------------------- */
typedef struct join_state {
  qa_remote_application *app;
  qa_remote_configuration *previous, *candidate;
  qa_app_console_script_files *previous_scripts;
  qa_app_options options;
  qa_app_configuration_content *mounted;
  qa_remote_seat_peer **before; size_t before_count;
  bool published;
  qa_app_local_seat_preparation *input;
  qa_command_append_target release;
} join_state;
static int join_cheats(void *ctx) {
  join_state *state = ctx;
  return qa_cvar_registry_variable_value(state->previous->source, "sv_cheats") == 1;
}
static bool join_was_before(join_state *state, qa_remote_seat_peer *peer) {
  for (size_t i = 0; i < state->before_count; i++) if (state->before[i] == peer) return true;
  return false;
}
static void join_body(void *ctx) {
  join_state *state = ctx;
  qa_remote_application *app = state->app;
  qa_app_application_input *controls = app->controls;
  qa_remote_application_prepare_remote_seats(app, state->candidate);
  size_t count = qa_app_application_input_local_count(controls);
  qa_session_seat **seats = seats_xcalloc(count, sizeof *seats);
  for (size_t i = 0; i < count; i++) seats[i] = qa_app_application_input_local(controls, i)->player.seat;
  state->input = qa_app_application_input_prepare_local_seats(controls, seats, count);
  free(seats);
  qa_app_prepared_client_commands_validate_publication(qa_app_prepared_profile_program(state->candidate->profile));
  qa_app_image_client_settings images = {state->candidate->images.settings, state->candidate->images.transfer};
  qa_app_image_client_settings_validate_publication(&images);
  qa_remote_configuration_validate_source(state->candidate);
  state->release = qa_remote_release_commands(state->candidate->profile);
  qa_app_application_input_release_for_profile_change(controls, &state->release);
  qa_remote_application_publish_configuration(app, state->candidate);
  state->published = true;
  qa_app_options_destroy(&app->launch_options);
  app->launch_options = state->options;
  memset(&state->options, 0, sizeof state->options);
  qa_remote_command_owner *owner = app->client_command_owner;
  if (owner == NULL) qa_raise("Error", 0, 0, "Remote source publication lost its command owner");
  qa_app_input_command_owner view = qa_remote_input_owner(owner);
  qa_app_application_input_adopt_client_source(controls, &view);
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    if (join_was_before(state, app->remote_seats[i])) continue;
    qa_session_connection *previous = qa_app_remote_seat_channel_publish_connection(app->remote_seats[i]->channel);
    if (previous != NULL) qa_session_connection_close(previous);
  }
  count = qa_app_application_input_local_count(controls);
  qa_app_local_player *players = seats_xcalloc(count, sizeof *players);
  for (size_t i = 0; i < count; i++) players[i] = qa_app_application_input_local(controls, i)->player;
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, players);
  qa_app_local_seat_preparation_publish(state->input, players, count, app->ownership.borrowed ? app->ownership.client : NULL,
      qa_remote_application_source_access(app), true);
  qa_cleanup_pop(&guard);
  free(players);
  if (app->ownership.borrowed) {
    app->ownership.client->configuration_current.scripts = qa_app_prepared_profile_scripts(state->candidate->profile);
    app->ownership.client->configuration_current.options = &app->launch_options;
  }
  if (state->previous_scripts != NULL) qa_app_console_script_files_close(state->previous_scripts);
  qa_app_image_settings_close(state->previous->images.settings);
}
static void join_retire_peers(void *ctx) {
  join_state *state = ctx;
  qa_remote_application *app = state->app;
  for (size_t i = app->remote_seat_count; i > 0; i--) {
    qa_remote_seat_peer *peer = app->remote_seats[i - 1];
    if (!join_was_before(state, peer)) qa_remote_application_retire_remote_seat(app, peer, false);
  }
}
static void join_discard(void *ctx) { qa_app_remote_seat_identities_discard(((join_state *)ctx)->candidate->identities); }
static void join_close_scripts(void *ctx) {
  qa_app_console_script_files_close(qa_app_prepared_profile_scripts(((join_state *)ctx)->candidate->profile));
}
static void join_close_images(void *ctx) { qa_app_image_settings_close(((join_state *)ctx)->candidate->images.settings); }
void qa_remote_application_join_remote_seat(qa_remote_application *app) {
  if (qa_remote_application_source(app)->kind != QA_REMOTE_SOURCE_LIVE) qa_raise("Error", 0, 0, "Recorded playback has one recorded viewpoint");
  if (app->configuration->seat_count >= 4) qa_raise("Error", 0, 0, "Four local players are already connected");
  qa_app_application_input *controls = app->controls;
  if (controls == NULL || !qa_remote_application_primary_view_current(app))
    qa_raise("Error", 0, 0, "Wait for the current player to finish connecting");
  qa_app_application_input_save_settings(controls);
  join_state *state = seats_xcalloc(1, sizeof *state);
  qa_cleanup state_guard;
  qa_cleanup_push(&state_guard, free, state);
  state->app = app;
  state->previous = app->configuration;
  state->previous_scripts = app->client_command_owner != NULL ? app->client_command_owner->scripts : NULL;
  qa_remote_options_copy(&app->launch_options, &state->options);
  state->options.seats = (double)state->previous->seat_count + 1;
  state->mounted = qa_app_open_remote_application_content_native(&state->options);
  if (state->mounted == NULL) qa_range_error("Out of memory");
  qa_remote_arena_add(&app->arena, (void (*)(void *))qa_app_configuration_content_destroy, state->mounted);
  state->candidate = qa_remote_prepare_configuration(&state->options, state->mounted, app->session, app->image_settings,
      app->ownership, &app->host.base, join_cheats, state, state->previous, NULL, &app->arena);
  state->before = seats_xcalloc(app->remote_seat_count, sizeof *state->before);
  for (size_t i = 0; i < app->remote_seat_count; i++) state->before[i] = app->remote_seats[i];
  state->before_count = app->remote_seat_count;
  qa_caught_error caught;
  bool ok = qa_try(join_body, state, &caught);
  if (state->input != NULL) qa_app_local_seat_preparation_destroy(state->input);
  if (!ok) {
    bool aggregate = false;
    qa_caught_error cleanup;
    if (!state->published) {
      void (*steps[])(void *) = {join_retire_peers, join_discard, join_close_scripts, join_close_images};
      for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) if (!qa_try(steps[i], state, &cleanup)) aggregate = true;
    }
    qa_app_options_destroy(&state->options);
    free(state->before);
    qa_cleanup_pop(&state_guard);
    free(state);
    if (aggregate) qa_raise("AggregateError", 0, 0, "Remote player admission failed");
    qa_rethrow(&caught);
  }
  qa_app_options_destroy(&state->options);
  free(state->before);
  qa_cleanup_pop(&state_guard);
  free(state);
}

/* ---- dropRemoteSeat (TS 3366-3443) ------------------------------------- */
typedef struct promoted_routing {
  qa_command_cvar_routing previous;
  qa_cvar_registry *previous_source, *cvars;
  qa_cvar_registry **visible; size_t capacity;
} promoted_routing;
static void release_promoted_routing(void *object) {
  promoted_routing *routing = object;
  free(routing->visible);
  free(routing);
}
static qa_cvar_registry *promoted_owner(void *ctx, const char *name, qa_command_context source) {
  promoted_routing *routing = ctx;
  const qa_command_origin *origin = &source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL) origin = origin->caller;
  qa_cvar_registry *owner = routing->previous.owner(routing->previous.ctx, name, source);
  return origin->kind == QA_COMMAND_ORIGIN_LOCAL_CONSOLE && owner == routing->previous_source ? routing->cvars : owner;
}
static qa_cvar_registry *const *promoted_visible(void *ctx, qa_command_context source, size_t *out_count) {
  promoted_routing *routing = ctx;
  size_t count = 0;
  qa_cvar_registry *const *visible = routing->previous.visible(routing->previous.ctx, source, &count);
  if (count > routing->capacity) {
    qa_cvar_registry **grown = realloc(routing->visible, count * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    routing->visible = grown;
    routing->capacity = count;
  }
  for (size_t i = 0; i < count; i++) routing->visible[i] = visible[i] == routing->previous_source ? routing->cvars : visible[i];
  *out_count = count;
  return routing->visible;
}
static void drop_forward(void *ctx, const char *name, const char *const *args, size_t arg_count, qa_command_context source) {
  const qa_command_origin *origin = &source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL) origin = origin->caller;
  qa_seat_id seat = origin->seat;
  qa_remote_application_queue_command(ctx, name, args, arg_count, origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ? &seat : NULL, &source);
}
void qa_remote_application_drop_remote_seat(qa_remote_application *app, int64_t index) {
  if (qa_remote_application_source(app)->kind != QA_REMOTE_SOURCE_LIVE) qa_raise("Error", 0, 0, "Recorded playback has one recorded viewpoint");
  qa_session_seat *seat = NULL;
  for (size_t i = 0; i < app->configuration->seat_count && seat == NULL; i++)
    if (qa_session_seat_id(app->configuration->seats[i]).index == index) seat = app->configuration->seats[i];
  if (seat == NULL) qa_raise("Error", 0, 0, "That player is not connected");
  if (app->configuration->seat_count == 1) qa_raise("Error", 0, 0, "Use disconnect to leave the last connection");
  qa_app_application_input *controls = app->controls;
  if (controls == NULL) qa_raise("Error", 0, 0, "Wait for the current player to finish connecting");
  qa_app_application_input_save_settings(controls);
  qa_remote_seat_peer *old_primary = app->primary_peer;
  bool primary = qa_same_seat(qa_session_seat_id(seat), qa_remote_application_seat_id(app));
  qa_remote_seat_peer *replacement = NULL;
  if (primary)
    for (size_t i = 0; i < app->remote_seat_count && replacement == NULL; i++) {
      qa_remote_seat_peer *peer = app->remote_seats[i];
      const char *map = app->launch_options.map;
      if (!peer->closed && peer != old_primary && peer->view != NULL && qa_app_remote_seat_channel_active(peer->channel)
          && peer->map != NULL && map != NULL && strcmp(peer->map, map) == 0) replacement = peer;
    }
  if (primary && replacement == NULL)
    qa_raise("Error", 0, 0, "Wait for another player to finish connecting before removing this player");
  size_t local_count = qa_app_application_input_local_count(controls);
  qa_app_local_player *survivors = seats_xcalloc(local_count, sizeof *survivors);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, survivors);
  size_t count = 0;
  for (size_t i = 0; i < local_count; i++) {
    qa_app_local_input *local = qa_app_application_input_local(controls, i);
    if (!qa_same_seat(qa_session_seat_id(local->player.seat), qa_session_seat_id(seat))) survivors[count++] = local->player;
  }
  if (replacement != NULL)
    for (size_t i = 1; i < count; i++) {
      qa_app_local_player moving = survivors[i];
      size_t at = i;
      while (at > 0) {
        qa_app_local_player *a = &survivors[at - 1];
        int order = a->seat == replacement->seat ? -1 : moving.seat == replacement->seat ? 1
            : qa_session_seat_id(a->seat).index < qa_session_seat_id(moving.seat).index ? -1
            : qa_session_seat_id(a->seat).index > qa_session_seat_id(moving.seat).index ? 1 : 0;
        if (order <= 0) break;
        survivors[at] = survivors[at - 1];
        at--;
      }
      survivors[at] = moving;
    }
  qa_session_seat **seats = seats_xcalloc(count, sizeof *seats);
  for (size_t i = 0; i < count; i++) seats[i] = survivors[i].seat;
  qa_app_local_seat_preparation *prepared = qa_app_application_input_prepare_local_seats(controls, seats, count);
  free(seats);
  qa_cleanup prepared_guard;
  qa_cleanup_push(&prepared_guard, (void (*)(void *))qa_app_local_seat_preparation_destroy, prepared);
  qa_cvar_registry *previous_source = app->configuration->source;
  if (replacement != NULL) {
    app->primary_peer = replacement;
    qa_cvar_registry *cvars = qa_remote_configuration_seat_source(app->configuration, qa_session_seat_id(replacement->seat));
    if (cvars == NULL) qa_raise("Error", 0, 0, "Promoted player lost its source registry");
    promoted_routing *routing = seats_xcalloc(1, sizeof *routing);
    qa_remote_arena_add(&app->arena, release_promoted_routing, routing);
    routing->previous = app->configuration->routing;
    routing->previous_source = previous_source;
    routing->cvars = cvars;
    qa_remote_configuration *configuration = seats_xcalloc(1, sizeof *configuration);
    qa_remote_arena_add(&app->arena, qa_remote_release_configuration_record, configuration);
    *configuration = *app->configuration;
    configuration->seats = qa_remote_copy_seats(app->configuration->seats, app->configuration->seat_count);
    configuration->seat_sources = qa_remote_copy_seat_sources(app->configuration->seat_sources, app->configuration->seat_source_count);
    configuration->download_permissions = qa_remote_copy_permissions(app->configuration->download_permissions,
        app->configuration->download_permission_count);
    configuration->transfers = NULL; configuration->transfer_count = 0;
    configuration->visible = NULL; configuration->visible_capacity = 0;
    configuration->seat = replacement->seat;
    configuration->source = cvars;
    configuration->routing = (qa_command_cvar_routing){routing, promoted_owner, promoted_visible};
    app->configuration = configuration;
    qa_remote_command_owner *owner = app->client_command_owner;
    const qa_app_prepared_seat *mouse = qa_remote_prepared_seat(configuration->prepared, qa_session_seat_id(replacement->seat));
    if (owner == NULL || mouse == NULL) qa_raise("Error", 0, 0, "Promoted player lost its command owner");
    owner->cvars = cvars;
    owner->routing = configuration->routing;
    owner->input_settings = (qa_mouse_settings *)&mouse->mouse;
    qa_app_prepared_owners owners;
    memset(&owners, 0, sizeof owners);
    owners.source = cvars;
    owners.movement = qa_app_prepared_startup_movement(configuration->prepared);
    owners.fallback = cvars;
    owners.scripts = owner->scripts;
    owners.read = qa_app_prepared_profile_read(configuration->profile, &owners.read_ctx);
    qa_app_prepared_startup_adopt(configuration->prepared, configuration->routing, drop_forward, app, &owners);
    qa_app_input_command_owner view = qa_remote_input_owner(owner);
    qa_app_application_input_adopt_client_source(controls, &view);
    qa_app_keys_publish(app->keys, app->key_profile, cvars);
  }
  /* this.configuration = { ...this.configuration, seats: seats.filter(candidate => candidate !== seat) } */
  qa_remote_configuration *filtered = seats_xcalloc(1, sizeof *filtered);
  qa_remote_arena_add(&app->arena, qa_remote_release_configuration_record, filtered);
  *filtered = *app->configuration;
  filtered->seats = qa_remote_copy_seats(app->configuration->seats, app->configuration->seat_count);
  filtered->seat_sources = qa_remote_copy_seat_sources(app->configuration->seat_sources, app->configuration->seat_source_count);
  filtered->download_permissions = qa_remote_copy_permissions(app->configuration->download_permissions,
      app->configuration->download_permission_count);
  filtered->transfers = NULL; filtered->transfer_count = 0;
  filtered->visible = NULL; filtered->visible_capacity = 0;
  if (app->configuration->routing.ctx == app->configuration) filtered->routing.ctx = filtered;
  size_t kept = 0;
  for (size_t i = 0; i < app->configuration->seat_count; i++)
    if (app->configuration->seats[i] != seat) filtered->seats[kept++] = app->configuration->seats[i];
  filtered->seat_count = kept;
  app->configuration = filtered;
  app->launch_options.seats = (double)kept;
  qa_app_local_seat_preparation_publish(prepared, survivors, count, app->ownership.borrowed ? app->ownership.client : NULL,
      qa_remote_application_source_access(app), true);
  qa_cleanup_pop(&prepared_guard);
  qa_app_local_seat_preparation_destroy(prepared);
  qa_cleanup_pop(&guard);
  free(survivors);
  qa_remote_application_publish_remote_layouts(app);
  if (primary && old_primary == NULL) {
    qa_app_world_seat_presentation *initial = app->initial_presentation;
    qa_app_application_q3_client *client = qa_remote_presentation_q3_client(initial);
    if (client != NULL) qa_app_application_q3_client_shutdown(client);
    qa_seat_presentation current;
    if (initial != NULL && qa_session_seat_presentation(seat, &current)
        && current.ctx == qa_app_world_seat_presentation_seat(initial).ctx) qa_session_seat_clear_presentation(seat);
    app->initial_presentation = NULL;
    qa_remote_application_close_qw_downloads(app);
    qa_remote_application_close_q3_downloads(app);
    if (app->initial_source.kind == QA_REMOTE_SOURCE_LIVE) qa_app_remote_seat_network_close(&app->initial_source.network);
    if (qa_session_client_connection(qa_session_seat_client(seat)) == app->source_connection)
      qa_session_client_disconnect(qa_session_seat_client(seat));
    app->source_connection = NULL;
    if (app->has_initial_qport) remove_qport(app, app->initial_qport);
  } else {
    qa_remote_seat_peer *peer = old_primary != NULL && primary ? old_primary : NULL;
    for (size_t i = 0; i < app->remote_seat_count && peer == NULL; i++)
      if (!app->remote_seats[i]->closed && app->remote_seats[i]->seat == seat) peer = app->remote_seats[i];
    if (peer != NULL) qa_remote_application_retire_remote_seat(app, peer, false);
  }
}

/* ---- prepareRemoteSeats (TS 3445-3706) --------------------------------- */
/* A peer's pumped operation. TS enqueues it on the seat's RemoteSeatPump and the
 * network receive that asked for it waits for its result. The C hooks return
 * synchronously, so the operation is enqueued and the pump drained at once: it
 * runs now when its frame gate (ready) holds and nothing is queued ahead of it;
 * otherwise the receive cannot wait and the hook raises the pending condition. */
typedef struct pumped_operation {
  void (*run)(void *ctx); void *ctx;
  bool (*ready)(void *ctx);
  bool done; qa_caught_error error; bool failed;
} pumped_operation;
static bool pumped_ready(void *ctx) {
  pumped_operation *operation = ctx;
  return operation->ready == NULL || operation->ready(operation->ctx);
}
static const char *pumped_execute(void *ctx) {
  pumped_operation *operation = ctx;
  operation->done = true;
  operation->failed = !qa_try(operation->run, operation->ctx, &operation->error);
  return NULL;
}
static void pumped_reject(void *ctx, const char *error) {
  pumped_operation *operation = ctx;
  operation->done = true;
  operation->failed = true;
  operation->error.name = "Error";
  snprintf(operation->error.message, sizeof operation->error.message, "%s", error);
}
static void run_pumped(qa_remote_seat_peer *peer, void (*run)(void *ctx), void *ctx, bool (*ready)(void *ctx)) {
  pumped_operation operation;
  memset(&operation, 0, sizeof operation);
  operation.run = run;
  operation.ctx = ctx;
  operation.ready = ready;
  qa_app_remote_seat_publication publication = {&operation, pumped_ready, pumped_execute, pumped_reject};
  if (qa_app_remote_seat_pump_enqueue(peer->pump, publication)) qa_app_remote_seat_pump_drain_ready(peer->pump);
  if (!operation.done) qa_raise("Error", 0, 0, "Remote seat operation is waiting for its frame boundary");
  if (operation.failed) qa_rethrow(&operation.error);
}

typedef struct seat_record {
  qa_remote_application *app;
  qa_session_seat *seat;
  qa_cvar_registry *cvars;
  qa_remote_seat_peer *created;   /* NULL = TS null */
  qa_app_presentation_time *clock;
} seat_record;
static qa_remote_seat_peer *record_current(seat_record *record) {
  if (record->created == NULL) qa_raise("Error", 0, 0, "Remote seat source has not been constructed");
  qa_remote_application *app = record->app;
  if (record->created->closed || app->closed || app->closing) qa_raise("Error", 0, 0, "%s", qa_app_remote_seat_pump_retirement_error_text);
  return record->created;
}
static void record_current_body(void *ctx) { (void)record_current(ctx); }
static qa_cvar_registry *record_cvars(void *ctx) {
  seat_record *record = ctx;
  qa_cvar_registry *cvars = qa_remote_configuration_seat_source(record->app->configuration, qa_session_seat_id(record->seat));
  return cvars != NULL ? cvars : record->cvars;
}
static const qa_app_options *hook_options(void *ctx) { return &((seat_record *)ctx)->app->launch_options; }
static qa_content_mounted *hook_mounts(void *ctx) {
  seat_record *record = ctx;
  qa_remote_seat_peer *peer = record_current(record);
  return peer->content != NULL ? peer->content->mounts : qa_remote_application_mounts(record->app);
}
static qa_app_loaded_content *hook_content(void *ctx) { return qa_remote_application_content(((seat_record *)ctx)->app); }
static qa_app_remote_content_mounts *hook_current_content(void *ctx) { return record_current(ctx)->content; }
/* select(selection, assertCurrent) */
typedef struct select_call {
  seat_record *record; const qa_remote_content_selection *selection;
  qa_app_remote_assert_fn assert_current; void *assert_ctx;
  qa_app_remote_content_mounts *result;
} select_call;
static const char *select_assert_text(void *ctx) {
  select_call *call = ctx;
  call->assert_current(call->assert_ctx);
  return NULL;
}
static void select_close_content(void *ctx) {
  qa_app_remote_content_mounts *content = ctx;
  qa_app_remote_content_mounts_close(content);
  qa_app_remote_content_mounts_destroy(content);
}
static void select_run(void *ctx) {
  select_call *call = ctx;
  seat_record *record = call->record;
  qa_remote_application *app = record->app;
  qa_remote_seat_peer *peer = record_current(record);
  call->assert_current(call->assert_ctx);
  if (peer == app->primary_peer) {
    qa_app_remote_content_mounts *content = qa_remote_application_select_remote_content(app, call->selection, select_assert_text, call);
    peer->content = content;
    call->result = content;
    return;
  }
  qa_app_remote_content_mounts *content = qa_app_open_remote_content_native(&app->launch_options, call->selection, select_assert_text,
      call, (int64_t)++peer->generation);
  if (content == NULL) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, select_close_content, content);
  (void)record_current(record);
  call->assert_current(call->assert_ctx);
  qa_cleanup_pop(&guard);
  qa_app_remote_content_mounts *previous = peer->content;
  peer->content = content;
  qa_remote_arena_add(&app->arena, (void (*)(void *))qa_app_remote_content_mounts_destroy, content);
  if (previous != NULL) qa_app_remote_content_mounts_close(previous);
  call->result = content;
}
typedef struct select_ready { seat_record *record; const qa_remote_content_selection *selection; } select_ready;
static bool select_is_ready(void *ctx) {
  select_call *call = ctx;
  qa_remote_application *app = call->record->app;
  if (record_current(call->record) == app->primary_peer) return true;
  if (!qa_remote_application_primary_view_current(app)) return false;
  char *product = qa_remote_content_product(call->selection, NULL, 0);
  if (product == NULL) qa_range_error("Out of memory");
  bool same = strcmp(app->launch_options.product, product) == 0;
  free(product);
  return same;
}
static qa_app_remote_content_mounts *hook_select_content(void *ctx, const qa_remote_content_selection *selection,
    qa_app_remote_assert_fn assert_current, void *assert_ctx) {
  seat_record *record = ctx;
  select_call call = {record, selection, assert_current, assert_ctx, NULL};
  run_pumped(record_current(record), select_run, &call, select_is_ready);
  return call.result;
}
static qa_app_remote_content_mounts *hook_refresh_content(void *ctx, qa_app_remote_assert_fn assert_current, void *assert_ctx) {
  seat_record *record = ctx;
  qa_remote_seat_peer *peer = record_current(record);
  if (peer->content == NULL) qa_raise("Error", 0, 0, "Remote seat package refresh has no selected directory");
  qa_remote_content_selection selection;
  if (!qa_remote_content_selection_make(peer->content->selection.base, peer->content->selection.directory, &selection))
    qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_remote_content_selection_free, &selection);
  qa_app_remote_content_mounts *content = hook_select_content(ctx, &selection, assert_current, assert_ctx);
  qa_cleanup_pop(&guard);
  qa_remote_content_selection_free(&selection);
  return content;
}
/* load(world, prepared?) */
typedef struct load_call {
  seat_record *record; const qa_app_q1_remote_world_info *world; const char *const *images; size_t image_count;
  qa_app_loaded_content *prepared; qa_app_loaded_content *result;
} load_call;
static void load_run(void *ctx) {
  load_call *call = ctx;
  seat_record *record = call->record;
  qa_remote_application *app = record->app;
  qa_remote_seat_peer *peer = record_current(record);
  if (peer->view != NULL) {
    qa_app_remote_seat_view *view = peer->view;
    peer->view = NULL;
    qa_remote_arena_add(&app->arena, (void (*)(void *))qa_app_remote_seat_view_destroy, view);
    qa_app_remote_seat_view_close(view);
  }
  char *map = qa_app_server_map_path(call->world->map);
  if (map == NULL) qa_range_error("Out of memory");
  free(peer->map);
  peer->map = map;
  peer->loaded_content = peer == app->primary_peer
      ? qa_remote_application_load_server_world(app, call->world, call->images, call->image_count, call->prepared, false)
      : call->prepared != NULL ? call->prepared : qa_remote_application_content(app);
  if (peer == app->primary_peer && app->remote_content == NULL) peer->content = NULL;
  call->result = peer->loaded_content;
}
static bool load_is_ready(void *ctx) {
  load_call *call = ctx;
  qa_remote_application *app = call->record->app;
  if (record_current(call->record) == app->primary_peer) return true;
  if (!qa_remote_application_primary_view_current(app) || app->loaded_content == NULL) return false;
  char *map = qa_app_server_map_path(call->world->map);
  if (map == NULL) qa_range_error("Out of memory");
  bool same = strcmp(qa_app_loaded_content_recipe(app->loaded_content)->map.geometry.requested_path, map) == 0;
  free(map);
  return same;
}
static qa_app_loaded_content *seat_load(seat_record *record, const qa_app_q1_remote_world_info *world, const char *const *images,
    size_t image_count, qa_app_loaded_content *prepared) {
  load_call call = {record, world, images, image_count, prepared, NULL};
  run_pumped(record_current(record), load_run, &call, load_is_ready);
  return call.result;
}
static qa_app_loaded_content *hook_load_q1_world(void *ctx, const qa_app_q1_remote_world_info *world, bool refresh_content) {
  (void)refresh_content; /* TS loadQ1World: (world) => load(world) */
  return seat_load(ctx, world, NULL, 0, NULL);
}
static const char *q2_config_get(const qa_app_q2_config_snapshot *configs, double index) {
  for (size_t i = 0; i < configs->count; i++) if (configs->entries[i].index == index) return configs->entries[i].value;
  return NULL;
}
static qa_app_loaded_content *hook_load_q2_world(void *ctx, const qa_app_q2_game_state *state) {
  seat_record *record = ctx;
  const qa_remote_presentation *remote = qa_app_remote_seat_source_remote(record_current(record)->source);
  if (remote->family != QA_APP_REMOTE_FAMILY_Q2) qa_raise("Error", 0, 0, "Q2 remote seat has no protocol owner");
  qa_app_q2_layout layout = qa_app_q2_application_layout(qa_app_q2_remote_protocol(remote->q2));
  const qa_app_q2_config_snapshot *configs = state->config_strings;
  const char *map = q2_config_get(configs, layout.models + 1);
  if (map == NULL) qa_raise("Error", 0, 0, "Q2 remote seat server supplied no world model");
  double firsts[3] = {layout.models, layout.sounds, layout.images}, maxima[3] = {layout.max_models, layout.max_sounds, layout.max_images};
  const char **names[3];
  size_t counts[3];
  for (int k = 0; k < 3; k++) {
    size_t length = maxima[k] > 1 ? (size_t)(maxima[k] - 1) : 0;
    names[k] = seats_xcalloc(length, sizeof *names[k]);
    counts[k] = 0;
    for (size_t i = 0; i < length; i++) {
      const char *value = q2_config_get(configs, firsts[k] + (double)i + 1);
      if (value != NULL && value[0] != '\0') names[k][counts[k]++] = value;
    }
  }
  qa_app_q1_remote_world_info world = {map, names[0], counts[0], names[1], counts[1]};
  qa_cleanup guards[3];
  for (int k = 0; k < 3; k++) qa_cleanup_push(&guards[k], free, (void *)names[k]);
  qa_app_loaded_content *content = seat_load(record, &world, names[2], counts[2], NULL);
  for (int k = 2; k >= 0; k--) { qa_cleanup_pop(&guards[k]); free((void *)names[k]); }
  return content;
}
static qa_app_loaded_content *hook_load_q3_world(void *ctx, const qa_app_q1_remote_world_info *world,
    qa_q3_client_connection *connection, qa_app_loaded_content *prepared) {
  (void)connection; /* TS loadQ3World: (world, _connection, prepared) => load(world, prepared) */
  return seat_load(ctx, world, NULL, 0, prepared);
}
typedef struct initialize_call { seat_record *record; qa_q3_client_connection *connection; } initialize_call;
static void initialize_run(void *ctx) {
  initialize_call *call = ctx;
  qa_remote_application_bind_remote_seat(call->record->app, record_current(call->record), call->connection);
}
static bool initialize_is_ready(void *ctx) {
  initialize_call *call = ctx;
  return record_current(call->record) == call->record->app->primary_peer
      || qa_remote_application_primary_view_current(call->record->app);
}
static void hook_initialize_q3(void *ctx, qa_q3_client_connection *connection) {
  seat_record *record = ctx;
  initialize_call call = {record, connection};
  run_pumped(record_current(record), initialize_run, &call, initialize_is_ready);
}
static void shutdown_run(void *ctx) {
  seat_record *record = ctx;
  qa_remote_seat_peer *peer = record_current(record);
  if (peer->view != NULL) {
    qa_app_remote_seat_view *view = peer->view;
    peer->view = NULL;
    qa_remote_arena_add(&record->app->arena, (void (*)(void *))qa_app_remote_seat_view_destroy, view);
    qa_app_remote_seat_view_close(view);
  }
}
static void hook_shutdown_q3(void *ctx) {
  seat_record *record = ctx;
  if (record->created == NULL || record->created->closed) return;
  run_pumped(record->created, shutdown_run, record, NULL);
}
typedef struct cinematic_call { seat_record *record; const char *name; qa_app_q2_cinematic_ended_fn ended; void *ended_ctx; } cinematic_call;
static void cinematic_run(void *ctx) {
  cinematic_call *call = ctx;
  qa_remote_application *app = call->record->app;
  qa_remote_seat_peer *peer = record_current(call->record);
  if (peer == app->primary_peer) qa_remote_application_start_cinematic(app, call->name, false, false, false, call->ended, call->ended_ctx);
  else {
    peer->cinematic_ended = call->ended;
    peer->cinematic_ended_ctx = call->ended_ctx;
  }
}
static bool cinematic_is_ready(void *ctx) {
  cinematic_call *call = ctx;
  return record_current(call->record) == call->record->app->primary_peer || call->record->app->cinematic != NULL;
}
static void hook_cinematic_start(void *ctx, const char *name, qa_app_q2_cinematic_ended_fn ended, void *ended_ctx) {
  seat_record *record = ctx;
  cinematic_call call = {record, name, ended, ended_ctx};
  run_pumped(record_current(record), cinematic_run, &call, cinematic_is_ready);
}
static void hook_cinematic_stop(void *ctx) {
  seat_record *record = ctx;
  if (record->created != NULL) {
    record->created->cinematic_ended = NULL;
    record->created->cinematic_ended_ctx = NULL;
  }
}
static void hook_publish(void *ctx, const qa_simulation_output *output) {
  seat_record *record = ctx;
  qa_remote_seat_peer *created = record->created;
  qa_remote_application *app = record->app;
  if (created == NULL || created->closed || app->closing || created->view == NULL) return;
  if (created == app->primary_peer) qa_remote_application_publish_remote(app, output);
  else {
    qa_simulation_event *events = seats_xcalloc(output->event_count, sizeof *events);
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, events);
    (void)qa_app_remote_seat_channel_receive(created->channel, *output, events);
    qa_cleanup_pop(&guard);
    free(events);
  }
}
static void hook_disconnected(void *ctx, const char *reason) {
  seat_record *record = ctx;
  if (record->created == NULL || record->created->closed) return;
  char *copy = strdup(reason);
  if (copy == NULL) qa_range_error("Out of memory");
  free(record->created->disconnected);
  record->created->disconnected = copy;
}
static void hook_print(void *ctx, const char *text) {
  seat_record *record = ctx;
  qa_command_context source;
  memset(&source, 0, sizeof source);
  source.session = qa_session_seat_id(record->seat).session;
  source.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  source.origin.seat = qa_session_seat_id(record->seat);
  source.origin.client = qa_session_client_id(qa_session_seat_client(record->seat));
  qa_remote_application_print(record->app, text, &source);
}
static bool hook_recording(void *ctx) {
  qa_remote_application *app = ((seat_record *)ctx)->app;
  if (app->ownership.borrowed) return qa_app_client_demo_recording_path(app->ownership.client->recording) != NULL;
  return app->owned_recording != NULL && qa_app_client_demo_recording_path(app->owned_recording) != NULL;
}
/* unified */
typedef struct unified_load_call {
  seat_record *record; const qa_app_unified_control *offer; qa_app_unified_operation_fn assert_current; void *assert_ctx;
  qa_app_loaded_content *result;
} unified_load_call;
static const char *unified_assert_text(void *ctx) {
  unified_load_call *call = ctx;
  call->assert_current(call->assert_ctx);
  return NULL;
}
typedef struct unified_world_call {
  qa_remote_application *app; const qa_app_q1_remote_world_info *world; qa_app_loaded_content *content;
  qa_app_loaded_content *result;
} unified_world_call;
static void unified_world_run(void *ctx) {
  unified_world_call *call = ctx;
  call->result = qa_remote_application_load_server_world(call->app, call->world, NULL, 0, call->content, false);
}
static void unified_load_run(void *ctx) {
  unified_load_call *call = ctx;
  seat_record *record = call->record;
  qa_remote_application *app = record->app;
  qa_remote_seat_peer *peer = record_current(record);
  call->assert_current(call->assert_ctx);
  if (peer == app->primary_peer) {
    qa_app_loaded_content *content = qa_remote_application_load_unified_content(app, call->offer);
    qa_caught_error caught;
    if (!qa_try(record_current_body, record, &caught)) {
      qa_app_loaded_content_close(content);
      qa_app_loaded_content_destroy(content);
      qa_rethrow(&caught);
    }
    qa_remote_application_adopt_unified_profile(app, content, call->offer->mode, unified_assert_text, call);
    if (peer->view != NULL) {
      qa_app_remote_seat_view *view = peer->view;
      peer->view = NULL;
      qa_remote_arena_add(&app->arena, (void (*)(void *))qa_app_remote_seat_view_destroy, view);
      qa_app_remote_seat_view_close(view);
    }
    qa_app_q1_remote_world_info world = {qa_app_loaded_content_recipe(content)->map.geometry.requested_path, NULL, 0, NULL, 0};
    unified_world_call world_call = {app, &world, content, NULL};
    if (!qa_try(unified_world_run, &world_call, &caught)) {
      /* loadServerWorld leaves a prepared content to its caller; TS closes it (loaded.close()) */
      if (app->loaded_content != content) {
        qa_app_loaded_content_close(content);
        qa_app_loaded_content_destroy(content);
      }
      qa_rethrow(&caught);
    }
    peer->loaded_content = world_call.result;
  } else {
    if (peer->view != NULL) {
      qa_app_remote_seat_view *view = peer->view;
      peer->view = NULL;
      qa_remote_arena_add(&app->arena, (void (*)(void *))qa_app_remote_seat_view_destroy, view);
      qa_app_remote_seat_view_close(view);
    }
    call->assert_current(call->assert_ctx);
    peer->loaded_content = qa_remote_application_content(app);
  }
  char *map = strdup(qa_app_loaded_content_recipe(peer->loaded_content)->map.geometry.requested_path);
  if (map == NULL) qa_range_error("Out of memory");
  free(peer->map);
  peer->map = map;
  call->result = peer->loaded_content;
}
static bool unified_load_ready(void *ctx) {
  unified_load_call *call = ctx;
  qa_remote_application *app = call->record->app;
  if (record_current(call->record) == app->primary_peer) return true;
  const qa_remote_presentation *remote = qa_remote_application_remote(app);
  return remote->family == QA_APP_REMOTE_FAMILY_UNIFIED && qa_app_unified_remote_loaded_epoch(remote->unified) == call->offer->epoch
      && app->loaded_content != NULL
      && strcmp(qa_app_unified_remote_composition_digest(remote->unified), call->offer->composition->digest) == 0;
}
static qa_app_loaded_content *unified_load(void *ctx, const qa_app_unified_control *offer, qa_app_unified_operation_fn assert_current,
    void *assert_ctx) {
  seat_record *record = ctx;
  unified_load_call call = {record, offer, assert_current, assert_ctx, NULL};
  run_pumped(record_current(record), unified_load_run, &call, unified_load_ready);
  return call.result;
}
static qa_decoded_model unified_model(void *ctx, const qa_app_unified_resource_key *key, bool has_brush, double brush) {
  seat_record *record = ctx;
  qa_remote_application *app = record->app;
  qa_app_loaded_content *content = record_current(record)->loaded_content;
  if (content == NULL || app->frontend == NULL) qa_raise("Error", 0, 0, "Unified peer model has no admitted world");
  if (has_brush) {
    qa_decoded_model model;
    memset(&model, 0, sizeof model);
    model.kind = QA_DECODED_MODEL_BRUSH_MODEL;
    model.brush_world = qa_app_remote_decoded_world(content);
    model.brush_model_index = brush;
    return model;
  }
  return qa_app_application_assets_model(app->frontend->assets, key->content, key->path).model;
}
typedef struct component_call { qa_app_unified_operation_fn operation; void *ctx; } component_call;
static void component_run(void *ctx) {
  component_call *call = ctx;
  call->operation(call->ctx);
}
static void unified_publish_components(void *ctx, qa_app_unified_operation_fn operation, void *operation_ctx) {
  seat_record *record = ctx;
  component_call call = {operation, operation_ctx};
  run_pumped(record_current(record), component_run, &call, NULL);
}
/* RemoteSeatPump source over the seat source's network (remote-seat-pump.ts). */
typedef struct pump_source { qa_app_remote_seat_source *source; char error[QA_CAUGHT_MESSAGE_CAPACITY]; } pump_source;
typedef struct pump_poll { pump_source *source; double now; } pump_poll;
static void pump_poll_body(void *ctx) {
  pump_poll *poll = ctx;
  qa_app_remote_seat_network_poll(qa_app_remote_seat_source_network(poll->source->source), poll->now);
}
static const char *pump_network_poll(void *ctx, double now) {
  pump_source *source = ctx;
  pump_poll poll = {source, now};
  qa_caught_error caught;
  if (qa_try(pump_poll_body, &poll, &caught)) return NULL;
  snprintf(source->error, sizeof source->error, "%s", caught.message);
  return source->error;
}
static void pump_close(void *ctx) { qa_app_remote_seat_source_close(((pump_source *)ctx)->source); }
static void release_peer(void *object) {
  qa_remote_seat_peer *peer = object;
  if (peer->pump != NULL) qa_app_remote_seat_pump_destroy(peer->pump);
  if (peer->channel != NULL) qa_app_remote_seat_channel_destroy(peer->channel);
  if (peer->source != NULL) {
    qa_app_application_transport *transport = qa_app_remote_seat_source_transport(peer->source);
    qa_app_remote_seat_source_destroy(peer->source);
    if (transport != NULL) qa_app_application_transport_destroy(transport);
  }
  free(peer->map);
  free(peer->disconnected);
  free(peer->userinfo);
  free(peer);
}
typedef struct seat_prepare {
  qa_remote_application *app; qa_remote_configuration *configuration; qa_session_seat *seat;
  qa_app_application_transport *transport; bool has_qport; uint16_t qport; qa_net_address address;
} seat_prepare;
static void seat_prepare_body(void *ctx) {
  seat_prepare *p = ctx;
  qa_remote_application *app = p->app;
  qa_cvar_registry *cvars = qa_remote_configuration_seat_source(p->configuration, qa_session_seat_id(p->seat));
  qa_remote_configuration_prepare_transport(p->configuration, qa_remote_application_socks_connector(p->transport));
  seat_record *record = seats_xcalloc(1, sizeof *record);
  qa_remote_arena_add(&app->arena, free, record);
  record->app = app;
  record->seat = p->seat;
  record->cvars = cvars;
  record->clock = seats_xcalloc(1, sizeof *record->clock);
  qa_app_presentation_time_init(record->clock);
  qa_app_remote_seat_source_options options;
  memset(&options, 0, sizeof options);
  options.identity = app->identity;
  options.session = app->session;
  options.seat = p->seat;
  options.cvars = record_cvars;
  options.cvars_ctx = record;
  options.clock = record->clock;
  options.transport = p->transport;
  options.address = p->address;
  p->has_qport = true;
  p->qport = qa_remote_application_allocate_qport(app);
  options.qport = p->qport;
  options.download_permission = NULL;
  for (size_t i = 0; i < p->configuration->download_permission_count; i++)
    if (qa_same_seat(p->configuration->download_permissions[i].id, qa_session_seat_id(p->seat)))
      options.download_permission = p->configuration->download_permissions[i].permission;
  options.hooks = (qa_app_remote_seat_source_hooks){record, hook_options, hook_mounts, hook_content, hook_current_content,
      hook_select_content, hook_refresh_content, hook_load_q1_world, hook_load_q2_world, hook_load_q3_world, hook_initialize_q3,
      hook_shutdown_q3, hook_cinematic_start, hook_cinematic_stop, hook_publish, hook_disconnected, hook_print, hook_recording};
  if (qa_remote_network_is(&app->launch_options, "unified-client")) {
    options.family = QA_APP_REMOTE_FAMILY_UNIFIED;
    options.unified = (qa_app_remote_seat_unified_hooks){record, unified_load, unified_model, unified_publish_components};
  } else if (app->family == QA_APP_DEMO_Q3) {
    options.family = QA_APP_REMOTE_FAMILY_Q3;
    options.authorization = qa_remote_application_q3_authorization(app);
  } else if (app->family == QA_APP_DEMO_Q2) {
    options.family = QA_APP_REMOTE_FAMILY_Q2;
    const qa_catalog_discovered_product *product = qa_content_catalog_product(
        qa_app_configuration_content_catalog(app->mounted_content), app->launch_options.product);
    options.protocol = qa_app_live_q2_protocol(&app->launch_options,
        product->expectation.edition != NULL && strcmp(product->expectation.edition, "rerelease") == 0);
  } else options.family = app->family == QA_APP_DEMO_QW ? QA_APP_REMOTE_FAMILY_QW : QA_APP_REMOTE_FAMILY_Q1;
  qa_remote_seat_peer *peer = seats_xcalloc(1, sizeof *peer);
  peer->seat = p->seat;
  qa_cleanup peer_guard;
  qa_cleanup_push(&peer_guard, release_peer, peer);
  peer->source = qa_app_remote_seat_source_create(&options);
  if (peer->source == NULL) qa_range_error("Out of memory");
  qa_app_remote_seat_services services = {qa_app_remote_seat_source_network(peer->source), qa_app_remote_seat_source_remote(peer->source), cvars};
  peer->channel = qa_app_remote_seat_channel_create(p->seat, record->clock, services);
  if (peer->channel == NULL) qa_range_error("Out of memory");
  pump_source *source = seats_xcalloc(1, sizeof *source);
  qa_remote_arena_add(&app->arena, free, source);
  source->source = peer->source;
  peer->pump = qa_app_remote_seat_pump_create((qa_app_remote_seat_pump_source){source, pump_network_poll, pump_close});
  if (peer->pump == NULL) qa_range_error("Out of memory");
  qa_cleanup_pop(&peer_guard);
  record->created = peer;
  qa_remote_arena_add(&app->arena, release_peer, peer);
  p->transport = NULL; /* the peer's source owns the transport from here */
  if (app->remote_seat_count == app->remote_seat_capacity) {
    size_t capacity = app->remote_seat_capacity == 0 ? 4 : app->remote_seat_capacity * 2;
    qa_remote_seat_peer **grown = realloc(app->remote_seats, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    app->remote_seats = grown;
    app->remote_seat_capacity = capacity;
  }
  app->remote_seats[app->remote_seat_count++] = peer;
}
void qa_remote_application_prepare_remote_seats(qa_remote_application *app, qa_remote_configuration *configuration) {
  if (qa_remote_application_source(app)->kind != QA_REMOTE_SOURCE_LIVE || configuration->seat_count == 1) return;
  const qa_app_options *options = &app->launch_options;
  if (!qa_remote_network_is(options, "q1-client") && !qa_remote_network_is(options, "qw-client") && !qa_remote_network_is(options, "q2-client")
      && !qa_remote_network_is(options, "q3-client") && !qa_remote_network_is(options, "unified-client"))
    qa_raise("Error", 0, 0, "Remote seat requires a native server address");
  bool unified = qa_remote_network_is(options, "unified-client");
  for (size_t i = 0; i < configuration->seat_count; i++) {
    qa_session_seat *seat = configuration->seats[i];
    if (qa_same_seat(qa_session_seat_id(seat), qa_remote_application_seat_id(app))) continue;
    bool open = false;
    for (size_t j = 0; j < app->remote_seat_count && !open; j++) open = !app->remote_seats[j]->closed && app->remote_seats[j]->seat == seat;
    if (open) continue;
    if (qa_remote_configuration_seat_source(configuration, qa_session_seat_id(seat)) == NULL)
      qa_raise("Error", 0, 0, "Remote seat has no source registry");
    qa_app_network_family family = app->family == QA_APP_DEMO_Q1 ? QA_APP_FAMILY_Q1 : app->family == QA_APP_DEMO_QW ? QA_APP_FAMILY_QW
        : app->family == QA_APP_DEMO_Q3 ? QA_APP_FAMILY_Q3 : QA_APP_FAMILY_Q2;
    double port = unified ? 27960 : app->family == QA_APP_DEMO_Q1 ? 26000 : app->family == QA_APP_DEMO_QW ? 27500
        : app->family == QA_APP_DEMO_Q3 ? 27960 : options->has_q2_protocol && strcmp(options->q2_protocol.kind, "q2-kex") == 0 ? 5069 : 27910;
    seat_prepare p;
    memset(&p, 0, sizeof p);
    p.app = app;
    p.configuration = configuration;
    p.seat = seat;
    p.address = qa_app_resolve_application_address(options->network_remote, port, qa_remote_options_transport_selection(options), family);
    qa_cleanup address_guard;
    qa_cleanup_push(&address_guard, (void (*)(void *))qa_net_address_destroy, &p.address);
    qa_app_open_transport_options transport;
    memset(&transport, 0, sizeof transport);
    transport.selection = qa_remote_options_transport_selection(options);
    transport.family = family;
    transport.host = p.address.kind == QA_NET_IPV6 ? "::" : "0.0.0.0";
    transport.port = 0;
    transport.limits = !unified && app->family == QA_APP_DEMO_Q2 ? QA_Q2_DATAGRAM_LIMITS_NET : QA_UNIFIED_DATAGRAM_LIMITS_NET;
    p.transport = qa_app_open_application_transport(&transport, NULL);
    if (p.transport == NULL) qa_range_error("Out of memory");
    qa_caught_error caught;
    if (!qa_try(seat_prepare_body, &p, &caught)) {
      if (p.transport != NULL) {
        qa_app_application_transport_close(p.transport);
        qa_app_application_transport_destroy(p.transport);
      }
      if (p.has_qport) remove_qport(app, p.qport);
      qa_rethrow(&caught);
    }
    qa_cleanup_pop(&address_guard);
    qa_net_address_destroy(&p.address);
  }
}

/* ---- prepareRemoteSeatFrames (TS 3863-3903) ---------------------------- */
void qa_remote_application_prepare_remote_seat_frames(qa_remote_application *app, double now, double wall_elapsed) {
  qa_remote_seat_peer **peers = seats_xcalloc(app->remote_seat_count, sizeof *peers);
  size_t count = app->remote_seat_count;
  if (count > 0) memcpy(peers, app->remote_seats, count * sizeof *peers);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, peers);
  for (size_t i = 0; i < count; i++) {
    qa_remote_seat_peer *peer = peers[i];
    if (peer->closed || peer == app->primary_peer) continue;
    if (peer->disconnected != NULL) {
      char player[40];
      qa_net_js_number((double)qa_session_seat_id(peer->seat).index + 1, player, sizeof player);
      size_t length = strlen(player) + strlen(peer->disconnected) + 16;
      char *line = seats_xcalloc(length, 1);
      snprintf(line, length, "Player %s: %s\n", player, peer->disconnected);
      qa_cleanup line_guard;
      qa_cleanup_push(&line_guard, free, line);
      qa_remote_application_print(app, line, NULL);
      qa_cleanup_pop(&line_guard);
      free(line);
      qa_remote_application_retire_remote_seat(app, peer, true);
      continue;
    }
    const qa_remote_presentation *remote = qa_app_remote_seat_source_remote(peer->source);
    if (!qa_app_remote_seat_channel_active(peer->channel) || !qa_remote_application_peer_world_current(app, peer)) continue;
    const qa_app_remote_seat_network *network = qa_app_remote_seat_source_network(peer->source);
    if (network->family == QA_APP_REMOTE_FAMILY_Q2 && remote->family == QA_APP_REMOTE_FAMILY_Q2) {
      const char *info = qa_app_q2_remote_userinfo(remote->q2);
      if (peer->userinfo == NULL || strcmp(info, peer->userinfo) != 0) {
        char *copy = strdup(info);
        if (copy == NULL) qa_range_error("Out of memory");
        qa_app_q2_client_network_userinfo(network->q2, copy);
        free(peer->userinfo);
        peer->userinfo = copy;
      }
    }
    (void)qa_remote_presentation_sample(remote, now, &peer->sample_scratch);
    if (remote->family == QA_APP_REMOTE_FAMILY_QW) qa_app_qw_remote_prepare_skins(remote->qw, NULL, NULL);
    if (remote->family != QA_APP_REMOTE_FAMILY_Q3) qa_remote_application_bind_remote_seat(app, peer, NULL);
    const qa_simulation_output *output = qa_app_remote_seat_presentation_output(remote);
    qa_actor_id player;
    bool has_player = qa_app_remote_seat_presentation_player(remote, &player);
    if (peer->view == NULL || app->controls == NULL || output == NULL || !has_player) continue;
    qa_app_local_input *local = local_for(app->controls, qa_session_seat_id(peer->seat));
    if (local == NULL) qa_raise("Error", 0, 0, "Remote source has a view without input ownership");
    local->player.actor = player;
    qa_app_application_q3_client *q3 = qa_remote_presentation_q3_client(qa_app_remote_seat_view_presentation(peer->view));
    if (q3 != NULL) {
      qa_app_q3_command_selection selection = qa_app_application_q3_client_user_command_selection(q3);
      qa_app_application_input_set_q3_command_selection(app->controls, qa_session_seat_id(peer->seat), selection);
      const qa_q3_weapon_item *item = qa_q3_get_weapon_item(selection.weapon);
      qa_app_arsenal_selection arsenal = {qa_app_loaded_content_recipe(qa_remote_application_content(app))->inventory.provider,
          item != NULL ? item->item : NULL};
      qa_app_application_input_set_arsenal_selection(app->controls, qa_session_seat_id(peer->seat), &arsenal);
    }
    qa_actor_command command = qa_app_application_input_build_for_seat(app->controls, qa_session_seat_id(peer->seat),
        qa_app_remote_seat_channel_frame_milliseconds(peer->channel), qa_app_remote_seat_channel_elapsed_milliseconds(peer->channel),
        output->snapshot.frame.frame, wall_elapsed);
    qa_app_remote_seat_channel_submit(peer->channel, command, now);
  }
  qa_cleanup_pop(&guard);
  free(peers);
  qa_remote_application_poll_remote_seats(app, now);
}
