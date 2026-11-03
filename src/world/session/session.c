/* Port of src/world/session/session.ts. GPL-2.0-or-later. */
#include "qa/world/session/session.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/world/handles.h"

/* TS `throw new Error(...)`: a catchable raise (aborts only uncaught). */
static noreturn void qa_errorf(const char *format, ...) {
  char message[2048];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(message, sizeof message, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}
static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static void *xmalloc(size_t size) {
  void *ptr = malloc(size != 0 ? size : 1);
  if (ptr == NULL) abort();
  return ptr;
}
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}
static qa_resource_scope *new_scope(const char *name) {
  qa_resource_scope *scope = qa_resource_scope_create(name);
  if (scope == NULL) abort();
  return scope;
}

/* TS closeAll(resources, message): every resource closes in its own try;
 * failures become one AggregateError carrying the site's message. The collect
 * form returns the failure count so a caller can free memory before raising. */
static void resource_close_call(void *ctx) {
  const qa_session_resource *resource = ctx;
  resource->close(resource->ctx);
}
static size_t close_all_collect(const qa_session_resource *resources, size_t count) {
  size_t errors = 0;
  for (size_t i = 0; i < count; i++) {
    qa_caught_error caught;
    if (!qa_try(resource_close_call, (void *)&resources[i], &caught)) errors++;
  }
  return errors;
}
static void raise_aggregate(size_t errors, const char *message) {
  if (errors > 0) qa_raise("AggregateError", 0, 0, "%s", message);
}
static void close_all(const qa_session_resource *resources, size_t count, const char *message) {
  raise_aggregate(close_all_collect(resources, count), message);
}
static void scope_close_resource(void *ctx) { qa_resource_scope_close(ctx); }
static qa_session_resource scope_resource(qa_resource_scope *scope) {
  qa_session_resource resource = {scope, scope_close_resource};
  return resource;
}
/* Closes then frees a scope whose owner dropped it (TS lets the GC have it). */
static void scope_dispose(qa_resource_scope *scope) {
  if (scope == NULL) return;
  qa_caught_error caught;
  bool ok = qa_try(scope_close_resource, scope, &caught);
  qa_resource_scope_destroy(scope);
  if (!ok) qa_rethrow(&caught);
}

/* ---------- WorldLifetime ---------- */

struct qa_world_lifetime {
  qa_simulation simulation;
  qa_resource_scope *resources;
  bool closed;
};

qa_world_lifetime *qa_world_lifetime_create(qa_simulation simulation) {
  qa_world_lifetime *world = calloc(1, sizeof *world);
  if (world == NULL) return NULL;
  world->simulation = simulation;
  world->resources = new_scope("World resources");
  return world;
}
void qa_world_lifetime_destroy(qa_world_lifetime *world) {
  if (world == NULL) return;
  qa_world_lifetime_close(world);
  qa_resource_scope_destroy(world->resources);
  free(world);
}
qa_simulation qa_world_lifetime_simulation(const qa_world_lifetime *world) {
  return world->simulation;
}
qa_resource_scope *qa_world_lifetime_resources(qa_world_lifetime *world) {
  return world->resources;
}
bool qa_world_lifetime_is_closed(const qa_world_lifetime *world) {
  return world->closed;
}
void qa_world_lifetime_close(qa_world_lifetime *world) {
  if (world->closed) return;
  world->closed = true;
  qa_session_resource resources[2] = {
      {world->simulation.ctx, world->simulation.close},
      scope_resource(world->resources)};
  close_all(resources, 2, "World shutdown failed");
}
static void world_close_ctx(void *ctx) {
  qa_world_lifetime_close((qa_world_lifetime *)ctx);
}
qa_session_resource qa_world_lifetime_as_resource(qa_world_lifetime *world) {
  qa_session_resource resource = {world, world_close_ctx};
  return resource;
}

/* ---------- SessionConnection ---------- */

struct qa_session_connection {
  qa_client_id client;
  const char *kind;
  qa_resource_scope *scope;
};

qa_session_connection *qa_session_connection_create(qa_client_id client,
                                                    const char *kind) {
  qa_session_connection *connection = calloc(1, sizeof *connection);
  if (connection == NULL) return NULL;
  connection->client = client;
  connection->kind = kind;
  char name[96];
  snprintf(name, sizeof name, "Client %lld %s connection",
           (long long)client.slot, kind);
  connection->scope = new_scope(name);
  return connection;
}
void qa_session_connection_destroy(qa_session_connection *connection) {
  if (connection == NULL) return;
  qa_resource_scope_destroy(connection->scope);
  free(connection);
}
qa_client_id qa_session_connection_client(
    const qa_session_connection *connection) {
  return connection->client;
}
const char *qa_session_connection_kind(
    const qa_session_connection *connection) {
  return connection->kind;
}
qa_resource_scope *qa_session_connection_scope(
    qa_session_connection *connection) {
  return connection->scope;
}
void qa_session_connection_close(qa_session_connection *connection) {
  qa_resource_scope_close(connection->scope);
}

/* ---------- SessionClient / SessionSeat ---------- */

struct qa_session_client {
  qa_client_id id;
  qa_resource_scope *resources;
  qa_resource_scope *active_resources; /* NULL after final disconnect */
  qa_session_seat **seats;
  size_t seat_count;
  size_t seat_capacity;
  qa_session_connection *active_connection;
};

struct qa_session_seat {
  qa_seat_id id;
  qa_session_client *client;
  qa_resource_scope *resources;
  bool has_presentation;
  qa_seat_presentation presentation;
  qa_resource_scope *presentation_resources;
};

static void client_close_active(void *ctx) {
  qa_session_client *client = ctx;
  if (client->active_resources != NULL)
    qa_resource_scope_close(client->active_resources);
}
static void client_disconnect_cb(void *ctx) {
  qa_session_client_disconnect((qa_session_client *)ctx);
}

qa_session_client *qa_session_client_create(qa_client_id id) {
  qa_session_client *client = calloc(1, sizeof *client);
  if (client == NULL) return NULL;
  client->id = id;
  client->resources = new_scope("Client resources");
  client->active_resources = new_scope("Client world resources");
  qa_resource_scope_defer(client->resources, client_close_active, client);
  qa_resource_scope_defer(client->resources, client_disconnect_cb, client);
  return client;
}
void qa_session_client_destroy(qa_session_client *client) {
  if (client == NULL) return;
  qa_session_client_close(client);
  if (client->active_resources != NULL)
    qa_resource_scope_destroy(client->active_resources);
  qa_session_connection_destroy(client->active_connection);
  qa_resource_scope_destroy(client->resources);
  free(client->seats);
  free(client);
}
qa_client_id qa_session_client_id(const qa_session_client *client) {
  return client->id;
}
qa_resource_scope *qa_session_client_resources(qa_session_client *client) {
  return client->resources;
}
bool qa_session_client_is_closed(const qa_session_client *client) {
  return qa_resource_scope_is_closed(client->resources);
}
qa_resource_scope *qa_session_client_world_resources(
    qa_session_client *client) {
  qa_resource_scope_assert_open(client->resources);
  return client->active_resources;
}
qa_session_connection *qa_session_client_connection(
    qa_session_client *client) {
  return client->active_connection;
}

void qa_session_client_validate_seat_binding(qa_session_client *client,
                                             qa_session_seat *seat) {
  qa_resource_scope_assert_open(client->resources);
  qa_resource_scope_assert_open(seat->resources);
  if (seat->client != client)
    qa_range_error("Seat belongs to another client");
  for (size_t i = 0; i < client->seat_count; i++)
    if (client->seats[i] == seat) qa_errorf("Seat is already bound");
}

