/*
 * Port of src/world/session/mod-client-presentation.ts (types only).
 * Mirrored from unported modules (named per type): PlayerView (TS
 * owner src/app/bootstrap/simulation/types.ts) and NativeQ2HudFrame
 * (TS owner src/ui/hud/q2-native.ts), with collision-proof
 * qa_session_* names. GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_MOD_CLIENT_PRESENTATION_H
#define QA_WORLD_SESSION_MOD_CLIENT_PRESENTATION_H

#include "qa/ui/hud/q2_native.h"
#include <stdbool.h>
#include <stddef.h>

#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/mods.h"
#include "qa/contracts/presentation.h"
#include "qa/contracts/protocol.h"

/* hud: "none" | "overlay" | "replace" (exact strings). */
typedef struct qa_mod_client_presentation_admission {
  const char *hud;
  bool view;
} qa_mod_client_presentation_admission;

/* TS PlayerView: real owner type (simulation/types.h). */
typedef qa_app_sim_player_view qa_session_player_view;

/* TS NativeQ2HudFrame is the ui/hud/q2-native port (qa_native_q2_hud_frame). */

/** TS NativeModCameraView: a player view plus native fields.
 * edition: "classic" | "rerelease". */
typedef struct qa_native_mod_camera_view {
  qa_session_player_view view;
  const char *edition;
  qa_vec3 movement_origin;
  double render_flags;
  bool position_prediction;
  bool angular_prediction;
  bool weapon_visible;
} qa_native_mod_camera_view;

/** TS ModClientPresentationFrame union on kind. */
typedef enum qa_mod_client_presentation_frame_kind {
  QA_MOD_CLIENT_PRESENTATION_QVM,
  QA_MOD_CLIENT_PRESENTATION_QUAKEC,
  QA_MOD_CLIENT_PRESENTATION_NATIVE
} qa_mod_client_presentation_frame_kind;
typedef struct qa_mod_client_presentation_frame {
  qa_mod_client_presentation_frame_kind kind;
  /* qvm hud mode: "overlay" | "replace-status" (view always null). */
  const char *qvm_hud_mode;
  /* quakec */
  bool has_quakec_hud;
  double quakec_health;
  double quakec_armor;
  bool has_quakec_view;
  qa_session_player_view quakec_view;
  /* native hud mode: "layout-overlay" | "replace-status". */
  bool has_native_hud;
  const char *native_hud_mode;
  qa_native_q2_hud_frame native_hud_frame;
  bool has_native_view;
  qa_native_mod_camera_view native_view;
} qa_mod_client_presentation_frame;

/** TS ModClientPresentationSource. frame false is the TS null. */
typedef struct qa_mod_client_presentation_source {
  void *ctx;
  double generation;
  bool (*frame)(void *ctx, qa_actor_id actor,
                qa_mod_client_presentation_frame *out);
  void (*assert_current)(void *ctx);
} qa_mod_client_presentation_source;

typedef struct qa_active_mod_client_presentation {
  qa_presentation_owner owner;
  qa_mod_identity identity;
  qa_mod_client_presentation_source source;
} qa_active_mod_client_presentation;

#endif
