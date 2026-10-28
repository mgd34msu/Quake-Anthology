/* Port of src/app/bootstrap/network/unified-frame-codec.ts.
 * Simulation/presentation events ride as qa_save_value trees
 * (unified-event-codec); everything else is typed. Read results
 * own their arrays; free with _frame_free. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_FRAME_CODEC_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_FRAME_CODEC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/network/unified_components.h"
#include "qa/app/bootstrap/network/unified_control.h"
#include "qa/world/session/mod_client_presentation.h"
#include "qa/app/bootstrap/network/unified_content.h"
#include "qa/app/bootstrap/network/unified_prediction.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/q3/foundation/presentation.h"
#include "qa/contracts/session.h"
#include "qa/text/world.h"
typedef struct qa_app_unified_identity_encoder {
  void *ctx;
  qa_session_id session;
  bool (*session_matches)(void *ctx, qa_session_id session);
  qa_app_unified_actor_reference (*actor)(void *ctx, qa_actor_id actor);
} qa_app_unified_identity_encoder;
/* TS UnifiedFrameDecoder. */
typedef struct qa_app_unified_frame_decoder {
  void *ctx;
  qa_app_unified_identity_decoder identity;
  qa_resolved_resource_reference (*resource)(void *ctx, const qa_app_unified_resource_key *key);
  qa_decoded_model (*model)(void *ctx, const qa_resolved_resource_reference *reference);
  const qa_scene_snapshot_world *(*world)(void *ctx, const qa_resolved_resource_reference *reference);
} qa_app_unified_frame_decoder;
typedef struct qa_app_unified_native_camera_frame {
  qa_presentation_owner owner; qa_mod_identity identity;
  const qa_save_value *identity_wire; double generation;
  qa_native_mod_camera_view view;
} qa_app_unified_native_camera_frame;
typedef struct qa_app_unified_presentation_frame_t {
  double epoch;
  bool has_components; qa_app_unified_component_frames components;
  bool has_native_camera; qa_app_unified_native_camera_frame native_camera;
  double acknowledged_input;
  qa_app_unified_prediction_projection prediction;
  qa_simulation_output output;
  qa_save_value **events; size_t event_count; /* owned trees */
  qa_app_simulation_presentation *models; size_t model_count;
  qa_q3_character_view *characters; size_t character_count;
  qa_world_text *world_text; size_t world_text_count;
  qa_actor_id player_actor;
  qa_app_sim_player_view player_view;
  qa_app_sim_player_ui player_ui;
} qa_app_unified_presentation_frame_t;
void qa_app_unified_presentation_frame_free(qa_app_unified_presentation_frame_t *frame);
uint8_t *qa_app_unified_encode_frame(const qa_app_unified_presentation_frame_t *frame,
    const qa_app_unified_identity_encoder *identity, size_t *out_length);
qa_app_unified_presentation_frame_t qa_app_unified_decode_frame(
    const uint8_t *bytes, size_t length, const qa_app_unified_frame_decoder *decoder);
#endif