struct seat_unbind {
  qa_session_client *client;
  qa_session_seat *seat;
};
static void seat_unbind_cb(void *ctx) {
  struct seat_unbind *unbind = ctx;
  qa_session_client *client = unbind->client;
  for (size_t i = 0; i < client->seat_count; i++)
    if (client->seats[i] == unbind->seat) {
      memmove(&client->seats[i], &client->seats[i + 1],
              (client->seat_count - i - 1) * sizeof *client->seats);
      client->seat_count--;
      break;
    }
  free(unbind);
}
static void close_bound_seat(void *ctx) {
  struct seat_unbind owned = *(struct seat_unbind *)ctx;
  free(ctx);
  for (size_t i = 0; i < owned.client->seat_count; i++)
    if (owned.client->seats[i] == owned.seat) {
      qa_session_seat_close(owned.seat);
      return;
    }
}
void qa_session_client_bind_seat(qa_session_client *client,
                                 qa_session_seat *seat) {
  qa_session_client_validate_seat_binding(client, seat);
  if (client->seat_count == client->seat_capacity) {
    client->seat_capacity =
        client->seat_capacity != 0 ? client->seat_capacity * 2 : 4;
    client->seats =
        xrealloc(client->seats, client->seat_capacity * sizeof *client->seats);
  }
  client->seats[client->seat_count++] = seat;
  struct seat_unbind *unbind = xmalloc(sizeof *unbind);
  unbind->client = client;
  unbind->seat = seat;
  qa_resource_scope_defer(seat->resources, seat_unbind_cb, unbind);
  /* TS this.resources.own(seat). A seat retired and freed before its client
   * closes has already unbound itself (closing it is then a TS no-op), so the
   * client closes it only while it is still bound. */
  struct seat_unbind *owned = xmalloc(sizeof *owned);
  owned->client = client;
  owned->seat = seat;
  qa_resource_scope_defer(client->resources, close_bound_seat, owned);
}

qa_session_connection *qa_session_client_replace_connection(
    qa_session_client *client, const char *kind,
    qa_session_connection **out_retired) {
  qa_resource_scope_assert_open(client->resources);
  qa_session_connection *retired = client->active_connection;
  qa_session_connection *connection =
      qa_session_connection_create(client->id, kind);
  if (connection == NULL) abort();
  client->active_connection = connection;
  *out_retired = retired;
  return connection;
}
qa_session_connection *qa_session_client_connect(qa_session_client *client,
                                                 const char *kind) {
  qa_resource_scope_assert_open(client->resources);
  qa_session_client_disconnect(client);
  qa_session_connection *retired = NULL;
  return qa_session_client_replace_connection(client, kind, &retired);
}

static void seat_clear_resource(void *ctx) {
  qa_session_seat_clear_presentation((qa_session_seat *)ctx);
}
/* TS clearSeatPresentations: closeAll over a snapshot of the bound seats. */
static void clear_seat_presentations(qa_session_client *client) {
  size_t count = client->seat_count;
  if (count == 0) return;
  qa_session_resource *resources = xmalloc(count * sizeof *resources);
  for (size_t i = 0; i < count; i++) {
    resources[i].ctx = client->seats[i];
    resources[i].close = seat_clear_resource;
  }
  size_t errors = close_all_collect(resources, count);
  free(resources);
  raise_aggregate(errors, "Seat presentation shutdown failed");
}
static void clear_seats_resource(void *ctx) {
  clear_seat_presentations((qa_session_client *)ctx);
}
static void connection_close_resource(void *ctx) {
  qa_session_connection_close((qa_session_connection *)ctx);
}

void qa_session_client_disconnect(qa_session_client *client) {
  qa_session_connection *connection = client->active_connection;
  client->active_connection = NULL;
  qa_resource_scope *active = client->active_resources;
  /* TS keeps the closed scope once the client is closed; C frees it below,
   * so a closed client holds none. */
  client->active_resources =
      qa_session_client_is_closed(client)
          ? NULL
          : new_scope("Client world resources");
  qa_session_resource resources[3];
  size_t count = 0;
  resources[count].ctx = client;
  resources[count++].close = clear_seats_resource;
  if (active != NULL) resources[count++] = scope_resource(active);
  if (connection != NULL) {
    resources[count].ctx = connection;
    resources[count++].close = connection_close_resource;
  }
  size_t errors = close_all_collect(resources, count);
  if (active != NULL) qa_resource_scope_destroy(active);
  qa_session_connection_destroy(connection);
  raise_aggregate(errors, "Client disconnect failed");
}

qa_resource_scope *qa_session_client_replace_world_resources(
    qa_session_client *client) {
  qa_resource_scope_assert_open(client->resources);
  qa_resource_scope *previous = client->active_resources;
  client->active_resources = new_scope("Client world resources");
  return previous;
}
void qa_session_client_clear_world(qa_session_client *client) {
  qa_resource_scope *previous =
      qa_session_client_replace_world_resources(client);
  qa_session_resource resources[2] = {{client, clear_seats_resource},
                                      scope_resource(previous)};
  size_t errors = close_all_collect(resources, 2);
  qa_resource_scope_destroy(previous);
  raise_aggregate(errors, "Client world shutdown failed");
}
void qa_session_client_close(qa_session_client *client) {
  qa_resource_scope_close(client->resources);
}
static void client_close_ctx(void *ctx) {
  qa_session_client_close((qa_session_client *)ctx);
}
qa_session_resource qa_session_client_as_resource(qa_session_client *client) {
  qa_session_resource resource = {client, client_close_ctx};
  return resource;
}

static void seat_clear_cb(void *ctx) {
  qa_session_seat_clear_presentation((qa_session_seat *)ctx);
}

qa_session_seat *qa_session_seat_create(qa_seat_id id,
                                        qa_session_client *client) {
  qa_session_seat *seat = calloc(1, sizeof *seat);
  if (seat == NULL) return NULL;
  seat->id = id;
  seat->client = client;
  seat->resources = new_scope("Seat resources");
  qa_resource_scope_defer(seat->resources, seat_clear_cb, seat);
  return seat;
}
void qa_session_seat_destroy(qa_session_seat *seat) {
  if (seat == NULL) return;
  qa_session_seat_close(seat);
  if (seat->presentation_resources != NULL)
    qa_resource_scope_destroy(seat->presentation_resources);
  qa_resource_scope_destroy(seat->resources);
  free(seat);
}
qa_seat_id qa_session_seat_id(const qa_session_seat *seat) { return seat->id; }
qa_session_client *qa_session_seat_client(const qa_session_seat *seat) {
  return seat->client;
}
qa_resource_scope *qa_session_seat_resources(qa_session_seat *seat) {
  return seat->resources;
}
bool qa_session_seat_is_closed(const qa_session_seat *seat) {
  return qa_resource_scope_is_closed(seat->resources);
}
bool qa_session_seat_presentation(qa_session_seat *seat,
                                  qa_seat_presentation *out) {
  if (!seat->has_presentation) return false;
  *out = seat->presentation;
  return true;
}

qa_resource_scope *qa_session_seat_replace_presentation(
    qa_session_seat *seat, const qa_seat_presentation *presentation,
    qa_resource_scope *resources) {
  qa_resource_scope *previous = seat->has_presentation
                                    ? seat->presentation_resources
                                    : NULL;
  if (presentation == NULL) {
    seat->has_presentation = false;
    seat->presentation_resources = NULL;
  } else {
    seat->has_presentation = true;
    seat->presentation = *presentation;
    seat->presentation_resources = resources;
  }
  return previous;
}

