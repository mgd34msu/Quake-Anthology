/* Port of src/app/bootstrap/network/remote-q3.ts
 * (Q3RemotePresentation). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_REMOTE_Q3_H
#define QA_APP_BOOTSTRAP_NETWORK_REMOTE_Q3_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/q3_client.h"
#include "qa/app/bootstrap/network/q3_types.h"
#include "qa/app/bootstrap/network/remote_world.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/session.h"
#include "qa/content/q3/presentation/snapshots.h"
#include "qa/network/q3/clock.h"
#include "qa/network/q3/client.h"
#include "qa/network/q3/download.h"
#include "qa/text/world.h"
struct qa_app_q3_client_source;
typedef struct qa_app_q3_client_source qa_app_q3_client_source;
typedef void (*qa_app_q3_remote_assert_fn)(void *ctx);
typedef struct qa_app_q3_remote_downloads {
  void *ctx;
  bool (*prepare)(void *ctx, qa_q3_client_connection *connection);
  double (*publish_size)(void *ctx, double size);
  void (*receive)(void *ctx, const qa_q3_download_block *block);
  void (*close)(void *ctx);
} qa_app_q3_remote_downloads;
typedef struct qa_app_q3_remote_options {
  void *ctx;
  qa_app_remote_world_host world_host;
  void *content;
  qa_session_id session;
  qa_client_id client;
  double (*next_generation)(void *ctx, double slot);
  qa_actor_id (*identity_actor)(void *ctx, double slot, double generation);
  qa_owned_actor (*owned_actor)(void *ctx, qa_actor_id actor, const void *provider);
  const char *(*userinfo)(void *ctx);
  double (*time_nudge)(void *ctx); /* NULL = absent */
  double (*timescale)(void *ctx);  /* NULL = absent */
  double (*presentation_time)(void *ctx); /* NULL = absent */
  void *(*load_content)(void *ctx, const char *map,
      const char *const *models, size_t model_count,
      const char *const *sounds, size_t sound_count, qa_q3_client_connection *connection);
  const qa_executable_recipe *(*content_recipe)(void *ctx, void *loaded);
  const qa_scene_snapshot_world *(*content_world)(void *ctx, void *loaded);
  bool has_downloads; qa_app_q3_remote_downloads downloads;
  void (*shutdown)(void *ctx);    /* NULL = absent */
  void (*initialize)(void *ctx, qa_q3_client_connection *connection); /* NULL = absent */
  void (*send_command)(void *ctx, const char *text);
  void (*disconnected)(void *ctx, const char *reason);
  void (*publish)(void *ctx, const qa_simulation_output *output);
  void (*print)(void *ctx, const char *text);
} qa_app_q3_remote_options;
typedef struct qa_app_q3_remote qa_app_q3_remote;
qa_app_q3_remote *qa_app_q3_remote_create(qa_app_q3_remote_options options);
void qa_app_q3_remote_destroy(qa_app_q3_remote *remote);
qa_client_id qa_app_q3_remote_client(const qa_app_q3_remote *remote);
const char *qa_app_q3_remote_userinfo(qa_app_q3_remote *remote);
qa_q3_client_clock *qa_app_q3_remote_clock(qa_app_q3_remote *remote);
bool qa_app_q3_remote_downloading(const qa_app_q3_remote *remote);
void *qa_app_q3_remote_scene(qa_app_q3_remote *remote);
void qa_app_q3_remote_bind_collision_settings(qa_app_q3_remote *remote, const void *settings);
const qa_simulation_output *qa_app_q3_remote_output(const qa_app_q3_remote *remote);
bool qa_app_q3_remote_player(const qa_app_q3_remote *remote, qa_app_q3_player *out);
qa_app_q3_player qa_app_q3_remote_admitted_player(qa_app_q3_remote *remote);
const qa_q3_player_state_record *qa_app_q3_remote_initial_player(qa_app_q3_remote *remote);
const qa_app_q3_client_source *qa_app_q3_remote_cgame_source(qa_app_q3_remote *remote);
const char *const *qa_app_q3_remote_source_records(qa_app_q3_remote *remote, size_t *out_count);
qa_actor_id qa_app_q3_remote_actor_at(qa_app_q3_remote *remote, double number);
bool qa_app_q3_remote_number_of(qa_app_q3_remote *remote, qa_actor_id actor, double *out);
bool qa_app_q3_remote_is_player(qa_app_q3_remote *remote, qa_actor_id actor);
void qa_app_q3_remote_attach(qa_app_q3_remote *remote, qa_q3_client_connection *connection);
void qa_app_q3_remote_clear_active(qa_app_q3_remote *remote,
    qa_app_q3_remote_assert_fn assert_current, void *assert_ctx);
void qa_app_q3_remote_system_info(qa_app_q3_remote *remote, const char *info);
void qa_app_q3_remote_gamestate(qa_app_q3_remote *remote, const qa_q3_gamestate *state,
    double generation, qa_app_q3_remote_assert_fn assert_current, void *assert_ctx);
double qa_app_q3_remote_download_size(qa_app_q3_remote *remote, double size);
void qa_app_q3_remote_download(qa_app_q3_remote *remote, const qa_q3_download_block *block);
void qa_app_q3_remote_snapshot(qa_app_q3_remote *remote,
    const qa_q3_wire_snapshot *snapshot, double ping);
void qa_app_q3_remote_map_restart(qa_app_q3_remote *remote);
const qa_world_text *qa_app_q3_remote_world_text(qa_app_q3_remote *remote, size_t *out_count);
qa_app_sim_player_view qa_app_q3_remote_player_view(qa_app_q3_remote *remote, qa_actor_id actor);
qa_app_sim_player_ui qa_app_q3_remote_player_ui(qa_app_q3_remote *remote, qa_actor_id actor);
const void *qa_app_q3_remote_character_views(qa_app_q3_remote *remote, size_t *out_count);
const void *qa_app_q3_remote_presentations(qa_app_q3_remote *remote, size_t *out_count);
void qa_app_q3_remote_register_resource(qa_app_q3_remote *remote, qa_content_id content,
    const char *path, qa_resolved_resource_reference resource);
void qa_app_q3_remote_player_command(qa_app_q3_remote *remote, qa_actor_id actor,
    const char *name, const char *const *args, size_t count);
qa_q3_wire_user_command qa_app_q3_remote_command(qa_app_q3_remote *remote,
    const qa_actor_command *command);
const qa_simulation_output *qa_app_q3_remote_sample_presentation(qa_app_q3_remote *remote,
    double now);
struct qa_app_presentation_prediction_adapter;
struct qa_app_presentation_prediction_adapter *qa_app_q3_remote_movement(
    qa_app_q3_remote *remote, qa_seat_id seat);
const qa_app_sim_presentation_event *qa_app_q3_remote_drain_events(qa_app_q3_remote *remote,
    size_t *out_count);
void qa_app_q3_remote_print(qa_app_q3_remote *remote, const char *text);
void qa_app_q3_remote_disconnected(qa_app_q3_remote *remote, const char *reason);
qa_app_q3_client_host qa_app_q3_remote_as_client_host(qa_app_q3_remote *remote);
#endif
