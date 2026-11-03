/* C helpers of the RemoteApplication port (remote-application.ts): the
 * presentation-union dispatch the class reads (TS calls the member on the
 * union directly), the Q3 authorization and SOCKS views of the keys and the
 * transport, the Q2 mvdPresentation view, small TS expressions (join, the
 * transport selection, seat lookups) and the storage release of a dropped
 * application. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "remote_application_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/network/q3/client_authorization.h"

bool qa_remote_seat_id_equal(qa_seat_id a, qa_seat_id b) { return a.token == b.token && a.index == b.index; }
void qa_remote_cvar_set(qa_cvar_registry *registry, const char *name, const char *value, bool force) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(registry, name, value, force, &snapshot)) qa_cvar_read_free(&snapshot);
}

/* The ApplicationViewSettings change closure (TS 265-275). */
void qa_remote_application_view_fov_changed(void *ctx, double value) {
  qa_remote_application *app = ctx;
  char text[40];
  qa_net_js_number(value, text, sizeof text);
  qa_app_world_seat_presentation *presentation = qa_remote_application_presentation(app);
  const qa_app_seat_q3_client *client = presentation != NULL ? qa_app_world_seat_presentation_q3_client(presentation) : NULL;
  if (client != NULL) qa_remote_cvar_set(client->cvars(client->ctx), "cg_fov", text, false);
  const qa_remote_live_network *network = qa_remote_application_network(app);
  const qa_remote_presentation *remote = qa_remote_application_remote(app);
  if (network != NULL && network->family == QA_APP_REMOTE_FAMILY_Q2 && remote->family == QA_APP_REMOTE_FAMILY_Q2) {
    qa_remote_cvar_set(app->configuration->source, "fov", text, false);
    qa_app_q2_client_network_userinfo(network->q2, qa_app_q2_remote_userinfo(remote->q2));
  }
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    qa_remote_seat_peer *peer = app->remote_seats[i];
    if (peer->view != NULL) {
      const qa_app_seat_q3_client *seat = qa_app_world_seat_presentation_q3_client(qa_app_remote_seat_view_presentation(peer->view));
      if (seat != NULL) qa_remote_cvar_set(seat->cvars(seat->ctx), "cg_fov", text, false);
    }
    if (qa_app_remote_seat_source_remote(peer->source)->family == QA_APP_REMOTE_FAMILY_Q2) {
      qa_cvar_registry *cvars = qa_remote_configuration_seat_source(app->configuration, qa_session_seat_id(peer->seat));
      if (cvars != NULL) qa_remote_cvar_set(cvars, "fov", text, false);
    }
  }
}

qa_datagram_transport *qa_remote_application_datagram(qa_remote_application *app, qa_app_application_transport *transport) {
  app->datagram = qa_app_application_transport_as_transport(transport);
  return &app->datagram;
}

/* TS keys.authorization.request(assertCurrent, send) */
static void authorization_request(void *ctx, qa_app_q3_assert_fn assert_current, void *assert_ctx,
    void (*send)(void *send_ctx, const qa_q3_address *address, const uint8_t *payload, size_t length), void *send_ctx) {
  qa_remote_application *app = ctx;
  qa_q3_client_authorization_request(qa_app_keys_authorization(app->keys), assert_current, assert_ctx, send, send_ctx);
}
qa_app_q3_authorization qa_remote_application_q3_authorization(qa_remote_application *app) {
  qa_app_q3_authorization authorization = {app, authorization_request};
  return authorization;
}

static void mvd_select_view(void *ctx, double clientnum) { qa_app_q2_remote_select_recorded_view(ctx, clientnum); }
qa_app_q2_mvd_presentation qa_remote_q2_mvd_presentation(qa_app_q2_remote *remote) {
  qa_app_q2_mvd_presentation presentation;
  presentation.visibility = qa_app_q2_remote_mvd_visibility(remote);
  presentation.ctx = remote;
  presentation.select_view = mvd_select_view;
  return presentation;
}

qa_mouse_settings *qa_remote_profile_seat_mouse(qa_app_prepared_profile *profile, qa_seat_id id) {
  size_t count = 0;
  const qa_app_prepared_seat_configuration *seats = qa_app_prepared_profile_seats(profile, &count);
  for (size_t i = 0; i < count; i++)
    if (qa_remote_seat_id_equal(seats[i].id, id)) return (qa_mouse_settings *)&seats[i].mouse;
  return NULL;
}
const qa_app_prepared_seat *qa_remote_prepared_seat(const qa_app_prepared_startup *prepared, qa_seat_id id) {
  size_t count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(prepared, &count);
  for (size_t i = 0; i < count; i++)
    if (qa_remote_seat_id_equal(seats[i].id, id)) return &seats[i];
  return NULL;
}

