/* Port of src/app/bootstrap/network/remote-unified.ts
 * (UnifiedRemotePresentation). Loaded content rides as the
 * remote-world opaque handle; prepared mods, recipe and resource
 * resolution come through the options host. Presentation events are
 * decoded as unified event trees and received into the replica's own
 * PresentationState (media) as typed events (unified_event_values.h);
 * the consumers admit and retire owners on it. Scene link/unlink (TS
 * createSceneQueries surface beyond landed qa_scene_queries) is a
 * host seam. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_REMOTE_UNIFIED_H
#define QA_APP_BOOTSTRAP_NETWORK_REMOTE_UNIFIED_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/network/remote_world.h"
#include "qa/app/bootstrap/network/unified_components.h"
#include "qa/app/bootstrap/network/unified_control.h"
#include "qa/app/bootstrap/network/unified_frame_codec.h"
#include "qa/app/bootstrap/simulation/prediction/runtime.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/q3/foundation/presentation.h"
#include "qa/contracts/scene.h"
#include "qa/world/session/mods.h"
#include "qa/world/spatial/index.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"
#include "qa/text/world.h"
typedef void (*qa_app_unified_operation_fn)(void *ctx);
typedef struct qa_app_unified_remote_options {
  void *ctx;
  qa_app_remote_world_host world_host;
  qa_session_id session;
  qa_client_id client;
  qa_seat_id seat;
  double (*next_generation)(void *ctx, double slot);
  qa_actor_id (*identity_actor)(void *ctx, double slot, double generation);
  qa_client_id (*identity_client)(void *ctx, double slot, double generation);
  qa_owned_actor (*owned_actor)(void *ctx, qa_actor_id actor, const void *provider);
  void *(*load_content)(void *ctx, const qa_app_unified_control *offer,
      qa_app_unified_operation_fn assert_current, void *assert_ctx);
  const qa_prepared_mod *(*prepared_mods)(void *ctx, void *loaded, size_t *out_count);
  bool (*mod_has_presentation)(void *ctx, void *loaded);
  const qa_executable_recipe *(*recipe)(void *ctx, void *loaded);
  const void *(*map_entities_provider)(void *ctx, void *loaded);
  /* TS resolveUnifiedResource(content, key). */
  qa_resolved_resource_reference (*resolve_resource)(void *ctx, void *loaded,
      const qa_app_unified_resource_key *key);
  qa_decoded_model (*model)(void *ctx, const qa_app_unified_resource_key *key,
      bool has_brush_model, double brush_model);
  /* Frame decoder world/resource: recipe map geometry + world. */
  qa_resolved_resource_reference (*world_resource)(void *ctx, void *loaded);
  const qa_scene_snapshot_world *(*world_geometry)(void *ctx, void *loaded);
  void (*publish)(void *ctx, const qa_simulation_output *output);
  /* NULL = TS undefined. Wraps the operation thunk. */
  void (*publish_components)(void *ctx, qa_app_unified_operation_fn operation, void *operation_ctx);
  void (*send_command)(void *ctx, const char *name, const char *const *args, size_t count);
  bool has_mod_files;
  /* Consumer mounts host (content owner). */
  const void *(*installed_mounts)(void *ctx, qa_content_id content);
  const void *(*borrow_mounts)(void *ctx, const char *selection, qa_content_id content,
      const void *installed, const void **out_writable);
  void (*close_mounts)(void *ctx, const void *mounts);
  void (*product_expectation)(void *ctx, qa_content_id content,
      const char **family, const char **edition);
  /* NULL = TS undefined. */
  void (*send_component_command)(void *ctx, qa_presentation_owner owner, double generation,
      const char *const *args, size_t count);
  /* Scene link seam over remote-world queries object. */
  void (*scene_link)(void *ctx, void *scene, qa_linked_body body, qa_actor_collision collision);
  void (*scene_unlink)(void *ctx, void *scene, qa_actor_id actor);
  bool (*scene_actor_is_model)(void *ctx, void *scene, qa_actor_id actor);
  void (*disconnected)(void *ctx, const char *reason);
  void (*print)(void *ctx, const char *text);
} qa_app_unified_remote_options;
typedef struct qa_app_unified_remote qa_app_unified_remote;
qa_app_unified_remote *qa_app_unified_remote_create(qa_app_unified_remote_options options);
void qa_app_unified_remote_destroy(qa_app_unified_remote *remote);
qa_client_id qa_app_unified_remote_client(const qa_app_unified_remote *remote);
const char *qa_app_unified_remote_composition_digest(const qa_app_unified_remote *remote);
double qa_app_unified_remote_epoch(const qa_app_unified_remote *remote);
double qa_app_unified_remote_loaded_epoch(const qa_app_unified_remote *remote);
void *qa_app_unified_remote_scene(qa_app_unified_remote *remote);
const qa_simulation_output *qa_app_unified_remote_output(const qa_app_unified_remote *remote);
bool qa_app_unified_remote_player(const qa_app_unified_remote *remote,
    qa_actor_id *actor, qa_client_id *client, double *source_entity);