void qa_session_seat_validate_presentation(
    qa_session_seat *seat, qa_seat_presentation presentation) {
  qa_resource_scope_assert_open(seat->resources);
  if (qa_session_client_is_closed(seat->client))
    qa_errorf("Seat client is closed");
  if (!qa_same_seat(presentation.state.seat, seat->id) ||
      !qa_same_client(presentation.state.client, seat->client->id))
    qa_range_error("Presentation belongs to another seat or client");
}

qa_resource_scope *qa_session_seat_attach_presentation(
    qa_session_seat *seat, qa_seat_presentation presentation,
    qa_session_cleanup_fn cleanup, void *cleanup_ctx) {
  qa_session_seat_validate_presentation(seat, presentation);
  char name[64];
  snprintf(name, sizeof name, "Seat %lld presentation",
           (long long)seat->id.index);
  qa_resource_scope *resources = new_scope(name);
  qa_resource_scope_defer(resources, cleanup, cleanup_ctx);
  qa_resource_scope *previous =
      qa_session_seat_replace_presentation(seat, &presentation, resources);
  scope_dispose(previous);
  return resources;
}

void qa_session_seat_clear_presentation(qa_session_seat *seat) {
  qa_resource_scope *previous =
      qa_session_seat_replace_presentation(seat, NULL, NULL);
  scope_dispose(previous);
}

void qa_session_seat_receive(qa_session_seat *seat,
                             const qa_simulation_event *events,
                             size_t event_count) {
  qa_resource_scope_assert_open(seat->resources);
  if (!seat->has_presentation) return;
  qa_simulation_event *filtered = NULL;
  size_t count = 0;
  if (event_count != 0) {
    filtered = xmalloc(event_count * sizeof *filtered);
    for (size_t i = 0; i < event_count; i++)
      if (qa_event_targets_seat(events[i], seat->id, seat->client->id))
        filtered[count++] = events[i];
  }
  seat->presentation.receive(seat->presentation.ctx, filtered, count);
  free(filtered);
}

qa_render_frame qa_session_seat_present(qa_session_seat *seat,
                                        qa_world_snapshot snapshot,
                                        qa_renderer_backend backend) {
  qa_resource_scope_assert_open(seat->resources);
  if (snapshot.session.token != seat->id.session.token)
    qa_range_error("Snapshot belongs to another session");
  if (!seat->has_presentation) qa_errorf("Seat has no presentation");
  qa_render_frame frame =
      seat->presentation.frame(seat->presentation.ctx, snapshot);
  seat->presentation.render(seat->presentation.ctx, frame, backend);
  return frame;
}
void qa_session_seat_close(qa_session_seat *seat) {
  qa_resource_scope_close(seat->resources);
}
static void seat_close_ctx(void *ctx) {
  qa_session_seat_close((qa_session_seat *)ctx);
}
qa_session_resource qa_session_seat_as_resource(qa_session_seat *seat) {
  qa_session_resource resource = {seat, seat_close_ctx};
  return resource;
}

bool qa_event_targets_seat(qa_simulation_event event, qa_seat_id seat,
                           qa_client_id client) {
  if (str_eq(event.audience.kind, "world")) return true;
  if (str_eq(event.audience.kind, "seat"))
    return qa_same_seat(event.audience.seat, seat);
  /* "client" */
  return qa_same_client(event.audience.client, client);
}

/* ---------- EngineSession ---------- */

struct client_slot {
  int64_t slot;
  qa_session_client *client;
};
struct seat_slot {
  int64_t index;
  qa_session_seat *seat;
};
struct prepared_client {
  qa_session_client *client;
  qa_session_client *incumbent; /* nullable */
};
struct prepared_seat {
  qa_session_seat *seat;
  qa_session_seat *incumbent; /* nullable */
};
struct generation {
  int64_t slot;
  int64_t value;
};
struct qa_engine_session {
  const qa_identity_owner *identity;
  qa_session_mode mode;
  qa_session_id session;
  qa_resource_scope *resources;
  struct client_slot *clients;
  size_t client_count, client_capacity;
  struct prepared_client *prepared_clients;
  size_t prepared_client_count, prepared_client_capacity;
  struct seat_slot *seats;
  size_t seat_count, seat_capacity;
  struct prepared_seat *prepared_seats;
  size_t prepared_seat_count, prepared_seat_capacity;
  struct generation *generations;
  size_t generation_count, generation_capacity;
  qa_world_lifetime *current_world;
  bool has_published;
  qa_world_snapshot published;
  bool stepping;
  qa_simulation_event *scratch_events;
  size_t scratch_event_count, scratch_event_capacity;
  qa_simulation_output scratch_output;
  /* Clients and seats that left every session map without a retired bundle
   * or closeClient freeing them (TS lets the GC have them): a closed slot
   * occupant replaced by Map.set, a prepared one closed before publication,
   * and everything close() closed. Freed by destroy. */
  qa_session_client **buried_clients;
  size_t buried_client_count, buried_client_capacity;
  qa_session_seat **buried_seats;
  size_t buried_seat_count, buried_seat_capacity;
};

static void bury_client(qa_engine_session *session, qa_session_client *client) {
  for (size_t i = 0; i < session->buried_client_count; i++)
    if (session->buried_clients[i] == client) return;
  if (session->buried_client_count == session->buried_client_capacity) {
    session->buried_client_capacity =
        session->buried_client_capacity != 0 ? session->buried_client_capacity * 2 : 8;
    session->buried_clients = xrealloc(
        session->buried_clients, session->buried_client_capacity * sizeof *session->buried_clients);
  }
  session->buried_clients[session->buried_client_count++] = client;
}
static void bury_seat(qa_engine_session *session, qa_session_seat *seat) {
  for (size_t i = 0; i < session->buried_seat_count; i++)
    if (session->buried_seats[i] == seat) return;
  if (session->buried_seat_count == session->buried_seat_capacity) {
    session->buried_seat_capacity =
        session->buried_seat_capacity != 0 ? session->buried_seat_capacity * 2 : 8;
    session->buried_seats = xrealloc(
        session->buried_seats, session->buried_seat_capacity * sizeof *session->buried_seats);
  }
  session->buried_seats[session->buried_seat_count++] = seat;
}

