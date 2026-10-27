/* Port of src/render/scene/flare.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_FLARE_H
#define QA_RENDER_SCENE_FLARE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/flare.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/materials/geometry.h"
bool qa_default_flare_image(const char *path);
typedef struct qa_scene_owned_geometry {
  qa_material_geometry geometry;
  qa_material_vertex *owned_vertices; double *owned_indices;
} qa_scene_owned_geometry;
void qa_scene_owned_geometry_destroy(qa_scene_owned_geometry *geometry);
bool qa_flare_geometry(qa_scene_flare flare, qa_vec3 origin, qa_scene_camera camera, const char *image_path, qa_scene_owned_geometry *out);
typedef struct qa_scene_owned_batch {
  qa_draw_batch batch;
  qa_render_vertex *owned_vertices; double *owned_indices;
} qa_scene_owned_batch;
void qa_scene_owned_batch_destroy(qa_scene_owned_batch *batch);
bool qa_prepare_flare(qa_scene_flare flare, qa_vec3 origin, qa_scene_camera camera, qa_renderer_image image, const char *image_path, qa_scene_owned_batch *out);
#endif
