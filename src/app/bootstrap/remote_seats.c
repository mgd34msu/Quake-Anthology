/* Port of src/app/bootstrap/remote-seats.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/remote_seats.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/frame_time.h"
#include "qa/foundation/support.h"
struct qa_app_remote_seat_channel {
  qa_session_seat *seat;
  qa_app_presentation_time *clock;
  qa_app_remote_seat_services services;
  qa_cvar_registry *source_cvars;
  qa_session_connection *connection;
  double source_elapsed, source_frame_elapsed, frame_number;
  bool closed;
};
qa_app_remote_seat_channel *qa_app_remote_seat_channel_create(qa_session_seat *seat, qa_app_presentation_time *clock,
                                                              qa_app_remote_seat_services services) {
  qa_app_remote_seat_channel *c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->seat = seat; c->clock = clock; c->services = services;
  c->source_cvars = services.cvars;
  if (!qa_same_client(qa_app_remote_seat_presentation_client(services.remote),
          qa_session_client_id(qa_session_seat_client(seat)))) {
    free(c->clock); free(c);
    qa_raise("Error", 0, 0, "Remote channel belongs to another session client");
  }
  return c;
}
void qa_app_remote_seat_channel_destroy(qa_app_remote_seat_channel *c) {
  if (!c) return;
  qa_app_remote_seat_channel_close(c);
  free(c->clock);
  free(c);
}
qa_app_presentation_time *qa_app_remote_seat_channel_clock(const qa_app_remote_seat_channel *c) { return c->clock; }
qa_session_seat *qa_app_remote_seat_channel_seat(const qa_app_remote_seat_channel *c) { return c->seat; }
void qa_app_remote_seat_channel_adopt_cvars(qa_app_remote_seat_channel *c, qa_cvar_registry *cvars) {
  if (qa_cvar_registry_context(cvars).session.token != qa_session_seat_id(c->seat).session.token)
    qa_raise("Error", 0, 0, "Remote clock registry belongs to another session");
  c->source_cvars = cvars;
}
double qa_app_remote_seat_channel_elapsed_milliseconds(const qa_app_remote_seat_channel *c) { return c->source_elapsed; }
double qa_app_remote_seat_channel_frame_milliseconds(const qa_app_remote_seat_channel *c) { return c->source_frame_elapsed; }
double qa_app_remote_seat_channel_frames(const qa_app_remote_seat_channel *c) { return c->frame_number; }
bool qa_app_remote_seat_channel_active(const qa_app_remote_seat_channel *c) {
  return !c->closed && !qa_session_seat_is_closed(c->seat)
      && qa_app_remote_seat_network_phase(c->services.network) == QA_APP_PHASE_ACTIVE;
}
qa_session_connection *qa_app_remote_seat_channel_publish_connection(qa_app_remote_seat_channel *c) {
  qa_session_client *client = qa_session_seat_client(c->seat);
  if (c->closed || qa_session_seat_is_closed(c->seat) || qa_session_client_is_closed(client))
    qa_raise("Error", 0, 0, "Remote seat has retired");
  if (c->connection != NULL) qa_raise("Error", 0, 0, "Remote seat connection is already published");
  qa_session_connection *retired = NULL;
  c->connection = qa_session_client_replace_connection(client, "remote", &retired);
  return retired;
}
void qa_app_remote_seat_channel_begin_frame(qa_app_remote_seat_channel *c, double wall_now, double wall_elapsed) {
  if (c->closed) qa_raise("Error", 0, 0, "Remote channel is closed");
  qa_command_dialect dialect = qa_cvar_registry_dialect(c->source_cvars);
  double raw_milliseconds = wall_elapsed;
  qa_app_frame_time_controls controls = qa_app_read_frame_time_controls(c->source_cvars);
  c->source_frame_elapsed = qa_app_source_frame_milliseconds(dialect, raw_milliseconds,
      controls, (qa_app_frame_host){false, false});
  c->source_elapsed += c->source_frame_elapsed;
  qa_app_presentation_time_advance(c->clock, wall_now, wall_elapsed, c->source_frame_elapsed);
  c->frame_number++;
}
void qa_app_remote_seat_channel_poll(qa_app_remote_seat_channel *c, double wall_now) {
  if (c->closed) return;
  qa_app_remote_seat_network_poll(c->services.network, wall_now);
}
void qa_app_remote_seat_channel_submit(qa_app_remote_seat_channel *c, qa_actor_command command, double wall_now) {
  qa_command_source source = command.source;
  if (source.kind != QA_COMMAND_SOURCE_LOCAL_SEAT || !qa_same_seat(source.seat, qa_session_seat_id(c->seat))
      || !qa_same_client(source.client, qa_session_client_id(qa_session_seat_client(c->seat))))
    qa_raise("Error", 0, 0, "Remote command belongs to another seat");
  qa_actor_id player;
  if (!qa_app_remote_seat_presentation_player(c->services.remote, &player) || !qa_same_actor(player, command.actor))
    qa_raise("Error", 0, 0, "Remote command belongs to another admitted actor");
  if (!qa_app_remote_seat_channel_active(c)) qa_raise("Error", 0, 0, "Remote command has no active connection");
  if (c->services.network->family == QA_APP_REMOTE_FAMILY_UNIFIED)
    qa_app_unified_client_network_submit_timed(c->services.network->unified, &command, 1, wall_now, c->source_frame_elapsed);
  else
    qa_app_remote_seat_network_submit(c->services.network, &command, 1, wall_now);
}
qa_simulation_output qa_app_remote_seat_channel_receive(qa_app_remote_seat_channel *c, qa_simulation_output output,
                                                        qa_simulation_event *event_buffer) {
  if (output.snapshot.session.token != qa_session_seat_id(c->seat).session.token)
    qa_raise("Error", 0, 0, "Remote snapshot belongs to another session");
  size_t n = 0;
  for (size_t i = 0; i < output.event_count; i++) {
    qa_simulation_event event = output.events[i];
    if (!qa_event_targets_seat(event, qa_session_seat_id(c->seat), qa_session_client_id(qa_session_seat_client(c->seat)))) continue;
    event.audience.kind = "seat";
    event.audience.seat = qa_session_seat_id(c->seat);
    event_buffer[n++] = event;
  }
  qa_session_seat_receive(c->seat, event_buffer, n);
  qa_simulation_output out = {output.snapshot, event_buffer, n};
  return out;
}
void qa_app_remote_seat_channel_close(qa_app_remote_seat_channel *c) {
  if (c->closed) return;
  c->closed = true;
  qa_app_remote_seat_network_close(c->services.network);
  qa_session_client *client = qa_session_seat_client(c->seat);
  if (c->connection != NULL && qa_session_client_connection(client) == c->connection)
    qa_session_client_disconnect(client);
  c->connection = NULL;
}
typedef struct prepare_state {
  qa_session_seat **seats; qa_app_remote_seat_channel_factory factory;
  qa_app_remote_seat_channel **channels; size_t created, count;
} prepare_state;
typedef struct construct_state {
  qa_session_seat *seat; qa_app_presentation_time *clock; qa_app_remote_seat_services services;
  qa_app_remote_seat_channel *channel;
} construct_state;
static void construct_body(void *ctx) {
  construct_state *state = ctx;
  state->channel = qa_app_remote_seat_channel_create(state->seat, state->clock, state->services);
  if (state->channel == NULL) qa_range_error("Out of memory");
}
static void close_network(void *ctx) { qa_app_remote_seat_network_close(ctx); }
static void close_channel(void *ctx) { qa_app_remote_seat_channel_close(ctx); }
static void prepare_body(void *ctx) {
  prepare_state *state = ctx;
  for (size_t i = 0; i < state->count; i++) {
    qa_app_presentation_time *clock = calloc(1, sizeof *clock);
    if (!clock) qa_range_error("Out of memory");
    qa_app_presentation_time_init(clock);
    qa_cleanup clock_guard;
    qa_cleanup_push(&clock_guard, free, clock);
    qa_app_remote_seat_services services = state->factory.create(state->factory.ctx, state->seats[i], clock);
    qa_cleanup_pop(&clock_guard);
    /* TS: try { new RemoteSeatChannel } catch { await services.network.close(); throw } (the channel
     * constructor frees the clock it was given when it raises) */
    construct_state construct = {state->seats[i], clock, services, NULL};
    qa_caught_error caught;
    if (!qa_try(construct_body, &construct, &caught)) {
      qa_caught_error cleanup;
      if (!qa_try(close_network, (void *)services.network, &cleanup))
        qa_raise("AggregateError", 0, 0, "Remote seat construction cleanup failed");
      qa_rethrow(&caught);
    }
    state->channels[state->created++] = construct.channel;
  }
}
qa_app_remote_seat_channel **qa_app_prepare_remote_seat_channels(qa_session_seat **seats, size_t seat_count,
                                                                 qa_app_remote_seat_channel_factory factory, size_t *out_count) {
  if (seat_count < 1 || seat_count > 4) qa_raise("Error", 0, 0, "Remote play requires one to four local seats");
  for (size_t i = 0; i < seat_count; i++) {
    bool bad = qa_session_seat_is_closed(seats[i]) || qa_session_client_is_closed(qa_session_seat_client(seats[i]));
    for (size_t j = 0; j < i; j++)
      if (qa_same_seat(qa_session_seat_id(seats[j]), qa_session_seat_id(seats[i]))
          || qa_same_client(qa_session_client_id(qa_session_seat_client(seats[j])), qa_session_client_id(qa_session_seat_client(seats[i]))))
        bad = true;
    if (bad) qa_raise("Error", 0, 0, "Remote channels require distinct live seats and clients");
  }
  qa_app_remote_seat_channel **channels = calloc(seat_count, sizeof *channels);
  if (!channels) qa_range_error("Out of memory");
  prepare_state state = {seats, factory, channels, 0, seat_count};
  qa_caught_error caught;
  if (!qa_try(prepare_body, &state, &caught)) {
    /* TS: close the created channels newest first; a failed cleanup makes an AggregateError */
    bool cleanup_failed = false;
    for (size_t j = state.created; j-- > 0;) {
      qa_caught_error cleanup;
      if (!qa_try(close_channel, channels[j], &cleanup)) cleanup_failed = true;
      qa_app_remote_seat_channel_destroy(channels[j]);
    }
    free(channels);
    if (cleanup_failed) qa_raise("AggregateError", 0, 0, "Remote channel preparation failed");
    qa_rethrow(&caught);
  }
  *out_count = seat_count;
  return channels;
}
