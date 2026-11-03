/* Port of src/app/bootstrap/simulation/native-mod-camera.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_mod_camera.h"
#include "qa/app/bootstrap/simulation/classic_guest_player.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_player.h"
#include "qa/foundation/js.h"
/* Public API3/API2023 prediction flags decide whether source or selected movement controls the view. */
qa_app_sim_native_mod_camera_view qa_app_sim_native_mod_camera_classic(qa_q2_player_state state) {
  qa_app_sim_native_mod_camera_view out; out.view = qa_app_sim_classic_guest_player_view(state);
  const qa_q2_movement_state *m = &state.movement; int32_t flags = qa_js_to_int32(m->flags);
  out.native.edition = "classic";
  out.native.movement_origin.x = m->origin_eighths.values[0] / 8; out.native.movement_origin.y = m->origin_eighths.values[1] / 8; out.native.movement_origin.z = m->origin_eighths.values[2] / 8;
  out.native.render_flags = state.view.render_flags;
  out.native.position_prediction = m->type != 4 && (flags & 64) == 0;
  out.native.angular_prediction = m->type < 2;
  out.native.weapon_visible = state.view.gun_index != 0 && state.view.fov <= 90;
  return out;
}
qa_app_sim_native_mod_camera_view qa_app_sim_native_mod_camera_rerelease(qa_q2_rerelease_player_state state) {
  qa_app_sim_native_mod_camera_view out; out.view = qa_app_sim_rerelease_guest_player_view(state);
  const qa_q2_rerelease_movement_state *m = &state.movement; int32_t flags = qa_js_to_int32(m->flags);
  out.native.edition = "rerelease"; out.native.movement_origin = m->origin; out.native.render_flags = state.view.render_flags;
  out.native.position_prediction = m->type != 6 && (flags & 64) == 0;
  out.native.angular_prediction = m->type < 4 && (flags & 256) == 0;
  out.native.weapon_visible = state.view.gun_index != 0;
  return out;
}
qa_app_sim_player_view qa_app_sim_resolve_native_mod_camera(qa_app_sim_native_mod_camera_view view,
    const qa_vec3 *origin, qa_vec3 angles) {
  qa_app_sim_player_view out = view.view;
  if (view.native.position_prediction && origin != NULL) {
    out.origin.x = origin->x + view.view.origin.x - view.native.movement_origin.x;
    out.origin.y = origin->y + view.view.origin.y - view.native.movement_origin.y;
    out.origin.z = origin->z + view.view.origin.z - view.native.movement_origin.z;
  }
  out.angles = view.native.angular_prediction ? angles : view.view.angles;
  return out;
}
