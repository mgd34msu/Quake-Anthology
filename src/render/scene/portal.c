/* Port of src/render/scene/portal.ts. GPL-2.0-or-later. */
#include "qa/render/scene/portal.h"
#include <math.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
static float fr(double v) { return (float)v; }
static qa_vec3 transform(qa_vec3 vector, qa_axis surface, qa_axis camera) {
  qa_vec3 result = { 0, 0, 0 };
  for (int i = 0; i < 3; i++)
    result = qa_add3(result, qa_scale3(camera.vectors[i], qa_dot3(vector, surface.vectors[i])));
  return result;
}
bool qa_portal_camera_new(qa_plane original, const qa_portal_entity *entities, size_t count,
                          qa_scene_camera view, double milliseconds,
                          const qa_scene_model_transform *model, qa_portal_camera *out) {
  qa_scene_model_transform identity; memset(&identity, 0, sizeof identity);
  identity.axis.vectors[0] = qa_math_vec3(1, 0, 0); identity.axis.vectors[1] = qa_math_vec3(0, 1, 0); identity.axis.vectors[2] = qa_math_vec3(0, 0, 1); identity.scale = 1;
  qa_scene_model_transform m = model ? *model : identity;
  qa_vec3 normal = model ? qa_normalize3(qa_scene_world_vector(original.normal, m)) : original.normal;
  double distance = model ? qa_dot3(normal, qa_scene_world_point(qa_scale3(original.normal, original.distance), m)) : original.distance;
  const qa_portal_entity *entity = NULL;
  for (size_t i = 0; i < count; i++)
    if (fabs(fr(qa_dot3(entities[i].origin, normal) - distance)) <= 64) { entity = &entities[i]; break; }
  if (!entity) return false;
  qa_vec3 side = qa_perpendicular_vector(normal);
  qa_axis surface; surface.vectors[0] = normal; surface.vectors[1] = side;
  surface.vectors[2] = qa_cross3(normal, side);
  bool mirror = entity->origin.x == entity->old_origin.x && entity->origin.y == entity->old_origin.y && entity->origin.z == entity->old_origin.z;
  qa_vec3 surface_origin, camera_origin; qa_axis camera_axis;
  if (mirror) {
    surface_origin = qa_scale3(normal, distance); camera_origin = surface_origin;
    camera_axis.vectors[0] = qa_scale3(normal, -1);
    camera_axis.vectors[1] = surface.vectors[1]; camera_axis.vectors[2] = surface.vectors[2];
  } else {
    surface_origin = qa_add3(entity->origin, qa_scale3(normal, -fr(qa_dot3(entity->origin, normal) - distance)));
    camera_origin = entity->old_origin;
    qa_vec3 forward = qa_scale3(entity->axis.vectors[0], -1), left = qa_scale3(entity->axis.vectors[1], -1);
    int has_angle = 0; double angle = 0;
    if (entity->old_frame != 0) {
      has_angle = 1;
      angle = entity->frame != 0 ? fr(fr(fr(milliseconds) / 1000) * fr(entity->frame))
        : fr(fr(entity->skin_num) + fr(fr(sin(fr(fr(milliseconds) * fr(0.003)))) * 4));
    } else if (entity->skin_num != 0) { has_angle = 1; angle = fr(entity->skin_num); }
    qa_vec3 rotated = has_angle ? qa_rotate_point_around_vector(forward, left, angle) : left;
    camera_axis.vectors[0] = forward; camera_axis.vectors[1] = rotated;
    camera_axis.vectors[2] = has_angle ? qa_cross3(forward, rotated) : entity->axis.vectors[2];
  }
  qa_vec3 plane_normal = qa_scale3(camera_axis.vectors[0], -1);
  out->camera = view;
  out->camera.origin = qa_add3(transform(qa_sub3(view.origin, surface_origin), surface, camera_axis), camera_origin);
  for (int i = 0; i < 3; i++) out->camera.axis.vectors[i] = transform(view.axis.vectors[i], surface, camera_axis);
  out->camera.clip_is_portal = true; out->camera.clip_mirror = mirror;
  out->camera.clip_plane.normal = plane_normal;
  out->camera.clip_plane.distance = qa_dot3(camera_origin, plane_normal);
  out->pvs_origin = entity->old_origin; out->mirror = mirror;
  return true;
}
bool qa_portal_surface_offscreen(qa_material_geometry mesh, qa_scene_camera camera, double range, bool mirror) {
  qa_scene_view_projector project = qa_scene_create_view_projector(camera);
  int point_and = -1;
  for (size_t i = 0; i < mesh.vertex_count; i++) {
    qa_vec4 clip = qa_scene_projector_project(project, mesh.vertices[i].position);
    double comps[3] = { clip.x, clip.y, clip.z }; int flags = 0;
    for (int k = 0; k < 3; k++) {
      if (comps[k] >= clip.w) flags |= 1 << (k * 2);
      else if (comps[k] <= -clip.w) flags |= 1 << (k * 2 + 1);
    }
    point_and &= flags;
  }
  if (point_and != 0) return true;
  double triangles = (double)mesh.index_count / 3, shortest = 100000000;
  for (size_t i = 0; i < mesh.index_count; i += 3) {
    double vi = mesh.indices[i];
    if (!(vi >= 0 && vi == floor(vi) && vi < (double)mesh.vertex_count))
      qa_range_error("Portal triangle index is outside its vertices");
    size_t idx = (size_t)vi;
    const qa_material_vertex *vertex = &mesh.vertices[idx];
    qa_vec3 relative = qa_sub3(vertex->position, camera.origin);
    shortest = qa_js_min(shortest, qa_dot3(relative, relative)); /* Math.min keeps NaN */
    if (qa_dot3(relative, vertex->normal) >= 0) triangles--;
  }
  return triangles == 0 || (!mirror && shortest > fr(fr(range) * fr(range)));
}