qa_app_network_transport_selection qa_remote_options_transport_selection(const qa_app_options *options) {
  qa_app_network_transport_selection selection = {QA_APP_TRANSPORT_UDP, NULL};
  const char *kind = options->network_transport_kind;
  if (kind != NULL && strcmp(kind, "ipx-native") == 0) selection.kind = QA_APP_TRANSPORT_IPX_NATIVE;
  else if (kind != NULL && strcmp(kind, "ipx-dosbox") == 0) {
    selection.kind = QA_APP_TRANSPORT_IPX_DOSBOX;
    selection.relay = options->network_transport_relay;
  }
  return selection;
}

/* TS node:path join(a, b, c): segments joined by "/" and normalized ("." dropped, ".." pops). */
char *qa_remote_join3(const char *a, const char *b, const char *c) {
  size_t length = strlen(a) + strlen(b) + strlen(c) + 4;
  char *joined = malloc(length);
  if (joined == NULL) qa_range_error("Out of memory");
  snprintf(joined, length, "%s/%s/%s", a, b, c);
  bool absolute = joined[0] == '/';
  char **parts = calloc(length, sizeof *parts);
  if (parts == NULL) { free(joined); qa_range_error("Out of memory"); }
  size_t count = 0;
  for (char *save = NULL, *part = strtok_r(joined, "/", &save); part != NULL; part = strtok_r(NULL, "/", &save)) {
    if (strcmp(part, ".") == 0) continue;
    if (strcmp(part, "..") == 0 && count > 0 && strcmp(parts[count - 1], "..") != 0) { count--; continue; }
    if (strcmp(part, "..") == 0 && absolute) continue;
    parts[count++] = part;
  }
  char *out = malloc(length + 2);
  if (out == NULL) { free(parts); free(joined); qa_range_error("Out of memory"); }
  size_t at = 0;
  if (absolute) out[at++] = '/';
  for (size_t i = 0; i < count; i++) {
    size_t part = strlen(parts[i]);
    memcpy(out + at, parts[i], part);
    at += part;
    if (i + 1 < count) out[at++] = '/';
  }
  if (at == 0) out[at++] = '.';
  out[at] = '\0';
  free(parts);
  free(joined);
  return out;
}

static void socks_connect(void *ctx, const qa_socks_options *options) {
  qa_app_application_transport_connect_socks(ctx, options);
}
qa_socks_connector qa_remote_application_socks_connector(qa_app_application_transport *transport) {
  qa_socks_connector connector = {transport, socks_connect};
  return connector;
}

qa_app_application_q3_client *qa_remote_presentation_q3_client(const qa_app_world_seat_presentation *presentation) {
  if (presentation == NULL) return NULL;
  const qa_app_seat_q3_client *client = qa_app_world_seat_presentation_q3_client(presentation);
  /* the seat view of an ApplicationQ3Client carries the client as its ctx (q3_seat_client.c) */
  return client == NULL ? NULL : client->ctx;
}

/* ---- the presentation union ------------------------------------------- */
void qa_remote_presentation_register_resource(const qa_remote_presentation *remote, qa_content_id content, const char *path,
    qa_resolved_resource_reference resource) {
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: qa_app_unified_remote_register_resource(remote->unified, content, path, resource); return;
    case QA_APP_REMOTE_FAMILY_Q1: qa_app_q1_remote_register_resource(remote->q1, content, path, resource); return;
    case QA_APP_REMOTE_FAMILY_QW: qa_app_qw_remote_register_resource(remote->qw, content, path, resource); return;
    case QA_APP_REMOTE_FAMILY_Q2: qa_app_q2_remote_register_resource(remote->q2, content, path, resource); return;
    case QA_APP_REMOTE_FAMILY_Q3: qa_app_q3_remote_register_resource(remote->q3, content, path, resource); return;
  }
}
const qa_simulation_output *qa_remote_presentation_sample(const qa_remote_presentation *remote, double now,
    qa_simulation_output *scratch) {
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: return qa_app_unified_remote_sample_presentation(remote->unified, now, scratch);
    case QA_APP_REMOTE_FAMILY_Q1: return qa_app_q1_remote_sample_presentation(remote->q1, now);
    case QA_APP_REMOTE_FAMILY_QW: return qa_app_qw_remote_sample_presentation(remote->qw, now);
    case QA_APP_REMOTE_FAMILY_Q2: return qa_app_q2_remote_sample_presentation(remote->q2, now);
    case QA_APP_REMOTE_FAMILY_Q3: return qa_app_q3_remote_sample_presentation(remote->q3, now);
  }
  return NULL;
}
void qa_remote_presentation_player_command(const qa_remote_presentation *remote, qa_actor_id actor, const char *name,
    const char *const *args, size_t count) {
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: qa_app_unified_remote_player_command(remote->unified, actor, name, args, count); return;
    case QA_APP_REMOTE_FAMILY_Q1: qa_app_q1_remote_player_command(remote->q1, actor, name, args, count); return;
    case QA_APP_REMOTE_FAMILY_QW: qa_app_qw_remote_player_command(remote->qw, actor, name, args, count); return;
    case QA_APP_REMOTE_FAMILY_Q2: qa_app_q2_remote_player_command(remote->q2, actor, name, args, count); return;
    case QA_APP_REMOTE_FAMILY_Q3: qa_app_q3_remote_player_command(remote->q3, actor, name, args, count); return;
  }
}
qa_app_sim_player_ui qa_remote_presentation_player_ui(const qa_remote_presentation *remote, qa_actor_id actor) {
  switch (remote->family) {
    case QA_APP_REMOTE_FAMILY_UNIFIED: return *qa_app_unified_remote_player_ui(remote->unified, actor);
    case QA_APP_REMOTE_FAMILY_Q1: return qa_app_q1_remote_player_ui(remote->q1, actor);
    case QA_APP_REMOTE_FAMILY_QW: return qa_app_qw_remote_player_ui(remote->qw, actor);
    case QA_APP_REMOTE_FAMILY_Q2: return qa_app_q2_remote_player_ui(remote->q2, actor);
    case QA_APP_REMOTE_FAMILY_Q3: return qa_app_q3_remote_player_ui(remote->q3, actor);
  }
  qa_range_error("Unknown remote family");
}
bool qa_remote_presentation_admitted_player(const qa_remote_presentation *remote, bool q3_connection, qa_actor_id *out) {
  if (q3_connection && remote->family == QA_APP_REMOTE_FAMILY_Q3) {
    *out = qa_app_q3_remote_admitted_player(remote->q3).actor;
    return true;
  }
  return qa_app_remote_seat_presentation_player(remote, out);
}

