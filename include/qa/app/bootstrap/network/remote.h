/* Port of src/app/bootstrap/network/remote.ts
 * (Q2RemotePresentation + q2RemoteEntityBounds). Loaded content
 * access (recipe/world/mount read/catalog rebind) comes through the
 * options host; the world scene is the qa_shared_scene_queries the
 * remote world host creates, linked and traced directly. Events are
 * SimulationPresentationEvent (kind "q2"/"q2-player"/"q2-weapon").
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_REMOTE_H
#define QA_APP_BOOTSTRAP_NETWORK_REMOTE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/network/client_download_policy.h"
#include "qa/app/bootstrap/network/q2_downloads.h"
#include "qa/app/bootstrap/network/q2_layout.h"
#include "qa/app/bootstrap/network/q2_mvd_presentation.h"
#include "qa/app/bootstrap/network/remote_world.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/session.h"
#include "qa/network/q2/frames.h"
#include "qa/content/q3/foundation/presentation.h"
#include "qa/network/q2/server_messages.h"
#include "qa/text/world.h"
qa_bounds qa_app_q2_remote_entity_bounds(double solid, bool long_solid);
typedef void (*qa_app_q2_cinematic_ended_fn)(void *ctx);
typedef struct qa_app_q2_remote_cinematic {
  void *ctx;
  void (*start)(void *ctx, const char *name, qa_app_q2_cinematic_ended_fn ended, void *ended_ctx);
  void (*stop)(void *ctx);
} qa_app_q2_remote_cinematic;
typedef void (*qa_app_q2_remote_assert_fn)(void *ctx);
typedef struct qa_app_q2_remote_options {
  void *ctx;
  qa_app_remote_world_host world_host;
  void *content; /* LoadedApplicationContent or NULL */
  bool has_recorded_profile; qa_q2_mvd_profile recorded_profile;
  qa_seat_id seat;
  qa_session_id session;
  qa_client_id client;
  double (*next_generation)(void *ctx, double slot);
  qa_actor_id (*identity_actor)(void *ctx, double slot, double generation);
  qa_owned_actor (*owned_actor)(void *ctx, qa_actor_id actor, const void *provider);
  qa_q2_protocol_identity protocol;
  const char *(*userinfo)(void *ctx);
  const qa_app_client_download_permission *download_permission; /* NULL = TS undefined */
  bool has_cinematic; qa_app_q2_remote_cinematic cinematic;
  void (*publish)(void *ctx, const qa_simulation_output *output);
  void (*disconnected)(void *ctx, const char *reason);
  double (*presentation_time)(void *ctx); /* NULL = absent */
  void (*print)(void *ctx, const char *text);
  void (*send_command)(void *ctx, const char *text);
  /* NULL = TS undefined (world.content used). */
  void *(*load_content)(void *ctx, const qa_app_q2_game_state *state);
  const qa_app_q2_remote_mounts *(*prepare_server_data)(void *ctx, const void *data,
      qa_app_q2_remote_assert_fn assert_current, void *assert_ctx);
  const qa_app_q2_remote_mounts *(*refresh_downloads)(void *ctx,
      qa_app_q2_remote_assert_fn assert_current, void *assert_ctx);
  /* Content accessors over a loaded handle. */
  const qa_executable_recipe *(*content_recipe)(void *ctx, void *loaded);
  const qa_scene_snapshot_world *(*content_world)(void *ctx, void *loaded);
  const uint8_t *(*content_read)(void *ctx, void *loaded,
      const qa_resolved_resource_reference *reference, size_t *out_length);
  const qa_app_q2_remote_mounts *(*rebind_download_content)(void *ctx, const qa_app_q2_remote_mounts *owner, void *loaded);
} qa_app_q2_remote_options;
typedef struct qa_app_q2_remote qa_app_q2_remote;
qa_app_q2_remote *qa_app_q2_remote_create(qa_app_q2_remote_options options);
void qa_app_q2_remote_destroy(qa_app_q2_remote *remote);
qa_app_q2_download_receiver *qa_app_q2_remote_downloads(qa_app_q2_remote *remote);
size_t qa_app_q2_remote_download_progress(qa_app_q2_remote *remote,
    qa_app_client_download_progress *out, size_t capacity);
