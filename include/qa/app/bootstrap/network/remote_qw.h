/* Port of src/app/bootstrap/network/remote-qw.ts
 * (QwRemotePresentation). Wraps the shared Q1 presentation;
 * camera trace and sound resolution are options host seams.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_REMOTE_QW_H
#define QA_APP_BOOTSTRAP_NETWORK_REMOTE_QW_H
#include <stdbool.h>
#include "qa/app/bootstrap/network/qw_camera.h"
#include "qa/app/bootstrap/network/qw_skins.h"
#include "qa/app/bootstrap/network/qw_types.h"
#include "qa/app/bootstrap/network/remote_q1.h"
#include "qa/network/q1/quakeworld.h"
#include "qa/world/spatial/index.h"
typedef void (*qa_app_qw_remote_assert_fn)(void *ctx);
typedef struct qa_app_qw_remote_options {
  qa_app_q1_remote_options shared;
  qa_seat_id seat;
  bool has_downloads; qa_app_qw_downloads downloads;
  qa_app_qw_skin_options skin_options;
  bool has_camera_options; qa_app_qw_camera_options camera_options;
  /* Camera trace over the shared scene queries object. */
  qa_app_qw_camera_trace (*camera_trace)(void *ctx, void *scene, qa_vec3 start, qa_vec3 end,
      bool has_pass_actor, qa_actor_id pass_actor);
  void (*prepare_server_data)(void *ctx, const qa_quakeworld_message *data);
  double (*map_checksum)(void *ctx, qa_app_q1_remote_world_info world, const char *game_directory);
  bool (*resolve_sound)(void *ctx, void *loaded, const char *path);
  qa_owned_actor (*owned_actor)(void *ctx, qa_actor_id actor, const void *provider);
  /* Scene link seam (same shape as remote Q2). */
  void (*scene_link)(void *ctx, void *scene, qa_linked_body body, qa_actor_collision collision);
  void (*scene_unlink)(void *ctx, void *scene, qa_actor_id actor);
  qa_bounds (*scene_model_bounds)(void *ctx, void *scene, double model);
  double (*now_milliseconds)(void *ctx); /* performance.now seam */
} qa_app_qw_remote_options;
typedef struct qa_app_qw_remote qa_app_qw_remote;
qa_app_qw_remote *qa_app_qw_remote_create(qa_app_qw_remote_options options);
void qa_app_qw_remote_destroy(qa_app_qw_remote *remote);
qa_app_q1_remote *qa_app_qw_remote_shared(qa_app_qw_remote *remote);
bool qa_app_qw_remote_move_variables(const qa_app_qw_remote *remote, qa_qw_move_variables *out);
qa_client_id qa_app_qw_remote_client(const qa_app_qw_remote *remote);
bool qa_app_qw_remote_player(const qa_app_qw_remote *remote, qa_app_q1_player *out);
const qa_simulation_output *qa_app_qw_remote_output(const qa_app_qw_remote *remote);
void *qa_app_qw_remote_scene(qa_app_qw_remote *remote);
const qa_quakeworld_message *qa_app_qw_remote_source_records(const qa_app_qw_remote *remote,
    size_t *out_count);
size_t qa_app_qw_remote_scoreboard(const qa_app_qw_remote *remote,
    const qa_app_q1_client_row **out_rows);
void qa_app_qw_remote_prepare_skins(qa_app_qw_remote *remote,
    qa_app_qw_remote_assert_fn assert_current, void *assert_ctx);
bool qa_app_qw_remote_take_spectator_teleport(qa_app_qw_remote *remote, qa_vec3 *out);
void qa_app_qw_remote_server_data(qa_app_qw_remote *remote, const qa_quakeworld_message *data);
double qa_app_qw_remote_game_state(qa_app_qw_remote *remote, const qa_quakeworld_message *data,
    const char *const *models, size_t model_count,
    const char *const *sounds, size_t sound_count,
    qa_app_qw_remote_assert_fn assert_current, void *assert_ctx);
void qa_app_qw_remote_receive(qa_app_qw_remote *remote,
    const qa_quakeworld_message *messages, size_t count, double now,
    qa_app_qw_remote_assert_fn assert_current, void *assert_ctx);
qa_qw_user_command qa_app_qw_remote_command(qa_app_qw_remote *remote,
    const qa_actor_command *input);
bool qa_app_qw_remote_is_player(qa_app_qw_remote *remote, qa_actor_id actor);
const qa_world_text *qa_app_qw_remote_world_text(qa_app_qw_remote *remote, size_t *out_count);
qa_app_sim_player_ui qa_app_qw_remote_player_ui(qa_app_qw_remote *remote, qa_actor_id actor);
qa_app_sim_player_view qa_app_qw_remote_player_view(qa_app_qw_remote *remote, qa_actor_id actor);
void qa_app_qw_remote_player_command(qa_app_qw_remote *remote, qa_actor_id actor,
    const char *name, const char *const *args, size_t count);
const void *qa_app_qw_remote_character_views(qa_app_qw_remote *remote, size_t *out_count);
const qa_app_simulation_presentation *qa_app_qw_remote_presentations(qa_app_qw_remote *remote,
    size_t *out_count);
void qa_app_qw_remote_register_resource(qa_app_qw_remote *remote, qa_content_id content,
    const char *path, qa_resolved_resource_reference resource);
const qa_simulation_output *qa_app_qw_remote_sample_presentation(qa_app_qw_remote *remote,
    double now);
const qa_app_sim_presentation_event *qa_app_qw_remote_drain_events(qa_app_qw_remote *remote,
    size_t *out_count);
void qa_app_qw_remote_prediction_sent(qa_app_qw_remote *remote, double sequence,
    const qa_qw_user_command *command, double now);
void qa_app_qw_remote_prediction_acknowledged(qa_app_qw_remote *remote,
    double sequence, double now);
const char *const *qa_app_qw_remote_skin_names(qa_app_qw_remote *remote, size_t *out_count);
void qa_app_qw_remote_skins_loading(qa_app_qw_remote *remote, bool value);
void qa_app_qw_remote_skins_prepare(qa_app_qw_remote *remote);
void qa_app_qw_remote_disconnected(qa_app_qw_remote *remote, const char *reason);
void qa_app_qw_remote_print(qa_app_qw_remote *remote, const char *text);
qa_app_qw_client_host qa_app_qw_remote_as_client_host(qa_app_qw_remote *remote);
#endif