static qa_session_client *client_at_slot(qa_engine_session *session,
                                         int64_t slot) {
  for (size_t i = 0; i < session->client_count; i++)
    if (session->clients[i].slot == slot) return session->clients[i].client;
  return NULL;
}
static qa_session_seat *seat_at_index(qa_engine_session *session,
                                      int64_t index) {
  for (size_t i = 0; i < session->seat_count; i++)
    if (session->seats[i].index == index) return session->seats[i].seat;
  return NULL;
}
static struct prepared_client *prepared_client_of(
    qa_engine_session *session, qa_session_client *client) {
  for (size_t i = 0; i < session->prepared_client_count; i++)
    if (session->prepared_clients[i].client == client)
      return &session->prepared_clients[i];
  return NULL;
}
static struct prepared_seat *prepared_seat_of(qa_engine_session *session,
                                              qa_session_seat *seat) {
  for (size_t i = 0; i < session->prepared_seat_count; i++)
    if (session->prepared_seats[i].seat == seat)
      return &session->prepared_seats[i];
  return NULL;
}
static void remove_client_slot(qa_engine_session *session, int64_t slot) {
  for (size_t i = 0; i < session->client_count; i++)
    if (session->clients[i].slot == slot) {
      memmove(&session->clients[i], &session->clients[i + 1],
              (session->client_count - i - 1) * sizeof *session->clients);
      session->client_count--;
      return;
    }
}
static void remove_seat_slot(qa_engine_session *session, int64_t index) {
  for (size_t i = 0; i < session->seat_count; i++)
    if (session->seats[i].index == index) {
      memmove(&session->seats[i], &session->seats[i + 1],
              (session->seat_count - i - 1) * sizeof *session->seats);
      session->seat_count--;
      return;
    }
}
static bool remove_prepared_client(qa_engine_session *session,
                                   qa_session_client *client) {
  for (size_t i = 0; i < session->prepared_client_count; i++)
    if (session->prepared_clients[i].client == client) {
      memmove(&session->prepared_clients[i],
              &session->prepared_clients[i + 1],
              (session->prepared_client_count - i - 1) *
                  sizeof *session->prepared_clients);
      session->prepared_client_count--;
      return true;
    }
  return false;
}
static bool remove_prepared_seat(qa_engine_session *session,
                                 qa_session_seat *seat) {
  for (size_t i = 0; i < session->prepared_seat_count; i++)
    if (session->prepared_seats[i].seat == seat) {
      memmove(&session->prepared_seats[i], &session->prepared_seats[i + 1],
              (session->prepared_seat_count - i - 1) *
                  sizeof *session->prepared_seats);
      session->prepared_seat_count--;
      return true;
    }
  return false;
}
/* TS Map.set: an existing slot keeps its insertion position. */
static void put_client_slot(qa_engine_session *session,
                            qa_session_client *client) {
  for (size_t i = 0; i < session->client_count; i++)
    if (session->clients[i].slot == client->id.slot) {
      if (session->clients[i].client != client) bury_client(session, session->clients[i].client);
      session->clients[i].client = client;
      return;
    }
  if (session->client_count == session->client_capacity) {
    session->client_capacity =
        session->client_capacity != 0 ? session->client_capacity * 2 : 8;
    session->clients = xrealloc(
        session->clients, session->client_capacity * sizeof *session->clients);
  }
  session->clients[session->client_count].slot = client->id.slot;
  session->clients[session->client_count].client = client;
  session->client_count++;
}
static void put_seat_slot(qa_engine_session *session, qa_session_seat *seat) {
  for (size_t i = 0; i < session->seat_count; i++)
    if (session->seats[i].index == seat->id.index) {
      if (session->seats[i].seat != seat) bury_seat(session, session->seats[i].seat);
      session->seats[i].seat = seat;
      return;
    }
  if (session->seat_count == session->seat_capacity) {
    session->seat_capacity =
        session->seat_capacity != 0 ? session->seat_capacity * 2 : 8;
    session->seats =
        xrealloc(session->seats, session->seat_capacity * sizeof *session->seats);
  }
  session->seats[session->seat_count].index = seat->id.index;
  session->seats[session->seat_count].seat = seat;
  session->seat_count++;
}

/* A retired bundle: TS { close: () => closeAll(retired, message) }. Closing
 * closes every item in order (one AggregateError with the site's message),
 * then frees the items, which nothing in the session references any more.
 * The returned resource's ctx is a closure handle, so a second close (a TS
 * no-op: every item is already closed) finds no bundle and returns. */
enum retired_kind { RET_WORLD, RET_SEAT, RET_CLIENT, RET_SCOPE };
struct retired_item {
  enum retired_kind kind;
  void *ptr;
};
struct retired_bundle {
  struct retired_item *items;
  size_t count, capacity;
  const char *message;
};
static struct retired_bundle *bundle_new(const char *message) {
  struct retired_bundle *bundle = calloc(1, sizeof *bundle);
  if (bundle == NULL) abort();
  bundle->message = message;
  return bundle;
}
static void bundle_add(struct retired_bundle *bundle, enum retired_kind kind,
                       void *ptr) {
  if (ptr == NULL) return;
  if (bundle->count == bundle->capacity) {
    bundle->capacity = bundle->capacity != 0 ? bundle->capacity * 2 : 8;
    bundle->items =
        xrealloc(bundle->items, bundle->capacity * sizeof *bundle->items);
  }
  bundle->items[bundle->count].kind = kind;
  bundle->items[bundle->count].ptr = ptr;
  bundle->count++;
}
static void world_close_resource(void *ctx) { qa_world_lifetime_close(ctx); }
static void seat_close_resource(void *ctx) { qa_session_seat_close(ctx); }
static void client_close_resource(void *ctx) { qa_session_client_close(ctx); }
static void bundle_close(void *token) {
  struct retired_bundle *bundle = qa_world_handle_get(token);
  if (bundle == NULL) return;
  qa_world_handle_close(token);
  qa_session_resource *resources = NULL;
  if (bundle->count != 0) resources = xmalloc(bundle->count * sizeof *resources);
  for (size_t i = 0; i < bundle->count; i++) {
    resources[i].ctx = bundle->items[i].ptr;
    switch (bundle->items[i].kind) {
      case RET_WORLD: resources[i].close = world_close_resource; break;
      case RET_SEAT: resources[i].close = seat_close_resource; break;
      case RET_CLIENT: resources[i].close = client_close_resource; break;
      case RET_SCOPE: resources[i].close = scope_close_resource; break;
    }
  }
  size_t errors = close_all_collect(resources, bundle->count);
  free(resources);
  for (size_t i = 0; i < bundle->count; i++) {
    switch (bundle->items[i].kind) {
      case RET_WORLD: qa_world_lifetime_destroy(bundle->items[i].ptr); break;
      case RET_SEAT: qa_session_seat_destroy(bundle->items[i].ptr); break;
      case RET_CLIENT: qa_session_client_destroy(bundle->items[i].ptr); break;
      case RET_SCOPE: qa_resource_scope_destroy(bundle->items[i].ptr); break;
    }
  }
  const char *message = bundle->message;
  free(bundle->items);
  free(bundle);
  raise_aggregate(errors, message);
}
static qa_session_resource bundle_resource(struct retired_bundle *bundle) {
  qa_session_resource resource = {qa_world_handle_open(bundle), bundle_close};
  return resource;
}

/* TS closeAll([...map.values()].reverse(), message) over a snapshot. */
static void close_clients_reverse(qa_engine_session *session, bool prepared) {
  size_t count = prepared ? session->prepared_client_count
                          : session->client_count;
  if (count == 0) return;
  qa_session_resource *resources = xmalloc(count * sizeof *resources);
  for (size_t i = 0; i < count; i++) {
    resources[i].ctx = prepared ? session->prepared_clients[count - 1 - i].client
                                : session->clients[count - 1 - i].client;
    resources[i].close = client_close_resource;
  }
  size_t errors = close_all_collect(resources, count);
  free(resources);
  raise_aggregate(errors, prepared ? "Prepared client shutdown failed" : "Client shutdown failed");
}
static void close_seats_reverse(qa_engine_session *session, bool prepared) {
  size_t count =
      prepared ? session->prepared_seat_count : session->seat_count;
  if (count == 0) return;
  qa_session_resource *resources = xmalloc(count * sizeof *resources);
  for (size_t i = 0; i < count; i++) {
    resources[i].ctx = prepared ? session->prepared_seats[count - 1 - i].seat
                                : session->seats[count - 1 - i].seat;
    resources[i].close = seat_close_resource;
  }
  size_t errors = close_all_collect(resources, count);
  free(resources);
  raise_aggregate(errors, prepared ? "Prepared seat shutdown failed" : "Seat shutdown failed");
}
static void defer_clients(void *ctx) {
  close_clients_reverse((qa_engine_session *)ctx, false);
}
static void defer_prepared_clients(void *ctx) {
  close_clients_reverse((qa_engine_session *)ctx, true);
}
static void defer_prepared_seats(void *ctx) {
  close_seats_reverse((qa_engine_session *)ctx, true);
}
static void defer_seats(void *ctx) {
  close_seats_reverse((qa_engine_session *)ctx, false);
}
static void defer_world(void *ctx) {
  qa_engine_session_close_world((qa_engine_session *)ctx);
}

