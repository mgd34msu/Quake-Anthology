/*
 * Port of src/world/session/session.ts: world lifetimes, session
 * clients/seats/connections, and the EngineSession. TS plain Error
 * throws abort identically; RangeError throws use qa_range_error.
 * Cleanup failure aggregation (AggregateError) degenerates to the
 * first fatal failure. stepAsync is synchronous; its progress
 * callback runs inline per completed output.
 * Clients, seats, connections, and worlds are heap objects created
 * and owned by the session (or client); retired bundles returned as
 * qa_session_resource destroy their contents on close. Prepared
 * clients/seats the session still holds at destroy are freed there.
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_SESSION_H
#define QA_WORLD_SESSION_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/render.h"
#include "qa/contracts/session.h"
#include "qa/world/session/clocks.h"
#include "qa/world/session/resources.h"

typedef struct qa_world_lifetime qa_world_lifetime;
typedef struct qa_session_connection qa_session_connection;
typedef struct qa_session_client qa_session_client;
typedef struct qa_session_seat qa_session_seat;
typedef struct qa_engine_session qa_engine_session;

/** TS WorldLifetime. The simulation is copied (fn table + ctx). */
qa_world_lifetime *qa_world_lifetime_create(qa_simulation simulation);
void qa_world_lifetime_destroy(qa_world_lifetime *world);
qa_simulation qa_world_lifetime_simulation(const qa_world_lifetime *world);
qa_resource_scope *qa_world_lifetime_resources(qa_world_lifetime *world);
qa_provider_runtime_state *qa_world_lifetime_providers(
    qa_world_lifetime *world);
bool qa_world_lifetime_is_closed(const qa_world_lifetime *world);
void qa_world_lifetime_close(qa_world_lifetime *world);
qa_session_resource qa_world_lifetime_as_resource(qa_world_lifetime *world);

/** TS SessionConnection (a ResourceScope). kind: "loopback" |
 * "remote" | "demo". */
qa_session_connection *qa_session_connection_create(qa_client_id client,
                                                    const char *kind);
void qa_session_connection_destroy(qa_session_connection *connection);
qa_client_id qa_session_connection_client(
    const qa_session_connection *connection);
const char *qa_session_connection_kind(
    const qa_session_connection *connection);
qa_resource_scope *qa_session_connection_scope(
    qa_session_connection *connection);
void qa_session_connection_close(qa_session_connection *connection);

/** TS SessionClient. */
qa_session_client *qa_session_client_create(qa_client_id id);
void qa_session_client_destroy(qa_session_client *client);
qa_client_id qa_session_client_id(const qa_session_client *client);
qa_resource_scope *qa_session_client_resources(qa_session_client *client);
bool qa_session_client_is_closed(const qa_session_client *client);
qa_resource_scope *qa_session_client_world_resources(
    qa_session_client *client);
/* NULL when not connected. */
qa_session_connection *qa_session_client_connection(
    qa_session_client *client);
void qa_session_client_validate_seat_binding(qa_session_client *client,
                                             qa_session_seat *seat);
void qa_session_client_bind_seat(qa_session_client *client,
                                 qa_session_seat *seat);
qa_session_connection *qa_session_client_connect(qa_session_client *client,
                                                 const char *kind);
/* retired (nullable) is written through the out param; the caller
 * owns closing/freeing it, as the TS returns it unclosed. */
qa_session_connection *qa_session_client_replace_connection(
    qa_session_client *client, const char *kind,
    qa_session_connection **out_retired);
void qa_session_client_disconnect(qa_session_client *client);
void qa_session_client_clear_world(qa_session_client *client);
/* Returns the previous scope (caller closes/frees it). */
qa_resource_scope *qa_session_client_replace_world_resources(
    qa_session_client *client);
void qa_session_client_close(qa_session_client *client);
qa_session_resource qa_session_client_as_resource(qa_session_client *client);

/** TS SessionSeat. */
qa_session_seat *qa_session_seat_create(qa_seat_id id,
                                        qa_session_client *client);
void qa_session_seat_destroy(qa_session_seat *seat);
qa_seat_id qa_session_seat_id(const qa_session_seat *seat);
qa_session_client *qa_session_seat_client(const qa_session_seat *seat);
qa_resource_scope *qa_session_seat_resources(qa_session_seat *seat);
bool qa_session_seat_is_closed(const qa_session_seat *seat);
/* False is the TS null presentation. */
bool qa_session_seat_presentation(qa_session_seat *seat,
                                  qa_seat_presentation *out);
qa_resource_scope *qa_session_seat_attach_presentation(
    qa_session_seat *seat, qa_seat_presentation presentation,
    qa_session_cleanup_fn cleanup, void *cleanup_ctx);
void qa_session_seat_validate_presentation(
    qa_session_seat *seat, qa_seat_presentation presentation);
/* Replaces the lifetime; returns the previous scope or NULL. A NULL
 * presentation clears it. */
qa_resource_scope *qa_session_seat_replace_presentation(
    qa_session_seat *seat, const qa_seat_presentation *presentation,
    qa_resource_scope *resources);