/* ---- storage ------------------------------------------------------------ */
void qa_remote_application_free_storage(qa_remote_application *app) {
  for (size_t i = 0; i < app->command_count; i++) qa_remote_command_free(&app->commands[i]);
  free(app->commands);
  for (size_t i = 0; i < app->source_handler_count; i++) free(app->source_handlers[i].name);
  free(app->source_handlers);
  for (size_t i = 0; i < app->reported_effect_gap_count; i++) free(app->reported_effect_gaps[i]);
  free(app->reported_effect_gaps);
  free(app->remote_seats);
  free(app->qports);
  free(app->client_inputs);
  free(app->initial_userinfo);
  free(app->qw_all_skins);
  free(app->q1_seat_name);
  free(app->returned_text);
  free((char *)app->launch_storage.label);
  if (app->launch_storage.kind == QA_REMOTE_SOURCE_LIVE) qa_net_address_destroy(&app->launch_storage.address);
  free(app->published_events);
  free(app->local_player_rows);
  free(app->progress_rows);
  if (app->retired_frontends != NULL) {
    qa_event_ring_destroy(app->retired_frontends);
    free(app->retired_frontends);
  }
  if (app->view_settings != NULL) qa_app_view_settings_destroy(app->view_settings);
  qa_app_remote_access_release(&app->access);
  qa_remote_arena_release(&app->arena);
  free(app);
}

/* ---- small TS expressions --------------------------------------------- */
int qa_remote_cheats_never(void *ctx) {
  (void)ctx; /* TS () => false */
  return 0;
}
qa_app_input_command_owner qa_remote_input_owner(const qa_remote_command_owner *owner) {
  qa_app_input_command_owner view;
  view.movement = owner->movement;
  view.fallback = owner->fallback;
  view.scripts = owner->scripts;
  view.cvars = owner->cvars;
  view.commands = owner->commands;
  view.routing = owner->routing;
  view.input_settings = owner->input_settings;
  return view;
}
void qa_remote_renderer_execute(qa_remote_application *app, const qa_render_command *commands, size_t count) {
  qa_render_frame frame;
  frame.owner = qa_app_native_renderer_owner(app->renderer);
  frame.sequence = app->frames;
  frame.commands = commands;
  frame.command_count = count;
  qa_app_native_renderer_execute(app->renderer, frame);
}
qa_session_seat **qa_remote_copy_seats(qa_session_seat *const *seats, size_t count) {
  qa_session_seat **copy = calloc(count + 1, sizeof *copy);
  if (copy == NULL) qa_range_error("Out of memory");
  if (count > 0) memcpy(copy, seats, count * sizeof *copy);
  return copy;
}
qa_remote_seat_source_entry *qa_remote_copy_seat_sources(const qa_remote_seat_source_entry *entries, size_t count) {
  qa_remote_seat_source_entry *copy = calloc(count + 1, sizeof *copy);
  if (copy == NULL) qa_range_error("Out of memory");
  if (count > 0) memcpy(copy, entries, count * sizeof *copy);
  return copy;
}
qa_remote_seat_permission_entry *qa_remote_copy_permissions(const qa_remote_seat_permission_entry *entries, size_t count) {
  qa_remote_seat_permission_entry *copy = calloc(count + 1, sizeof *copy);
  if (copy == NULL) qa_range_error("Out of memory");
  if (count > 0) memcpy(copy, entries, count * sizeof *copy);
  return copy;
}