qa_engine_session *qa_engine_session_create(
    const qa_identity_owner *identity, qa_session_mode mode) {
  qa_engine_session *session = calloc(1, sizeof *session);
  if (session == NULL) return NULL;
  session->identity = identity;
  session->mode = mode;
  session->session = identity->session;
  session->resources = new_scope("Session resources");
  qa_resource_scope_defer(session->resources, defer_clients, session);
  qa_resource_scope_defer(session->resources, defer_prepared_clients,
                          session);
  qa_resource_scope_defer(session->resources, defer_prepared_seats, session);
  qa_resource_scope_defer(session->resources, defer_seats, session);
  qa_resource_scope_defer(session->resources, defer_world, session);
  return session;
}
static void session_close_call(void *ctx) { qa_engine_session_close(ctx); }
void qa_engine_session_destroy(qa_engine_session *session) {
  if (session == NULL) return;
  qa_caught_error caught;
  bool ok = qa_try(session_close_call, session, &caught);
  /* Seats first: a seat's close reaches its client. Each was closed above. */
  for (size_t i = 0; i < session->buried_seat_count; i++)
    qa_session_seat_destroy(session->buried_seats[i]);
  for (size_t i = 0; i < session->buried_client_count; i++)
    qa_session_client_destroy(session->buried_clients[i]);
  free(session->buried_seats);
  free(session->buried_clients);
  qa_resource_scope_destroy(session->resources);
  free(session->clients);
  free(session->prepared_clients);
  free(session->seats);
  free(session->prepared_seats);
  free(session->generations);
  free(session->scratch_events);
  free(session);
  if (!ok) qa_rethrow(&caught);
}
qa_session_id qa_engine_session_id(const qa_engine_session *session) {
  return session->session;
}
qa_resource_scope *qa_engine_session_resources(qa_engine_session *session) {
  return session->resources;
}
bool qa_engine_session_is_closed(const qa_engine_session *session) {
  return qa_resource_scope_is_closed(session->resources);
}
qa_world_lifetime *qa_engine_session_world(qa_engine_session *session) {
  return session->current_world;
}
bool qa_engine_session_snapshot(qa_engine_session *session,
                                qa_world_snapshot *out) {
  if (!session->has_published) return false;
  *out = session->published;
  return true;
}
qa_session_client *qa_engine_session_client_at(qa_engine_session *session,
                                               int64_t slot) {
  qa_session_client *client = client_at_slot(session, slot);
  return client == NULL || qa_session_client_is_closed(client) ? NULL
                                                                : client;
}

static bool ptr_in_clients(qa_session_client *const *list, size_t count,
                           qa_session_client *client) {
  for (size_t i = 0; i < count; i++)
    if (list[i] == client) return true;
  return false;
}
static bool ptr_in_seats(qa_session_seat *const *list, size_t count,
                         qa_session_seat *seat) {
  for (size_t i = 0; i < count; i++)
    if (list[i] == seat) return true;
  return false;
}

static void validate_ownership_change(
    qa_engine_session *session,
    const qa_session_presentation_entry *presentations,
    size_t presentation_count, qa_session_client_change clients,
    qa_session_seat_change seats) {
  for (size_t i = 0; i < seats.removed_count; i++) {
    qa_session_seat *seat = seats.removed[i];
    for (size_t j = i + 1; j < seats.removed_count; j++)
      if (seats.removed[j] == seat) qa_errorf("Duplicate retired seat");
    if (qa_session_seat_is_closed(seat) ||
        seat_at_index(session, seat->id.index) != seat)
      qa_errorf("Retired seat is not owned by this session");
  }
  for (size_t i = 0; i < clients.removed_count; i++) {
    qa_session_client *client = clients.removed[i];
    for (size_t j = i + 1; j < clients.removed_count; j++)
      if (clients.removed[j] == client)
        qa_errorf("Duplicate retired client");
    if (qa_session_client_is_closed(client) ||
        client_at_slot(session, client->id.slot) != client)
      qa_errorf("Retired client is not owned by this session");
    for (size_t j = 0; j < session->seat_count; j++) {
      qa_session_seat *seat = session->seats[j].seat;
      if (seat->client == client && !qa_session_seat_is_closed(seat) &&
          !ptr_in_seats(seats.removed, seats.removed_count, seat))
        qa_errorf(
            "Cannot retire a client with a local seat during world "
            "replacement");
    }
  }
  for (size_t i = 0; i < clients.added_count; i++) {
    qa_session_client *client = clients.added[i];
    struct prepared_client *prepared = prepared_client_of(session, client);
    if (qa_session_client_is_closed(client) || prepared == NULL)
      qa_errorf("Replacement client was not prepared by this session");
    for (size_t j = 0; j < i; j++)
      if (clients.added[j]->id.slot == client->id.slot)
        qa_errorf("Duplicate prepared client slot");
    qa_session_client *existing = client_at_slot(session, client->id.slot);
    qa_session_client *incumbent = prepared->incumbent;
    if (incumbent != NULL &&
        (existing != incumbent ||
         !ptr_in_clients(clients.removed, clients.removed_count, incumbent)))
      qa_errorf("Prepared client incumbent changed or is not retired");
    if (incumbent == NULL && existing != NULL &&
        !qa_session_client_is_closed(existing))
      qa_errorf("Client slot %lld is occupied", (long long)client->id.slot);
  }
  for (size_t i = 0; i < seats.added_count; i++) {
    qa_session_seat *seat = seats.added[i];
    struct prepared_seat *prepared = prepared_seat_of(session, seat);
    if (qa_session_seat_is_closed(seat) || prepared == NULL)
      qa_errorf("Replacement seat was not prepared by this session");
    for (size_t j = 0; j < i; j++)
      if (seats.added[j]->id.index == seat->id.index)
        qa_errorf("Duplicate prepared seat slot");
    qa_session_client *added_client = NULL;
    for (size_t j = 0; j < clients.added_count; j++)
      if (clients.added[j]->id.slot == seat->client->id.slot)
        added_client = clients.added[j];
    if (qa_session_client_is_closed(seat->client) ||
        ptr_in_clients(clients.removed, clients.removed_count,
                       seat->client) ||
        (client_at_slot(session, seat->client->id.slot) != seat->client &&
         added_client != seat->client))
      qa_errorf("Replacement seat client is not published by this transition");
    qa_session_client_validate_seat_binding(seat->client, seat);
    qa_session_seat *existing = seat_at_index(session, seat->id.index);
    qa_session_seat *incumbent = prepared->incumbent;
    if (incumbent != NULL &&
        (existing != incumbent ||
         !ptr_in_seats(seats.removed, seats.removed_count, incumbent)))
      qa_errorf("Prepared seat incumbent changed or is not retired");
    if (incumbent == NULL && existing != NULL &&
        !qa_session_seat_is_closed(existing))
      qa_errorf("Seat %lld is occupied", (long long)seat->id.index);
  }
  for (size_t i = 0; i < presentation_count; i++) {
    qa_session_seat *seat = presentations[i].seat;
    qa_session_seat *added_seat = NULL;
    for (size_t j = 0; j < seats.added_count; j++)
      if (seats.added[j]->id.index == seat->id.index)
        added_seat = seats.added[j];
    if (ptr_in_seats(seats.removed, seats.removed_count, seat) ||
        (seat_at_index(session, seat->id.index) != seat &&
         added_seat != seat))
      qa_errorf("Presentation seat is not owned by this session");
    for (size_t j = 0; j < i; j++)
      if (presentations[j].seat == seat)
        qa_errorf("Duplicate replacement presentation");
    qa_session_seat_validate_presentation(seat,
                                          presentations[i].presentation);
  }
}