void qa_app_unified_remote_offer(qa_app_unified_remote *remote,
    const qa_app_unified_control *offer);
void qa_app_unified_remote_admitted(qa_app_unified_remote *remote,
    const qa_app_unified_control *binding);
void qa_app_unified_remote_receive_components(qa_app_unified_remote *remote,
    double epoch, const qa_app_unified_component_update *update);
const struct qa_active_mod_presentation *qa_app_unified_remote_mod_presentation_sources(qa_app_unified_remote *remote,
    size_t *out_count);
const qa_active_mod_client_presentation *qa_app_unified_remote_mod_client_sources(
    qa_app_unified_remote *remote, size_t *out_count);
void qa_app_unified_remote_declare(qa_app_unified_remote *remote, double epoch,
    const qa_app_unified_resource_key *keys, size_t count);
void qa_app_unified_remote_receive_events(qa_app_unified_remote *remote, double epoch,
    double frame, const uint8_t *payload, size_t payload_length,
    const uint8_t *simulation, size_t simulation_length);
/* false = TS null (acknowledged); *out acknowledged input. */
bool qa_app_unified_remote_receive_frame(qa_app_unified_remote *remote,
    const uint8_t *bytes, size_t length, double *out_acknowledged);
bool qa_app_unified_remote_command_time(const qa_app_unified_remote *remote, double *out);
void qa_app_unified_remote_predict(qa_app_unified_remote *remote,
    qa_app_prediction_command command);
const qa_simulation_output *qa_app_unified_remote_sample_presentation(
    qa_app_unified_remote *remote, double now, qa_simulation_output *scratch);
const qa_app_sim_player_ui *qa_app_unified_remote_player_ui(qa_app_unified_remote *remote,
    qa_actor_id actor);
qa_app_sim_player_view qa_app_unified_remote_player_view(qa_app_unified_remote *remote,
    qa_actor_id actor);
const qa_app_simulation_presentation *qa_app_unified_remote_presentations(
    qa_app_unified_remote *remote, size_t *out_count);
const qa_q3_character_view *qa_app_unified_remote_character_views(
    qa_app_unified_remote *remote, size_t *out_count);
const qa_world_text *qa_app_unified_remote_world_text(qa_app_unified_remote *remote,
    size_t *out_count);
bool qa_app_unified_remote_is_player(qa_app_unified_remote *remote, qa_actor_id actor);
bool qa_app_unified_remote_number_of(qa_app_unified_remote *remote, qa_actor_id actor,
    double *out);
void qa_app_unified_remote_player_command(qa_app_unified_remote *remote, qa_actor_id actor,
    const char *name, const char *const *args, size_t count);
void qa_app_unified_remote_register_resource(qa_app_unified_remote *remote,
    qa_content_id content, const char *path, qa_resolved_resource_reference resource);
/* TS drainSimulationEvents(): valid until the next drain. */
const qa_simulation_event *qa_app_unified_remote_drain_simulation_events(qa_app_unified_remote *remote,
    size_t *out_count);
/* TS drainPresentationEvents() = media.takePresentation(): owned by the media until its next take. */
const qa_app_sim_presentation_event *qa_app_unified_remote_drain_presentation_events(qa_app_unified_remote *remote,
    size_t *out_count);
/* TS get media(): the replica's PresentationState (replaced on each offer). */
struct qa_app_presentation_state *qa_app_unified_remote_media(qa_app_unified_remote *remote);
void qa_app_unified_remote_disconnected(qa_app_unified_remote *remote, const char *reason);
void qa_app_unified_remote_print(qa_app_unified_remote *remote, const char *text);
void qa_app_unified_remote_close(qa_app_unified_remote *remote);
#endif
