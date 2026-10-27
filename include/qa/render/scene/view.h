/* Port of src/render/scene/view.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_VIEW_H
#define QA_RENDER_SCENE_VIEW_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
/* TS ModelTransform: scale optional; has_scale false is TS undefined. */
typedef struct qa_scene_model_transform {
  qa_vec3 origin; qa_axis axis; bool has_scale; double scale;
} qa_scene_model_transform;
double qa_scene_model_scale(qa_scene_model_transform model);
qa_vec3 qa_scene_local_vector(qa_vec3 vector, qa_scene_model_transform model);
qa_vec3 qa_scene_local_point(qa_vec3 point, qa_scene_model_transform model);
/* near_plane default in TS is 4 (caller passes 4). Exact Math.fround staging. */
qa_mat4 qa_scene_perspective_projection(double fov_x, double fov_y, double far_plane, double near_plane);
qa_mat4 qa_scene_view_projection_matrix(qa_scene_camera camera);
qa_vec4 qa_scene_project_point(qa_mat4 matrix, qa_vec3 point);
/* TS createViewProjector closure state. */
typedef struct qa_scene_view_projector {
  qa_vec4 eye_x, eye_y, eye_z; qa_mat4 projection;
} qa_scene_view_projector;
qa_scene_view_projector qa_scene_create_view_projector(qa_scene_camera camera);
qa_scene_view_projector qa_scene_create_model_projector(qa_scene_camera camera, qa_scene_model_transform model);
qa_vec4 qa_scene_projector_project(qa_scene_view_projector projector, qa_vec3 point);
/* Writes 4 planes (+1 portal clip). Returns count. */
size_t qa_scene_camera_frustum(qa_scene_camera camera, qa_plane out[5]);
bool qa_scene_bounds_in_frustum(qa_bounds bounds, const qa_plane *planes, size_t count);
double qa_scene_far_clip(qa_vec3 origin, qa_bounds bounds);
/* False is TS null. */
bool qa_scene_portal_clip_plane(qa_scene_camera camera, qa_vec4 *out);
qa_vec3 qa_scene_world_vector(qa_vec3 point, qa_scene_model_transform model);
qa_vec3 qa_scene_world_point(qa_vec3 point, qa_scene_model_transform model);
#endif