void qa_engine_session_validate_world_replacement(
    qa_engine_session *session, qa_simulation simulation,
    const qa_session_presentation_entry *presentations,
    size_t presentation_count, qa_session_client_change clients,
    qa_session_seat_change seats) {
  qa_resource_scope_assert_open(session->resources);
  if (session->stepping)
    qa_errorf("Cannot replace a world during simulation.step");
  if (simulation.session.token != session->session.token)
    qa_range_error("Simulation belongs to another session");
  if (session->current_world != NULL &&
      session->current_world->simulation.ctx == simulation.ctx &&
      session->current_world->simulation.step == simulation.step)
    qa_errorf("Simulation is already attached");
  validate_ownership_change(session, presentations, presentation_count,
                            clients, seats);
}

static qa_resource_scope *presentation_scope(
    const qa_session_presentation_entry *entry) {
  char name[64];
  snprintf(name, sizeof name, "Seat %lld presentation",
           (long long)entry->seat->id.index);
  qa_resource_scope *resources = new_scope(name);
  qa_resource_scope_defer(resources, entry->cleanup, entry->cleanup_ctx);
  return resources;
}

qa_session_resource qa_engine_session_replace_world(
    qa_engine_session *session, qa_simulation simulation,
    const qa_session_presentation_entry *presentations,
    size_t presentation_count, qa_session_client_change clients,
    qa_session_seat_change seats, qa_world_lifetime **out_world) {
  qa_engine_session_validate_world_replacement(
      session, simulation, presentations, presentation_count, clients,
      seats);
  /* Create the next presentation scopes first (TS order). */
  qa_resource_scope **next_scopes = NULL;
  if (presentation_count != 0) {
    next_scopes = xmalloc(presentation_count * sizeof *next_scopes);
    for (size_t i = 0; i < presentation_count; i++)
      next_scopes[i] = presentation_scope(&presentations[i]);
  }
  qa_world_lifetime *world = qa_world_lifetime_create(simulation);
  if (world == NULL) abort();
  struct retired_bundle *retired = bundle_new("Retired world shutdown failed");
  if (session->current_world != NULL)
    bundle_add(retired, RET_WORLD, session->current_world);
  for (size_t i = 0; i < seats.removed_count; i++) {
    remove_seat_slot(session, seats.removed[i]->id.index);
    bundle_add(retired, RET_SEAT, seats.removed[i]);
  }
  for (size_t i = 0; i < seats.added_count; i++) {
    qa_session_client_bind_seat(seats.added[i]->client, seats.added[i]);
    put_seat_slot(session, seats.added[i]);
    remove_prepared_seat(session, seats.added[i]);
  }
  for (size_t i = 0; i < session->seat_count; i++) {
    qa_session_seat *seat = session->seats[i].seat;
    const qa_seat_presentation *next = NULL;
    qa_resource_scope *scope = NULL;
    for (size_t j = 0; j < presentation_count; j++)
      if (presentations[j].seat == seat) {
        next = &presentations[j].presentation;
        scope = next_scopes[j];
      }
    qa_resource_scope *previous =
        qa_session_seat_replace_presentation(seat, next, scope);
    bundle_add(retired, RET_SCOPE, previous);
  }
  for (size_t i = 0; i < session->client_count; i++) {
    qa_session_client *client = session->clients[i].client;
    if (!qa_session_client_is_closed(client) &&
        !ptr_in_clients(clients.removed, clients.removed_count, client))
      bundle_add(retired, RET_SCOPE,
                 qa_session_client_replace_world_resources(client));
  }
  for (size_t i = 0; i < clients.removed_count; i++) {
    remove_client_slot(session, clients.removed[i]->id.slot);
    bundle_add(retired, RET_CLIENT, clients.removed[i]);
  }
  for (size_t i = 0; i < clients.added_count; i++) {
    put_client_slot(session, clients.added[i]);
    remove_prepared_client(session, clients.added[i]);
  }
  free(next_scopes);
  session->current_world = world;
  session->has_published = false;
  *out_world = world;
  return bundle_resource(retired);
}

qa_world_lifetime *qa_engine_session_attach_world(
    qa_engine_session *session, qa_simulation simulation) {
  qa_world_lifetime *world = NULL;
  qa_session_client_change no_clients = {NULL, 0, NULL, 0};
  qa_session_seat_change no_seats = {NULL, 0, NULL, 0};
  qa_session_resource retired = qa_engine_session_replace_world(
      session, simulation, NULL, 0, no_clients, no_seats, &world);
  retired.close(retired.ctx);
  return world;
}

void qa_engine_session_validate_local_seats(
    qa_engine_session *session,
    const qa_session_presentation_entry *presentations,
    size_t presentation_count, qa_session_client_change clients,
    qa_session_seat_change seats) {
  qa_resource_scope_assert_open(session->resources);
  if (session->stepping)
    qa_errorf("Cannot change local seats during simulation.step");
  if (session->current_world != NULL && session->current_world->closed)
    qa_errorf("Local seat publication cannot use a retired world");
  validate_ownership_change(session, presentations, presentation_count,
                            clients, seats);
}

qa_session_resource qa_engine_session_publish_local_seats(
    qa_engine_session *session,
    const qa_session_presentation_entry *presentations,
    size_t presentation_count, qa_session_client_change clients,
    qa_session_seat_change seats) {
  qa_engine_session_validate_local_seats(session, presentations,
                                         presentation_count, clients, seats);
  struct retired_bundle *retired = bundle_new("Retired local seats shutdown failed");
  for (size_t i = 0; i < seats.removed_count; i++) {
    remove_seat_slot(session, seats.removed[i]->id.index);
    bundle_add(retired, RET_SEAT, seats.removed[i]);
  }
  for (size_t i = 0; i < clients.removed_count; i++) {
    remove_client_slot(session, clients.removed[i]->id.slot);
    bundle_add(retired, RET_CLIENT, clients.removed[i]);
  }
  for (size_t i = 0; i < clients.added_count; i++) {
    put_client_slot(session, clients.added[i]);
    remove_prepared_client(session, clients.added[i]);
  }
  for (size_t i = 0; i < seats.added_count; i++) {
    qa_session_client_bind_seat(seats.added[i]->client, seats.added[i]);
    put_seat_slot(session, seats.added[i]);
    remove_prepared_seat(session, seats.added[i]);
  }
  for (size_t i = 0; i < presentation_count; i++) {
    qa_resource_scope *scope = presentation_scope(&presentations[i]);
    qa_resource_scope *previous = qa_session_seat_replace_presentation(
        presentations[i].seat, &presentations[i].presentation, scope);
    bundle_add(retired, RET_SCOPE, previous);
  }
  return bundle_resource(retired);
}