void qa_app_q2_remote_cancel_downloads(qa_app_q2_remote *remote);
void qa_app_q2_remote_retry_downloads(qa_app_q2_remote *remote);
qa_client_id qa_app_q2_remote_client(const qa_app_q2_remote *remote);
qa_q2_protocol_identity qa_app_q2_remote_protocol(const qa_app_q2_remote *remote);
void qa_app_q2_remote_message_options(const qa_app_q2_remote *remote,
    qa_q2_server_message_options *out);
const char *qa_app_q2_remote_userinfo(qa_app_q2_remote *remote);
qa_q2_mvd_visibility qa_app_q2_remote_mvd_visibility(qa_app_q2_remote *remote);
void qa_app_q2_remote_select_recorded_view(qa_app_q2_remote *remote, double clientnum);
qa_app_q2_layout qa_app_q2_remote_config_layout(const qa_app_q2_remote *remote);
bool qa_app_q2_remote_player_of(const qa_app_q2_remote *remote, qa_app_q2_player *out);
const qa_simulation_output *qa_app_q2_remote_output(const qa_app_q2_remote *remote);
const qa_q2_server_record *qa_app_q2_remote_source_records(const qa_app_q2_remote *remote,
    size_t *out_count);
const char *qa_app_q2_remote_native_layout(const qa_app_q2_remote *remote);
void *qa_app_q2_remote_scene(qa_app_q2_remote *remote);
bool qa_app_q2_remote_is_player(qa_app_q2_remote *remote, qa_actor_id actor);
void qa_app_q2_remote_server_data(qa_app_q2_remote *remote, const void *data,
    qa_app_q2_remote_assert_fn assert_current, void *assert_ctx);
void qa_app_q2_remote_game_state(qa_app_q2_remote *remote, qa_app_q2_game_state state);
const qa_world_text *qa_app_q2_remote_world_text(qa_app_q2_remote *remote, size_t *out_count);
qa_app_sim_player_view qa_app_q2_remote_player_view(qa_app_q2_remote *remote, qa_actor_id actor);
qa_app_sim_player_ui qa_app_q2_remote_player_ui(qa_app_q2_remote *remote, qa_actor_id actor);
const qa_q3_character_view *qa_app_q2_remote_character_views(qa_app_q2_remote *remote,
    size_t *out_count);
const qa_app_simulation_presentation *qa_app_q2_remote_presentations(qa_app_q2_remote *remote,
    size_t *out_count);
void qa_app_q2_remote_register_resource(qa_app_q2_remote *remote, qa_content_id content,
    const char *path, qa_resolved_resource_reference resource);
void qa_app_q2_remote_player_command(qa_app_q2_remote *remote, qa_actor_id actor,
    const char *name, const char *const *args, size_t count);
qa_q2_wire_usercmd qa_app_q2_remote_wire_command(qa_app_q2_remote *remote,
    const qa_actor_command *command);
void qa_app_q2_remote_frame(qa_app_q2_remote *remote, qa_q2_wire_frame frame,
    const qa_q2_server_record *records, size_t count, double now_milliseconds);
const qa_simulation_output *qa_app_q2_remote_sample_presentation(qa_app_q2_remote *remote,
    double now_milliseconds);
const qa_simulation_output *qa_app_q2_remote_sample_demo(qa_app_q2_remote *remote,
    double recorded_milliseconds);
void qa_app_q2_remote_records(qa_app_q2_remote *remote,
    const qa_q2_server_record *records, size_t count);
/* TS drainPresentationEvents. The events are owned copies the remote keeps
 * until the next drain or destroy. */
const qa_app_sim_presentation_event *qa_app_q2_remote_drain_events(qa_app_q2_remote *remote,
    size_t *out_count);
void qa_app_q2_remote_prediction_acknowledged(qa_app_q2_remote *remote,
    double sequence, double now_milliseconds);
void qa_app_q2_remote_prediction_sent(qa_app_q2_remote *remote,
    double sequence, const qa_q2_wire_usercmd *command, double now_milliseconds);
void qa_app_q2_remote_disconnected(qa_app_q2_remote *remote, const char *reason);
void qa_app_q2_remote_print(qa_app_q2_remote *remote, const char *text);
/* Q2ApplicationClientHost view over this presentation. */
qa_app_q2_client_host qa_app_q2_remote_as_client_host(qa_app_q2_remote *remote);
#endif
