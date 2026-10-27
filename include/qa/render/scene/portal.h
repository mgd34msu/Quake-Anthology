/* Port of src/render/scene/portal.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_PORTAL_H
#define QA_RENDER_SCENE_PORTAL_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/materials/geometry.h"
#include "qa/render/scene/view.h"
typedef struct qa_portal_entity { qa_vec3 origin, old_origin; qa_axis axis; double frame, old_frame, skin_num; } qa_portal_entity;
typedef struct qa_portal_camera { qa_scene_camera camera; qa_vec3 pvs_origin; bool mirror; } qa_portal_camera;
/* model NULL is TS null. False is TS null. */
bool qa_portal_camera_new(qa_plane original, const qa_portal_entity *entities, size_t count, qa_scene_camera view, double milliseconds, const qa_scene_model_transform *model, qa_portal_camera *out);
bool qa_portal_surface_offscreen(qa_material_geometry mesh, qa_scene_camera camera, double range, bool mirror);
#endif
