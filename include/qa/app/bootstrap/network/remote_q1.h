/* Port of src/app/bootstrap/network/remote-q1.ts
 * (Q1RemotePresentation). Events are SimulationPresentationEvent (kind
 * "q1"/"view-reset"/"music"/"q1-client"/"q1-sky"). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_REMOTE_Q1_H
#define QA_APP_BOOTSTRAP_NETWORK_REMOTE_Q1_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/q1_types.h"
#include "qa/app/bootstrap/network/remote_world.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/app/bootstrap/q1_service_presentation.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/session.h"
#include "qa/network/q1/netquake.h"
struct qa_app_q1_client_host;
typedef struct qa_app_q1_client_host qa_app_q1_client_host;
#include "qa/text/world.h"
typedef struct qa_app_q1_remote_world_info {
  const char *map;
  const char *const *models; size_t model_count;
  const char *const *sounds; size_t sound_count;
} qa_app_q1_remote_world_info;
typedef void (*qa_app_q1_remote_assert_fn)(void *ctx);
typedef struct qa_app_q1_remote_options {
  void *ctx;
  qa_app_remote_world_host world_host;
  void *content; /* LoadedApplicationContent or NULL */
  double (*presentation_time)(void *ctx); /* NULL = absent */
  qa_session_id session;
  qa_client_id client;
  double (*next_generation)(void *ctx, double slot);
  qa_actor_id (*identity_actor)(void *ctx, double slot, double generation);
  void *(*load_content)(void *ctx, qa_app_q1_remote_world_info world,
      qa_app_q1_remote_assert_fn assert_current, void *assert_ctx);
  const qa_executable_recipe *(*content_recipe)(void *ctx, void *loaded);
  const qa_scene_snapshot_world *(*content_world)(void *ctx, void *loaded);
  void (*send_command)(void *ctx, const char *text);
  void (*print)(void *ctx, const char *text);
  void (*publish)(void *ctx, const qa_simulation_output *output);
  void (*disconnected)(void *ctx, const char *reason);
} qa_app_q1_remote_options;
typedef struct qa_app_q1_remote qa_app_q1_remote;
qa_app_q1_remote *qa_app_q1_remote_create(qa_app_q1_remote_options options);
void qa_app_q1_remote_destroy(qa_app_q1_remote *remote);
qa_client_id qa_app_q1_remote_client(const qa_app_q1_remote *remote);
void *qa_app_q1_remote_scene(qa_app_q1_remote *remote);
const qa_simulation_output *qa_app_q1_remote_output(const qa_app_q1_remote *remote);
const qa_netquake_message *qa_app_q1_remote_source_records(const qa_app_q1_remote *remote,
    size_t *out_count);
bool qa_app_q1_remote_player(const qa_app_q1_remote *remote, qa_app_q1_player *out);
bool qa_app_q1_remote_player_slot(qa_app_q1_remote *remote, qa_actor_id actor, double *out);
bool qa_app_q1_remote_is_player(qa_app_q1_remote *remote, qa_actor_id actor);
size_t qa_app_q1_remote_scoreboard(const qa_app_q1_remote *remote,
    const qa_app_q1_client_row **out_rows);
void qa_app_q1_remote_receive(qa_app_q1_remote *remote,
    const qa_netquake_message *messages, size_t count, double now,
    qa_app_q1_remote_assert_fn assert_current, void *assert_ctx);
const qa_world_text *qa_app_q1_remote_world_text(qa_app_q1_remote *remote, size_t *out_count);
qa_app_sim_player_view qa_app_q1_remote_player_view(qa_app_q1_remote *remote, qa_actor_id actor);
qa_app_sim_player_ui qa_app_q1_remote_player_ui(qa_app_q1_remote *remote, qa_actor_id actor);
const void *qa_app_q1_remote_character_views(qa_app_q1_remote *remote, size_t *out_count);
const qa_app_simulation_presentation *qa_app_q1_remote_presentations(qa_app_q1_remote *remote,
    size_t *out_count);
void qa_app_q1_remote_register_resource(qa_app_q1_remote *remote, qa_content_id content,
    const char *path, qa_resolved_resource_reference resource);
qa_q1_user_command qa_app_q1_remote_command(qa_app_q1_remote *remote,
    const qa_actor_command *command);
void qa_app_q1_remote_player_command(qa_app_q1_remote *remote, qa_actor_id actor,
    const char *name, const char *const *args, size_t count);
const qa_simulation_output *qa_app_q1_remote_sample_presentation(qa_app_q1_remote *remote,
    double now);
double qa_app_q1_remote_recorded_seconds(const qa_app_q1_remote *remote);
void qa_app_q1_remote_set_demo_view_angles(qa_app_q1_remote *remote,
    qa_vec3 current, bool interpolate);
const qa_simulation_output *qa_app_q1_remote_sample_demo(qa_app_q1_remote *remote, double seconds);
/* TS drainPresentationEvents: none (NULL, 0) before the first publish. The
 * events are owned copies the remote keeps until the next drain or destroy. */
const qa_app_sim_presentation_event *qa_app_q1_remote_drain_events(qa_app_q1_remote *remote,
    size_t *out_count);
void qa_app_q1_remote_disconnected(qa_app_q1_remote *remote, const char *reason);
qa_app_q1_client_host qa_app_q1_remote_as_client_host(qa_app_q1_remote *remote);
#endif
