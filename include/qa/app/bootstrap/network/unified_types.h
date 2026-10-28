/* Port of src/app/bootstrap/network/unified-types.ts. Frame payload
 * members whose owners are simulation/content headers ride as
 * borrowed opaque pointers; identity uses landed contracts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_TYPES_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
typedef struct qa_app_unified_identity_decoder {
  void *ctx;
  qa_session_id session;
  qa_actor_id (*actor)(void *ctx, double slot, double generation);
  qa_client_id (*client)(void *ctx, double slot, double generation);
  qa_seat_id (*seat)(void *ctx, double index);
  qa_resource_id (*resource_id)(void *ctx, qa_resource_id id);
} qa_app_unified_identity_decoder;
typedef struct qa_app_unified_native_camera {
  const void *owner;      /* PresentationOwner */
  const void *identity;   /* ModIdentity */
  double generation;
  const void *view;       /* NativeModCameraView */
} qa_app_unified_native_camera;
typedef struct qa_app_unified_presentation_frame {
  double epoch;
  const void *components;      /* UnifiedComponentFrames or NULL */
  bool has_native_camera; qa_app_unified_native_camera native_camera;
  double acknowledged_input;
  const void *prediction;      /* UnifiedPredictionProjection */
  const void *output;          /* SimulationOutput */
  const void *const *models; size_t model_count;
  const void *const *characters; size_t character_count;
  const void *const *world_text; size_t world_text_count;
  qa_actor_id player_actor;
  const void *player_view; const void *player_ui;
} qa_app_unified_presentation_frame;
#endif
