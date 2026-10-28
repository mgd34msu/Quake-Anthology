/* Port of src/app/bootstrap/simulation/native-mod-camera.ts (resolve). GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_mod_camera.h"
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