qa_session_resource qa_engine_session_detach_world(
    qa_engine_session *session) {
  struct retired_bundle *retired = bundle_new("World replacement failed");
  if (session->current_world != NULL)
    bundle_add(retired, RET_WORLD, session->current_world);
  session->current_world = NULL;
  session->has_published = false;
  for (size_t i = 0; i < session->seat_count; i++) {
    qa_resource_scope *previous = qa_session_seat_replace_presentation(
        session->seats[i].seat, NULL, NULL);
    bundle_add(retired, RET_SCOPE, previous);
  }
  for (size_t i = 0; i < session->client_count; i++) {
    qa_session_client *client = session->clients[i].client;
    if (!qa_session_client_is_closed(client))
      bundle_add(retired, RET_SCOPE,
                 qa_session_client_replace_world_resources(client));
  }
  return bundle_resource(retired);
}
void qa_engine_session_close_world(qa_engine_session *session) {
  qa_session_resource retired = qa_engine_session_detach_world(session);
  retired.close(retired.ctx);
}

struct prepared_client_cleanup {
  qa_engine_session *session;
  qa_session_client *client;
};
static void prepared_client_cleanup_cb(void *ctx) {
  struct prepared_client_cleanup cleanup = *(struct prepared_client_cleanup *)ctx;
  free(ctx);
  qa_engine_session *session = cleanup.session;
  if (remove_prepared_client(session, cleanup.client)) bury_client(session, cleanup.client);
  /* closeAll(over a snapshot: each close deletes its preparedSeats entry) */
  size_t count = 0;
  qa_session_resource *resources = NULL;
  if (session->prepared_seat_count != 0)
    resources = xmalloc(session->prepared_seat_count * sizeof *resources);
  for (size_t i = 0; i < session->prepared_seat_count; i++)
    if (session->prepared_seats[i].seat->client == cleanup.client) {
      resources[count].ctx = session->prepared_seats[i].seat;
      resources[count++].close = seat_close_resource;
    }
  size_t errors = close_all_collect(resources, count);
  free(resources);
  raise_aggregate(errors, "Prepared client seats shutdown failed");
}

qa_session_client *qa_engine_session_prepare_client(
    qa_engine_session *session, int64_t slot,
    qa_session_client *retiring) {
  qa_resource_scope_assert_open(session->resources);
  qa_session_client *existing = client_at_slot(session, slot);
  if (retiring != NULL && (existing != retiring ||
                           qa_session_client_is_closed(retiring)))
    qa_errorf("Retiring client is not the current slot owner");
  if (retiring == NULL && existing != NULL &&
      !qa_session_client_is_closed(existing))
    qa_errorf("Client slot %lld is occupied", (long long)slot);
  int64_t generation = 0;
  struct generation *entry = NULL;
  for (size_t i = 0; i < session->generation_count; i++)
    if (session->generations[i].slot == slot)
      entry = &session->generations[i];
  generation = entry != NULL ? entry->value + 1 : 0;
  qa_client_id id =
      qa_identity_owner_client(session->identity, slot, generation);
  qa_session_client *client = qa_session_client_create(id);
  if (client == NULL) abort();
  if (entry == NULL) {
    if (session->generation_count == session->generation_capacity) {
      session->generation_capacity =
          session->generation_capacity != 0 ? session->generation_capacity * 2
                                            : 8;
      session->generations = xrealloc(
          session->generations,
          session->generation_capacity * sizeof *session->generations);
    }
    entry = &session->generations[session->generation_count++];
    entry->slot = slot;
  }
  entry->value = generation;
  if (session->prepared_client_count == session->prepared_client_capacity) {
    session->prepared_client_capacity =
        session->prepared_client_capacity != 0
            ? session->prepared_client_capacity * 2
            : 8;
    session->prepared_clients =
        xrealloc(session->prepared_clients, session->prepared_client_capacity *
                                                sizeof *session->prepared_clients);
  }
  session->prepared_clients[session->prepared_client_count].client = client;
  session->prepared_clients[session->prepared_client_count].incumbent =
      retiring;
  session->prepared_client_count++;
  struct prepared_client_cleanup *cleanup = xmalloc(sizeof *cleanup);
  cleanup->session = session;
  cleanup->client = client;
  qa_resource_scope_defer(client->resources, prepared_client_cleanup_cb,
                          cleanup);
  return client;
}

qa_session_client *qa_engine_session_create_client(
    qa_engine_session *session, int64_t slot) {
  qa_session_client *client =
      qa_engine_session_prepare_client(session, slot, NULL);
  put_client_slot(session, client);
  remove_prepared_client(session, client);
  return client;
}

void qa_engine_session_close_client(qa_engine_session *session,
                                    qa_client_id id) {
  qa_session_client *client = client_at_slot(session, id.slot);
  if (client == NULL || !qa_same_client(client->id, id)) return;
  qa_session_seat **bound = NULL;
  size_t bound_count = 0;
  if (session->seat_count != 0)
    bound = xmalloc(session->seat_count * sizeof *bound);
  for (size_t i = session->seat_count; i-- > 0;) {
    if (session->seats[i].seat->client == client) {
      bound[bound_count++] = session->seats[i].seat;
      remove_seat_slot(session, session->seats[i].index);
    }
  }
  remove_client_slot(session, id.slot);
  /* closeAll([...bound.reverse(), client], ...): bound is already newest first. */
  qa_session_resource *resources = xmalloc((bound_count + 1) * sizeof *resources);
  for (size_t i = 0; i < bound_count; i++) {
    resources[i].ctx = bound[i];
    resources[i].close = seat_close_resource;
  }
  resources[bound_count].ctx = client;
  resources[bound_count].close = client_close_resource;
  size_t errors = close_all_collect(resources, bound_count + 1);
  free(resources);
  for (size_t i = 0; i < bound_count; i++)
    qa_session_seat_destroy(bound[i]);
  free(bound);
  qa_session_client_destroy(client);
  raise_aggregate(errors, "Client shutdown failed");
}

struct prepared_seat_cleanup {
  qa_engine_session *session;
  qa_session_seat *seat;
};
static void prepared_seat_cleanup_cb(void *ctx) {
  struct prepared_seat_cleanup *cleanup = ctx;
  if (remove_prepared_seat(cleanup->session, cleanup->seat)) bury_seat(cleanup->session, cleanup->seat);
  free(cleanup);
}

qa_session_seat *qa_engine_session_prepare_seat(
    qa_engine_session *session, int64_t index, qa_session_client *client,
    qa_session_seat *retiring) {
  qa_resource_scope_assert_open(session->resources);
  if (session->mode.kind == QA_SESSION_MODE_HEADLESS)
    qa_errorf("Headless sessions do not have local seats");
  if ((client_at_slot(session, client->id.slot) != client &&
       prepared_client_of(session, client) == NULL) ||
      qa_session_client_is_closed(client))
    qa_range_error("Client is not owned by this session");
  qa_session_seat *previous = seat_at_index(session, index);
  if (retiring != NULL && (previous != retiring ||
                           qa_session_seat_is_closed(retiring)))
    qa_errorf("Retiring seat is not the current index owner");
  if (retiring == NULL && previous != NULL &&
      !qa_session_seat_is_closed(previous))
    qa_errorf("Seat %lld is occupied", (long long)index);
  qa_session_seat *seat = qa_session_seat_create(
      qa_identity_owner_seat(session->identity, index), client);
  if (seat == NULL) abort();
  if (session->prepared_seat_count == session->prepared_seat_capacity) {
    session->prepared_seat_capacity =
        session->prepared_seat_capacity != 0
            ? session->prepared_seat_capacity * 2
            : 8;
    session->prepared_seats =
        xrealloc(session->prepared_seats, session->prepared_seat_capacity *
                                               sizeof *session->prepared_seats);
  }
  session->prepared_seats[session->prepared_seat_count].seat = seat;
  session->prepared_seats[session->prepared_seat_count].incumbent = retiring;
  session->prepared_seat_count++;
  struct prepared_seat_cleanup *cleanup = xmalloc(sizeof *cleanup);
  cleanup->session = session;
  cleanup->seat = seat;
  qa_resource_scope_defer(seat->resources, prepared_seat_cleanup_cb, cleanup);
  return seat;
}

