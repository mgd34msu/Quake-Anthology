/* Port of src/app/bootstrap/remote-seats.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/remote_seats.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
struct qa_app_remote_seat_channel {
  qa_session_seat *seat;
  qa_app_presentation_time_mirror clock;
  qa_app_remote_seat_services services;
  qa_app_frame_time_mirror frame_time;
  qa_cvar_registry *source_cvars;
  qa_session_connection *connection;
  double source_elapsed, source_frame_elapsed, frame_number;
  bool closed;
};
qa_app_remote_seat_channel *qa_app_remote_seat_channel_create(qa_session_seat *seat, qa_app_presentation_time_mirror clock,
                                                              qa_app_remote_seat_services services, qa_app_frame_time_mirror frame_time) {
  qa_app_remote_seat_channel *c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->seat = seat; c->clock = clock; c->services = services; c->frame_time = frame_time;
  c->source_cvars = services.cvars;
  if (services.remote.client != qa_session_seat_client(seat)) {
    free(c);
    qa_range_error("Remote channel belongs to another session client");
  }
  return c;
}
void qa_app_remote_seat_channel_destroy(qa_app_remote_seat_channel *c) {
  if (!c) return;
  qa_app_remote_seat_channel_close(c);
  free(c);
}
qa_session_seat *qa_app_remote_seat_channel_seat(const qa_app_remote_seat_channel *c) { return c->seat; }
void qa_app_remote_seat_channel_adopt_cvars(qa_app_remote_seat_channel *c, qa_cvar_registry *cvars) {
  if (qa_cvar_registry_context(cvars).session.token != qa_session_seat_id(c->seat).session.token)
    qa_range_error("Remote clock registry belongs to another session");
  c->source_cvars = cvars;
}
double qa_app_remote_seat_channel_elapsed_milliseconds(const qa_app_remote_seat_channel *c) { return c->source_elapsed; }
double qa_app_remote_seat_channel_frame_milliseconds(const qa_app_remote_seat_channel *c) { return c->source_frame_elapsed; }
double qa_app_remote_seat_channel_frames(const qa_app_remote_seat_channel *c) { return c->frame_number; }
bool qa_app_remote_seat_channel_active(const qa_app_remote_seat_channel *c) {
  const char *phase = c->services.network.phase ? c->services.network.phase(c->services.network.ctx) : "";
  return !c->closed && !qa_session_seat_is_closed(c->seat) && strcmp(phase, "active") == 0;
}
qa_session_connection *qa_app_remote_seat_channel_publish_connection(qa_app_remote_seat_channel *c) {
  qa_session_client *client = qa_session_seat_client(c->seat);
  if (c->closed || qa_session_seat_is_closed(c->seat) || qa_session_client_is_closed(client))
    qa_range_error("Remote seat has retired");
  if (c->connection != NULL) qa_range_error("Remote seat connection is already published");
  qa_session_connection *retired = NULL;
  c->connection = qa_session_client_replace_connection(client, "remote", &retired);
  return retired;
}
void qa_app_remote_seat_channel_begin_frame(qa_app_remote_seat_channel *c, double wall_now, double wall_elapsed) {
  if (c->closed) qa_range_error("Remote channel is closed");
  c->source_frame_elapsed = c->frame_time.source_frame_milliseconds(c->frame_time.ctx, c->source_cvars, wall_elapsed);
  c->source_elapsed += c->source_frame_elapsed;
  c->clock.advance(c->clock.ctx, wall_now, wall_elapsed, c->source_frame_elapsed);
  c->frame_number++;
}
void qa_app_remote_seat_channel_poll(qa_app_remote_seat_channel *c, double wall_now) {
  if (c->closed) return;
  c->services.network.poll(c->services.network.ctx, wall_now);
}
void qa_app_remote_seat_channel_submit(qa_app_remote_seat_channel *c, qa_actor_command command, double wall_now) {
  qa_command_source source = command.source;
  if (source.kind != QA_COMMAND_SOURCE_LOCAL_SEAT || !qa_same_seat(source.seat, qa_session_seat_id(c->seat))
      || !qa_same_client(source.client, qa_session_client_id(qa_session_seat_client(c->seat))))
    qa_range_error("Remote command belongs to another seat");
  if (!c->services.remote.has_player || !qa_same_actor(c->services.remote.player_actor, command.actor))
    qa_range_error("Remote command belongs to another admitted actor");
  if (!qa_app_remote_seat_channel_active(c)) qa_range_error("Remote command has no active connection");
  const qa_actor_command *one = &command;
  if (c->services.network.family == QA_APP_REMOTE_FAMILY_UNIFIED)
    c->services.network.submit_timed(c->services.network.ctx, (const void *const *)&one, 1, wall_now, c->source_frame_elapsed);
  else
    c->services.network.submit(c->services.network.ctx, (const void *const *)&one, 1, wall_now);
}
qa_simulation_output qa_app_remote_seat_channel_receive(qa_app_remote_seat_channel *c, qa_simulation_output output,
                                                        qa_simulation_event *event_buffer) {
  if (output.snapshot.session.token != qa_session_seat_id(c->seat).session.token)
    qa_range_error("Remote snapshot belongs to another session");
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
  if (c->services.network.close) c->services.network.close(c->services.network.ctx);
  qa_session_client *client = qa_session_seat_client(c->seat);
  if (c->connection != NULL && qa_session_client_connection(client) == c->connection)
    qa_session_client_disconnect(client);
  c->connection = NULL;
}
qa_app_remote_seat_channel **qa_app_prepare_remote_seat_channels(qa_session_seat **seats, size_t seat_count,
                                                                 qa_app_remote_seat_channel_factory factory, size_t *out_count) {
  if (seat_count < 1 || seat_count > 4) qa_range_error("Remote play requires one to four local seats");
  for (size_t i = 0; i < seat_count; i++) {
    bool bad = qa_session_seat_is_closed(seats[i]) || qa_session_client_is_closed(qa_session_seat_client(seats[i]));
    for (size_t j = 0; j < i; j++)
      if (qa_same_seat(qa_session_seat_id(seats[j]), qa_session_seat_id(seats[i]))
          || qa_same_client(qa_session_client_id(qa_session_seat_client(seats[j])), qa_session_client_id(qa_session_seat_client(seats[i]))))
        bad = true;
    if (bad) qa_range_error("Remote channels require distinct live seats and clients");
  }
  qa_app_remote_seat_channel **channels = calloc(seat_count, sizeof *channels);
  if (!channels) qa_range_error("Out of memory");
  for (size_t i = 0; i < seat_count; i++) {
    qa_app_presentation_time_mirror clock = factory.make_clock(factory.ctx);
    qa_app_remote_seat_services services = factory.create(factory.ctx, seats[i], clock);
    channels[i] = qa_app_remote_seat_channel_create(seats[i], clock, services, factory.frame_time);
    if (channels[i] == NULL) {
      if (services.network.close) services.network.close(services.network.ctx);
      for (size_t j = i; j-- > 0;) qa_app_remote_seat_channel_destroy(channels[j]);
      free(channels);
      qa_range_error("Out of memory");
    }
  }
  *out_count = seat_count;
  return channels;
}
