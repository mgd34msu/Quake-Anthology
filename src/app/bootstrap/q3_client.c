/* Port of src/app/bootstrap/q3-client.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_client.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct pose_entry { qa_actor_id actor; qa_vec3 origin, angles; } pose_entry;
struct qa_app_application_q3_client {
  qa_app_q3_bootstrap_client_host host;
  qa_cvar_registry *cvar_owner;
  bool closed, initialized;
  double event_count;
  double key_catcher;
  qa_scene_camera latest_camera;
  pose_entry *poses; size_t pose_count, pose_capacity;
  bool has_round;
  qa_app_q3_local_round round;
};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
qa_app_application_q3_client *qa_app_application_q3_client_create(qa_app_q3_bootstrap_client_host host) {
  if (!is(host.options.input_kind, "q3") && host.options.input_kind != NULL)
    qa_range_error("Foreign movement requires prediction command binding");
  qa_app_application_q3_client *client = calloc(1, sizeof *client);
  if (client == NULL) qa_range_error("Out of memory");
  client->host = host;
  client->cvar_owner = host.options.cvars;
  const char *error = host.initialize(host.ctx);
  if (error != NULL) {
    free(client);
    qa_range_error("%s", error);
  }
  client->initialized = true;
  return client;
}
qa_cvar_registry *qa_app_application_q3_client_cvars(const qa_app_application_q3_client *client) {
  return client->cvar_owner;
}
qa_cvar_registry *qa_app_application_q3_client_time_cvars(const qa_app_application_q3_client *client) {
  return client->host.options.time_cvars;
}
void qa_app_application_q3_client_adopt_cvars(qa_app_application_q3_client *client, qa_cvar_registry *cvars) {
  if (qa_cvar_registry_dialect(cvars) != qa_cvar_registry_dialect(client->cvar_owner)
      || qa_cvar_registry_context(cvars).session.token != qa_cvar_registry_context(client->cvar_owner).session.token)
    qa_range_error("Q3 client cvar owner changed identity");
  client->cvar_owner = cvars;
}
double qa_app_application_q3_client_presented_events(const qa_app_application_q3_client *client) {
  return client->event_count;
}
static bool backend_typescript(const qa_app_application_q3_client *client) {
  return is(client->host.options.kind, "typescript");
}
static void require_backend(const qa_app_application_q3_client *client) {
  if (client->closed || !client->initialized) qa_range_error("Q3 cgame seat is closed or uninitialized");
}
static void require_game(const qa_app_application_q3_client *client) {
  require_backend(client);
  if (!backend_typescript(client)) qa_range_error("This seat runs native guest cgame");
}
void qa_app_application_q3_client_assert_can_restart_round(qa_app_application_q3_client *client) {
  if (!client->initialized || !backend_typescript(client) || client->host.options.local_source == NULL)
    qa_range_error("Q3 fast restart requires an active native local cgame");
}
void qa_app_application_q3_client_rebind_round(qa_app_application_q3_client *client, qa_app_q3_local_round binding) {
  qa_app_application_q3_client_assert_can_restart_round(client);
  if (client->host.options.local_source == NULL) qa_range_error("Missing native local source");
  if (client->has_round && binding.movement_timing != client->round.movement_timing)
    qa_range_error("Q3 restart changed movement timing");
  client->round = binding;
  client->has_round = true;
}
void qa_app_application_q3_client_receive_events(qa_app_application_q3_client *client, const void *events, size_t count) {
  (void)events;
  if (client->host.options.local_source == NULL)
    qa_range_error("Remote Q3 cgame receives events through its network connection");
  client->event_count += (double)count;
}
void qa_app_application_q3_client_receive(qa_app_application_q3_client *client, const void *events, size_t count) {
  (void)events;
  require_game(client);
  if (client->host.options.local_source == NULL)
    qa_range_error("Remote Q3 cgame receives snapshots through its network connection");
  client->event_count += (double)count;
}
bool qa_app_application_q3_client_command(qa_app_application_q3_client *client, const char *const *argv, size_t argc) {
  require_backend(client);
  return client->host.game.command(client->host.game.ctx, argv, argc);
}
bool qa_app_application_q3_client_handles_command(qa_app_application_q3_client *client, const char *name) {
  require_backend(client);
  return client->host.game.handles(client->host.game.ctx, name);
}
void qa_app_application_q3_client_key_event(qa_app_application_q3_client *client, double key, bool down) {
  require_backend(client);
  const char *error = client->host.game.key_event(client->host.game.ctx, key, down);
  if (error != NULL) qa_range_error("%s", error);
}
void qa_app_application_q3_client_mouse_event(qa_app_application_q3_client *client, double x, double y) {
  require_backend(client);
  const char *error = client->host.game.mouse_event(client->host.game.ctx, x, y);
  if (error != NULL) qa_range_error("%s", error);
}
void qa_app_application_q3_client_event_handling(qa_app_application_q3_client *client, double type) {
  if (type != trunc(type) || type < 0 || type > 3) qa_range_error("Invalid Q3 event handling mode");
  require_backend(client);
  const char *error = client->host.game.event_handling(client->host.game.ctx, type);
  if (error != NULL) qa_range_error("%s", error);
}
bool qa_app_application_q3_client_captures_input(const qa_app_application_q3_client *client) {
  if (!client->initialized) return false;
  if (is(client->host.options.kind, "qvm"))
    return client->host.game.captures_input(client->host.game.ctx);
  return client->key_catcher != 0;
}
bool qa_app_application_q3_client_weapon_hud_view(qa_app_application_q3_client *client, bool *visible, bool *aggregate_warning) {
  require_backend(client);
  if (is(client->host.options.kind, "qvm")) {
    *visible = client->host.game.shared_equipment_hud;
    *aggregate_warning = false;
    return true;
  }
  return client->host.game.weapon_view(client->host.game.ctx, visible, aggregate_warning);
}
bool qa_app_application_q3_client_consume_weapon_selection(qa_app_application_q3_client *client, double *out) {
  require_backend(client);
  bool has_value = false;
  const char *error = client->host.game.consume_weapon_selection(client->host.game.ctx, &has_value, out);
  if (error != NULL) qa_range_error("%s", error);
  return has_value;
}
qa_scene_camera qa_app_application_q3_client_camera(const qa_app_application_q3_client *client) {
  return client->latest_camera;
}
void qa_app_application_q3_client_set_camera(qa_app_application_q3_client *client, qa_scene_camera camera) {
  client->latest_camera = camera;
}
bool qa_app_application_q3_client_body_pose(const qa_app_application_q3_client *client,
    qa_actor_id actor, qa_vec3 *origin_out, qa_vec3 *angles_out) {
  for (size_t i = 0; i < client->pose_count; i++) {
    if (qa_same_actor(client->poses[i].actor, actor)) {
      *origin_out = client->poses[i].origin;
      *angles_out = client->poses[i].angles;
      return true;
    }
  }
  return false;
}
void qa_app_application_q3_client_set_body_pose(qa_app_application_q3_client *client,
    qa_actor_id actor, qa_vec3 origin, qa_vec3 angles) {
  for (size_t i = 0; i < client->pose_count; i++) {
    if (qa_same_actor(client->poses[i].actor, actor)) {
      client->poses[i].origin = origin;
      client->poses[i].angles = angles;
      return;
    }
  }
  if (client->pose_count == client->pose_capacity) {
    size_t cap = client->pose_capacity == 0 ? 8 : client->pose_capacity * 2;
    pose_entry *next = realloc(client->poses, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    client->poses = next; client->pose_capacity = cap;
  }
  client->poses[client->pose_count].actor = actor;
  client->poses[client->pose_count].origin = origin;
  client->poses[client->pose_count].angles = angles;
  client->pose_count++;
}
void qa_app_application_q3_client_assert_seat(qa_app_application_q3_client *client, qa_seat_id seat) {
  if (!qa_same_seat(seat, client->host.options.seat)) qa_range_error("Q3 input belongs to another seat");
}
void qa_app_application_q3_client_refresh_system_info(qa_app_application_q3_client *client) {
  if (client->closed) qa_range_error("Q3 presentation is closed");
  /* System-info fields are re-applied by the backend host during
   * prepare; the refresh itself only validates liveness here. */
}
const char *qa_app_application_q3_client_prepare(qa_app_application_q3_client *client, double frame_number) {
  require_backend(client);
  return client->host.prepare(client->host.ctx, frame_number);
}
const char *qa_app_application_q3_client_frame(qa_app_application_q3_client *client) {
  require_backend(client);
  return client->host.frame(client->host.ctx);
}
void qa_app_application_q3_client_shutdown(qa_app_application_q3_client *client) {
  if (client != NULL && is(client->host.options.kind, "qvm") && client->initialized) {
    const char *error = client->host.game.shutdown(client->host.game.ctx);
    qa_app_application_q3_client_close(client);
    if (error != NULL) qa_range_error("%s", error);
    return;
  }
  qa_app_application_q3_client_close(client);
}
void qa_app_application_q3_client_close(qa_app_application_q3_client *client) {
  if (client == NULL || client->closed) return;
  client->closed = true;
  const char *error = client->host.close != NULL ? client->host.close(client->host.ctx) : NULL;
  free(client->poses);
  free(client);
  if (error != NULL) qa_range_error("%s", error);
}
