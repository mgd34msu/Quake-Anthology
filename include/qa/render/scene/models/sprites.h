/* Port of src/render/scene/models/sprites.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_MODELS_SPRITES_H
#define QA_RENDER_SCENE_MODELS_SPRITES_H

#include <stdbool.h>

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/materials/geometry.h"

/* Owned material geometry (vertices/indices malloc'd). */
typedef struct qa_model_owned_geometry {
  qa_material_vertex *vertices;
  size_t vertex_count;
  double *indices;
  size_t index_count;
} qa_model_owned_geometry;
void qa_model_owned_geometry_destroy(qa_model_owned_geometry *geometry);
qa_material_geometry qa_model_owned_geometry_view(
    const qa_model_owned_geometry *geometry);

/* False is TS null (also for orientations outside 0..4, TS undefined). */
bool qa_q1_sprite_axes(const qa_q1_sprite_model *model,
                       qa_model_transform transform, qa_scene_camera camera,
                       double roll, qa_axis *out);
typedef struct qa_sprite_extents {
  double left, right, top, bottom;
} qa_sprite_extents;
qa_model_owned_geometry qa_sprite_quad(qa_vec3 origin, qa_axis axis,
                                       qa_sprite_extents extents, qa_vec4 color,
                                       bool mirror);
qa_model_owned_geometry qa_q1_sprite_geometry(
    const qa_q1_sprite_model *model, qa_sprite_frame frame,
    qa_model_transform transform, qa_scene_camera camera, qa_vec4 color,
    double roll);

#endif
