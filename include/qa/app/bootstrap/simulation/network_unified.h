/* Port of src/app/bootstrap/simulation/network-unified.ts.
 *
 * UnifiedApplicationServerHost is the network layer's qa_app_unified_server_host
 * (app/bootstrap/network/unified_server.h); UnifiedApplicationPlayer is its
 * qa_app_unified_player. Presentation events are unified-event-codec qa_save_value
 * trees (TS typed-object shape): an identity node is {slot, generation} carrying its
 * qa_actor_id / qa_client_id as the node attachment, the form the codec reads and
 * writes. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_UNIFIED_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_UNIFIED_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/network.h"
#include "qa/app/bootstrap/network/unified_server.h"

/* TS unifiedPresentationFor(actor, client, value). An identity node without its
 * attachment cannot equal a live identity. A missing identity where TS calls
 * `.equals` raises TypeError, as TS does. */
bool qa_app_sim_unified_presentation_for(qa_actor_id actor, qa_client_id client, const qa_save_value *value);

/* TS unifiedModelPresentations(viewer, models): out[i] is models[i] (the same value)
 * unless it is another actor's view weapon, which becomes its public projection.
 * Pointer members of a projection borrow from models[i]. */
void qa_app_sim_unified_model_presentations(qa_actor_id viewer, const qa_app_simulation_presentation *models,
    size_t count, qa_app_simulation_presentation *out);

/* One SharedSimulation.modPresentationSources() / modClientPresentationSources() row. */
typedef struct qa_app_sim_unified_mod_source {
  qa_presentation_owner owner;
  qa_mod_identity identity; const qa_save_value *identity_wire;
  const char *runtime; /* prepared.declaration.runtime: "qvm-scene"|"qvm-player-events" */
  qa_qvm_abi_profile abi_profile;
  void *ctx;
  void (*assert_current)(void *ctx);
  double (*generation)(void *ctx);
  /* source.context(actor): false is TS null. Fills the context members of `out`
   * (client number, game state, snapshot, weapon presentation). */
  bool (*context)(void *ctx, qa_actor_id actor, qa_app_unified_component_publication *out);
  /* source.scene?.().current: NULL fn is TS undefined scene; false is undefined current. */
  bool (*scene)(void *ctx, const qa_qvm_scene_context **out);
  /* selectComponentScene(scene, viewer, simulation.scene, leaves, print) over this source. */
  const qa_qvm_scene_context *(*select_scene)(void *ctx, const qa_qvm_scene_context *scene, qa_actor_id viewer,
      double leaf_count);
  const qa_qvm_scene_actor *(*bindings)(void *ctx, size_t *out_count);
  bool (*live)(void *ctx, qa_actor_id actor);
  /* NULL is TS undefined clientCommand. */
  void (*client_command)(void *ctx, qa_actor_id actor, const char *const *args, size_t count);
  /* Client presentation sources: source.frame(actor); false is TS null. */
  bool (*frame)(void *ctx, qa_actor_id actor, qa_mod_client_presentation_frame *out);
} qa_app_sim_unified_mod_source;

typedef struct qa_app_sim_unified_host_options {
  qa_engine_session *session;
  const qa_app_sim_network_simulation *simulation;
  const qa_app_sim_network_content *content;
  /* simulation.modPresentationSources() / modClientPresentationSources(); NULL = none. */
  void *mod_ctx;
  const qa_app_sim_unified_mod_source *(*mod_presentation_sources)(void *ctx, size_t *out_count);
  const qa_app_sim_unified_mod_source *(*mod_client_presentation_sources)(void *ctx, size_t *out_count);
  void *print_ctx; void (*print)(void *ctx, const char *text);
} qa_app_sim_unified_host_options;

/* TS createUnifiedApplicationServerHost. host.ctx is owned; free with _destroy.
 * Frames are deep copies the caller frees with qa_app_unified_presentation_frame_free.
 * Returned presentation-event arrays are owned by the host until its next call. */
qa_app_unified_server_host qa_app_sim_create_unified_application_server_host(qa_app_sim_unified_host_options options);
void qa_app_sim_unified_application_server_host_destroy(qa_app_unified_server_host host);
#endif
