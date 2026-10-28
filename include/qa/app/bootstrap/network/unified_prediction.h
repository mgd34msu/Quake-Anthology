/* Port of src/app/bootstrap/network/unified-prediction.ts.
 * projectUnifiedPrediction reads through a host mirroring the
 * SharedSimulation surface it uses. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_PREDICTION_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_PREDICTION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/network/unified_types.h"
#include "qa/app/bootstrap/simulation/prediction/types.h"
#include "qa/world/spatial/index.h"
typedef struct qa_app_unified_prediction_projection {
  qa_app_movement_prediction_snapshot snapshot; /* q3Arsenal excluded */
  qa_actor_id actor;
  qa_movement_profile profile;
  qa_bounds standing_bounds;
  double standing_view_height;
  qa_spatial_actor *collisions; size_t collision_count; /* owned after decode/project */
} qa_app_unified_prediction_projection;
uint8_t *qa_app_unified_encode_prediction(const qa_app_unified_prediction_projection *value,
    size_t *out_length);
qa_app_unified_prediction_projection qa_app_unified_decode_prediction(
    const uint8_t *bytes, size_t length, const qa_app_unified_identity_decoder *identity);
void qa_app_unified_prediction_projection_free(qa_app_unified_prediction_projection *projection);
typedef struct qa_app_unified_prediction_player {
  qa_movement_state state;
  qa_arsenal_state arsenal;
  qa_actor_animation_state animation;
  qa_movement_environment environment;
  qa_bounds bounds;
  qa_vec3 view_angles; double view_height;
  qa_trace_hit ground; double water_level, water_type;
  qa_movement_profile profile;
  qa_bounds standing_bounds;
  const char *character_kind; /* "q3" selects the 26-unit view height */
} qa_app_unified_prediction_player;
typedef struct qa_app_unified_prediction_simulation {
  void *ctx;
  bool (*movement_player)(void *ctx, qa_actor_id actor, qa_app_unified_prediction_player *out);
  double (*time_seconds)(void *ctx);
  /* Visit actor observations, then linked spatial actors. */
  void (*observations)(void *ctx, void (*visit)(void *visit_ctx, qa_actor_id actor), void *visit_ctx);
  bool (*linked_actor)(void *ctx, qa_actor_id actor, qa_spatial_actor *out);
} qa_app_unified_prediction_simulation;
qa_app_unified_prediction_projection qa_app_unified_project_prediction(
    const qa_app_unified_prediction_simulation *simulation, qa_actor_id actor,
    double acknowledged_input);
#endif