qa_session_seat *qa_engine_session_create_seat(qa_engine_session *session,
                                               int64_t index,
                                               qa_session_client *client) {
  if (client_at_slot(session, client->id.slot) != client ||
      qa_session_client_is_closed(client))
    qa_range_error("Client is not owned by this session");
  qa_session_seat *seat =
      qa_engine_session_prepare_seat(session, index, client, NULL);
  qa_session_client_bind_seat(client, seat);
  put_seat_slot(session, seat);
  remove_prepared_seat(session, seat);
  return seat;
}

void qa_engine_session_close_seat(qa_engine_session *session,
                                  qa_seat_id id) {
  qa_session_seat *seat = seat_at_index(session, id.index);
  if (seat == NULL || !qa_same_seat(seat->id, id)) return;
  remove_seat_slot(session, id.index);
  qa_caught_error caught;
  bool ok = qa_try(seat_close_resource, seat, &caught);
  qa_session_seat_destroy(seat);
  if (!ok) qa_rethrow(&caught);
}

static void end_stepping(void *arg) { ((qa_engine_session *)arg)->stepping = false; }
static qa_world_lifetime *begin_step(qa_engine_session *session) {
  qa_resource_scope_assert_open(session->resources);
  if (session->stepping) qa_errorf("Simulation step is already running");
  qa_world_lifetime *world = session->current_world;
  if (world == NULL || world->closed)
    qa_errorf("Session has no active simulation");
  session->stepping = true;
  return world;
}
static qa_simulation_output complete_step(qa_engine_session *session,
                                          qa_world_lifetime *world,
                                          qa_simulation_output output) {
  if (qa_engine_session_is_closed(session) ||
      session->current_world != world || world->closed)
    qa_errorf("Simulation closed during its step");
  qa_engine_session_publish(session, output);
  return output;
}

qa_simulation_output qa_engine_session_step(qa_engine_session *session,
                                            qa_input_batch input) {
  qa_world_lifetime *world = begin_step(session);
  qa_cleanup node; /* finally { stepping = false } */
  qa_cleanup_push(&node, end_stepping, session);
  qa_simulation_output output =
      world->simulation.step(world->simulation.ctx, input);
  output = complete_step(session, world, output);
  qa_cleanup_pop(&node);
  session->stepping = false;
  return output;
}

struct progress_state {
  qa_engine_session *session;
  qa_world_lifetime *world;
  qa_session_progress_fn progress;
  void *progress_ctx;
};
static void progress_cb(void *ctx, qa_simulation_progress frame) {
  struct progress_state *state = ctx;
  qa_engine_session *session = state->session;
  complete_step(session, state->world, *frame.output);
  size_t needed = session->scratch_event_count + frame.output->event_count;
  if (needed > session->scratch_event_capacity) {
    session->scratch_event_capacity = needed;
    session->scratch_events =
        xrealloc(session->scratch_events, session->scratch_event_capacity *
                                              sizeof *session->scratch_events);
  }
  if (frame.output->event_count != 0)
    memcpy(&session->scratch_events[session->scratch_event_count],
           frame.output->events,
           frame.output->event_count * sizeof *frame.output->events);
  session->scratch_event_count = needed;
  state->progress(state->progress_ctx, frame);
  if (qa_engine_session_is_closed(session) ||
      session->current_world != state->world || state->world->closed)
    qa_errorf("Simulation closed during its step");
}

qa_simulation_output qa_engine_session_step_async(
    qa_engine_session *session, qa_input_batch input,
    qa_session_progress_fn progress, void *progress_ctx) {
  qa_world_lifetime *world = begin_step(session);
  qa_cleanup node; /* finally { stepping = false } */
  qa_cleanup_push(&node, end_stepping, session);
  session->scratch_event_count = 0;
  qa_simulation_output output;
  if (progress != NULL && world->simulation.step_async != NULL) {
    struct progress_state state = {session, world, progress, progress_ctx};
    output = world->simulation.step_async(world->simulation.ctx, input,
                                          progress_cb, &state);
  } else if (world->simulation.step_async != NULL) {
    output = world->simulation.step_async(world->simulation.ctx, input,
                                          NULL, NULL);
  } else {
    output = world->simulation.step(world->simulation.ctx, input);
  }
  complete_step(session, world, output);
  qa_cleanup_pop(&node);
  session->stepping = false;
  if (session->scratch_event_count == 0) return output;
  size_t progressed = session->scratch_event_count;
  size_t needed = progressed + output.event_count;
  if (needed > session->scratch_event_capacity) {
    session->scratch_event_capacity = needed;
    session->scratch_events =
        xrealloc(session->scratch_events, session->scratch_event_capacity *
                                              sizeof *session->scratch_events);
  }
  if (output.event_count != 0)
    memcpy(&session->scratch_events[progressed], output.events,
           output.event_count * sizeof *output.events);
  session->scratch_output.snapshot = output.snapshot;
  session->scratch_output.events = session->scratch_events;
  session->scratch_output.event_count = needed;
  return session->scratch_output;
}

void qa_engine_session_publish(qa_engine_session *session,
                               qa_simulation_output output) {
  qa_resource_scope_assert_open(session->resources);
  if (output.snapshot.session.token != session->session.token)
    qa_range_error("Snapshot belongs to another session");
  session->published = output.snapshot;
  session->has_published = true;
  size_t count = session->seat_count;
  qa_session_seat **seats = NULL;
  if (count != 0) {
    seats = xmalloc(count * sizeof *seats);
    for (size_t i = 0; i < count; i++) seats[i] = session->seats[i].seat;
  }
  for (size_t i = 0; i < count; i++) {
    if (qa_engine_session_is_closed(session)) break;
    qa_session_seat *seat = seats[i];
    if (!qa_session_seat_is_closed(seat) &&
        !qa_session_client_is_closed(seat->client))
      qa_session_seat_receive(seat, output.events, output.event_count);
  }
  free(seats);
}

static void session_scope_close(void *ctx) {
  qa_resource_scope_close(((qa_engine_session *)ctx)->resources);
}
void qa_engine_session_close(qa_engine_session *session) {
  if (qa_engine_session_is_closed(session)) return;
  /* The closed clients and seats stay allocated until destroy (callers may
   * still hold them, as TS references keep them alive). */
  for (size_t i = 0; i < session->client_count; i++)
    bury_client(session, session->clients[i].client);
  for (size_t i = 0; i < session->prepared_client_count; i++)
    bury_client(session, session->prepared_clients[i].client);
  for (size_t i = 0; i < session->seat_count; i++)
    bury_seat(session, session->seats[i].seat);
  for (size_t i = 0; i < session->prepared_seat_count; i++)
    bury_seat(session, session->prepared_seats[i].seat);
  /* try { return this.resources.close() } finally { clear the maps } */
  qa_caught_error caught;
  bool ok = qa_try(session_scope_close, session, &caught);
  session->client_count = 0;
  session->prepared_client_count = 0;
  session->prepared_seat_count = 0;
  session->seat_count = 0;
  session->has_published = false;
  if (!ok) qa_rethrow(&caught);
}
static void session_close_ctx(void *ctx) {
  qa_engine_session_close((qa_engine_session *)ctx);
}
qa_session_resource qa_engine_session_as_resource(
    qa_engine_session *session) {
  qa_session_resource resource = {session, session_close_ctx};
  return resource;
}
