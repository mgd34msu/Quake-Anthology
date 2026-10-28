/* Port of src/app/bootstrap/simulation/network-unified.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_UNIFIED_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_UNIFIED_H
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/scene.h"
/* UnifiedNetworkPlayerView: SimulationPlayerView plus weapon key and
 * clock seconds, flat over the landed view fields. */
typedef struct qa_app_sim_unified_player_view {
  qa_app_sim_player_view view;
  bool has_weapon; qa_item_id weapon;
  double clock_seconds;
  bool has_health; double health;
} qa_app_sim_unified_player_view;
typedef struct qa_app_sim_unified_participant {
  qa_actor_id actor; qa_client_id client; double seat; const char *role;
} qa_app_sim_unified_participant;
typedef struct qa_app_sim_unified_snapshot {
  const qa_app_simulation_presentation *presentations; size_t presentation_count;
  const qa_app_sim_unified_participant *participants; size_t participant_count;
  const qa_body_state *bodies; size_t body_count;
  /* By-actor lookups (parallel to bodies by actor match in caller). */
  void *ctx;
  bool (*player_view)(void *ctx, qa_actor_id actor, qa_app_sim_unified_player_view *out);
  bool (*body_read)(void *ctx, qa_actor_id actor, qa_body_state *out);
  bool (*combat_health)(void *ctx, qa_actor_id actor, double *out);
} qa_app_sim_unified_snapshot;
typedef struct qa_app_sim_unified_presentation qa_app_sim_unified_presentation;
/* allocations: one content id per presentation is malloc'd; free the
 * returned pointer with qa_app_sim_unified_presentation_for_free. */
qa_app_sim_unified_presentation *qa_app_sim_unified_presentation_for(qa_app_sim_unified_snapshot snapshot, const char *recipient_kind, qa_client_id recipient_client);
void qa_app_sim_unified_presentation_for_free(qa_app_sim_unified_presentation *presentation);
size_t qa_app_sim_unified_presentation_model_count(qa_app_sim_unified_presentation *presentation);
const qa_app_simulation_presentation *qa_app_sim_unified_presentation_models(qa_app_sim_unified_presentation *presentation);
bool qa_app_sim_unified_presentation_view(qa_app_sim_unified_presentation *presentation, qa_app_sim_unified_player_view *out);
void qa_app_sim_unified_presentation_set_view(qa_app_sim_unified_presentation *presentation, qa_app_sim_unified_player_view *value);
typedef struct qa_app_sim_unified_model_presentations {
  qa_app_simulation_presentation *presentations; size_t count;
  qa_content_id *content_ids; /* parallel malloc'd strings */
  char **paths;
} qa_app_sim_unified_model_presentations;
qa_app_sim_unified_model_presentations qa_app_sim_unified_model_presentations_for(qa_app_sim_unified_snapshot snapshot);
void qa_app_sim_unified_model_presentations_free(qa_app_sim_unified_model_presentations *presentations);
/* Malloc'd; caller frees. */
char *qa_app_sim_unified_wire_model_path(const char *family, const char *path);
/* Malloc'd; caller frees. */
char *qa_app_sim_unified_source_model_path(const char *family, const char *path);
#endif
