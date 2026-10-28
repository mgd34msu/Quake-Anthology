/* Port of src/app/bootstrap/remote-seats.ts. PresentationTime
 * (frame-clock.ts) and frame-time helpers (frame-time.ts) are sibling
 * bootstrap slices (unlanded): mirrored as host fn+ctx. Network /
 * presentation use the remote-seat-source mirrors. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_REMOTE_SEATS_H
#define QA_APP_BOOTSTRAP_REMOTE_SEATS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/remote_seat_source.h"
#include "qa/contracts/session.h"
#include "qa/core/cvars/index.h"
#include "qa/world/session/session.h"
/* Mirror: TS PresentationTime (src/app/bootstrap/frame-clock.ts). */
typedef struct qa_app_presentation_time_mirror {
  void *ctx;
  double (*milliseconds)(void *ctx);
  void (*advance)(void *ctx, double wall_now, double wall_elapsed, double source_elapsed);
} qa_app_presentation_time_mirror;
/* Mirror: TS readFrameTimeControls/sourceFrameMilliseconds (frame-time.ts). */
typedef struct qa_app_frame_time_mirror {
  void *ctx;
  double (*source_frame_milliseconds)(void *ctx, qa_cvar_registry *cvars, double wall_elapsed);
} qa_app_frame_time_mirror;
typedef struct qa_app_remote_seat_services {
  qa_app_remote_seat_network_mirror network;
  qa_app_remote_seat_presentation_mirror remote;
  qa_cvar_registry *cvars;
} qa_app_remote_seat_services;
typedef struct qa_app_remote_seat_channel qa_app_remote_seat_channel;
qa_app_remote_seat_channel *qa_app_remote_seat_channel_create(qa_session_seat *seat, qa_app_presentation_time_mirror clock,
                                                              qa_app_remote_seat_services services, qa_app_frame_time_mirror frame_time);
void qa_app_remote_seat_channel_destroy(qa_app_remote_seat_channel *c);
qa_session_seat *qa_app_remote_seat_channel_seat(const qa_app_remote_seat_channel *c);
void qa_app_remote_seat_channel_adopt_cvars(qa_app_remote_seat_channel *c, qa_cvar_registry *cvars);
double qa_app_remote_seat_channel_elapsed_milliseconds(const qa_app_remote_seat_channel *c);
double qa_app_remote_seat_channel_frame_milliseconds(const qa_app_remote_seat_channel *c);
double qa_app_remote_seat_channel_frames(const qa_app_remote_seat_channel *c);
bool qa_app_remote_seat_channel_active(const qa_app_remote_seat_channel *c);
/* Returns retired connection or NULL (caller owns it, as TS). */
qa_session_connection *qa_app_remote_seat_channel_publish_connection(qa_app_remote_seat_channel *c);
void qa_app_remote_seat_channel_begin_frame(qa_app_remote_seat_channel *c, double wall_now, double wall_elapsed);
void qa_app_remote_seat_channel_poll(qa_app_remote_seat_channel *c, double wall_now);
void qa_app_remote_seat_channel_submit(qa_app_remote_seat_channel *c, qa_actor_command command, double wall_now);
/* Filtered output events are written to caller buffer (capacity = input count). */
qa_simulation_output qa_app_remote_seat_channel_receive(qa_app_remote_seat_channel *c, qa_simulation_output output,
                                                        qa_simulation_event *event_buffer);
void qa_app_remote_seat_channel_close(qa_app_remote_seat_channel *c);
/* TS prepareRemoteSeatChannels: create returns false on failure (host
 * aborts carry the TS error). Channels are caller-owned. */
typedef struct qa_app_remote_seat_channel_factory {
  void *ctx;
  qa_app_remote_seat_services (*create)(void *ctx, qa_session_seat *seat, qa_app_presentation_time_mirror clock);
  qa_app_presentation_time_mirror (*make_clock)(void *ctx);
  qa_app_frame_time_mirror frame_time;
} qa_app_remote_seat_channel_factory;
qa_app_remote_seat_channel **qa_app_prepare_remote_seat_channels(qa_session_seat **seats, size_t seat_count,
                                                                 qa_app_remote_seat_channel_factory factory, size_t *out_count);
#endif
