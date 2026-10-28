/* Port of src/app/bootstrap/weapon-view.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/weapon_view.h"
#include <string.h>
#include "qa/core/math.h"
qa_app_view_weapon_entity qa_app_anchored_view_weapon(qa_app_view_weapon_entity entity, qa_vec3 anchor, qa_vec3 camera) {
  qa_vec3 delta = qa_sub3(camera, anchor);
  if (delta.x == 0 && delta.y == 0 && delta.z == 0) return entity;
  entity.transform.origin = qa_add3(entity.transform.origin, delta);
  entity.previous_origin = qa_add3(entity.previous_origin, delta);
  entity.lighting_origin = qa_add3(entity.lighting_origin, delta);
  entity.shadow_plane += delta.z;
  return entity;
}
static qa_vec3 rotate_vec(qa_vec3 v, qa_axis anchor, qa_axis camera) {
  return qa_add3(qa_add3(qa_scale3(camera.vectors[0], qa_dot3(v, anchor.vectors[0])),
                         qa_scale3(camera.vectors[1], qa_dot3(v, anchor.vectors[1]))),
                 qa_scale3(camera.vectors[2], qa_dot3(v, anchor.vectors[2])));
}
qa_axis qa_app_anchored_view_weapon_axis(qa_axis axis, qa_axis anchor, qa_axis camera) {
  qa_axis r = {{rotate_vec(axis.vectors[0], anchor, camera), rotate_vec(axis.vectors[1], anchor, camera), rotate_vec(axis.vectors[2], anchor, camera)}};
  return r;
}
qa_vec3 qa_app_weapon_view_origin(qa_app_simulation_presentation_view source) {
  if (source.has_view_weapon && source.family && strcmp(source.family, "q1") == 0) {
    qa_vec3 r = source.origin; r.z += 2; return r;
  }
  return source.origin;
}
qa_scene_camera qa_app_weapon_view_camera(qa_scene_camera camera, const qa_rect *occupied, size_t count) {
  qa_rect area = camera.viewport;
  double bottom = area.y + area.height;
  for (size_t i = 0; i < count; i++) {
    qa_rect rect = occupied[i];
    if (rect.width > 0 && rect.height > 0 && rect.x < area.x + area.width && rect.x + rect.width > area.x
        && rect.y + rect.height > area.y + area.height / 2) {
      double cand = rect.y > area.y ? rect.y : area.y;
      if (cand < bottom) bottom = cand;
    }
  }
  if (bottom == area.y + area.height || bottom <= area.y) return camera;
  double offset = (area.y + area.height - bottom) / area.height;
  double *p = camera.projection.values;
  double np[16] = {p[0], p[1] + offset * p[3], p[2], p[3], p[4], p[5] + offset * p[7], p[6], p[7],
                   p[8], p[9] + offset * p[11], p[10], p[11], p[12], p[13] + offset * p[15], p[14], p[15]};
  memcpy(p, np, sizeof np);
  return camera;
}