void qa_session_seat_clear_presentation(qa_session_seat *seat);
void qa_session_seat_receive(qa_session_seat *seat,
                             const qa_simulation_event *events,
                             size_t event_count);
qa_render_frame qa_session_seat_present(qa_session_seat *seat,
                                        qa_world_snapshot snapshot,
                                        qa_renderer_backend backend);
void qa_session_seat_close(qa_session_seat *seat);
qa_session_resource qa_session_seat_as_resource(qa_session_seat *seat);

bool qa_event_targets_seat(qa_simulation_event event, qa_seat_id seat,
                           qa_client_id client);

/** TS SessionMode. */
typedef enum qa_session_mode_kind {
  QA_SESSION_MODE_HEADLESS, QA_SESSION_MODE_LOCAL
} qa_session_mode_kind;
typedef struct qa_session_mode { qa_session_mode_kind kind; } qa_session_mode;

/** One replaceWorld presentation entry. */
typedef struct qa_session_presentation_entry {
  qa_session_seat *seat;
  qa_seat_presentation presentation;
  qa_session_cleanup_fn cleanup;
  void *cleanup_ctx;
} qa_session_presentation_entry;
typedef struct qa_session_client_change {
  qa_session_client **added;
  size_t added_count;
  qa_session_client **removed;
  size_t removed_count;
} qa_session_client_change;
typedef struct qa_session_seat_change {
  qa_session_seat **added;
  size_t added_count;
  qa_session_seat **removed;
  size_t removed_count;
} qa_session_seat_change;

typedef void (*qa_session_progress_fn)(void *ctx,
                                       qa_simulation_progress frame);

/** TS EngineSession. The identity owner is borrowed. */
qa_engine_session *qa_engine_session_create(
    const qa_identity_owner *identity, qa_session_mode mode);
void qa_engine_session_destroy(qa_engine_session *session);
qa_session_id qa_engine_session_id(const qa_engine_session *session);
qa_resource_scope *qa_engine_session_resources(qa_engine_session *session);
bool qa_engine_session_is_closed(const qa_engine_session *session);
qa_world_lifetime *qa_engine_session_world(qa_engine_session *session);
bool qa_engine_session_snapshot(qa_engine_session *session,
                                qa_world_snapshot *out);
/* NULL is the TS null. */
qa_session_client *qa_engine_session_client_at(qa_engine_session *session,
                                               int64_t slot);
qa_world_lifetime *qa_engine_session_attach_world(
    qa_engine_session *session, qa_simulation simulation);
void qa_engine_session_validate_world_replacement(
    qa_engine_session *session, qa_simulation simulation,
    const qa_session_presentation_entry *presentations,
    size_t presentation_count, qa_session_client_change clients,
    qa_session_seat_change seats);
/* world is written to *out_world; the retired bundle destroys the
 * replaced world/seats/clients/scopes on close. */
qa_session_resource qa_engine_session_replace_world(
    qa_engine_session *session, qa_simulation simulation,
    const qa_session_presentation_entry *presentations,
    size_t presentation_count, qa_session_client_change clients,
    qa_session_seat_change seats, qa_world_lifetime **out_world);
void qa_engine_session_validate_local_seats(
    qa_engine_session *session,
    const qa_session_presentation_entry *presentations,
    size_t presentation_count, qa_session_client_change clients,
    qa_session_seat_change seats);
qa_session_resource qa_engine_session_publish_local_seats(
    qa_engine_session *session,
    const qa_session_presentation_entry *presentations,
    size_t presentation_count, qa_session_client_change clients,
    qa_session_seat_change seats);
qa_session_resource qa_engine_session_detach_world(
    qa_engine_session *session);
void qa_engine_session_close_world(qa_engine_session *session);
/* retiring may be NULL (TS undefined). */
qa_session_client *qa_engine_session_prepare_client(
    qa_engine_session *session, int64_t slot,
    qa_session_client *retiring);
qa_session_client *qa_engine_session_create_client(
    qa_engine_session *session, int64_t slot);
void qa_engine_session_close_client(qa_engine_session *session,
                                    qa_client_id id);
qa_session_seat *qa_engine_session_create_seat(qa_engine_session *session,
                                               int64_t index,
                                               qa_session_client *client);
qa_session_seat *qa_engine_session_prepare_seat(
    qa_engine_session *session, int64_t index, qa_session_client *client,
    qa_session_seat *retiring);
void qa_engine_session_close_seat(qa_engine_session *session,
                                  qa_seat_id id);
qa_simulation_output qa_engine_session_step(qa_engine_session *session,
                                            qa_input_batch input);
/* progress may be NULL. The returned output may borrow a scratch
 * event array owned by the session when progress outputs merged. */
qa_simulation_output qa_engine_session_step_async(
    qa_engine_session *session, qa_input_batch input,
    qa_session_progress_fn progress, void *progress_ctx);
void qa_engine_session_publish(qa_engine_session *session,
                               qa_simulation_output output);
void qa_engine_session_close(qa_engine_session *session);
qa_session_resource qa_engine_session_as_resource(
    qa_engine_session *session);

#endif
