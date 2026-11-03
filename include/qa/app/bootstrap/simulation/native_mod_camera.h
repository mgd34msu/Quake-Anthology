/* Port of src/app/bootstrap/simulation/native-mod-camera.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_MOD_CAMERA_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_MOD_CAMERA_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
typedef struct qa_app_sim_native_mod_camera_native {
  const char *edition; qa_vec3 movement_origin; double render_flags;
  bool position_prediction; bool angular_prediction; bool weapon_visible;
} qa_app_sim_native_mod_camera_native;
typedef struct qa_app_sim_native_mod_camera_view {
  qa_app_sim_player_view view; qa_app_sim_native_mod_camera_native native;
} qa_app_sim_native_mod_camera_view;
/* TS nativeModCamera(state) for each player-state kind. */
qa_app_sim_native_mod_camera_view qa_app_sim_native_mod_camera_classic(qa_q2_player_state state);
qa_app_sim_native_mod_camera_view qa_app_sim_native_mod_camera_rerelease(qa_q2_rerelease_player_state state);
qa_app_sim_player_view qa_app_sim_resolve_native_mod_camera(qa_app_sim_native_mod_camera_view view,
    const qa_vec3 *origin, qa_vec3 angles);
#endif
